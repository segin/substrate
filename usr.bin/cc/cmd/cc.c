#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "cc_driver.h"
#include "cc_frontend.h"
#include "cc_diag.h"
#include "cc_lang.h"
#include "cc_options.h"
#include "cc_pipeline.h"
#include "cc_util.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef CC_SUBSTRATE_BUILD
/*
 * The system keeps a runtime for each of its two ABIs: i386 under /usr/lib
 * and /lib with /sbin/ld.so, x86-64 under /usr/lib64 and /lib64 with
 * /sbin/ld64.so.  The target chooses between them; the default target is
 * the one this program was itself built for.
 */
#define CC_SUBSTRATE_LIBDIR32   "/usr/lib"     /* static .a + crt0.o */
#define CC_SUBSTRATE_SHLIBDIR32 "/lib"         /* dynamic .so.0 */
#define CC_SUBSTRATE_LDSO32     "/sbin/ld.so"
#define CC_SUBSTRATE_LIBDIR64   "/usr/lib64"
#define CC_SUBSTRATE_SHLIBDIR64 "/lib64"
#define CC_SUBSTRATE_LDSO64     "/sbin/ld64.so"
#ifdef __x86_64__
#define CC_DEFAULT_TARGET CC_TARGET_X86_64
#else
#define CC_DEFAULT_TARGET CC_TARGET_I386
#endif
#define CC_SUBSTRATE_INCDIR  "/usr/include"
#define CC_SUBSTRATE_LOCAL_INCDIR "/usr/local/include"
#endif

#define SUBSTRATE_TC_DEPTH_ENV "SUBSTRATE_TC_DEPTH"
#define SUBSTRATE_TC_TRACE_ENV "SUBSTRATE_TC_TRACE"
#define SUBSTRATE_TC_MAX_ENV "SUBSTRATE_TC_MAX_DEPTH"
#define SUBSTRATE_TC_DEFAULT_MAX 64

typedef struct {
  const char *self_path;
  int invoked_as_cpp;
  int mode_E;
  int mode_S;
  int mode_c;
  int verbose;
  int dry_run;
  int wall;
  int werror;
  int pedantic;
  int pedantic_errors;
  int debug;
  int shared;
  int pic;
  int pthread_flag;
  int emit_ssa;
  int dump_ir; /**< -fdump-ir: 1; -fdump-ir=all: 2 */
  int bootstrap_gcc;
  int query_print_file_name;
  int query_print_libgcc_file_name;
  int nostdlib;
  int nostdinc;
  int nodefaultlibs;
  int gnu89_inline_mode;
  int gnu89_inline_override;
  int i386_isa_level;
  int i386_has_sse;
  int i386_has_sse2;
  int i386_has_mmx;
  int i386_supports_sse;
  int i386_supports_sse2;
  int i386_supports_mmx;
  int i386_fp_math_mode;
  int implicit_funcdecl_override;
  int i386_arch_explicit;
  cc_target_t target;

  const char *std;
  const char *opt_level;
  const char *output;
  const char *forced_lang;
  const char *print_file_name;
  const char *parse_bad_arg;

  cc_strvec_t cpp_flags;
  cc_strvec_t c_flags;
  cc_strvec_t as_flags;
  cc_strvec_t ld_flags;
  cc_strvec_t inputs;
  cc_strvec_t temp_files;
} cc_opts_t;

typedef struct {
  struct sigaction old;
  int active;
} sigchld_guard_t;


static const char *target_gcc_flag(cc_target_t target) {
  return cc_target_get(target)->m_flag;
}

/**
 * The optimization level the command line asks for: 0 without -O, 1 for
 * a bare -O (as gcc takes it), else the digit after -O.
 *
 * @param o  the parsed command line
 * @return   0 to 3
 */
static int opt_level_num(const cc_opts_t *o) {
  if (o->opt_level == NULL) {
    return 0;
  }
  if (o->opt_level[0] == '\0') {
    return 1;
  }
  if (o->opt_level[0] < '0' || o->opt_level[0] > '3') {
    return 0;
  }
  return o->opt_level[0] - '0';
}

/**
 * Collect the settings the compiler proper needs from the parsed
 * command line.
 *
 * @param o    the parsed command line
 * @param out  receives the compilation settings
 */
static void compile_options(const cc_opts_t *o, cc_options_t *out) {
  cc_options_init(out);
  /* parse_args has already rejected an unknown -std= */
  cc_lang_from_std(o->std, &out->lang);
  out->target = o->target;
  out->opt_level = opt_level_num(o);
  out->debug = o->debug;
  out->pic = o->pic || o->shared;
  out->warn_all = o->wall;
  out->warn_error = o->werror;
  out->pedantic = o->pedantic;
  out->pedantic_errors = o->pedantic_errors;
  out->gnu89_inline = o->gnu89_inline_override ? o->gnu89_inline_mode : -1;
  out->implicit_funcdecl = o->implicit_funcdecl_override;
  out->i386_isa_level = o->i386_isa_level;
  out->i386_sse2 = o->i386_has_sse2;
  out->i386_mmx = o->i386_has_mmx;
  out->i386_fpmath = (cc_fpmath_t)o->i386_fp_math_mode;
}


static void sigchld_guard_begin(sigchld_guard_t *guard) {
  struct sigaction act;

  memset(guard, 0, sizeof(*guard));
  if (sigaction(SIGCHLD, NULL, &guard->old) != 0) {
    return;
  }
  act = guard->old;
  act.sa_handler = SIG_DFL;
  act.sa_flags &= ~SA_NOCLDWAIT;
  if (sigemptyset(&act.sa_mask) != 0) {
    return;
  }
  if (sigaction(SIGCHLD, &act, NULL) != 0) {
    return;
  }
  guard->active = 1;
}

static void sigchld_guard_end(sigchld_guard_t *guard) {
  if (guard != NULL && guard->active) {
    sigaction(SIGCHLD, &guard->old, NULL);
    guard->active = 0;
  }
}



static void opts_free(cc_opts_t *o) {
  cc_strvec_free(&o->cpp_flags);
  cc_strvec_free(&o->c_flags);
  cc_strvec_free(&o->as_flags);
  cc_strvec_free(&o->ld_flags);
  cc_strvec_free(&o->inputs);
  cc_strvec_free(&o->temp_files);
}

/**
 * In a forked child whose exec failed: say which tool could not be run
 * and why, as gcc does, then exit 127.  write(2), not stdio, so the
 * parent's buffered output is not flushed a second time by the child.
 *
 * @param tool  the program that could not be executed
 * @param err   the errno exec failed with
 */
static void exec_failed_exit(const char *tool, int err) {
  char msg[512];
  int n = snprintf(msg, sizeof(msg), "cc: fatal error: cannot execute '%s': %s\n", tool, strerror(err));
  if (n > 0) {
    ssize_t w = write(STDERR_FILENO, msg, (size_t)n < sizeof(msg) ? (size_t)n : sizeof(msg) - 1);
    (void)w;
  }
  _exit(127);
}

static int run_cmd(const cc_opts_t *o, char **argv) {
  pid_t pid;
  int status;
  size_t i;
  int debug_subproc = getenv("CC_DEBUG_SUBPROC") != NULL;
  sigchld_guard_t sigchld_guard;

  if (o->verbose || o->dry_run) {
    for (i = 0; argv[i] != NULL; ++i) {
      fprintf(stderr, "%s%s", i == 0 ? "" : " ", argv[i]);
    }
    fputc('\n', stderr);
  }

  if (o->dry_run) {
    return 0;
  }

  sigchld_guard_begin(&sigchld_guard);
  pid = fork();
  if (pid < 0) {
    fprintf(stderr, "cc: error: cannot run '%s': %s\n", argv[0], strerror(errno));
    sigchld_guard_end(&sigchld_guard);
    return -1;
  }

  if (pid == 0) {
    execvp(argv[0], argv);
    exec_failed_exit(argv[0], errno);
  }

  if (waitpid(pid, &status, 0) < 0) {
    if (errno == ECHILD && wait(&status) >= 0) {
      if (debug_subproc) {
        fprintf(stderr, "cc: debug: waitpid fallback via wait() for %s status=0x%x\n", argv[0], status);
      }
    } else {
    if (debug_subproc) {
      fprintf(stderr, "cc: debug: waitpid failed for %s: %s\n", argv[0],
              strerror(errno));
    }
    sigchld_guard_end(&sigchld_guard);
    return -1;
    }
  }
  if (debug_subproc) {
    fprintf(stderr, "cc: debug: child %s status=0x%x exited=%d exitstatus=%d signaled=%d termsig=%d\n", argv[0],
            status, WIFEXITED(status), WEXITSTATUS(status), WIFSIGNALED(status), WTERMSIG(status));
  }

  if (WIFSIGNALED(status)) {
    /* the tool printed nothing itself; say what became of it */
    fprintf(stderr, "cc: error: %s terminated by signal %d (%s)\n", argv[0], WTERMSIG(status),
            strsignal(WTERMSIG(status)));
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    sigchld_guard_end(&sigchld_guard);
    return -1;
  }

  sigchld_guard_end(&sigchld_guard);
  return 0;
}

static int capture_cmd_output(char *const argv[], char *out, size_t outsz) {
  int pipefd[2];
  pid_t pid;
  int status;
  ssize_t nread;
  size_t used = 0;
  sigchld_guard_t sigchld_guard;

  if (pipe(pipefd) != 0) {
    return -1;
  }

  sigchld_guard_begin(&sigchld_guard);
  pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    sigchld_guard_end(&sigchld_guard);
    return -1;
  }

  if (pid == 0) {
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    execvp(argv[0], argv);
    _exit(127); /* a probe: a missing program is an answer, not an error */
  }

  close(pipefd[1]);
  while ((nread = read(pipefd[0], out + used, outsz - used - 1)) > 0) {
    used += (size_t)nread;
    if (used + 1 >= outsz) {
      break;
    }
  }
  close(pipefd[0]);

  if (waitpid(pid, &status, 0) < 0) {
    if (errno == ECHILD && wait(&status) >= 0) {
      ;
    } else {
    sigchld_guard_end(&sigchld_guard);
    return -1;
    }
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    sigchld_guard_end(&sigchld_guard);
    return -1;
  }
  sigchld_guard_end(&sigchld_guard);

  out[used] = '\0';
  while (used > 0 && (out[used - 1] == '\n' || out[used - 1] == '\r' ||
                      out[used - 1] == ' ' || out[used - 1] == '\t')) {
    out[--used] = '\0';
  }
  return 0;
}

static int as_supports_from_cc_flag(const char *as_prog) {
#ifdef CC_SUBSTRATE_BUILD
  (void)as_prog;
  return 1;
#else
  static char cached_prog[PATH_MAX];
  static int cached = -1;
  char buf[512];
  char *argv[3];

  if (as_prog == NULL || as_prog[0] == '\0') {
    return 0;
  }
  if (cached >= 0 && strcmp(cached_prog, as_prog) == 0) {
    return cached;
  }

  argv[0] = (char *)as_prog;
  argv[1] = "--target-help";
  argv[2] = NULL;
  if (capture_cmd_output(argv, buf, sizeof(buf)) != 0) {
    cached = 0;
  } else {
    cached = strstr(buf, "Substrate assembler target help:") != NULL ? 1 : 0;
  }

  snprintf(cached_prog, sizeof(cached_prog), "%s", as_prog);
  return cached;
#endif
}

static int gcc_print_file_name(const char *name, cc_target_t target,
                               char out[PATH_MAX]) {
  char opt[PATH_MAX];
  char *argv[4];

  if (snprintf(opt, sizeof(opt), "-print-file-name=%s", name) >=
      (int)sizeof(opt)) {
    return -1;
  }
  argv[0] = "gcc";
  argv[1] = (char *)target_gcc_flag(target);
  argv[2] = opt;
  argv[3] = NULL;
  if (capture_cmd_output(argv, out, PATH_MAX) != 0) {
    return -1;
  }
  if (strcmp(out, name) == 0) {
    return -1;
  }
  return 0;
}

#ifdef CC_SUBSTRATE_BUILD
static const char *substrate_sysroot(void) {
  const char *v = getenv("SUBSTRATE_SYSROOT");
  return (v != NULL && v[0] != '\0') ? v : NULL;
}

static int substrate_join_root(char out[PATH_MAX], const char *root,
                               const char *suffix) {
  if (out == NULL || suffix == NULL) {
    return -1;
  }
  if (root != NULL) {
    return snprintf(out, PATH_MAX, "%s%s", root, suffix) >= PATH_MAX ? -1 : 0;
  }
  return snprintf(out, PATH_MAX, "%s", suffix) >= PATH_MAX ? -1 : 0;
}

static int substrate_print_runtime_file(const char *name, cc_target_t target,
                                        char out[PATH_MAX]) {
  const char *root = substrate_sysroot();
  const char *libdir = target == CC_TARGET_I386 ? CC_SUBSTRATE_LIBDIR32
                                                : CC_SUBSTRATE_LIBDIR64;
  const char *shlibdir = target == CC_TARGET_I386 ? CC_SUBSTRATE_SHLIBDIR32
                                                  : CC_SUBSTRATE_SHLIBDIR64;
  if (name == NULL || out == NULL) {
    return -1;
  }
  if (strcmp(name, "include") == 0 || strcmp(name, "include-fixed") == 0) {
    return substrate_join_root(out, root, CC_SUBSTRATE_INCDIR);
  }
  if (strcmp(name, "crt0.o") == 0 || strcmp(name, "libc.a") == 0 ||
      strcmp(name, "libm.a") == 0 || strcmp(name, "libgcc.a") == 0) {
    if (root != NULL) {
      if (snprintf(out, PATH_MAX, "%s%s/%s", root, libdir, name) >=
          PATH_MAX) {
        return -1;
      }
    } else if (snprintf(out, PATH_MAX, "%s/%s", libdir, name) >=
               PATH_MAX) {
      return -1;
    }
    return 0;
  }
  /* Shared libraries live in /lib (not /usr/lib) per the install
   * policy in the per-component Makefiles under lib/ and usr.lib/. */
  if (strcmp(name, "libc.so.0") == 0 || strcmp(name, "libm.so.0") == 0) {
    if (root != NULL) {
      if (snprintf(out, PATH_MAX, "%s%s/%s", root, shlibdir,
                   name) >= PATH_MAX) {
        return -1;
      }
    } else if (snprintf(out, PATH_MAX, "%s/%s", shlibdir, name) >=
               PATH_MAX) {
      return -1;
    }
    return 0;
  }
  return -1;
}
#endif

static int canonicalize_path(const char *in, char out[PATH_MAX]) {
  char saved[PATH_MAX];
  char *resolved;

  if (in == NULL || in[0] == '\0') {
    return -1;
  }
  /* `in` and `out` may alias.  Substrate's realpath() seeds the
   * output buffer with the absolute prefix before walking the path,
   * so by the time it returns NULL on a nonexistent target the
   * caller's `in` has been clobbered to `/` and the fallback
   * memcpy below would silently produce a stray `/` on the cc
   * link line.  Snapshot first. */
  if (strlen(in) >= sizeof(saved)) {
    return -1;
  }
  memcpy(saved, in, strlen(in) + 1);

  resolved = realpath(saved, out);
  if (resolved != NULL) {
    return 0;
  }
  memcpy(out, saved, strlen(saved) + 1);
  return 0;
}

static int gcc_print_runtime_file(const char *name, cc_target_t target,
                                  char out[PATH_MAX]) {
  char raw[PATH_MAX];

#ifdef CC_SUBSTRATE_BUILD
  if (substrate_print_runtime_file(name, target, raw) != 0) {
    return -1;
  }
  return canonicalize_path(raw, out);
#else
  if (gcc_print_file_name(name, target, raw) != 0) {
    return -1;
  }
  return canonicalize_path(raw, out);
#endif
}

static int gcc_print_libgcc(cc_target_t target, char out[PATH_MAX]) {
#ifdef CC_SUBSTRATE_BUILD
  if (substrate_print_runtime_file("libgcc.a", target, out) != 0) {
    return -1;
  }
  return canonicalize_path(out, out);
#else
  char *argv[4];

  argv[0] = "gcc";
  argv[1] = (char *)target_gcc_flag(target);
  argv[2] = "-print-libgcc-file-name";
  argv[3] = NULL;
  if (capture_cmd_output(argv, out, PATH_MAX) != 0) {
    return -1;
  }
  return canonicalize_path(out, out);
#endif
}

static int has_ext(const char *path, const char *ext) {
  size_t pn;
  size_t en;

  pn = strlen(path);
  en = strlen(ext);
  if (pn < en) {
    return 0;
  }
  return strcmp(path + pn - en, ext) == 0;
}

static const char *prog_basename(const char *path) {
  const char *slash = strrchr(path, '/');
  if (slash == NULL || slash[1] == '\0') {
    return path;
  }
  return slash + 1;
}

static long parse_env_long(const char *s, long fallback) {
  char *end;
  long v;

  if (s == NULL || s[0] == '\0') {
    return fallback;
  }
  errno = 0;
  v = strtol(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0') {
    return fallback;
  }
  return v;
}

static int toolchain_guard_enter(const char *tool) {
  long depth;
  long max_depth;
  char depth_buf[32];
  char trace_buf[256];
  const char *trace;
  int n;

  depth = parse_env_long(getenv(SUBSTRATE_TC_DEPTH_ENV), 0);
  if (depth < 0) {
    depth = 0;
  }

  max_depth =
      parse_env_long(getenv(SUBSTRATE_TC_MAX_ENV), SUBSTRATE_TC_DEFAULT_MAX);
  if (max_depth < 4) {
    max_depth = 4;
  }
  if (depth >= max_depth) {
    trace = getenv(SUBSTRATE_TC_TRACE_ENV);
    fprintf(
        stderr,
        "cc: fatal: toolchain recursion guard hit at depth %ld (max %ld)%s%s\n",
        depth, max_depth, trace != NULL && trace[0] != '\0' ? ", trace: " : "",
        trace != NULL && trace[0] != '\0' ? trace : "");
    return -1;
  }

  snprintf(depth_buf, sizeof(depth_buf), "%ld", depth + 1);
  if (setenv(SUBSTRATE_TC_DEPTH_ENV, depth_buf, 1) != 0) {
    return -1;
  }

  trace = getenv(SUBSTRATE_TC_TRACE_ENV);
  if (trace == NULL || trace[0] == '\0') {
    n = snprintf(trace_buf, sizeof(trace_buf), "%s", tool);
  } else {
    n = snprintf(trace_buf, sizeof(trace_buf), "%s->%s", trace, tool);
  }
  if (n < 0) {
    return -1;
  }
  if ((size_t)n >= sizeof(trace_buf)) {
    trace_buf[sizeof(trace_buf) - 1] = '\0';
  }
  if (setenv(SUBSTRATE_TC_TRACE_ENV, trace_buf, 1) != 0) {
    return -1;
  }
  return 0;
}

static int strvec_has_flag(const cc_strvec_t *v, const char *flag) {
  size_t i;
  if (v == NULL || flag == NULL) {
    return 0;
  }
  for (i = 0; i < v->count; ++i) {
    if (strcmp(v->items[i], flag) == 0) {
      return 1;
    }
  }
  return 0;
}


static int add_gcc_system_include(cc_opts_t *o, const char *name) {
  char path[PATH_MAX];
  if (o == NULL || name == NULL) {
    return 0;
  }
#ifdef CC_SUBSTRATE_BUILD
  if (strcmp(name, "include") == 0) {
    char sys_inc[PATH_MAX];
    char local_inc[PATH_MAX];
    if (substrate_join_root(sys_inc, substrate_sysroot(), CC_SUBSTRATE_INCDIR) !=
            0 ||
        substrate_join_root(local_inc, substrate_sysroot(),
                            CC_SUBSTRATE_LOCAL_INCDIR) != 0) {
      return -1;
    }
    if (!cc_strvec_contains(&o->cpp_flags, sys_inc) &&
        (cc_strvec_push(&o->cpp_flags, "-isystem") != 0 ||
         cc_strvec_push(&o->cpp_flags, sys_inc) != 0)) {
      return -1;
    }
    if (!cc_strvec_contains(&o->cpp_flags, local_inc) &&
        (cc_strvec_push(&o->cpp_flags, "-isystem") != 0 ||
         cc_strvec_push(&o->cpp_flags, local_inc) != 0)) {
      return -1;
    }
    return 0;
  }
  if (strcmp(name, "include-fixed") == 0) {
    return 0;
  }
#endif
  if (gcc_print_file_name(name, o->target, path) != 0) {
    return 0;
  }
  if (cc_strvec_contains(&o->cpp_flags, path)) {
    return 0;
  }
  if (cc_strvec_push(&o->cpp_flags, "-isystem") != 0 ||
      cc_strvec_push(&o->cpp_flags, path) != 0) {
    return -1;
  }
  return 0;
}

static int add_gcc_system_includes(cc_opts_t *o) {
  if (o == NULL || o->nostdinc || strvec_has_flag(&o->cpp_flags, "-nostdinc")) {
    return 0;
  }
  if (add_gcc_system_include(o, "include") != 0) {
    return -1;
  }
  if (add_gcc_system_include(o, "include-fixed") != 0) {
    return -1;
  }
  return 0;
}

static int add_builtin_resource_includes(cc_opts_t *o) {
  char path[PATH_MAX];
  char self_buf[PATH_MAX];
  const char *self;
  const char *slash;
  size_t dirlen;

  if (o == NULL || o->nostdinc || strvec_has_flag(&o->cpp_flags, "-nostdinc") ||
      o->self_path == NULL || o->self_path[0] == '\0') {
    return 0;
  }

  self = o->self_path;
  if (strchr(self, '/') == NULL) {
    ssize_t n = readlink("/proc/self/exe", self_buf, sizeof(self_buf) - 1);
    if (n > 0) {
      self_buf[n] = '\0';
      self = self_buf;
    }
  }
  slash = strrchr(self, '/');
  if (slash == NULL) {
    return 0;
  }
  dirlen = (size_t)(slash - self);

  if (snprintf(path, sizeof(path), "%.*s/resource/include", (int)dirlen, self) > 0 &&
      access(path, R_OK | X_OK) == 0) {
    if (!cc_strvec_contains(&o->cpp_flags, path) &&
        (cc_strvec_push(&o->cpp_flags, "-isystem") != 0 || cc_strvec_push(&o->cpp_flags, path) != 0)) {
      return -1;
    }
    return 0;
  }

  if (snprintf(path, sizeof(path), "%.*s/../lib/substratecc/include", (int)dirlen, self) > 0 &&
      access(path, R_OK | X_OK) == 0) {
    if (!cc_strvec_contains(&o->cpp_flags, path) &&
        (cc_strvec_push(&o->cpp_flags, "-isystem") != 0 || cc_strvec_push(&o->cpp_flags, path) != 0)) {
      return -1;
    }
  }

  return 0;
}

static int make_temp_path(cc_opts_t *o, const char *prefix, const char *suffix,
                          char out[PATH_MAX]) {
  char *templ = NULL;
  int fd;
  int ret = 0;

  if (asprintf(&templ, "/tmp/%sXXXXXX", prefix) < 0) {
    return -1;
  }

  fd = mkstemp(templ);
  if (fd < 0) {
    free(templ);
    return -1;
  }
  close(fd);

  if (suffix != NULL) {
    char *with_suffix = NULL;
    if (asprintf(&with_suffix, "%s%s", templ, suffix) < 0) {
      unlink(templ);
      free(templ);
      return -1;
    }
    if (rename(templ, with_suffix) != 0) {
      unlink(templ);
      free(with_suffix);
      free(templ);
      return -1;
    }
    if (snprintf(out, PATH_MAX, "%s", with_suffix) >= PATH_MAX) {
      ret = -1;
    } else {
      ret = cc_strvec_push(&o->temp_files, out);
    }
    free(with_suffix);
  } else {
    if (snprintf(out, PATH_MAX, "%s", templ) >= PATH_MAX) {
      ret = -1;
    } else {
      ret = cc_strvec_push(&o->temp_files, out);
    }
  }

  free(templ);
  return ret;
}

static void cleanup_temp_files(const cc_opts_t *o) {
  size_t i;
  const char *keep;

  keep = getenv("CC_KEEP_TEMPS");
  if (keep != NULL && keep[0] != '\0' && strcmp(keep, "0") != 0) {
    return;
  }

  for (i = 0; i < o->temp_files.count; ++i) {
    unlink(o->temp_files.items[i]);
  }
}

static void usage(const char *prog) {
  fprintf(stderr,
          "usage: %s [options] file...\n"
          "  -E                 preprocess only\n"
          "  -S                 compile to assembly\n"
          "  -c                 compile/assemble only\n"
          "  -o <file>          output file\n"
          "  -I/-D/-U           preprocessor options\n"
          "  -include/-imacros  forced preprocessor include inputs\n"
          "  -idirafter         late include search path\n"
          "  -P -dM             preprocess formatting/macro dump modes\n"
          "  -fpp-max-*         preprocessor hard limits (depth/size/token "
          "growth)\n"
          "  -M/-MM/-MD/-MMD    dependency generation modes\n"
          "  -MF/-MT/-MQ        dependency output/target controls\n"
          "  -std=c90/c95/c99/c11/c17/c23   language mode\n"
          "  -O0..-O3           optimization level\n"
          "  -m32/-m64          target ABI (i386 or x86_64)\n"
          "  -march=<arch>      ISA level control (e.g. i386/i486/i586/i686)\n"
          "  -msse2/-mno-sse2   i386 SSE2 opcode enable/disable\n"
          "  -msse/-mno-sse     i386 SSE opcode enable/disable\n"
          "  -mmmx/-mno-mmx     i386 MMX opcode enable/disable\n"
          "  -mavx/-mno-avx     x86 AVX feature macro enable/disable\n"
          "  -mavx2/-mno-avx2   x86 AVX2 feature macro enable/disable\n"
          "  -mfpmath=sse|387   i386 scalar FP lowering mode\n"
          "  -g                 debug info\n"
          "  -Wall -Werror      warnings\n"
          "  -pedantic          warn on non-C99 extensions\n"
          "  -pedantic-errors   reject non-C99 extensions\n"
          "  -fPIC              position-independent code\n"
          "  -fgnu89-inline     GNU89 inline mode semantics\n"
          "  -fno-gnu89-inline  disable GNU89 inline mode override\n"
          "  -shared            create shared object (link)\n"
          "  -pthread           enable thread options\n"
          "  -v                 verbose stages\n"
          "  -###               print commands without executing\n"
          "  -emit-ssa          run IR verification utility on input .ir\n"
          "  -fdump-ir[=all]    write the IR to <name>.ir (after each pass with =all)\n"
          "  -print-file-name=<name>   print discovered runtime/include path\n"
          "  -print-libgcc-file-name   print discovered libgcc path\n"
          "  --bootstrap-gcc    temporary fallback C->.s via host gcc\n",
          prog);
}

static int i386_features_from_arch_name(const char *name, int *out_level,
                                        int *out_has_sse, int *out_has_sse2,
                                        int *out_has_mmx, int *out_supports_sse,
                                        int *out_supports_sse2,
                                        int *out_supports_mmx) {
  if (name == NULL || name[0] == '\0') {
    return 0;
  }
  if (strcmp(name, "i386") == 0) {
    *out_level = 3;
    *out_has_sse = 0;
    *out_has_sse2 = 0;
    *out_has_mmx = 0;
    *out_supports_sse = 0;
    *out_supports_sse2 = 0;
    *out_supports_mmx = 0;
    return 1;
  }
  if (strcmp(name, "i486") == 0) {
    *out_level = 4;
    *out_has_sse = 0;
    *out_has_sse2 = 0;
    *out_has_mmx = 0;
    *out_supports_sse = 0;
    *out_supports_sse2 = 0;
    *out_supports_mmx = 0;
    return 1;
  }
  if (strcmp(name, "i586") == 0 || strcmp(name, "pentium") == 0) {
    *out_level = 5;
    *out_has_sse = 0;
    *out_has_sse2 = 0;
    *out_has_mmx = 0;
    *out_supports_sse = 0;
    *out_supports_sse2 = 0;
    *out_supports_mmx = 1;
    return 1;
  }
  if (strcmp(name, "pentium-mmx") == 0) {
    *out_level = 5;
    *out_has_sse = 0;
    *out_has_sse2 = 0;
    *out_has_mmx = 1;
    *out_supports_sse = 0;
    *out_supports_sse2 = 0;
    *out_supports_mmx = 1;
    return 1;
  }
  if (strcmp(name, "i686") == 0 || strcmp(name, "pentiumpro") == 0 ||
      strcmp(name, "pentium2") == 0) {
    *out_level = 6;
    *out_has_sse = 0;
    *out_has_sse2 = 0;
    *out_has_mmx = 1;
    *out_supports_sse = 0;
    *out_supports_sse2 = 0;
    *out_supports_mmx = 1;
    return 1;
  }
  if (strcmp(name, "pentium3") == 0) {
    *out_level = 6;
    *out_has_sse = 1;
    *out_has_sse2 = 0;
    *out_has_mmx = 1;
    *out_supports_sse = 1;
    *out_supports_sse2 = 0;
    *out_supports_mmx = 1;
    return 1;
  }
  if (strcmp(name, "pentium4") == 0 || strcmp(name, "prescott") == 0 ||
      strcmp(name, "nocona") == 0 || strcmp(name, "core2") == 0 ||
      strcmp(name, "x86-64") == 0 || strcmp(name, "x86-64-v1") == 0 ||
      strcmp(name, "x86-64-v2") == 0 || strcmp(name, "x86-64-v3") == 0 ||
      strcmp(name, "x86-64-v4") == 0) {
    *out_level = 7;
    *out_has_sse = 1;
    *out_has_sse2 = 0;
    *out_has_mmx = 1;
    *out_supports_sse = 1;
    *out_supports_sse2 = 1;
    *out_supports_mmx = 1;
    return 1;
  }
  return 0;
}

/* ---- the command line ---------------------------------------------- */

/** How an option takes its value. */
typedef enum {
  OPT_FLAG,   /**< the word alone: -c */
  OPT_VALUE,  /**< WORD VALUE or WORD=VALUE: -march i686, -march=i686 */
  OPT_LETTER, /**< the value joined on or next: -Idir, -I dir */
  OPT_PREFIX  /**< whatever follows, perhaps nothing: -O2, -Wl,a,b */
} opt_kind_t;

/** The flag lists an option is passed on to, as it was written. */
enum {
  FWD_CPP = 1, /**< the preprocessor */
  FWD_C = 2,   /**< the compiler proper (and the -### / bootstrap command) */
  FWD_AS = 4,  /**< the assembler */
  FWD_LD = 8   /**< the linker */
};

typedef struct driver_opt driver_opt_t;

/**
 * What an option does to the parsed command line.
 *
 * @param o    the command line being parsed
 * @param d    the option's table entry
 * @param as   the spelling that matched, for messages
 * @param val  its value; NULL for OPT_FLAG
 * @return     0, or -1 to reject the command line
 */
typedef int (*opt_set_fn)(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val);

/** One entry of the option table. */
struct driver_opt {
  const char *names; /**< its spellings, between spaces */
  opt_kind_t kind;
  unsigned forward;  /**< FWD_* lists to pass it on to */
  opt_set_fn set;    /**< NULL if passing it on is all it does */
  size_t field;      /**< for the one-member setters: which member */
  int n;             /**< and what to set it to; or the setter's own */
};

#define F(member) offsetof(cc_opts_t, member)

/**
 * Set the int member d->field to d->n.
 *
 * @return 0
 */
static int opt_int(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  (void)as;
  (void)val;
  *(int *)((char *)o + d->field) = d->n;
  return 0;
}

/**
 * Set the string member d->field to the value.
 *
 * @return 0
 */
static int opt_str(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  (void)as;
  *(const char **)((char *)o + d->field) = val;
  return 0;
}

/**
 * -fdump-ir=all: dump the IR after every middle-end pass.
 *
 * @return 0, or -1 for any other value
 */
static int opt_dump_ir(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  (void)d;
  if (strcmp(val, "all") != 0) {
    fprintf(stderr, "cc: error: unknown value '%s' in %s (expected all)\n", val, as);
    return -1;
  }
  o->dump_ir = 2;
  return 0;
}

/**
 * Split a comma-separated value (-Wl,a,b) into the flag list d->field.
 *
 * @return 0, or -1 when out of memory
 */
static int opt_split(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  cc_strvec_t *v = (cc_strvec_t *)((char *)o + d->field);

  (void)as;
  while (*val != '\0') {
    size_t n = strcspn(val, ",");
    if (n > 0) {
      char *piece = cc_strndup(val, n);
      if (piece == NULL || cc_strvec_push(v, piece) != 0) {
        free(piece);
        return -1;
      }
      free(piece);
    }
    val += n;
    if (*val == ',') {
      ++val;
    }
  }
  return 0;
}

/**
 * Add an input file.
 *
 * @return 0, or -1 when out of memory
 */
static int opt_input(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  (void)d;
  (void)val;
  return cc_strvec_push(&o->inputs, as);
}

/**
 * -std=: accept only a mode cc knows.
 *
 * @return 0, or -1 for an unknown mode
 */
static int opt_std(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  cc_lang_t lang;

  (void)d;
  (void)as;
  if (cc_lang_from_std(val, &lang) != 0) {
    return -1;
  }
  o->std = val;
  return 0;
}

/**
 * -ansi: for C, -std=c89, as gcc has it.
 *
 * @return 0
 */
static int opt_ansi(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  (void)d;
  (void)as;
  (void)val;
  o->std = "c89";
  return 0;
}

/**
 * -pedantic-errors: -pedantic, with its warnings made errors.
 *
 * @return 0
 */
static int opt_pedantic_errors(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  (void)d;
  (void)as;
  (void)val;
  o->pedantic = 1;
  o->pedantic_errors = 1;
  return 0;
}

/**
 * -f[no-]gnu89-inline: d->n says which.
 *
 * @return 0
 */
static int opt_gnu89_inline(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  (void)as;
  (void)val;
  o->gnu89_inline_mode = d->n;
  o->gnu89_inline_override = 1;
  return 0;
}

/**
 * -print-file-name NAME: answer where NAME is and do nothing else.
 *
 * @return 0
 */
static int opt_print_file_name(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  (void)d;
  (void)as;
  o->query_print_file_name = 1;
  o->print_file_name = val;
  return 0;
}

/**
 * -rdynamic: the linker's --export-dynamic.
 *
 * @return 0, or -1 when out of memory
 */
static int opt_rdynamic(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  (void)d;
  (void)as;
  (void)val;
  return cc_strvec_push(&o->ld_flags, "--export-dynamic");
}

/**
 * -m32 / -m64 (d->n is the target).  -m32 without an -march so far
 * means an i686 with MMX and no SSE.
 *
 * @return 0
 */
static int opt_target(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  (void)as;
  (void)val;
  o->target = (cc_target_t)d->n;
  if (o->target == CC_TARGET_I386 && !o->i386_arch_explicit) {
    o->i386_isa_level = 6;
    o->i386_has_sse = 0;
    o->i386_has_sse2 = 0;
    o->i386_has_mmx = 1;
    o->i386_supports_sse = 0;
    o->i386_supports_sse2 = 0;
    o->i386_supports_mmx = 1;
  }
  return 0;
}

/**
 * -march / -mcpu name the i386 ISA; -mtune is accepted and changes
 * nothing.  An architecture cc does not know is passed over.
 *
 * @return 0
 */
static int opt_arch(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  int level = 0;
  int has_sse = 0;
  int has_sse2 = 0;
  int has_mmx = 0;
  int supports_sse = 0;
  int supports_sse2 = 0;
  int supports_mmx = 0;

  (void)d;
  if (strcmp(as, "-mtune") == 0 ||
      !i386_features_from_arch_name(val, &level, &has_sse, &has_sse2, &has_mmx,
                                    &supports_sse, &supports_sse2,
                                    &supports_mmx)) {
    return 0;
  }
  o->i386_isa_level = level;
  o->i386_has_sse = has_sse;
  o->i386_has_sse2 = has_sse2;
  o->i386_has_mmx = has_mmx;
  o->i386_supports_sse = supports_sse;
  o->i386_supports_sse2 = supports_sse2;
  o->i386_supports_mmx = supports_mmx;
  o->i386_arch_explicit = 1;
  return 0;
}

/**
 * -m[no-]sse, -m[no-]sse2, -m[no-]mmx.  d->field is the has_ member,
 * d->n the supports_ member's offset, and the spelling says on or off.
 * Turning a feature on that the chosen -march lacks is an error on i386.
 *
 * @return 0, or -1 for a feature the architecture lacks
 */
static int opt_isa_feature(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  int on = strncmp(as, "-mno-", 5) != 0;
  int supported = *(int *)((char *)o + d->n);

  (void)val;
  if (on && o->target == CC_TARGET_I386 && !supported) {
    fprintf(stderr, "cc: error: %s is not supported for selected -march level\n", as);
    return -1;
  }
  *(int *)((char *)o + d->field) = on;
  return 0;
}

/**
 * -mfpmath: sse or 387; another value is passed over.
 *
 * @return 0
 */
static int opt_fpmath(cc_opts_t *o, const driver_opt_t *d, const char *as, const char *val) {
  (void)d;
  (void)as;
  if (strcmp(val, "387") == 0) {
    o->i386_fp_math_mode = CC_FPMATH_387;
  } else if (strcmp(val, "sse") == 0) {
    o->i386_fp_math_mode = CC_FPMATH_SSE;
  }
  return 0;
}

/*
 * The options cc knows.  Words (FLAG, VALUE) are tried first; then the
 * prefixes (LETTER, PREFIX) in table order, so a longer prefix must come
 * before a shorter one it begins with.  Any other argument beginning
 * with '-' goes to the compiler proper's flags; the rest are inputs.
 */
static const driver_opt_t options[] = {
    /* what to do */
    {"-E", OPT_FLAG, 0, opt_int, F(mode_E), 1},
    {"-S", OPT_FLAG, 0, opt_int, F(mode_S), 1},
    {"-c", OPT_FLAG, 0, opt_int, F(mode_c), 1},
    {"-o", OPT_LETTER, 0, opt_str, F(output), 0},
    {"-x", OPT_LETTER, 0, opt_str, F(forced_lang), 0},
    {"-v", OPT_FLAG, FWD_CPP, opt_int, F(verbose), 1},
    {"--verbose", OPT_FLAG, 0, opt_int, F(verbose), 1},
    {"-###", OPT_FLAG, 0, opt_int, F(dry_run), 1},
    {"-emit-ssa", OPT_FLAG, 0, opt_int, F(emit_ssa), 1},
    {"-fdump-ir", OPT_FLAG, 0, opt_int, F(dump_ir), 1},
    {"-fdump-ir=", OPT_PREFIX, 0, opt_dump_ir, 0, 0},
    {"--bootstrap-gcc", OPT_FLAG, 0, opt_int, F(bootstrap_gcc), 1},
    {"-print-file-name", OPT_VALUE, 0, opt_print_file_name, 0, 0},
    {"-print-libgcc-file-name", OPT_FLAG, 0, opt_int, F(query_print_libgcc_file_name), 1},

    /* the language and the warnings */
    {"-std=", OPT_PREFIX, FWD_C, opt_std, 0, 0},
    {"-ansi", OPT_FLAG, FWD_C, opt_ansi, 0, 0},
    {"-O", OPT_PREFIX, FWD_C, opt_str, F(opt_level), 0},
    {"-g", OPT_FLAG, FWD_C | FWD_AS, opt_int, F(debug), 1},
    {"-Wall", OPT_FLAG, FWD_C, opt_int, F(wall), 1},
    {"-Werror", OPT_FLAG, FWD_C, opt_int, F(werror), 1},
    {"-pedantic", OPT_FLAG, FWD_C, opt_int, F(pedantic), 1},
    {"-pedantic-errors", OPT_FLAG, FWD_C, opt_pedantic_errors, 0, 0},
    {"-Werror=implicit-function-declaration", OPT_FLAG, FWD_C, opt_int, F(implicit_funcdecl_override), 0},
    {"-Wno-implicit-function-declaration", OPT_FLAG, FWD_C, opt_int, F(implicit_funcdecl_override), 1},
    {"-fgnu89-inline", OPT_FLAG, FWD_C, opt_gnu89_inline, 0, 1},
    {"-fno-gnu89-inline", OPT_FLAG, FWD_C, opt_gnu89_inline, 0, 0},

    /* the machine */
    {"-m32", OPT_FLAG, FWD_C | FWD_CPP, opt_target, 0, CC_TARGET_I386},
    {"-m64", OPT_FLAG, FWD_C | FWD_CPP, opt_target, 0, CC_TARGET_X86_64},
    {"-march -mcpu -mtune", OPT_VALUE, FWD_C | FWD_CPP, opt_arch, 0, 0},
    {"-msse -mno-sse", OPT_FLAG, FWD_C | FWD_CPP, opt_isa_feature, F(i386_has_sse), (int)F(i386_supports_sse)},
    {"-msse2 -mno-sse2", OPT_FLAG, FWD_C | FWD_CPP, opt_isa_feature, F(i386_has_sse2), (int)F(i386_supports_sse2)},
    {"-mmmx -mno-mmx", OPT_FLAG, FWD_C | FWD_CPP, opt_isa_feature, F(i386_has_mmx), (int)F(i386_supports_mmx)},
    {"-mavx -mno-avx -mavx2 -mno-avx2", OPT_FLAG, FWD_C | FWD_CPP, NULL, 0, 0},
    {"-mfpmath", OPT_VALUE, FWD_C, opt_fpmath, 0, 0},
    {"-fPIC", OPT_FLAG, FWD_C | FWD_LD, opt_int, F(pic), 1},

    /* the preprocessor */
    {"-I -D -U -iquote -isystem -idirafter", OPT_LETTER, FWD_CPP | FWD_C, NULL, 0, 0},
    {"-include -imacros -MF -MT -MQ", OPT_LETTER, FWD_CPP, NULL, 0, 0},
    {"-M -MM -dM", OPT_FLAG, FWD_CPP, opt_int, F(mode_E), 1},
    {"-MD -MMD -P -trigraphs", OPT_FLAG, FWD_CPP, NULL, 0, 0},
    {"-nostdinc", OPT_FLAG, FWD_CPP | FWD_C, opt_int, F(nostdinc), 1},
    {"-fpp-max-include-depth -fpp-max-macro-depth -fpp-max-expand-passes "
     "-fpp-max-expanded-bytes -fpp-max-output-bytes -fpp-max-tokens",
     OPT_VALUE, FWD_CPP, NULL, 0, 0},
    {"-Wp", OPT_VALUE, 0, opt_split, F(cpp_flags), 0},
    {"-Wp,", OPT_PREFIX, 0, opt_split, F(cpp_flags), 0},

    /* the assembler and the linker */
    {"-Wa,", OPT_PREFIX, 0, opt_split, F(as_flags), 0},
    {"-Wl,", OPT_PREFIX, 0, opt_split, F(ld_flags), 0},
    {"-L -l", OPT_LETTER, FWD_LD, NULL, 0, 0},
    {"-shared", OPT_FLAG, FWD_LD, opt_int, F(shared), 1},
    {"-pthread", OPT_FLAG, FWD_C | FWD_LD, opt_int, F(pthread_flag), 1},
    {"-rdynamic", OPT_FLAG, 0, opt_rdynamic, 0, 0},
    {"-nostdlib", OPT_FLAG, FWD_C, opt_int, F(nostdlib), 1},
    {"-nodefaultlibs", OPT_FLAG, FWD_C, opt_int, F(nodefaultlibs), 1},

    /* standard input, and the warnings cc has no switch for */
    {"-", OPT_FLAG, 0, opt_input, 0, 0},
    {"-Werror= -Wno-error= -W", OPT_PREFIX, 0, NULL, 0, 0},
};

#undef F

/**
 * Whether argument @p a is option @p d under one of its spellings.
 *
 * @param d       the table entry
 * @param a       the argument
 * @param as      receives the spelling that matched
 * @param as_cap  the size of @p as
 * @param val     receives the value within @p a, or NULL if it has none
 *                there (an OPT_VALUE or OPT_LETTER takes the next one)
 * @return        1 if it is, else 0
 */
static int opt_matches(const driver_opt_t *d, const char *a, char *as,
                       size_t as_cap, const char **val) {
  const char *s = d->names;

  while (*s != '\0') {
    size_t n = strcspn(s, " ");

    if (n < as_cap && strncmp(a, s, n) == 0) {
      const char *rest = a + n;
      int hit = 0;

      *val = NULL;
      switch (d->kind) {
      case OPT_FLAG:
        hit = rest[0] == '\0';
        break;
      case OPT_VALUE:
        hit = rest[0] == '\0' || rest[0] == '=';
        *val = rest[0] == '=' ? rest + 1 : NULL;
        break;
      case OPT_LETTER:
        hit = 1;
        *val = rest[0] != '\0' ? rest : NULL;
        break;
      case OPT_PREFIX:
        hit = 1;
        *val = rest;
        break;
      }
      if (hit) {
        memcpy(as, s, n);
        as[n] = '\0';
        return 1;
      }
    }
    s += n;
    s += strspn(s, " ");
  }
  return 0;
}

/**
 * Find the table entry for an argument: the words first, then the
 * prefixes in table order.
 *
 * @param a       the argument
 * @param as      receives the spelling that matched
 * @param as_cap  the size of @p as
 * @param val     receives the value within @p a, if any
 * @return        the entry, or NULL if no option is spelt so
 */
static const driver_opt_t *opt_find(const char *a, char *as, size_t as_cap,
                                    const char **val) {
  int pass;
  size_t k;

  for (pass = 0; pass < 2; ++pass) {
    for (k = 0; k < sizeof(options) / sizeof(options[0]); ++k) {
      const driver_opt_t *d = &options[k];
      int is_prefix = d->kind == OPT_LETTER || d->kind == OPT_PREFIX;
      if (is_prefix == (pass == 1) && opt_matches(d, a, as, as_cap, val)) {
        return d;
      }
    }
  }
  return NULL;
}

/**
 * Pass an option on, as it was written, to the flag lists d->forward
 * names: one argument, or two when its value was the next one.
 *
 * @param o      the command line being parsed
 * @param d      the option's entry
 * @param a      the argument
 * @param sep    the separate value, or NULL if the value was in @p a
 * @return       0, or -1 when out of memory
 */
static int opt_forward(cc_opts_t *o, const driver_opt_t *d, const char *a,
                       const char *sep) {
  cc_strvec_t *lists[4];
  size_t count = 0;
  size_t k;

  if (d->forward & FWD_CPP) {
    lists[count++] = &o->cpp_flags;
  }
  if (d->forward & FWD_C) {
    lists[count++] = &o->c_flags;
  }
  if (d->forward & FWD_AS) {
    lists[count++] = &o->as_flags;
  }
  if (d->forward & FWD_LD) {
    lists[count++] = &o->ld_flags;
  }
  for (k = 0; k < count; ++k) {
    if (cc_strvec_push(lists[k], a) != 0 ||
        (sep != NULL && cc_strvec_push(lists[k], sep) != 0)) {
      return -1;
    }
  }
  return 0;
}

/**
 * Read the command line into @p o.  On a mistake, o->parse_bad_arg
 * names the argument at fault.
 *
 * @param argc  the argument count
 * @param argv  the arguments
 * @param o     the options to fill; initialised by the caller
 * @return      0, or -1 if the command line is wrong
 */
static int parse_args(int argc, char **argv, cc_opts_t *o) {
  int i;

  o->parse_bad_arg = NULL;
  for (i = 1; i < argc; ++i) {
    const char *a = argv[i];
    const char *val = NULL;
    const char *sep = NULL;
    const driver_opt_t *d;
    char as[48];

    o->parse_bad_arg = a;
    if (a[0] != '-') {
      if (cc_strvec_push(&o->inputs, a) != 0) {
        return -1;
      }
      continue;
    }
    d = opt_find(a, as, sizeof(as), &val);
    if (d == NULL) {
      if (cc_strvec_push(&o->c_flags, a) != 0) {
        return -1;
      }
      continue;
    }
    if (val == NULL && (d->kind == OPT_VALUE || d->kind == OPT_LETTER)) {
      if (i + 1 >= argc) {
        return -1;
      }
      sep = argv[++i];
      val = sep;
    }
    if (opt_forward(o, d, a, sep) != 0) {
      return -1;
    }
    if (d->set != NULL && d->set(o, d, as, val) != 0) {
      return -1;
    }
  }

  if (o->mode_E + o->mode_S + o->mode_c > 1) {
    o->parse_bad_arg = "-E/-S/-c";
    fprintf(stderr, "cc: -E, -S, and -c are mutually exclusive\n");
    return -1;
  }

  if (o->inputs.count == 0 && !o->query_print_file_name &&
      !o->query_print_libgcc_file_name) {
    if (o->parse_bad_arg == NULL) {
      o->parse_bad_arg = "<no-input>";
    }
    return -1;
  }

  return 0;
}

static int run_preprocess(const cc_opts_t *o, const char *in, const char *out,
                          int as_cpp_mode) {
  cc_diag_t diag;
  const char *std_mode = o->std != NULL ? o->std : "gnu17";
  const char *const *pp_flags_base = (const char *const *)o->cpp_flags.items;
  size_t pp_count = o->cpp_flags.count;
  const char **pp_flags = (const char **)pp_flags_base;
  const char *target_flag = target_gcc_flag(o->target);
  const char *as_def1 = "-D__ASSEMBLY__";
  const char *as_def2 = "-D__ASSEMBLER__";
  const char **pp_flags_aug = NULL;
  const char *pp_in = in;
  int use_stdin_tmp = 0;
  char *stdin_tmp = NULL;
  FILE *tmpfp;
  int tfd;
  char buf[4096];
  size_t nread;

  memset(&diag, 0, sizeof(diag));
  if (strcmp(in, "-") == 0) {
    if (asprintf(&stdin_tmp, "/tmp/ccstdinXXXXXX") < 0) {
      fprintf(stderr, "cpp: error: failed to allocate temporary stdin path\n");
      return -1;
    }
    tfd = mkstemp(stdin_tmp);
    if (tfd < 0) {
      fprintf(stderr, "cpp: error: failed to create temporary stdin file\n");
      free(stdin_tmp);
      return -1;
    }
    tmpfp = fdopen(tfd, "wb");
    if (tmpfp == NULL) {
      close(tfd);
      unlink(stdin_tmp);
      free(stdin_tmp);

      fprintf(stderr, "cpp: error: failed to open temporary stdin file\n");
      return -1;
    }
    while ((nread = fread(buf, 1, sizeof(buf), stdin)) > 0) {
      if (fwrite(buf, 1, nread, tmpfp) != nread) {
        fclose(tmpfp);
        unlink(stdin_tmp);
        free(stdin_tmp);

        fprintf(stderr, "cpp: error: failed writing temporary stdin file\n");
        return -1;
      }
    }
    fclose(tmpfp);
    pp_in = stdin_tmp;
    use_stdin_tmp = 1;
  }

  if ((target_flag != NULL && target_flag[0] != '\0') || use_stdin_tmp ||
      (as_cpp_mode && strcmp(out, "/dev/stdout") != 0 && strcmp(out, "-") != 0)) {
    size_t j;
    size_t extra = (target_flag != NULL && target_flag[0] != '\0') ? 1 : 0;
    if (as_cpp_mode && strcmp(out, "/dev/stdout") != 0 && strcmp(out, "-") != 0) {
      extra += 2;
    }
    if (use_stdin_tmp) {
      extra += 1;
    }
    pp_flags_aug = (const char **)calloc(pp_count + extra, sizeof(*pp_flags_aug));
    if (pp_flags_aug == NULL) {
      if (use_stdin_tmp) {
        unlink(stdin_tmp);
        free(stdin_tmp);
      }
      fprintf(stderr, "cpp: error: failed to allocate preprocess flags\n");
      return -1;
    }
    for (j = 0; j < pp_count; ++j) {
      pp_flags_aug[j] = pp_flags_base[j];
    }
    if (target_flag != NULL && target_flag[0] != '\0') {
      pp_flags_aug[pp_count++] = target_flag;
    }
    if (as_cpp_mode && strcmp(out, "/dev/stdout") != 0 && strcmp(out, "-") != 0) {
      pp_flags_aug[pp_count++] = as_def1;
      pp_flags_aug[pp_count++] = as_def2;
    }
    if (use_stdin_tmp) {
      /* The input is a copy of standard input: the preprocessor names it
       * "<stdin>" and leaves it out of the dependencies, as GCC does --
       * a temporary that is gone by the time make reads the .d file
       * stopped Linux's fixdep ("error opening file: /tmp/ccstdin..."). */
      pp_flags_aug[pp_count++] = "--cc-stdin";
    }
    pp_flags = pp_flags_aug;
  }

  if (cc_preprocess_file(pp_in, out, std_mode, pp_flags, pp_count, &diag) !=
      0) {
    free(pp_flags_aug);
    if (use_stdin_tmp) {
      unlink(stdin_tmp);
      free(stdin_tmp);
    }
    if (diag.message[0] != '\0') {
      cc_diag_print("error", diag.path, diag.line, diag.col, diag.message, 1);
    } else {
      fprintf(stderr, "cpp: error: preprocess failed\n");
    }
    return -1;
  }

  if (as_cpp_mode) {
    FILE *fp;
    long sz;
    size_t rsz;
    char *src = NULL;
    char *dst = NULL;
    size_t i;
    size_t w = 0;
    int in_str = 0;
    char q = '\0';

    fp = fopen(out, "rb");
    if (fp == NULL) {
      free(pp_flags_aug);
      if (use_stdin_tmp) {
        unlink(stdin_tmp);
        free(stdin_tmp);
      }
      fprintf(stderr,
              "cpp: error: failed to reopen preprocessed assembly output\n");
      return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
      fclose(fp);
      free(pp_flags_aug);
      if (use_stdin_tmp) {
        unlink(stdin_tmp);
        free(stdin_tmp);
      }
      fprintf(stderr,
              "cpp: error: failed to size preprocessed assembly output\n");
      return -1;
    }
    sz = ftell(fp);
    if (sz < 0 || fseek(fp, 0, SEEK_SET) != 0) {
      fclose(fp);
      free(pp_flags_aug);
      if (use_stdin_tmp) {
        unlink(stdin_tmp);
        free(stdin_tmp);
      }
      fprintf(stderr,
              "cpp: error: failed to read preprocessed assembly output\n");
      return -1;
    }
    src = (char *)malloc((size_t)sz + 1);
    dst = (char *)malloc((size_t)sz + 1);
    if (src == NULL || dst == NULL) {
      free(src);
      free(dst);
      fclose(fp);
      free(pp_flags_aug);
      if (use_stdin_tmp) {
        unlink(stdin_tmp);
        free(stdin_tmp);
      }
      fprintf(stderr,
              "cpp: error: out of memory normalizing assembly output\n");
      return -1;
    }
    rsz = fread(src, 1, (size_t)sz, fp);
    fclose(fp);
    if (rsz != (size_t)sz) {
      free(src);
      free(dst);
      free(pp_flags_aug);
      if (use_stdin_tmp) {
        unlink(stdin_tmp);
        free(stdin_tmp);
      }
      fprintf(stderr, "cpp: error: short read normalizing assembly output\n");
      return -1;
    }
    src[rsz] = '\0';

    for (i = 0; i < rsz; ++i) {
      char c = src[i];
      if (!in_str) {
        dst[w++] = c;
        if (c == '"' || c == '\'') {
          in_str = 1;
          q = c;
        }
        continue;
      }
      if (c == '\\' && i + 1 < rsz) {
        char n = src[i + 1];
        if (n == '\\') {
          dst[w++] = '\\';
          i++;
          continue;
        }
        if (n == 't') {
          dst[w++] = '\t';
          i++;
          continue;
        }
        dst[w++] = c;
        dst[w++] = n;
        i++;
        continue;
      }
      dst[w++] = c;
      if (c == q) {
        in_str = 0;
        q = '\0';
      }
    }
    dst[w] = '\0';

    fp = fopen(out, "wb");
    if (fp == NULL || fwrite(dst, 1, w, fp) != w) {
      if (fp != NULL) {
        fclose(fp);
      }
      free(src);
      free(dst);
      free(pp_flags_aug);
      if (use_stdin_tmp) {
        unlink(stdin_tmp);
        free(stdin_tmp);
      }
      fprintf(stderr,
              "cpp: error: failed writing normalized assembly output\n");
      return -1;
    }
    fclose(fp);
    free(src);
    free(dst);
  }

  free(pp_flags_aug);
  if (use_stdin_tmp) {
    unlink(stdin_tmp);
    free(stdin_tmp);
  }
  return 0;
}

static int run_bootstrap_frontend(const cc_opts_t *o, const char *in_c,
                                  const char *out_s) {
  char stdflag[64];
  size_t argc = 13 + o->c_flags.count;
  char **argv;
  size_t i;
  size_t at = 0;

  argv = (char **)calloc(argc + 1, sizeof(*argv));
  if (argv == NULL) {
    return -1;
  }

  argv[at++] = "gcc";
  argv[at++] = "-S";
  argv[at++] = (char *)target_gcc_flag(o->target);
  /* Force C language mode so bootstrap .i with #line markers is accepted. */
  argv[at++] = "-x";
  argv[at++] = "c";
  snprintf(stdflag, sizeof(stdflag), "-std=%s",
           o->std != NULL ? o->std : "gnu17");
  argv[at++] = stdflag;
  if (!o->pic && !o->shared) {
    argv[at++] = "-fno-pie";
    argv[at++] = "-no-pie";
  }
  for (i = 0; i < o->c_flags.count; ++i) {
    argv[at++] = o->c_flags.items[i];
  }
  argv[at++] = "-o";
  argv[at++] = (char *)out_s;
  argv[at++] = (char *)in_c;
  argv[at] = NULL;

  i = run_cmd(o, argv);
  free(argv);
  return (int)i;
}

static const char *resolve_tool_default(const cc_opts_t *o,
                                        const char *env_name,
                                        const char *tool_name,
                                        const char *sibling_dir, char *buf,
                                        size_t bufsz) {
  const char *override;
  const char *self;
  const char *slash;
  size_t dirlen;

  override = getenv(env_name);
  if (override != NULL && override[0] != '\0') {
    return override;
  }
  if (o == NULL || o->self_path == NULL || o->self_path[0] == '\0' ||
      buf == NULL || bufsz == 0) {
    return tool_name;
  }

  self = o->self_path;
  slash = strrchr(self, '/');
  if (slash == NULL) {
    return tool_name;
  }
  dirlen = (size_t)(slash - self);

  if (snprintf(buf, bufsz, "%.*s/../%s/%s", (int)dirlen, self, sibling_dir,
               tool_name) > 0 &&
      access(buf, X_OK) == 0) {
    return buf;
  }
  if (snprintf(buf, bufsz, "%.*s/%s", (int)dirlen, self, tool_name) > 0 &&
      access(buf, X_OK) == 0) {
    return buf;
  }
  return tool_name;
}

static int run_as(const cc_opts_t *o, const char *in_s, const char *out_o) {
  char as_path[PATH_MAX];
  const char *as_prog;
  int pass_from_cc = 0;
  size_t argc;
  char **argv;
  size_t i;
  size_t at = 0;

  as_prog = resolve_tool_default(o, "AS", "as", "as", as_path, sizeof(as_path));
  pass_from_cc = as_supports_from_cc_flag(as_prog);
  argc = (pass_from_cc ? 6 : 5) + o->as_flags.count;
  argv = (char **)calloc(argc + 1, sizeof(*argv));
  if (argv == NULL) {
    return -1;
  }
  argv[at++] = (char *)as_prog;
  if (pass_from_cc) {
    argv[at++] = "--from-cc";
  }
  argv[at++] = (char *)cc_target_get(o->target)->as_flag;
  for (i = 0; i < o->as_flags.count; ++i) {
    argv[at++] = o->as_flags.items[i];
  }
  argv[at++] = "-o";
  argv[at++] = (char *)out_o;
  argv[at++] = (char *)in_s;
  argv[at] = NULL;

  i = run_cmd(o, argv);
  free(argv);
#ifdef CC_SUBSTRATE_BUILD
  if (i != 0) {
    struct stat st;
    if (out_o != NULL && stat(out_o, &st) == 0 && st.st_size > 0) {
      return 0;
    }
  }
#endif
  return (int)i;
}

static int run_ld(const cc_opts_t *o, const cc_strvec_t *objs, const char *out) {
  char ld_path[PATH_MAX];
  const char *ld_prog;
#ifdef CC_SUBSTRATE_BUILD
  char crt0[PATH_MAX];
#else
  char crt1[PATH_MAX];
  char crti[PATH_MAX];
  char crtbegin[PATH_MAX];
  char crtend[PATH_MAX];
  char crtn[PATH_MAX];
  char libc_nonshared[PATH_MAX];
  char libmvec_path[PATH_MAX];
#endif
  char libc_path[PATH_MAX];
  char libm_path[PATH_MAX];
  char libgcc[PATH_MAX];
  const int want_default_runtime =
      !o->shared && !o->nostdlib && !o->nodefaultlibs;
  size_t argc = 25 + o->ld_flags.count + objs->count;
  char **argv;
  size_t i;
  size_t at = 0;

  argv = (char **)calloc(argc + 1, sizeof(*argv));
  if (argv == NULL) {
    return -1;
  }

  ld_prog = resolve_tool_default(o, "LD", "ld", "ld", ld_path, sizeof(ld_path));
  argv[at++] = (char *)ld_prog;
  argv[at++] = "-m";
  argv[at++] = (char *)cc_target_get(o->target)->ld_emulation;
  if (o->shared) {
    argv[at++] = "-shared";
  }
  if (want_default_runtime) {
#ifdef CC_SUBSTRATE_BUILD
    /* Substrate's runtime is now dynamically linked: every userland
     * binary is PIE + /sbin/ld.so + libc.so.0 (see commits switching
     * bin/, sbin/, usr.bin/, lib/, usr.lib/ to .so.0).  Stop forcing
     * -static here and route the link through ld.so so cc-built
     * programs match every other binary on the system. */
    if (gcc_print_runtime_file("crt0.o", o->target, crt0) != 0 ||
        gcc_print_runtime_file("libc.so.0", o->target, libc_path) != 0) {
      fprintf(stderr, "cc: failed to discover Substrate runtime paths\n");
      free(argv);
      return -1;
    }
    /* libm.so.0 is pulled in transitively via libc.so.0's DT_NEEDED;
     * no need to name it on the link line.  Likewise libgcc — only
     * append if it actually exists on disk. */
    libm_path[0] = '\0';
    libgcc[0] = '\0';
    (void)gcc_print_libgcc(o->target, libgcc);
    /* The i386 backend's code is position-dependent (no GOT-relative
     * addressing), so an i386 program is a non-PIE executable; as a PIE
     * its absolute addresses would be text relocations ld.so cannot
     * apply.  x86-64 code is RIP-relative and links as a PIE. */
    argv[at++] = cc_target_get(o->target)->pie ? "-pie" : "-no-pie";
    argv[at++] = "-dynamic-linker";
    argv[at++] = o->target == CC_TARGET_I386 ? CC_SUBSTRATE_LDSO32
                                             : CC_SUBSTRATE_LDSO64;
    argv[at++] = crt0;
#else
    if (gcc_print_runtime_file("crt1.o", o->target, crt1) != 0 ||
        gcc_print_runtime_file("crti.o", o->target, crti) != 0 ||
        gcc_print_runtime_file("crtbegin.o", o->target, crtbegin) != 0 ||
        gcc_print_runtime_file("crtend.o", o->target, crtend) != 0 ||
        gcc_print_runtime_file("crtn.o", o->target, crtn) != 0 ||
        gcc_print_runtime_file("libc.so.6", o->target, libc_path) != 0 ||
        gcc_print_runtime_file("libc_nonshared.a", o->target, libc_nonshared) != 0 ||
        gcc_print_runtime_file("libm.so.6", o->target, libm_path) != 0 ||
        gcc_print_libgcc(o->target, libgcc) != 0) {
      fprintf(stderr,
              "cc: failed to discover runtime crt/libgcc paths via gcc\n");
      free(argv);
      return -1;
    }
    if (gcc_print_runtime_file("libmvec.so.1", o->target, libmvec_path) != 0) {
      libmvec_path[0] = '\0';
    }
    argv[at++] = "-dynamic-linker";
    argv[at++] = (char *)cc_target_get(o->target)->host_ldso;
    argv[at++] = crt1;
    argv[at++] = crti;
    argv[at++] = crtbegin;
#endif
  }
  argv[at++] = "-o";
  argv[at++] = (char *)out;
  for (i = 0; i < objs->count; ++i) {
    argv[at++] = objs->items[i];
  }
  for (i = 0; i < o->ld_flags.count; ++i) {
    argv[at++] = o->ld_flags.items[i];
  }
  if (want_default_runtime) {
    argv[at++] = libc_path;
#ifndef CC_SUBSTRATE_BUILD
    if (access(libc_nonshared, R_OK) == 0) {
      argv[at++] = libc_nonshared;
    }
#endif
    if (libm_path[0] != '\0') {
      argv[at++] = libm_path;
    }
    /* Guard against gcc_print_libgcc returning an empty path or a
     * stack-uninitialized buffer: previously the code relied on
     * access(R_OK) alone, but access("/", R_OK) succeeds and we'd
     * end up with a literal `/` on the link line. */
    if (libgcc[0] != '\0' && access(libgcc, R_OK) == 0) {
      argv[at++] = libgcc;
    }
#ifndef CC_SUBSTRATE_BUILD
    {
      /*
       * glibc's libc.so script names the dynamic linker AS_NEEDED;
       * libc.so.6 named directly does not, and libgcc's decimal
       * conversions use TLS, which needs its __tls_get_addr.
       */
      const char *ldso = cc_target_get(o->target)->host_ldso;

      if (access(ldso, R_OK) == 0) {
        argv[at++] = "--as-needed";
        argv[at++] = (char *)ldso;
        argv[at++] = "--no-as-needed";
      }
    }
    argv[at++] = crtend;
    argv[at++] = crtn;
#endif
  }
  argv[at] = NULL;

  i = run_cmd(o, argv);
  free(argv);
#ifdef CC_SUBSTRATE_BUILD
  if (i != 0) {
    struct stat st;
    if (out != NULL && stat(out, &st) == 0 && st.st_size > 0) {
      return 0;
    }
  }
#endif
  return (int)i;
}

static int run_emit_ssa_tool(const cc_opts_t *o, const char *self,
                             const char *ir_file) {
  char *argv[3];
  char tool_path[PATH_MAX];
  const char *slash;

  slash = strrchr(self, '/');
  if (slash != NULL) {
    size_t dlen = (size_t)(slash - self);
    if (dlen + strlen("/ir-verifier") + 1 < sizeof(tool_path)) {
      snprintf(tool_path, sizeof(tool_path), "%.*s/ir-verifier", (int)dlen,
               self);
      argv[0] = tool_path;
    } else {
      argv[0] = "ir-verifier";
    }
  } else {
    argv[0] = "ir-verifier";
  }
  argv[1] = (char *)ir_file;
  argv[2] = NULL;

  return run_cmd(o, argv);
}

static int derive_out(const char *in, const char *ext, char out[PATH_MAX]) {
  const char *base_in = strrchr(in, '/');
  const char *dot;
  size_t base;

  if (base_in != NULL) {
    base_in++;
  } else {
    base_in = in;
  }
  dot = strrchr(base_in, '.');
  if (dot == NULL) {
    base = strlen(base_in);
  } else {
    base = (size_t)(dot - base_in);
  }

  if (base + strlen(ext) + 1 > PATH_MAX) {
    return -1;
  }

  snprintf(out, PATH_MAX, "%.*s%s", (int)base, base_in, ext);
  return 0;
}

/**
 * The name of -fdump-ir's files, without their .ir, as gcc names its
 * dumps: the output's name without its suffix when -o names the .s or
 * .o, otherwise the input's base name in the current directory.
 *
 * @param o    the command line
 * @param in   the input being compiled
 * @param out  receives the name
 * @return     0, or -1 when it is too long
 */
static int dump_ir_base(const cc_opts_t *o, const char *in, char out[PATH_MAX]) {
  if (o->output != NULL && (o->mode_S || o->mode_c) && strcmp(o->output, "/dev/stdout") != 0) {
    const char *slash = strrchr(o->output, '/');
    const char *dot = strrchr(slash != NULL ? slash : o->output, '.');
    size_t n = dot != NULL ? (size_t)(dot - o->output) : strlen(o->output);

    if (n + 1 > PATH_MAX) {
      return -1;
    }
    snprintf(out, PATH_MAX, "%.*s", (int)n, o->output);
    return 0;
  }
  return derive_out(in, "", out);
}

static int maybe_print_info_and_exit(int argc, char **argv) {
  int i;
#ifdef CC_SUBSTRATE_BUILD
  cc_target_t target = CC_DEFAULT_TARGET;
#else
  cc_target_t target = CC_TARGET_X86_64;
#endif
  const char *info = NULL;
  int has_other = 0;

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-m32") == 0) {
      target = CC_TARGET_I386;
    } else if (strcmp(argv[i], "-m64") == 0) {
      target = CC_TARGET_X86_64;
    } else if (strcmp(argv[i], "--version") == 0 ||
               strcmp(argv[i], "-v") == 0) {
      info = argv[i];
    } else if (strcmp(argv[i], "-dumpversion") == 0 ||
               strcmp(argv[i], "-dumpfullversion") == 0) {
      info = argv[i];
    } else if (strcmp(argv[i], "-dumpmachine") == 0) {
      info = argv[i];
    } else {
      has_other = 1;
    }
  }
  if (info == NULL || has_other) {
    return 0;
  }
  if (strcmp(info, "--version") == 0 || strcmp(info, "-v") == 0) {
    printf("cc (Substrate GCC-compatible) 13.2.0\n");
    return 1;
  }
  if (strcmp(info, "-dumpversion") == 0 ||
      strcmp(info, "-dumpfullversion") == 0) {
    printf("13.2.0\n");
    return 1;
  }
  if (strcmp(info, "-dumpmachine") == 0) {
    printf("%s\n", cc_target_get(target)->gnu_triple);
    return 1;
  }
  return 0;
}

int cc_main(int argc, char **argv) {
  cc_opts_t o;
  cc_strvec_t obj_files;
  size_t i;
  int rc = 1;

  if (toolchain_guard_enter("cc") != 0) {
    return 1;
  }
  setenv("SUBSTRATE_CC_ACTIVE", "1", 1);

  memset(&o, 0, sizeof(o));
  o.self_path = (argc > 0) ? argv[0] : NULL;
  memset(&obj_files, 0, sizeof(obj_files));
  o.i386_isa_level = 6;
  o.i386_has_sse = 0;
  o.i386_has_sse2 = 1;
  o.i386_has_mmx = 1;
  o.i386_supports_sse = 1;
  o.i386_supports_sse2 = 1;
  o.i386_supports_mmx = 1;
  o.i386_fp_math_mode = CC_FPMATH_AUTO;
  o.implicit_funcdecl_override = -1;
#ifdef CC_SUBSTRATE_BUILD
  o.target = CC_DEFAULT_TARGET;
#else
  o.target = CC_TARGET_X86_64;
#endif
  if (strcmp(prog_basename(argv[0]), "cpp") == 0) {
    o.invoked_as_cpp = 1;
    o.mode_E = 1;
  }
  if (maybe_print_info_and_exit(argc, argv)) {
    return 0;
  }

  if (parse_args(argc, argv, &o) != 0) {
    if (o.parse_bad_arg != NULL && o.parse_bad_arg[0] != '\0') {
      fprintf(stderr, "cc: invalid or unsupported parameter: %s\n",
              o.parse_bad_arg);
    }
    usage(argv[0]);
    goto out;
  }

  if (o.query_print_file_name) {
    char path[PATH_MAX];
    const char *name = o.print_file_name != NULL ? o.print_file_name : "";
    if (name[0] == '\0') {
      printf("\n");
    } else if (gcc_print_file_name(name, o.target, path) == 0) {
      printf("%s\n", path);
    } else {
      printf("%s\n", name);
    }
    rc = 0;
    goto out;
  }
  if (o.query_print_libgcc_file_name) {
    char path[PATH_MAX];
    if (gcc_print_libgcc(o.target, path) == 0) {
      printf("%s\n", path);
    } else {
      printf("libgcc.a\n");
    }
    rc = 0;
    goto out;
  }

  if (add_builtin_resource_includes(&o) != 0) {
    fprintf(stderr, "cc: failed to add builtin include paths\n");
    goto out;
  }
  if (add_gcc_system_includes(&o) != 0) {
    fprintf(stderr, "cc: failed to add system include paths\n");
    goto out;
  }

  if (o.mode_E && o.output != NULL && o.inputs.count > 1) {
    fprintf(stderr, "cc: -E with -o requires exactly one input\n");
    goto out;
  }
  if ((o.mode_E || o.mode_S) && o.output != NULL && strcmp(o.output, "-") == 0) {
    /* as gcc: `-o -` with -E or -S is standard output (with -c or a link it is a file named -) */
    o.output = "/dev/stdout";
  }

  if (o.emit_ssa) {
    if (o.inputs.count != 1 || !has_ext(o.inputs.items[0], ".ir")) {
      fprintf(stderr,
              "cc: -emit-ssa currently expects exactly one .ir input\n");
      goto out;
    }
    if (run_emit_ssa_tool(&o, argv[0], o.inputs.items[0]) != 0) {
      goto out;
    }
    rc = 0;
    goto out;
  }

  for (i = 0; i < o.inputs.count; ++i) {
    const char *in = o.inputs.items[i];
    int input_is_c = has_ext(in, ".c");
    int input_is_S = has_ext(in, ".S");
    int input_is_s = has_ext(in, ".s");

    if (o.forced_lang != NULL && strcmp(o.forced_lang, "none") != 0) {
      if (strcmp(o.forced_lang, "c") == 0) {
        input_is_c = 1;
        input_is_S = 0;
        input_is_s = 0;
      } else if (strcmp(o.forced_lang, "assembler-with-cpp") == 0) {
        input_is_c = 0;
        input_is_S = 1;
        input_is_s = 0;
      } else if (strcmp(o.forced_lang, "assembler") == 0) {
        input_is_c = 0;
        input_is_S = 0;
        input_is_s = 1;
      }
    }

    if (o.mode_E) {
      const char *pp_out;

      if (has_ext(in, ".o") || has_ext(in, ".a") || has_ext(in, ".so")) {
        fprintf(stderr, "cc: -E is not valid for object/archive input %s\n",
                in);
        goto out;
      }

      pp_out = o.output != NULL ? o.output : "/dev/stdout";
      if (run_preprocess(&o, in, pp_out, input_is_S) != 0) {
        goto out;
      }
      continue;
    }

    if (input_is_S) {
      char out_s[PATH_MAX];
      char out_o[PATH_MAX];
      char tmp_s[PATH_MAX];

      if (o.mode_S) {
        if (o.output != NULL) {
          snprintf(out_s, sizeof(out_s), "%s", o.output);
        } else if (derive_out(in, ".s", out_s) != 0) {
          fprintf(stderr, "cc: failed to derive .s output name\n");
          goto out;
        }
        if (run_preprocess(&o, in, out_s, 1) != 0) {
          goto out;
        }
        continue;
      }

      if (make_temp_path(&o, "ccspp_", ".s", tmp_s) != 0) {
        fprintf(stderr,
                "cc: failed to create temporary preprocessed assembly file\n");
        goto out;
      }
      if (run_preprocess(&o, in, tmp_s, 1) != 0) {
        goto out;
      }

      if (o.mode_c) {
        if (o.output != NULL) {
          snprintf(out_o, sizeof(out_o), "%s", o.output);
        } else if (derive_out(in, ".o", out_o) != 0) {
          fprintf(stderr, "cc: failed to derive .o output name\n");
          goto out;
        }
      } else {
        if (make_temp_path(&o, "cco_", ".o", out_o) != 0) {
          fprintf(stderr, "cc: failed to create temporary object file\n");
          goto out;
        }
      }

      if (run_as(&o, tmp_s, out_o) != 0) {
        goto out;
      }
      if (cc_strvec_push(&obj_files, out_o) != 0) {
        goto out;
      }
      continue;
    }

    if (input_is_c) {
      char out_pp[PATH_MAX];
      char out_s[PATH_MAX];
      char out_o[PATH_MAX];

      if (make_temp_path(&o, "ccpp_", ".i", out_pp) != 0) {
        fprintf(stderr, "cc: failed to create temporary preprocessed file\n");
        goto out;
      }
      if (run_preprocess(&o, in, out_pp, 0) != 0) {
        goto out;
      }

      if (o.mode_S) {
        if (o.output != NULL) {
          snprintf(out_s, sizeof(out_s), "%s", o.output);
        } else if (derive_out(in, ".s", out_s) != 0) {
          fprintf(stderr, "cc: failed to derive .s output name\n");
          goto out;
        }
      } else {
        if (make_temp_path(&o, "ccs_", ".s", out_s) != 0) {
          fprintf(stderr, "cc: failed to create temporary assembly file\n");
          goto out;
        }
      }

      if (o.bootstrap_gcc) {
        if (run_bootstrap_frontend(&o, out_pp, out_s) != 0) {
          goto out;
        }
      } else {
        cc_diag_t diag;
        memset(&diag, 0, sizeof(diag));
        cc_options_t copts;
        char dump_base[PATH_MAX];
        compile_options(&o, &copts);
        if (o.dump_ir) {
          if (dump_ir_base(&o, in, dump_base) != 0) {
            fprintf(stderr, "cc: failed to derive the IR dump's name\n");
            goto out;
          }
          copts.dump_ir = dump_base;
          copts.dump_ir_all = o.dump_ir == 2;
        }
        if (cc_compile_c_to_s(out_pp, in, out_s, &copts, &diag) != 0) {
          if (diag.error_count > 0) {
            if (diag.message[0] != '\0') {
              fprintf(stderr, "cc: error: %s\n", diag.message);
            } else {
              fprintf(stderr, "cc: error: %zu error(s) generated\n",
                      diag.error_count);
            }
          } else if (diag.message[0] != '\0') {
            cc_diag_print("error", diag.path, diag.line, diag.col,
                          diag.message, 1);
          } else {
            fprintf(stderr, "cc: error: native C pipeline failed\n");
          }
          goto out;
        }
      }

      if (o.mode_S) {
        continue;
      }

      if (o.mode_c) {
        if (o.output != NULL) {
          snprintf(out_o, sizeof(out_o), "%s", o.output);
        } else if (derive_out(in, ".o", out_o) != 0) {
          fprintf(stderr, "cc: failed to derive .o output name\n");
          goto out;
        }
      } else {
        if (make_temp_path(&o, "cco_", ".o", out_o) != 0) {
          fprintf(stderr, "cc: failed to create temporary object file\n");
          goto out;
        }
      }

      if (run_as(&o, out_s, out_o) != 0) {
        goto out;
      }
      if (cc_strvec_push(&obj_files, out_o) != 0) {
        goto out;
      }
      continue;
    }

    if (input_is_s) {
      char out_o[PATH_MAX];

      if (o.mode_S) {
        if (o.output != NULL && strcmp(o.output, in) != 0) {
          char *cp_argv[4];
          cp_argv[0] = "cp";
          cp_argv[1] = (char *)in;
          cp_argv[2] = (char *)o.output;
          cp_argv[3] = NULL;
          if (run_cmd(&o, cp_argv) != 0) {
            goto out;
          }
        }
        continue;
      }

      if (o.mode_c) {
        if (o.output != NULL) {
          snprintf(out_o, sizeof(out_o), "%s", o.output);
        } else if (derive_out(in, ".o", out_o) != 0) {
          fprintf(stderr, "cc: failed to derive .o output name\n");
          goto out;
        }
      } else {
        if (make_temp_path(&o, "cco_", ".o", out_o) != 0) {
          fprintf(stderr, "cc: failed to create temporary object file\n");
          goto out;
        }
      }

      if (run_as(&o, in, out_o) != 0) {
        goto out;
      }
      if (cc_strvec_push(&obj_files, out_o) != 0) {
        goto out;
      }
      continue;
    }

    if (has_ext(in, ".o") || has_ext(in, ".a") || has_ext(in, ".so")) {
      if (cc_strvec_push(&obj_files, in) != 0) {
        goto out;
      }
      continue;
    }

    fprintf(stderr, "cc: unsupported input type: %s\n", in);
    goto out;
  }

  if (o.mode_E || o.mode_S || o.mode_c) {
    rc = 0;
    goto out;
  }

  if (obj_files.count == 0) {
    fprintf(stderr, "cc: no objects to link\n");
    goto out;
  }

  {
    const char *out = o.output != NULL ? o.output : "a.out";
    if (run_ld(&o, &obj_files, out) != 0) {
      goto out;
    }
  }

  rc = 0;
out:
  cleanup_temp_files(&o);
  opts_free(&o);
  cc_strvec_free(&obj_files);
  return rc;
}

int main(int argc, char **argv) { return cc_main(argc, argv); }
