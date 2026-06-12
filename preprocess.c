#include "xiaocc.h"

static bool is_hash(Token *tok) {
    return tok->at_bol && equal(tok, "#");
}

// 访问所有 token，同时评估预处理宏和指令
static Token *preprocess2(Token *tok) {
    Token head = {};
    Token *cur = &head;

    while (tok->kind != TK_EOF) {
        // 如果不是 "#"，直接传递
        if (!is_hash(tok)) {
            cur = cur->next = tok;
            tok = tok->next;
            continue;
        }

        tok = tok->next;

        // 仅 "#" 的行是合法的，称为空指令
        if (tok->at_bol)
            continue;

        error_tok(tok, "invalid preprocessor directive");
    }

    cur->next = tok;
    return head.next;
}

// 预处理器的入口函数
Token *preprocess(Token *tok) {
    tok = preprocess2(tok);
    convert_keywords(tok);
    return tok;
}
