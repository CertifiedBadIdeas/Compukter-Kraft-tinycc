/*
 * TinyCC K16 ELF relocation definitions
 *
 * Copyright (c) 2026 Compukter Kraft contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifdef TARGET_DEFS_ONLY

#define EM_TCC_TARGET EM_K16
#define R_DATA_32 R_K16_ABS32
#define R_DATA_PTR R_K16_ABS32
#define R_JMP_SLOT R_K16_NONE
#define R_GLOB_DAT R_K16_NONE
#define R_COPY R_K16_NONE
#define R_RELATIVE R_K16_NONE
#define R_NUM R_K16_NUM
#define ELF_START_ADDR 0
#define ELF_PAGE_SIZE 0x1000
#define PCRELATIVE_DLLPLT 0
#define RELOCATE_DLLPLT 0

#else

#include "tcc.h"

ST_FUNC int code_reloc(int reloc_type)
{
    switch (reloc_type) {
    case R_K16_CALL32:
    case R_K16_BRANCH4:
        return 1;
    case R_K16_ABS32:
        return 0;
    default:
        return -1;
    }
}

ST_FUNC int gotplt_entry_type(int reloc_type)
{
    (void)reloc_type;
    return NO_GOTPLT_ENTRY;
}

ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset,
                                  struct sym_attr *attr)
{
    (void)s1;
    (void)got_offset;
    (void)attr;
    tcc_error_noabort("K16 TinyCC final linking is unsupported; use k16 link");
    return 0;
}

ST_FUNC void relocate_plt(TCCState *s1)
{
    (void)s1;
}

ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type,
                      unsigned char *ptr, addr_t addr, addr_t val)
{
    (void)s1;
    (void)rel;
    (void)type;
    (void)ptr;
    (void)addr;
    (void)val;
    tcc_error_noabort("K16 TinyCC final linking is unsupported; use k16 link");
}

#endif
