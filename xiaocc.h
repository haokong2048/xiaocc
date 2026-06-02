#ifndef XIAOCC_H
#define XIAOCC_H

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// tokenize.c
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

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *op);
Token *skip(Token *tok, char *op);
Token *tokenize(char *input);

//
// parse.c
//

typedef enum {
    ND_ADD,       // +
    ND_SUB,       // -
    ND_MUL,       // *
    ND_DIV,       // /
    ND_NEG,       // 一元负号
    ND_EQ,        // ==
    ND_NE,        // !=
    ND_LT,        // <
    ND_LE,        // <=
    ND_EXPR_STMT, // 表达式语句
    ND_NUM,       // 整数
} NodeKind;

// AST 节点类型
typedef struct Node Node;
struct Node {
    NodeKind kind; // 节点类型
    Node *next;    // 下一个节点
    Node *lhs;     // 左操作数
    Node *rhs;     // 右操作数
    int val;       // 如果类型是 ND_NUM，存储其值
};

Node *parse(Token *tok);

//
// codegen.c
//

void codegen(Node *node);

#endif
