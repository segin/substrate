#include "rm.h"

#include "rm_opts.h"
#include "rm_safety.h"
#include "rm_walk.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static volatile sig_atomic_t rm_interrupted = 0;

static void
rm_handle_signal(int signum)
{
    rm_interrupted = signum;
}

static void
rm_print_usage(FILE *stream, const char *progname)
{
    fprintf(stream,
        "Usage: %s [OPTION]... [FILE]...\n"
        "Remove (unlink) FILE(s).\n",
        progname);
}

static void
rm_print_help(const char *progname)
{
    rm_print_usage(stdout, progname);
    fputs(
        "\n"
        "Options:\n"
        "  -d, --dir                 remove empty directories\n"
        "  -f, --force               ignore nonexistent files and never prompt\n"
        "  -i                        prompt before every removal\n"
        "  -I                        prompt once before removing more than 3 files or recursively\n"
        "      --interactive[=WHEN]  prompt according to WHEN: never, once, always\n"
        "  -r, -R, --recursive       remove directories and their contents recursively\n"
        "      --one-file-system     do not descend across filesystem boundaries\n"
        "      --preserve-root       refuse recursive removal of '/' (default)\n"
        "      --no-preserve-root    allow recursive removal checks to ignore '/' protection\n"
        "  -v, --verbose             explain what is being done\n"
        "      --help                display this help and exit\n"
        "      --version             output version information and exit\n",
        stdout);
}

static void
rm_print_version(void)
{
    puts(RM_VERSION);
}

static void
rm_install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = rm_handle_signal;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
}

static int
rm_maybe_prompt_once(const struct rm_options *opts, FILE **prompt_input)
{
    char question[256];

    if (opts->prompt_mode != RM_PROMPT_ONCE ||
        (!opts->recursive && opts->operand_count <= 3)) {
        return 1;
    }

    if (*prompt_input == NULL) {
        *prompt_input = rm_open_prompt_stream();
    }
    if (*prompt_input == NULL) {
        return 0;
    }

    snprintf(question, sizeof(question),
        opts->recursive ? "%s: remove %d arguments recursively? " :
        "%s: remove %d arguments? ",
        opts->progname, opts->operand_count);
    return rm_prompt_string(*prompt_input, question);
}

int
main(int argc, char *argv[])
{
    const char *err_msg;
    struct rm_options opts;
    struct rm_walk_state walk_state;
    FILE *prompt_input;
    int index;
    int status;

    err_msg = NULL;
    prompt_input = NULL;
    status = 0;

    rm_options_init(&opts, argv[0]);
    if (rm_parse_options(&opts, argc, argv, &err_msg) != 0) {
        rm_print_usage(stderr, opts.progname);
        if (err_msg != NULL) {
            fprintf(stderr, "%s: %s\n", opts.progname, err_msg);
        }
        return 1;
    }
    if (opts.show_help) {
        rm_print_help(opts.progname);
        return 0;
    }
    if (opts.show_version) {
        rm_print_version();
        return 0;
    }
    if (opts.operand_count == 0) {
        return 0;
    }

    rm_install_signal_handlers();
    if (!rm_maybe_prompt_once(&opts, &prompt_input)) {
        if (prompt_input != NULL && prompt_input != stdin) {
            fclose(prompt_input);
        }
        return 0;
    }

    walk_state.opts = &opts;
    walk_state.prompt_input = prompt_input;
    walk_state.interrupted = &rm_interrupted;

    for (index = opts.operand_start; index < argc; ++index) {
        int result;

        result = rm_remove_operand(&walk_state, argv[index]);
        if (result == RM_WALK_FAILED) {
            status = 1;
        }
    }

    if (rm_interrupted != 0 && status == 0) {
        status = 1;
    }
    if (walk_state.prompt_input != NULL && walk_state.prompt_input != stdin) {
        fclose(walk_state.prompt_input);
    }
    return status;
}

