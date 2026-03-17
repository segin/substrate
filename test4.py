with open("usr.bin/cc/frontend/parser.c", "r") as f:
    text = f.read()

# push_global seems to still use linear reallocation!
# Ah, I see:
# cc_global_t *next = (cc_global_t *)realloc(tu->globals, (tu->global_count + 1) * sizeof(*next));

import re
push_global_pattern = re.compile(r"    cc_global_t \*next = \(cc_global_t \*\)realloc\(tu->globals, \(tu->global_count \+ 1\) \* sizeof\(\*next\)\);\n    if \(next == NULL\) \{\n        return -1;\n    \}\n    tu->globals = next;\n    tu->globals\[tu->global_count\+\+\] = g;\n    return 0;\n\}", re.DOTALL)

push_global_replacement = """    if (tu->global_count == tu->global_cap) {
        size_t ncap = tu->global_cap == 0 ? 32 : tu->global_cap * 2;
        cc_global_t *next = (cc_global_t *)realloc(tu->globals, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        tu->globals = next;
        tu->global_cap = ncap;
    }
    tu->globals[tu->global_count++] = g;
    return 0;
}"""
text = push_global_pattern.sub(push_global_replacement, text)

with open("usr.bin/cc/frontend/parser.c", "w") as f:
    f.write(text)
