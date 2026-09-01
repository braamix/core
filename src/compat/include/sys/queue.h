// <sys/queue.h>. FreeBSD's four list flavours, less the debugging hooks and
// CIRCLEQ, which the BSDs themselves dropped.
//
// TAILQ_LAST and TAILQ_PREV take the head's struct tag, not the head: that is
// upstream's trick for reaching a tail pointer through a last-element pointer,
// and it is why TAILQ_HEAD's first argument is worth naming.
#pragma once

#include <stddef.h>

// --------------------------------------------------------------- SLIST

#define SLIST_HEAD(name, type)                                                                \
    struct name {                                                                             \
        struct type *slh_first;                                                               \
    }
#define SLIST_HEAD_INITIALIZER(head) { NULL }
#define SLIST_ENTRY(type)                                                                     \
    struct {                                                                                  \
        struct type *sle_next;                                                                \
    }

#define SLIST_INIT(head)        ((head)->slh_first = NULL)
#define SLIST_FIRST(head)       ((head)->slh_first)
#define SLIST_EMPTY(head)       ((head)->slh_first == NULL)
#define SLIST_NEXT(elm, field)  ((elm)->field.sle_next)

#define SLIST_FOREACH(var, head, field)                                                       \
    for ((var) = SLIST_FIRST(head); (var); (var) = SLIST_NEXT(var, field))
#define SLIST_FOREACH_SAFE(var, head, field, tvar)                                            \
    for ((var) = SLIST_FIRST(head); (var) && ((tvar) = SLIST_NEXT(var, field), 1);            \
         (var) = (tvar))

#define SLIST_INSERT_HEAD(head, elm, field)                                                   \
    do {                                                                                      \
        SLIST_NEXT(elm, field) = SLIST_FIRST(head);                                           \
        SLIST_FIRST(head)      = (elm);                                                       \
    } while (0)
#define SLIST_INSERT_AFTER(slistelm, elm, field)                                              \
    do {                                                                                      \
        SLIST_NEXT(elm, field)       = SLIST_NEXT(slistelm, field);                           \
        SLIST_NEXT(slistelm, field)  = (elm);                                                 \
    } while (0)
#define SLIST_REMOVE_HEAD(head, field)                                                        \
    (SLIST_FIRST(head) = SLIST_NEXT(SLIST_FIRST(head), field))
#define SLIST_REMOVE(head, elm, type, field)                                                  \
    do {                                                                                      \
        if (SLIST_FIRST(head) == (elm)) {                                                     \
            SLIST_REMOVE_HEAD(head, field);                                                   \
        } else {                                                                              \
            struct type *_c = SLIST_FIRST(head);                                              \
            while (SLIST_NEXT(_c, field) != (elm))                                            \
                _c = SLIST_NEXT(_c, field);                                                   \
            SLIST_NEXT(_c, field) = SLIST_NEXT(elm, field);                                   \
        }                                                                                     \
    } while (0)

// ---------------------------------------------------------------- LIST
//
// Doubly linked through a pointer to the predecessor's next, so a removal needs
// no head and no branch.

#define LIST_HEAD(name, type)                                                                 \
    struct name {                                                                             \
        struct type *lh_first;                                                                \
    }
#define LIST_HEAD_INITIALIZER(head) { NULL }
#define LIST_ENTRY(type)                                                                      \
    struct {                                                                                  \
        struct type *le_next;                                                                 \
        struct type **le_prev;                                                                \
    }

#define LIST_INIT(head)       ((head)->lh_first = NULL)
#define LIST_FIRST(head)      ((head)->lh_first)
#define LIST_EMPTY(head)      ((head)->lh_first == NULL)
#define LIST_NEXT(elm, field) ((elm)->field.le_next)

#define LIST_FOREACH(var, head, field)                                                        \
    for ((var) = LIST_FIRST(head); (var); (var) = LIST_NEXT(var, field))
#define LIST_FOREACH_SAFE(var, head, field, tvar)                                             \
    for ((var) = LIST_FIRST(head); (var) && ((tvar) = LIST_NEXT(var, field), 1); (var) = (tvar))

#define LIST_INSERT_HEAD(head, elm, field)                                                    \
    do {                                                                                      \
        if ((LIST_NEXT(elm, field) = LIST_FIRST(head)) != NULL)                               \
            LIST_FIRST(head)->field.le_prev = &LIST_NEXT(elm, field);                         \
        LIST_FIRST(head)      = (elm);                                                        \
        (elm)->field.le_prev  = &LIST_FIRST(head);                                            \
    } while (0)
#define LIST_INSERT_AFTER(listelm, elm, field)                                                \
    do {                                                                                      \
        if ((LIST_NEXT(elm, field) = LIST_NEXT(listelm, field)) != NULL)                      \
            LIST_NEXT(listelm, field)->field.le_prev = &LIST_NEXT(elm, field);                \
        LIST_NEXT(listelm, field) = (elm);                                                    \
        (elm)->field.le_prev      = &LIST_NEXT(listelm, field);                               \
    } while (0)
#define LIST_INSERT_BEFORE(listelm, elm, field)                                               \
    do {                                                                                      \
        (elm)->field.le_prev      = (listelm)->field.le_prev;                                 \
        LIST_NEXT(elm, field)     = (listelm);                                                \
        *(listelm)->field.le_prev = (elm);                                                    \
        (listelm)->field.le_prev  = &LIST_NEXT(elm, field);                                   \
    } while (0)
#define LIST_REMOVE(elm, field)                                                               \
    do {                                                                                      \
        if (LIST_NEXT(elm, field) != NULL)                                                    \
            LIST_NEXT(elm, field)->field.le_prev = (elm)->field.le_prev;                      \
        *(elm)->field.le_prev = LIST_NEXT(elm, field);                                         \
    } while (0)

// -------------------------------------------------------------- STAILQ

#define STAILQ_HEAD(name, type)                                                               \
    struct name {                                                                             \
        struct type *stqh_first;                                                              \
        struct type **stqh_last;                                                              \
    }
#define STAILQ_HEAD_INITIALIZER(head) { NULL, &(head).stqh_first }
#define STAILQ_ENTRY(type)                                                                    \
    struct {                                                                                  \
        struct type *stqe_next;                                                               \
    }

#define STAILQ_INIT(head)                                                                     \
    do {                                                                                      \
        STAILQ_FIRST(head)  = NULL;                                                           \
        (head)->stqh_last   = &STAILQ_FIRST(head);                                            \
    } while (0)
#define STAILQ_FIRST(head)      ((head)->stqh_first)
#define STAILQ_EMPTY(head)      ((head)->stqh_first == NULL)
#define STAILQ_NEXT(elm, field) ((elm)->field.stqe_next)
#define STAILQ_LAST(head, type, field)                                                        \
    (STAILQ_EMPTY(head) ? NULL                                                                \
                        : (struct type *)(void *)((char *)(head)->stqh_last -                 \
                                                  offsetof(struct type, field)))

#define STAILQ_FOREACH(var, head, field)                                                      \
    for ((var) = STAILQ_FIRST(head); (var); (var) = STAILQ_NEXT(var, field))
#define STAILQ_FOREACH_SAFE(var, head, field, tvar)                                           \
    for ((var) = STAILQ_FIRST(head); (var) && ((tvar) = STAILQ_NEXT(var, field), 1);          \
         (var) = (tvar))

#define STAILQ_INSERT_HEAD(head, elm, field)                                                  \
    do {                                                                                      \
        if ((STAILQ_NEXT(elm, field) = STAILQ_FIRST(head)) == NULL)                           \
            (head)->stqh_last = &STAILQ_NEXT(elm, field);                                     \
        STAILQ_FIRST(head) = (elm);                                                           \
    } while (0)
#define STAILQ_INSERT_TAIL(head, elm, field)                                                  \
    do {                                                                                      \
        STAILQ_NEXT(elm, field) = NULL;                                                       \
        *(head)->stqh_last      = (elm);                                                      \
        (head)->stqh_last       = &STAILQ_NEXT(elm, field);                                   \
    } while (0)
#define STAILQ_INSERT_AFTER(head, tqelm, elm, field)                                          \
    do {                                                                                      \
        if ((STAILQ_NEXT(elm, field) = STAILQ_NEXT(tqelm, field)) == NULL)                    \
            (head)->stqh_last = &STAILQ_NEXT(elm, field);                                     \
        STAILQ_NEXT(tqelm, field) = (elm);                                                    \
    } while (0)
#define STAILQ_REMOVE_HEAD(head, field)                                                       \
    do {                                                                                      \
        if ((STAILQ_FIRST(head) = STAILQ_NEXT(STAILQ_FIRST(head), field)) == NULL)            \
            (head)->stqh_last = &STAILQ_FIRST(head);                                          \
    } while (0)
#define STAILQ_REMOVE(head, elm, type, field)                                                 \
    do {                                                                                      \
        if (STAILQ_FIRST(head) == (elm)) {                                                    \
            STAILQ_REMOVE_HEAD(head, field);                                                  \
        } else {                                                                              \
            struct type *_c = STAILQ_FIRST(head);                                             \
            while (STAILQ_NEXT(_c, field) != (elm))                                           \
                _c = STAILQ_NEXT(_c, field);                                                  \
            STAILQ_REMOVE_AFTER(head, _c, field);                                             \
        }                                                                                     \
    } while (0)
#define STAILQ_REMOVE_AFTER(head, elm, field)                                                 \
    do {                                                                                      \
        if ((STAILQ_NEXT(elm, field) = STAILQ_NEXT(STAILQ_NEXT(elm, field), field)) == NULL)  \
            (head)->stqh_last = &STAILQ_NEXT(elm, field);                                     \
    } while (0)

// --------------------------------------------------------------- TAILQ

#define TAILQ_HEAD(name, type)                                                                \
    struct name {                                                                             \
        struct type *tqh_first;                                                               \
        struct type **tqh_last;                                                               \
    }
#define TAILQ_HEAD_INITIALIZER(head) { NULL, &(head).tqh_first }
#define TAILQ_ENTRY(type)                                                                     \
    struct {                                                                                  \
        struct type *tqe_next;                                                                \
        struct type **tqe_prev;                                                               \
    }

#define TAILQ_INIT(head)                                                                      \
    do {                                                                                      \
        TAILQ_FIRST(head) = NULL;                                                             \
        (head)->tqh_last  = &TAILQ_FIRST(head);                                               \
    } while (0)
#define TAILQ_FIRST(head)      ((head)->tqh_first)
#define TAILQ_EMPTY(head)      ((head)->tqh_first == NULL)
#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)
#define TAILQ_LAST(head, headname)                                                            \
    (*(((struct headname *)((head)->tqh_last))->tqh_last))
#define TAILQ_PREV(elm, headname, field)                                                      \
    (*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))

#define TAILQ_FOREACH(var, head, field)                                                       \
    for ((var) = TAILQ_FIRST(head); (var); (var) = TAILQ_NEXT(var, field))
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                                            \
    for ((var) = TAILQ_FIRST(head); (var) && ((tvar) = TAILQ_NEXT(var, field), 1);            \
         (var) = (tvar))
#define TAILQ_FOREACH_REVERSE(var, head, headname, field)                                     \
    for ((var) = TAILQ_LAST(head, headname); (var); (var) = TAILQ_PREV(var, headname, field))
#define TAILQ_FOREACH_REVERSE_SAFE(var, head, headname, field, tvar)                          \
    for ((var) = TAILQ_LAST(head, headname);                                                  \
         (var) && ((tvar) = TAILQ_PREV(var, headname, field), 1); (var) = (tvar))

#define TAILQ_INSERT_HEAD(head, elm, field)                                                   \
    do {                                                                                      \
        if ((TAILQ_NEXT(elm, field) = TAILQ_FIRST(head)) != NULL)                             \
            TAILQ_FIRST(head)->field.tqe_prev = &TAILQ_NEXT(elm, field);                      \
        else                                                                                  \
            (head)->tqh_last = &TAILQ_NEXT(elm, field);                                       \
        TAILQ_FIRST(head)    = (elm);                                                         \
        (elm)->field.tqe_prev = &TAILQ_FIRST(head);                                           \
    } while (0)
#define TAILQ_INSERT_TAIL(head, elm, field)                                                   \
    do {                                                                                      \
        TAILQ_NEXT(elm, field) = NULL;                                                        \
        (elm)->field.tqe_prev  = (head)->tqh_last;                                            \
        *(head)->tqh_last      = (elm);                                                       \
        (head)->tqh_last       = &TAILQ_NEXT(elm, field);                                     \
    } while (0)
#define TAILQ_INSERT_AFTER(head, listelm, elm, field)                                         \
    do {                                                                                      \
        if ((TAILQ_NEXT(elm, field) = TAILQ_NEXT(listelm, field)) != NULL)                    \
            TAILQ_NEXT(elm, field)->field.tqe_prev = &TAILQ_NEXT(elm, field);                 \
        else                                                                                  \
            (head)->tqh_last = &TAILQ_NEXT(elm, field);                                       \
        TAILQ_NEXT(listelm, field) = (elm);                                                   \
        (elm)->field.tqe_prev      = &TAILQ_NEXT(listelm, field);                             \
    } while (0)
#define TAILQ_INSERT_BEFORE(listelm, elm, field)                                              \
    do {                                                                                      \
        (elm)->field.tqe_prev      = (listelm)->field.tqe_prev;                               \
        TAILQ_NEXT(elm, field)     = (listelm);                                               \
        *(listelm)->field.tqe_prev = (elm);                                                   \
        (listelm)->field.tqe_prev  = &TAILQ_NEXT(elm, field);                                 \
    } while (0)
#define TAILQ_REMOVE(head, elm, field)                                                        \
    do {                                                                                      \
        if ((TAILQ_NEXT(elm, field)) != NULL)                                                 \
            TAILQ_NEXT(elm, field)->field.tqe_prev = (elm)->field.tqe_prev;                   \
        else                                                                                  \
            (head)->tqh_last = (elm)->field.tqe_prev;                                         \
        *(elm)->field.tqe_prev = TAILQ_NEXT(elm, field);                                      \
    } while (0)
#define TAILQ_CONCAT(head1, head2, field)                                                     \
    do {                                                                                      \
        if (!TAILQ_EMPTY(head2)) {                                                            \
            *(head1)->tqh_last              = (head2)->tqh_first;                             \
            (head2)->tqh_first->field.tqe_prev = (head1)->tqh_last;                           \
            (head1)->tqh_last               = (head2)->tqh_last;                              \
            TAILQ_INIT(head2);                                                                \
        }                                                                                     \
    } while (0)
