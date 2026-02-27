#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <histedit.h>

struct hist_node {
    char *str;
    struct hist_node *next;
    struct hist_node *prev;
};

struct history {
    struct hist_node *head;
    struct hist_node *tail;
    struct hist_node *curr;
    int size;
    int max_size;
};

History *history_init(void) {
    History *h = calloc(1, sizeof(History));
    if (!h) return NULL;
    h->max_size = 100;
    return h;
}

void history_end(History *h) {
    if (!h) return;
    struct hist_node *node = h->head;
    while (node) {
        struct hist_node *next = node->next;
        free(node->str);
        free(node);
        node = next;
    }
    free(h);
}

int history(History *h, HistEvent *ev, int op, ...) {
    va_list ap;
    va_start(ap, op);

    switch (op) {
    case H_SETSIZE:
        h->max_size = va_arg(ap, int);
        break;
    case H_ENTER: {
        const char *str = va_arg(ap, const char *);
        if (!str || !*str) break;

        /* Don't add if same as last */
        if (h->tail && strcmp(h->tail->str, str) == 0) break;

        struct hist_node *node = malloc(sizeof(struct hist_node));
        if (!node) break;
        node->str = strdup(str);
        node->next = NULL;
        node->prev = h->tail;

        if (h->tail) h->tail->next = node;
        h->tail = node;
        if (!h->head) h->head = node;

        h->size++;
        if (h->size > h->max_size) {
            struct hist_node *old = h->head;
            h->head = old->next;
            if (h->head) h->head->prev = NULL;
            free(old->str);
            free(old);
            h->size--;
        }
        h->curr = NULL; /* Reset traversal */
        break;
    }
    case H_FIRST:
        h->curr = h->head;
        if (ev && h->curr) ev->str = h->curr->str;
        break;
    case H_LAST:
        h->curr = h->tail;
        if (ev && h->curr) ev->str = h->curr->str;
        break;
    case H_PREV:
        if (!h->curr) h->curr = h->tail;
        else if (h->curr->prev) h->curr = h->curr->prev;
        if (ev && h->curr) ev->str = h->curr->str;
        break;
    case H_NEXT:
        if (h->curr) h->curr = h->curr->next;
        if (ev && h->curr) ev->str = h->curr->str;
        else if (ev) ev->str = NULL;
        break;
    default:
        va_end(ap);
        return -1;
    }

    va_end(ap);
    return 0;
}
