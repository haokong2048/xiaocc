#include "xiaocc.h"

static FILE *output_file;
static int depth;
static char *argreg8[] = {"w0", "w1", "w2", "w3", "w4", "w5"};
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
    if (ty->kind == TY_ARRAY)
        return;

    if (ty->size == 1)
        println("    ldrsb x0, [x0]");
    else
        println("    ldr x0, [x0]");
}

// 将 x0 存储到栈顶指向的地址中
static void store(Type *ty) {
    pop("x1");

    if (ty->size == 1)
        println("    strb w0, [x1]");
    else
        println("    str x0, [x1]");
}

// 将 n 向上取整到 align 的倍数
static int align_to(int n, int align) {
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
    }

    error_tok(node->tok, "not an lvalue");
}

// 为给定节点生成代码
static void gen_expr(Node *node) {
    println("    .loc 1 %d", node->tok->line_no);

    switch (node->kind) {
    case ND_NUM:
        println("    mov x0, #%d", node->val);
        return;
    case ND_NEG:
        gen_expr(node->lhs);
        println("    neg x0, x0");
        return;
    case ND_VAR:
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

    switch (node->kind) {
    case ND_ADD:
        println("    add x0, x0, x1");
        return;
    case ND_SUB:
        println("    sub x0, x0, x1");
        return;
    case ND_MUL:
        println("    mul x0, x0, x1");
        return;
    case ND_DIV:
        println("    sdiv x0, x0, x1");
        return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
        println("    cmp x0, x1");

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

static void emit_text(Obj *prog) {
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function)
            continue;

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
        for (Obj *var = fn->params; var; var = var->next) {
            if (var->ty->size == 1)
                println("    strb %s, [x29, #%d]", argreg8[i++], var->offset);
            else
                println("    str %s, [x29, #%d]", argreg64[i++], var->offset);
        }

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
