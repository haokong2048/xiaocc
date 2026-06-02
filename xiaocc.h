#ifndef XIAOCC_H
#define XIAOCC_H

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node Node;

//
// tokenize.c
//

// Token
typedef enum {
    TK_IDENT,   // 标识符
    TK_PUNCT,   // 标点符号
    TK_KEYWORD, // 关键字
    TK_NUM,     // 数字字面量
    TK_EOF,     // 文件结束标记
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

// 局部变量
typedef struct Obj Obj;
struct Obj {
    Obj *next;
    char *name; // 变量名
    int offset; // 相对于 RBP 的偏移量
};

// 函数
typedef struct Function Function;
struct Function {
    Node *body;
    Obj *locals;
    int stack_size;
};

// AST 节点
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
    ND_ASSIGN,    // =
    ND_RETURN,    // "return"
    ND_IF,        // "if"
    ND_FOR,       // "for" or "while"
    ND_BLOCK,     // { ... }
    ND_EXPR_STMT, // 表达式语句
    ND_VAR,       // 变量
    ND_NUM,       // 整数
} NodeKind;

// AST 节点类型
struct Node {
    NodeKind kind; // 节点类型
    Node *next;    // 下一个节点
    Node *lhs;     // 左操作数
    Node *rhs;     // 右操作数

    // "if" 或 "for" 语句
    Node *cond;
    Node *then;
    Node *els;
    Node *init;
    Node *inc;

    // 块
    Node *body;

    Obj *var;      // 如果类型是 ND_VAR，存储变量引用
    int val;       // 如果类型是 ND_NUM，存储其值
};

Function *parse(Token *tok);

//
// codegen.c
//

void codegen(Function *prog);

#endif
