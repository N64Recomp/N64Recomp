#ifndef __SECTIONS_H__
#define __SECTIONS_H__

#include <stdint.h>

#define ARRLEN(x) (sizeof(x) / sizeof((x)[0]))

typedef struct {
    void* func;
    uint32_t offset;
} FuncEntry;

typedef struct {
    uint32_t rom_addr;
    uint32_t ram_addr;
    uint32_t size;
    FuncEntry *funcs;
    size_t num_funcs;
} SectionTableEntry;

#endif
