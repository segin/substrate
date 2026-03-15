/*
 * pgrp.c - Process Group and Session Management
 *
 * Implements POSIX process groups and sessions for job control.
 * Based on 4.4BSD design patterns.
 */

#include <sys/proc.h>
#include <sys/session.h>
#include <sys/lock.h>
#include <stddef.h>
#include <string.h>
#include <vm/vm_kmem.h>



/* Global pgrp hash table for O(1) lookup by pgid */
#define PGRP_HASH_SIZE 16
static struct pgrp *pgrp_hash[PGRP_HASH_SIZE];

/* Global session list - currently unused, will be used for session enumeration */
/* static struct session *session_list = NULL; */

/* Hash function for pgrp lookup */
static inline int pgrp_hashval(int pgid) {
    return pgid % PGRP_HASH_SIZE;
}

static void __pgrp_add_proc(struct pgrp *pgrp, struct process *proc);
static void __pgrp_remove_proc(struct process *proc);
static int __pgrp_is_orphaned(struct pgrp *pgrp);
static int __pgrp_has_stopped(struct pgrp *pgrp);
static struct session *__pgrp_unlink_locked(struct pgrp *pgrp);

/*
 * session_alloc - Allocate and initialize a new session
 *
 * The calling process becomes the session leader.
 * Returns new session or NULL on error.
 */
struct session *session_alloc(struct process *leader) {
    struct session *sess = kmalloc(sizeof(struct session));
    if (!sess) return NULL;
    
    memset(sess, 0, sizeof(struct session));
    sess->s_sid = leader->pid;
    sess->s_leader = leader;
    sess->s_ttyvp = NULL;  /* No controlling terminal initially */
    sess->s_count = 1;
    sess->s_pgrps = NULL;
    memset(sess->s_login, 0, sizeof(sess->s_login));
    
    /* Add to global session list */
    /* (Simple linked list; production would use hash or tree) */
    
    return sess;
}

/*
 * session_free - Free a session when reference count drops to 0
 */
void session_free(struct session *sess) {
    if (!sess) return;
    if (--sess->s_count > 0) return;
    
    /* Session should have no pgrps at this point */
    kfree(sess, sizeof(struct session));
}

/*
 * pgrp_alloc - Allocate and initialize a new process group
 *
 * The group is added to the specified session.
 * Returns new pgrp or NULL on error.
 */
struct pgrp *pgrp_alloc(struct process *leader, struct session *sess) {
    struct pgrp *pgrp = kmalloc(sizeof(struct pgrp));
    if (!pgrp) return NULL;
    
    memset(pgrp, 0, sizeof(struct pgrp));
    pgrp->pg_id = leader->pid;
    pgrp->pg_members = NULL;
    pgrp->pg_session = sess;
    pgrp->pg_jobc = 0;
    
    /* Add to session's pgrp list */
    mutex_lock(&proctree_lock);
    pgrp->pg_sess_next = sess->s_pgrps;
    sess->s_pgrps = pgrp;
    
    /* Add to hash table */
    int hash = pgrp_hashval(pgrp->pg_id);
    pgrp->pg_hash_next = pgrp_hash[hash];
    pgrp_hash[hash] = pgrp;
    mutex_unlock(&proctree_lock);
    
    return pgrp;
}

/*
 * pgrp_free - Free a process group when it becomes empty
 */
void pgrp_free(struct pgrp *pgrp) {
    struct session *sess;

    if (!pgrp) return;
    if (pgrp->pg_members != NULL) return; /* Still has members */

    mutex_lock(&proctree_lock);
    sess = __pgrp_unlink_locked(pgrp);
    mutex_unlock(&proctree_lock);

    kfree(pgrp, sizeof(struct pgrp));
    if (sess) {
        session_free(sess);
    }
}

/*
 * pgrp_find - Find a process group by ID
 */
struct pgrp *pgrp_find(int pgid) {
    mutex_lock(&proctree_lock);
    int hash = pgrp_hashval(pgid);
    struct pgrp *pgrp = pgrp_hash[hash];
    while (pgrp) {
        if (pgrp->pg_id == pgid) {
            mutex_unlock(&proctree_lock);
            return pgrp;
        }
        pgrp = pgrp->pg_hash_next;
    }
    mutex_unlock(&proctree_lock);
    return NULL;
}

/*
 * pgrp_add_proc - Add a process to a process group
 */
/*
 * __pgrp_add_proc - Internal (must hold proctree_lock)
 */
static void __pgrp_add_proc(struct pgrp *pgrp, struct process *proc) {
    if (!pgrp || !proc) return;
    
    /* Remove from current group first */
    __pgrp_remove_proc(proc);
    
    /* Add to new group's member list */
    proc->p_pgrp = pgrp;
    proc->p_pgrp_link = pgrp->pg_members;
    pgrp->pg_members = proc;
}

void pgrp_add_proc(struct pgrp *pgrp, struct process *proc) {
    mutex_lock(&proctree_lock);
    __pgrp_add_proc(pgrp, proc);
    mutex_unlock(&proctree_lock);
}

/*
 * __pgrp_remove_proc - Internal (must hold proctree_lock)
 */
static void __pgrp_remove_proc(struct process *proc) {
    if (!proc || !proc->p_pgrp) return;
    
    struct pgrp *pgrp = proc->p_pgrp;
    struct process **pp = &pgrp->pg_members;
    
    while (*pp && *pp != proc) {
        pp = &(*pp)->p_pgrp_link;
    }
    
    if (*pp) {
        *pp = proc->p_pgrp_link;
    }
    
    proc->p_pgrp = NULL;
    proc->p_pgrp_link = NULL;
    
    /* If group is now empty, consider freeing it */
    if (pgrp->pg_members == NULL) {
        /* Don't free immediately; let caller decide */
    }
}

void pgrp_remove_proc(struct process *proc) {
    struct pgrp *old_pgrp;
    struct session *free_sess = NULL;
    int orphaned = 0;
    int has_stopped = 0;

    if (!proc) return;

    old_pgrp = proc->p_pgrp;
    mutex_lock(&proctree_lock);
    __pgrp_remove_proc(proc);
    if (old_pgrp && old_pgrp->pg_members) {
        orphaned = __pgrp_is_orphaned(old_pgrp);
        has_stopped = __pgrp_has_stopped(old_pgrp);
    } else if (old_pgrp) {
        free_sess = __pgrp_unlink_locked(old_pgrp);
    }
    mutex_unlock(&proctree_lock);

    if (!old_pgrp) {
        return;
    }
    if (free_sess || old_pgrp->pg_members == NULL) {
        kfree(old_pgrp, sizeof(struct pgrp));
        if (free_sess) {
            session_free(free_sess);
        }
    } else {
        if (orphaned && has_stopped) {
            pgrp_signal(old_pgrp, 1);  /* SIGHUP */
            pgrp_signal(old_pgrp, 18); /* SIGCONT */
        }
    }
}

/*
 * sys_setsid - Create a new session
 *
 * The calling process becomes the session leader and process group leader.
 * Returns new session ID on success, -1 with errno on error.
 *
 * EPERM if already a process group leader.
 */
int sys_setsid(void) {
    /* L522: Check if already a process group leader */
    if (current_process->p_pgrp && 
        current_process->p_pgrp->pg_id == current_process->pid) {
        return -1; /* EPERM - already a group leader */
    }
    
    /* L523: Allocate new session and pgrp */
    struct session *sess = session_alloc(current_process);
    if (!sess) return -1; /* ENOMEM */
    
    struct pgrp *pgrp = pgrp_alloc(current_process, sess);
    if (!pgrp) {
        session_free(sess);
        return -1; /* ENOMEM */
    }
    
    /* Remove from current group (if any) implicitly handled by pgrp_add_proc */
    
    /* L524: Add to new group (p_pgid = p_pid since we're the leader) */
    pgrp_add_proc(pgrp, current_process);
    
    /* L525: Detach current CTTY (if any) */
    current_process->tty = NULL;
    
    /* Return session ID (s_sid = p_pid) */
    return sess->s_sid;
}

/*
 * sys_getsid - Get session ID
 *
 * Returns the session ID of the process specified by pid.
 * If pid is 0, returns session ID of calling process.
 */
int sys_getsid(int pid) {
    struct process *target;
    
    if (pid == 0) {
        target = current_process;
    } else {
        /* Find process by PID */
        target = NULL;
        for (int i = 0; i < 16; i++) {
            if (processes[i].pid == pid) {
                target = &processes[i];
                break;
            }
        }
    }
    
    if (!target) return -1; /* ESRCH */
    
    /* Return session ID from pgrp->session */
    if (!target->p_pgrp || !target->p_pgrp->pg_session) {
        return -1; /* No session */
    }
    
    return target->p_pgrp->pg_session->s_sid;
}

/*
 * sys_getpgid - Get process group ID
 *
 * Returns the process group ID of the process specified by pid.
 * If pid is 0, returns process group ID of calling process.
 */
int sys_getpgid(int pid) {
    struct process *target;
    
    if (pid == 0) {
        target = current_process;
    } else {
        /* Find process by PID */
        target = NULL;
        for (int i = 0; i < 16; i++) {
            if (processes[i].pid == pid) {
                target = &processes[i];
                break;
            }
        }
    }
    
    if (!target) return -1; /* ESRCH */
    
    if (!target->p_pgrp) return -1;
    
    return target->p_pgrp->pg_id;
}

/*
 * sys_setpgid - Set process group ID
 *
 * Sets the process group of process pid to pgid.
 * If pid is 0, uses calling process.
 * If pgid is 0, uses pid as the new pgid.
 */
int sys_setpgid(int pid, int pgid) {
    struct process *target;
    
    if (pid == 0) {
        target = current_process;
        pid = target->pid;
    } else {
        target = NULL;
        for (int i = 0; i < 16; i++) {
            if (processes[i].pid == pid) {
                target = &processes[i];
                break;
            }
        }
    }
    
    if (!target) return -1; /* ESRCH */
    
    if (pgid < 0) return -1; /* EINVAL */
    if (pgid == 0) pgid = pid;
    
    /* L530: Must be in same session */
    struct session *target_sess = NULL;
    if (target->p_pgrp && target->p_pgrp->pg_session) {
        target_sess = target->p_pgrp->pg_session;
    }
    
    struct session *caller_sess = NULL;
    if (current_process->p_pgrp && current_process->p_pgrp->pg_session) {
        caller_sess = current_process->p_pgrp->pg_session;
    }
    
    /* If changing another process, check it's a child and in same session */
    if (target != current_process) {
        /* Must be a child of caller */
        if (target->ppid != current_process->pid) {
            return -1; /* ESRCH - not a child */
        }
        /* Must be in same session */
        if (target_sess != caller_sess) {
            return -1; /* EPERM - different session */
        }
    }
    
    /* L529: Join existing group or create new one */
    struct pgrp *new_pgrp = pgrp_find(pgid);
    
    if (new_pgrp) {
        /* L530: Verify same session */
        if (new_pgrp->pg_session != caller_sess) {
            return -1; /* EPERM - pgrp in different session */
        }
        pgrp_add_proc(new_pgrp, target);
    } else if (pgid == target->pid) {
        /* Create new pgrp with target as leader */
        if (!caller_sess) {
            /* Create a default session if none exists */
            caller_sess = session_alloc(target);
            if (!caller_sess) return -1;
        }
        new_pgrp = pgrp_alloc(target, caller_sess);
        if (!new_pgrp) return -1;
        pgrp_add_proc(new_pgrp, target);
    } else {
        /* pgid must be an existing group or target->pid */
        return -1; /* EPERM */
    }
    
    return 0;
}

/*
 * pgrp_signal - Send signal to all processes in a group
 */
void pgrp_signal(struct pgrp *pgrp, int sig) {
    if (!pgrp) return;
    
    /* psignal is defined in kern/signal.c */
    extern void psignal(process_t *proc, int sig);
    
    struct process *p = pgrp->pg_members;
    while (p) {
        psignal(p, sig);
        p = p->p_pgrp_link;
    }
}

/*
 * session_find - Find a session by ID
 */
struct session *session_find(int sid) {
    /* Linear search through hash table pgrps for matching session */
    for (int i = 0; i < PGRP_HASH_SIZE; i++) {
        struct pgrp *pg = pgrp_hash[i];
        while (pg) {
            if (pg->pg_session && pg->pg_session->s_sid == sid) {
                return pg->pg_session;
            }
            pg = pg->pg_hash_next;
        }
    }
    return NULL;
}

/*
 * pgrp_is_orphaned - Check if a process group is orphaned
 *
 * A process group is orphaned if no member has a parent in a different
 * process group within the same session.
 */
int pgrp_is_orphaned(struct pgrp *pgrp) {
    int orphaned;

    mutex_lock(&proctree_lock);
    orphaned = __pgrp_is_orphaned(pgrp);
    mutex_unlock(&proctree_lock);
    return orphaned;
}

static int __pgrp_is_orphaned(struct pgrp *pgrp) {
    if (!pgrp || !pgrp->pg_session) return 1;
    
    struct process *p = pgrp->pg_members;
    while (p) {
        if (p->p_parent && p->p_parent->p_pgrp != pgrp) {
            /* Parent is in different pgrp */
            if (p->p_parent->p_pgrp && 
                p->p_parent->p_pgrp->pg_session == pgrp->pg_session) {
                /* Parent is in same session but different pgrp */
                return 0; /* Not orphaned */
            }
        }
        p = p->p_pgrp_link;
    }
    
    return 1; /* Orphaned */
}

static int __pgrp_has_stopped(struct pgrp *pgrp) {
    struct process *p;

    if (!pgrp) {
        return 0;
    }

    p = pgrp->pg_members;
    while (p) {
        if (p->state == SSTOP) {
            return 1;
        }
        p = p->p_pgrp_link;
    }

    return 0;
}

static struct session *__pgrp_unlink_locked(struct pgrp *pgrp) {
    struct session *sess;
    int hash;
    struct pgrp **pp;

    if (!pgrp) {
        return NULL;
    }

    hash = pgrp_hashval(pgrp->pg_id);
    pp = &pgrp_hash[hash];
    while (*pp && *pp != pgrp) {
        pp = &(*pp)->pg_hash_next;
    }
    if (*pp) {
        *pp = pgrp->pg_hash_next;
    }

    sess = pgrp->pg_session;
    if (!sess) {
        return NULL;
    }

    pp = &sess->s_pgrps;
    while (*pp && *pp != pgrp) {
        pp = &(*pp)->pg_sess_next;
    }
    if (*pp) {
        *pp = pgrp->pg_sess_next;
    }
    if (sess->s_pgrps == NULL) {
        return sess;
    }

    return NULL;
}

/*
 * pgrp_check_orphan - Handle orphaned process group
 *
 * If a group becomes orphaned and has stopped members,
 * send SIGHUP followed by SIGCONT to all members.
 */
void pgrp_check_orphan(struct pgrp *pgrp) {
    int orphaned;
    int has_stopped;

    if (!pgrp) return;

    mutex_lock(&proctree_lock);
    orphaned = __pgrp_is_orphaned(pgrp);
    has_stopped = __pgrp_has_stopped(pgrp);
    mutex_unlock(&proctree_lock);

    if (orphaned && has_stopped) {
        /* Send SIGHUP + SIGCONT to all members */
        pgrp_signal(pgrp, 1);  /* SIGHUP */
        pgrp_signal(pgrp, 18); /* SIGCONT */
    }
}

void proc_leave_pgrp(struct process *proc) {
    pgrp_remove_proc(proc);
}

int proc_join_pgrp(struct process *proc, struct pgrp *pgrp) {
    if (!proc || !pgrp) {
        return -1;
    }
    pgrp_add_proc(pgrp, proc);
    return 0;
}
