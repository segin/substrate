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

static void print_formatted_size(off_t size, const ls_config_t *config) {
    if (!config->human_readable) {
        printf("%8ld ", (long)size);
        return;
    }
    const char *units[] = {"B", "K", "M", "G", "T"};
    int i = 0;
    double dsize = (double)size;
    while (dsize >= 1024 && i < 4) {
        dsize /= 1024.0;
        i++;
    }
    if (i == 0) printf("%7ld%s ", (long)size, units[i]);
    else printf("%7.1f%s ", dsize, units[i]);
}

static void print_color_name(file_info_t *f, const ls_config_t *config) {
    if (config->color) {
        if (S_ISDIR(f->st.st_mode)) printf("%s%s%s", COLOR_DIR, f->name, COLOR_RESET);
        else if (S_ISLNK(f->st.st_mode)) printf("%s%s%s", COLOR_LINK, f->name, COLOR_RESET);
        else if (f->st.st_mode & S_IXUSR) printf("%s%s%s", COLOR_EXE, f->name, COLOR_RESET);
        else if (S_ISCHR(f->st.st_mode) || S_ISBLK(f->st.st_mode)) printf("%s%s%s", COLOR_DEV, f->name, COLOR_RESET);
        else printf("%s", f->name);
    } else {
        printf("%s", f->name);
    }
    
    if (config->classify) {
        if (S_ISDIR(f->st.st_mode)) printf("/");
        else if (S_ISLNK(f->st.st_mode)) printf("@");
        else if (f->st.st_mode & S_IXUSR) printf("*");
        else if (S_ISFIFO(f->st.st_mode)) printf("|");
        else if (S_ISSOCK(f->st.st_mode)) printf("=");
    }
}

void ls_print_entry(file_info_t *f, const ls_config_t *config) {
    if (config->inode) {
        printf("%lu ", (unsigned long)f->st.st_ino);
    }

    if (config->long_fmt) {
        // Mode
        printf( (S_ISDIR(f->st.st_mode)) ? "d" : (S_ISLNK(f->st.st_mode) ? "l" : "-"));
        printf( (f->st.st_mode & S_IRUSR) ? "r" : "-");
        printf( (f->st.st_mode & S_IWUSR) ? "w" : "-");
        printf( (f->st.st_mode & S_IXUSR) ? "x" : "-");
        printf( (f->st.st_mode & S_IRGRP) ? "r" : "-");
        printf( (f->st.st_mode & S_IWGRP) ? "w" : "-");
        printf( (f->st.st_mode & S_IXGRP) ? "x" : "-");
        printf( (f->st.st_mode & S_IROTH) ? "r" : "-");
        printf( (f->st.st_mode & S_IWOTH) ? "w" : "-");
        printf( (f->st.st_mode & S_IXOTH) ? "x" : "-");
        printf(" ");

        // Links
        printf("%2ld ", (long)f->st.st_nlink);

        // User/Group
        if (config->numeric_ids) {
            printf("%-8d ", (int)f->st.st_uid);
            if (!config->no_group) printf("%-8d ", (int)f->st.st_gid);
        } else {
            struct passwd *pwd = getpwuid(f->st.st_uid);
            if (pwd) printf("%-8s ", pwd->pw_name);
            else printf("%-8d ", (int)f->st.st_uid);

            if (!config->no_group) {
                struct group *grp = getgrgid(f->st.st_gid);
                if (grp) printf("%-8s ", grp->gr_name);
                else printf("%-8d ", (int)f->st.st_gid);
            }
        }

        // Size
        print_formatted_size(f->st.st_size, config);

        // Time
        char timebuf[64];
        struct tm *tm = localtime(&f->st.st_mtime);
        if (tm) {
            strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", tm);
            printf("%s ", timebuf);
        } else {
            printf("??? ?? ??:?? ");
        }

        // Name
        print_color_name(f, config);
        
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
        print_color_name(f, config);
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
    if (config->long_fmt || config->one_per_line || !isatty(STDOUT_FILENO)) {
        for (int i = 0; i < count; i++) {
            ls_print_entry(&files[i], config);
            if (!config->long_fmt) printf("\n");
        }
        return;
    }

    int term_width = get_term_width();
    int max_len = 0;
    for (int i = 0; i < count; i++) {
        int len = strlen(files[i].name);
        if (config->inode) {
             char tmp[32];
             sprintf(tmp, "%lu ", (unsigned long)files[i].st.st_ino);
             len += strlen(tmp);
        }
        if (config->classify) len++;
        if (len > max_len) max_len = len;
    }

    int col_width = max_len + 2;
    int cols = term_width / col_width;
    if (cols <= 0) cols = 1;
    int rows = (count + cols - 1) / cols;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = c * rows + r; // Vertical ordering
            if (idx < count) {
                // Just use printf with width
                if (config->inode) printf("%lu ", (unsigned long)files[idx].st.st_ino);
                print_color_name(&files[idx], config);
                
                if (c < cols - 1 && (idx + rows) < count) {
                    int len = strlen(files[idx].name);
                    if (config->classify) len++;
                    if (config->inode) {
                        char tmp[32];
                        sprintf(tmp, "%lu ", (unsigned long)files[idx].st.st_ino);
                        len += strlen(tmp);
                    }
                    for (int s = 0; s < col_width - len; s++) printf(" ");
                }
            }
        }
        printf("\n");
    }
}

void ls_print_newline(const ls_config_t *config) {
    (void)config; // Not used anymore as ls_print_list handles its own newlines
}
