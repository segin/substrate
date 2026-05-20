/*
 * h_errno.c — backing storage for the resolver error code.
 *
 * Substrate exposes h_errno as `*__h_errno()` (matches glibc's
 * thread-aware approach so callers don't have to recompile when we
 * add real per-thread storage).  For now the storage is process-wide
 * since the resolver is not yet thread-safe.
 */

#include <resolv.h>

static int g_h_errno;

int *__h_errno(void) {
    return &g_h_errno;
}
