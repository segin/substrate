#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <histedit.h>
#include <unistd.h>

struct hist_node {
    char *str;
    int num;            /* monotonically increasing event number */
    time_t timestamp;
    struct hist_node *next;
    struct hist_node *prev;
};

struct history {
    struct hist_node *head;
    struct hist_node *tail;
    struct hist_node *curr;
    int size;
    int max_size;
    int next_num;       /* next event number to assign */
    int unique;         /* 0=off, 1=consecutive, 2=all duplicates */
};

History *history_init(void) {
    History *h = calloc(1, sizeof(History));
    if (!h) return NULL;
    h->max_size = 100;
    h->next_num = 1;
    return h;
}

static void hist_remove_node(History *h, struct hist_node *node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        h->head = node->next;
    if (node->next)
        node->next->prev = node->prev;
    else
        h->tail = node->prev;
    if (h->curr == node)
        h->curr = NULL;
    free(node->str);
    free(node);
    h->size--;
}

void history_end(History *h) {
    struct hist_node *node, *next;
    if (!h) return;
    node = h->head;
    while (node) {
        next = node->next;
        free(node->str);
        free(node);
        node = next;
    }
    free(h);
}

static void hist_set_event(HistEvent *ev, struct hist_node *node)
{
    if (ev && node) {
        ev->num = node->num;
        ev->str = node->str;
    }
}

int history(History *h, HistEvent *ev, int op, ...) {
    va_list ap;
    va_start(ap, op);

    if (ev) {
        ev->num = 0;
        ev->str = NULL;
    }

    switch (op) {
    case H_SETSIZE:
        h->max_size = va_arg(ap, int);
        /* Evict oldest entries if we're over the new limit */
        while (h->size > h->max_size && h->head)
            hist_remove_node(h, h->head);
        break;

    case H_GETSIZE:
        if (ev) ev->num = h->size;
        break;

    case H_ENTER: {
        const char *str = va_arg(ap, const char *);
        struct hist_node *node;
        if (!str || !*str) break;

        /* Consecutive duplicate suppression */
        if (h->unique >= 1 && h->tail && strcmp(h->tail->str, str) == 0)
            break;

        /* All-duplicates mode: remove any prior entry with same text */
        if (h->unique >= 2) {
            struct hist_node *n = h->head;
            while (n) {
                struct hist_node *nx = n->next;
                if (strcmp(n->str, str) == 0)
                    hist_remove_node(h, n);
                n = nx;
            }
        }

        node = malloc(sizeof(struct hist_node));
        if (!node) break;
        node->str = strdup(str);
        if (!node->str) { free(node); break; }
        node->num = h->next_num++;
        node->timestamp = 0; /* caller can set via timestamp support */
        node->next = NULL;
        node->prev = h->tail;

        if (h->tail) h->tail->next = node;
        h->tail = node;
        if (!h->head) h->head = node;

        h->size++;
        while (h->size > h->max_size && h->head)
            hist_remove_node(h, h->head);

        h->curr = NULL;
        if (ev) hist_set_event(ev, node);
        break;
    }

    case H_FIRST:
        h->curr = h->head;
        if (h->curr) hist_set_event(ev, h->curr);
        break;

    case H_LAST:
        h->curr = h->tail;
        if (h->curr) hist_set_event(ev, h->curr);
        break;

    case H_PREV:
        if (!h->curr)
            h->curr = h->tail;
        else if (h->curr->prev)
            h->curr = h->curr->prev;
        if (h->curr) hist_set_event(ev, h->curr);
        break;

    case H_NEXT:
        if (h->curr)
            h->curr = h->curr->next;
        if (h->curr)
            hist_set_event(ev, h->curr);
        else if (ev)
            ev->str = NULL;
        break;

    case H_CURR:
        if (h->curr)
            hist_set_event(ev, h->curr);
        else {
            va_end(ap);
            return -1;
        }
        break;

    case H_SET: {
        int target = va_arg(ap, int);
        struct hist_node *n;
        for (n = h->head; n; n = n->next) {
            if (n->num == target) {
                h->curr = n;
                hist_set_event(ev, n);
                break;
            }
        }
        if (!n) { va_end(ap); return -1; }
        break;
    }

    case H_ADD: {
        const char *str = va_arg(ap, const char *);
        char *ns;
        size_t ol, nl;
        if (!h->curr || !str) { va_end(ap); return -1; }
        ol = strlen(h->curr->str);
        nl = strlen(str);
        ns = realloc(h->curr->str, ol + nl + 1);
        if (!ns) { va_end(ap); return -1; }
        memcpy(ns + ol, str, nl + 1);
        h->curr->str = ns;
        hist_set_event(ev, h->curr);
        break;
    }

    case H_APPEND: {
        const char *str = va_arg(ap, const char *);
        char *ns;
        size_t ol, nl;
        if (!h->tail || !str) { va_end(ap); return -1; }
        ol = strlen(h->tail->str);
        nl = strlen(str);
        ns = realloc(h->tail->str, ol + nl + 1);
        if (!ns) { va_end(ap); return -1; }
        memcpy(ns + ol, str, nl + 1);
        h->tail->str = ns;
        hist_set_event(ev, h->tail);
        break;
    }

    case H_END:
        h->curr = NULL;
        if (ev) { ev->num = 0; ev->str = NULL; }
        break;

    case H_NEXT_STR: {
        const char *str = va_arg(ap, const char *);
        struct hist_node *n;
        if (!str) { va_end(ap); return -1; }
        n = h->curr ? h->curr->next : h->head;
        for (; n; n = n->next) {
            if (strstr(n->str, str)) {
                h->curr = n;
                hist_set_event(ev, n);
                break;
            }
        }
        if (!n) { va_end(ap); return -1; }
        break;
    }

    case H_PREV_STR: {
        const char *str = va_arg(ap, const char *);
        struct hist_node *n;
        if (!str) { va_end(ap); return -1; }
        n = h->curr ? h->curr->prev : h->tail;
        for (; n; n = n->prev) {
            if (strstr(n->str, str)) {
                h->curr = n;
                hist_set_event(ev, n);
                break;
            }
        }
        if (!n) { va_end(ap); return -1; }
        break;
    }

    case H_NEXT_EVENT: {
        int target = va_arg(ap, int);
        struct hist_node *n;
        n = h->curr ? h->curr->next : h->head;
        for (; n; n = n->next) {
            if (n->num == target) {
                h->curr = n;
                hist_set_event(ev, n);
                break;
            }
        }
        if (!n) { va_end(ap); return -1; }
        break;
    }

    case H_PREV_EVENT: {
        int target = va_arg(ap, int);
        struct hist_node *n;
        n = h->curr ? h->curr->prev : h->tail;
        for (; n; n = n->prev) {
            if (n->num == target) {
                h->curr = n;
                hist_set_event(ev, n);
                break;
            }
        }
        if (!n) { va_end(ap); return -1; }
        break;
    }

    case H_LOAD: {
        const char *path = va_arg(ap, const char *);
        FILE *fp;
        char buf[4096];
        if (!path) { va_end(ap); return -1; }
        fp = fopen(path, "r");
        if (!fp) { va_end(ap); return -1; }
        while (fgets(buf, (int)sizeof(buf), fp)) {
            size_t len = strlen(buf);
            /* Unescape \\n back to newlines */
            char *dst = buf, *src = buf;
            while (*src) {
                if (src[0] == '\\' && src[1] == 'n') {
                    *dst++ = '\n';
                    src += 2;
                } else {
                    *dst++ = *src++;
                }
            }
            *dst = '\0';
            len = (size_t)(dst - buf);
            /* Strip trailing newline from file line */
            if (len > 0 && buf[len - 1] == '\n')
                buf[--len] = '\0';
            if (len > 0) {
                HistEvent tmp;
                va_end(ap);
                history(h, &tmp, H_ENTER, buf);
                va_start(ap, op);
            }
        }
        fclose(fp);
        break;
    }

    case H_SAVE: {
        const char *path = va_arg(ap, const char *);
        char tmppath[1024];
        FILE *fp;
        struct hist_node *n;
        int len;
        if (!path) { va_end(ap); return -1; }
        /* Write atomically via tmp + rename */
        len = snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);
        if (len < 0 || (size_t)len >= sizeof(tmppath)) {
            va_end(ap);
            return -1;
        }
        fp = fopen(tmppath, "w");
        if (!fp) { va_end(ap); return -1; }
        for (n = h->head; n; n = n->next) {
            const char *s = n->str;
            /* Escape embedded newlines as \\n */
            while (*s) {
                if (*s == '\n') {
                    fputc('\\', fp);
                    fputc('n', fp);
                } else {
                    fputc(*s, fp);
                }
                s++;
            }
            fputc('\n', fp);
        }
        if (fclose(fp) != 0) {
            unlink(tmppath);
            va_end(ap);
            return -1;
        }
        if (rename(tmppath, path) != 0) {
            unlink(tmppath);
            va_end(ap);
            return -1;
        }
        break;
    }

    case H_CLEAR: {
        struct hist_node *n = h->head;
        while (n) {
            struct hist_node *nx = n->next;
            free(n->str);
            free(n);
            n = nx;
        }
        h->head = h->tail = h->curr = NULL;
        h->size = 0;
        break;
    }

    case H_SETUNIQUE:
        h->unique = va_arg(ap, int);
        break;

    default:
        va_end(ap);
        return -1;
    }

    va_end(ap);
    return 0;
}
