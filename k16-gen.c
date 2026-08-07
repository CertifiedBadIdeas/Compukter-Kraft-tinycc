/*
 * TinyCC K16 code generator
 *
 * Copyright (c) 2026 Compukter Kraft contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifdef TARGET_DEFS_ONLY

#define NB_REGS 12

#define RC_INT (1 << 0)
#define RC_FLOAT 0
#define RC_R(x) (1 << (1 + (x)))

#define RC_IRET RC_R(0)
#define RC_IRE2 RC_R(1)
#define RC_FRET RC_IRET

#define REG_IRET 0
#define REG_IRE2 1
#define REG_FRET REG_IRET

#define PTR_SIZE 4
#define LDOUBLE_SIZE 8
#define LDOUBLE_ALIGN 4
#define MAX_ALIGN 4
#define PROMOTE_RET

#else

#define USING_GLOBALS
#include "tcc.h"

ST_DATA const char * const target_machine_defs =
    "__k16__\0"
    "__K16__\0"
    ;

ST_DATA const int reg_classes[NB_REGS] = {
    RC_INT | RC_R(0), RC_INT | RC_R(1), RC_INT | RC_R(2),
    RC_INT | RC_R(3), RC_INT | RC_R(4), RC_INT | RC_R(5),
    RC_INT | RC_R(6), RC_INT | RC_R(7), RC_INT | RC_R(8),
    RC_INT | RC_R(9), RC_INT | RC_R(10), RC_INT | RC_R(11),
};

static void k16_unimplemented(const char *feature)
{
    tcc_error("K16 TinyCC backend does not support %s yet", feature);
}

ST_FUNC void o(unsigned int word)
{
    unsigned char *p;
    if (nocode_wanted)
        return;
    p = section_ptr_add(cur_text_section, 2);
    write16le(p, word);
    ind += 2;
}

static void k16_emit_u32(uint32_t value)
{
    unsigned char *p;
    if (nocode_wanted)
        return;
    p = section_ptr_add(cur_text_section, 4);
    write32le(p, value);
    ind += 4;
}

static void k16_const32(int reg, uint32_t value)
{
    o(0xe001u | (reg << 8));
    k16_emit_u32(value);
}

ST_FUNC void load(int r, SValue *sv)
{
    int v = sv->r & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;

    if (is_float(bt))
        k16_unimplemented("floating-point values");
    if (sv->r & VT_LVAL)
        k16_unimplemented("memory loads");
    if (v == VT_CONST && !(sv->r & VT_SYM)) {
        k16_const32(r, (uint32_t)sv->c.i);
        return;
    }
    if (v < VT_CONST) {
        if (r != v) {
            k16_const32(11, 0);
            o(0x2000u | (r << 8));
            o((v << 4) | 11);
        }
        return;
    }
    k16_unimplemented("this value load");
}

ST_FUNC void store(int r, SValue *sv)
{
    (void)r;
    (void)sv;
    k16_unimplemented("memory stores");
}

ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret,
                       int *ret_align, int *regsize)
{
    int align;
    int size = type_size(vt, &align);
    (void)variadic;
    ret->t = VT_INT;
    ret->ref = NULL;
    *ret_align = 1;
    *regsize = 4;
    return size <= 4 ? 1 : 0;
}

ST_FUNC void gfunc_call(int nb_args)
{
    (void)nb_args;
    k16_unimplemented("function calls");
}

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    Sym *param = func_sym->type.ref;
    if (func_var)
        k16_unimplemented("variadic functions");
    if (param && param->next)
        k16_unimplemented("function parameters");
    loc = 0;
}

ST_FUNC void gfunc_epilog(void)
{
    o(0x9000);
}

ST_FUNC void gen_fill_nops(int bytes)
{
    if (bytes & 1)
        tcc_error("K16 code alignment must be a multiple of 2 bytes");
    while (bytes > 0) {
        o(0);
        bytes -= 2;
    }
}

ST_FUNC void gsym_addr(int t, int a) { (void)t; (void)a; k16_unimplemented("branches"); }
ST_FUNC int gjmp(int t) { (void)t; k16_unimplemented("branches"); return 0; }
ST_FUNC void gjmp_addr(int a) { (void)a; k16_unimplemented("branches"); }
ST_FUNC int gjmp_cond(int op, int t) { (void)op; (void)t; k16_unimplemented("conditional branches"); return 0; }
ST_FUNC int gjmp_append(int n, int t) { (void)n; (void)t; k16_unimplemented("branches"); return 0; }
ST_FUNC void gen_opi(int op) { (void)op; k16_unimplemented("integer operators"); }
ST_FUNC void gen_opf(int op) { (void)op; k16_unimplemented("floating-point operators"); }
ST_FUNC void gen_cvt_ftoi(int t) { (void)t; k16_unimplemented("floating-point conversions"); }
ST_FUNC void gen_cvt_itof(int t) { (void)t; k16_unimplemented("floating-point conversions"); }
ST_FUNC void gen_cvt_ftof(int t) { (void)t; k16_unimplemented("floating-point conversions"); }
ST_FUNC void ggoto(void) { k16_unimplemented("computed goto"); }
ST_FUNC void gen_vla_sp_save(int addr) { (void)addr; k16_unimplemented("variable-length arrays"); }
ST_FUNC void gen_vla_sp_restore(int addr) { (void)addr; k16_unimplemented("variable-length arrays"); }
ST_FUNC void gen_vla_alloc(CType *type, int align) { (void)type; (void)align; k16_unimplemented("variable-length arrays"); }

#endif
