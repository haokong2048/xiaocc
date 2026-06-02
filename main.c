#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// 报告错误并退出
static void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

// 判断当前 Token 是否匹配字符串 op
static bool equal(Token *tok, char *op) {
    return memcmp(tok->loc, op, tok->len) == 0 && op[tok->len] == '\0';
}

// 确保当前 Token 为 s
static Token *skip(Token *tok, char *s) {
    if (!equal(tok, s))
        error("expected '%s'", s);
    return tok->next;
}

// 确保当前 Token 为 TK_NUM
static int get_number(Token *tok) {
    if (tok->kind != TK_NUM)
        error("expected a number");
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

// 对字符串 p 进行词法分析，返回 Token 链表
static Token *tokenize(char *p) {
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
        if (*p == '+' || *p == '-') {
            cur = cur->next = new_token(TK_PUNCT, p, p + 1);
            p++;
            continue;
        }

        error("invalid token");
    }

    cur = cur->next = new_token(TK_EOF, p, p);
    return head.next;
}

int main(int argc, char **argv) {
    if (argc != 2)
        error("%s: invalid number of arguments", argv[0]);

    Token *tok = tokenize(argv[1]);

    printf("    .global main\n");
    printf("main:\n");

    // 第一个 Token 必须是数字
    printf("    mov x0, #%d\n", get_number(tok));
    tok = tok->next;

    // 后续为 '+ <数字>' 或 '- <数字>' 的重复
    while (tok->kind != TK_EOF) {
        if (equal(tok, "+")) {
            printf("    add x0, x0, #%d\n", get_number(tok->next));
            tok = tok->next->next;
            continue;
        }

        tok = skip(tok, "-");
        printf("    sub x0, x0, #%d\n", get_number(tok));
        tok = tok->next;
    }

    printf("    ret\n");
    return 0;
}
