#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *xstrdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static char *normalize_to_string(const char *path, ir_error_t *err) {
    ir_module_t m;
    FILE *tmp;
    long sz;
    char *buf;

    ir_module_init(&m);

    if (ir_parse_file(path, &m, err) != 0) {
        ir_module_free(&m);
        return NULL;
    }
    if (ir_verify_module(&m, err) != 0) {
        ir_module_free(&m);
        return NULL;
    }

    tmp = tmpfile();
    if (tmp == NULL) {
        err->line = 0;
        err->msg = xstrdup("tmpfile failed");
        ir_module_free(&m);
        return NULL;
    }

    if (ir_serialize_module(&m, tmp, 1) != 0) {
        fclose(tmp);
        ir_module_free(&m);
        err->line = 0;
        err->msg = xstrdup("serialization failed");
        return NULL;
    }

    if (fseek(tmp, 0, SEEK_END) != 0) {
        fclose(tmp);
        ir_module_free(&m);
        err->line = 0;
        err->msg = xstrdup("fseek failed");
        return NULL;
    }
    sz = ftell(tmp);
    if (sz < 0 || fseek(tmp, 0, SEEK_SET) != 0) {
        fclose(tmp);
        ir_module_free(&m);
        err->line = 0;
        err->msg = xstrdup("ftell/fseek failed");
        return NULL;
    }

    buf = (char *)malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(tmp);
        ir_module_free(&m);
        err->line = 0;
        err->msg = xstrdup("out of memory");
        return NULL;
    }

    if (fread(buf, 1, (size_t)sz, tmp) != (size_t)sz) {
        fclose(tmp);
        ir_module_free(&m);
        free(buf);
        err->line = 0;
        err->msg = xstrdup("fread failed");
        return NULL;
    }
    buf[sz] = '\0';

    fclose(tmp);
    ir_module_free(&m);
    return buf;
}

int main(int argc, char **argv) {
    ir_error_t err;
    char *a;
    char *b;
    int rc;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <left.ir> <right.ir>\n", argv[0]);
        return 2;
    }

    err.msg = NULL;
    err.line = 0;

    a = normalize_to_string(argv[1], &err);
    if (a == NULL) {
        fprintf(stderr, "%s:%zu: %s\n", argv[1], err.line, err.msg ? err.msg : "error");
        ir_error_free(&err);
        return 1;
    }

    b = normalize_to_string(argv[2], &err);
    if (b == NULL) {
        fprintf(stderr, "%s:%zu: %s\n", argv[2], err.line, err.msg ? err.msg : "error");
        ir_error_free(&err);
        free(a);
        return 1;
    }

    rc = strcmp(a, b) == 0 ? 0 : 1;
    if (rc != 0) {
        fprintf(stderr, "IR differs after normalization\n");
    }

    free(a);
    free(b);
    return rc;
}
