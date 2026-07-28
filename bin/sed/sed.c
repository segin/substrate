/*
 * sed.c - Stream Editor
 *
 * POSIX sed with BSD extensions (q exit-code, Q, R, W, T, I flag,
 * first~step, 0,/re/) and selected GNU extensions (z, F, e, +N/~N
 * ranges, l width, \u\l\U\L\E in replacement).
 *
 * Usage:
 *   sed [-EnrsSz] [-i[ext]] script [file ...]
 *   sed [-EnrsSz] [-i[ext]] -e script ... [-f file ...] [file ...]
 *   sed [-EnrsSz] [-i[ext]] [-e script ...] -f file ... [file ...]
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sed.h"
#include <sys/stat.h>

/* Global execution state */
sed_state_t G;

static void
usage(void)
{
    fputs(
        "usage: sed [-EnrsSz] [-i[ext]] script [file ...]\n"
        "       sed [-EnrsSz] [-i[ext]] -e script [-f file] ... [file ...]\n"
        "       sed [-EnrsSz] [-i[ext]] [-e script] -f file  ... [file ...]\n"
        "\n"
        "Options:\n"
        "  -E, -r        use extended regular expressions (ERE)\n"
        "  -n            suppress default output\n"
        "  -i[ext]       edit files in-place (backup extension optional)\n"
        "  -e script     add script to the list\n"
        "  -f file       read script from file\n"
        "  -s            treat each file separately (reset line counter)\n"
        "  -S            sandbox mode (disable r/R/w/W/e commands)\n"
        "  -z            use NUL as line delimiter\n"
        "  -l width      set l-command wrap width (default: 70)\n"
        "\n"
        "SECURITY: without -S, a sed script can read and write arbitrary\n"
        "files (r/R/w/W) and run shell commands (e, s///e). Always pass -S\n"
        "when running a script from an untrusted source.\n",
        stderr);
}

static int
process_files(int argc, char **argv, int optind)
{
    int ret = 0;
    int nfiles = argc - optind;

    if (nfiles == 0) {
        /* read from stdin */
        ret = sed_process_file(stdin, "(stdin)", true);
    } else if (!G.inplace) {
        for (int i = optind; i < argc; i++) {
            FILE *fp;
            const char *name = argv[i];
            if (strcmp(name, "-") == 0) {
                fp = stdin;
            } else {
                fp = fopen(name, "r");
                if (!fp) {
                    warn("cannot open '%s': %s", name, strerror(errno));
                    ret = 1;
                    continue;
                }
            }
            int r = sed_process_file(fp, name, i == argc - 1);
            if (fp != stdin) fclose(fp);
            if (r != 0) { ret = r; break; }
        }
    } else {
        /* in-place editing */
        for (int i = optind; i < argc; i++) {
            const char *name = argv[i];
            FILE *fp = fopen(name, "r");
            if (!fp) {
                warn("cannot open '%s': %s", name, strerror(errno));
                ret = 1;
                continue;
            }

            /* Capture the original's mode/owner so the replacement can
             * preserve them (SED-04) rather than inherit mkstemp's 0600. */
            struct stat orig_st;
            int have_st = (fstat(fileno(fp), &orig_st) == 0);

            /* build temp filename */
            size_t nlen = strlen(name);
            char *tmpname = malloc(nlen + 16);
            if (!tmpname) die("out of memory");
            snprintf(tmpname, nlen + 16, "%s.XXXXXX", name);

            int tmpfd = -1;
            FILE *tmpfp = NULL;

#ifdef HAVE_MKSTEMP
            tmpfd = mkstemp(tmpname);
            if (tmpfd < 0) {
                warn("mkstemp '%s': %s", tmpname, strerror(errno));
                free(tmpname); fclose(fp); ret = 1; continue;
            }
            tmpfp = fdopen(tmpfd, "w");
#else
            /* fallback: use a deterministic name */
            snprintf(tmpname, nlen + 16, "%s.sed%d", name, (int)getpid());
            tmpfp = fopen(tmpname, "w");
#endif
            if (!tmpfp) {
                warn("cannot create temp '%s': %s", tmpname, strerror(errno));
                if (tmpfd >= 0) { close(tmpfd); unlink(tmpname); } /* SED-09 */
                free(tmpname); fclose(fp); ret = 1; continue;
            }

            /* Flush the real stdout BEFORE redirecting, otherwise pending
             * output would be written into the temp file (SED-07). */
            fflush(stdout);
            int saved_stdout_fd = dup(STDOUT_FILENO);
            dup2(fileno(tmpfp), STDOUT_FILENO);

            if (G.separate) {
                /* reset ranges per-file */
                for (cmd_t *c = G.cmds; c; c = c->next) {
                    c->in_range = 0; c->range_end = 0;
                }
                G.lineno = 0; G.subst_flag = false;
            }

            int r = sed_process_file(fp, name, i == argc - 1);

            /* Detect any write error to the temp before committing it over
             * the original: a silent ENOSPC would otherwise rename a
             * truncated temp and lose the file (SED-03). */
            int werr = (fflush(stdout) != 0) || ferror(stdout);

            /* restore stdout */
            dup2(saved_stdout_fd, STDOUT_FILENO);
            close(saved_stdout_fd);

            /* Preserve the original mode/owner (SED-04) and force the data to
             * disk before the rename commits it. */
            if (have_st) {
                (void)fchmod(fileno(tmpfp), orig_st.st_mode & 07777);
                (void)fchown(fileno(tmpfp), orig_st.st_uid, orig_st.st_gid);
            }
            if (fsync(fileno(tmpfp)) != 0) werr = 1;
            if (fclose(tmpfp) != 0) werr = 1;
            fclose(fp);

            if (werr) {
                warn("write error on '%s'; original left unchanged", name);
                unlink(tmpname); free(tmpname); ret = 1; continue;
            }
            if (r != 0 && r != G.exit_code) {
                unlink(tmpname); free(tmpname); ret = r; continue;
            }

            /* backup if extension specified */
            if (G.inplace_ext && G.inplace_ext[0]) {
                char *backname = malloc(nlen + strlen(G.inplace_ext) + 2);
                if (!backname) die("out of memory");
                snprintf(backname, nlen + strlen(G.inplace_ext) + 2,
                         "%s%s", name, G.inplace_ext);
                if (rename(name, backname) < 0) {
                    warn("cannot rename '%s' to '%s': %s",
                         name, backname, strerror(errno));
                    free(backname); unlink(tmpname); free(tmpname);
                    ret = 1; continue;
                }
                free(backname);
            }
            if (rename(tmpname, name) < 0) {
                warn("cannot rename '%s' to '%s': %s",
                     tmpname, name, strerror(errno));
                ret = 1;
            }
            free(tmpname);
        }
    }
    return ret;
}

int
main(int argc, char **argv)
{
    /* init global state */
    memset(&G, 0, sizeof(G));
    G.list_wrap = DEFAULT_LIST_WRAP;
    db_init(&G.pat);
    db_init(&G.hold);
    db_init(&G.append_queue);
    /* hold space starts as empty string */
    db_set(&G.hold, "", 0);

    bool have_script = false;
    int  optind_out  = 1;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (arg[0] != '-' || arg[1] == '\0') {
            /* not an option: first non-option is script if none yet */
            optind_out = i;
            break;
        }

        if (arg[1] == '-' && arg[2] == '\0') {
            /* -- */
            optind_out = i + 1;
            break;
        }

        /* parse short options (may be clustered) */
        bool done = false;
        for (int j = 1; arg[j] && !done; j++) {
            switch (arg[j]) {
            case 'E': case 'r':
                G.use_ere = true;
                break;
            case 'n':
                G.suppress = true;
                break;
            case 's':
                G.separate = true;
                break;
            case 'S':
                G.sandbox = true;
                break;
            case 'z':
                G.null_delim = true;
                break;
            case 'i':
                G.inplace = true;
                /* extension is optional, may be attached or separate */
                if (arg[j+1]) {
                    G.inplace_ext = strdup(arg + j + 1);
                } else if (i + 1 < argc && argv[i+1][0] != '-') {
                    /* BSD convention: -i ext as separate arg */
                    /* Actually BSD sed takes it as attached: -i.bak */
                    /* GNU sed: -i.bak or -i with no ext */
                    /* We take attached only (BSD style) */
                    G.inplace_ext = strdup("");
                } else {
                    G.inplace_ext = strdup("");
                }
                done = true; /* rest of cluster is the extension */
                break;
            case 'e':
                if (arg[j+1]) {
                    script_append(arg + j + 1);
                } else {
                    if (i + 1 >= argc) die("-e requires an argument");
                    script_append(argv[++i]);
                }
                have_script = true;
                done = true;
                break;
            case 'f':
                if (arg[j+1]) {
                    script_append_file(arg + j + 1);
                } else {
                    if (i + 1 >= argc) die("-f requires an argument");
                    script_append_file(argv[++i]);
                }
                have_script = true;
                done = true;
                break;
            case 'l':
                if (arg[j+1]) {
                    G.list_wrap = atoi(arg + j + 1);
                } else {
                    if (i + 1 >= argc) die("-l requires an argument");
                    G.list_wrap = atoi(argv[++i]);
                }
                done = true;
                break;
            default:
                die("unknown option '-%c'", arg[j]);
            }
        }

        optind_out = i + 1;
    }

    /* if no -e or -f, first non-option arg is the script */
    if (!have_script) {
        if (optind_out >= argc) {
            usage();
            return 1;
        }
        script_append(argv[optind_out]);
        optind_out++;
    }

    /* parse the script */
    if (script_parse() < 0) return 1;

    /* process files */
    int ret = process_files(argc, argv, optind_out);

    /* close write files */
    for (int i = 0; i < G.write_count; i++) {
        if (G.write_fps[i]) fclose(G.write_fps[i]);
        free(G.write_files[i]);
    }

    db_free(&G.pat);
    db_free(&G.hold);
    db_free(&G.append_queue);

    return ret;
}
