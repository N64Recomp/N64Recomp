# N64: Recompiled
N64: Recompiled is a tool to statically recompile N64 binaries into C code that can be compiled for any platform. This can be used for ports or tools as well as for simulating behaviors significantly faster than interpreters or dynamic recompilation can. More widely, it can be used in any context where you want to run some part of an N64 binary in a standalone environment.

This is not the first project that uses static recompilation on game console binaries. A well known example is [jamulator](https://github.com/andrewrk/jamulator), which targets NES binaries. Additionally, this is not even the first project to apply static recompilation to N64-related projects: the [IDO static recompilation](https://github.com/decompals/ido-static-recomp) recompiles the SGI IRIX IDO compiler on modern systems to faciliate matching decompilation of N64 games. This project works similarly to the IDO static recomp project in some ways, and that project was my main inspiration for making this.

## Table of Contents
* [How it Works](#how-it-works)
* [Overlays](#overlays)
* [How to Use](#how-to-use)
* [Single File Output Mode](#single-file-output-mode-for-patches)
* [RSP Microcode Support](#rsp-microcode-support)
* [Planned Features](#planned-features)
* [Building](#building)

## How it Works
The recompiler works by accepting a list of symbols and metadata alongside the binary with the goal of splitting the input binary into functions that are each individually recompiled into a C function, named according to the metadata.

Instructions are processed one-by-one and corresponding C code is emitted as each one gets processed. This translation is very literal in order to keep complexity low. For example, the instruction `addiu $r4, $r4, 0x20`, which adds `0x20` to the 32-bit value in the low bytes of register `$r4` and stores the sign extended 64-bit result in `$r4`, gets recompiled into `ctx->r4 = ADD32(ctx->r4, 0X20);` The `jal` (jump-and-link) instruction is recompiled directly into a function call, and `j` or `b` instructions (unconditional jumps and branches) that can be identified as tail-call optimizations are also recompiled into function calls as well. Branch delay slots are handled by duplicating instructions as necessary. There are other specific behaviors for certain instructions, such as the recompiler attempting to turn a `jr` instruction into a switch-case statement if it can tell that it's being used with a jump table. The recompiler has mostly been tested on binaries built with old MIPS compilers (e.g. mips gcc 2.7.2 and IDO) as well as modern clang targeting mips. Modern mips gcc may trip up the recompiler due to certain optimizations it can do, but those cases can probably be avoided by setting specific compilation flags.

Every output function created by the recompiler is currently emitted into its own file. An option may be provided in the future to group functions together into output files, which should help improve build times of the recompiler output by reducing file I/O in the build process.

Recompiler output can be compiled with any C compiler (tested with msvc, gcc and clang). The output is expected to be used with a runtime that can provide the necessary functionality and macro implementations to run it. A runtime is provided in [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) which can be seen in action in the [Zelda 64: Recompiled](https://github.com/Zelda64Recomp/Zelda64Recomp) project.

## Overlays
Statically linked and relocatable overlays can both be handled by this tool. In both cases, the tool emits function lookups for jump-and-link-register (i.e. function pointers or virtual functions) which the provided runtime can implement using any sort of lookup table. For example, the instruction `jalr $25` would get recompiled as `LOOKUP_FUNC(ctx->r25)(rdram, ctx);` The runtime can then maintain a list of which program sections are loaded and at what address they are at in order to determine which function to run whenever a lookup is triggered during runtime.

For relocatable overlays, the tool will modify supported instructions possessing relocation data (`lui`, `addiu`, load and store instructions) by emitting an extra macro that enables the runtime to relocate the instruction's immediate value field. For example, the instruction `lui $24, 0x80C0` in a section beginning at address `0x80BFA100` with a relocation against a symbol with an address of `0x80BFA730` will get recompiled as `ctx->r24 = S32(RELOC_HI16(1754, 0X630) << 16);`, where 1754 is the index of this section. The runtime can then implement the RELOC_HI16 and RELOC_LO16 macros in order to handle modifying the immediate based on the current loaded address of the section.

Support for relocations for TLB mapping is coming in the future, which will add the ability to provide a list of MIPS32 relocations so that the runtime can relocate them on load. Combining this with the functionality used for relocatable overlays should allow running most TLB mapped code without incurring a performance penalty on every RAM access.

## How to Use
The recompiler is configured by providing a toml file in order to configure the recompiler behavior, which is the only argument provided to the recompiler. The toml is where you specify input and output file paths, as well as optionally stub out specific functions, skip recompilation of specific functions, and patch single instructions in the target binary. There is also planned functionality to be able to emit hooks in the recompiler output by adding them to the toml (the `[[patches.func]]` and `[[patches.hook]]` sections of the linked toml below), but this is currently unimplemented. Documentation on every option that the recompiler provides is not currently available, but an example toml can be found in the Zelda 64: Recompiled project [here](https://github.com/Mr-Wiseguy/Zelda64Recomp/blob/dev/us.rev1.toml).

Currently, the only way to provide the required metadata is by passing an elf file to this tool. The easiest way to get such an elf is to set up a disassembly or decompilation of the target binary, but there will be support for providing the metadata via a custom format to bypass the need to do so in the future.

## Single File Output Mode (for Patches)
This tool can also be configured to recompile in "single file output" mode via an option in the configuration toml. This will emit all of the functions in the provided elf into a single output file. The purpose of this mode is to be able to compile patched versions of functions from the target binary.

This mode can be combined with the functionality provided by almost all linkers (ld, lld, MSVC's link.exe, etc.) to replace functions from the original recompiler output with modified versions. Those linkers only look for symbols in a static library if they weren't already found in a previous input file, so providing the recompiled patches to the linker before providing the original recompiler output will result in the patches taking priority over functions with the same names from the original recompiler output.

This saves a tremendous amount of time while iterating on patches for the target binary, as you can bypass rerunning the recompiler on the target binary as well as compiling the original recompiler output. An example of using this single file output mode for that purpose can be found in the Zelda 64: Recompiled project [here](https://github.com/Mr-Wiseguy/Zelda64Recomp/blob/dev/patches.toml), with the corresponding Makefile that gets used to build the elf for those patches [here](https://github.com/Mr-Wiseguy/Zelda64Recomp/blob/dev/patches/Makefile).

## RSP Microcode Support
RSP microcode can also be recompiled with this tool. Currently there is no support for recompiling RSP overlays, but it may be added in the future if desired. Documentation on how to use this functionality will be coming soon.

## Planned Features
* Custom metadata format to provide symbol names, relocations, and any other necessary data in order to operate without an elf
* Emitting multiple functions per output file to speed up compilation
* Support for recording MIPS32 relocations to allow runtimes to relocate them for TLB mapping
* Ability to recompile into a dynamic language (such as Lua) to be able to load code at runtime for mod support

## Building
This project can be built with CMake 3.20 or above and a C++ compiler that supports C++20. This repo uses git submodules, so be sure to clone recursively (`git clone --recurse-submodules`) or initialize submodules recursively after cloning (`git submodule update --init --recursive`).

From there, building is identical to any other CMake project, e.g: 

1) Configure with `cmake -S . -B build` to create a `build` directory
2) Build with `cmake --build build`

You can also use different generators if you'd like to work in different IDEs.

Xcode:
```bash
# generates into a folder called `build-cmake`
# open the generated Xcode project in that folder
cmake -H . -B build-cmake -G Xcode
```
Visual Studio:
```bash
# generates into a folder called `build-cmake`
# open the generated Visual Studio 2022 solution in that folder
cmake -S . -B build-cmake -G "Visual Studio 17 2022"
```

## Libraries Used
* [rabbitizer](https://github.com/Decompollaborate/rabbitizer) for instruction decoding/analysis
* [ELFIO](https://github.com/serge1/ELFIO) for elf parsing
* [toml11](https://github.com/ToruNiina/toml11) for toml parsing
* [fmtlib](https://github.com/fmtlib/fmt)
