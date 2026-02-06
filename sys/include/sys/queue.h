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

/*
 * List definitions.
 */
#define LIST_HEAD(name, type)                       \
struct name {                                       \
    struct type *lh_first;  /* first element */     \
}

#define LIST_HEAD_INITIALIZER(head)                 \
    { NULL }

#define LIST_ENTRY(type)                            \
struct {                                            \
    struct type *le_next;   /* next element */      \
    struct type **le_prev;  /* address of previous next element */ \
}

/*
 * List functions.
 */
#define LIST_INIT(head) do {                        \
    (head)->lh_first = NULL;                        \
} while (0)

#define LIST_INSERT_AFTER(listelm, elm, field) do { \
    if (((elm)->field.le_next = (listelm)->field.le_next) != NULL) \
        (listelm)->field.le_next->field.le_prev =   \
            &(elm)->field.le_next;                  \
    (listelm)->field.le_next = (elm);               \
    (elm)->field.le_prev = &(listelm)->field.le_next; \
} while (0)

#define LIST_INSERT_BEFORE(listelm, elm, field) do { \
    (elm)->field.le_prev = (listelm)->field.le_prev; \
    (elm)->field.le_next = (listelm);               \
    *(listelm)->field.le_prev = (elm);              \
    (listelm)->field.le_prev = &(elm)->field.le_next; \
} while (0)

#define LIST_INSERT_HEAD(head, elm, field) do {     \
    if (((elm)->field.le_next = (head)->lh_first) != NULL) \
        (head)->lh_first->field.le_prev = &(elm)->field.le_next; \
    (head)->lh_first = (elm);                       \
    (elm)->field.le_prev = &(head)->lh_first;       \
} while (0)

#define LIST_REMOVE(elm, field) do {                \
    if ((elm)->field.le_next != NULL)               \
        (elm)->field.le_next->field.le_prev =       \
            (elm)->field.le_prev;                   \
    *(elm)->field.le_prev = (elm)->field.le_next;   \
} while (0)

#define LIST_FOREACH(var, head, field)              \
    for ((var) = ((head)->lh_first);                \
        (var);                                      \
        (var) = ((var)->field.le_next))

#define LIST_FOREACH_SAFE(var, head, field, tvar)   \
    for ((var) = ((head)->lh_first);                \
        (var) && ((tvar) = ((var)->field.le_next), 1); \
        (var) = (tvar))

#endif /* !_SYS_QUEUE_H */
