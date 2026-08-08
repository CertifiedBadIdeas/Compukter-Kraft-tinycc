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
#define RC_FLOAT RC_INT
#define RC_R(x) (1 << (1 + (x)))

#define RC_IRET RC_R(0)
#define RC_IRE2 RC_R(1)
#define RC_FRET RC_IRET

#define REG_IRET 0
#define REG_IRE2 1
#define REG_FRET REG_IRET

#define PTR_SIZE 4
#define LDOUBLE_SIZE 8
#define LDOUBLE_ALIGN 8
#define MAX_ALIGN 8
#define PROMOTE_RET

#else

#define USING_GLOBALS
#include "tcc.h"
#include <assert.h>

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

#define K16_FP 12
#define K16_SCRATCH0 13
#define K16_SCRATCH1 14
#define K16_SP 15

static int func_prolog_offset;
static int func_varargs_offset;

static void k16_assert_reg(int reg)
{
    assert(reg >= 0 && reg < 16);
}

static void k16_assert_i16(int value)
{
    assert(value >= -32768 && value <= 32767);
}

static void k16_unimplemented(const char *feature)
{
    tcc_error("K16 TinyCC backend does not support %s yet", feature);
}

static void k16_reject_float(void)
{
    tcc_error("K16 TinyCC does not support floating-point code yet");
}

static void k16_reject_aggregate(void)
{
    tcc_error("K16 TinyCC does not support aggregate arguments or returns yet");
}

typedef struct K16ArgumentClass {
    int indirect;
    int size;
    int align;
    int slots;
    int object_size;
    int object_align;
} K16ArgumentClass;

typedef struct K16CallArgument {
    K16ArgumentClass classification;
    int fixed_slot;
    int stack_passed;
    int stack_offset;
    int copy_offset;
} K16CallArgument;

static int k16_align_up(int value, int alignment)
{
    return (value + alignment - 1) & -alignment;
}

static K16ArgumentClass k16_classify_argument(CType *type)
{
    K16ArgumentClass result;
    int bt = type->t & VT_BTYPE;

    result.indirect = bt == VT_STRUCT;
    result.object_size = type_size(type, &result.object_align);
    if (result.indirect) {
        if (result.object_size <= 0)
            tcc_error("K16 TinyCC does not support empty aggregate arguments");
        if (result.object_align > MAX_ALIGN)
            tcc_error("K16 TinyCC arguments cannot require alignment above 8 bytes");
        result.size = PTR_SIZE;
        result.align = PTR_SIZE;
        result.slots = 1;
        return result;
    }
    result.size = result.object_size;
    result.align = result.object_align;
    if (result.size <= 0 || result.size > 8)
        tcc_error("K16 TinyCC cannot classify this direct argument type");
    if (result.align > MAX_ALIGN)
        tcc_error("K16 TinyCC arguments cannot require alignment above 8 bytes");
    result.slots = (result.size + 3) / 4;
    return result;
}

static K16ArgumentClass k16_classify_call_argument(CType *type, int variadic)
{
    int bt = type->t & VT_BTYPE;

    if (variadic &&
        (bt == VT_BOOL || bt == VT_BYTE || bt == VT_SHORT)) {
        CType promoted = *type;

        promoted.t = (promoted.t & ~(VT_BTYPE | VT_LONG | VT_UNSIGNED)) |
                     VT_INT;
        return k16_classify_argument(&promoted);
    }
    return k16_classify_argument(type);
}

static int k16_fixed_parameter_count(SValue *func, int nb_args)
{
    Sym *parameter;
    int count = 0;

    if (!func->type.ref || func->type.ref->f.func_type != FUNC_ELLIPSIS)
        return nb_args;
    parameter = func->type.ref;
    while ((parameter = parameter->next) != NULL)
        ++count;
    return count;
}

ST_FUNC void o(unsigned int word)
{
    int next = ind + 2;
    if (nocode_wanted)
        return;
    if (next > cur_text_section->data_allocated)
        section_realloc(cur_text_section, next);
    write16le(cur_text_section->data + ind, word);
    ind = next;
}

static void k16_emit_u32(uint32_t value)
{
    int next = ind + 4;
    if (nocode_wanted)
        return;
    if (next > cur_text_section->data_allocated)
        section_realloc(cur_text_section, next);
    write32le(cur_text_section->data + ind, value);
    ind = next;
}

static void k16_const32(int reg, uint32_t value)
{
    k16_assert_reg(reg);
    o(0xe001u | (reg << 8));
    k16_emit_u32(value);
}

static void k16_const32_sym(int reg, Sym *sym, int addend, int reloc)
{
    k16_assert_reg(reg);
    o(0xe001u | (reg << 8));
    greloca(cur_text_section, sym, ind, reloc, addend);
    k16_emit_u32(0);
}

static void k16_rrr(unsigned int opcode, int dst, int lhs, int rhs)
{
    k16_assert_reg(dst);
    k16_assert_reg(lhs);
    k16_assert_reg(rhs);
    o(opcode | (dst << 8));
    o((lhs << 4) | rhs);
}

static void k16_add(int dst, int lhs, int rhs) { k16_rrr(0x2000, dst, lhs, rhs); }
static void k16_sub(int dst, int lhs, int rhs) { k16_rrr(0x2001, dst, lhs, rhs); }
static void k16_and(int dst, int lhs, int rhs) { k16_rrr(0x2002, dst, lhs, rhs); }
static void k16_or(int dst, int lhs, int rhs) { k16_rrr(0x2003, dst, lhs, rhs); }
static void k16_xor(int dst, int lhs, int rhs) { k16_rrr(0x2004, dst, lhs, rhs); }
static void k16_shl(int dst, int lhs, int rhs) { k16_rrr(0x2005, dst, lhs, rhs); }
static void k16_shr(int dst, int lhs, int rhs) { k16_rrr(0x2006, dst, lhs, rhs); }
static void k16_sar(int dst, int lhs, int rhs) { k16_rrr(0x2007, dst, lhs, rhs); }
static void k16_eq(int dst, int lhs, int rhs) { k16_rrr(0x2008, dst, lhs, rhs); }
static void k16_ne(int dst, int lhs, int rhs) { k16_rrr(0x2009, dst, lhs, rhs); }
static void k16_ltu(int dst, int lhs, int rhs) { k16_rrr(0x200a, dst, lhs, rhs); }
static void k16_lts(int dst, int lhs, int rhs) { k16_rrr(0x200b, dst, lhs, rhs); }
static void k16_mul(int dst, int lhs, int rhs) { k16_rrr(0x200c, dst, lhs, rhs); }

static void k16_addi(int dst, int src, int offset)
{
    k16_assert_reg(dst);
    k16_assert_reg(src);
    k16_assert_i16(offset);
    o(0x3002u | (dst << 8) | (src << 4));
    o((uint16_t)offset);
}

static void k16_load_offset(int size, int dst, int base, int offset)
{
    unsigned int opcode;
    k16_assert_reg(dst);
    k16_assert_reg(base);
    k16_assert_i16(offset);
    opcode = size == 1 ? 0x3003u : size == 2 ? 0x3004u : 0x3005u;
    o(opcode | (dst << 8) | (base << 4));
    o((uint16_t)offset);
}

static void k16_store_offset(int size, int base, int src, int offset)
{
    unsigned int opcode;
    k16_assert_reg(base);
    k16_assert_reg(src);
    k16_assert_i16(offset);
    opcode = size == 1 ? 0x3006u : size == 2 ? 0x3007u : 0x3008u;
    o(opcode | (base << 8) | (src << 4));
    o((uint16_t)offset);
}

static void k16_move(int dst, int src)
{
    if (dst == src)
        return;
    k16_addi(dst, src, 0);
}

static void k16_adjust_sp(int amount)
{
    if (amount >= -32768 && amount <= 32767) {
        k16_addi(K16_SP, K16_SP, amount);
    } else {
        k16_const32(K16_SCRATCH0, (uint32_t)(amount < 0 ? -amount : amount));
        if (amount < 0)
            k16_sub(K16_SP, K16_SP, K16_SCRATCH0);
        else
            k16_add(K16_SP, K16_SP, K16_SCRATCH0);
    }
}

static void k16_base_offset(int *base, int *offset, int scratch)
{
    int other = scratch == K16_SCRATCH0 ? K16_SCRATCH1 : K16_SCRATCH0;
    if (*offset >= -32768 && *offset <= 32767)
        return;
    k16_const32(other, (uint32_t)*offset);
    k16_add(scratch, *base, other);
    *base = scratch;
    *offset = 0;
}

static int k16_value_address(SValue *sv, int scratch, int *offset)
{
    int v = sv->r & VT_VALMASK;
    *offset = sv->c.i;
    if (sv->r & VT_SYM) {
        k16_const32_sym(scratch, sv->sym, *offset, R_K16_ABS32);
        *offset = 0;
        return scratch;
    }
    if (v == VT_LOCAL)
        return K16_FP;
    if (v == VT_LLOCAL) {
        k16_load_offset(4, scratch, K16_FP, *offset);
        *offset = 0;
        return scratch;
    }
    if (v < VT_CONST) {
        *offset = 0;
        return v;
    }
    if (v == VT_CONST) {
        k16_const32(scratch, (uint32_t)*offset);
        *offset = 0;
        return scratch;
    }
    k16_unimplemented("this address form");
    return scratch;
}

static void k16_materialize_compare(int dst, int op, int lhs, int rhs)
{
    int invert = 0;
    switch (op) {
    case TOK_EQ: k16_eq(dst, lhs, rhs); return;
    case TOK_NE: k16_ne(dst, lhs, rhs); return;
    case TOK_ULT: k16_ltu(dst, lhs, rhs); return;
    case TOK_UGE: invert = 1; k16_ltu(dst, lhs, rhs); break;
    case TOK_ULE: invert = 1; k16_ltu(dst, rhs, lhs); break;
    case TOK_UGT: k16_ltu(dst, rhs, lhs); return;
    case TOK_LT: k16_lts(dst, lhs, rhs); return;
    case TOK_GE: invert = 1; k16_lts(dst, lhs, rhs); break;
    case TOK_LE: invert = 1; k16_lts(dst, rhs, lhs); break;
    case TOK_GT: k16_lts(dst, rhs, lhs); return;
    default: k16_unimplemented("this comparison"); return;
    }
    if (invert) {
        k16_const32(K16_SCRATCH1, 1);
        k16_xor(dst, dst, K16_SCRATCH1);
    }
}

ST_FUNC void load(int r, SValue *sv)
{
    int v = sv->r & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;
    int align, size, base, offset;

    if (bt == VT_STRUCT)
        k16_reject_aggregate();
    if (sv->r & VT_LVAL) {
        size = type_size(&sv->type, &align);
        if (bt == VT_PTR || bt == VT_FUNC)
            size = PTR_SIZE;
        if (size != 1 && size != 2 && size != 4)
            k16_unimplemented("this memory load width");
        base = k16_value_address(sv, K16_SCRATCH0, &offset);
        k16_base_offset(&base, &offset, K16_SCRATCH0);
        k16_load_offset(size, r, base, offset);
        if (size < 4 && !(sv->type.t & VT_UNSIGNED)) {
            k16_const32(K16_SCRATCH0, size == 1 ? 24 : 16);
            k16_shl(r, r, K16_SCRATCH0);
            k16_sar(r, r, K16_SCRATCH0);
        }
        return;
    }
    if (v == VT_CONST && !(sv->r & VT_SYM)) {
        k16_const32(r, (uint32_t)sv->c.i);
        return;
    }
    if (v == VT_CONST && (sv->r & VT_SYM)) {
        k16_const32_sym(r, sv->sym, sv->c.i, R_K16_ABS32);
        return;
    }
    if (v == VT_LOCAL) {
        offset = sv->c.i;
        if (offset >= -32768 && offset <= 32767)
            k16_addi(r, K16_FP, offset);
        else {
            k16_const32(K16_SCRATCH0, (uint32_t)offset);
            k16_add(r, K16_FP, K16_SCRATCH0);
        }
        return;
    }
    if (v < VT_CONST) {
        k16_move(r, v);
        return;
    }
    if (v == VT_CMP) {
        k16_materialize_compare(r, sv->cmp_op,
                                sv->cmp_r & 0xff, sv->cmp_r >> 8);
        return;
    }
    if ((v & ~1) == VT_JMP) {
        int t = v & 1;
        k16_const32(r, t);
        gjmp_addr(ind + 14);
        gsym(sv->c.i);
        k16_const32(r, t ^ 1);
        return;
    }
    k16_unimplemented("this value load");
}

ST_FUNC void store(int r, SValue *sv)
{
    int bt = sv->type.t & VT_BTYPE;
    int align, size, base, offset;
    if (bt == VT_STRUCT)
        k16_reject_aggregate();
    size = type_size(&sv->type, &align);
    if (bt == VT_PTR || bt == VT_FUNC)
        size = PTR_SIZE;
    if (size != 1 && size != 2 && size != 4)
        k16_unimplemented("this memory store width");
    base = k16_value_address(sv, K16_SCRATCH0, &offset);
    k16_base_offset(&base, &offset, K16_SCRATCH0);
    k16_store_offset(size, base, r, offset);
}

ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret,
                       int *ret_align, int *regsize)
{
    int bt = vt->t & VT_BTYPE;
    (void)variadic;
    if (is_float(bt))
        k16_reject_float();
    if (bt == VT_STRUCT)
        k16_reject_aggregate();
    ret->t = VT_INT;
    ret->ref = NULL;
    *ret_align = 1;
    *regsize = 4;
    return 1;
}

static void k16_copy_call_argument(SValue *argument, int destination,
                                   int size)
{
    int source_offset, source_base, copied = 0;

    if (!(argument->r & VT_LVAL))
        tcc_error("K16 TinyCC aggregate argument is not addressable");
    source_base = k16_value_address(argument, K16_SCRATCH0, &source_offset);
    while (copied < size) {
        int base = source_base;
        int offset = source_offset + copied;
        int remaining = size - copied;
        int width = remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;

        k16_base_offset(&base, &offset, K16_SCRATCH0);
        k16_load_offset(width, K16_SCRATCH1, base, offset);
        k16_store_offset(width, K16_SP, K16_SCRATCH1,
                         destination + copied);
        copied += width;
    }
}

ST_FUNC void gfunc_call(int nb_args)
{
    SValue *func = &vtop[-nb_args];
    K16CallArgument *arguments;
    int fixed_count = k16_fixed_parameter_count(func, nb_args);
    int fixed_slots = 0;
    int fixed_stack_size;
    int next_vararg_offset;
    int logical_size;
    int outgoing_size;
    int i, part, r;

    arguments = tcc_mallocz(sizeof(*arguments) * nb_args);
    for (i = 0; i < nb_args; ++i) {
        SValue *arg = &vtop[-nb_args + 1 + i];
        K16CallArgument *argument = &arguments[i];

        argument->classification =
            k16_classify_call_argument(&arg->type, i >= fixed_count);
        argument->fixed_slot = -1;
        argument->stack_passed = 0;
        argument->stack_offset = -1;
        argument->copy_offset = -1;
        if (i < fixed_count) {
            argument->fixed_slot = fixed_slots;
            fixed_slots += argument->classification.slots;
        }
    }

    fixed_stack_size = fixed_slots > 3 ? (fixed_slots - 3) * 4 : 0;
    next_vararg_offset = 4 + fixed_stack_size;
    for (i = 0; i < nb_args; ++i) {
        K16CallArgument *argument = &arguments[i];

        if (argument->fixed_slot >= 0) {
            if (argument->fixed_slot + argument->classification.slots > 3) {
                argument->stack_passed = 1;
                argument->stack_offset = (argument->fixed_slot - 3) * 4;
            }
            continue;
        }
        next_vararg_offset =
            k16_align_up(next_vararg_offset, argument->classification.align);
        argument->stack_offset = next_vararg_offset - 4;
        argument->stack_passed = 1;
        next_vararg_offset += argument->classification.size;
    }
    logical_size = next_vararg_offset - 4;
    for (i = 0; i < nb_args; ++i) {
        K16CallArgument *argument = &arguments[i];
        K16ArgumentClass *classification = &argument->classification;
        int copy_callee_offset;

        if (!classification->indirect)
            continue;
        copy_callee_offset =
            k16_align_up(4 + logical_size, classification->object_align);
        argument->copy_offset = copy_callee_offset - 4;
        logical_size = argument->copy_offset + classification->object_size;
    }
    outgoing_size = k16_align_up(logical_size, 8) + 4;
    if (outgoing_size > 32767)
        tcc_error("K16 TinyCC call frame exceeds the supported 32767 bytes");

    save_regs(0);
    k16_adjust_sp(-outgoing_size);

    for (i = 0; i < nb_args; ++i) {
        SValue *arg = &vtop[-nb_args + 1 + i];
        K16CallArgument *argument = &arguments[i];

        if (!argument->classification.indirect)
            continue;
        k16_copy_call_argument(arg, argument->copy_offset,
                               argument->classification.object_size);
    }

    for (i = 0; i < nb_args; ++i) {
        SValue *arg = &vtop[-nb_args + 1 + i];
        K16CallArgument *argument = &arguments[i];

        if (!argument->stack_passed)
            continue;
        if (argument->classification.indirect) {
            k16_addi(K16_SCRATCH0, K16_SP, argument->copy_offset);
            k16_store_offset(4, K16_SP, K16_SCRATCH0,
                             argument->stack_offset);
            continue;
        }
        vpushv(arg);
        gv(RC_INT);
        for (part = 0; part < argument->classification.slots; ++part) {
            int destination = argument->stack_offset + part * 4;

            if (destination < 0)
                continue;
            r = part == 0 ? vtop->r & VT_VALMASK : vtop->r2;
            k16_store_offset(4, K16_SP, r, destination);
        }
        --vtop;
    }

    for (i = nb_args - 1; i >= 0; --i) {
        SValue *arg = &vtop[-nb_args + 1 + i];
        K16CallArgument *argument = &arguments[i];

        if (argument->fixed_slot < 0 || argument->fixed_slot >= 3)
            continue;
        if (argument->classification.indirect) {
            k16_addi(argument->fixed_slot + 1, K16_SP,
                     argument->copy_offset);
            continue;
        }
        vpushv(arg);
        gv(RC_R(argument->fixed_slot + 1));
        --vtop;
    }

    func = &vtop[-nb_args];
    if ((func->r & (VT_VALMASK | VT_LVAL)) == VT_CONST &&
        (func->r & VT_SYM)) {
        k16_const32_sym(K16_SCRATCH1, func->sym, func->c.i, R_K16_CALL32);
    } else {
        vpushv(func);
        r = gv(RC_INT);
        k16_move(K16_SCRATCH1, r);
        --vtop;
    }
    o(0x8000u | (K16_SCRATCH1 << 8));
    k16_adjust_sp(outgoing_size);
    tcc_free(arguments);
    vtop -= nb_args + 1;
}

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    Sym *param = func_sym->type.ref;
    int slot = 0;
    loc = 0;
    func_prolog_offset = ind;
    ind += 14;
    while ((param = param->next) != NULL) {
        K16ArgumentClass argument = k16_classify_argument(&param->type);
        int address, part;

        if (slot < 3) {
            if (argument.slots == 1) {
                loc -= 4;
            } else {
                loc = (loc - argument.size - 4) & -argument.align;
                loc += 4;
            }
            address = loc;
            for (part = 0; part < argument.slots; ++part) {
                int fragment_slot = slot + part;
                int source;

                if (fragment_slot < 3) {
                    source = fragment_slot + 1;
                } else {
                    k16_load_offset(4, K16_SCRATCH0, K16_FP,
                                    8 + (fragment_slot - 3) * 4);
                    source = K16_SCRATCH0;
                }
                k16_store_offset(4, K16_FP, source, address + part * 4);
            }
        } else {
            address = 8 + (slot - 3) * 4;
        }
        gfunc_set_param(param, address, argument.indirect);
        slot += argument.slots;
    }
    func_varargs_offset = 8 + (slot > 3 ? slot - 3 : 0) * 4;
}

ST_FUNC void gfunc_epilog(void)
{
    int frame_size = (-loc + 4 + 7) & -8;
    int saved_ind;

    k16_addi(K16_SP, K16_FP, 0);
    k16_load_offset(4, K16_FP, K16_SP, 0);
    k16_addi(K16_SP, K16_SP, 4);
    o(0x9000);
    saved_ind = ind;

    ind = func_prolog_offset;
    k16_store_offset(4, K16_SP, K16_FP, -4);
    k16_addi(K16_FP, K16_SP, -4);
    k16_adjust_sp(-frame_size);
    gen_fill_nops(func_prolog_offset + 14 - ind);
    ind = saved_ind;
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

ST_FUNC void gsym_addr(int t, int a)
{
    Sym label = {0};
    int saved_nocode_wanted;
    int next;
    if (!t)
        return;
    saved_nocode_wanted = nocode_wanted;
    nocode_wanted = 0;
    label.type.t = VT_VOID | VT_STATIC;
    put_extern_sym(&label, cur_text_section, a, 0);
    while (t) {
        next = read32le(cur_text_section->data + t);
        write32le(cur_text_section->data + t, 0);
        greloca(cur_text_section, &label, t, R_K16_ABS32, 0);
        t = next;
    }
    nocode_wanted = saved_nocode_wanted;
}

ST_FUNC int gjmp(int t)
{
    int chain;
    if (nocode_wanted)
        return t;
    o(0xe001u | (K16_SCRATCH1 << 8));
    chain = ind;
    k16_emit_u32((uint32_t)t);
    o(0x7000u | (K16_SCRATCH1 << 8));
    return chain;
}

ST_FUNC void gjmp_addr(int a)
{
    Sym label = {0};
    label.type.t = VT_VOID | VT_STATIC;
    put_extern_sym(&label, cur_text_section, a, 0);
    k16_const32_sym(K16_SCRATCH1, &label, 0, R_K16_ABS32);
    o(0x7000u | (K16_SCRATCH1 << 8));
}

ST_FUNC int gjmp_cond(int op, int t)
{
    int lhs = vtop->cmp_r & 0xff;
    int rhs = vtop->cmp_r >> 8;
    k16_materialize_compare(K16_SCRATCH0, op, lhs, rhs);
    o(0x6000u | (K16_SCRATCH0 << 8) | 4);
    return gjmp(t);
}

ST_FUNC int gjmp_append(int n, int t)
{
    unsigned char *p;
    int next;
    if (!n)
        return t;
    next = n;
    while (read32le(cur_text_section->data + next))
        next = read32le(cur_text_section->data + next);
    p = cur_text_section->data + next;
    write32le(p, (uint32_t)t);
    return n;
}

static void k16_unsigned_division(void)
{
    int bit;
    k16_move(8, K16_SCRATCH0);
    k16_move(9, K16_SCRATCH1);
    k16_const32(10, 0);
    k16_const32(11, 0);
    for (bit = 31; bit >= 0; --bit) {
        k16_const32(K16_SCRATCH1, (uint32_t)bit);
        k16_shr(K16_SCRATCH0, 8, K16_SCRATCH1);
        k16_const32(K16_SCRATCH1, 1);
        k16_and(K16_SCRATCH0, K16_SCRATCH0, K16_SCRATCH1);
        k16_shl(11, 11, K16_SCRATCH1);
        k16_or(11, 11, K16_SCRATCH0);
        k16_ltu(K16_SCRATCH0, 11, 9);
        o(0x6010u | (K16_SCRATCH0 << 8) | 7);
        k16_sub(11, 11, 9);
        k16_const32(K16_SCRATCH0, 1u << bit);
        k16_or(10, 10, K16_SCRATCH0);
    }
}

static int k16_division(int op, int lhs, int rhs)
{
    int is_signed = op == '/' || op == '%' || op == TOK_PDIV;
    int want_remainder = op == '%' || op == TOK_UMOD;

    k16_move(K16_SCRATCH0, lhs);
    k16_move(K16_SCRATCH1, rhs);
    if (is_signed) {
        k16_const32(5, 0);
        k16_lts(6, K16_SCRATCH0, 5);
        k16_lts(7, K16_SCRATCH1, 5);
        o(0x6000u | (6 << 8) | 2);
        k16_sub(K16_SCRATCH0, 5, K16_SCRATCH0);
        o(0x6000u | (7 << 8) | 2);
        k16_sub(K16_SCRATCH1, 5, K16_SCRATCH1);
    }
    k16_unsigned_division();
    if (is_signed) {
        k16_xor(7, 6, 7);
        k16_const32(5, 0);
        o(0x6000u | (7 << 8) | 2);
        k16_sub(10, 5, 10);
        o(0x6000u | (6 << 8) | 2);
        k16_sub(11, 5, 11);
    }
    return want_remainder ? 11 : 10;
}

ST_FUNC void gen_opi(int op)
{
    int lhs, rhs, dst;
    gv2(RC_INT, RC_INT);
    lhs = vtop[-1].r;
    rhs = vtop[0].r;

    if (op == '/' || op == '%' || op == TOK_PDIV ||
        op == TOK_UDIV || op == TOK_UMOD) {
        dst = k16_division(op, lhs, rhs);
        vtop -= 2;
        ++vtop;
        vtop->r = dst;
        return;
    }

    vtop -= 2;
    dst = get_reg(RC_INT);
    ++vtop;
    vtop->r = dst;
    switch (op) {
    case '+': k16_add(dst, lhs, rhs); break;
    case '-': k16_sub(dst, lhs, rhs); break;
    case '*': k16_mul(dst, lhs, rhs); break;
    case '&': k16_and(dst, lhs, rhs); break;
    case '|': k16_or(dst, lhs, rhs); break;
    case '^': k16_xor(dst, lhs, rhs); break;
    case TOK_SHL: k16_shl(dst, lhs, rhs); break;
    case TOK_SHR: k16_shr(dst, lhs, rhs); break;
    case TOK_SAR: k16_sar(dst, lhs, rhs); break;
    default:
        if (op >= TOK_ULT && op <= TOK_GT) {
            vset_VT_CMP(op);
            vtop->cmp_r = lhs | (rhs << 8);
        } else {
            k16_unimplemented("this integer operator");
        }
        break;
    }
}
ST_FUNC void gen_opf(int op) { (void)op; k16_reject_float(); }
ST_FUNC void gen_cvt_ftoi(int t) { (void)t; k16_reject_float(); }
ST_FUNC void gen_cvt_itof(int t) { (void)t; k16_reject_float(); }
ST_FUNC void gen_cvt_ftof(int t) { (void)t; k16_reject_float(); }

ST_FUNC void gen_va_start(void)
{
    --vtop;
    vset(&char_pointer_type, VT_LOCAL, func_varargs_offset);
}

ST_FUNC void gen_va_arg(CType *type)
{
    K16ArgumentClass argument = k16_classify_argument(type);
    int base, offset, cursor;
    int extent = argument.slots * 4;

    if (!(vtop->r & VT_LVAL))
        tcc_error("__builtin_va_arg expects a modifiable va_list");
    base = k16_value_address(vtop, K16_SCRATCH1, &offset);
    k16_base_offset(&base, &offset, K16_SCRATCH1);
    cursor = get_reg(RC_INT);
    k16_load_offset(4, cursor, base, offset);
    if (argument.align > 1) {
        k16_const32(K16_SCRATCH0, (uint32_t)(argument.align - 1));
        k16_add(cursor, cursor, K16_SCRATCH0);
        k16_const32(K16_SCRATCH0, (uint32_t)-argument.align);
        k16_and(cursor, cursor, K16_SCRATCH0);
    }
    k16_addi(K16_SCRATCH0, cursor, extent);
    k16_store_offset(4, base, K16_SCRATCH0, offset);
    if (argument.indirect)
        k16_load_offset(4, cursor, cursor, 0);
    vtop->type = *type;
    vtop->r = cursor | VT_LVAL;
    vtop->c.i = 0;
    vtop->sym = NULL;
}

ST_FUNC void ggoto(void) { k16_unimplemented("computed goto"); }
ST_FUNC void gen_vla_sp_save(int addr) { (void)addr; k16_unimplemented("variable-length arrays"); }
ST_FUNC void gen_vla_sp_restore(int addr) { (void)addr; k16_unimplemented("variable-length arrays"); }
ST_FUNC void gen_vla_alloc(CType *type, int align) { (void)type; (void)align; k16_unimplemented("variable-length arrays"); }

#endif
