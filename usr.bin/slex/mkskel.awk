# mkskel.awk - turn scanner.skel into skel.h.
#
# Each "%% section NAME" part becomes
#     static const char *const skel_NAME[] = { "line", ..., NULL };
# Other lines starting with "%%" are comments and are dropped.

BEGIN {
    print "/* Generated from scanner.skel by mkskel.awk; do not edit. */"
    open_section = 0
}

function close_section() {
    if (open_section)
        print "    NULL\n};\n"
    open_section = 0
}

/^%% section / {
    close_section()
    printf "static const char *const skel_%s[] = {\n", $3
    open_section = 1
    next
}

/^%%/ {
    close_section()
    next
}

{
    if (!open_section) {
        print "mkskel.awk: text outside a section at line " NR > "/dev/stderr"
        exit 1
    }
    printf "    \"%s\",\n", cquote($0)
}

# Escape \ and " one character at a time: the meaning of backslashes in a
# gsub() replacement differs between awk implementations.
function cquote(s,    out, i, ch) {
    out = ""
    for (i = 1; i <= length(s); i++) {
        ch = substr(s, i, 1)
        if (ch == "\\" || ch == "\"")
            out = out "\\"
        out = out ch
    }
    return out
}

END {
    close_section()
}
