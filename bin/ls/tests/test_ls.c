#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../ls.h"
#include "../ls_opts.h"
#include "../ls_traverse.h"

// Helper to capture stdout
int saved_stdout;
char capture_buf[8192];

void start_capture(const char *tmpfile) {
    fflush(stdout);
    saved_stdout = dup(STDOUT_FILENO);
    int fd = open(tmpfile, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    dup2(fd, STDOUT_FILENO);
    close(fd);
}

void end_capture(const char *tmpfile) {
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    int fd = open(tmpfile, O_RDONLY);
    memset(capture_buf, 0, sizeof(capture_buf));
    int len = read(fd, capture_buf, sizeof(capture_buf)-1);
    if (len >= 0) capture_buf[len] = 0;
    close(fd);
    unlink(tmpfile);
}

void create_file(const char *path, const char *content, time_t mtime) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
    if (mtime > 0) {
        struct utimbuf { time_t actime; time_t modtime; } times;
        times.actime = mtime;
        times.modtime = mtime;
        utime(path, &times);
    }
}

void test_sorting(void) {
    mkdir("test_ls_sort", 0755);
    create_file("test_ls_sort/a.txt", "123", 1000); // size 3, time 1000
    create_file("test_ls_sort/b.txt", "1", 2000);   // size 1, time 2000
    create_file("test_ls_sort/c.txt", "12", 500);   // size 2, time 500

    ls_config_t config;
    memset(&config, 0, sizeof(config));
    config.color = 0;

    // Default sort (alpha): a.txt, b.txt, c.txt
    start_capture("out.tmp");
    ls_list_dir("test_ls_sort", &config);
    end_capture("out.tmp");
    
    char *pA = strstr(capture_buf, "a.txt");
    char *pB = strstr(capture_buf, "b.txt");
    char *pC = strstr(capture_buf, "c.txt");
    assert(pA && pB && pC);
    assert(pA < pB && pB < pC);
    
    // Size sort (-S): a.txt (3), c.txt (2), b.txt (1)
    config.sort_size = true;
    start_capture("out.tmp");
    ls_list_dir("test_ls_sort", &config);
    end_capture("out.tmp");
    pA = strstr(capture_buf, "a.txt");
    pB = strstr(capture_buf, "b.txt");
    pC = strstr(capture_buf, "c.txt");
    assert(pA < pC && pC < pB);
    config.sort_size = false;

    // Time sort (-t): b (2000), a (1000), c (500)
    config.sort_time = true;
    start_capture("out.tmp");
    ls_list_dir("test_ls_sort", &config);
    end_capture("out.tmp");
    pA = strstr(capture_buf, "a.txt");
    pB = strstr(capture_buf, "b.txt");
    pC = strstr(capture_buf, "c.txt");
    assert(pB < pA && pA < pC);
    config.sort_time = false;
    
    // Reverse (-r) + alpha: c, b, a
    config.reverse = true;
    start_capture("out.tmp");
    ls_list_dir("test_ls_sort", &config);
    end_capture("out.tmp");
    pA = strstr(capture_buf, "a.txt");
    pB = strstr(capture_buf, "b.txt");
    pC = strstr(capture_buf, "c.txt");
    assert(pC < pB && pB < pA);

    unlink("test_ls_sort/a.txt");
    unlink("test_ls_sort/b.txt");
    unlink("test_ls_sort/c.txt");
    rmdir("test_ls_sort");
    printf("PASS: test_sorting\n");
}

void test_flags(void) {
    mkdir("test_ls_flags", 0755);
    create_file("test_ls_flags/visible", "", 0);
    create_file("test_ls_flags/.hidden", "", 0);
    
    ls_config_t config;
    memset(&config, 0, sizeof(config));

    // Basic: only visible
    start_capture("out.tmp");
    ls_list_dir("test_ls_flags", &config);
    end_capture("out.tmp");
    assert(strstr(capture_buf, "visible"));
    assert(!strstr(capture_buf, ".hidden"));
    
    // -a: all
    config.all = true;
    start_capture("out.tmp");
    ls_list_dir("test_ls_flags", &config);
    end_capture("out.tmp");
    assert(strstr(capture_buf, "visible"));
    assert(strstr(capture_buf, ".hidden"));
    assert(strstr(capture_buf, "."));
    assert(strstr(capture_buf, ".."));
    
    unlink("test_ls_flags/visible");
    unlink("test_ls_flags/.hidden");
    rmdir("test_ls_flags");
    printf("PASS: test_flags\n");
}

void test_new_flags(void) {
    mkdir("test_ls_new", 0755);
    create_file("test_ls_new/file", "data", 0);
    mkdir("test_ls_new/subdir", 0755);

    ls_config_t config;
    
    // -F: classify
    memset(&config, 0, sizeof(config));
    config.classify = true;
    start_capture("out.tmp");
    ls_list_dir("test_ls_new", &config);
    end_capture("out.tmp");
    assert(strstr(capture_buf, "subdir/"));
    assert(strstr(capture_buf, "file"));

    // -i: inode
    memset(&config, 0, sizeof(config));
    config.inode = true;
    start_capture("out.tmp");
    ls_list_dir("test_ls_new", &config);
    end_capture("out.tmp");
    // Should have numbers at start of lines
    assert(capture_buf[0] >= '0' && capture_buf[0] <= '9');

    // -n: numeric IDs (implies -l)
    memset(&config, 0, sizeof(config));
    config.numeric_ids = true;
    config.long_fmt = true;
    start_capture("out.tmp");
    ls_list_dir("test_ls_new", &config);
    end_capture("out.tmp");
    // Logic for -n check: look for numeric uid/gid
    // Simplified: just ensure it runs for now
    assert(strstr(capture_buf, "subdir"));

    // -d: directory itself
    memset(&config, 0, sizeof(config));
    config.directory = true;
    start_capture("out.tmp");
    ls_list_dir("test_ls_new", &config);
    end_capture("out.tmp");
    assert(strcmp(capture_buf, "test_ls_new\n") == 0 || strcmp(capture_buf, "test_ls_new  \n") == 0);

    unlink("test_ls_new/file");
    rmdir("test_ls_new/subdir");
    rmdir("test_ls_new");
    printf("PASS: test_new_flags\n");
}

int main(void) {
    test_sorting();
    test_flags();
    test_new_flags();
    return 0;
}
