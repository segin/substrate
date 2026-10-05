/*
 * getusershell(3), setusershell(3), endusershell(3) -- the permitted login
 * shells, one per call, from /etc/shells.
 *
 * A line of /etc/shells names one shell by absolute path.  Anything from a
 * '#' on is a comment, and a line that does not start with '/' once leading
 * blanks are gone is ignored, as in 4.3BSD.  Without the file the list is
 * the shell every system has, _PATH_BSHELL: an administrator who has not
 * written /etc/shells has not restricted anything beyond that.
 */
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The file read.  The host test (tests/lib/c/host_test_getusershell.c)
 * points this at a file of its own. */
#ifndef SHELLS_FILE
#define SHELLS_FILE _PATH_SHELLS
#endif

/* Longer than any path; a longer line is not a shell. */
#define SHELL_LINE_MAX 1024

static const char *const default_shells[] = { _PATH_BSHELL, NULL };

static FILE *shells_fp;             /* /etc/shells, once opened */
static int shells_opened;           /* the open has been tried */
static int default_idx;             /* next default, when there is no file */
static char shell_line[SHELL_LINE_MAX];

void setusershell(void)
{
    if (shells_fp != NULL) {
        rewind(shells_fp);
    }
    default_idx = 0;
}

void endusershell(void)
{
    if (shells_fp != NULL) {
        fclose(shells_fp);
        shells_fp = NULL;
    }
    shells_opened = 0;
    default_idx = 0;
}

char *getusershell(void)
{
    if (!shells_opened) {
        shells_fp = fopen(SHELLS_FILE, "r");
        shells_opened = 1;
        default_idx = 0;
    }

    if (shells_fp == NULL) {
        const char *s = default_shells[default_idx];

        if (s == NULL) {
            return NULL;
        }
        default_idx++;
        strlcpy(shell_line, s, sizeof(shell_line));
        return shell_line;
    }

    while (fgets(shell_line, sizeof(shell_line), shells_fp) != NULL) {
        char *p = shell_line;
        char *end;

        if (strchr(p, '\n') == NULL && !feof(shells_fp)) {
            /* Too long to be a path: drop the rest of the line. */
            int c;

            while ((c = fgetc(shells_fp)) != EOF && c != '\n') {
            }
            continue;
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p != '/') {
            continue;
        }
        end = p;
        while (*end != '\0' && *end != '\n' && *end != '#' &&
               *end != ' ' && *end != '\t') {
            end++;
        }
        *end = '\0';
        return p;
    }
    return NULL;
}
