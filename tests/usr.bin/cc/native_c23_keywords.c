thread_local int tls_value;
alignas(32) int aligned_global;

int main(void) {
    bool flag = true;
    static_assert(alignof(aligned_global) >= 4, "alignof(expr)");
    static_assert(1);
    if (!flag || false)
        return 1;
    if (tls_value != 0)
        return 2;
    if ((alignof(aligned_global) & (alignof(aligned_global) - 1)) != 0)
        return 3;
    return aligned_global;
}
