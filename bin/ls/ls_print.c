#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "ls_print.h"
#include "ls_colors.h"

static void print_formatted_size(off_t size, const ls_config_t *config) {
    // Apply block-size scaling if specified
    if (config->block_size > 0) {
        printf("%8ld ", (long)((size + config->block_size - 1) / config->block_size));
        return;
    }
    
    if (!config->human_readable) {
        printf("%8ld ", (long)size);
        return;
    }
    const char *units[] = {"B", "K", "M", "G", "T"};
    int base = config->si_units ? 1000 : 1024;
    int i = 0;
    double dsize = (double)size;
    while (dsize >= base && i < 4) {
        dsize /= base;
        i++;
    }
    if (i == 0) printf("%7ld%s ", (long)size, units[i]);
    else printf("%7.1f%s ", dsize, units[i]);
}

static void print_name(file_info_t *f, const ls_config_t *config) {
    if (config->quote_names) printf("\"");
    
    if (config->color) {
        const char *color = ls_colors_get(f->name, f->st.st_mode);
        if (color && *color) {
            printf("%s%s%s", color, f->name, ls_colors_reset());
        } else {
            printf("%s", f->name);
        }
    } else {
        printf("%s", f->name);
    }

    
    if (config->quote_names) printf("\"");
    
    if (config->classify) {
        if (S_ISDIR(f->st.st_mode)) printf("/");
        else if (S_ISLNK(f->st.st_mode)) printf("@");
        else if (f->st.st_mode & S_IXUSR) printf("*");
        else if (S_ISFIFO(f->st.st_mode)) printf("|");
        else if (S_ISSOCK(f->st.st_mode)) printf("=");
    } else if (config->file_type) {
        // Like -F but do not append * for executables
        if (S_ISDIR(f->st.st_mode)) printf("/");
        else if (S_ISLNK(f->st.st_mode)) printf("@");
        else if (S_ISFIFO(f->st.st_mode)) printf("|");
        else if (S_ISSOCK(f->st.st_mode)) printf("=");
    } else if (config->slash_dirs && S_ISDIR(f->st.st_mode)) {
        printf("/");
    }
}

void ls_print_entry(file_info_t *f, const ls_config_t *config) {
    if (config->show_blocks) {
        printf("%4ld ", (long)((f->st.st_blocks + 1) / 2)); // 1K blocks
    }
    
    if (config->inode) {
        printf("%lu ", (unsigned long)f->st.st_ino);
    }

    if (config->long_fmt) {
        // Mode
        char type = '-';
        if (S_ISDIR(f->st.st_mode)) type = 'd';
        else if (S_ISLNK(f->st.st_mode)) type = 'l';
        else if (S_ISCHR(f->st.st_mode)) type = 'c';
        else if (S_ISBLK(f->st.st_mode)) type = 'b';
        else if (S_ISFIFO(f->st.st_mode)) type = 'p';
        else if (S_ISSOCK(f->st.st_mode)) type = 's';
        printf("%c", type);
        printf("%c", (f->st.st_mode & S_IRUSR) ? 'r' : '-');
        printf("%c", (f->st.st_mode & S_IWUSR) ? 'w' : '-');
        printf("%c", (f->st.st_mode & S_IXUSR) ? 'x' : '-');
        printf("%c", (f->st.st_mode & S_IRGRP) ? 'r' : '-');
        printf("%c", (f->st.st_mode & S_IWGRP) ? 'w' : '-');
        printf("%c", (f->st.st_mode & S_IXGRP) ? 'x' : '-');
        printf("%c", (f->st.st_mode & S_IROTH) ? 'r' : '-');
        printf("%c", (f->st.st_mode & S_IWOTH) ? 'w' : '-');
        printf("%c ", (f->st.st_mode & S_IXOTH) ? 'x' : '-');

        // Links
        printf("%2ld ", (long)f->st.st_nlink);

        // Owner (skip if -g)
        if (!config->no_owner) {
            if (config->numeric_ids) {
                printf("%-8d ", (int)f->st.st_uid);
            } else {
                struct passwd *pwd = getpwuid(f->st.st_uid);
                if (pwd) printf("%-8s ", pwd->pw_name);
                else printf("%-8d ", (int)f->st.st_uid);
            }
        }

        // Group (skip if -o or -G)
        if (!config->no_group) {
            if (config->numeric_ids) {
                printf("%-8d ", (int)f->st.st_gid);
            } else {
                struct group *grp = getgrgid(f->st.st_gid);
                if (grp) printf("%-8s ", grp->gr_name);
                else printf("%-8d ", (int)f->st.st_gid);
            }
        }

        // Size
        print_formatted_size(f->st.st_size, config);

        // Time
        time_t t;
        switch (config->time_type) {
            case TIME_ATIME: t = f->st.st_atime; break;
            case TIME_CTIME: t = f->st.st_ctime; break;
            default: t = f->st.st_mtime; break;
        }
        char timebuf[64];
        struct tm *tm = localtime(&t);
        if (tm) {
            strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", tm);
            printf("%s ", timebuf);
        } else {
            printf("??? ?? ??:?? ");
        }

        // Name
        print_name(f, config);
        
        if (S_ISLNK(f->st.st_mode)) {
            char linkbuf[1024];
            ssize_t len = readlink(f->full_path, linkbuf, sizeof(linkbuf)-1);
            if (len != -1) {
                linkbuf[len] = 0;
                printf(" -> %s", linkbuf);
            }
        }
        printf("\n");
    } else {
        print_name(f, config);
    }
}

static int get_term_width(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return w.ws_col;
    }
    const char *cols = getenv("COLUMNS");
    if (cols) return atoi(cols);
    return 80;
}

void ls_print_list(file_info_t *files, int count, const ls_config_t *config) {
    // Comma-separated mode
    if (config->comma_sep) {
        for (int i = 0; i < count; i++) {
            print_name(&files[i], config);
            if (i < count - 1) printf(", ");
        }
        printf("\n");
        return;
    }

    // Long format or one per line
    if (config->long_fmt || config->one_per_line || !isatty(STDOUT_FILENO)) {
        for (int i = 0; i < count; i++) {
            ls_print_entry(&files[i], config);
            if (!config->long_fmt) printf("\n");
        }
        return;
    }

    // Multi-column output
    int term_width = get_term_width();
    int max_len = 0;
    for (int i = 0; i < count; i++) {
        int len = strlen(files[i].name);
        if (config->inode) {
             char tmp[32];
             sprintf(tmp, "%lu ", (unsigned long)files[i].st.st_ino);
             len += strlen(tmp);
        }
        if (config->show_blocks) len += 5;
        if (config->classify || config->slash_dirs) len++;
        if (config->quote_names) len += 2;
        if (len > max_len) max_len = len;
    }

    int col_width = max_len + 2;
    int num_cols = term_width / col_width;
    if (num_cols <= 0) num_cols = 1;
    int num_rows = (count + num_cols - 1) / num_cols;

    for (int r = 0; r < num_rows; r++) {
        for (int c = 0; c < num_cols; c++) {
            int idx;
            if (config->by_lines) {
                idx = r * num_cols + c; // Horizontal
            } else {
                idx = c * num_rows + r; // Vertical (default)
            }
            if (idx < count) {
                ls_print_entry(&files[idx], config);
                
                if (c < num_cols - 1) {
                    int len = strlen(files[idx].name);
                    if (config->classify || config->slash_dirs) len++;
                    if (config->quote_names) len += 2;
                    if (config->inode) {
                        char tmp[32];
                        sprintf(tmp, "%lu ", (unsigned long)files[idx].st.st_ino);
                        len += strlen(tmp);
                    }
                    if (config->show_blocks) len += 5;
                    int next_idx = config->by_lines ? (r * num_cols + c + 1) : ((c + 1) * num_rows + r);
                    if (next_idx < count) {
                        for (int s = 0; s < col_width - len; s++) printf(" ");
                    }
                }
            }
        }
        printf("\n");
    }
}

void ls_print_newline(const ls_config_t *config) {
    (void)config;
}
