#include "cc_backend.h"
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

int cc_compile_c_to_s(const char *in_c, const char *display_src, const char *out_s, const char *std_mode,
                      int emit_debug, cc_target_t target, int opt_level, int wall, int werror, int pedantic,
                      int pedantic_errors, int gnu89_inline_mode, int gnu89_inline_override, int i386_isa_level,
                      int i386_has_sse2, int i386_has_mmx, int i386_fp_math_mode, int implicit_funcdecl_override,
                      int pic,
                      cc_diag_t *diag) {
    cc_translation_unit_t tu;
    cc_ssa_module_t ssa;
    int rc = -1;
    int pointer_size = target == CC_TARGET_I386 ? 4 : 8;
    int timings = getenv("CC_TIMINGS") != NULL;
    double t_parse = 0.0;
    double t_sema = 0.0;
    double t_ast2ir = 0.0;
    double t_opt = 0.0;
    double t_emit = 0.0;
    double t0 = 0.0;

    if (diag != NULL) {
        diag->path[0] = '\0';
        diag->line = 0;
        diag->col = 0;
        diag->error_count = 0;
        diag->message[0] = '\0';
    }

    cc_frontend_set_pointer_size(pointer_size);
    cc_frontend_set_std_mode(std_mode);
    cc_frontend_set_gnu89_inline_mode(gnu89_inline_mode, gnu89_inline_override);
    cc_frontend_set_implicit_funcdecl_policy(implicit_funcdecl_override > 0, implicit_funcdecl_override >= 0);
    cc_frontend_set_diag_flags(wall, werror, pedantic, pedantic_errors);
    cc_backend_set_i386_isa_level(i386_isa_level);
    cc_backend_set_i386_sse2(i386_has_sse2);
    cc_backend_set_i386_mmx(i386_has_mmx);
    cc_backend_set_i386_fp_math_mode(i386_fp_math_mode);
    cc_ssa_set_pointer_size(pointer_size);

    if (timings) {
        t0 = now_seconds();
    }
    if (cc_parse_file(in_c, &tu, diag) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "parser failed");
        }
        return -1;
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
    if (timings) {
        t_ast2ir = now_seconds() - t0;
        fprintf(stderr, "cc: timing ast2ir=%.3fs\n", t_ast2ir);
        t0 = now_seconds();
    }
    if (cc_run_middle_passes(&ssa, opt_level, diag) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "middle-end optimization failed");
        }
        cc_ssa_module_free(&ssa);
        cc_tu_free(&tu);
        return -1;
    }
    if (timings) {
        t_opt = now_seconds() - t0;
        fprintf(stderr, "cc: timing opt=%.3fs\n", t_opt);
        t0 = now_seconds();
    }
    if (cc_emit_gas(&ssa, out_s, display_src, emit_debug, target, pic, diag) == 0) {
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
