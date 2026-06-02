#include "xiaocc.h"

int main(int argc, char **argv) {
    if (argc != 2)
        error("%s: invalid number of arguments", argv[0]);

    Token *tok = tokenize(argv[1]);
    Function *prog = parse(tok);

    // 遍历 AST 生成汇编
    codegen(prog);

    return 0;
}
