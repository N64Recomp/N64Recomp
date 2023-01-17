#include "recomp.h"
#include "../portultra/ultra64.h"
#include "../portultra/multilibultra.hpp"

extern "C" void osPfsInitPak_recomp(uint8_t * restrict rdram, recomp_context* restrict ctx) {
	ctx->r2 = 1; // PFS_ERR_NOPACK
}

extern "C" void osPfsFreeBlocks_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
	ctx->r2 = 1; // PFS_ERR_NOPACK
}

extern "C" void osPfsAllocateFile_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
	ctx->r2 = 1; // PFS_ERR_NOPACK
}

extern "C" void osPfsDeleteFile_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
	ctx->r2 = 1; // PFS_ERR_NOPACK
}

extern "C" void osPfsFileState_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
	ctx->r2 = 1; // PFS_ERR_NOPACK
}

extern "C" void osPfsFindFile_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
	ctx->r2 = 1; // PFS_ERR_NOPACK
}

extern "C" void osPfsReadWriteFile_recomp(uint8_t * restrict rdram, recomp_context * restrict ctx) {
	ctx->r2 = 1; // PFS_ERR_NOPACK
}
