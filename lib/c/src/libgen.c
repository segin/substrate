#include <libgen.h>
#include <string.h>
#include <stddef.h>

char *basename(char *path) {
    static char dot[] = ".";
    static char slash[] = "/";

    if (path == NULL || *path == '\0') {
        return dot;
    }

    // Find end of string
    size_t len = strlen(path);
    char *end = path + len - 1;

    // Remove trailing slashes
    while (end >= path && *end == '/') {
        end--;
    }

    if (end < path) {
        // String consists entirely of slash characters
        return slash;
    }

    // Null terminate after the last component
    *(end + 1) = '\0';

    // Find the last slash
    while (end >= path && *end != '/') {
        end--;
    }

    if (end < path) {
        // No slashes found
        return path;
    }

    // Return pointer after the slash
    return end + 1;
}

char *dirname(char *path) {
    static char dot[] = ".";
    static char slash[] = "/";

    if (path == NULL || *path == '\0') {
        return dot;
    }

    size_t len = strlen(path);
    char *end = path + len - 1;

    // Remove trailing slashes
    while (end >= path && *end == '/') {
        end--;
    }

    if (end < path) {
        // String consists entirely of slash characters
        return slash;
    }

    // Remove trailing component
    while (end >= path && *end != '/') {
        end--;
    }

    if (end < path) {
        // No slashes found in the component-stripped string
        // meaning the path was just a filename
        return dot;
    }

    // Remove trailing slashes from the directory part
    while (end >= path && *end == '/') {
        end--;
    }

    if (end < path) {
        // The remaining string was just slashes (e.g. /usr)
        return slash;
    }

    *(end + 1) = '\0';
    return path;
}
