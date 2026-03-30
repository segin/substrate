#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <at.h>

time_t parsed_time = -1;
time_t base_time;

struct yy_buffer_state;
typedef struct yy_buffer_state *YY_BUFFER_STATE;
YY_BUFFER_STATE yy_scan_string(const char *str);
void yy_delete_buffer(YY_BUFFER_STATE b);

int yyparse(void);

int at_parse_time(int argc, char *argv[], int optind, time_t *out_time) {
    if (optind >= argc) {
        *out_time = 0; // Immediate/now
        return 0;
    }

    char buf[256] = {0};
    for (int i = optind; i < argc; i++) {
        strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
        if (i < argc - 1) {
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        }
    }

    // Initialize lexer string
    YY_BUFFER_STATE buffer = yy_scan_string(buf);

    base_time = time(NULL);
    parsed_time = -1;

    int result = yyparse();

    yy_delete_buffer(buffer);

    if (result == 0 && parsed_time != -1) {
        *out_time = parsed_time;
        return 0;
    }

    return -1;
}
