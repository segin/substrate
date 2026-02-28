#include "cc_backend.h"
#include "cc_frontend.h"
#include "cc_middle.h"
#include "cc_pipeline.h"
#include "cc_ssa.h"

int cc_compile_c_to_s(const char *in_c, const char *display_src, const char *out_s, const char *std_mode,
                      int emit_debug, cc_target_t target, int opt_level, int wall, int werror, int pedantic,
                      int pedantic_errors, int gnu89_inline_mode, int gnu89_inline_override, int i386_isa_level,
                      int i386_has_sse2, int i386_has_mmx, int i386_fp_math_mode,
                      cc_diag_t *diag) {
    cc_translation_unit_t tu;
    cc_ssa_module_t ssa;
    int rc = -1;
    int pointer_size = target == CC_TARGET_I386 ? 4 : 8;

    if (diag != NULL) {
        diag->path[0] = '\0';
        diag->line = 0;
        diag->col = 0;
        diag->message[0] = '\0';
    }

    cc_frontend_set_pointer_size(pointer_size);
    cc_frontend_set_std_mode(std_mode);
    cc_frontend_set_gnu89_inline_mode(gnu89_inline_mode, gnu89_inline_override);
    cc_frontend_set_diag_flags(wall, werror, pedantic, pedantic_errors);
    cc_backend_set_i386_isa_level(i386_isa_level);
    cc_backend_set_i386_sse2(i386_has_sse2);
    cc_backend_set_i386_mmx(i386_has_mmx);
    cc_backend_set_i386_fp_math_mode(i386_fp_math_mode);
    cc_ssa_set_pointer_size(pointer_size);

    if (cc_parse_file(in_c, &tu, diag) != 0) {
        return -1;
    }
    if (cc_sema_check(&tu, diag) != 0) {
        cc_tu_free(&tu);
        return -1;
    }
    if (cc_ast_to_ssa(&tu, &ssa, diag) != 0) {
        cc_tu_free(&tu);
        return -1;
    }
    if (cc_run_middle_passes(&ssa, opt_level, diag) != 0) {
        cc_ssa_module_free(&ssa);
        cc_tu_free(&tu);
        return -1;
    }
    if (cc_emit_gas(&ssa, out_s, display_src, emit_debug, target, diag) == 0) {
        rc = 0;
    }

    cc_ssa_module_free(&ssa);
    cc_tu_free(&tu);
    return rc;
}
