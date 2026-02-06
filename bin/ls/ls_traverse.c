#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include "ls_traverse.h"
#include "ls_sort.h"
#include "ls_print.h"

static void list_single_dir(const char *path, const ls_config_t *config);

void ls_list_dir(const char *path, const ls_config_t *config) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        perror(path);
        return;
    }

    if (!S_ISDIR(st.st_mode) || config->directory) {
        file_info_t f;
        f.name = strdup(path);
        f.full_path = strdup(path);
        f.st = st;
        if (f.name && f.full_path) {
            ls_print_list(&f, 1, config);
        }
        free(f.name);
        free(f.full_path);
        return;
    }

    list_single_dir(path, config);
}

static void list_single_dir(const char *path, const ls_config_t *config) {
    DIR *d = opendir(path);
    if (!d) {
        perror(path);
        return;
    }

    if (config->recursive) {
        printf("%s:\n", path);
    }

    file_info_t *files = NULL;
    int count = 0;
    int cap = 16;
    files = malloc(cap * sizeof(file_info_t));
    if (!files) {
        fprintf(stderr, "ls: out of memory\n");
        closedir(d);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!config->all) {
            if (ent->d_name[0] == '.') {
                if (!config->almost_all) continue;
                if (config->almost_all && (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)) continue;
            }
        } else {
            if (config->almost_all && (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)) continue;
        }

        if (count >= cap) {
            cap *= 2;
            file_info_t *new_files = realloc(files, cap * sizeof(file_info_t));
            if (!new_files) {
                fprintf(stderr, "ls: out of memory\n");
                // Cleanup what we have so far
                for (int i = 0; i < count; i++) {
                    free(files[i].name);
                    free(files[i].full_path);
                }
                free(files);
                closedir(d);
                return;
            }
            files = new_files;
        }

        files[count].name = strdup(ent->d_name);
        if (!files[count].name) {
             fprintf(stderr, "ls: out of memory\n");
             // Cleanup
             for (int i = 0; i < count; i++) {
                 free(files[i].name);
                 free(files[i].full_path);
             }
             free(files);
             closedir(d);
             return;
        }
        
        size_t len = strlen(path) + strlen(ent->d_name) + 2;
        files[count].full_path = malloc(len);
        if (!files[count].full_path) {
             fprintf(stderr, "ls: out of memory\n");
             free(files[count].name);
             for (int i = 0; i < count; i++) {
                 free(files[i].name);
                 free(files[i].full_path);
             }
             free(files);
             closedir(d);
             return;
        }

        if (strcmp(path, ".") == 0 || strcmp(path, "/") == 0) {
             sprintf(files[count].full_path, "%s%s", (strcmp(path, "/")==0) ? "/" : "", ent->d_name);
        } else {
             sprintf(files[count].full_path, "%s/%s", path, ent->d_name);
        }
        
        if (lstat(files[count].full_path, &files[count].st) != 0) {
            memset(&files[count].st, 0, sizeof(struct stat));
        }
        count++;
    }
    closedir(d);

    if (count > 0) {
        ls_sort_entries(files, count, config);
        ls_print_list(files, count, config);
    }

    if (config->recursive) {
        for (int i = 0; i < count; i++) {
            if (S_ISDIR(files[i].st.st_mode)) {
                if (strcmp(files[i].name, ".") == 0 || strcmp(files[i].name, "..") == 0) continue;
                printf("\n");
                list_single_dir(files[i].full_path, config);
            }
        }
    }

    // Individual cleanup
    for (int i = 0; i < count; i++) {
        free(files[i].name);
        free(files[i].full_path);
    }
    free(files);
}
