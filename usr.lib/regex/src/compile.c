#include <stdlib.h>
#include <string.h>

#include "regex_internal.h"

static regex_limits_t regex_limits_default_impl(void) {
    regex_limits_t limits;
    limits.max_states = 8192;
    limits.max_captures = 64;
    limits.match_steps = 1000000;
    limits.max_matches = 10000;
    limits.max_stream_buffer = 1024 * 1024;
    return limits;
}

regex_limits_t regex_default_limits(void) {
    return regex_limits_default_impl();
}

regex_err_t regex_set_limits(regex_t *re, const regex_limits_t *limits) {
    if (!re) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    if (!limits) {
        re->limits = regex_limits_default_impl();
        return REGEX_OK;
    }
    re->limits = *limits;
    return REGEX_OK;
}

regex_t *regex_compile(const char *pattern, unsigned flags, regex_err_t *out_err) {
    regex_t *re;
    const regex_engine_vtable *engine = NULL;
    regex_err_t err = REGEX_OK;

    if (!pattern) {
        if (out_err) {
            *out_err = REGEX_ERR_INVALID_ARGUMENT;
        }
        return NULL;
    }

    re = (regex_t *)calloc(1, sizeof(*re));
    if (!re) {
        if (out_err) {
            *out_err = REGEX_ERR_NOMEM;
        }
        return NULL;
    }

    re->flags = flags;
    re->limits = regex_limits_default_impl();

    if (flags & REGEX_FLAG_PCRE_COMPAT) {
        engine = regex_engine_pcre2_vtable();
        if (!engine) {
            err = REGEX_ERR_UNSUPPORTED;
            goto fail;
        }
    } else if (flags & REGEX_FLAG_SAFE_ENGINE) {
        engine = regex_engine_safe_vtable();
    } else if (!(flags & REGEX_FLAG_SAFE_ENGINE)) {
        engine = regex_engine_posix_vtable();
        if (!engine) {
            engine = regex_engine_safe_vtable();
        }
#ifdef REGEX_DEFAULT_ENGINE_RE2
        if (!engine) {
            engine = regex_engine_re2_vtable();
        }
#endif
    }

    if (!engine) {
        engine = regex_engine_safe_vtable();
    }

    if (!engine) {
        err = REGEX_ERR_INTERNAL;
        goto fail;
    }

    re->engine = engine;
    err = re->engine->compile(re, pattern, flags);
    if (err != REGEX_OK) {
        goto fail;
    }

    if (engine == regex_engine_safe_vtable()) {
        re->flags |= REGEX_FLAG_SAFE_ENGINE;
    }

    if (out_err) {
        *out_err = REGEX_OK;
    }
    return re;

fail:
    if (out_err) {
        *out_err = err;
    }
    free(re);
    return NULL;
}

void regex_free(regex_t *re) {
    if (!re) {
        return;
    }
    if (re->engine && re->engine->destroy) {
        re->engine->destroy(re);
    }
    free(re);
}

size_t regex_capture_count(const regex_t *re) {
    if (!re) {
        return 0;
    }
    return re->capture_count;
}
