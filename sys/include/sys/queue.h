#ifndef _SYS_QUEUE_H
#define _SYS_QUEUE_H

/*
 * Tail queue definitions.
 */
#define TAILQ_HEAD(name, type)                      \
struct name {                                       \
    struct type *tqh_first; /* first element */     \
    struct type **tqh_last; /* addr of last next */ \
}

#define TAILQ_HEAD_INITIALIZER(head)                \
    { NULL, &(head).tqh_first }

#define TAILQ_ENTRY(type)                           \
struct {                                            \
    struct type *tqe_next;  /* next element */      \
    struct type **tqe_prev; /* address of previous next element */ \
}

/*
 * Tail queue functions.
 */
#define TAILQ_INIT(head) do {                       \
    (head)->tqh_first = NULL;                       \
    (head)->tqh_last = &(head)->tqh_first;          \
} while (0)

#define TAILQ_INSERT_HEAD(head, elm, field) do {    \
    if (((elm)->field.tqe_next = (head)->tqh_first) != NULL) \
        (head)->tqh_first->field.tqe_prev =         \
            &(elm)->field.tqe_next;                 \
    else                                            \
        (head)->tqh_last = &(elm)->field.tqe_next;  \
    (head)->tqh_first = (elm);                      \
    (elm)->field.tqe_prev = &(head)->tqh_first;     \
} while (0)

#define TAILQ_INSERT_TAIL(head, elm, field) do {    \
    (elm)->field.tqe_next = NULL;                   \
    (elm)->field.tqe_prev = (head)->tqh_last;       \
    *(head)->tqh_last = (elm);                      \
    (head)->tqh_last = &(elm)->field.tqe_next;      \
} while (0)

#define TAILQ_INSERT_AFTER(head, listelm, elm, field) do { \
    if (((elm)->field.tqe_next = (listelm)->field.tqe_next) != NULL) \
        (elm)->field.tqe_next->field.tqe_prev =     \
            &(elm)->field.tqe_next;                 \
    else                                            \
        (head)->tqh_last = &(elm)->field.tqe_next;  \
    (listelm)->field.tqe_next = (elm);              \
    (elm)->field.tqe_prev = &(listelm)->field.tqe_next; \
} while (0)

#define TAILQ_INSERT_BEFORE(listelm, elm, field) do { \
    (elm)->field.tqe_prev = (listelm)->field.tqe_prev; \
    (elm)->field.tqe_next = (listelm);              \
    *(listelm)->field.tqe_prev = (elm);             \
    (listelm)->field.tqe_prev = &(elm)->field.tqe_next; \
} while (0)

#define TAILQ_REMOVE(head, elm, field) do {         \
    if (((elm)->field.tqe_next) != NULL)            \
        (elm)->field.tqe_next->field.tqe_prev =     \
            (elm)->field.tqe_prev;                  \
    else                                            \
        (head)->tqh_last = (elm)->field.tqe_prev;   \
    *(elm)->field.tqe_prev = (elm)->field.tqe_next; \
} while (0)

#define TAILQ_FOREACH(var, head, field)             \
    for ((var) = ((head)->tqh_first);               \
        (var);                                      \
        (var) = ((var)->field.tqe_next))

#define TAILQ_FOREACH_SAFE(var, head, field, tvar)  \
    for ((var) = ((head)->tqh_first);               \
        (var) && ((tvar) = ((var)->field.tqe_next), 1); \
        (var) = (tvar))

#define TAILQ_EMPTY(head) ((head)->tqh_first == NULL)

#define TAILQ_FIRST(head) ((head)->tqh_first)

#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)

#define TAILQ_LAST(head, headname) \
    (*(((struct headname *)((head)->tqh_last))->tqh_last))

#endif /* !_SYS_QUEUE_H */
