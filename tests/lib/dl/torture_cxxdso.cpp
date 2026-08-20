/*
 * torture_cxxdso.cpp — the shared C++ runtime, end to end.
 *
 * Substrate linked every C++ binary against libstdc++.a for a long time, not
 * by choice but because the `libstdc++.so` link name was missing from the
 * cross sysroot: ld tries libstdc++.so first and libstdc++.a second, so it
 * silently fell through to the archive and the link succeeded either way.
 *
 * A stack built that way gives every shared object a private copy of the C++
 * runtime -- its own operator new/delete, its own iostream and locale
 * globals, its own typeinfo objects.  The TDE desktop shipped as 125 shared
 * libraries with 125 private libstdc++ copies for exactly this reason.
 *
 * Once that was fixed, dlopen was still impossible for any C++ module: a
 * shared libstdc++ brings a PT_TLS segment along, and ld.so laid TLS out in a
 * single startup pass, so anything arriving later kept tls_modid 0 and every
 * TLS relocation against it was refused.  That is precisely what a plugin
 * host (TDE's tdeinit/KLibLoader, and this test) does for a living.  ld.so
 * now reserves surplus static TLS for post-startup modules.
 *
 * This exercises both, plus the properties that only hold when the runtime is
 * genuinely shared rather than duplicated per module.
 *
 * Build (substrate target):
 *     make CROSS=/opt/substrate/bin/i386-unknown-substrate-
 *
 * Run on target with the plugin installed in a default search directory
 * (ld.so implements neither DT_RPATH nor DT_RUNPATH):
 *     cp libtorture_plugin.so /lib/ && ./torture_cxxdso
 */

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <pthread.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>

#define PLUGIN_PATH "/lib/libtorture_plugin.so"

static int failures;

static void ok(const char *what)            { std::printf("ok       %s\n", what); }
static void fail(const char *what)          { std::printf("FAIL     %s\n", what); failures++; }
static void failf(const char *what, const char *d) {
    std::printf("FAIL     %s: %s\n", what, d ? d : "(no detail)");
    failures++;
}

/* ------------------------------------------------------------------ *
 * Thread-local state, exercised from several threads at once.
 * ------------------------------------------------------------------ */
static thread_local int  tls_value;
static thread_local char tls_pad[512];   /* non-trivial TLS image */

static void *tls_worker(void *arg) {
    long id = (long)arg;
    /* A fresh thread must see zero-initialized TLS, not the creator's. */
    if (tls_value != 0) fail("new thread starts with zeroed TLS");
    tls_value  = (int)(id + 100);
    tls_pad[0] = (char)id;
    /* Spin so the threads genuinely overlap before re-reading. */
    for (int i = 0; i < 200000; i++) __asm__ volatile ("" ::: "memory");
    if (tls_value != (int)(id + 100)) fail("thread-local int survived contention");
    if (tls_pad[0] != (char)id)       fail("thread-local array survived contention");
    return 0;
}

int main(void) {
    /* -------- shared runtime, in this module -------- */
    {
        std::ostringstream os;
        os << "iostream " << 123;
        if (os.str() == "iostream 123") ok("ostringstream + locale");
        else                            fail("ostringstream + locale");
    }

    /* -------- dlopen a C++ module (needs post-startup TLS) -------- */
    void *h = dlopen(PLUGIN_PATH, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        /* dlerror() is single-shot: read it once or the message is lost. */
        failf("dlopen " PLUGIN_PATH, dlerror());
        std::printf("torture_cxxdso: FAILED (%d)\n", failures);
        return 1;
    }
    ok("dlopen of a C++ module");

    typedef void (*throw_fn)(const char *);
    typedef const std::type_info *(*ti_fn)(void);
    typedef int (*tls_fn)(int);
    throw_fn plugin_throw     = (throw_fn)dlsym(h, "plugin_throw");
    ti_fn    plugin_ti_string = (ti_fn)dlsym(h, "plugin_ti_string");
    tls_fn   plugin_tls       = (tls_fn)dlsym(h, "plugin_tls_roundtrip");
    if (!plugin_throw || !plugin_ti_string || !plugin_tls) {
        failf("dlsym of plugin entry points", dlerror());
        std::printf("torture_cxxdso: FAILED (%d)\n", failures);
        return 1;
    }
    ok("dlsym of plugin entry points");

    /* -------- an exception crossing the dlopen boundary --------
     * Needs PT_GNU_EH_FRAME in both modules; without it the unwinder finds no
     * FDEs through dl_iterate_phdr and this reaches std::terminate instead. */
    try {
        plugin_throw("boom");
        fail("plugin threw");
    } catch (const std::runtime_error &e) {
        if (std::strcmp(e.what(), "plugin: boom") == 0)
            ok("catch across dlopen, by base class, what() intact");
        else
            failf("catch across dlopen: wrong what()", e.what());
    } catch (...) {
        /* Reaching here means the catch matched nothing specific, i.e. the
         * two modules disagree about what std::runtime_error IS. */
        fail("catch across dlopen matched the wrong type");
    }

    /* -------- one runtime, not two --------
     * With a private libstdc++ per module these are distinct objects and the
     * comparison fails even though both name std::string. */
    if (*plugin_ti_string() == typeid(std::string))
        ok("typeinfo identity across modules (std::string)");
    else
        fail("typeinfo identity across modules (std::string)");

    /* -------- the plugin's own TLS is addressable -------- */
    if (plugin_tls(0x5a5a) == 0x5a5a) ok("thread_local inside a dlopen'd module");
    else                              fail("thread_local inside a dlopen'd module");

    /* -------- per-thread TLS stays isolated -------- */
    tls_value = 7;
    pthread_t t[4];
    int made = 0;
    for (long i = 0; i < 4; i++)
        if (pthread_create(&t[i], 0, tls_worker, (void *)i) == 0) made++;
    if (made != 4) fail("pthread_create x4");
    for (int i = 0; i < made; i++) pthread_join(t[i], 0);
    if (tls_value == 7) ok("per-thread TLS isolated from 4 threads");
    else                fail("per-thread TLS isolated from 4 threads");

    dlclose(h);

    std::printf("torture_cxxdso: %s (%d failure%s)\n",
                failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures != 0;
}
