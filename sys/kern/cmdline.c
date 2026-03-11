#include <kern/cmdline.h>
#include <string.h>
#include <kern/console.h>

#ifdef HOST_TEST
__attribute__((weak)) void kprint(const char *msg) {
    (void)msg;
}
#endif

static char kernel_cmdline[1024];
static int initialized = 0;

static const char *cmdline_skip_spaces(const char *p) {
    while (p && *p == ' ') {
        p++;
    }
    return p;
}

static const char *cmdline_next_token(const char *p,
                                      const char **token_out,
                                      size_t *token_len_out) {
    const char *start;
    const char *end;

    if (token_out) {
        *token_out = NULL;
    }
    if (token_len_out) {
        *token_len_out = 0;
    }
    if (!p) {
        return NULL;
    }

    start = cmdline_skip_spaces(p);
    if (!start || *start == '\0') {
        return NULL;
    }

    end = start;
    while (*end != '\0' && *end != ' ') {
        end++;
    }

    if (token_out) {
        *token_out = start;
    }
    if (token_len_out) {
        *token_len_out = (size_t)(end - start);
    }

    return end;
}

static int cmdline_token_matches_key(const char *token, size_t token_len,
                                     const char *key, size_t key_len,
                                     const char **value_out,
                                     size_t *value_len_out) {
    if (value_out) {
        *value_out = NULL;
    }
    if (value_len_out) {
        *value_len_out = 0;
    }
    if (!token || !key || token_len < key_len) {
        return 0;
    }
    if (memcmp(token, key, key_len) != 0) {
        return 0;
    }
    if (token_len == key_len) {
        return 1;
    }
    if (token[key_len] != '=') {
        return 0;
    }

    if (value_out) {
        *value_out = token + key_len + 1;
    }
    if (value_len_out) {
        *value_len_out = token_len - key_len - 1;
    }
    return 1;
}

static int cmdline_debug_token_matches(const char *item, size_t item_len,
                                       const char *channel, size_t channel_len) {
    if (!item || !channel || channel_len == 0) {
        return 0;
    }
    if (item_len == 0) {
        return 1;
    }
    if ((item_len == 1 && item[0] == '*') ||
        (item_len == 3 && memcmp(item, "all", 3) == 0)) {
        return 1;
    }
    if (item_len == channel_len && memcmp(item, channel, channel_len) == 0) {
        return 1;
    }
    if (item_len < channel_len &&
        memcmp(item, channel, item_len) == 0 &&
        channel[item_len] == ':') {
        return 1;
    }
    return 0;
}

void cmdline_init(const char *cmdline) {
    if (!cmdline) {
        kernel_cmdline[0] = 0;
        initialized = 1;
        return;
    }

    strncpy(kernel_cmdline, cmdline, sizeof(kernel_cmdline));
    kernel_cmdline[sizeof(kernel_cmdline) - 1] = 0;
    initialized = 1;

    kprint("Kernel Command Line: ");
    kprint(kernel_cmdline);
    kprint("\n");
}

int cmdline_has(const char *key) {
    const char *cursor;
    const char *token;
    size_t token_len;
    size_t key_len;

    if (!initialized || !key || !*key) return 0;

    key_len = strlen(key);
    cursor = kernel_cmdline;
    while ((cursor = cmdline_next_token(cursor, &token, &token_len)) != NULL) {
        if (cmdline_token_matches_key(token, token_len, key, key_len, NULL, NULL)) {
            return 1;
        }
    }
    return 0;
}

int cmdline_get(const char *key, char *buf, size_t buf_len) {
    const char *cursor;
    const char *token;
    const char *value;
    size_t token_len;
    size_t value_len;
    size_t key_len;

    if (!initialized || !buf || buf_len == 0 || !key || !*key) return -1;

    key_len = strlen(key);
    cursor = kernel_cmdline;
    while ((cursor = cmdline_next_token(cursor, &token, &token_len)) != NULL) {
        if (!cmdline_token_matches_key(token, token_len, key, key_len, &value, &value_len)) {
            continue;
        }
        if (!value) {
            return -1;
        }
        if (value_len >= buf_len) value_len = buf_len - 1;
        memcpy(buf, value, value_len);
        buf[value_len] = 0;
        return 0;
    }

    return -1;
}

int cmdline_debug_enabled(const char *channel) {
    const char *cursor;
    const char *token;
    const char *value;
    size_t token_len;
    size_t value_len;
    size_t channel_len;

    if (!initialized || !channel || !*channel) {
        return 0;
    }

    channel_len = strlen(channel);
    cursor = kernel_cmdline;
    while ((cursor = cmdline_next_token(cursor, &token, &token_len)) != NULL) {
        const char *item;
        const char *item_end;

        if (!cmdline_token_matches_key(token, token_len, "debug", 5, &value, &value_len)) {
            continue;
        }
        if (!value) {
            return 1;
        }

        item = value;
        item_end = value + value_len;
        while (item < item_end) {
            const char *next = item;

            while (next < item_end && *next != ',') {
                next++;
            }
            if (cmdline_debug_token_matches(item, (size_t)(next - item),
                                            channel, channel_len)) {
                return 1;
            }
            item = next;
            if (item < item_end && *item == ',') {
                item++;
            }
        }
    }

    return 0;
}

int cmdline_get_full(char *buf, size_t buf_len) {
    if (!initialized || !buf || buf_len == 0) return -1;
    strncpy(buf, kernel_cmdline, buf_len);
    buf[buf_len - 1] = 0;
    return 0;
}
