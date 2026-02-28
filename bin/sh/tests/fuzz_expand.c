#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../expand.h"
#include "../shell_var.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) return 0;

    char *buffer = malloc(Size + 1);
    if (!buffer) return 0;
    
    memcpy(buffer, Data, Size);
    buffer[Size] = '\0';

    // Initialize environment for expansions
    shell_var_init(NULL);

    // Call expand_word, which handles the brunt of parameter expansions, math, etc.
    char *expanded = expand_word(buffer);
    if (expanded) {
        free(expanded);
    }
    
    // Also test expand_heredoc
    char *expanded_heredoc = expand_heredoc(buffer, 0);
    if (expanded_heredoc) {
        free(expanded_heredoc);
    }

    shell_var_destroy();
    free(buffer);

    return 0;
}
