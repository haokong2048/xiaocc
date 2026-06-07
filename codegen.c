#include "xiaocc.h"

static int depth;
static char *argreg8[] = {"w0", "w1", "w2", "w3", "w4", "w5"};
static char *argreg64[] = {"x0", "x1", "x2", "x3", "x4", "x5"};
static Obj *current_fn;

static void gen_expr(Node *node);

// 从 x0 指向的地址加载值。
// 如果是数组类型则不加载，因为无法将整个数组加载到寄存器中。
// 这也就是 C 语言中"数组会自动转换为指向首元素的指针"发生的地方。
static void load(Type *ty) {
    if (ty->kind == TY_ARRAY)
        return;

    if (ty->size == 1)
        printf("    ldrsb x0, [x0]\n");
    else
        printf("    ldr x0, [x0]\n");
}

static int count(void) {
    static int i = 1;
    return i++;
}

static void push(void) {
    printf("    str x0, [sp, #-16]!\n");
    depth++;
}

static void pop(char *arg) {
    printf("    ldr %s, [sp], #16\n", arg);
    depth--;
}

// 将 x0 存储到栈顶指向的地址中
static void store(Type *ty) {
    pop("x1");

    if (ty->size == 1)
        printf("    strb w0, [x1]\n");
    else
        printf("    str x0, [x1]\n");
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
            printf("    sub x0, x29, #%d\n", -node->var->offset);
        } else {
            // 全局变量
            printf("    adrp x0, %s\n", node->var->name);
            printf("    add x0, x0, :lo12:%s\n", node->var->name);
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
    switch (node->kind) {
    case ND_NUM:
        printf("    mov x0, #%d\n", node->val);
        return;
    case ND_NEG:
        gen_expr(node->lhs);
        printf("    neg x0, x0\n");
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
    case ND_FUNCALL: {
        int nargs = 0;
        for (Node *arg = node->args; arg; arg = arg->next) {
            gen_expr(arg);
            push();
            nargs++;
        }

        for (int i = nargs - 1; i >= 0; i--)
            pop(argreg64[i]);

        printf("    bl %s\n", node->funcname);
        return;
    }
    }

    gen_expr(node->rhs);
    push();
    gen_expr(node->lhs);
    pop("x1");

    switch (node->kind) {
    case ND_ADD:
        printf("    add x0, x0, x1\n");
        return;
    case ND_SUB:
        printf("    sub x0, x0, x1\n");
        return;
    case ND_MUL:
        printf("    mul x0, x0, x1\n");
        return;
    case ND_DIV:
        printf("    sdiv x0, x0, x1\n");
        return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
        printf("    cmp x0, x1\n");

        if (node->kind == ND_EQ)
            printf("    cset x0, eq\n");
        else if (node->kind == ND_NE)
            printf("    cset x0, ne\n");
        else if (node->kind == ND_LT)
            printf("    cset x0, lt\n");
        else if (node->kind == ND_LE)
            printf("    cset x0, le\n");

        return;
    }

    error_tok(node->tok, "invalid expression");
}

static void gen_stmt(Node *node) {
    switch (node->kind) {
    case ND_IF: {
        int c = count();
        gen_expr(node->cond);
        printf("    cmp x0, #0\n");
        printf("    b.eq .L.else.%d\n", c);
        gen_stmt(node->then);
        printf("    b .L.end.%d\n", c);
        printf(".L.else.%d:\n", c);
        if (node->els)
            gen_stmt(node->els);
        printf(".L.end.%d:\n", c);
        return;
    }
    case ND_FOR: {
        int c = count();
        if (node->init)
            gen_stmt(node->init);
        printf(".L.begin.%d:\n", c);
        if (node->cond) {
            gen_expr(node->cond);
            printf("    cmp x0, #0\n");
            printf("    b.eq .L.end.%d\n", c);
        }
        gen_stmt(node->then);
        if (node->inc)
            gen_expr(node->inc);
        printf("    b .L.begin.%d\n", c);
        printf(".L.end.%d:\n", c);
        return;
    }
    case ND_BLOCK:
        for (Node *n = node->body; n; n = n->next)
            gen_stmt(n);
        return;
    case ND_RETURN:
        gen_expr(node->lhs);
        printf("    b .L.return.%s\n", current_fn->name);
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

        printf("    .data\n");
        printf("    .globl %s\n", var->name);
        printf("%s:\n", var->name);

        if (var->init_data) {
            for (int i = 0; i < var->ty->size; i++)
                printf("    .byte %d\n", var->init_data[i]);
        } else {
            printf("    .zero %d\n", var->ty->size);
        }
    }
}

static void emit_text(Obj *prog) {
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function)
            continue;

        printf("    .global %s\n", fn->name);
        printf("    .text\n");
        printf("%s:\n", fn->name);
        current_fn = fn;

        // 函数序言
        printf("    stp x29, x30, [sp, #-16]!\n");
        printf("    mov x29, sp\n");
        printf("    sub sp, sp, #%d\n", fn->stack_size);

        // 将寄存器传入的参数保存到栈中
        int i = 0;
        for (Obj *var = fn->params; var; var = var->next) {
            if (var->ty->size == 1)
                printf("    strb %s, [x29, #%d]\n", argreg8[i++], var->offset);
            else
                printf("    str %s, [x29, #%d]\n", argreg64[i++], var->offset);
        }

        gen_stmt(fn->body);
        assert(depth == 0);

        printf(".L.return.%s:\n", fn->name);

        // 函数尾声
        printf("    add sp, sp, #%d\n", fn->stack_size);
        printf("    ldp x29, x30, [sp], #16\n");
        printf("    ret\n");
    }
}

void codegen(Obj *prog) {
    assign_lvar_offsets(prog);
    emit_data(prog);
    emit_text(prog);
}
