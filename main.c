#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// 词法分析器
//

typedef enum {
    TK_PUNCT, // 标点符号
    TK_NUM,   // 数字字面量
    TK_EOF,   // 文件结束标记
} TokenKind;

// Token 类型
typedef struct Token Token;
struct Token {
    TokenKind kind; // Token 类型
    Token *next;    // 下一个 Token
    int val;        // 如果类型是 TK_NUM，存储其值
    char *loc;      // Token 位置
    int len;        // Token 长度
};

// 当前输入字符串
static char *current_input;

// 报告错误并退出
static void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

// 报告错误位置并退出
static void verror_at(char *loc, char *fmt, va_list ap) {
    int pos = loc - current_input;
    fprintf(stderr, "%s\n", current_input);
    fprintf(stderr, "%*s", pos, ""); // 打印 pos 个空格
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

static void error_at(char *loc, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(loc, fmt, ap);
}

static void error_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(tok->loc, fmt, ap);
}

// 判断当前 Token 是否匹配字符串 op
static bool equal(Token *tok, char *op) {
    return memcmp(tok->loc, op, tok->len) == 0 && op[tok->len] == '\0';
}

// 确保当前 Token 为 s
static Token *skip(Token *tok, char *s) {
    if (!equal(tok, s))
        error_tok(tok, "expected '%s'", s);
    return tok->next;
}

// 确保当前 Token 为 TK_NUM
static int get_number(Token *tok) {
    if (tok->kind != TK_NUM)
        error_tok(tok, "expected a number");
    return tok->val;
}

// 创建一个新的 Token
static Token *new_token(TokenKind kind, char *start, char *end) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->loc = start;
    tok->len = end - start;
    return tok;
}

// 对 current_input 进行词法分析，返回 Token 链表
static Token *tokenize(void) {
    char *p = current_input;
    Token head = {};
    Token *cur = &head;

    while (*p) {
        // 跳过空白字符
        if (isspace(*p)) {
            p++;
            continue;
        }

        // 数字字面量
        if (isdigit(*p)) {
            cur = cur->next = new_token(TK_NUM, p, p);
            char *q = p;
            cur->val = strtoul(p, &p, 10);
            cur->len = p - q;
            continue;
        }

        // 标点符号
        if (ispunct(*p)) {
            cur = cur->next = new_token(TK_PUNCT, p, p + 1);
            p++;
            continue;
        }

        error_at(p, "invalid token");
    }

    cur = cur->next = new_token(TK_EOF, p, p);
    return head.next;
}

//
// 语法分析器
//

typedef enum {
    ND_ADD, // +
    ND_SUB, // -
    ND_MUL, // *
    ND_DIV, // /
    ND_NEG, // 一元负号
    ND_NUM, // 整数
} NodeKind;

// AST 节点类型
typedef struct Node Node;
struct Node {
    NodeKind kind; // 节点类型
    Node *lhs;     // 左操作数
    Node *rhs;     // 右操作数
    int val;       // 如果类型是 ND_NUM，存储其值
};

static Node *new_node(NodeKind kind) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    return node;
}

static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs) {
    Node *node = new_node(kind);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

static Node *new_unary(NodeKind kind, Node *expr) {
    Node *node = new_node(kind);
    node->lhs = expr;
    return node;
}

static Node *new_num(int val) {
    Node *node = new_node(ND_NUM);
    node->val = val;
    return node;
}

static Node *expr(Token **rest, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *unary(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);

// expr = mul ("+" mul | "-" mul)*
static Node *expr(Token **rest, Token *tok) {
    Node *node = mul(&tok, tok);

    for (;;) {
        if (equal(tok, "+")) {
            node = new_binary(ND_ADD, node, mul(&tok, tok->next));
            continue;
        }

        if (equal(tok, "-")) {
            node = new_binary(ND_SUB, node, mul(&tok, tok->next));
            continue;
        }

        *rest = tok;
        return node;
    }
}

// mul = unary ("*" unary | "/" unary)*
static Node *mul(Token **rest, Token *tok) {
    Node *node = unary(&tok, tok);

    for (;;) {
        if (equal(tok, "*")) {
            node = new_binary(ND_MUL, node, unary(&tok, tok->next));
            continue;
        }

        if (equal(tok, "/")) {
            node = new_binary(ND_DIV, node, unary(&tok, tok->next));
            continue;
        }

        *rest = tok;
        return node;
    }
}

// unary = ("+" | "-") unary
//       | primary
static Node *unary(Token **rest, Token *tok) {
    if (equal(tok, "+"))
        return unary(rest, tok->next);

    if (equal(tok, "-"))
        return new_unary(ND_NEG, unary(rest, tok->next));

    return primary(rest, tok);
}

// primary = "(" expr ")" | num
static Node *primary(Token **rest, Token *tok) {
    if (equal(tok, "(")) {
        Node *node = expr(&tok, tok->next);
        *rest = skip(tok, ")");
        return node;
    }

    if (tok->kind == TK_NUM) {
        Node *node = new_num(tok->val);
        *rest = tok->next;
        return node;
    }

    error_tok(tok, "expected an expression");
}

//
// 代码生成器
//

static int depth;

static void push(void) {
    printf("    str x0, [sp, #-16]!\n");
    depth++;
}

static void pop(char *arg) {
    printf("    ldr %s, [sp], #16\n", arg);
    depth--;
}

static void gen_expr(Node *node) {
    switch (node->kind) {
    case ND_NUM:
        printf("    mov x0, #%d\n", node->val);
        return;
    case ND_NEG:
        gen_expr(node->lhs);
        printf("    neg x0, x0\n");
        return;
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
    }

    error("invalid expression");
}

int main(int argc, char **argv) {
    if (argc != 2)
        error("%s: invalid number of arguments", argv[0]);

    // 词法分析和语法分析
    current_input = argv[1];
    Token *tok = tokenize();
    Node *node = expr(&tok, tok);

    if (tok->kind != TK_EOF)
        error_tok(tok, "extra token");

    printf("    .global main\n");
    printf("main:\n");

    // 遍历 AST 生成汇编
    gen_expr(node);
    printf("    ret\n");

    assert(depth == 0);
    return 0;
}
