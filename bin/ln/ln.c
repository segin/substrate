#include <liblink.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
    ln_options_t opts;
    int operand_index;
    int show_help;

    ln_options_init(&opts, (argc > 0 && argv[0]) ? argv[0] : "ln");

    if (ln_parse_options(&opts, argc, argv, &operand_index, &show_help) != 0) {
        fprintf(stderr, "%s", ln_usage());
        return 1;
    }

    if (show_help) {
        printf("%s", ln_usage());
        return 0;
    }

    return ln_execute(&opts, argc, argv, operand_index);
}
