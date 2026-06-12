#include "xiaocc.h"

// 接受 printf 风格的格式字符串，返回格式化后的字符串
char *format(char *fmt, ...) {
    char *buf;
    size_t buflen;
    FILE *out = open_memstream(&buf, &buflen);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fclose(out);
    return buf;
}

void strarray_push(StringArray *arr, char *s) {
    if (!arr->data) {
        arr->data = calloc(8, sizeof(char *));
        arr->cap = 8;
    }
    if (arr->len == arr->cap) {
        arr->cap *= 2;
        arr->data = realloc(arr->data, arr->cap * sizeof(char *));
    }
    arr->data[arr->len++] = s;
}
