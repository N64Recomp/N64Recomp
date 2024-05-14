#include <cstdio>
#include <iostream>

#define TOML_IMPLEMENTATION
#define TOML_HEADER_ONLY 0
// toml++ header, uncomment to use
#include <toml++/toml.hpp>

// toml11 header, uncomment to use
//#include <toml.hpp>

int main(int argc, char** argv) {
    auto time_start = std::chrono::high_resolution_clock::now();

    // toml11 parsing
//    auto data = toml::parse("test.toml");
    // tomlplusplus parsing
    auto data = toml::parse_file("test.toml");

    std::cout << data << std::endl;

    auto time_end = std::chrono::high_resolution_clock::now();
    auto time_diff = std::chrono::duration_cast<std::chrono::milliseconds>(time_end - time_start);
    printf("Time taken: %lld ms\n", time_diff.count());

    return 0;
}