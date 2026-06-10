#ifndef XIAOCC_H
#define XIAOCC_H

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Type Type;
typedef struct Node Node;
typedef struct Member Member;

//
// strings.c
//

char *format(char *fmt, ...);

//
// tokenize.c
//

// Token
typedef enum {
    TK_IDENT,   // 标识符
    TK_PUNCT,   // 标点符号
    TK_KEYWORD, // 关键字
    TK_STR,     // 字符串字面量
    TK_NUM,     // 数字字面量
    TK_EOF,     // 文件结束标记
} TokenKind;

// Token 类型
typedef struct Token Token;
struct Token {
    TokenKind kind; // Token 类型
    Token *next;    // 下一个 Token
    int64_t val;    // 如果类型是 TK_NUM，存储其值
    char *loc;      // Token 位置
    int len;        // Token 长度
    Type *ty;       // 如果类型是 TK_STR 则使用
    char *str;      // 字符串字面量内容（含结尾 '\0'）

    int line_no;    // 行号
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *op);
Token *skip(Token *tok, char *op);
bool consume(Token **rest, Token *tok, char *str);
Token *tokenize_file(char *filename);

#define unreachable() \
    error("internal error at %s:%d", __FILE__, __LINE__)

//
// parse.c
//

// 变量或函数
typedef struct Obj Obj;
struct Obj {
    Obj *next;
    char *name;    // 变量名
    Type *ty;      // 类型
    bool is_local; // 局部变量还是全局变量/函数

    // 局部变量
    int offset; // 相对于帧指针的偏移量

    // 全局变量或函数
    bool is_function;
    bool is_definition;
    bool is_static;

    // 全局变量
    char *init_data;

    // 函数
    Obj *params;
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
    ND_COMMA,     // ,
    ND_MEMBER,    // . (结构体成员访问)
    ND_ADDR,      // 一元 &
    ND_DEREF,     // 一元 *
    ND_RETURN,    // "return"
    ND_IF,        // "if"
    ND_FOR,       // "for" or "while"
    ND_BLOCK,     // { ... }
    ND_FUNCALL,   // 函数调用
    ND_EXPR_STMT, // 表达式语句
    ND_STMT_EXPR, // 语句表达式 (GNU 扩展)
    ND_VAR,       // 变量
    ND_NUM,       // 整数
    ND_CAST,      // 类型转换
} NodeKind;

// AST 节点类型
struct Node {
    NodeKind kind; // 节点类型
    Node *next;    // 下一个节点
    Token *tok;    // 代表 Token
    Type *ty;      // 类型，例如 int 或指向 int 的指针
    Node *lhs;     // 左操作数
    Node *rhs;     // 右操作数

    // "if" 或 "for" 语句
    Node *cond;
    Node *then;
    Node *els;
    Node *init;
    Node *inc;

    // 块或语句表达式
    Node *body;

    // 结构体成员访问
    Member *member;

    // 函数调用
    char *funcname;
    Type *func_ty;
    Node *args;

    Obj *var;      // 如果类型是 ND_VAR，存储变量引用
    int64_t val;   // 如果类型是 ND_NUM，存储其值
};

Node *new_cast(Node *expr, Type *ty);
Obj *parse(Token *tok);

//
// type.c
//

typedef enum {
    TY_VOID,
    TY_BOOL,
    TY_CHAR,
    TY_SHORT,
    TY_INT,
    TY_LONG,
    TY_ENUM,
    TY_PTR,
    TY_FUNC,
    TY_ARRAY,
    TY_STRUCT,
    TY_UNION,
} TypeKind;

struct Type {
    TypeKind kind;
    int size;      // sizeof() 值
    int align;     // 对齐

    // 指针或数组的基类型。我们有意使用同一个成员
    // 来表示 C 语言中的指针/数组二元性。
    //
    // 在许多需要指针的上下文中，我们检查此成员
    // 而非 "kind" 成员来判断类型是否为指针。
    // 这意味着在许多上下文中，"array of T"
    // 会被自然地当作 "pointer to T" 处理，符合
    // C 语言规范的要求。
    Type *base;

    // 声明
    Token *name;

    // 数组
    int array_len;

    // 结构体
    Member *members;

    // 函数类型
    Type *return_ty;
    Type *params;
    Type *next;
};

// 结构体成员
struct Member {
    Member *next;
    Type *ty;
    Token *name;
    int offset;
};

extern Type *ty_void;
extern Type *ty_bool;

extern Type *ty_char;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;

bool is_integer(Type *ty);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *enum_type(void);
void add_type(Node *node);

//
// codegen.c
//

void codegen(Obj *prog, FILE *out);
int align_to(int n, int align);

#endif
