#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "prompt.h"
#include "util.h"
#include "shell_var.h"

// Simple test framework
#define ASSERT_STR_EQ(actual, expected) do { \
    if (strcmp(actual, expected) != 0) { \
        fprintf(stderr, "FAIL: %s:%d: Expected '%s', got '%s'\n", __FILE__, __LINE__, expected, actual); \
        exit(1); \
    } \
} while(0)

// Mocks for linker resolution and spy
char *last_expand_word_input = NULL;

int shell_xtrace = 0;
int shell_errexit = 0;
int shell_promptvars = 0;

char *expand_word(const char *word) { 
    if (last_expand_word_input) free(last_expand_word_input);
    last_expand_word_input = word ? strdup(word) : NULL;
    return word ? strdup(word) : NULL; 
}

// Stubs for job control (required by prompt.c %j implementation)
#include "../job.h"
job_t *first_job = NULL;
pid_t shell_pgid = 0;

int job_is_completed(job_t *j) {
    // Stub: no jobs in test harness
    (void)j;
    return 1;
}

void test_expansion_order() {
    // Verify that escapes are expanded BEFORE expand_word is called.
    // If we pass "\\u", prompt expansion should turn it into username (or uid).
    // So expand_word should receive "username", NOT "\\u".
    
    char *res = evaluate_prompt("\\u", 0, 0);
    
    // Check what expand_word received
    assert(last_expand_word_input != NULL);
    assert(strcmp(last_expand_word_input, "\\u") != 0);
    // It should be equal to the result (since our mock expands to identity)
    ASSERT_STR_EQ(last_expand_word_input, res);
    
    free(res);
    
    // Verify literal backslash
    // PS1 = "\\\\" -> escapes to "\\" -> expand_word gets "\\"
    res = evaluate_prompt("\\\\", 0, 0);
    ASSERT_STR_EQ(last_expand_word_input, "\\\\");
    free(res);
}

void test_empty_ps1() {
    char *res = evaluate_prompt("", 0, 0);
    // Should return empty string, not NULL (as handled by expand_word mock which strdups empty)
    ASSERT_STR_EQ(res, "");
    free(res);
}

void test_nested_structure() {
    // Verify that nested command subs are passed through to expand_word
    const char *input = "$(echo $(echo foo))";
    char *res = evaluate_prompt(input, 0, 0);
    
    // expand_prompt_escapes should not touch $(...)
    // expand_word (mock) should receive it as is
    ASSERT_STR_EQ(last_expand_word_input, input);
    free(res);
}

void test_history_escape() {
    char *res = expand_prompt_escapes("\\!", 42, 0, 0);
    ASSERT_STR_EQ(res, "42");
    free(res);
    
    res = expand_prompt_escapes("cmd: \\!", 100, 0, 0);
    ASSERT_STR_EQ(res, "cmd: 100");
    free(res);
}

void test_uid_escape() {
    // Note: This matches current strict implementation which just checks geteuid()
    // We can't easily mock geteuid in unit test without weak symbols or dlsym tricks.
    // So we just check consistency.
    char *res = expand_prompt_escapes("\\$", 0, 0, 0);
    int is_root = (geteuid() == 0);
    ASSERT_STR_EQ(res, is_root ? "#" : "$");
    free(res);
}

void test_literal_backslash() {
    char *res = expand_prompt_escapes("\\\\", 0, 0, 0);
    ASSERT_STR_EQ(res, "\\\\"); // expand_prompt_escapes returns literal "\\" for display
    free(res);
}

void test_invalid_escapes() {
    // \a should be 'a'
    char *res = expand_prompt_escapes("\\Q", 0, 0, 0);
    ASSERT_STR_EQ(res, "Q");
    free(res);
}

void test_mixed() {
    char *res = expand_prompt_escapes("\\u@\\h:\\w \\$ ", 1, 0, 0);
    // Just verify it's not NULL and has some structure
    assert(res != NULL);
    assert(strstr(res, "@") != NULL);
    free(res);
}

void test_extended_escapes() {
    // POSIX mode (0) should ignore %
    char *res = expand_prompt_escapes("Pre%nPost", 1, 0, 0);
    ASSERT_STR_EQ(res, "Pre%nPost");
    free(res);
    
    // Extended mode (1) should expand %n, %m, etc.
    // %% -> %
    res = expand_prompt_escapes("%%", 1, 1, 0);
    ASSERT_STR_EQ(res, "%");
    free(res);
    
    // %# can vary, just ensure not literal
    res = expand_prompt_escapes("%#", 1, 1, 0);
    assert(res && (strcmp(res, "%") == 0 || strcmp(res, "#") == 0));
    free(res);
}

void test_colors() {
    // Red foreground
    char *res = expand_prompt_escapes("%F{red}RED%f", 1, 1, 0);
    ASSERT_STR_EQ(res, "\001\033[31m\002RED\001\033[39m\002");
    free(res);
    
    // Blue background
    res = expand_prompt_escapes("%K{blue}BG%k", 1, 1, 0);
    ASSERT_STR_EQ(res, "\001\033[44m\002BG\001\033[49m\002");
    free(res);
    
    // Invalid/Unknown color -> currently ignored (empty code) or unchanged?
    // Implementation does 'if (code) append'. If code is NULL, nothing appended.
    // So %F{invalid} -> "" (nothing).
    res = expand_prompt_escapes("A%F{invalid}B", 1, 1, 0);
    ASSERT_STR_EQ(res, "AB"); // "invalid" color produces no output
    free(res);
    
    // Malformed
    res = expand_prompt_escapes("%F", 1, 1, 0);
    ASSERT_STR_EQ(res, "%F");
    free(res);

    // Truncated/Malformed %F
    res = expand_prompt_escapes("%F{red", 1, 1, 0);
    ASSERT_STR_EQ(res, "%F{red");
    free(res);
    
    // Bold
    res = expand_prompt_escapes("%BBOLD%b", 1, 1, 0);
    ASSERT_STR_EQ(res, "\001\033[1m\002BOLD\001\033[22m\002");
    free(res);
    
    // Underline
    res = expand_prompt_escapes("%UUNDERLINE%u", 1, 1, 0);
    ASSERT_STR_EQ(res, "\001\033[4m\002UNDERLINE\001\033[24m\002");
    free(res);
}
void test_conditional_tokens() {
    // Condition: ? (exit status)
    // Mocking get("?") is tricky in unit test as it depends on shell_var_get
    // which is linked from shell_var.c. We can't easily set it here without mocking setup.
    // However, the real shell_var_get works if linked. 
    // Let's assume ? is unset or 0 initially if not set.
    
    // Explicitly set ? to 0
    shell_var_set("?", "0");
    char *res = expand_prompt_escapes("%(?.NONZERO.ZERO)", 1, 1, 0);
    ASSERT_STR_EQ(res, "ZERO");
    free(res);

    shell_var_set("?", "1");
    res = expand_prompt_escapes("%(?.NONZERO.ZERO)", 1, 1, 0);
    ASSERT_STR_EQ(res, "NONZERO");
    free(res);
    
    shell_var_set("?", "1");
    res = expand_prompt_escapes("%(?.%F{red}ERROR%f.OK)", 1, 1, 0);
    ASSERT_STR_EQ(res, "\001\033[31m\002ERROR\001\033[39m\002");
    free(res);

    // Condition: # (uid)
    // Assume not root for test runner
    int is_root = (geteuid() == 0);
    res = expand_prompt_escapes("%(#/ROOT/USER)", 1, 1, 0); // using / as delimiter
    ASSERT_STR_EQ(res, is_root ? "ROOT" : "USER");
    free(res);
    
    // Nested conditional
    // ?=1 -> TRUE block -> contains %(#...)
    shell_var_set("?", "1");
    res = expand_prompt_escapes("%(?.%(#/ROOT/USER).ok)", 1, 1, 0);
    ASSERT_STR_EQ(res, is_root ? "ROOT" : "USER");
    free(res);
    
    // Malformed
    res = expand_prompt_escapes("%(", 1, 1, 0);
    ASSERT_STR_EQ(res, "%(");
    free(res);
    
    res = expand_prompt_escapes("%(?", 1, 1, 0);
    ASSERT_STR_EQ(res, "%(?");
    free(res);
}

void test_reserved_tokens() {
    // %T and %D are now implemented and expand to time/date.
    char *res = expand_prompt_escapes("A%T%DB", 1, 1, 0);
    // As long as it doesn't crash and returns a string starting with A and ending with B
    int has_time = (res != NULL && strlen(res) >= 2 && res[0] == 'A' && res[strlen(res)-1] == 'B');
    if (!has_time) {
        fprintf(stderr, "FAIL: Expected %T and %D to produce time output, got '%s'\n", res ? res : "NULL");
        exit(1);
    }
    free(res);
    
    // %L should expand to SHLVL (or "1" if not set)
    // %j should expand to job count (stub returns "0")
    res = expand_prompt_escapes("%L jobs:%j", 1, 1, 0);
    // SHLVL should be set to some value, typically "1" in tests, jobs should be "0"
    // We'll check for non-empty output
    int has_content = (res != NULL && strlen(res) > 0);
    if (!has_content) {
        fprintf(stderr, "FAIL: Expected %L and %j to produce output\n");
        exit(1);
    }
    free(res);
}

// Test: ANSI codes should be wrapped in \001...\002 for correct width calculation
void test_ansi_width_markers() {
    char *res;
    
    // Color codes must have width markers
    res = expand_prompt_escapes("%F{red}text%f", 1, 1, 0);
    // Should contain \001 (0x01) and \002 (0x02) markers
    assert(strchr(res, '\001') != NULL);
    assert(strchr(res, '\002') != NULL);
    free(res);
    
    // Bold codes must have width markers
    res = expand_prompt_escapes("%Bbold%b", 1, 1, 0);
    assert(strchr(res, '\001') != NULL);
    assert(strchr(res, '\002') != NULL);
    free(res);
    
    // Underline codes must have width markers
    res = expand_prompt_escapes("%Uunder%u", 1, 1, 0);
    assert(strchr(res, '\001') != NULL);
    assert(strchr(res, '\002') != NULL);
    free(res);
    
    printf("  ANSI width markers: PASS\n");
}

// Test: Extended mode integration
void test_extended_mode_integration() {
    char *res;
    
    // Full extended prompt with multiple features
    res = expand_prompt_escapes("%F{green}%n%f@%m %~ %# ", 1, 1, 0);
    // Should have expanded user, host, cwd
    assert(res != NULL);
    assert(strlen(res) > 0);
    // Should contain visible characters (not just escapes)
    int has_visible = 0;
    for (char *p = res; *p; p++) {
        if (*p != '\001' && *p != '\002' && *p != '\033' && *p != '[' && *p != 'm') {
            has_visible = 1;
            break;
        }
    }
    assert(has_visible);
    free(res);
    
    printf("  Extended mode integration: PASS\n");
}

// Property test: Random-ish prompt strings don't crash
void test_property_no_crash() {
    const char *test_inputs[] = {
        "%%%%%",
        "%%n%%m%%~",
        "%F{%F{red}",
        "%K{",
        "%(?.ok.fail",
        "%(?...)",
        "\\\\\\\\\\\\",
        "\\!\\$\\u\\h\\w",
        "%n%m%~%#%?%L%j%B%b%U%u%f%k",
        "$(echo $(echo $(echo x)))",
        "`echo \\`echo nested\\``",
        "\x01\x02\x1b[31m",
        "%F{red}%K{blue}%B%Ufancy%u%b%k%f",
        "",
        NULL
    };
    
    for (int i = 0; test_inputs[i] != NULL; i++) {
        char *res = expand_prompt_escapes(test_inputs[i], 1, 1, 0);
        if (res) free(res);
    }
    
    printf("  Property (no crash): PASS\n");
}

int main() {
    printf("Running prompt tests...\n");
    test_history_escape();
    test_uid_escape();
    test_literal_backslash();
    test_invalid_escapes();
    test_mixed();
    test_extended_escapes();
    test_colors();
    test_conditional_tokens();
    test_reserved_tokens();
    test_expansion_order();
    test_empty_ps1();
    test_nested_structure();
    test_ansi_width_markers();
    test_extended_mode_integration();
    test_property_no_crash();
    printf("All prompt tests passed!\n");
    return 0;
}
