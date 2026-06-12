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
#include <strings.h>
#include <libgen.h>

#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

typedef struct Type Type;
typedef struct Node Node;
typedef struct Member Member;
typedef struct Relocation Relocation;
typedef struct Hideset Hideset;

typedef struct {
    char **data;
    int len;
    int cap;
} StringArray;

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

typedef struct {
    char *name;
    int file_no;
    char *contents;
} File;

// Token 类型
typedef struct Token Token;
struct Token {
    TokenKind kind; // Token 类型
    Token *next;    // 下一个 Token
    int64_t val;    // 如果类型是 TK_NUM，存储其值
    double fval;    // 如果类型是 TK_NUM 且为浮点数
    char *loc;      // Token 位置
    int len;        // Token 长度
    Type *ty;       // 如果类型是 TK_NUM 或 TK_STR 则使用
    char *str;      // 字符串字面量内容（含结尾 '\0'）

    File *file;       // 源文件位置
    int line_no;      // 行号
    bool at_bol;      // 此 token 是否在行首
    bool has_space;   // 此 token 前是否有空白字符
    Hideset *hideset; // 用于宏展开
    Token *origin;    // 如果来自宏展开，指向原始 token
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
void warn_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *op);
Token *skip(Token *tok, char *op);
bool consume(Token **rest, Token *tok, char *str);
void convert_keywords(Token *tok);
File **get_input_files(void);
File *new_file(char *name, int file_no, char *contents);
Token *tokenize(File *file);
Token *tokenize_file(char *filename);

#define unreachable() \
    error("internal error at %s:%d", __FILE__, __LINE__)

//
// preprocess.c
//

Token *preprocess(Token *tok);

//
// parse.c
//

// 变量或函数
typedef struct Obj Obj;
struct Obj {
    Obj *next;
    char *name;    // 变量名
    Type *ty;      // 类型
    Token *tok;    // 代表 Token
    bool is_local; // 局部变量还是全局变量/函数
    int align;     // 对齐

    // 局部变量
    int offset; // 相对于帧指针的偏移量

    // 全局变量或函数
    bool is_function;
    bool is_definition;
    bool is_static;

    // 全局变量
    char *init_data;
    Relocation *rel;

    // 函数
    Obj *params;
    Node *body;
    Obj *locals;
    Obj *va_area;
    int stack_size;
};

// 全局变量可以通过常量表达式或指向其他全局变量的指针来初始化。
// 此结构体表示后者。
struct Relocation {
    Relocation *next;
    int offset;
    char *label;
    long addend;
};

// AST 节点
typedef enum {
    ND_NULL_EXPR, // 空操作
    ND_ADD,       // +
    ND_SUB,       // -
    ND_MUL,       // *
    ND_DIV,       // /
    ND_NEG,       // 一元负号
    ND_MOD,       // %
    ND_BITAND,    // &
    ND_BITOR,     // |
    ND_BITXOR,    // ^
    ND_SHL,       // <<
    ND_SHR,       // >>
    ND_EQ,        // ==
    ND_NE,        // !=
    ND_LT,        // <
    ND_LE,        // <=
    ND_ASSIGN,    // =
    ND_COND,      // ?:
    ND_COMMA,     // ,
    ND_MEMBER,    // . (结构体成员访问)
    ND_ADDR,      // 一元 &
    ND_DEREF,     // 一元 *
    ND_NOT,       // !
    ND_BITNOT,    // ~
    ND_LOGAND,    // &&
    ND_LOGOR,     // ||
    ND_RETURN,    // "return"
    ND_IF,        // "if"
    ND_FOR,       // "for" or "while"
    ND_DO,        // "do"
    ND_SWITCH,    // "switch"
    ND_CASE,      // "case"
    ND_BLOCK,     // { ... }
    ND_GOTO,      // "goto"
    ND_LABEL,     // 标签语句
    ND_FUNCALL,   // 函数调用
    ND_EXPR_STMT, // 表达式语句
    ND_STMT_EXPR, // 语句表达式 (GNU 扩展)
    ND_VAR,       // 变量
    ND_NUM,       // 整数
    ND_CAST,      // 类型转换
    ND_MEMZERO,   // 将栈变量清零
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

    // "break" 和 "continue" 标签
    char *brk_label;
    char *cont_label;

    // 块或语句表达式
    Node *body;

    // 结构体成员访问
    Member *member;

    // 函数调用
    Type *func_ty;
    Node *args;

    // goto 或标签语句
    char *label;
    char *unique_label;
    Node *goto_next;

    // switch-case
    Node *case_next;
    Node *default_case;

    // 变量
    Obj *var;

    // 数字字面量
    int64_t val;
    double fval;
};

Node *new_cast(Node *expr, Type *ty);
int64_t const_expr(Token **rest, Token *tok);
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
    TY_FLOAT,
    TY_DOUBLE,
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
    bool is_unsigned; // 无符号还是带符号

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
    Token *name_pos;

    // 数组
    int array_len;

    // 结构体
    Member *members;
    bool is_flexible;

    // 函数类型
    Type *return_ty;
    Type *params;
    bool is_variadic;
    Type *next;
};

// 结构体成员
struct Member {
    Member *next;
    Type *ty;
    Token *tok;   // 用于错误信息
    Token *name;
    int idx;
    int align;
    int offset;
};

extern Type *ty_void;
extern Type *ty_bool;

extern Type *ty_char;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;

extern Type *ty_uchar;
extern Type *ty_ushort;
extern Type *ty_uint;
extern Type *ty_ulong;

extern Type *ty_float;
extern Type *ty_double;

bool is_integer(Type *ty);
bool is_flonum(Type *ty);
bool is_numeric(Type *ty);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
Type *enum_type(void);
Type *struct_type(void);
void add_type(Node *node);

//
// codegen.c
//

void codegen(Obj *prog, FILE *out);
int align_to(int n, int align);

//
// main.c
//

bool file_exists(char *path);
void strarray_push(StringArray *arr, char *s);

extern StringArray include_paths;

#endif
