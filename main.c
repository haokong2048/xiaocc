#include "xiaocc.h"

int main(int argc, char **argv) {
    if (argc != 2)
        error("%s: invalid number of arguments", argv[0]);

    // 词法分析 + 语法分析
    Token *tok = tokenize_file(argv[1]);
    Obj *prog = parse(tok);

    // 遍历 AST 生成汇编
    codegen(prog);

    return 0;
}
