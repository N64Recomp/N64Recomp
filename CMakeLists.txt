cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# Rabbitizer
project(rabbitizer)
add_library(rabbitizer STATIC)

target_sources(rabbitizer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/analysis/LoPairingInfo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/analysis/RegistersTracker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstrId.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstrIdType.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstructionBase.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstructionCpu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstructionR3000GTE.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstructionR5900.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstructionRsp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/analysis/RabbitizerLoPairingInfo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/analysis/RabbitizerRegistersTracker.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/analysis/RabbitizerTrackedRegisterState.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/common/RabbitizerConfig.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/common/RabbitizerVersion.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/common/Utils.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstrCategory.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstrDescriptor.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstrId.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstrIdType.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstrSuffix.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionCpu/RabbitizerInstructionCpu_OperandType.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR3000GTE/RabbitizerInstructionR3000GTE.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR3000GTE/RabbitizerInstructionR3000GTE_OperandType.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR3000GTE/RabbitizerInstructionR3000GTE_ProcessUniqueId.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR5900/RabbitizerInstructionR5900.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR5900/RabbitizerInstructionR5900_OperandType.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR5900/RabbitizerInstructionR5900_ProcessUniqueId.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionRsp/RabbitizerInstructionRsp.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionRsp/RabbitizerInstructionRsp_OperandType.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionRsp/RabbitizerInstructionRsp_ProcessUniqueId.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_Disassemble.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_Examination.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_Operand.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_ProcessUniqueId.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerRegister.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerRegisterDescriptor.c")

target_include_directories(rabbitizer PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/cplusplus/include")

target_include_directories(rabbitizer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/rabbitizer/tables")

# fmtlib
add_subdirectory(lib/fmt)

# tomlplusplus
set(TOML_ENABLE_FORMATTERS OFF)
add_subdirectory(lib/tomlplusplus)

# Hardcoded symbol lists (separate library to not force a dependency on N64Recomp)
project(SymbolLists)
add_library(SymbolLists)

target_sources(SymbolLists PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/symbol_lists.cpp
)

target_include_directories(SymbolLists PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# N64 recompiler core library
project(N64Recomp)
add_library(N64Recomp)

target_sources(N64Recomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/operations.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cgenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/luajitgenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recompilation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mod_symbols.cpp
)

target_include_directories(N64Recomp PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(N64Recomp SymbolLists fmt rabbitizer tomlplusplus::tomlplusplus)

# N64 recompiler elf parsing
project(N64RecompElf)
add_library(N64RecompElf)

target_sources(N64RecompElf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/elf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/symbol_lists.cpp
)

target_include_directories(N64RecompElf PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_include_directories(N64RecompElf PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/ELFIO"
)

target_link_libraries(N64RecompElf fmt)

# N64 recompiler executable
project(N64RecompCLI)
add_executable(N64RecompCLI)

target_sources(N64RecompCLI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_include_directories(N64RecompCLI PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(N64RecompCLI fmt rabbitizer tomlplusplus::tomlplusplus N64Recomp N64RecompElf)
set_target_properties(N64RecompCLI PROPERTIES OUTPUT_NAME N64Recomp)

# RSP recompiler
project(RSPRecomp)
add_executable(RSPRecomp)

target_include_directories(RSPRecomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(RSPRecomp fmt rabbitizer tomlplusplus::tomlplusplus)

target_sources(RSPRecomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/RSPRecomp/src/rsp_recomp.cpp)

# Mod tool
project(RecompModTool)
add_executable(RecompModTool)

target_sources(RecompModTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mod_symbols.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RecompModTool/main.cpp
)

target_include_directories(RecompModTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/ELFIO
)

target_link_libraries(RecompModTool fmt tomlplusplus::tomlplusplus N64RecompElf)

# Offline mod recompiler
project(OfflineModRecomp)
add_executable(OfflineModRecomp)

target_sources(OfflineModRecomp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OfflineModRecomp/main.cpp
)

target_link_libraries(OfflineModRecomp fmt rabbitizer tomlplusplus::tomlplusplus N64Recomp)
