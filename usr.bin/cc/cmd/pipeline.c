#include "cc_backend.h"
#include "cc_ctype.h"
#include "cc_frontend.h"
#include "cc_middle.h"
#include "cc_pipeline.h"
#include "cc_ssa.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

/**
 * Write the IR as it stands after a pass, for -fdump-ir: after every
 * pass into <name>.<pass>.ir with -fdump-ir=all, otherwise after the
 * last one only, into <name>.ir.
 *
 * @param ssa          the module
 * @param opts         the settings, which name the dump
 * @param pass         the pass just run
 * @param last         this is the IR the backend gets
 * @param display_src  the source file, the module's name
 * @param diag         receives the error
 * @return             0, or -1 when the file cannot be written
 */
static int dump_ir_after(const cc_ssa_module_t *ssa, const cc_options_t *opts, const char *pass, int last,
                         const char *display_src, cc_diag_t *diag) {
    char path[4096];
    FILE *fp;
    int rc;

    if (opts->dump_ir == NULL || (!last && !opts->dump_ir_all)) {
        return 0;
    }
    if (opts->dump_ir_all) {
        snprintf(path, sizeof(path), "%s.%s.ir", opts->dump_ir, pass);
    } else {
        snprintf(path, sizeof(path), "%s.ir", opts->dump_ir);
    }
    fp = fopen(path, "w");
    rc = fp != NULL ? cc_ssa_dump_module(ssa, display_src, opts->target == CC_TARGET_I386 ? 4 : 8, fp) : -1;
    if (fp != NULL && fclose(fp) != 0) {
        rc = -1;
    }
    if (rc != 0 && diag != NULL) {
        snprintf(diag->message, sizeof(diag->message), "cannot write the IR dump %.200s", path);
    }
    return rc;
}

/**
 * Compile one preprocessed C file to assembly.
 *
 * @param in_c         the preprocessed source
 * @param display_src  the name for diagnostics and debug info
 * @param out_s        the assembly file to write
 * @param opts         the settings
 * @param diag         receives the first error; may be NULL
 * @return             0, or -1 on error
 */
int cc_compile_c_to_s(const char *in_c, const char *display_src, const char *out_s, const cc_options_t *opts,
                      cc_diag_t *diag) {
    cc_translation_unit_t tu;
    cc_ssa_module_t ssa;
    int rc = -1;
    int timings = getenv("CC_TIMINGS") != NULL;
    int debug_counts = getenv("CC_DEBUG_COUNTS") != NULL;
    double t_parse = 0.0;
    double t_sema = 0.0;
    double t_ast2ir = 0.0;
    double t_opt = 0.0;
    double t_emit = 0.0;
    double t0 = 0.0;

    if (diag != NULL) {
        if (display_src != NULL && display_src[0] != '\0') {
            snprintf(diag->path, sizeof(diag->path), "%s", display_src);
        } else {
            diag->path[0] = '\0';
        }
        diag->line = 0;
        diag->col = 0;
        diag->error_count = 0;
        diag->message[0] = '\0';
    }

    cc_options_select(opts);
    cc_ct_reset();

    if (timings) {
        t0 = now_seconds();
    }
    if (cc_parse_file(in_c, &tu, diag) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "parser failed");
        }
        return -1;
    }
    if (diag != NULL && display_src != NULL && display_src[0] != '\0') {
        snprintf(diag->path, sizeof(diag->path), "%s", display_src);
    }
    if (debug_counts) {
        fprintf(stderr, "cc: debug: parsed funcs=%zu globals=%zu structs=%zu from %s\n", tu.func_count, tu.global_count,
                tu.struct_count, in_c != NULL ? in_c : "<null>");
    }
    if (timings) {
        t_parse = now_seconds() - t0;
        fprintf(stderr, "cc: timing parse=%.3fs\n", t_parse);
        t0 = now_seconds();
    }
    if (cc_sema_check(&tu, diag) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "semantic analysis failed");
        }
        cc_tu_free(&tu);
        return -1;
    }
    if (timings) {
        t_sema = now_seconds() - t0;
        fprintf(stderr, "cc: timing sema=%.3fs\n", t_sema);
        t0 = now_seconds();
    }
    if (cc_ast_to_ssa(&tu, &ssa, diag) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "AST to SSA lowering failed");
        }
        cc_tu_free(&tu);
        return -1;
    }
    if (dump_ir_after(&ssa, opts, "lower", 0, display_src, diag) != 0) {
        cc_ssa_module_free(&ssa);
        cc_tu_free(&tu);
        return -1;
    }
    if (cc_middle_legalize_module(&ssa, diag) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "SSA legalization failed");
        }
        cc_ssa_module_free(&ssa);
        cc_tu_free(&tu);
        return -1;
    }
    if (dump_ir_after(&ssa, opts, "legalize", 0, display_src, diag) != 0) {
        cc_ssa_module_free(&ssa);
        cc_tu_free(&tu);
        return -1;
    }
    if (timings) {
        t_ast2ir = now_seconds() - t0;
        fprintf(stderr, "cc: timing ast2ir=%.3fs\n", t_ast2ir);
        t0 = now_seconds();
    }
    if (debug_counts) {
        fprintf(stderr, "cc: debug: lowered funcs=%zu globals=%zu\n", ssa.func_count, ssa.global_count);
    }
    if (cc_run_middle_passes(&ssa, opts->opt_level, diag) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "middle-end optimization failed");
        }
        cc_ssa_module_free(&ssa);
        cc_tu_free(&tu);
        return -1;
    }
    if (dump_ir_after(&ssa, opts, "opt", 1, display_src, diag) != 0) {
        cc_ssa_module_free(&ssa);
        cc_tu_free(&tu);
        return -1;
    }
    if (timings) {
        t_opt = now_seconds() - t0;
        fprintf(stderr, "cc: timing opt=%.3fs\n", t_opt);
        t0 = now_seconds();
    }
    if (cc_emit_gas(&ssa, out_s, display_src, opts->debug, opts->target, opts->pic, diag) == 0) {
        rc = 0;
    } else if (diag != NULL && diag->message[0] == '\0') {
        snprintf(diag->message, sizeof(diag->message), "backend emission failed");
    }
    if (timings) {
        t_emit = now_seconds() - t0;
        fprintf(stderr, "cc: timing parse=%.3fs sema=%.3fs ast2ir=%.3fs opt=%.3fs emit=%.3fs total=%.3fs\n", t_parse,
                t_sema, t_ast2ir, t_opt, t_emit, t_parse + t_sema + t_ast2ir + t_opt + t_emit);
    }

    cc_ssa_module_free(&ssa);
    cc_tu_free(&tu);
    return rc;
}
