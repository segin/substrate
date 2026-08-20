/*
 * torture_plugin.cpp — the dlopen'd half of torture_cxxdso.
 *
 * Deliberately a C++ translation unit that links the SHARED libstdc++: that
 * gives it a PT_TLS segment (libstdc++ carries thread-local state), which is
 * the property that used to make it undlopenable.  See torture_cxxdso.cpp.
 */

#include <stdexcept>
#include <string>
#include <typeinfo>

/* A type defined here, thrown here, and caught by base class in the host. */
struct PluginError : std::runtime_error {
    explicit PluginError(const std::string &s) : std::runtime_error(s) {}
};

/* Thread-local state in the plugin itself, so the loader has to give this
 * module a real TLS slot rather than offset 0. */
static thread_local int plugin_tls = 0;

/* Deliberately NON-ZERO, and therefore in .tdata rather than .tbss.  A thread
 * that reads this correctly proves the loader copied the module's
 * initialization image into that thread's block -- fresh anonymous memory
 * would read as 0 and pass a zero-initialized check by accident. */
static thread_local int plugin_tls_init = 0x1234;

extern "C" void plugin_throw(const char *what) {
    throw PluginError(std::string("plugin: ") + what);
}

/* Returned so the host can compare typeinfo identity across the boundary.
 * std::string's typeinfo must be ONE object shared with the host, which is
 * only true when both sides use the shared libstdc++. */
extern "C" const std::type_info *plugin_ti_string(void) {
    return &typeid(std::string);
}

extern "C" int plugin_tls_roundtrip(int v) {
    plugin_tls = v;
    return plugin_tls;
}

extern "C" int plugin_tls_get(void)      { return plugin_tls; }
extern "C" int plugin_tls_initval(void)  { return plugin_tls_init; }
