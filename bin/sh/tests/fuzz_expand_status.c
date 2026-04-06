#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../expand.h"
#include "../shell_var.h"

int execute_line(char *line) {
    return (int)(strlen(line) & 7);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    static const char *prefixes[] = {
        "${X:?boom}",
        "$(echo hi)",
        "`echo hi`",
        "X=$(echo hi)",
        NULL,
    };
    char *buffer;

    if (Size == 0) return 0;
    buffer = malloc(Size + 32);
    if (!buffer) return 0;

    shell_var_init(NULL);
    shell_var_set("X", "value");

    for (int i = 0; prefixes[i]; i++) {
        char *word = NULL;
        char **list = NULL;
        char *heredoc = NULL;
        expand_state_t state = {0};

        strcpy(buffer, prefixes[i]);
        memcpy(buffer + strlen(prefixes[i]), Data, Size);
        buffer[strlen(prefixes[i]) + Size] = '\0';

        (void)expand_word_ex(buffer, &word, &state);
        free(word);
        (void)expand_list_ex((char *[]) { buffer, NULL }, &list, &state);
        if (list) {
            for (size_t j = 0; list[j]; j++) free(list[j]);
            free(list);
        }
        (void)expand_heredoc_ex(buffer, 0, &heredoc, &state);
        free(heredoc);
    }

    shell_var_destroy();
    free(buffer);
    return 0;
}
