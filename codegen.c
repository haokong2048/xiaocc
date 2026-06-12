#include "xiaocc.h"

static FILE *output_file;
static int depth;
static char *argreg64[] = {"x0", "x1", "x2", "x3", "x4", "x5"};
static Obj *current_fn;

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

static void println(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(output_file, fmt, ap);
    va_end(ap);
    fprintf(output_file, "\n");
}

static int count(void) {
    static int i = 1;
    return i++;
}

static void push(void) {
    println("    str x0, [sp, #-16]!");
    depth++;
}

static void pop(char *arg) {
    println("    ldr %s, [sp], #16", arg);
    depth--;
}

// 从 x0 指向的地址加载值。
// 如果是数组类型则不加载，因为无法将整个数组加载到寄存器中。
// 这也就是 C 语言中"数组会自动转换为指向首元素的指针"发生的地方。
static void load(Type *ty) {
    if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_UNION)
        return;

    if (ty->size == 1) {
        if (ty->is_unsigned)
            println("    ldrb w0, [x0]");
        else
            println("    ldrsb x0, [x0]");
    } else if (ty->size == 2) {
        if (ty->is_unsigned)
            println("    ldrh w0, [x0]");
        else
            println("    ldrsh x0, [x0]");
    } else if (ty->size == 4) {
        if (ty->is_unsigned)
            println("    ldr w0, [x0]");
        else
            println("    ldrsw x0, [x0]");
    } else {
        println("    ldr x0, [x0]");
    }
}

// 将 x0 存储到栈顶指向的地址中
static void store(Type *ty) {
    pop("x1");

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        for (int i = 0; i < ty->size; i++) {
            println("    ldrb w2, [x0, #%d]", i);
            println("    strb w2, [x1, #%d]", i);
        }
        return;
    }

    if (ty->size == 1)
        println("    strb w0, [x1]");
    else if (ty->size == 2)
        println("    strh w0, [x1]");
    else if (ty->size == 4)
        println("    str w0, [x1]");
    else
        println("    str x0, [x1]");
}

// 将 n 向上取整到 align 的倍数
int align_to(int n, int align) {
    return (n + align - 1) / align * align;
}

// 计算 x29 + offset 并将结果存入目标寄存器
// 处理大偏移量（> 4095）
static void compute_fp_offset(int offset, char *dst) {
    if (offset >= 0 && offset <= 4095) {
        println("    add %s, x29, #%d", dst, offset);
    } else if (offset < 0 && -offset <= 4095) {
        println("    sub %s, x29, #%d", dst, -offset);
    } else {
        // 使用 mov/movk 加载偏移量
        int abs_off = (offset < 0) ? -offset : offset;
        println("    movz %s, #%d", dst, abs_off & 0xFFFF);
        if (abs_off & 0xFFFF0000)
            println("    movk %s, #%d, lsl #16", dst, (abs_off >> 16) & 0xFFFF);
        if (offset < 0)
            println("    sub %s, x29, %s", dst, dst);
        else
            println("    add %s, x29, %s", dst, dst);
    }
}

// 计算给定节点的绝对地址
// 如果节点不在内存中则报错
static void gen_addr(Node *node) {
    switch (node->kind) {
    case ND_VAR:
        if (node->var->is_local) {
            // 局部变量
            compute_fp_offset(node->var->offset, "x0");
        } else {
            // 全局变量
            println("    adrp x0, %s", node->var->name);
            println("    add x0, x0, :lo12:%s", node->var->name);
        }
        return;
    case ND_DEREF:
        gen_expr(node->lhs);
        return;
    case ND_COMMA:
        gen_expr(node->lhs);
        gen_addr(node->rhs);
        return;
    case ND_MEMBER:
        gen_addr(node->lhs);
        println("    add x0, x0, #%d", node->member->offset);
        return;
    }

    error_tok(node->tok, "not an lvalue");
}

enum { I8, I16, I32, I64, U8, U16, U32, U64 };

static int getTypeId(Type *ty) {
    switch (ty->kind) {
    case TY_BOOL:
    case TY_CHAR:
        return ty->is_unsigned ? U8 : I8;
    case TY_SHORT:
        return ty->is_unsigned ? U16 : I16;
    case TY_INT:
        return ty->is_unsigned ? U32 : I32;
    case TY_LONG:
        return ty->is_unsigned ? U64 : I64;
    }
    return U64;
}

// ARM64 类型转换：使用符号/零扩展指令
// sxtb: 符号扩展 8→32, uxtb: 零扩展 8→32
// sxth: 符号扩展 16→32, uxth: 零扩展 16→32
// sxtw: 符号扩展 32→64
// mov w0, w0: 零扩展 32→64
static char i32i8[]  = "sxtb w0, w0";
static char i32u8[]  = "uxtb w0, w0";
static char i32i16[] = "sxth w0, w0";
static char i32u16[] = "uxth w0, w0";
static char i64i32[] = "sxtw x0, w0";
static char u64i32[] = "mov w0, w0";

static char *cast_table[][10] = {
    // i8   i16     i32   i64     u8     u16     u32   u64
    {NULL,  NULL,   NULL, NULL,   i32u8, i32u16, NULL, NULL},   // i8
    {i32i8, NULL,   NULL, NULL,   i32u8, i32u16, NULL, NULL},   // i16
    {i32i8, i32i16, NULL, i64i32, i32u8, i32u16, NULL, i64i32}, // i32
    {i32i8, i32i16, NULL, NULL,   i32u8, i32u16, NULL, NULL},   // i64
    {i32i8, NULL,   NULL, NULL,   NULL,  NULL,   NULL, NULL},   // u8
    {i32i8, i32i16, NULL, NULL,   i32u8, NULL,   NULL, NULL},   // u16
    {i32i8, i32i16, NULL, u64i32, i32u8, i32u16, NULL, u64i32}, // u32
    {i32i8, i32i16, NULL, NULL,   i32u8, i32u16, NULL, NULL},   // u64
};

static void cmp_zero(Type *ty) {
    if (is_integer(ty) && ty->size <= 4)
        println("    cmp w0, #0");
    else
        println("    cmp x0, #0");
}

static void cast(Type *from, Type *to) {
    if (to->kind == TY_VOID)
        return;

    if (to->kind == TY_BOOL) {
        cmp_zero(from);
        println("    cset x0, ne");
        return;
    }

    int t1 = getTypeId(from);
    int t2 = getTypeId(to);
    if (cast_table[t1][t2])
        println("    %s", cast_table[t1][t2]);
}

// 为给定节点生成代码
static void gen_expr(Node *node) {
    println("    .loc 1 %d", node->tok->line_no);

    switch (node->kind) {
    case ND_NULL_EXPR:
        return;
    case ND_NUM: {
        // 浮点常量
        if (is_flonum(node->ty)) {
            union { float f32; double f64; uint32_t u32; uint64_t u64; } u;
            if (node->ty->kind == TY_FLOAT) {
                u.f32 = (float)node->fval;
                println("    movz w0, #%d", u.u32 & 0xFFFF);
                if (u.u32 >> 16)
                    println("    movk w0, #%d, lsl #16", (u.u32 >> 16) & 0xFFFF);
                println("    fmov s0, w0");
            } else {
                u.f64 = node->fval;
                println("    movz x0, #%d", (int)(u.u64 & 0xFFFF));
                if (u.u64 >> 16)
                    println("    movk x0, #%d, lsl #16", (int)((u.u64 >> 16) & 0xFFFF));
                if (u.u64 >> 32)
                    println("    movk x0, #%d, lsl #32", (int)((u.u64 >> 32) & 0xFFFF));
                if (u.u64 >> 48)
                    println("    movk x0, #%d, lsl #48", (int)((u.u64 >> 48) & 0xFFFF));
                println("    fmov d0, x0");
            }
            return;
        }

        uint64_t val = (uint64_t)node->val;
        bool first = true;
        for (int shift = 0; shift < 64; shift += 16) {
            int chunk = (int)((val >> shift) & 0xFFFF);
            if (shift == 0 || chunk != 0) {
                if (first) {
                    println("    movz x0, #%d", chunk);
                    first = false;
                } else {
                    println("    movk x0, #%d, lsl #%d", chunk, shift);
                }
            }
        }
        return;
    }
    case ND_NEG:
        gen_expr(node->lhs);
        println("    neg x0, x0");
        return;
    case ND_VAR:
    case ND_MEMBER:
        gen_addr(node);
        load(node->ty);
        return;
    case ND_DEREF:
        gen_expr(node->lhs);
        load(node->ty);
        return;
    case ND_ADDR:
        gen_addr(node->lhs);
        return;
    case ND_ASSIGN:
        gen_addr(node->lhs);
        push();
        gen_expr(node->rhs);
        store(node->ty);
        return;
    case ND_STMT_EXPR:
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n);
        return;
    case ND_COMMA:
        gen_expr(node->lhs);
        gen_expr(node->rhs);
        return;
    case ND_COND: {
        int c = count();
        gen_expr(node->cond);
        println("    cmp x0, #0");
        println("    b.eq .L.else.%d", c);
        gen_expr(node->then);
        println("    b .L.end.%d", c);
        println(".L.else.%d:", c);
        gen_expr(node->els);
        println(".L.end.%d:", c);
        return;
    }
    case ND_NOT:
        gen_expr(node->lhs);
        println("    cmp x0, #0");
        println("    cset x0, eq");
        return;
    case ND_BITNOT:
        gen_expr(node->lhs);
        println("    mvn x0, x0");
        return;
    case ND_LOGAND: {
        int c = count();
        gen_expr(node->lhs);
        println("    cmp x0, #0");
        println("    b.eq .L.false.%d", c);
        gen_expr(node->rhs);
        println("    cmp x0, #0");
        println("    b.eq .L.false.%d", c);
        println("    mov x0, #1");
        println("    b .L.end.%d", c);
        println(".L.false.%d:", c);
        println("    mov x0, #0");
        println(".L.end.%d:", c);
        return;
    }
    case ND_LOGOR: {
        int c = count();
        gen_expr(node->lhs);
        println("    cmp x0, #0");
        println("    b.ne .L.true.%d", c);
        gen_expr(node->rhs);
        println("    cmp x0, #0");
        println("    b.ne .L.true.%d", c);
        println("    mov x0, #0");
        println("    b .L.end.%d", c);
        println(".L.true.%d:", c);
        println("    mov x0, #1");
        println(".L.end.%d:", c);
        return;
    }
    case ND_CAST:
        gen_expr(node->lhs);
        cast(node->lhs->ty, node->ty);
        return;
    case ND_MEMZERO: {
        int c = count();
        println("    mov w1, #%d", node->var->ty->size);
        compute_fp_offset(node->var->offset, "x0");
        println(".L.memzero.%d:", c);
        println("    strb wzr, [x0], #1");
        println("    sub w1, w1, #1");
        println("    cbnz w1, .L.memzero.%d", c);
        return;
    }
    case ND_FUNCALL: {
        int nargs = 0;
        for (Node *arg = node->args; arg; arg = arg->next) {
            gen_expr(arg);
            push();
            nargs++;
        }

        for (int i = nargs - 1; i >= 0; i--)
            pop(argreg64[i]);

        println("    bl %s", node->funcname);

        // 清除返回值寄存器的高位
        switch (node->ty->kind) {
        case TY_BOOL:
            println("    and w0, w0, #0xff");
            return;
        case TY_CHAR:
            if (node->ty->is_unsigned)
                println("    uxtb w0, w0");
            else
                println("    sxtb w0, w0");
            return;
        case TY_SHORT:
            if (node->ty->is_unsigned)
                println("    uxth w0, w0");
            else
                println("    sxth w0, w0");
            return;
        }
        return;
    }
    }

    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("x1");

    char *r0, *r1, *r2;
    if (node->lhs->ty->kind == TY_LONG || node->lhs->ty->base) {
        r0 = "x0";
        r1 = "x1";
        r2 = "x2";
    } else {
        r0 = "w0";
        r1 = "w1";
        r2 = "w2";
    }

    switch (node->kind) {
    case ND_ADD:
        println("    add %s, %s, %s", r0, r0, r1);
        return;
    case ND_SUB:
        println("    sub %s, %s, %s", r0, r0, r1);
        return;
    case ND_MUL:
        println("    mul %s, %s, %s", r0, r0, r1);
        return;
    case ND_DIV:
        if (node->ty->is_unsigned)
            println("    udiv %s, %s, %s", r0, r0, r1);
        else
            println("    sdiv %s, %s, %s", r0, r0, r1);
        return;
    case ND_MOD:
        if (node->ty->is_unsigned) {
            println("    udiv %s, %s, %s", r2, r0, r1);
            println("    msub %s, %s, %s, %s", r0, r2, r1, r0);
        } else {
            println("    sdiv %s, %s, %s", r2, r0, r1);
            println("    msub %s, %s, %s, %s", r0, r2, r1, r0);
        }
        return;
    case ND_BITAND:
        println("    and %s, %s, %s", r0, r0, r1);
        return;
    case ND_BITOR:
        println("    orr %s, %s, %s", r0, r0, r1);
        return;
    case ND_BITXOR:
        println("    eor %s, %s, %s", r0, r0, r1);
        return;
    case ND_SHL:
        println("    lsl %s, %s, %s", r0, r0, r1);
        return;
    case ND_SHR:
        if (node->lhs->ty->is_unsigned)
            println("    lsr %s, %s, %s", r0, r0, r1);
        else
            println("    asr %s, %s, %s", r0, r0, r1);
        return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
        println("    cmp %s, %s", r0, r1);

        if (node->kind == ND_EQ)
            println("    cset x0, eq");
        else if (node->kind == ND_NE)
            println("    cset x0, ne");
        else if (node->kind == ND_LT)
            println("    cset x0, %s", node->lhs->ty->is_unsigned ? "lo" : "lt");
        else if (node->kind == ND_LE)
            println("    cset x0, %s", node->lhs->ty->is_unsigned ? "ls" : "le");

        return;
    }

    error_tok(node->tok, "invalid expression");
}

static void gen_stmt(Node *node) {
    println("    .loc 1 %d", node->tok->line_no);

    switch (node->kind) {
    case ND_IF: {
        int c = count();
        gen_expr(node->cond);
        println("    cmp x0, #0");
        println("    b.eq .L.else.%d", c);
        gen_stmt(node->then);
        println("    b .L.end.%d", c);
        println(".L.else.%d:", c);
        if (node->els)
            gen_stmt(node->els);
        println(".L.end.%d:", c);
        return;
    }
    case ND_FOR: {
        int c = count();
        if (node->init)
            gen_stmt(node->init);
        println(".L.begin.%d:", c);
        if (node->cond) {
            gen_expr(node->cond);
            println("    cmp x0, #0");
            println("    b.eq %s", node->brk_label);
        }
        gen_stmt(node->then);
        println("%s:", node->cont_label);
        if (node->inc)
            gen_expr(node->inc);
        println("    b .L.begin.%d", c);
        println("%s:", node->brk_label);
        return;
    }
    case ND_DO: {
        int c = count();
        println(".L.begin.%d:", c);
        gen_stmt(node->then);
        println("%s:", node->cont_label);
        gen_expr(node->cond);
        println("    cmp x0, #0");
        println("    b.ne .L.begin.%d", c);
        println("%s:", node->brk_label);
        return;
    }
    case ND_SWITCH:
        gen_expr(node->cond);

        for (Node *n = node->case_next; n; n = n->case_next) {
            println("    cmp x0, #%ld", n->val);
            println("    b.eq %s", n->label);
        }

        if (node->default_case)
            println("    b %s", node->default_case->label);

        println("    b %s", node->brk_label);
        gen_stmt(node->then);
        println("%s:", node->brk_label);
        return;
    case ND_CASE:
        println("%s:", node->label);
        gen_stmt(node->lhs);
        return;
    case ND_BLOCK:
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n);
        return;
    case ND_GOTO:
        println("    b %s", node->unique_label);
        return;
    case ND_LABEL:
        println("%s:", node->unique_label);
        gen_stmt(node->lhs);
        return;
    case ND_RETURN:
        if (node->lhs)
            gen_expr(node->lhs);
        println("    b .L.return.%s", current_fn->name);
        return;
    case ND_EXPR_STMT:
        gen_expr(node->lhs);
        return;
    }

    error_tok(node->tok, "invalid statement");
}

// 为局部变量分配偏移量
static void assign_lvar_offsets(Obj *prog) {
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function)
            continue;

        int offset = 0;
        for (Obj *var = fn->locals; var; var = var->next) {
            offset += var->ty->size;
            offset = align_to(offset, var->align);
            var->offset = -offset;
        }
        fn->stack_size = align_to(offset, 16);
    }
}

static void emit_data(Obj *prog) {
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function || !var->is_definition)
            continue;

        if (var->is_static)
            println("    .local %s", var->name);
        else
            println("    .globl %s", var->name);
        println("    .balign %d", var->align);

        if (var->init_data) {
            println("    .data");
            println("%s:", var->name);

            Relocation *rel = var->rel;
            int pos = 0;
            while (pos < var->ty->size) {
                if (rel && rel->offset == pos) {
                    println("    .xword %s%+ld", rel->label, rel->addend);
                    rel = rel->next;
                    pos += 8;
                } else {
                    println("    .byte %d", var->init_data[pos++]);
                }
            }
            continue;
        }

        println("    .bss");
        println("%s:", var->name);
        println("    .zero %d", var->ty->size);
    }
}

static void store_gp(int r, int offset, int sz) {
    if (offset < -256 || offset > 255) {
        compute_fp_offset(offset, "x8");
        switch (sz) {
        case 1: println("    strb w%d, [x8]", r); return;
        case 2: println("    strh w%d, [x8]", r); return;
        case 4: println("    str w%d, [x8]", r); return;
        case 8: println("    str x%d, [x8]", r); return;
        }
    }
    switch (sz) {
    case 1:
        println("    strb w%d, [x29, #%d]", r, offset);
        return;
    case 2:
        println("    strh w%d, [x29, #%d]", r, offset);
        return;
    case 4:
        println("    str w%d, [x29, #%d]", r, offset);
        return;
    case 8:
        println("    str x%d, [x29, #%d]", r, offset);
        return;
    }
    unreachable();
}

static void emit_text(Obj *prog) {
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function || !fn->is_definition)
            continue;

        if (fn->is_static)
            println("    .local %s", fn->name);
        else
            println("    .global %s", fn->name);

        println("    .text");
        println("%s:", fn->name);
        current_fn = fn;

        // 函数序言
        println("    stp x29, x30, [sp, #-16]!");
        println("    mov x29, sp");
        if (fn->stack_size <= 4095)
            println("    sub sp, sp, #%d", fn->stack_size);
        else {
            println("    movz x8, #%d", fn->stack_size & 0xFFFF);
            if (fn->stack_size & 0xFFFF0000)
                println("    movk x8, #%d, lsl #16", (fn->stack_size >> 16) & 0xFFFF);
            println("    sub sp, sp, x8");
        }

        // 为可变参数函数保存寄存器 (ARM64 AAPCS64 布局)
        if (fn->va_area) {
            int gp = 0;
            for (Obj *var = fn->params; var; var = var->next)
                gp++;
            int off = fn->va_area->offset;

            // 使用 x8 作为基地址
            compute_fp_offset(off, "x8");

            // ARM64 va_area 布局:
            //   +0:   va_elem (32 bytes)
            //   +32:  FP save area (128 bytes, q0-q7 每个 16 bytes)
            //   +160: GP save area (48 bytes, x2-x7 每个 8 bytes)
            //   +208: end
            // 总共: 208 bytes (va_area 分配了 224)

            // 保存 GP 寄存器 x2-x7 到 GP save area (偏移 160)
            // x0-x1 是命名参数, 不保存在 variadic save area 中
            println("    str x2, [x8, #160]");
            println("    str x3, [x8, #168]");
            println("    str x4, [x8, #176]");
            println("    str x5, [x8, #184]");
            println("    str x6, [x8, #192]");
            println("    str x7, [x8, #200]");

            // 设置 va_elem 元数据 (匹配 ARM64 glibc 布局)
            // __gr_offs (偏移 24): = -(8-num_params)*8
            int gr_offs = -64 + gp * 8;
            println("    mov w0, #%d", gr_offs);
            println("    str w0, [x8, #24]");

            // __vr_offs (偏移 28): = -(FP save area 与 GP save area 的距离)
            // FP area 在 offset 32, GP area 在 offset 160
            // __vr_offs = 32 - 160 = -128
            println("    mov w0, #-128");
            println("    str w0, [x8, #28]");

            // __stack (偏移 0): overflow_arg_area 指针
            println("    add x0, x8, #208");
            println("    str x0, [x8, #0]");

            // __gr_top (偏移 8): 指向 GP save area 末尾之后 (offset 160+48=208)
            println("    add x0, x8, #208");
            println("    str x0, [x8, #8]");

            // __vr_top (偏移 16): 指向 GP save area 开头 (offset 160)
            println("    add x0, x8, #160");
            println("    str x0, [x8, #16]");
        }

        // 将寄存器传入的参数保存到栈中
        int i = 0;
        for (Obj *var = fn->params; var; var = var->next)
            store_gp(i++, var->offset, var->ty->size);

        gen_stmt(fn->body);
        assert(depth == 0);

        println(".L.return.%s:", fn->name);

        // 函数尾声
        if (fn->stack_size <= 4095)
            println("    add sp, sp, #%d", fn->stack_size);
        else {
            println("    movz x8, #%d", fn->stack_size & 0xFFFF);
            if (fn->stack_size & 0xFFFF0000)
                println("    movk x8, #%d, lsl #16", (fn->stack_size >> 16) & 0xFFFF);
            println("    add sp, sp, x8");
        }
        println("    ldp x29, x30, [sp], #16");
        println("    ret");
    }
}

void codegen(Obj *prog, FILE *out) {
    output_file = out;

    assign_lvar_offsets(prog);
    emit_data(prog);
    emit_text(prog);
}
