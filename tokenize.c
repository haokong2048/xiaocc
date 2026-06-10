#include "xiaocc.h"

// 输入文件名
static char *current_filename;

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

// 以以下格式报告错误位置并退出
//
// foo.c:10: x = y + 1;
//               ^ <错误信息>
static void verror_at(int line_no, char *loc, char *fmt, va_list ap) {
    // 查找包含 loc 的行
    char *line = loc;
    while (current_input < line && line[-1] != '\n')
        line--;

    char *end = loc;
    while (*end != '\n')
        end++;

    // 打印该行
    int indent = fprintf(stderr, "%s:%d: ", current_filename, line_no);
    fprintf(stderr, "%.*s\n", (int)(end - line), line);

    // 显示错误信息
    int pos = loc - line + indent;

    fprintf(stderr, "%*s", pos, ""); // 打印 pos 个空格
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

void error_at(char *loc, char *fmt, ...) {
    int line_no = 1;
    for (char *p = current_input; p < loc; p++)
        if (*p == '\n')
            line_no++;

    va_list ap;
    va_start(ap, fmt);
    verror_at(line_no, loc, fmt, ap);
    exit(1);
}

void error_tok(Token *tok, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    verror_at(tok->line_no, tok->loc, fmt, ap);
    exit(1);
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

static int from_hex(char c) {
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return c - 'a' + 10;
    return c - 'A' + 10;
}

// 从 p 读取标点符号并返回其长度
static int read_punct(char *p) {
    static char *kw[] = {
        "==", "!=", "<=", ">=", "->", "+=", "-=", "*=", "/=", "++", "--",
        "%=", "&=", "|=", "^=",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (startswith(p, kw[i]))
            return strlen(kw[i]);

    return ispunct(*p) ? 1 : 0;
}

static bool is_keyword(Token *tok) {
    static char *kw[] = {
        "return", "if", "else", "for", "while", "int", "sizeof", "char",
        "struct", "union", "short", "long", "void", "typedef", "_Bool",
        "enum", "static",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
        if (equal(tok, kw[i]))
            return true;
    return false;
}

static int read_escaped_char(char **new_pos, char *p) {
    if ('0' <= *p && *p <= '7') {
        // 读取八进制数
        int c = *p++ - '0';
        if ('0' <= *p && *p <= '7') {
            c = (c << 3) + (*p++ - '0');
            if ('0' <= *p && *p <= '7')
                c = (c << 3) + (*p++ - '0');
        }
        *new_pos = p;
        return c;
    }

    if (*p == 'x') {
        // 读取十六进制数
        p++;
        if (!isxdigit(*p))
            error_at(p, "invalid hex escape sequence");

        int c = 0;
        for (; isxdigit(*p); p++)
            c = (c << 4) + from_hex(*p);
        *new_pos = p;
        return c;
    }

    *new_pos = p + 1;

    // 转义序列在此用它们自身来定义。例如 '\n' 用 '\n' 来实现。
    // 这种同义反复的定义之所以可行，是因为编译我们编译器的编译器
    // 知道 '\n' 的实际含义。换句话说，我们从编译器的编译器中
    // "继承"了 '\n' 的 ASCII 码，因此无需在此手动编写实际代码。
    //
    // 这一事实不仅对编译器的正确性有重大影响，对生成代码的安全性
    // 也是如此。详见 Ken Thompson 的 "Reflections on Trusting Trust"。
    // https://github.com/rui314/chibicc/wiki/thompson1984.pdf
    switch (*p) {
    case 'a': return '\a';
    case 'b': return '\b';
    case 't': return '\t';
    case 'n': return '\n';
    case 'v': return '\v';
    case 'f': return '\f';
    case 'r': return '\r';
    // [GNU] \e 表示 ASCII 转义字符，是 GNU C 扩展
    case 'e': return 27;
    default: return *p;
    }
}

// 查找字符串的结束双引号
static char *string_literal_end(char *p) {
    char *start = p;
    for (; *p != '"'; p++) {
        if (*p == '\n' || *p == '\0')
            error_at(start, "unclosed string literal");
        if (*p == '\\')
            p++;
    }
    return p;
}

static Token *read_string_literal(char *start) {
    char *end = string_literal_end(start + 1);
    char *buf = calloc(1, end - start);
    int len = 0;

    for (char *p = start + 1; p < end;) {
        if (*p == '\\')
            buf[len++] = read_escaped_char(&p, p + 1);
        else
            buf[len++] = *p++;
    }

    Token *tok = new_token(TK_STR, start, end + 1);
    tok->ty = array_of(ty_char, len + 1);
    tok->str = buf;
    return tok;
}

static Token *read_char_literal(char *start) {
    char *p = start + 1;
    if (*p == '\0')
        error_at(start, "字符字面量未闭合");

    char c;
    if (*p == '\\')
        c = read_escaped_char(&p, p + 1);
    else
        c = *p++;

    char *end = strchr(p, '\'');
    if (!end)
        error_at(p, "字符字面量未闭合");

    Token *tok = new_token(TK_NUM, start, end + 1);
    tok->val = c;
    return tok;
}

static Token *read_int_literal(char *start) {
    char *p = start;

    int base = 10;
    if (!strncasecmp(p, "0x", 2) && isalnum(p[2])) {
        p += 2;
        base = 16;
    } else if (!strncasecmp(p, "0b", 2) && isalnum(p[2])) {
        p += 2;
        base = 2;
    } else if (*p == '0') {
        base = 8;
    }

    long val = strtoul(p, &p, base);
    if (isalnum(*p))
        error_at(p, "无效的数字");

    Token *tok = new_token(TK_NUM, start, p);
    tok->val = val;
    return tok;
}

// 为所有 token 初始化行号信息
static void add_line_numbers(Token *tok) {
    char *p = current_input;
    int n = 1;

    do {
        if (p == tok->loc) {
            tok->line_no = n;
            tok = tok->next;
        }
        if (*p == '\n')
            n++;
    } while (*p++);
}

static void convert_keywords(Token *tok) {
    for (Token *t = tok; t->kind != TK_EOF; t = t->next)
        if (is_keyword(t))
            t->kind = TK_KEYWORD;
}

// 对输入字符串进行词法分析，返回 Token 链表
static Token *tokenize(char *filename, char *p) {
    current_filename = filename;
    current_input = p;
    Token head = {};
    Token *cur = &head;

    while (*p) {
        // 跳过行注释
        if (startswith(p, "//")) {
            p += 2;
            while (*p != '\n')
                p++;
            continue;
        }

        // 跳过块注释
        if (startswith(p, "/*")) {
            char *q = strstr(p + 2, "*/");
            if (!q)
                error_at(p, "unclosed block comment");
            p = q + 2;
            continue;
        }

        // 跳过空白字符
        if (isspace(*p)) {
            p++;
            continue;
        }

        // 数字字面量
        if (isdigit(*p)) {
            cur = cur->next = read_int_literal(p);
            p += cur->len;
            continue;
        }

        // 字符串字面量
        if (*p == '"') {
            cur = cur->next = read_string_literal(p);
            p += cur->len;
            continue;
        }

        // 字符字面量
        if (*p == '\'') {
            cur = cur->next = read_char_literal(p);
            p += cur->len;
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
    add_line_numbers(head.next);
    convert_keywords(head.next);
    return head.next;
}

// 返回给定文件的内容
static char *read_file(char *path) {
    FILE *fp;

    if (strcmp(path, "-") == 0) {
        // 按惯例，如果文件名为 "-" 则从标准输入读取
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp)
            error("cannot open %s: %s", path, strerror(errno));
    }

    char *buf;
    size_t buflen;
    FILE *out = open_memstream(&buf, &buflen);

    // 读取整个文件
    for (;;) {
        char buf2[4096];
        int n = fread(buf2, 1, sizeof(buf2), fp);
        if (n == 0)
            break;
        fwrite(buf2, 1, n, out);
    }

    if (fp != stdin)
        fclose(fp);

    // 确保最后一行以 '\n' 正确终止
    fflush(out);
    if (buflen == 0 || buf[buflen - 1] != '\n')
        fputc('\n', out);
    fputc('\0', out);
    fclose(out);
    return buf;
}

Token *tokenize_file(char *path) {
    return tokenize(path, read_file(path));
}
