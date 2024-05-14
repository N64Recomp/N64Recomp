project(N64Recomp)
cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# fmtlib
add_subdirectory(lib/fmt)

# Rabbitizer
file(GLOB_RECURSE RABBITIZER_SOURCES
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/*.cpp"
    "${CMAKE_SOURCE_DIR}/lib/rabbitizer/src/*.c"
)

add_library(rabbitizer STATIC ${RABBITIZER_SOURCES})

target_include_directories(rabbitizer PRIVATE "${CMAKE_SOURCE_DIR}/lib/rabbitizer/tables")
target_include_directories(rabbitizer PUBLIC
        "${CMAKE_SOURCE_DIR}/lib/rabbitizer/include"
        "${CMAKE_SOURCE_DIR}/lib/rabbitizer/cplusplus/include")

# N64 recompiler
add_executable(N64Recomp)

target_sources(N64Recomp PRIVATE
    ${CMAKE_SOURCE_DIR}/src/analysis.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/main.cpp
    ${CMAKE_SOURCE_DIR}/src/recompilation.cpp)

target_include_directories(N64Recomp PRIVATE
    "${CMAKE_SOURCE_DIR}/lib/ELFIO"
    "${CMAKE_SOURCE_DIR}/lib/toml11"
    "${CMAKE_SOURCE_DIR}/include")

    target_link_libraries(N64Recomp fmt rabbitizer)

# RSP recompiler
add_executable(RSPRecomp)

target_include_directories(RSPRecomp PRIVATE
    "${CMAKE_SOURCE_DIR}/lib/toml11"
    "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(RSPRecomp fmt rabbitizer)

target_sources(RSPRecomp PRIVATE
    ${CMAKE_SOURCE_DIR}/RSPRecomp/src/rsp_recomp.cpp)
