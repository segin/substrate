/*
 * A string literal initializing an element of a global structure's array
 * member is that element -- a pointer -- unless the array is one of
 * characters, which the string fills.  cc took every string there as
 * filling a character array, and failed ("AST to SSA lowering failed",
 * no message) on Linux's struct cpu_dev, whose `const char *c_ident[2]`
 * is given `{ "CentaurHauls" }` -- 10 objects of a tinyconfig build.
 */
#include <stdio.h>
#include <wchar.h>

struct cpu_dev {
    const char *c_vendor;
    const char *c_ident[2];
    int c_x86_vendor;
    struct model_info {
        int family;
        const char *model_names[4];
    } legacy_models[2];
};

static const struct cpu_dev dev = {
    .c_vendor = "Centaur",
    .c_ident = {"CentaurHauls"},
    .c_x86_vendor = 5,
    .legacy_models = {{4, {"a", "b"}}, {.family = 5, .model_names = {[2] = "c"}}},
};

struct mixed {
    char name[8];
    const char *alias[3];
    wchar_t wide[4];
};

static struct mixed many[2] = {
    {"eight", {"x", "y", "z"}, L"ab"},
    {.alias = {[1] = "only"}, .name = "n"},
};

struct outer {
    int tag;
    struct mixed inner;
};

static struct outer nest = {7, {"in", {"p"}, L"w"}};

static const char *s(const char *p) { return p != NULL ? p : "(null)"; }

int main(void) {
    printf("%s %s %s %d\n", dev.c_vendor, s(dev.c_ident[0]), s(dev.c_ident[1]), dev.c_x86_vendor);
    printf("%d %s %s %s\n", dev.legacy_models[0].family, s(dev.legacy_models[0].model_names[0]),
           s(dev.legacy_models[0].model_names[1]), s(dev.legacy_models[0].model_names[2]));
    printf("%d %s %s\n", dev.legacy_models[1].family, s(dev.legacy_models[1].model_names[0]),
           s(dev.legacy_models[1].model_names[2]));
    printf("%s %s %s %s %d %d %d\n", many[0].name, s(many[0].alias[0]), s(many[0].alias[1]), s(many[0].alias[2]),
           (int)many[0].wide[0], (int)many[0].wide[1], (int)many[0].wide[2]);
    printf("%s %s %s %s\n", many[1].name, s(many[1].alias[0]), s(many[1].alias[1]), s(many[1].alias[2]));
    printf("%d %s %s %s %d\n", nest.tag, nest.inner.name, s(nest.inner.alias[0]), s(nest.inner.alias[1]),
           (int)nest.inner.wide[0]);
    return 0;
}
