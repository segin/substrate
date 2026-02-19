#pragma once
#include <stdint.h>
#include <stddef.h>

struct session {
    int s_sid;
    struct process *s_leader;
};

struct pgrp {
    int pg_id;
    struct session *pg_session;
};

struct process {
    int pid;
    int next_fd;
    struct pgrp *p_pgrp;
    struct tty *tty;
};

extern struct process *current_process;
