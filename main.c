#include "xiaocc.h"
#include <sys/stat.h>

StringArray include_paths;

static char *opt_o;

static char *input_path;

static void usage(int status) {
    fprintf(stderr, "xiaocc [ -o <path> ] <file>\n");
    exit(status);
}

static void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help"))
            usage(0);

        if (!strcmp(argv[i], "-o")) {
            if (!argv[++i])
                usage(1);
            opt_o = argv[i];
            continue;
        }

        if (!strncmp(argv[i], "-o", 2)) {
            opt_o = argv[i] + 2;
            continue;
        }

        if (argv[i][0] == '-' && argv[i][1] != '\0')
            error("unknown argument: %s", argv[i]);

        input_path = argv[i];
    }

    if (!input_path)
        error("no input files");
}

static FILE *open_file(char *path) {
    if (!path || strcmp(path, "-") == 0)
        return stdout;

    FILE *out = fopen(path, "w");
    if (!out)
        error("cannot open output file: %s: %s", path, strerror(errno));
    return out;
}

bool file_exists(char *path) {
    struct stat st;
    return !stat(path, &st);
}

int main(int argc, char **argv) {
    parse_args(argc, argv);

    // 添加默认 include 路径 (ARM64)
    strarray_push(&include_paths, ".");
    strarray_push(&include_paths, "include");
    strarray_push(&include_paths, "/usr/local/include");
    strarray_push(&include_paths, "/usr/aarch64-linux-gnu/include");
    strarray_push(&include_paths, "/usr/include");

    // 初始化预定义宏
    init_macros();

    // 词法分析 + 语法分析
    Token *tok = tokenize_file(input_path);
    if (!tok)
        error("%s: %s", input_path, strerror(errno));
    tok = preprocess(tok);
    Obj *prog = parse(tok);

    // 遍历 AST 生成汇编
    FILE *out = open_file(opt_o);
    codegen(prog, out);
    return 0;
}
