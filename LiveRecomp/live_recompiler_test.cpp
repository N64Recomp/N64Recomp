#include <fstream>
#include <chrono>
#include <filesystem>
#include <cinttypes>

#include "sljitLir.h"
#include "recompiler/live_recompiler.h"
#include "recomp.h"

static std::vector<uint8_t> read_file(const std::filesystem::path& path, bool& found) {
    std::vector<uint8_t> ret;
    found = false;

    std::ifstream file{ path, std::ios::binary};

    if (file.good()) {
        file.seekg(0, std::ios::end);
        ret.resize(file.tellg());
        file.seekg(0, std::ios::beg);

        file.read(reinterpret_cast<char*>(ret.data()), ret.size());
        found = true;
    }

    return ret;
}


uint32_t read_u32_swap(const std::vector<uint8_t>& vec, size_t offset) {
    return byteswap(*reinterpret_cast<const uint32_t*>(&vec[offset]));
}

uint32_t read_u32(const std::vector<uint8_t>& vec, size_t offset) {
    return *reinterpret_cast<const uint32_t*>(&vec[offset]);
}

std::vector<uint8_t> rdram;

void byteswap_copy(uint8_t* dst, uint8_t* src, size_t count) {
    for (size_t i = 0; i < count; i++) {
        dst[i ^ 3] = src[i];
    }
}

bool byteswap_compare(uint8_t* a, uint8_t* b, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (a[i ^ 3] != b[i]) {
            return false;
        }
    }
    return true;
}

enum class TestError {
    Success,
    FailedToOpenInput,
    FailedToRecompile,
    UnknownStructType,
    DataDifference
};

struct TestStats {
    TestError error;
    uint64_t codegen_microseconds;
    uint64_t execution_microseconds;
    uint64_t code_size;
};

void write1(uint8_t* rdram, recomp_context* ctx) {
    MEM_B(0, ctx->r4) = 1;
}

recomp_func_t* test_get_function(int32_t vram) {
    if (vram == 0x80100000) {
        return write1;
    }
    assert(false);
    return nullptr;
}

TestStats run_test(const std::filesystem::path& tests_dir, const std::string& test_name) {
    std::filesystem::path input_path = tests_dir / (test_name + "_data.bin");
    std::filesystem::path data_dump_path = tests_dir / (test_name + "_data_out.bin");

    bool found;
    std::vector<uint8_t> file_data = read_file(input_path, found);

    if (!found) {
        printf("Failed to open file: %s\n", input_path.string().c_str());
        return { TestError::FailedToOpenInput };
    }

    // Parse the test file.
    uint32_t text_offset = read_u32_swap(file_data, 0x00);
    uint32_t text_length = read_u32_swap(file_data, 0x04);
    uint32_t init_data_offset = read_u32_swap(file_data, 0x08);
    uint32_t good_data_offset = read_u32_swap(file_data, 0x0C);
    uint32_t data_length = read_u32_swap(file_data, 0x10);
    uint32_t text_address = read_u32_swap(file_data, 0x14);
    uint32_t data_address = read_u32_swap(file_data, 0x18);
    uint32_t next_struct_address = read_u32_swap(file_data, 0x1C);

    recomp_context ctx{};

    byteswap_copy(&rdram[text_address - 0x80000000], &file_data[text_offset], text_length);
    byteswap_copy(&rdram[data_address - 0x80000000], &file_data[init_data_offset], data_length);

    // Build recompiler context.
    N64Recomp::Context context{};

    // Move the file data into the context.
    context.rom = std::move(file_data);

    // Create a section for the function to exist in.
    context.sections.resize(1);
    context.sections[0].ram_addr = text_address;
    context.sections[0].rom_addr = text_offset;
    context.sections[0].size = text_length;
    context.sections[0].name = "test_section";
    context.sections[0].executable = true;
    context.section_functions.resize(context.sections.size());

    size_t start_func_index;
    uint32_t function_desc_address = 0;

    // Read any extra structs.
    while (next_struct_address != 0) {
        uint32_t cur_struct_address = next_struct_address;
        uint32_t struct_type = read_u32_swap(context.rom, next_struct_address + 0x00);
        next_struct_address = read_u32_swap(context.rom, next_struct_address + 0x04);

        switch (struct_type) {
            case 1: // Function desc
                function_desc_address = cur_struct_address;
                break;
            default:
                printf("Unknown struct type %u\n", struct_type);
                return { TestError::UnknownStructType };
        }
    }

    // Check if a function description exists.
    if (function_desc_address == 0) {
        // No function description, so treat the whole thing as one function.

        // Get the function's instruction words.
        std::vector<uint32_t> text_words{};
        text_words.resize(text_length / sizeof(uint32_t));
        for (size_t i = 0; i < text_words.size(); i++) {
            text_words[i] = read_u32(context.rom, text_offset + i * sizeof(uint32_t));
        }

        // Add the function to the context.
        context.functions_by_vram[text_address].emplace_back(context.functions.size());
        context.section_functions.emplace_back(context.functions.size());
        context.sections[0].function_addrs.emplace_back(text_address);
        context.functions.emplace_back(
            text_address,
            text_offset,
            text_words,
            "test_func",
            0
        );
        start_func_index = 0;
    }
    else {
        // Use the function description.
        uint32_t num_funcs = read_u32_swap(context.rom, function_desc_address + 0x08);
        start_func_index = read_u32_swap(context.rom, function_desc_address + 0x0C);

        for (size_t func_index = 0; func_index < num_funcs; func_index++) {
            uint32_t cur_func_address = read_u32_swap(context.rom, function_desc_address + 0x10 + 0x00 + 0x08 * func_index);
            uint32_t cur_func_length = read_u32_swap(context.rom, function_desc_address + 0x10 + 0x04 + 0x08 * func_index);
            uint32_t cur_func_offset = cur_func_address - text_address + text_offset;

            // Get the function's instruction words.
            std::vector<uint32_t> text_words{};
            text_words.resize(cur_func_length / sizeof(uint32_t));
            for (size_t i = 0; i < text_words.size(); i++) {
                text_words[i] = read_u32(context.rom, cur_func_offset + i * sizeof(uint32_t));
            }

            // Add the function to the context.
            context.functions_by_vram[cur_func_address].emplace_back(context.functions.size());
            context.section_functions.emplace_back(context.functions.size());
            context.sections[0].function_addrs.emplace_back(cur_func_address);
            context.functions.emplace_back(
                cur_func_address,
                cur_func_offset,
                std::move(text_words),
                "test_func_" + std::to_string(func_index),
                0
            );
        }
    }

    std::vector<std::vector<uint32_t>> dummy_static_funcs{};

    auto before_codegen = std::chrono::system_clock::now();

    N64Recomp::LiveGeneratorInputs generator_inputs {
        .get_function = test_get_function,
    };

    // Create the sljit compiler and the generator.
    N64Recomp::LiveGenerator generator{ context.functions.size(), generator_inputs };

    for (size_t func_index = 0; func_index < context.functions.size(); func_index++) {
        std::ostringstream dummy_ostream{};

        //sljit_emit_op0(compiler, SLJIT_BREAKPOINT);

        if (!N64Recomp::recompile_function_live(generator, context, func_index, dummy_ostream, dummy_static_funcs, true)) {
            return { TestError::FailedToRecompile };
        }
    }

    // Generate the code.
    N64Recomp::LiveGeneratorOutput output = generator.finish();

    auto after_codegen = std::chrono::system_clock::now();

    auto before_execution = std::chrono::system_clock::now();

    // Run the generated code.
    ctx.r29 = 0xFFFFFFFF80000000 + rdram.size() - 0x10; // Set the stack pointer.
    output.functions[start_func_index](rdram.data(), &ctx);

    auto after_execution = std::chrono::system_clock::now();

    // Check the result of running the code.
    bool good = byteswap_compare(&rdram[data_address - 0x80000000], &context.rom[good_data_offset], data_length);

    // Dump the data if the results don't match.
    if (!good) {
        std::ofstream data_dump_file{ data_dump_path, std::ios::binary };
        std::vector<uint8_t> data_swapped;
        data_swapped.resize(data_length);
        byteswap_copy(data_swapped.data(), &rdram[data_address - 0x80000000], data_length);
        data_dump_file.write(reinterpret_cast<char*>(data_swapped.data()), data_length);
        return { TestError::DataDifference };
    }

    // Return the test's stats.
    TestStats ret{};
    ret.error = TestError::Success;
    ret.codegen_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(after_codegen - before_codegen).count();
    ret.execution_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(after_execution - before_execution).count();
    ret.code_size = output.code_size;

    return ret;
}

int main(int argc, const char** argv) {
    if (argc < 3) {
        printf("Usage: %s [test directory] [test 1] ...\n", argv[0]);
        return EXIT_SUCCESS;
    }

    N64Recomp::live_recompiler_init();

    rdram.resize(0x8000000);

    // Skip the first argument (program name) and second argument (test directory).
    int count = argc - 1 - 1;
    int passed_count = 0;

    std::vector<size_t> failed_tests{};

    for (size_t test_index = 0; test_index < count; test_index++) {
        const char* cur_test_name = argv[2 + test_index];
        printf("Running test: %s\n", cur_test_name);
        TestStats stats = run_test(argv[1], cur_test_name);

        switch (stats.error) {
        case TestError::Success:
            printf("  Success\n");
            printf("  Generated %" PRIu64 " bytes in %" PRIu64 " microseconds and ran in %" PRIu64 " microseconds\n",
                stats.code_size, stats.codegen_microseconds, stats.execution_microseconds);
            passed_count++;
            break;
        case TestError::FailedToOpenInput:
            printf("  Failed to open input data file\n");
            break;
        case TestError::FailedToRecompile:
            printf("  Failed to recompile\n");
            break;
        case TestError::UnknownStructType:
            printf("  Unknown additional data struct type in test data\n");
            break;
        case TestError::DataDifference:
            printf("  Output data did not match, dumped to file\n");
            break;
        }

        if (stats.error != TestError::Success) {
            failed_tests.emplace_back(test_index);
        }

        printf("\n");
    }

    printf("Passed %d/%d tests\n", passed_count, count);
    if (!failed_tests.empty()) {
        printf("  Failed: ");
        for (size_t i = 0; i < failed_tests.size(); i++) {
            size_t test_index = failed_tests[i];

            printf("%s", argv[2 + test_index]);
            if (i != failed_tests.size() - 1) {
                printf(", ");
            }
        }
        printf("\n");
    }
    return 0;
}
