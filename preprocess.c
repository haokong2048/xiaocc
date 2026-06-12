#include "xiaocc.h"

// 预处理器的入口函数
Token *preprocess(Token *tok) {
    convert_keywords(tok);
    return tok;
}
