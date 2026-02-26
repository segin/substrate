#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "prompt.h"

// Mocks for dependencies if any
// prompt.c uses shell_var_get for "?" in evaluate_prompt, but expand_prompt_escapes is cleaner.
// verify prompt.c dependencies.
// expand_prompt_escapes uses:
// - buffer_append (util.c)
// - snprintf, geteuid, getpwuid, getuid, gethostname, getcwd, getenv.

// We don't need to mock standard libc functions for the fuzzer usually, 
// unless we want to stabilize them. 
// But protecting against getenv/getcwd crashes in fuzzer is good.
// Mocks for dependencies
// prompt.c:evaluate_prompt uses these, but we are only fuzzing expand_prompt_escapes.
// We provide stubs to satisfy the linker.

int shell_xtrace = 0;
int shell_errexit = 0;
int shell_promptvars = 0;

char *expand_word(const char *word) {
    return word ? strdup(word) : NULL;
}

// Job control stubs (required by prompt.c %j implementation)
typedef struct job { struct job *next; } job_t;
job_t *first_job = NULL;
int job_is_completed(job_t *j) { (void)j; return 1; }

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    // Null-terminate the input data to make it a valid C-string
    char *str = malloc(Size + 1);
    if (!str) return 0;
    memcpy(str, Data, Size);
    str[Size] = '\0';

    // Fuzz expand_prompt_escapes
    // command_count can be arbitrary, say 42.
    // Toggle extended mode based on first byte?
    int extended = (Size > 0) ? (Data[0] % 2) : 0;
    char *res = expand_prompt_escapes(str, 42, extended, 0);
    
    // We just want to ensure it doesn't crash or leak.
    if (res) free(res);
    free(str);
    
    return 0;
}
