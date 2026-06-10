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

    if (ty->size == 1)
        println("    ldrsb x0, [x0]");
    else if (ty->size == 2)
        println("    ldrsh x0, [x0]");
    else if (ty->size == 4)
        println("    ldrsw x0, [x0]");
    else
        println("    ldr x0, [x0]");
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

// 计算给定节点的绝对地址
// 如果节点不在内存中则报错
static void gen_addr(Node *node) {
    switch (node->kind) {
    case ND_VAR:
        if (node->var->is_local) {
            // 局部变量
            println("    sub x0, x29, #%d", -node->var->offset);
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

enum { I8, I16, I32, I64 };

static int getTypeId(Type *ty) {
    switch (ty->kind) {
    case TY_BOOL:
    case TY_CHAR:
        return I8;
    case TY_SHORT:
        return I16;
    case TY_INT:
        return I32;
    }
    return I64;
}

// ARM64 类型转换：使用符号扩展指令处理截断和扩展。
// sxtb: 符号扩展 8→32, sxth: 符号扩展 16→32, sxtw: 符号扩展 32→64
static char i32i8[]  = "sxtb w0, w0";
static char i32i16[] = "sxth w0, w0";
static char i64i32[] = "sxtw x0, w0";

static char *cast_table[][10] = {
    // i8  i16 i32 i64
    {NULL,  NULL,   NULL, NULL},    // i8
    {i32i8, NULL,   NULL, NULL},    // i16
    {i32i8, i32i16, NULL, i64i32},  // i32
    {i32i8, i32i16, NULL, NULL},    // i64
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
    case ND_NUM: {
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
    case ND_NOT:
        gen_expr(node->lhs);
        println("    cmp x0, #0");
        println("    cset x0, eq");
        return;
    case ND_BITNOT:
        gen_expr(node->lhs);
        println("    mvn x0, x0");
        return;
    case ND_CAST:
        gen_expr(node->lhs);
        cast(node->lhs->ty, node->ty);
        return;
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
        println("    sdiv %s, %s, %s", r0, r0, r1);
        return;
    case ND_MOD:
        println("    sdiv %s, %s, %s", r2, r0, r1);
        println("    msub %s, %s, %s, %s", r0, r2, r1, r0);
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
            println("    cset x0, lt");
        else if (node->kind == ND_LE)
            println("    cset x0, le");

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
            println("    b.eq .L.end.%d", c);
        }
        gen_stmt(node->then);
        if (node->inc)
            gen_expr(node->inc);
        println("    b .L.begin.%d", c);
        println(".L.end.%d:", c);
        return;
    }
    case ND_BLOCK:
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n);
        return;
    case ND_RETURN:
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
            offset = align_to(offset, var->ty->align);
            var->offset = -offset;
        }
        fn->stack_size = align_to(offset, 16);
    }
}

static void emit_data(Obj *prog) {
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function)
            continue;

        println("    .data");
        println("    .globl %s", var->name);
        println("%s:", var->name);

        if (var->init_data) {
            for (int i = 0; i < var->ty->size; i++)
                println("    .byte %d", var->init_data[i]);
        } else {
            println("    .zero %d", var->ty->size);
        }
    }
}

static void store_gp(int r, int offset, int sz) {
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
        println("    sub sp, sp, #%d", fn->stack_size);

        // 将寄存器传入的参数保存到栈中
        int i = 0;
        for (Obj *var = fn->params; var; var = var->next)
            store_gp(i++, var->offset, var->ty->size);

        gen_stmt(fn->body);
        assert(depth == 0);

        println(".L.return.%s:", fn->name);

        // 函数尾声
        println("    add sp, sp, #%d", fn->stack_size);
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
