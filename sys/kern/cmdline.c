#include <kern/cmdline.h>
#include <string.h>
#include <kern/console.h>

static char kernel_cmdline[1024];
static int initialized = 0;

void cmdline_init(const char *cmdline) {
    if (!cmdline) {
        kernel_cmdline[0] = 0;
        return;
    }
    
    // Copy safely
    size_t i = 0;
    while (cmdline[i] && i < sizeof(kernel_cmdline) - 1) {
        kernel_cmdline[i] = cmdline[i];
        i++;
    }
    kernel_cmdline[i] = 0;
    initialized = 1;
    
    // Print it
    kprint("Kernel Command Line: ");
    kprint(kernel_cmdline);
    kprint("\n");
}

// Check for boolean flag e.g. "serial_debug" or "quiet"
int cmdline_has(const char *key) {
    if (!initialized) return 0;
    if (!key || !*key) return 0;
    
    char *p = strstr(kernel_cmdline, key);
    if (!p) return 0;
    
    // Check boundaries to ensure we matched a whole word
    // Start boundary: p == kernel_cmdline OR *(p-1) == ' '
    if (p != kernel_cmdline && *(p-1) != ' ') return 0;
    
    // End boundary: end of key matches end of string OR followed by ' ' or '='
    size_t key_len = strlen(key);
    char next = p[key_len];
    if (next == 0 || next == ' ' || next == '=') return 1;
    
    return 0;
}

// Get value for key=value
int cmdline_get(const char *key, char *buf, size_t buf_len) {
    if (!initialized || !buf || buf_len == 0) return -1;
    
    char *p = strstr(kernel_cmdline, key);
    if (!p) return -1;
    
    // Check start boundary
    if (p != kernel_cmdline && *(p-1) != ' ') return -1;
    
    size_t key_len = strlen(key);
    // Must be followed by '='
    if (p[key_len] != '=') return -1;
    
    char *val_start = p + key_len + 1;
    char *val_end = val_start;
    
    while (*val_end && *val_end != ' ') {
        val_end++;
    }
    
    size_t val_len = val_end - val_start;
    if (val_len >= buf_len) val_len = buf_len - 1;
    
    memcpy(buf, val_start, val_len);
    buf[val_len] = 0;
    
    return 0;
}
int cmdline_get_full(char *buf, size_t buf_len) {
    if (!initialized || !buf || buf_len == 0) return -1;
    strncpy(buf, kernel_cmdline, buf_len);
    buf[buf_len - 1] = 0;
    return 0;
}
