// 该文件包含一个 C 语言递归下降解析器。
//
// 该文件中的大多数函数名称表示它们应该从
// 输入 token 列表中解析的语法符号。例如，stmt()
// 负责从 token 列表中读取语句，然后构建一个
// 表示该语句的 AST 节点。
//
// 每个函数概念上返回两个值：一个 AST 节点和
// 输入 token 的剩余部分。由于 C 语言不支持
// 多返回值，剩余 token 通过指针参数返回给
// 调用者。
//
// 输入 token 通过链表表示。与许多递归下降
// 解析器不同，我们没有"输入 token 流"的概念。
// 大多数解析函数不改变解析器的全局状态。
// 因此在这个解析器中很容易向前看任意数量的 token。

#include "xiaocc.h"

// 局部变量、全局变量、类型定义或枚举常量的作用域
typedef struct VarScope VarScope;
struct VarScope {
    VarScope *next;
    char *name;
    Obj *var;
    Type *type_def;
    Type *enum_ty;
    int enum_val;
};

// 结构体、联合体或枚举的标签作用域
typedef struct TagScope TagScope;
struct TagScope {
    TagScope *next;
    char *name;
    Type *ty;
};

// 表示一个块作用域
typedef struct Scope Scope;
struct Scope {
    Scope *next;

    // C 语言有两类块作用域：一个用于变量/typedef，一个用于结构体/联合体/枚举标签
    VarScope *vars;
    TagScope *tags;
};

// 变量属性，如 typedef 或 static
typedef struct {
    bool is_typedef;
    bool is_static;
    bool is_extern;
    int align;
} VarAttr;

// 此结构体表示变量初始化器。由于初始化器可以嵌套
//（例如 `int x[2][2] = {{1, 2}, {3, 4}}`），此结构体
// 是一棵树形数据结构。
typedef struct Initializer Initializer;
struct Initializer {
    Initializer *next;
    Type *ty;
    Token *tok;
    bool is_flexible;

    // 如果不是聚合类型且有初始化器，
    // `expr` 包含初始化表达式。
    Node *expr;

    // 如果是聚合类型（如数组或结构体）的初始化器，
    // `children` 包含其子元素的初始化器。
    Initializer **children;
};

// 用于局部变量初始化器
typedef struct InitDesg InitDesg;
struct InitDesg {
    InitDesg *next;
    int idx;
    Member *member;
    Obj *var;
};

// 解析过程中创建的所有局部变量实例
// 都被累积到这个链表中
static Obj *locals;

// 同样地，全局变量也被累积到这个链表中
static Obj *globals;

// 指向解析器当前正在解析的函数对象
static Obj *current_fn;

// 当前函数中所有 goto 语句和标签的链表
static Node *gotos;
static Node *labels;

// 当前 "break" 和 "continue" 跳转目标
static char *brk_label;
static char *cont_label;

// 指向表示 switch 的节点（如果正在解析 switch 语句）。
// 否则为 NULL。
static Node *current_switch;

static Scope *scope = &(Scope){};

static bool is_typename(Token *tok);
static Type *declspec(Token **rest, Token *tok, VarAttr *attr);
static Type *typename(Token **rest, Token *tok);
static Type *type_suffix(Token **rest, Token *tok, Type *ty);
static Type *declarator(Token **rest, Token *tok, Type *ty);
static Node *declaration(Token **rest, Token *tok, Type *basety, VarAttr *attr);
static void initializer2(Token **rest, Token *tok, Initializer *init);
static Initializer *initializer(Token **rest, Token *tok, Type *ty, Type **new_ty);
static Node *lvar_initializer(Token **rest, Token *tok, Obj *var);
static void gvar_initializer(Token **rest, Token *tok, Obj *var);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static int64_t eval(Node *node);
static int64_t eval2(Node *node, char **label);
static int64_t eval_rval(Node *node, char **label);
static Node *expr_stmt(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *conditional(Token **rest, Token *tok);
static Node *logor(Token **rest, Token *tok);
static int64_t const_expr(Token **rest, Token *tok);
static Node *logand(Token **rest, Token *tok);
static Node *bitor(Token **rest, Token *tok);
static Node *bitxor(Token **rest, Token *tok);
static Node *bitand(Token **rest, Token *tok);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *shift(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *new_add(Node *lhs, Node *rhs, Token *tok);
static Node *new_sub(Node *lhs, Node *rhs, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static Type *struct_decl(Token **rest, Token *tok);
static Type *union_decl(Token **rest, Token *tok);
static Type *enum_specifier(Token **rest, Token *tok);
static Node *postfix(Token **rest, Token *tok);
static Node *unary(Token **rest, Token *tok);
static Node *funcall(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);
static Token *parse_typedef(Token *tok, Type *basety);
static bool is_function(Token *tok);
static Token *function(Token *tok, Type *basety, VarAttr *attr);
static Token *global_variable(Token *tok, Type *basety, VarAttr *attr);

static void enter_scope(void) {
    Scope *sc = calloc(1, sizeof(Scope));
    sc->next = scope;
    scope = sc;
}

static void leave_scope(void) {
    scope = scope->next;
}

// 按名称查找变量
static VarScope *find_var(Token *tok) {
    for (Scope *sc = scope; sc; sc = sc->next)
        for (VarScope *sc2 = sc->vars; sc2; sc2 = sc2->next)
            if (equal(tok, sc2->name))
                return sc2;
    return NULL;
}

static Type *find_tag(Token *tok) {
    for (Scope *sc = scope; sc; sc = sc->next)
        for (TagScope *sc2 = sc->tags; sc2; sc2 = sc2->next)
            if (equal(tok, sc2->name))
                return sc2->ty;
    return NULL;
}

static Node *new_node(NodeKind kind, Token *tok) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    node->tok = tok;
    return node;
}

static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
    Node *node = new_node(kind, tok);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

static Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
    Node *node = new_node(kind, tok);
    node->lhs = expr;
    return node;
}

static Node *new_num(int64_t val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->val = val;
    return node;
}

static Node *new_long(int64_t val, Token *tok) {
    Node *node = new_node(ND_NUM, tok);
    node->val = val;
    node->ty = ty_long;
    return node;
}

static Node *new_var_node(Obj *var, Token *tok) {
    Node *node = new_node(ND_VAR, tok);
    node->var = var;
    return node;
}

Node *new_cast(Node *expr, Type *ty) {
    add_type(expr);

    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_CAST;
    node->tok = expr->tok;
    node->lhs = expr;
    node->ty = copy_type(ty);
    return node;
}

static VarScope *push_scope(char *name) {
    VarScope *sc = calloc(1, sizeof(VarScope));
    sc->name = name;
    sc->next = scope->vars;
    scope->vars = sc;
    return sc;
}

static Initializer *new_initializer(Type *ty, bool is_flexible) {
    Initializer *init = calloc(1, sizeof(Initializer));
    init->ty = ty;

    if (ty->kind == TY_ARRAY) {
        if (is_flexible && ty->size < 0) {
            init->is_flexible = true;
            return init;
        }

        init->children = calloc(ty->array_len, sizeof(Initializer *));
        for (int i = 0; i < ty->array_len; i++)
            init->children[i] = new_initializer(ty->base, false);
        return init;
    }

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        // 计算结构体/联合体成员数量
        int len = 0;
        for (Member *mem = ty->members; mem; mem = mem->next)
            len++;

        init->children = calloc(len, sizeof(Initializer *));

        for (Member *mem = ty->members; mem; mem = mem->next) {
            if (is_flexible && ty->is_flexible && !mem->next) {
                Initializer *child = calloc(1, sizeof(Initializer));
                child->ty = mem->ty;
                child->is_flexible = true;
                init->children[mem->idx] = child;
            } else {
                init->children[mem->idx] = new_initializer(mem->ty, false);
            }
        }
        return init;
    }

    return init;
}

static Obj *new_var(char *name, Type *ty) {
    Obj *var = calloc(1, sizeof(Obj));
    var->name = name;
    var->ty = ty;
    var->align = ty->align;
    push_scope(name)->var = var;
    return var;
}

static Obj *new_lvar(char *name, Type *ty) {
    Obj *var = new_var(name, ty);
    var->is_local = true;
    var->next = locals;
    locals = var;
    return var;
}

static Obj *new_gvar(char *name, Type *ty) {
    Obj *var = new_var(name, ty);
    var->next = globals;
    var->is_static = true;
    var->is_definition = true;
    globals = var;
    return var;
}

static char *new_unique_name(void) {
    static int id = 0;
    return format(".L..%d", id++);
}

static Obj *new_anon_gvar(Type *ty) {
    return new_gvar(new_unique_name(), ty);
}

static Obj *new_string_literal(char *p, Type *ty) {
    Obj *var = new_anon_gvar(ty);
    var->init_data = p;
    return var;
}

static char *get_ident(Token *tok) {
    if (tok->kind != TK_IDENT)
        error_tok(tok, "expected an identifier");
    return strndup(tok->loc, tok->len);
}

static Type *find_typedef(Token *tok) {
    if (tok->kind == TK_IDENT) {
        VarScope *sc = find_var(tok);
        if (sc)
            return sc->type_def;
    }
    return NULL;
}

static void push_tag_scope(Token *tok, Type *ty) {
    TagScope *sc = calloc(1, sizeof(TagScope));
    sc->name = strndup(tok->loc, tok->len);
    sc->ty = ty;
    sc->next = scope->tags;
    scope->tags = sc;
}

// declspec = ("void" | "_Bool" | "char" | "short" | "int" | "long"
//             | "typedef" | "static" | "extern"
//             | struct-decl | union-decl | typedef-name
//             | enum-specifier)+
//
// 类型说明符中的类型名顺序无关紧要。例如，
// `int long static` 与 `static long int` 含义相同。
// 也可以写成 `static long`，因为如果指定了 `long` 或 `short`，
// 就可以省略 `int`。但像 `char int` 这样的组合不是有效的类型说明符。
// 我们只接受有限组合的类型名。
//
// 在此函数中，我们记录每个类型名出现的次数，
// 同时维护一个表示当前类型的"当前"类型对象。
// 当遇到非类型名 token 时，返回当前类型对象。
static Type *declspec(Token **rest, Token *tok, VarAttr *attr) {
    // 使用单个整数作为所有类型名的计数器。
    // 例如，第 0、1 位表示关键字 "void" 出现的次数。
    // 这样我们就可以使用下面所示的 switch 语句。
    enum {
        VOID  = 1 << 0,
        BOOL  = 1 << 2,
        CHAR  = 1 << 4,
        SHORT = 1 << 6,
        INT   = 1 << 8,
        LONG   = 1 << 10,
        OTHER  = 1 << 12,
        SIGNED   = 1 << 13,
        UNSIGNED = 1 << 14,
    };

    Type *ty = ty_int;
    int counter = 0;

    while (is_typename(tok)) {
        // 处理存储类说明符
        if (equal(tok, "typedef") || equal(tok, "static") || equal(tok, "extern")) {
            if (!attr)
                error_tok(tok, "storage class specifier is not allowed in this context");

            if (equal(tok, "typedef"))
                attr->is_typedef = true;
            else if (equal(tok, "static"))
                attr->is_static = true;
            else
                attr->is_extern = true;

            if (attr->is_typedef && attr->is_static + attr->is_extern > 1)
                error_tok(tok, "typedef 不能与 static 或 extern 同时使用");
            tok = tok->next;
            continue;
        }

        // 处理 _Alignas
        if (equal(tok, "_Alignas")) {
            if (!attr)
                error_tok(tok, "_Alignas 不允许在此上下文中使用");
            tok = skip(tok->next, "(");

            if (is_typename(tok))
                attr->align = typename(&tok, tok)->align;
            else
                attr->align = const_expr(&tok, tok);
            tok = skip(tok, ")");
            continue;
        }

        // 处理用户自定义类型
        Type *ty2 = find_typedef(tok);
        if (equal(tok, "struct") || equal(tok, "union") || equal(tok, "enum") || ty2) {
            if (counter)
                break;

            if (equal(tok, "struct")) {
                ty = struct_decl(&tok, tok->next);
            } else if (equal(tok, "union")) {
                ty = union_decl(&tok, tok->next);
            } else if (equal(tok, "enum")) {
                ty = enum_specifier(&tok, tok->next);
            } else {
                ty = ty2;
                tok = tok->next;
            }

            counter += OTHER;
            continue;
        }

        // 处理内建类型。
        if (equal(tok, "void"))
            counter += VOID;
        else if (equal(tok, "_Bool"))
            counter += BOOL;
        else if (equal(tok, "char"))
            counter += CHAR;
        else if (equal(tok, "short"))
            counter += SHORT;
        else if (equal(tok, "int"))
            counter += INT;
        else if (equal(tok, "long"))
            counter += LONG;
        else if (equal(tok, "signed"))
            counter |= SIGNED;
        else if (equal(tok, "unsigned"))
            counter |= UNSIGNED;
        else
            unreachable();

        switch (counter) {
        case VOID:
            ty = ty_void;
            break;
        case BOOL:
            ty = ty_bool;
            break;
        case CHAR:
        case SIGNED + CHAR:
            ty = ty_char;
            break;
        case UNSIGNED + CHAR:
            ty = ty_uchar;
            break;
        case SHORT:
        case SHORT + INT:
        case SIGNED + SHORT:
        case SIGNED + SHORT + INT:
            ty = ty_short;
            break;
        case UNSIGNED + SHORT:
        case UNSIGNED + SHORT + INT:
            ty = ty_ushort;
            break;
        case INT:
        case SIGNED:
        case SIGNED + INT:
            ty = ty_int;
            break;
        case UNSIGNED:
        case UNSIGNED + INT:
            ty = ty_uint;
            break;
        case LONG:
        case LONG + INT:
        case LONG + LONG:
        case LONG + LONG + INT:
        case SIGNED + LONG:
        case SIGNED + LONG + INT:
        case SIGNED + LONG + LONG:
        case SIGNED + LONG + LONG + INT:
            ty = ty_long;
            break;
        case UNSIGNED + LONG:
        case UNSIGNED + LONG + INT:
        case UNSIGNED + LONG + LONG:
        case UNSIGNED + LONG + LONG + INT:
            ty = ty_ulong;
            break;
        default:
            error_tok(tok, "invalid type");
        }

        tok = tok->next;
    }

    *rest = tok;
    return ty;
}

// func-params = ("void" | param ("," param)*)? ")"
// param       = declspec declarator
static Type *func_params(Token **rest, Token *tok, Type *ty) {
    if (equal(tok, "void") && equal(tok->next, ")")) {
        *rest = tok->next->next;
        return func_type(ty);
    }

    Type head = {};
    Type *cur = &head;
    bool is_variadic = false;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(tok, ",");

        if (equal(tok, "...")) {
            is_variadic = true;
            tok = tok->next;
            skip(tok, ")");
            break;
        }

        Type *ty2 = declspec(&tok, tok, NULL);
        ty2 = declarator(&tok, tok, ty2);

        // 仅在参数上下文中，"array of T" 会被转换为 "pointer to T"。
        // 例如，*argv[] 会被转换为 **argv。
        if (ty2->kind == TY_ARRAY) {
            Token *name = ty2->name;
            ty2 = pointer_to(ty2->base);
            ty2->name = name;
        }

        cur = cur->next = copy_type(ty2);
    }

    if (cur == &head)
        is_variadic = true;

    ty = func_type(ty);
    ty->params = head.next;
    ty->is_variadic = is_variadic;
    *rest = tok->next;
    return ty;
}

// array-dimensions = const-expr? "]" type-suffix
static Type *array_dimensions(Token **rest, Token *tok, Type *ty) {
    if (equal(tok, "]")) {
        ty = type_suffix(rest, tok->next, ty);
        return array_of(ty, -1);
    }

    int sz = const_expr(&tok, tok);
    tok = skip(tok, "]");
    ty = type_suffix(rest, tok, ty);
    return array_of(ty, sz);
}

// type-suffix = "(" func-params
//             | "[" array-dimensions
//             | ε
static Type *type_suffix(Token **rest, Token *tok, Type *ty) {
    if (equal(tok, "("))
        return func_params(rest, tok->next, ty);

    if (equal(tok, "["))
        return array_dimensions(rest, tok->next, ty);

    *rest = tok;
    return ty;
}

// declarator = "*"* ("(" ident ")" | "(" declarator ")" | ident) type-suffix
static Type *declarator(Token **rest, Token *tok, Type *ty) {
    while (consume(&tok, tok, "*"))
        ty = pointer_to(ty);

    if (equal(tok, "(")) {
        Token *start = tok;
        Type dummy = {};
        declarator(&tok, start->next, &dummy);
        tok = skip(tok, ")");
        ty = type_suffix(rest, tok, ty);
        return declarator(&tok, start->next, ty);
    }

    if (tok->kind != TK_IDENT)
        error_tok(tok, "expected a variable name");

    ty = type_suffix(rest, tok->next, ty);
    ty->name = tok;
    return ty;
}

// abstract-declarator = "*"* ("(" abstract-declarator ")")? type-suffix
static Type *abstract_declarator(Token **rest, Token *tok, Type *ty) {
    while (equal(tok, "*")) {
        ty = pointer_to(ty);
        tok = tok->next;
    }

    if (equal(tok, "(")) {
        Token *start = tok;
        Type dummy = {};
        abstract_declarator(&tok, start->next, &dummy);
        tok = skip(tok, ")");
        ty = type_suffix(rest, tok, ty);
        return abstract_declarator(&tok, start->next, ty);
    }

    return type_suffix(rest, tok, ty);
}

// type-name = declspec abstract-declarator
static Type *typename(Token **rest, Token *tok) {
    Type *ty = declspec(&tok, tok, NULL);
    return abstract_declarator(rest, tok, ty);
}

static bool is_end(Token *tok) {
    return equal(tok, "}") || (equal(tok, ",") && equal(tok->next, "}"));
}

static bool consume_end(Token **rest, Token *tok) {
    if (equal(tok, "}")) {
        *rest = tok->next;
        return true;
    }

    if (equal(tok, ",") && equal(tok->next, "}")) {
        *rest = tok->next->next;
        return true;
    }

    return false;
}

// enum-specifier = ident? "{" enum-list? "}"
//                | ident ("{" enum-list? "}")?
//
// enum-list      = ident ("=" num)? ("," ident ("=" num)?)* ","?
static Type *enum_specifier(Token **rest, Token *tok) {
    Type *ty = enum_type();

    // 读取枚举标签
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    if (tag && !equal(tok, "{")) {
        Type *ty = find_tag(tag);
        if (!ty)
            error_tok(tag, "未知的枚举类型");
        if (ty->kind != TY_ENUM)
            error_tok(tag, "不是枚举标签");
        *rest = tok;
        return ty;
    }

    tok = skip(tok, "{");

    // 读取枚举列表
    int i = 0;
    int val = 0;
    while (!consume_end(rest, tok)) {
        if (i++ > 0)
            tok = skip(tok, ",");

        char *name = get_ident(tok);
        tok = tok->next;

        if (equal(tok, "="))
            val = const_expr(&tok, tok->next);

        VarScope *sc = push_scope(name);
        sc->enum_ty = ty;
        sc->enum_val = val++;
    }

    if (tag)
        push_tag_scope(tag, ty);
    return ty;
}

// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static Node *declaration(Token **rest, Token *tok, Type *basety, VarAttr *attr) {
    Node head = {};
    Node *cur = &head;
    int i = 0;

    while (!equal(tok, ";")) {
        if (i++ > 0)
            tok = skip(tok, ",");

        Type *ty = declarator(&tok, tok, basety);
        if (ty->kind == TY_VOID)
            error_tok(tok, "variable declared void");

        if (attr && attr->is_static) {
            // 静态局部变量
            Obj *var = new_anon_gvar(ty);
            push_scope(get_ident(ty->name))->var = var;
            if (equal(tok, "="))
                gvar_initializer(&tok, tok->next, var);
            continue;
        }

        Obj *var = new_lvar(get_ident(ty->name), ty);
        if (attr && attr->align)
            var->align = attr->align;

        if (equal(tok, "=")) {
            Node *expr = lvar_initializer(&tok, tok->next, var);
            cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
        }

        if (var->ty->size < 0)
            error_tok(ty->name, "variable has incomplete type");
        if (var->ty->kind == TY_VOID)
            error_tok(ty->name, "variable declared void");
    }

    Node *node = new_node(ND_BLOCK, tok);
    node->body = head.next;
    *rest = tok->next;
    return node;
}

static Token *skip_excess_element(Token *tok) {
    if (equal(tok, "{")) {
        tok = skip_excess_element(tok->next);
        return skip(tok, "}");
    }

    assign(&tok, tok);
    return tok;
}

// string-initializer = string-literal
static void string_initializer(Token **rest, Token *tok, Initializer *init) {
    if (init->is_flexible)
        *init = *new_initializer(array_of(init->ty->base, tok->ty->array_len), false);

    int len = MIN(init->ty->array_len, tok->ty->array_len);
    for (int i = 0; i < len; i++)
        init->children[i]->expr = new_num(tok->str[i], tok);
    *rest = tok->next;
}

static int count_array_init_elements(Token *tok, Type *ty) {
    Initializer *dummy = new_initializer(ty->base, false);
    int i = 0;

    for (; !consume_end(&tok, tok); i++) {
        if (i > 0)
            tok = skip(tok, ",");
        initializer2(&tok, tok, dummy);
    }
    return i;
}

// array-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void array_initializer1(Token **rest, Token *tok, Initializer *init) {
    tok = skip(tok, "{");

    if (init->is_flexible) {
        int len = count_array_init_elements(tok, init->ty);
        *init = *new_initializer(array_of(init->ty->base, len), false);
    }

    for (int i = 0; !consume_end(rest, tok); i++) {
        if (i > 0)
            tok = skip(tok, ",");

        if (i < init->ty->array_len)
            initializer2(&tok, tok, init->children[i]);
        else
            tok = skip_excess_element(tok);
    }
}

// array-initializer2 = initializer ("," initializer)*
static void array_initializer2(Token **rest, Token *tok, Initializer *init) {
    if (init->is_flexible) {
        int len = count_array_init_elements(tok, init->ty);
        *init = *new_initializer(array_of(init->ty->base, len), false);
    }

    for (int i = 0; i < init->ty->array_len && !is_end(tok); i++) {
        if (i > 0)
            tok = skip(tok, ",");
        initializer2(&tok, tok, init->children[i]);
    }
    *rest = tok;
}

static void union_initializer(Token **rest, Token *tok, Initializer *init) {
    // 与结构体不同，联合体初始化器只接受一个初始化器，
    // 且该初始化器初始化联合体的第一个成员。
    if (equal(tok, "{")) {
        initializer2(&tok, tok->next, init->children[0]);
        consume(&tok, tok, ",");
        *rest = skip(tok, "}");
    } else {
        initializer2(rest, tok, init->children[0]);
    }
}

// struct-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void struct_initializer1(Token **rest, Token *tok, Initializer *init) {
    tok = skip(tok, "{");

    Member *mem = init->ty->members;

    while (!consume_end(rest, tok)) {
        if (mem != init->ty->members)
            tok = skip(tok, ",");

        if (mem) {
            initializer2(&tok, tok, init->children[mem->idx]);
            mem = mem->next;
        } else {
            tok = skip_excess_element(tok);
        }
    }
}

// struct-initializer2 = initializer ("," initializer)*
static void struct_initializer2(Token **rest, Token *tok, Initializer *init) {
    bool first = true;

    for (Member *mem = init->ty->members; mem && !is_end(tok); mem = mem->next) {
        if (!first)
            tok = skip(tok, ",");
        first = false;
        initializer2(&tok, tok, init->children[mem->idx]);
    }
    *rest = tok;
}

// initializer = string-initializer | array-initializer
//             | struct-initializer | union-initializer
//             | assign
static void initializer2(Token **rest, Token *tok, Initializer *init) {
    if (init->ty->kind == TY_ARRAY && tok->kind == TK_STR) {
        string_initializer(rest, tok, init);
        return;
    }

    if (init->ty->kind == TY_ARRAY) {
        if (equal(tok, "{"))
            array_initializer1(rest, tok, init);
        else
            array_initializer2(rest, tok, init);
        return;
    }

    if (init->ty->kind == TY_STRUCT) {
        if (equal(tok, "{")) {
            struct_initializer1(rest, tok, init);
            return;
        }

        // 结构体可以用另一个结构体初始化。例如
        // `struct T x = y;` 其中 y 是 `struct T` 类型的变量。
        // 先处理这种情况。
        Node *expr = assign(rest, tok);
        add_type(expr);
        if (expr->ty->kind == TY_STRUCT) {
            init->expr = expr;
            return;
        }

        struct_initializer2(rest, tok, init);
        return;
    }

    if (init->ty->kind == TY_UNION) {
        union_initializer(rest, tok, init);
        return;
    }

    if (equal(tok, "{")) {
        // 标量变量的初始化器可以用大括号包围。
        // 例如 `int x = {3};`。处理这种情况。
        initializer2(&tok, tok->next, init);
        *rest = skip(tok, "}");
        return;
    }

    init->expr = assign(rest, tok);
}

static Type *copy_struct_type(Type *ty) {
    ty = copy_type(ty);

    Member head = {};
    Member *cur = &head;
    for (Member *mem = ty->members; mem; mem = mem->next) {
        Member *m = calloc(1, sizeof(Member));
        *m = *mem;
        cur = cur->next = m;
    }

    ty->members = head.next;
    return ty;
}

static Initializer *initializer(Token **rest, Token *tok, Type *ty, Type **new_ty) {
    Initializer *init = new_initializer(ty, true);
    initializer2(rest, tok, init);

    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->is_flexible) {
        ty = copy_struct_type(ty);

        Member *mem = ty->members;
        while (mem->next)
            mem = mem->next;
        mem->ty = init->children[mem->idx]->ty;
        ty->size += mem->ty->size;

        *new_ty = ty;
        return init;
    }

    *new_ty = init->ty;
    return init;
}

static Node *init_desg_expr(InitDesg *desg, Token *tok) {
    if (desg->var)
        return new_var_node(desg->var, tok);

    if (desg->member) {
        Node *node = new_unary(ND_MEMBER, init_desg_expr(desg->next, tok), tok);
        node->member = desg->member;
        return node;
    }

    Node *lhs = init_desg_expr(desg->next, tok);
    Node *rhs = new_num(desg->idx, tok);
    return new_unary(ND_DEREF, new_add(lhs, rhs, tok), tok);
}

static Node *create_lvar_init(Initializer *init, Type *ty, InitDesg *desg, Token *tok) {
    if (ty->kind == TY_ARRAY) {
        Node *node = new_node(ND_NULL_EXPR, tok);
        for (int i = 0; i < ty->array_len; i++) {
            InitDesg desg2 = {desg, i};
            Node *rhs = create_lvar_init(init->children[i], ty->base, &desg2, tok);
            node = new_binary(ND_COMMA, node, rhs, tok);
        }
        return node;
    }

    if (ty->kind == TY_STRUCT && !init->expr) {
        Node *node = new_node(ND_NULL_EXPR, tok);

        for (Member *mem = ty->members; mem; mem = mem->next) {
            InitDesg desg2 = {desg, 0, mem};
            Node *rhs = create_lvar_init(init->children[mem->idx], mem->ty, &desg2, tok);
            node = new_binary(ND_COMMA, node, rhs, tok);
        }
        return node;
    }

    if (ty->kind == TY_UNION) {
        InitDesg desg2 = {desg, 0, ty->members};
        return create_lvar_init(init->children[0], ty->members->ty, &desg2, tok);
    }

    if (!init->expr)
        return new_node(ND_NULL_EXPR, tok);

    Node *lhs = init_desg_expr(desg, tok);
    return new_binary(ND_ASSIGN, lhs, init->expr, tok);
}

// 带初始化器的变量定义是变量定义后跟赋值操作的简写形式。
// 此函数为初始化器生成赋值表达式。例如，
// `int x[2][2] = {{6, 7}, {8, 9}}` 会被转换为以下表达式：
//
//   x[0][0] = 6;
//   x[0][1] = 7;
//   x[1][0] = 8;
//   x[1][1] = 9;
static Node *lvar_initializer(Token **rest, Token *tok, Obj *var) {
    Initializer *init = initializer(rest, tok, var->ty, &var->ty);
    InitDesg desg = {NULL, 0, NULL, var};

    // 如果给定了部分初始化器列表，标准要求未指定的
    // 元素被设置为 0。这里我们先对整个变量内存区域
    // 清零，再用用户提供的值进行初始化。
    Node *lhs = new_node(ND_MEMZERO, tok);
    lhs->var = var;

    Node *rhs = create_lvar_init(init, var->ty, &desg, tok);
    return new_binary(ND_COMMA, lhs, rhs, tok);
}

static void write_buf(char *buf, uint64_t val, int sz) {
    if (sz == 1)
        *buf = val;
    else if (sz == 2)
        *(uint16_t *)buf = val;
    else if (sz == 4)
        *(uint32_t *)buf = val;
    else if (sz == 8)
        *(uint64_t *)buf = val;
    else
        unreachable();
}

static Relocation *
write_gvar_data(Relocation *cur, Initializer *init, Type *ty, char *buf, int offset) {
    if (ty->kind == TY_ARRAY) {
        int sz = ty->base->size;
        for (int i = 0; i < ty->array_len; i++)
            cur = write_gvar_data(cur, init->children[i], ty->base, buf, offset + sz * i);
        return cur;
    }

    if (ty->kind == TY_STRUCT) {
        for (Member *mem = ty->members; mem; mem = mem->next)
            cur = write_gvar_data(cur, init->children[mem->idx], mem->ty, buf,
                                  offset + mem->offset);
        return cur;
    }

    if (ty->kind == TY_UNION)
        return write_gvar_data(cur, init->children[0], ty->members->ty, buf, offset);

    if (!init->expr)
        return cur;

    char *label = NULL;
    uint64_t val = eval2(init->expr, &label);

    if (!label) {
        write_buf(buf + offset, val, ty->size);
        return cur;
    }

    Relocation *rel = calloc(1, sizeof(Relocation));
    rel->offset = offset;
    rel->label = label;
    rel->addend = val;
    cur->next = rel;
    return cur->next;
}

// 全局变量的初始化器在编译时求值并嵌入到 .data 段中。
// 此函数将 Initializer 对象序列化为扁平的字节数组。
// 如果初始化器列表包含非常量表达式，则为编译错误。
static void gvar_initializer(Token **rest, Token *tok, Obj *var) {
    Initializer *init = initializer(rest, tok, var->ty, &var->ty);

    Relocation head = {};
    char *buf = calloc(1, var->ty->size);
    write_gvar_data(&head, init, var->ty, buf, 0);
    var->init_data = buf;
    var->rel = head.next;
}

// 如果给定的 token 表示一个类型名则返回 true
static bool is_typename(Token *tok) {
    static char *kw[] = {
        "void", "_Bool", "char", "short", "int", "long", "struct", "union",
        "typedef", "enum", "static", "extern", "_Alignas", "signed", "unsigned",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (equal(tok, kw[i]))
            return true;
    return find_typedef(tok);
}

// stmt = "return" expr ";"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "switch" "(" expr ")" stmt
//      | "case" num ":" stmt
//      | "default" ":" stmt
//      | "for" "(" expr-stmt expr? ";" expr? ")" stmt
//      | "while" "(" expr ")" stmt
//      | "goto" ident ";"
//      | "break" ";"
//      | "continue" ";"
//      | ident ":" stmt
//      | "{" compound-stmt
//      | expr-stmt
static Node *stmt(Token **rest, Token *tok) {
    if (equal(tok, "return")) {
        Node *node = new_node(ND_RETURN, tok);
        if (consume(rest, tok->next, ";"))
            return node;

        Node *exp = expr(&tok, tok->next);
        *rest = skip(tok, ";");

        add_type(exp);
        node->lhs = new_cast(exp, current_fn->ty->return_ty);
        return node;
    }

    if (equal(tok, "if")) {
        Node *node = new_node(ND_IF, tok);
        tok = skip(tok->next, "(");
        node->cond = expr(&tok, tok);
        tok = skip(tok, ")");
        node->then = stmt(&tok, tok);
        if (equal(tok, "else"))
            node->els = stmt(&tok, tok->next);
        *rest = tok;
        return node;
    }

    if (equal(tok, "switch")) {
        Node *node = new_node(ND_SWITCH, tok);
        tok = skip(tok->next, "(");
        node->cond = expr(&tok, tok);
        tok = skip(tok, ")");

        Node *sw = current_switch;
        current_switch = node;

        char *brk = brk_label;
        brk_label = node->brk_label = new_unique_name();

        node->then = stmt(rest, tok);

        current_switch = sw;
        brk_label = brk;
        return node;
    }

    if (equal(tok, "case")) {
        if (!current_switch)
            error_tok(tok, "stray case");

        Node *node = new_node(ND_CASE, tok);
        int val = const_expr(&tok, tok->next);
        tok = skip(tok, ":");
        node->label = new_unique_name();
        node->lhs = stmt(rest, tok);
        node->val = val;
        node->case_next = current_switch->case_next;
        current_switch->case_next = node;
        return node;
    }

    if (equal(tok, "default")) {
        if (!current_switch)
            error_tok(tok, "stray default");

        Node *node = new_node(ND_CASE, tok);
        tok = skip(tok->next, ":");
        node->label = new_unique_name();
        node->lhs = stmt(rest, tok);
        current_switch->default_case = node;
        return node;
    }

    if (equal(tok, "for")) {
        Node *node = new_node(ND_FOR, tok);
        tok = skip(tok->next, "(");

        enter_scope();

        char *brk = brk_label;
        char *cont = cont_label;
        brk_label = node->brk_label = new_unique_name();
        cont_label = node->cont_label = new_unique_name();

        if (is_typename(tok)) {
            Type *basety = declspec(&tok, tok, NULL);
            node->init = declaration(&tok, tok, basety, NULL);
        } else {
            node->init = expr_stmt(&tok, tok);
        }

        if (!equal(tok, ";"))
            node->cond = expr(&tok, tok);
        tok = skip(tok, ";");

        if (!equal(tok, ")"))
            node->inc = expr(&tok, tok);
        tok = skip(tok, ")");

        node->then = stmt(rest, tok);

        leave_scope();
        brk_label = brk;
        cont_label = cont;
        return node;
    }

    if (equal(tok, "while")) {
        Node *node = new_node(ND_FOR, tok);
        tok = skip(tok->next, "(");
        node->cond = expr(&tok, tok);
        tok = skip(tok, ")");

        char *brk = brk_label;
        char *cont = cont_label;
        brk_label = node->brk_label = new_unique_name();
        cont_label = node->cont_label = new_unique_name();

        node->then = stmt(rest, tok);

        brk_label = brk;
        cont_label = cont;
        return node;
    }

    if (equal(tok, "do")) {
        Node *node = new_node(ND_DO, tok);

        char *brk = brk_label;
        char *cont = cont_label;
        brk_label = node->brk_label = new_unique_name();
        cont_label = node->cont_label = new_unique_name();

        node->then = stmt(&tok, tok->next);

        brk_label = brk;
        cont_label = cont;

        tok = skip(tok, "while");
        tok = skip(tok, "(");
        node->cond = expr(&tok, tok);
        tok = skip(tok, ")");
        *rest = skip(tok, ";");
        return node;
    }

    if (equal(tok, "goto")) {
        Node *node = new_node(ND_GOTO, tok);
        node->label = get_ident(tok->next);
        node->goto_next = gotos;
        gotos = node;
        *rest = skip(tok->next->next, ";");
        return node;
    }

    if (equal(tok, "break")) {
        if (!brk_label)
            error_tok(tok, "stray break");
        Node *node = new_node(ND_GOTO, tok);
        node->unique_label = brk_label;
        *rest = skip(tok->next, ";");
        return node;
    }

    if (equal(tok, "continue")) {
        if (!cont_label)
            error_tok(tok, "stray continue");
        Node *node = new_node(ND_GOTO, tok);
        node->unique_label = cont_label;
        *rest = skip(tok->next, ";");
        return node;
    }

    if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
        Node *node = new_node(ND_LABEL, tok);
        node->label = strndup(tok->loc, tok->len);
        node->unique_label = new_unique_name();
        node->lhs = stmt(rest, tok->next->next);
        node->goto_next = labels;
        labels = node;
        return node;
    }

    if (equal(tok, "{"))
        return compound_stmt(rest, tok->next);

    return expr_stmt(rest, tok);
}

// compound-stmt = (typedef | declaration | stmt)* "}"
static Node *compound_stmt(Token **rest, Token *tok) {
    Node *node = new_node(ND_BLOCK, tok);

    Node head = {};
    Node *cur = &head;

    enter_scope();

    while (!equal(tok, "}")) {
        if (is_typename(tok) && !equal(tok->next, ":")) {
            VarAttr attr = {};
            Type *basety = declspec(&tok, tok, &attr);

            if (attr.is_typedef) {
                tok = parse_typedef(tok, basety);
                continue;
            }

            if (is_function(tok)) {
                tok = function(tok, basety, &attr);
                continue;
            }

            if (attr.is_extern) {
                tok = global_variable(tok, basety, &attr);
                continue;
            }

            cur = cur->next = declaration(&tok, tok, basety, &attr);
        } else {
            cur = cur->next = stmt(&tok, tok);
        }
        add_type(cur);
    }

    leave_scope();

    node->body = head.next;
    *rest = tok->next;
    return node;
}

// expr-stmt = expr? ";"
static Node *expr_stmt(Token **rest, Token *tok) {
    if (equal(tok, ";")) {
        *rest = tok->next;
        return new_node(ND_BLOCK, tok);
    }

    Node *node = new_node(ND_EXPR_STMT, tok);
    node->lhs = expr(&tok, tok);
    *rest = skip(tok, ";");
    return node;
}

// expr = assign ("," expr)?
static Node *expr(Token **rest, Token *tok) {
    Node *node = assign(&tok, tok);

    if (equal(tok, ","))
        return new_binary(ND_COMMA, node, expr(rest, tok->next), tok);

    *rest = tok;
    return node;
}

static int64_t eval(Node *node) {
    return eval2(node, NULL);
}

// 将给定节点作为常量表达式求值。
//
// 常量表达式可以是一个数字，或者 ptr+n 的形式，
// 其中 ptr 是指向全局变量的指针，n 是正/负数。
// 后一种形式仅作为全局变量的初始化表达式被接受。
static int64_t eval2(Node *node, char **label) {
    add_type(node);

    switch (node->kind) {
    case ND_ADD:
        return eval2(node->lhs, label) + eval(node->rhs);
    case ND_SUB:
        return eval2(node->lhs, label) - eval(node->rhs);
    case ND_MUL:
        return eval(node->lhs) * eval(node->rhs);
    case ND_DIV:
        return eval(node->lhs) / eval(node->rhs);
    case ND_NEG:
        return -eval(node->lhs);
    case ND_MOD:
        return eval(node->lhs) % eval(node->rhs);
    case ND_BITAND:
        return eval(node->lhs) & eval(node->rhs);
    case ND_BITOR:
        return eval(node->lhs) | eval(node->rhs);
    case ND_BITXOR:
        return eval(node->lhs) ^ eval(node->rhs);
    case ND_SHL:
        return eval(node->lhs) << eval(node->rhs);
    case ND_SHR:
        return eval(node->lhs) >> eval(node->rhs);
    case ND_EQ:
        return eval(node->lhs) == eval(node->rhs);
    case ND_NE:
        return eval(node->lhs) != eval(node->rhs);
    case ND_LT:
        return eval(node->lhs) < eval(node->rhs);
    case ND_LE:
        return eval(node->lhs) <= eval(node->rhs);
    case ND_COND:
        return eval(node->cond) ? eval2(node->then, label) : eval2(node->els, label);
    case ND_COMMA:
        return eval2(node->rhs, label);
    case ND_NOT:
        return !eval(node->lhs);
    case ND_BITNOT:
        return ~eval(node->lhs);
    case ND_LOGAND:
        return eval(node->lhs) && eval(node->rhs);
    case ND_LOGOR:
        return eval(node->lhs) || eval(node->rhs);
    case ND_CAST: {
        int64_t val = eval2(node->lhs, label);
        if (is_integer(node->ty)) {
            switch (node->ty->size) {
            case 1: return (uint8_t)val;
            case 2: return (uint16_t)val;
            case 4: return (uint32_t)val;
            }
        }
        return val;
    }
    case ND_ADDR:
        return eval_rval(node->lhs, label);
    case ND_MEMBER:
        if (!label)
            error_tok(node->tok, "not a compile-time constant");
        if (node->ty->kind != TY_ARRAY)
            error_tok(node->tok, "invalid initializer");
        return eval_rval(node->lhs, label) + node->member->offset;
    case ND_VAR:
        if (!label)
            error_tok(node->tok, "not a compile-time constant");
        if (node->var->ty->kind != TY_ARRAY && node->var->ty->kind != TY_FUNC)
            error_tok(node->tok, "invalid initializer");
        *label = node->var->name;
        return 0;
    case ND_NUM:
        return node->val;
    }

    error_tok(node->tok, "not a compile-time constant");
}

static int64_t eval_rval(Node *node, char **label) {
    switch (node->kind) {
    case ND_VAR:
        if (node->var->is_local)
            error_tok(node->tok, "not a compile-time constant");
        *label = node->var->name;
        return 0;
    case ND_DEREF:
        return eval2(node->lhs, label);
    case ND_MEMBER:
        return eval_rval(node->lhs, label) + node->member->offset;
    }

    error_tok(node->tok, "invalid initializer");
}

static int64_t const_expr(Token **rest, Token *tok) {
    Node *node = conditional(rest, tok);
    return eval(node);
}

// 将 A op= B 转换为 tmp = &A, *tmp = *tmp op B
// 其中 tmp 是一个新的指针变量
static Node *to_assign(Node *binary) {
    add_type(binary->lhs);
    add_type(binary->rhs);
    Token *tok = binary->tok;

    Obj *var = new_lvar("", pointer_to(binary->lhs->ty));

    Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
                             new_unary(ND_ADDR, binary->lhs, tok), tok);

    Node *expr2 =
        new_binary(ND_ASSIGN,
                   new_unary(ND_DEREF, new_var_node(var, tok), tok),
                   new_binary(binary->kind,
                              new_unary(ND_DEREF, new_var_node(var, tok), tok),
                              binary->rhs,
                              tok),
                   tok);

    return new_binary(ND_COMMA, expr1, expr2, tok);
}

// assign    = conditional (assign-op assign)?
// assign-op = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^="
//           | "<<=" | ">>="
static Node *assign(Token **rest, Token *tok) {
    Node *node = conditional(&tok, tok);

    if (equal(tok, "="))
        return new_binary(ND_ASSIGN, node, assign(rest, tok->next), tok);

    if (equal(tok, "+="))
        return to_assign(new_add(node, assign(rest, tok->next), tok));

    if (equal(tok, "-="))
        return to_assign(new_sub(node, assign(rest, tok->next), tok));

    if (equal(tok, "*="))
        return to_assign(new_binary(ND_MUL, node, assign(rest, tok->next), tok));

    if (equal(tok, "/="))
        return to_assign(new_binary(ND_DIV, node, assign(rest, tok->next), tok));

    if (equal(tok, "%="))
        return to_assign(new_binary(ND_MOD, node, assign(rest, tok->next), tok));

    if (equal(tok, "&="))
        return to_assign(new_binary(ND_BITAND, node, assign(rest, tok->next), tok));

    if (equal(tok, "|="))
        return to_assign(new_binary(ND_BITOR, node, assign(rest, tok->next), tok));

    if (equal(tok, "^="))
        return to_assign(new_binary(ND_BITXOR, node, assign(rest, tok->next), tok));

    if (equal(tok, "<<="))
        return to_assign(new_binary(ND_SHL, node, assign(rest, tok->next), tok));

    if (equal(tok, ">>="))
        return to_assign(new_binary(ND_SHR, node, assign(rest, tok->next), tok));

    *rest = tok;
    return node;
}

// conditional = logor ("?" expr ":" conditional)?
static Node *conditional(Token **rest, Token *tok) {
    Node *cond = logor(&tok, tok);

    if (!equal(tok, "?")) {
        *rest = tok;
        return cond;
    }

    Node *node = new_node(ND_COND, tok);
    node->cond = cond;
    node->then = expr(&tok, tok->next);
    tok = skip(tok, ":");
    node->els = conditional(rest, tok);
    return node;
}

// logor = logand ("||" logand)*
static Node *logor(Token **rest, Token *tok) {
    Node *node = logand(&tok, tok);
    while (equal(tok, "||")) {
        Token *start = tok;
        node = new_binary(ND_LOGOR, node, logand(&tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

// logand = bitor ("&&" bitor)*
static Node *logand(Token **rest, Token *tok) {
    Node *node = bitor(&tok, tok);
    while (equal(tok, "&&")) {
        Token *start = tok;
        node = new_binary(ND_LOGAND, node, bitor(&tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

// bitor = bitxor ("|" bitxor)*
static Node *bitor(Token **rest, Token *tok) {
    Node *node = bitxor(&tok, tok);
    while (equal(tok, "|")) {
        Token *start = tok;
        node = new_binary(ND_BITOR, node, bitxor(&tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

// bitxor = bitand ("^" bitand)*
static Node *bitxor(Token **rest, Token *tok) {
    Node *node = bitand(&tok, tok);
    while (equal(tok, "^")) {
        Token *start = tok;
        node = new_binary(ND_BITXOR, node, bitand(&tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

// bitand = equality ("&" equality)*
static Node *bitand(Token **rest, Token *tok) {
    Node *node = equality(&tok, tok);
    while (equal(tok, "&")) {
        Token *start = tok;
        node = new_binary(ND_BITAND, node, equality(&tok, tok->next), start);
    }
    *rest = tok;
    return node;
}

// equality = relational ("==" relational | "!=" relational)*
static Node *equality(Token **rest, Token *tok) {
    Node *node = relational(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "==")) {
            node = new_binary(ND_EQ, node, relational(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "!=")) {
            node = new_binary(ND_NE, node, relational(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static Node *relational(Token **rest, Token *tok) {
    Node *node = shift(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "<")) {
            node = new_binary(ND_LT, node, shift(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "<=")) {
            node = new_binary(ND_LE, node, shift(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, ">")) {
            node = new_binary(ND_LT, shift(&tok, tok->next), node, start);
            continue;
        }

        if (equal(tok, ">=")) {
            node = new_binary(ND_LE, shift(&tok, tok->next), node, start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// shift = add ("<<" add | ">>" add)*
static Node *shift(Token **rest, Token *tok) {
    Node *node = add(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "<<")) {
            node = new_binary(ND_SHL, node, add(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, ">>")) {
            node = new_binary(ND_SHR, node, add(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// 在 C 语言中，`+` 运算符被重载以执行指针算术运算。
// 如果 p 是指针，p+n 不会将 n 加到 p 的值上，而是加上 sizeof(*p)*n,
// 这样 p+n 就指向 p 之后 n 个元素（而非 n 个字节）的位置。
// 换句话说，我们需要将整数值缩放后再加到指针值上。
// 此函数负责处理这种缩放。
static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    // num + num
    if (is_integer(lhs->ty) && is_integer(rhs->ty))
        return new_binary(ND_ADD, lhs, rhs, tok);

    if (lhs->ty->base && rhs->ty->base)
        error_tok(tok, "invalid operands");

    // 将 `num + ptr` 规范化为 `ptr + num`
    if (!lhs->ty->base && rhs->ty->base) {
        Node *tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }

    // ptr + num
    rhs = new_binary(ND_MUL, rhs, new_long(lhs->ty->base->size, tok), tok);
    return new_binary(ND_ADD, lhs, rhs, tok);
}

// 类似 `+`，`-` 运算符也针对指针类型进行了重载。
static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
    add_type(lhs);
    add_type(rhs);

    // num - num
    if (is_integer(lhs->ty) && is_integer(rhs->ty))
        return new_binary(ND_SUB, lhs, rhs, tok);

    // ptr - num
    if (lhs->ty->base && is_integer(rhs->ty)) {
        rhs = new_binary(ND_MUL, rhs, new_long(lhs->ty->base->size, tok), tok);
        add_type(rhs);
        Node *node = new_binary(ND_SUB, lhs, rhs, tok);
        node->ty = lhs->ty;
        return node;
    }

    // ptr - ptr，返回两个指针之间的元素个数。
    if (lhs->ty->base && rhs->ty->base) {
        Node *node = new_binary(ND_SUB, lhs, rhs, tok);
        node->ty = ty_int;
        return new_binary(ND_DIV, node, new_long(lhs->ty->base->size, tok), tok);
    }

    error_tok(tok, "invalid operands");
}

// add = mul ("+" mul | "-" mul)*
static Node *add(Token **rest, Token *tok) {
    Node *node = mul(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "+")) {
            node = new_add(node, mul(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "-")) {
            node = new_sub(node, mul(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// mul = cast ("*" cast | "/" cast | "%" cast)*
static Node *mul(Token **rest, Token *tok) {
    Node *node = cast(&tok, tok);

    for (;;) {
        Token *start = tok;

        if (equal(tok, "*")) {
            node = new_binary(ND_MUL, node, cast(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "/")) {
            node = new_binary(ND_DIV, node, cast(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "%")) {
            node = new_binary(ND_MOD, node, cast(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
        return node;
    }
}

// cast = "(" type-name ")" cast | unary
static Node *cast(Token **rest, Token *tok) {
    if (equal(tok, "(") && is_typename(tok->next)) {
        Token *start = tok;
        Type *ty = typename(&tok, tok->next);
        tok = skip(tok, ")");

        // 复合字面量
        if (equal(tok, "{"))
            return unary(rest, start);

        // 类型转换
        Node *node = new_cast(cast(rest, tok), ty);
        node->tok = start;
        return node;
    }

    return unary(rest, tok);
}

// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | ("++" | "--") unary
//       | postfix
static Node *unary(Token **rest, Token *tok) {
    if (equal(tok, "+"))
        return cast(rest, tok->next);

    if (equal(tok, "-"))
        return new_unary(ND_NEG, cast(rest, tok->next), tok);

    if (equal(tok, "&"))
        return new_unary(ND_ADDR, cast(rest, tok->next), tok);

    if (equal(tok, "*"))
        return new_unary(ND_DEREF, cast(rest, tok->next), tok);

    if (equal(tok, "!"))
        return new_unary(ND_NOT, cast(rest, tok->next), tok);

    if (equal(tok, "~"))
        return new_unary(ND_BITNOT, cast(rest, tok->next), tok);

    // 将 ++i 当作 i+=1
    if (equal(tok, "++"))
        return to_assign(new_add(unary(rest, tok->next), new_num(1, tok), tok));

    // 将 --i 当作 i-=1
    if (equal(tok, "--"))
        return to_assign(new_sub(unary(rest, tok->next), new_num(1, tok), tok));

    return postfix(rest, tok);
}

// struct-members = (declspec declarator (","  declarator)* ";")*
static void struct_members(Token **rest, Token *tok, Type *ty) {
    Member head = {};
    Member *cur = &head;
    int idx = 0;

    while (!equal(tok, "}")) {
        VarAttr attr = {};
        Type *basety = declspec(&tok, tok, &attr);
        bool first = true;

        while (!consume(&tok, tok, ";")) {
            if (!first)
                tok = skip(tok, ",");
            first = false;

            Member *mem = calloc(1, sizeof(Member));
            mem->ty = declarator(&tok, tok, basety);
            mem->name = mem->ty->name;
            mem->idx = idx++;
            mem->align = attr.align ? attr.align : mem->ty->align;
            cur = cur->next = mem;
        }
    }

    // 如果最后一个元素是不完整类型的数组，则称为
    // "灵活数组成员"。它应该表现得像零长度数组一样。
    if (cur != &head && cur->ty->kind == TY_ARRAY && cur->ty->array_len < 0) {
        cur->ty = array_of(cur->ty->base, 0);
        ty->is_flexible = true;
    }

    *rest = tok->next;
    ty->members = head.next;
}

// struct-union-decl = ident? ("{" struct-members)?
static Type *struct_union_decl(Token **rest, Token *tok) {
    // 读取标签
    Token *tag = NULL;
    if (tok->kind == TK_IDENT) {
        tag = tok;
        tok = tok->next;
    }

    if (tag && !equal(tok, "{")) {
        *rest = tok;

        Type *ty = find_tag(tag);
        if (ty)
            return ty;

        ty = struct_type();
        ty->size = -1;
        push_tag_scope(tag, ty);
        return ty;
    }

    tok = skip(tok, "{");

    // 构造结构体或联合体对象
    Type *ty = struct_type();
    struct_members(rest, tok, ty);

    if (tag) {
        // 如果是重新定义，覆盖之前的类型。
        // 否则，注册结构体类型。
        for (TagScope *sc = scope->tags; sc; sc = sc->next) {
            if (equal(tag, sc->name)) {
                *sc->ty = *ty;
                return sc->ty;
            }
        }

        push_tag_scope(tag, ty);
    }

    return ty;
}

// struct-decl = struct-union-decl
static Type *struct_decl(Token **rest, Token *tok) {
    Type *ty = struct_union_decl(rest, tok);
    ty->kind = TY_STRUCT;

    if (ty->size < 0)
        return ty;

    // 为结构体成员分配偏移量
    int offset = 0;
    for (Member *mem = ty->members; mem; mem = mem->next) {
        offset = align_to(offset, mem->align);
        mem->offset = offset;
        offset += mem->ty->size;

        if (ty->align < mem->align)
            ty->align = mem->align;
    }
    ty->size = align_to(offset, ty->align);
    return ty;
}

// union-decl = struct-union-decl
static Type *union_decl(Token **rest, Token *tok) {
    Type *ty = struct_union_decl(rest, tok);
    ty->kind = TY_UNION;

    if (ty->size < 0)
        return ty;

    // 对于联合体，我们不需要分配偏移量，因为所有成员
    // 偏移量已初始化为零。但需要计算对齐和大小。
    for (Member *mem = ty->members; mem; mem = mem->next) {
        if (ty->align < mem->align)
            ty->align = mem->align;
        if (ty->size < mem->ty->size)
            ty->size = mem->ty->size;
    }
    ty->size = align_to(ty->size, ty->align);
    return ty;
}

static Member *get_struct_member(Type *ty, Token *tok) {
    for (Member *mem = ty->members; mem; mem = mem->next)
        if (mem->name->len == tok->len &&
            !strncmp(mem->name->loc, tok->loc, tok->len))
            return mem;
    error_tok(tok, "no such member");
}

static Node *struct_ref(Node *lhs, Token *tok) {
    add_type(lhs);
    if (lhs->ty->kind != TY_STRUCT && lhs->ty->kind != TY_UNION)
        error_tok(lhs->tok, "not a struct nor a union");

    Node *node = new_unary(ND_MEMBER, lhs, tok);
    node->member = get_struct_member(lhs->ty, tok);
    return node;
}

// 将 A++ 转换为 (typeof A)((A += 1) - 1)
static Node *new_inc_dec(Node *node, Token *tok, int addend) {
    add_type(node);
    return new_cast(new_add(to_assign(new_add(node, new_num(addend, tok), tok)),
                            new_num(-addend, tok), tok),
                    node->ty);
}

// postfix = "(" type-name ")" "{" initializer-list "}"
//         | primary ("[" expr "]" | "." ident | "++" | "--")*
static Node *postfix(Token **rest, Token *tok) {
    if (equal(tok, "(") && is_typename(tok->next)) {
        // 复合字面量
        Token *start = tok;
        Type *ty = typename(&tok, tok->next);
        tok = skip(tok, ")");

        if (scope->next == NULL) {
            Obj *var = new_anon_gvar(ty);
            gvar_initializer(rest, tok, var);
            return new_var_node(var, start);
        }

        Obj *var = new_lvar("", ty);
        Node *lhs = lvar_initializer(rest, tok, var);
        Node *rhs = new_var_node(var, tok);
        return new_binary(ND_COMMA, lhs, rhs, start);
    }

    Node *node = primary(&tok, tok);

    for (;;) {
        if (equal(tok, "[")) {
            // x[y] 等价于 *(x+y)
            Token *start = tok;
            Node *idx = expr(&tok, tok->next);
            tok = skip(tok, "]");
            node = new_unary(ND_DEREF, new_add(node, idx, start), start);
            continue;
        }

        if (equal(tok, ".")) {
            node = struct_ref(node, tok->next);
            tok = tok->next->next;
            continue;
        }

        if (equal(tok, "->")) {
            // x->y 等价于 (*x).y
            node = new_unary(ND_DEREF, node, tok);
            node = struct_ref(node, tok->next);
            tok = tok->next->next;
            continue;
        }

        if (equal(tok, "++")) {
            node = new_inc_dec(node, tok, 1);
            tok = tok->next;
            continue;
        }

        if (equal(tok, "--")) {
            node = new_inc_dec(node, tok, -1);
            tok = tok->next;
            continue;
        }

        *rest = tok;
        return node;
    }
}

// funcall = ident "(" (assign ("," assign)*)? ")"
static Node *funcall(Token **rest, Token *tok) {
    Token *start = tok;
    tok = tok->next->next;

    VarScope *sc = find_var(start);
    if (!sc)
        error_tok(start, "implicit declaration of a function");
    if (!sc->var || sc->var->ty->kind != TY_FUNC)
        error_tok(start, "not a function");

    Type *ty = sc->var->ty;
    Type *param_ty = ty->params;

    Node head = {};
    Node *cur = &head;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(tok, ",");

        Node *arg = assign(&tok, tok);
        add_type(arg);

        if (!param_ty && !ty->is_variadic)
            error_tok(tok, "too many arguments");

        if (param_ty) {
            if (param_ty->kind == TY_STRUCT || param_ty->kind == TY_UNION)
                error_tok(arg->tok, "passing struct or union is not supported yet");
            arg = new_cast(arg, param_ty);
            param_ty = param_ty->next;
        }

        cur = cur->next = arg;
    }

    if (param_ty)
        error_tok(tok, "too few arguments");

    *rest = skip(tok, ")");

    Node *node = new_node(ND_FUNCALL, start);
    node->funcname = strndup(start->loc, start->len);
    node->func_ty = ty;
    node->ty = ty->return_ty;
    node->args = head.next;
    return node;
}

// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" "(" type-name ")"
//         | "sizeof" unary
//         | ident func-args?
//         | str
//         | num
static Node *primary(Token **rest, Token *tok) {
    Token *start = tok;

    if (equal(tok, "(") && equal(tok->next, "{")) {
        // GNU 语句表达式
        Node *node = new_node(ND_STMT_EXPR, tok);
        node->body = compound_stmt(&tok, tok->next->next)->body;
        *rest = skip(tok, ")");
        return node;
    }

    if (equal(tok, "(")) {
        Node *node = expr(&tok, tok->next);
        *rest = skip(tok, ")");
        return node;
    }

    if (equal(tok, "sizeof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
        Type *ty = typename(&tok, tok->next->next);
        *rest = skip(tok, ")");
        return new_num(ty->size, start);
    }

    if (equal(tok, "sizeof")) {
        Node *node = unary(rest, tok->next);
        add_type(node);
        return new_num(node->ty->size, tok);
    }

    if (equal(tok, "_Alignof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
        Type *ty = typename(&tok, tok->next->next);
        *rest = skip(tok, ")");
        return new_num(ty->align, tok);
    }

    if (equal(tok, "_Alignof")) {
        Node *node = unary(rest, tok->next);
        add_type(node);
        return new_num(node->ty->align, tok);
    }

    if (tok->kind == TK_IDENT) {
        // 函数调用
        if (equal(tok->next, "("))
            return funcall(rest, tok);

        // 变量或枚举常量
        VarScope *sc = find_var(tok);
        if (!sc || (!sc->var && !sc->enum_ty))
            error_tok(tok, "未定义的变量");

        Node *node;
        if (sc->var)
            node = new_var_node(sc->var, tok);
        else
            node = new_num(sc->enum_val, tok);

        *rest = tok->next;
        return node;
    }

    if (tok->kind == TK_STR) {
        Obj *var = new_string_literal(tok->str, tok->ty);
        *rest = tok->next;
        return new_var_node(var, tok);
    }

    if (tok->kind == TK_NUM) {
        Node *node = new_num(tok->val, tok);
        node->ty = tok->ty;
        *rest = tok->next;
        return node;
    }

    error_tok(tok, "expected an expression");
}

static Token *parse_typedef(Token *tok, Type *basety) {
    bool first = true;

    while (!consume(&tok, tok, ";")) {
        if (!first)
            tok = skip(tok, ",");
        first = false;

        Type *ty = declarator(&tok, tok, basety);
        push_scope(get_ident(ty->name))->type_def = ty;
    }
    return tok;
}

static void create_param_lvars(Type *param) {
    if (param) {
        create_param_lvars(param->next);
        new_lvar(get_ident(param->name), param);
    }
}

// 此函数将 goto 与标签进行匹配。
//
// 我们无法在解析函数时立即解析 goto，因为 goto
// 可能引用函数中稍后出现的标签。
// 因此，我们需要在解析整个函数之后再进行匹配。
static void resolve_goto_labels(void) {
    for (Node *x = gotos; x; x = x->goto_next) {
        for (Node *y = labels; y; y = y->goto_next) {
            if (!strcmp(x->label, y->label)) {
                x->unique_label = y->unique_label;
                break;
            }
        }

        if (x->unique_label == NULL)
            error_tok(x->tok->next, "use of undeclared label");
    }

    gotos = labels = NULL;
}

static Token *function(Token *tok, Type *basety, VarAttr *attr) {
    Type *ty = declarator(&tok, tok, basety);

    Obj *fn = new_gvar(get_ident(ty->name), ty);
    fn->is_function = true;
    fn->is_definition = !consume(&tok, tok, ";");
    fn->is_static = attr->is_static;

    if (!fn->is_definition)
        return tok;

    current_fn = fn;
    locals = NULL;
    enter_scope();
    create_param_lvars(ty->params);
    fn->params = locals;
    if (ty->is_variadic)
        fn->va_area = new_lvar("__va_area__", array_of(ty_char, 224));

    tok = skip(tok, "{");
    fn->body = compound_stmt(&tok, tok);
    fn->locals = locals;
    leave_scope();
    resolve_goto_labels();
    return tok;
}

static Token *global_variable(Token *tok, Type *basety, VarAttr *attr) {
    bool first = true;

    while (!consume(&tok, tok, ";")) {
        if (!first)
            tok = skip(tok, ",");
        first = false;

        Type *ty = declarator(&tok, tok, basety);
        Obj *var = new_gvar(get_ident(ty->name), ty);
        var->is_definition = !attr->is_extern;
        var->is_static = attr->is_static;
        if (attr->align)
            var->align = attr->align;

        if (equal(tok, "="))
            gvar_initializer(&tok, tok->next, var);
    }
    return tok;
}

// 向前查看并判断给定 token 是否是一个函数定义的开始
static bool is_function(Token *tok) {
    if (equal(tok, ";"))
        return false;

    Type dummy = {};
    Type *ty = declarator(&tok, tok, &dummy);
    return ty->kind == TY_FUNC;
}

// program = (typedef | function-definition | global-variable)*
Obj *parse(Token *tok) {
    globals = NULL;

    while (tok->kind != TK_EOF) {
        VarAttr attr = {};
        Type *basety = declspec(&tok, tok, &attr);

        // 类型定义
        if (attr.is_typedef) {
            tok = parse_typedef(tok, basety);
            continue;
        }

        // 函数定义
        if (is_function(tok)) {
            tok = function(tok, basety, &attr);
            continue;
        }

        // 全局变量
        tok = global_variable(tok, basety, &attr);
    }
    return globals;
}
