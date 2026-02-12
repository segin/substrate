#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "tar.h"

struct tar_options opt;

void test_checksum(void) {
    struct ustar_header h;
    memset(&h, 0, sizeof(h));
    strcpy(h.name, "test");
    update_checksum(&h);
    assert(verify_header(&h));
    printf("test_checksum passed\n");
}

void test_octal(void) {
    char buf[12];
    format_octal(0755, buf, 8);
    assert(strcmp(buf, "0000755") == 0);
    int64_t val = parse_octal(buf, 8);
    assert(val == 0755);
    printf("test_octal passed\n");
}

void test_list(void) {
    if (system("echo 'Hello World' > test_file.txt") != 0) return;
    if (system("tar --format=ustar -cf test_archive.tar test_file.txt") != 0) {
        system("tar -cf test_archive.tar test_file.txt");
    }

    printf("Testing list of test_archive.tar...\n");
    opt.mode = MODE_LIST;
    opt.file = "test_archive.tar";
    opt.verbose = true;
    opt.format = NULL;

    int ret = tar_list(0, NULL);
    if (ret != 0) {
        printf("tar_list failed\n");
        exit(1);
    }

    unlink("test_file.txt");
    unlink("test_archive.tar");
    printf("test_list passed\n");
}

void test_create(void) {
    system("rm -rf test_dir");
    mkdir("test_dir", 0755);
    system("echo 'File A' > test_dir/a");
    mkdir("test_dir/b", 0755);
    system("echo 'File B' > test_dir/b/c");

    printf("Testing create...\n");
    opt.mode = MODE_CREATE;
    opt.file = "test_create.tar";
    opt.verbose = true;
    opt.format = NULL;

    char *args[] = { "test_dir" };
    int ret = tar_create(1, args);
    if (ret != 0) {
        printf("tar_create failed\n");
        exit(1);
    }

    if (system("tar -tf test_create.tar > list.out") != 0) {
        printf("Host tar failed to list created archive\n");
        exit(1);
    }

    system("grep 'test_dir/a' list.out > /dev/null || (echo 'Missing file a'; exit 1)");

    system("rm -rf test_dir test_create.tar list.out");
    printf("test_create passed\n");
}

void test_pax(void) {
    printf("Testing PAX create...\n");
    system("rm -rf test_pax_dir");
    mkdir("test_pax_dir", 0755);
    system("echo 'PAX' > test_pax_dir/file");

    opt.mode = MODE_CREATE;
    opt.file = "test_pax.tar";
    opt.verbose = true;
    opt.format = "pax";

    char *args[] = { "test_pax_dir" };
    int ret = tar_create(1, args);
    if (ret != 0) {
        printf("tar_create failed\n");
        exit(1);
    }

    if (system("tar -tf test_pax.tar > pax_list.out") != 0) {
        printf("Host tar failed to read PAX archive\n");
        exit(1);
    }
    system("grep 'test_pax_dir/file' pax_list.out > /dev/null || (echo 'Missing file in PAX archive'; exit 1)");

    system("rm -rf test_pax_dir test_pax.tar pax_list.out");
    printf("test_pax passed\n");
}

void test_extract(void) {
    printf("Testing extract...\n");

    /* Create archive with content */
    system("rm -rf extract_src extract_dest extract.tar");
    mkdir("extract_src", 0755);
    system("echo 'Extract Me' > extract_src/file");
    mkdir("extract_src/sub", 0755);
    system("echo 'Subfile' > extract_src/sub/file2");

    system("tar -cf extract.tar extract_src");

    /* Extract to new location */
    mkdir("extract_dest", 0755);
    chdir("extract_dest");

    opt.mode = MODE_EXTRACT;
    opt.file = "../extract.tar";
    opt.verbose = true;
    opt.safe_extract = true;

    int ret = tar_extract(0, NULL);
    if (ret != 0) {
        printf("tar_extract failed\n");
        exit(1);
    }

    if (access("extract_src/file", F_OK) != 0) {
        printf("Failed to extract file\n");
        exit(1);
    }
    if (access("extract_src/sub/file2", F_OK) != 0) {
        printf("Failed to extract subfile\n");
        exit(1);
    }

    chdir("..");
    system("rm -rf extract_src extract_dest extract.tar");
    printf("test_extract passed\n");
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Running tar tests...\n");

    setprogname("test_tar");

    test_checksum();
    test_octal();
    test_list();
    test_create();
    test_pax();
    test_extract();

    printf("All tests passed!\n");
    return 0;
}
