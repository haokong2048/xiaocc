#include "xiaocc.h"

// 当前输入字符串
static char *current_input;

// 报告错误并退出
void error(char *fmt, ...) {
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

void error_at(char *loc, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(loc, fmt, ap);
}

void error_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(tok->loc, fmt, ap);
}

// 判断当前 Token 是否匹配字符串 op
bool equal(Token *tok, char *op) {
    return memcmp(tok->loc, op, tok->len) == 0 && op[tok->len] == '\0';
}

// 确保当前 Token 为 op
Token *skip(Token *tok, char *op) {
    if (!equal(tok, op))
        error_tok(tok, "expected '%s'", op);
    return tok->next;
}

bool consume(Token **rest, Token *tok, char *str) {
    if (equal(tok, str)) {
        *rest = tok->next;
        return true;
    }
    *rest = tok;
    return false;
}

// 创建一个新的 Token
static Token *new_token(TokenKind kind, char *start, char *end) {
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->loc = start;
    tok->len = end - start;
    return tok;
}

static bool startswith(char *p, char *q) {
    return strncmp(p, q, strlen(q)) == 0;
}

// 如果 c 是标识符的首字符，返回 true
static bool is_ident1(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

// 如果 c 是标识符的非首字符，返回 true
static bool is_ident2(char c) {
    return is_ident1(c) || ('0' <= c && c <= '9');
}

// 从 p 读取标点符号并返回其长度
static int read_punct(char *p) {
    if (startswith(p, "==") || startswith(p, "!=") ||
        startswith(p, "<=") || startswith(p, ">="))
        return 2;

    return ispunct(*p) ? 1 : 0;
}

static bool is_keyword(Token *tok) {
    static char *kw[] = {
        "return", "if", "else", "for", "while", "int", "sizeof",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (equal(tok, kw[i]))
            return true;
    return false;
}

static void convert_keywords(Token *tok) {
    for (Token *t = tok; t->kind != TK_EOF; t = t->next)
        if (is_keyword(t))
            t->kind = TK_KEYWORD;
}

// 对输入字符串进行词法分析，返回 Token 链表
Token *tokenize(char *p) {
    current_input = p;
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

        // 标识符或关键字
        if (is_ident1(*p)) {
            char *start = p;
            do {
                p++;
            } while (is_ident2(*p));
            cur = cur->next = new_token(TK_IDENT, start, p);
            continue;
        }

        // 标点符号
        int punct_len = read_punct(p);
        if (punct_len) {
            cur = cur->next = new_token(TK_PUNCT, p, p + punct_len);
            p += cur->len;
            continue;
        }

        error_at(p, "invalid token");
    }

    cur = cur->next = new_token(TK_EOF, p, p);
    convert_keywords(head.next);
    return head.next;
}
