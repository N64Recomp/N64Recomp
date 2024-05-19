project(N64Recomp)
cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# Rabbitizer
target_sources(rabbitizer PRIVATE
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/analysis/LoPairingInfo.cpp"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/analysis/RegistersTracker.cpp"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstrId.cpp"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstrIdType.cpp"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstructionBase.cpp"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstructionCpu.cpp"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstructionR3000GTE.cpp"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstructionR5900.cpp"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/src/instructions/InstructionRsp.cpp"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/analysis/RabbitizerLoPairingInfo.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/analysis/RabbitizerRegistersTracker.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/analysis/RabbitizerTrackedRegisterState.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/common/RabbitizerConfig.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/common/RabbitizerVersion.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/common/Utils.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstrCategory.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstrDescriptor.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstrId.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstrIdType.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstrSuffix.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionCpu/RabbitizerInstructionCpu_OperandType.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR3000GTE/RabbitizerInstructionR3000GTE.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR3000GTE/RabbitizerInstructionR3000GTE_OperandType.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR3000GTE/RabbitizerInstructionR3000GTE_ProcessUniqueId.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR5900/RabbitizerInstructionR5900.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR5900/RabbitizerInstructionR5900_OperandType.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionR5900/RabbitizerInstructionR5900_ProcessUniqueId.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionRsp/RabbitizerInstructionRsp.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionRsp/RabbitizerInstructionRsp_OperandType.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstructionRsp/RabbitizerInstructionRsp_ProcessUniqueId.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_Disassemble.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_Examination.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_Operand.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerInstruction/RabbitizerInstruction_ProcessUniqueId.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerRegister.c"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/instructions/RabbitizerRegisterDescriptor.c")

target_include_directories(rabbitizer PUBLIC
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/include"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/include")

target_include_directories(rabbitizer PRIVATE
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/tables")

# fmtlib
add_subdirectory(lib/fmt)

# tomlplusplus
set(TOML_ENABLE_FORMATTERS OFF)
add_subdirectory(lib/tomlplusplus)

# N64 recompiler
add_executable(N64Recomp)

target_sources(N64Recomp PRIVATE
    ${CMAKE_SOURCE_DIR}/src/analysis.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/main.cpp
    ${CMAKE_SOURCE_DIR}/src/recompilation.cpp)

target_include_directories(N64Recomp PRIVATE
    "${CMAKE_SOURCE_DIR}/lib/ELFIO"
    "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(N64Recomp fmt rabbitizer tomlplusplus::tomlplusplus)

# RSP recompiler
add_executable(RSPRecomp)

target_include_directories(RSPRecomp PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(RSPRecomp fmt rabbitizer tomlplusplus::tomlplusplus)

target_sources(RSPRecomp PRIVATE
    ${CMAKE_SOURCE_DIR}/RSPRecomp/src/rsp_recomp.cpp)
