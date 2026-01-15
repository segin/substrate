/*
 * sys/session.h - Process Group and Session Structures
 *
 * POSIX-compliant process groups and sessions for job control.
 * Based on 4.4BSD design patterns.
 */

#ifndef _SYS_SESSION_H
#define _SYS_SESSION_H

#include <stdint.h>

// Forward declarations
struct process;
struct fs_node;

// Maximum login name length (POSIX LOGIN_NAME_MAX)
#define MAXLOGNAME 17

/*
 * Process Group Structure
 *
 * Every process belongs to exactly one process group.
 * Every process group belongs to exactly one session.
 * Groups are used for job control signal delivery.
 */
struct pgrp {
    int             pg_id;          /* Process group ID */
    struct process *pg_members;     /* Head of member list (via p_pgrp_link) */
    struct session *pg_session;     /* Parent session */
    struct pgrp    *pg_next;        /* Next pgrp in session's list */
    int             pg_jobc;        /* Job control counter (# procs with parent in different pgrp) */
};

/*
 * Session Structure
 *
 * Sessions group process groups and manage the controlling terminal.
 * The session leader is the process that called setsid().
 */
struct session {
    int             s_sid;          /* Session ID (same as session leader's PID) */
    struct process *s_leader;       /* Session leader process */
    struct fs_node *s_ttyvp;        /* Controlling terminal vnode (NULL if none) */
    struct pgrp    *s_pgrps;        /* List of process groups in session */
    int             s_count;        /* Reference count */
    char            s_login[MAXLOGNAME]; /* Login name of session creator */
};

/* Session/pgrp management functions */
struct session *session_alloc(struct process *leader);
void session_free(struct session *sess);
struct pgrp *pgrp_alloc(struct process *leader, struct session *sess);
void pgrp_free(struct pgrp *pgrp);
void pgrp_add_proc(struct pgrp *pgrp, struct process *proc);
void pgrp_remove_proc(struct process *proc);
struct pgrp *pgrp_find(int pgid);
struct session *session_find(int sid);

/* Signal delivery to process group */
void pgrp_signal(struct pgrp *pgrp, int sig);

/* Orphaned process group handling */
int pgrp_is_orphaned(struct pgrp *pgrp);
void pgrp_check_orphan(struct pgrp *pgrp);

/* Job control helpers */
void proc_leave_pgrp(struct process *proc);
int proc_join_pgrp(struct process *proc, struct pgrp *pgrp);

#endif /* _SYS_SESSION_H */
