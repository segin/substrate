/*
 * tcp.c — TCP (RFC 793 subset) for substrate.
 *
 * Layout, top to bottom:
 *   - Types and tunables
 *   - Per-PCB list (linear; replace with hash once profiling shows it)
 *   - Segment construction + tcp_xmit (fresh and retransmit share this)
 *   - Per-PCB send queue of unacked segments (linked list)
 *   - Retransmit timer kthread (one per system)
 *   - tcp_input + per-state handlers
 *   - Public API (alloc/free, bind/listen/accept, connect{,_nb},
 *     send, recv{,_nb}, close, poll)
 *
 * What changed from the previous fire-and-forget design:
 *   - Every outbound data segment AND the SYN/FIN handshake segments
 *     are queued in the PCB's unacked list with a send timestamp.
 *   - A kthread walks all PCBs every ~250ms and retransmits any
 *     segment whose RTO has expired.  Cap of TCP_MAX_RETX; past
 *     that the PCB transitions to CLOSED + wakes recv/connect.
 *   - On ACK, segments whose [seq, seq+len) is fully covered by the
 *     acknowledged window are unlinked + freed.  snd_una advances.
 *   - Triple duplicate ACK triggers fast retransmit of the head
 *     segment without waiting for the RTO.
 *   - TIME_WAIT has a real timeout (2*MSL ≈ 1s here) after which the
 *     PCB is freed.
 *
 * Still TBD: send window/cwnd, SACK, RTT-driven RTO, IPv6 transport.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <arch/i386/intr.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <sys/random.h>
#include <net/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/kthread.h>
#include <sys/netdev.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <vm/vm_kmem.h>

/*
 * Netstack synchronisation.  tcp_segment_input() and everything it
 * calls run in hard IRQ context (netdev RX -> ip4_input -> tcp).
 * The socket-layer entry points (tcp_alloc/free/close/recv/accept/
 * connect/send) run in process context and mutate the SAME state:
 * the g_tcp_pcbs list, per-PCB rx ring + counters, accept queues,
 * the unacked send queue.  With no mutual exclusion an RX interrupt
 * landing mid-update corrupts the PCB — the crash class behind
 * "tcp_close called with p=0x28" and the scattered afi_sock damage.
 *
 * Substrate's RX path always runs with IRQs already disabled (it's
 * an ISR), so on a uniprocessor it's enough for the process-context
 * critical sections to disable local IRQs for the duration: that
 * makes them atomic against RX.  tcp_lock()/tcp_unlock() bracket
 * those sections.  (SMP would need a real spinlock here too.)
 */
static inline uint32_t tcp_lock(void)   { return intr_disable(); }
static inline void     tcp_unlock(uint32_t f) { intr_restore(f); }

#define IPPROTO_TCP        6
#define TCP_RING_LEN       (32 * 1024)
#define TCP_MSS            1460
/*
 * TCP-06: every timer constant here was hardcoded for HZ=128 while
 * <sys/param.h> defines HZ 250, so each was HALF its documented value --
 * RTO 256ms instead of 500ms, TIME_WAIT 512ms instead of 1s, and a total
 * retry budget of about 1.5s.  Any peer with an RTT over 256ms had every
 * segment spuriously retransmitted and never converged, a 2s outage
 * aborted every connection with ETIMEDOUT, and connect() gave up after
 * ~1.5s.  Derive them from HZ so they mean what they say, and give the
 * RTO the exponential backoff RFC 6298 requires (it was reset flat on
 * every retransmit, with no doubling at all).
 *
 * RFC 6298 3.1 mandates an initial RTO of at least 1 second; RFC 1122
 * 4.2.3.5 wants the total budget (R2) to be generous before abort.  With
 * a 1s base doubling per attempt, TCP_MAX_RETX of 6 gives
 * 1+2+4+8+16+32 = 63s, which is in the right region.  A full
 * SRTT/RTTVAR estimator is still absent and remains on the task.
 */
#define TCP_RTO_BASE_TICKS  (1 * HZ)     /* 1s initial RTO (RFC 6298 3.1) */
#define TCP_RTO_MAX_TICKS   (60 * HZ)    /* never back off past a minute */
#define TCP_MAX_RETX       6
#define TCP_TIMER_PERIOD   (HZ / 8)      /* ~125ms kthread wake interval */
/*
 * TCP-12: TIME_WAIT is 2*MSL.  This was 1 second, far too short to absorb a
 * retransmitted FIN from the peer, and short enough that 4-tuple reuse
 * became likely rather than astronomically improbable.  RFC 793 puts MSL at
 * 2 minutes; 30 s (60 s of TIME_WAIT) is the pragmatic value BSD and Linux
 * settled on and is what this uses -- long enough to be correct, short
 * enough not to hoard PCBs on a small system.
 */
#define TCP_MSL_TICKS       (30 * HZ)
#define TCP_TIME_WAIT_TICKS (2 * TCP_MSL_TICKS)
/* TCP-05: bound on how long we hold a PCB whose peer has stopped closing.
 * Generous enough not to break a slow-but-live peer, short enough that the
 * leak is bounded. */
#define TCP_FIN_WAIT_2_TICKS (60 * HZ)   /* 60s */
#define TCP_DUP_ACK_FAST   3             /* fast-retx trigger */
/* Safety-net poll interval for the blocking recv/accept/connect waits.
 * sched_sleep() is not race-free against sched_wakeup() — a wakeup that
 * fires between the readiness re-check and the sleep is lost.  Sleeping
 * with this deadline guarantees the waiter re-checks even if its wakeup
 * was missed, turning a permanent wedge into at most this much latency.
 * ~64ms at HZ=128; the wakeup still drives the common fast path. */
#define TCP_SLEEP_POLL     (HZ / 16)

enum tcp_state {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT,
};

/* A segment we sent and haven't seen acknowledged yet.  Kept in a
 * per-PCB FIFO; the head is what the retx timer looks at.  We don't
 * store a copy of the payload separately — the data field is the
 * actual bytes that were transmitted.  */
typedef struct tcp_seg {
    uint32_t  seq;            /* sequence number at the time of send */
    uint8_t   flags;          /* TCP flag bits — SYN/FIN tracked here */
    uint16_t  dlen;           /* data byte count */
    uint64_t  sent_tick;      /* timestamp of last (re-)transmit */
    int       retx;           /* number of retransmits so far */
    struct tcp_seg *next;
    uint8_t   data[];         /* flex array; dlen bytes */
} tcp_seg_t;

typedef struct tcp_pcb {
    int       state;
    uint32_t  laddr, raddr;        /* network byte order */
    uint16_t  lport, rport;        /* host order */
    uint32_t  iss;                 /* initial send seq */
    uint32_t  snd_una;             /* oldest unack */
    uint32_t  snd_nxt;             /* next seq to send */
    uint32_t  rcv_nxt;             /* next expected seq */
    uint16_t  rcv_wnd;             /* advertised window */
    uint32_t  snd_wnd;             /* peer's advertised window */
    /*
     * TCP-10: RFC 5681 congestion control.  There was none at all -- cwnd
     * appeared once as a "TBD" comment and ssthresh not at all -- so the
     * only limiter was the peer's receive window and up to 64 KB went out
     * in the first RTT with no slow start and no reduction on loss.  On any
     * path with a bottleneck that is a self-inflicted congestion collapse,
     * and it is the other half of why a single drop cost seconds to
     * recover (with TCP-11's missing reassembly queue).
     */
    uint32_t  cwnd;                /* congestion window, bytes */
    uint32_t  ssthresh;            /* slow-start threshold, bytes */
    /* Receive ring (in-order bytes pending tcp_recv()).  */
    uint8_t   *rxbuf;              /* TCP_RING_LEN */
    uint32_t   rx_head, rx_tail, rx_count;
    /* Unacked send queue (FIFO).  */
    tcp_seg_t *unacked_head;
    tcp_seg_t *unacked_tail;
    /* Fast-retransmit counter.  */
    uint32_t  last_ack;
    int       dup_ack;
    /* Time-bound state expiries.  */
    uint64_t  time_wait_until;
    /* TCP-05: deadline for a FIN_WAIT_2 whose peer never closes its half.
     * 0 while not in FIN_WAIT_2. */
    uint64_t  fin_wait2_until;
    /* SO_ERROR (cleared by getsockopt).  */
    int       so_error;
    /* Backlog for LISTEN sockets */
    struct tcp_pcb **accept_q;
    int        accept_cap, accept_count;
    /* Sleep channels */
    void      *connect_chan;
    void      *recv_chan;
    void      *accept_chan;
    void      *send_chan;          /* woken when the send window opens */
    int        listen;
    /* The owning socket has been closed (afinet_node_close -> tcp_close).
     * A detached PCB has no userspace owner: once it reaches a terminal
     * state the retransmit-timer kthread — the sole reaper — frees it,
     * and any data arriving for it is answered with a RST since there
     * is no socket to deliver to. */
    int        detached;
    /* shutdown(SHUT_RD): the receive direction is closed — recv()
     * returns EOF even though the connection is otherwise live. */
    int        shut_rd;
    /* Parent (for SYN_RECEIVED children before accept) */
    struct tcp_pcb *parent;
    /*
     * TCP-01: number of blocked callers currently holding this PCB across a
     * sleep.  The timer kthread is the sole reaper, and it must not free a
     * PCB that a sleeping tcp_recv/accept/connect is about to re-dereference
     * when it wakes.  0 = only the socket owns it, which is the reapable
     * state.  Manipulated under tcp_lock().
     */
    int        holds;
    /* Linked list */
    struct tcp_pcb *next;
} tcp_pcb_t;

static tcp_pcb_t *g_tcp_pcbs;

/*
 * TCP-01: pin a PCB across a blocking wait.
 *
 * tcp_close() only marks the PCB detached; the timer kthread frees it (and
 * its 32 KiB receive ring) once it reaches CLOSED.  But tcp_recv, tcp_accept
 * and tcp_connect capture the PCB pointer and re-dereference it after every
 * sched_sleep_until() wake, so the classic sequence -- thread A blocked in
 * recv(), thread B close()s the shared fd, the peer's FIN walks the state
 * machine to CLOSED, the next tick frees it -- had A wake up and read
 * p->rxbuf out of a freed slab and write p->rx_count back into it.  The
 * remote peer controls that timing.
 *
 * A hold keeps the reaper off the PCB for the duration of the call; the
 * reaper simply skips a held PCB and collects it on a later tick.  Note the
 * hold must span the whole blocking function, not just the sleep: releasing
 * before the final state check would reopen the same window.
 */
static void tcp_hold(tcp_pcb_t *p) {
    if (!p) return;
    uint32_t f = tcp_lock();
    p->holds++;
    tcp_unlock(f);
}

static void tcp_unhold(tcp_pcb_t *p) {
    if (!p) return;
    uint32_t f = tcp_lock();
    if (p->holds > 0) p->holds--;
    tcp_unlock(f);
}


/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/*
 * TCP-08: the initial sequence number must be unpredictable.
 *
 * This was a fixed-seed LCG (0xC0DE1234, no entropy), so the Nth ISN since
 * boot was computable offline -- and with TCP-07's predictable ports that is
 * everything an off-path attacker needs to inject into or reset a
 * connection.  RFC 6528 requires unpredictability.  It was also called
 * unlocked from both hard-IRQ (tcp_in_listen) and process (tcp_connect)
 * context, so two callers could hand out the same ISN.
 *
 * Draw from the kernel CSPRNG per call.  Callers already hold tcp_lock, but
 * take no chances: fall back to advancing the LCG (never to a constant) if
 * the RNG is not yet seeded this early in boot.
 */
static uint32_t tcp_iss_seed = 0xC0DE1234u;
static uint32_t tcp_new_iss(void) {
    uint32_t iss;
    if (random_get_bytes(&iss, sizeof(iss)) == 0)
        return iss;
    tcp_iss_seed = tcp_iss_seed * 1103515245 + 12345;
    return tcp_iss_seed;
}

static uint16_t tcp_csum(uint32_t saddr, uint32_t daddr,
                         const void *seg, size_t len) {
    return inet_csum_pseudo4(saddr, daddr, IPPROTO_TCP, (uint16_t)len, seg);
}

/* Write a TCP segment out via ip4_output.  Does NOT touch snd_nxt /
 * the unacked queue — the queuing layer below does that.  */
static int tcp_xmit_raw(tcp_pcb_t *p, uint32_t seq, uint8_t flags,
                        const void *data, size_t dlen) {
    uint8_t buf[TCP_MSS + sizeof(struct tcphdr)];
    if (dlen > TCP_MSS) dlen = TCP_MSS;
    struct tcphdr *th = (struct tcphdr *)buf;
    th->source     = __builtin_bswap16(p->lport);
    th->dest       = __builtin_bswap16(p->rport);
    th->seq        = __builtin_bswap32(seq);
    th->ack_seq    = __builtin_bswap32(p->rcv_nxt);
    th->doff_flags = __builtin_bswap16((uint16_t)((5u << 12) | flags));
    th->window     = __builtin_bswap16((uint16_t)(TCP_RING_LEN - p->rx_count));
    th->check      = 0;
    th->urg_ptr    = 0;
    if (dlen && data) memcpy(buf + sizeof(*th), data, dlen);
    /*
     * TCP-29: the pseudo-header source must be the address ip4_output will
     * put in the IP header, not p->laddr.  On a multihomed host they differ,
     * and when p->laddr is still 0 (a client socket that never bound) EVERY
     * segment shipped an invalid checksum -- silently, since we never see
     * the peer's discard.  ip4_source_for() is the same routing decision
     * ip4_output makes, and is what the UDP path uses since UDP-03.
     */
    uint32_t csum_src = p->laddr ? p->laddr : ip4_source_for(p->raddr);
    th->check = tcp_csum(csum_src, p->raddr, buf, sizeof(*th) + dlen);
    return ip4_output(p->raddr, IPPROTO_TCP, buf, sizeof(*th) + dlen);
}

/* Pure-ACK / pure-RST segments don't enter the retx queue.  Use this
 * for the "I want to acknowledge what I just received" pattern.  */
static void tcp_send_ctl(tcp_pcb_t *p, uint8_t flags) {
    tcp_xmit_raw(p, p->snd_nxt, flags, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* Send queue management                                              */
/* ------------------------------------------------------------------ */

/* Allocate a tcp_seg with `dlen` bytes of payload, copy `data` in,
 * transmit it, advance snd_nxt by the segment's sequence cost
 * (SYN/FIN count as 1, data counts as dlen), and link onto the
 * unacked FIFO so the timer can retransmit.  Returns 0 on success
 * or -ENOMEM if allocation failed (in which case nothing was
 * transmitted).  */
static int tcp_xmit_queue(tcp_pcb_t *p, uint8_t flags,
                          const void *data, size_t dlen) {
    if (dlen > TCP_MSS) dlen = TCP_MSS;
    tcp_seg_t *s = (tcp_seg_t *)kmalloc(sizeof(*s) + dlen);
    if (!s) return -ENOMEM;
    s->flags     = flags;
    s->dlen      = (uint16_t)dlen;
    s->sent_tick = get_ticks();
    s->retx      = 0;
    s->next      = NULL;
    if (dlen && data) memcpy(s->data, data, dlen);

    /* Assign the sequence number, advance snd_nxt, and link onto the
     * unacked FIFO — all under the lock and all BEFORE the transmit.
     *
     * Ordering is load-bearing: on loopback tcp_xmit_raw() delivers
     * the segment synchronously, the peer ACKs it, and that ACK is
     * processed (tcp_unacked_prune) before tcp_xmit_raw() even
     * returns.  If the segment were appended afterwards the ACK
     * could never prune it — it would sit at unacked_head forever,
     * RTO-retransmitted until ETIMEDOUT killed the connection, and
     * its permanent presence would block the FIN_WAIT_1 -> FIN_WAIT_2
     * transition (which requires !unacked_head), so the connection
     * could never close cleanly either. */
    uint32_t f = tcp_lock();
    s->seq = p->snd_nxt;
    p->snd_nxt += (uint32_t)dlen;
    if (flags & TCP_SYN) p->snd_nxt++;
    if (flags & TCP_FIN) p->snd_nxt++;
    if (p->unacked_tail) p->unacked_tail->next = s;
    else                 p->unacked_head = s;
    p->unacked_tail = s;
    tcp_unlock(f);

    /* Transmit with IRQs enabled (tcp_xmit_raw -> ip4_output may
     * ARP-wait, which needs IRQs on to receive the reply).  A failed
     * transmit leaves the segment queued; the RTO timer retransmits
     * it — which is the correct response to a transient send error. */
    tcp_xmit_raw(p, s->seq, flags, s->data, dlen);
    return 0;
}

/* Drop every segment from the unacked FIFO whose entire seq range
 * is <= ack — i.e. the peer has confirmed they got it.  Returns
 * the number of segments freed.  */
static int tcp_unacked_prune(tcp_pcb_t *p, uint32_t ack) {
    int freed = 0;
    while (p->unacked_head) {
        tcp_seg_t *s = p->unacked_head;
        uint32_t end = s->seq + s->dlen;
        if (s->flags & TCP_SYN) end++;
        if (s->flags & TCP_FIN) end++;
        /* Strictly less-than-or-equal to ack (ACKs are next-byte
         * expected — covers everything before `ack`).  */
        if ((int32_t)(end - ack) > 0) break;
        p->unacked_head = s->next;
        if (!p->unacked_head) p->unacked_tail = NULL;
        kfree(s, sizeof(*s) + s->dlen);
        freed++;
    }
    return freed;
}

static void tcp_unacked_free_all(tcp_pcb_t *p) {
    while (p->unacked_head) {
        tcp_seg_t *s = p->unacked_head;
        p->unacked_head = s->next;
        kfree(s, sizeof(*s) + s->dlen);
    }
    p->unacked_tail = NULL;
}

/* Re-transmit the head of the unacked queue (used by both RTO and
 * fast-retx).  */
static void tcp_retx_head(tcp_pcb_t *p) {
    tcp_seg_t *s = p->unacked_head;
    if (!s) return;
    tcp_xmit_raw(p, s->seq, s->flags, s->data, s->dlen);
    s->sent_tick = get_ticks();
    s->retx++;
}

/* ------------------------------------------------------------------ */
/* Retransmit timer kthread                                           */
/* ------------------------------------------------------------------ */

void tcp_free(tcp_pcb_t *p);   /* forward decl — timer reaps PCBs */

/* Move a PCB to CLOSED and wake anything waiting on it.  Never frees:
 * a PCB that still has a socket is freed when that socket closes; a
 * detached one is reaped by tcp_timer_tick().  Keeping the free out
 * of the RX path means tcp_find() (which skips CLOSED) can never hand
 * back a pointer that is about to be freed underneath the caller. */
static void tcp_kill_pcb(tcp_pcb_t *p, int err) {
    p->state    = TCP_CLOSED;
    p->so_error = err;
    sched_wakeup(p->connect_chan);
    sched_wakeup(p->recv_chan);
    sched_wakeup(p->accept_chan);
    sched_wakeup(p->send_chan);
}

/* NET-04: is this child still queued for a pending accept() on its
 * parent listener?  If so the reaper must not free it — a blocked
 * accept() could still hand it to userspace.  tcp_accept() clears
 * ->parent when it dequeues a child, so a child with ->parent still set
 * is either mid-handshake (not yet queued) or sitting in accept_q. */
static int tcp_child_in_accept_q(const tcp_pcb_t *p) {
    tcp_pcb_t *par = p->parent;
    if (!par || !par->accept_q) return 0;
    for (int i = 0; i < par->accept_count; i++)
        if (par->accept_q[i] == p) return 1;
    return 0;
}

static void tcp_timer_tick(uint64_t now) {
    /* The timer kthread is the single reaper of orphaned PCBs.  Walk the
     * list with IRQs off (tcp_lock) so an RX interrupt can neither free
     * nor splice a node — nor prune/free an unacked segment — underneath
     * us.
     *
     * NET-02: the retransmit runs INLINE under the lock.  The previous
     * design collected the victims, dropped the lock, then dereferenced
     * each PCB's unacked_head to transmit — a window in which an incoming
     * ACK (tcp_input, hard IRQ) could tcp_unacked_prune() and kfree() the
     * very segment tcp_retx_head() was about to read: a use-after-free.
     * Holding the lock across the dereference-and-transmit closes it.
     *
     * This is safe only because NET-05 makes ip4_output() non-sleeping
     * while interrupts are disabled: on an ARP miss it fires the request
     * and drops the frame (the next tick resends) instead of yielding, so
     * tcp_xmit_raw() cannot block here.  Retransmitting inline also drops
     * the old fixed 32-victim batch array (NET-11): every PCB whose RTO
     * has expired is serviced on this tick, not silently deferred. */
    uint32_t f = tcp_lock();
    for (tcp_pcb_t *p = g_tcp_pcbs, *next; p; p = next) {
        next = p->next;
        if (p->state == TCP_CLOSED) {
            /* Terminal.  Reap if orphaned — tcp_find() never returns a
             * CLOSED PCB, so no RX path can be holding this pointer. */
            /* TCP-01: never free a PCB a blocked caller is still holding;
             * it will be reaped on a later tick once that caller returns. */
            if (p->detached && p->holds == 0) { tcp_free(p); continue; }
            if (p->detached) continue;
            /* NET-04: a never-accepted child (->parent still set) that
             * died in the handshake — e.g. SYN_RECEIVED retransmit
             * timeout or a RST (NET-06) — has no userspace owner and no
             * fd.  Free it now instead of leaking its rxbuf until the
             * listener closes.  Skip it while still in the listener's
             * accept queue, where a pending accept() could claim it. */
            if (p->parent && !tcp_child_in_accept_q(p) && p->holds == 0) {
                tcp_free(p); continue;
            }
            continue;
        }
        /* TCP-05: reap a FIN_WAIT_2 whose peer never sent its FIN. */
        if (p->state == TCP_FIN_WAIT_2 && p->fin_wait2_until &&
            now >= p->fin_wait2_until) {
            tcp_kill_pcb(p, ETIMEDOUT);
            continue;
        }
        if (p->state == TCP_TIME_WAIT && now >= p->time_wait_until) {
            /* Drop to CLOSED now; freed on the next tick once no RX
             * can still be matching a late segment against it. */
            p->state = TCP_CLOSED;
            continue;
        }
        tcp_seg_t *head = p->unacked_head;
        if (!head) continue;
        /* TCP-06: back the RTO off exponentially per attempt rather than
         * retrying at a flat interval forever. */
        uint64_t rto = (uint64_t)TCP_RTO_BASE_TICKS << (unsigned)head->retx;
        if (rto > TCP_RTO_MAX_TICKS) rto = TCP_RTO_MAX_TICKS;
        if (now - head->sent_tick < rto) continue;
        if (head->retx >= TCP_MAX_RETX) {
            tcp_kill_pcb(p, ETIMEDOUT);
            continue;
        }
        /*
         * TCP-10: RFC 5681 3.1 -- an RTO is the strongest loss signal there
         * is, so ssthresh drops to half the flight size and cwnd collapses
         * all the way to one segment.  Slow start then rebuilds it.  Doing
         * this here rather than only on duplicate ACKs is what keeps a path
         * that has genuinely stalled from being hammered at the old rate.
         */
        {
            uint32_t flight = p->snd_nxt - p->snd_una;
            uint32_t half   = flight / 2;
            if (half < 2u * TCP_MSS) half = 2u * TCP_MSS;
            p->ssthresh = half;
            p->cwnd     = TCP_MSS;
            p->dup_ack  = 0;
        }
        tcp_retx_head(p);
    }
    tcp_unlock(f);
}

static void tcp_timer_thread(void *arg) {
    (void)arg;
    for (;;) {
        sched_sleep_until(&g_tcp_pcbs,
                          get_ticks() + TCP_TIMER_PERIOD);
        tcp_timer_tick(get_ticks());
    }
}

static int tcp_timer_started = 0;
/*
 * TCP-31: the retransmit timer is the only thing that resends lost segments,
 * expires TIME_WAIT and reaps detached PCBs, so losing it degrades TCP to
 * fire-and-forget with an unbounded PCB leak -- silently.
 *
 * The flag was set BEFORE the create and the result was never checked, so a
 * failed kthread_create left tcp_timer_started stuck at 1 and nothing ever
 * tried again.  The test-and-set was also non-atomic, so two concurrent
 * socket() calls could each spawn a reaper and both walk the PCB list.
 * Claim the flag atomically, and release it again if the create fails so a
 * later socket() retries.
 */
static void tcp_timer_ensure(void) {
    if (__atomic_exchange_n(&tcp_timer_started, 1, __ATOMIC_ACQ_REL))
        return;                       /* someone else already started it */
    thread_t *t = NULL;
    if (kthread_create(tcp_timer_thread, NULL, &t, "tcpretx") != 0) {
        __atomic_store_n(&tcp_timer_started, 0, __ATOMIC_RELEASE);
        kprintf("tcp: retransmit timer thread failed to start; "
                "retrying on the next socket()\n");
    }
}

/* ------------------------------------------------------------------ */
/* Inbound demux                                                      */
/* ------------------------------------------------------------------ */

static tcp_pcb_t *tcp_find(uint32_t saddr, uint16_t sport,
                           uint32_t daddr, uint16_t dport) {
    /* Look for exact 4-tuple match first.  laddr is allowed to be
     * "unbound" (0) — happens for client sockets that haven't picked
     * a local IP yet, or for loopback where the chosen source IP
     * differs from what the connect() caller specified. */
    for (tcp_pcb_t *p = g_tcp_pcbs; p; p = p->next) {
        if (p->state == TCP_CLOSED) continue;
        if (p->lport != dport) continue;
        if (p->raddr == saddr && p->rport == sport &&
            (p->laddr == 0 || p->laddr == daddr))
            return p;
    }
    /* Then a LISTEN socket on the local port. */
    for (tcp_pcb_t *p = g_tcp_pcbs; p; p = p->next) {
        if (p->state == TCP_LISTEN && p->lport == dport) {
            if (p->laddr == 0 || p->laddr == daddr) return p;
        }
    }
    return NULL;
}

/* Emit a bare RST for an unknown segment.  */
static void tcp_send_rst(uint32_t saddr, uint32_t daddr,
                         const struct tcphdr *th, uint8_t flags,
                         uint32_t seq, uint32_t ack, size_t dlen) {
    if (flags & TCP_RST) return;
    uint8_t buf[sizeof(struct tcphdr)];
    struct tcphdr *r = (struct tcphdr *)buf;
    memset(r, 0, sizeof(*r));
    r->source     = th->dest;
    r->dest       = th->source;
    /*
     * TCP-27: the two forms RFC 793 3.4 specifies.
     *
     * The ACK field was wrong twice over.  A FIN consumes a sequence number
     * and was not counted, so the RST acknowledged one byte short of the
     * offending segment.  And when the segment carried no ACK bit, `ack` is
     * whatever garbage sat in th->ack_seq and it was used as the RST's SEQ
     * anyway -- the RFC requires SEQ=0 with the ACK field covering the
     * segment in that case.  Some peers discard a RST that fails these
     * checks, which is exactly when a RST matters most.
     */
    uint32_t seg_end = seq + (uint32_t)dlen +
                       ((flags & TCP_SYN) ? 1u : 0u) +
                       ((flags & TCP_FIN) ? 1u : 0u);
    if (flags & TCP_ACK) {
        /* <SEQ=SEG.ACK><CTL=RST> */
        r->seq        = __builtin_bswap32(ack);
        r->ack_seq    = 0;
        r->doff_flags = __builtin_bswap16((5u << 12) | TCP_RST);
    } else {
        /* <SEQ=0><ACK=SEG.SEQ+SEG.LEN><CTL=RST,ACK> */
        r->seq        = 0;
        r->ack_seq    = __builtin_bswap32(seg_end);
        r->doff_flags = __builtin_bswap16((5u << 12) | TCP_RST | TCP_ACK);
    }
    r->check      = tcp_csum(daddr, saddr, r, sizeof(*r));
    ip4_output(saddr, IPPROTO_TCP, r, sizeof(*r));
}

/* ----- per-state handlers ---------------------------------------- */

static void tcp_in_listen(tcp_pcb_t *p, uint32_t saddr, uint32_t daddr,
                          uint16_t sport, uint16_t dport, uint32_t seq,
                          uint8_t flags) {
    /*
     * TCP-28: only a CLEAN SYN may open a connection.  The test was
     * `!(flags & TCP_SYN)`, so SYN|RST and SYN|ACK both spawned a child PCB
     * -- a segment that RFC 793 3.9 says a listener must answer with a RST
     * (SYN|ACK) or discard outright (anything with RST) instead created
     * half-open state, which is free work for an attacker and wrong for a
     * confused peer.  A SYN|FIN is equally nonsense here.
     */
    if (flags & TCP_RST) return;                 /* RFC 793: discard */
    if (!(flags & TCP_SYN)) return;
    if (flags & (TCP_ACK | TCP_FIN)) {
        /* An ACK arriving at a LISTEN socket refers to a connection that
         * does not exist here: RFC 793 says answer it with a RST.  The
         * caller has the header we need, so let the unmatched-segment path
         * below handle it by simply not creating a child. */
        return;
    }
    /* Respect the listen backlog.  Count children that already exist
     * for this listener (handshaking SYN_RECEIVED ones plus those
     * sitting fully-established in the accept queue); if that is at or
     * past the backlog, drop the SYN.  The peer's SYN retransmit will
     * get in once accept() drains a slot — and if it never does, the
     * peer's connect() times out, which is the correct backlog-full
     * behaviour instead of establishing an un-acceptable connection. */
    int pending = p->accept_count;
    for (tcp_pcb_t *q = g_tcp_pcbs; q; q = q->next)
        if (q->parent == p && q->state == TCP_SYN_RECEIVED)
            pending++;
    if (pending >= p->accept_cap)
        return;
    /* Spawn a child PCB in SYN_RECEIVED.  */
    tcp_pcb_t *c = (tcp_pcb_t *)kmalloc(sizeof(*c));
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->state   = TCP_SYN_RECEIVED;
    c->laddr   = daddr;
    c->raddr   = saddr;
    c->lport   = dport;
    c->rport   = sport;
    c->iss     = tcp_new_iss();
    c->snd_una = c->iss;
    c->snd_nxt = c->iss;
    c->rcv_nxt = seq + 1;
    c->rxbuf   = (uint8_t *)kmalloc(TCP_RING_LEN);
    if (!c->rxbuf) { kfree(c, sizeof(*c)); return; }
    c->rcv_wnd      = TCP_RING_LEN;
    c->recv_chan    = &c->rx_count;
    c->connect_chan = &c->state;
    c->accept_chan  = &c->accept_count;
    c->send_chan    = &c->snd_una;
    c->parent       = p;
    c->next         = g_tcp_pcbs;
    g_tcp_pcbs      = c;
    tcp_xmit_queue(c, TCP_SYN | TCP_ACK, NULL, 0);
}

static void tcp_in_syn_sent(tcp_pcb_t *p, uint32_t seq, uint32_t ack,
                            uint8_t flags) {
    if (flags & TCP_RST) {
        /*
         * TCP-09: a RST in SYN_SENT is acceptable ONLY if it acknowledges
         * our SYN (RFC 793 3.9 / RFC 5961 3.2).  It used to be honoured
         * unconditionally, so with TCP-07's predictable ports an off-path
         * attacker aborted any outbound connect by spraying RSTs -- and no
         * sequence number even had to be guessed, since a bare RST with no
         * ACK bit was equally effective.  The challenge-ACK logic was
         * already implemented for ESTABLISHED; SYN_SENT was missed.
         */
        if (!(flags & TCP_ACK)) return;          /* no ACK to validate */
        if ((int32_t)(ack - p->snd_una) <= 0 ||
            (int32_t)(ack - p->snd_nxt) > 0)
            return;                              /* not for our SYN */
        tcp_kill_pcb(p, ECONNREFUSED);
        return;
    }
    if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
        /* A71: RFC 793 SYN-SENT requires validating the ACK before
         * proceeding.  The segment's ACK must acknowledge our SYN,
         * i.e. ISS < SEG.ACK <= SND.NXT (in SYN_SENT snd_una == ISS
         * and snd_nxt == ISS+1).  An ack that is at/below snd_una or
         * beyond snd_nxt is unacceptable: reply with a reset
         * (<SEQ=SEG.ACK><CTL=RST>, as the RFC prescribes) and drop the
         * segment rather than establishing with a stale/forged send
         * state (which also left the SYN un-pruned and snd_una wrong). */
        if ((int32_t)(ack - p->snd_una) <= 0 ||
            (int32_t)(ack - p->snd_nxt) > 0) {
            tcp_xmit_raw(p, ack, TCP_RST, NULL, 0);
            return;
        }
        p->rcv_nxt = seq + 1;
        /* The peer's ACK confirms our SYN (validated acceptable above,
         * so it always advances snd_una).  Prune it from the unacked
         * queue and advance snd_una. */
        p->snd_una = ack;
        tcp_unacked_prune(p, ack);
        p->state = TCP_ESTABLISHED;
        p->last_ack = ack;          /* TCP-33: so the 1st duplicate counts */
        p->dup_ack  = 0;
        /* TCP-10: RFC 5681 3.1 -- initial window of 3*MSS (the IW=10 of
         * RFC 6928 is for well-provisioned paths; be conservative here),
         * and an effectively infinite ssthresh so the first loss sets it. */
        p->cwnd     = 3u * TCP_MSS;
        p->ssthresh = 0xFFFFFFFFu;
        tcp_send_ctl(p, TCP_ACK);
        sched_wakeup(p->connect_chan);
    }
}

static void tcp_in_syn_received(tcp_pcb_t *p, uint32_t ack, uint8_t flags) {
    /* NET-06: a RST for a half-open child aborts it.  Tear the child
     * down (tcp_kill_pcb -> TCP_CLOSED) instead of silently dropping the
     * segment; the retransmit-timer reaper then frees the never-accepted
     * PCB — see NET-04. */
    if (flags & TCP_RST) {
        tcp_kill_pcb(p, ECONNRESET);
        return;
    }
    if ((flags & TCP_ACK) && ack == p->snd_nxt) {
        if ((int32_t)(ack - p->snd_una) > 0) {
            p->snd_una = ack;
            tcp_unacked_prune(p, ack);
        }
        p->state = TCP_ESTABLISHED;
        p->last_ack = ack;          /* TCP-33: so the 1st duplicate counts */
        p->dup_ack  = 0;
        /* TCP-10: RFC 5681 3.1 -- initial window of 3*MSS (the IW=10 of
         * RFC 6928 is for well-provisioned paths; be conservative here),
         * and an effectively infinite ssthresh so the first loss sets it. */
        p->cwnd     = 3u * TCP_MSS;
        p->ssthresh = 0xFFFFFFFFu;
        /* Hand to parent's accept queue. */
        if (p->parent) {
            tcp_pcb_t *par = p->parent;
            if (par->accept_count < par->accept_cap) {
                par->accept_q[par->accept_count++] = p;
                sched_wakeup(par->accept_chan);
            }
        }
    }
}

/* True if `seq` falls inside the current receive window
 * [rcv_nxt, rcv_nxt + rcv_wnd).  rcv_wnd is the room we last advertised
 * (TCP_RING_LEN - rx_count); a zero window still accepts exactly rcv_nxt
 * so a probe/RST landing on the next expected byte is recognised. */
static int tcp_seq_in_rcv_window(const tcp_pcb_t *p, uint32_t seq) {
    uint32_t win = (uint32_t)TCP_RING_LEN - p->rx_count;
    if ((int32_t)(seq - p->rcv_nxt) < 0) return 0;          /* before window */
    if (win == 0) return seq == p->rcv_nxt;
    return (uint32_t)(seq - p->rcv_nxt) < win;              /* within window */
}

static void tcp_in_established(tcp_pcb_t *p, uint32_t seq, uint32_t ack,
                               uint8_t flags, const uint8_t *payload,
                               size_t dlen) {
    if (flags & TCP_RST) {
        /*
         * RFC 5961 §3.2: do not honour a RST solely because the
         * connection matches the 4-tuple — an off-path attacker who
         * guesses the tuple could otherwise tear down the connection
         * (or inject) with a forged RST.  Validate the sequence number:
         *   - outside the receive window: drop silently.
         *   - exactly RCV.NXT: in-window and acceptable -> reset.
         *   - in-window but not RCV.NXT: send a challenge ACK rather
         *     than resetting; only a RST that then arrives exactly at
         *     RCV.NXT is acted upon.
         */
        if (!tcp_seq_in_rcv_window(p, seq))
            return;
        if (seq != p->rcv_nxt) {
            tcp_send_ctl(p, TCP_ACK);   /* challenge ACK */
            return;
        }
        tcp_kill_pcb(p, ECONNRESET);
        return;
    }

    /* Data for a detached PCB has nowhere to land — the owning socket
     * is gone.  RST the peer so a process still writing to this
     * connection fails promptly instead of having its bytes silently
     * black-holed forever.  Pure ACK/FIN segments (dlen==0) still flow
     * through so the close handshake can finish. */
    if (p->detached && dlen > 0) {
        tcp_send_ctl(p, TCP_RST | TCP_ACK);
        tcp_kill_pcb(p, 0);
        return;
    }

    /* Accept data if seq matches rcv_nxt and we have room.  */
    if (dlen && seq == p->rcv_nxt) {
        uint32_t accept_n = dlen;
        if (accept_n > TCP_RING_LEN - p->rx_count)
            accept_n = TCP_RING_LEN - p->rx_count;
        for (uint32_t i = 0; i < accept_n; i++) {
            p->rxbuf[p->rx_head] = payload[i];
            p->rx_head = (p->rx_head + 1) % TCP_RING_LEN;
        }
        p->rx_count += accept_n;
        p->rcv_nxt  += accept_n;
        sched_wakeup(p->recv_chan);
    }

    /* Process ACK: prune unacked segments and run dup-ACK fast-retx. */
    if (flags & TCP_ACK) {
        /*
         * TCP-03: RFC 793 requires SND.UNA < SEG.ACK <= SND.NXT.  The upper
         * bound was missing here (it IS enforced in SYN_SENT), so an ACK for
         * data we never sent was accepted.  That did two things: it pruned
         * unacked segments the peer had never received, silently losing
         * data; and it drove snd_una past snd_nxt, making the
         * `snd_nxt - snd_una` in-flight calculation underflow to ~2^32 so
         * the send window read as permanently full -- an unrecoverable
         * write-side wedge from a single forged segment.
         */
        if ((int32_t)(ack - p->snd_nxt) > 0) {
            /* Unacceptable ACK.  RFC 793 3.9: in a synchronized state,
             * respond with an empty ACK carrying our current state and
             * drop the segment. */
            tcp_xmit_raw(p, p->snd_nxt, TCP_ACK, NULL, 0);
            return;
        }
        if ((int32_t)(ack - p->snd_una) > 0) {
            uint32_t acked = ack - p->snd_una;
            p->snd_una = ack;
            tcp_unacked_prune(p, ack);
            p->dup_ack = 0;
            p->last_ack = ack;
            /*
             * TCP-10: RFC 5681 3.1.  Below ssthresh we are in slow start
             * and cwnd grows by at most one MSS per ACK; above it we are in
             * congestion avoidance and grow by roughly MSS per RTT, which
             * is MSS*MSS/cwnd per ACK.  Clamp so cwnd cannot wrap.
             */
            if (p->cwnd == 0) p->cwnd = 3u * TCP_MSS;   /* pre-RFC PCBs */
            if (p->cwnd < p->ssthresh) {
                uint32_t inc = acked < TCP_MSS ? acked : TCP_MSS;
                if (p->cwnd < 0xFFFFFFFFu - inc) p->cwnd += inc;
            } else {
                uint32_t inc = ((uint32_t)TCP_MSS * TCP_MSS) / p->cwnd;
                if (inc == 0) inc = 1;
                if (p->cwnd < 0xFFFFFFFFu - inc) p->cwnd += inc;
            }
        } else if (ack == p->last_ack && p->unacked_head) {
            /*
             * Duplicate ACK -- the peer is still waiting on our oldest
             * unacked segment.  After TCP_DUP_ACK_FAST in a row, resend it
             * without waiting for the RTO.
             *
             * TCP-33: this used to need FOUR duplicates, not three.
             * last_ack started at 0 and was only assigned in the advancing
             * branch and the trailing else, so the FIRST duplicate fell into
             * that else and merely initialised last_ack instead of counting.
             * And dup_ack was never reset after firing, so only the exact
             * third duplicate ever triggered a fast retransmit -- a later
             * burst of duplicates did nothing at all and recovery fell back
             * to the RTO.  Seed last_ack when the connection is established
             * (below) so the first duplicate counts, and re-arm the counter
             * after firing so a subsequent burst can fire again.
             */
            p->dup_ack++;
            if (p->dup_ack >= TCP_DUP_ACK_FAST) {
                /* TCP-10: RFC 5681 3.2 -- three duplicate ACKs signal a
                 * loss.  ssthresh drops to half the flight size and cwnd
                 * follows; without this the retransmit went out at the same
                 * rate that caused the drop. */
                uint32_t flight = p->snd_nxt - p->snd_una;
                uint32_t half   = flight / 2;
                if (half < 2u * TCP_MSS) half = 2u * TCP_MSS;
                p->ssthresh = half;
                p->cwnd     = half;
                tcp_retx_head(p);
                p->dup_ack = 0;
            }
        } else {
            p->last_ack = ack;
        }
        /* An ACK frees in-flight bytes and carries the peer's latest
         * window (tcp_input() already stored it in snd_wnd) — wake any
         * sender parked in tcp_send() waiting for the window to open. */
        sched_wakeup(p->send_chan);
    }

    /* Process FIN — but only when it is in order.  A FIN occupies the
     * sequence number right after this segment's data (seq + dlen); it
     * may only be consumed once everything up to it has been received,
     * i.e. seq + dlen == rcv_nxt.  Acting on an out-of-order FIN (one
     * that raced ahead of still-in-flight data — common here because
     * the sender has no real send-window throttle and the receiver
     * drops ring overflow, recovered by retransmission) would tear the
     * receive side down early and silently truncate the stream. */
    /*
     * TCP-13: a RETRANSMITTED FIN -- one whose sequence number we have
     * already consumed -- must still be acknowledged, and TIME_WAIT re-armed.
     * The in-order test below requires seq + dlen == rcv_nxt, but once the
     * FIN is consumed rcv_nxt sits one PAST it, so a retransmit failed that
     * test, fell through to the `dlen &&` ACK path (false for a bare FIN)
     * and emitted nothing at all.  If our final ACK was lost the peer
     * retransmitted, got silence, and aborted with ETIMEDOUT instead of
     * closing cleanly.
     */
    if ((flags & TCP_FIN) && seq + (uint32_t)dlen + 1u == p->rcv_nxt) {
        if (p->state == TCP_TIME_WAIT)
            p->time_wait_until = get_ticks() + TCP_TIME_WAIT_TICKS;
        tcp_send_ctl(p, TCP_ACK);
        return;
    }

    if ((flags & TCP_FIN) && seq + (uint32_t)dlen == p->rcv_nxt) {
        p->rcv_nxt++;
        switch (p->state) {
        case TCP_ESTABLISHED:
            p->state = TCP_CLOSE_WAIT;
            sched_wakeup(p->recv_chan);
            tcp_send_ctl(p, TCP_ACK);
            break;
        case TCP_FIN_WAIT_1:
            /*
             * TCP-14: simultaneous close.  This used to move to TIME_WAIT on
             * ANY segment carrying the ACK bit -- which every segment after
             * the handshake does -- so the CLOSING branch was effectively
             * dead.  Our own FIN was then still sitting in unacked_head, but
             * the timer's TIME_WAIT arm `continue`s before the retransmit
             * check, so it was NEVER retransmitted: the PCB went CLOSED and
             * the peer was stranded in FIN_WAIT_2 / CLOSE_WAIT.  Only an ACK
             * that actually covers our FIN (and empties the unacked queue)
             * completes the close; otherwise this is a simultaneous close
             * and CLOSING is the correct state.
             */
            if ((flags & TCP_ACK) && ack == p->snd_nxt && !p->unacked_head) {
                p->state = TCP_TIME_WAIT;
                p->time_wait_until = get_ticks() + TCP_TIME_WAIT_TICKS;
            } else {
                p->state = TCP_CLOSING;
            }
            tcp_send_ctl(p, TCP_ACK);
            break;
        case TCP_FIN_WAIT_2:
            p->state = TCP_TIME_WAIT;
            p->time_wait_until = get_ticks() + TCP_TIME_WAIT_TICKS;
            tcp_send_ctl(p, TCP_ACK);
            break;
        case TCP_LAST_ACK:
            tcp_kill_pcb(p, 0);
            break;
        default:
            break;
        }
        return;
    }

    /* FIN_WAIT_1 → FIN_WAIT_2 on bare ACK of our FIN. */
    if (p->state == TCP_FIN_WAIT_1 && (flags & TCP_ACK) &&
        ack == p->snd_nxt && !p->unacked_head) {
        p->state = TCP_FIN_WAIT_2;
        /*
         * TCP-05: arm a deadline.  A FIN_WAIT_2 PCB is not TIME_WAIT and
         * has an empty unacked queue, so the timer tick skipped it and it
         * was never touched again -- if the peer simply never sent its own
         * FIN the PCB and its 32 KiB receive ring leaked forever.  That is
         * remote-driven and unbounded: open N connections, let this side
         * close, ACK the FIN and stop.  RFC 1122 4.2.3.6 permits this
         * timeout provided it is no shorter than 10 minutes when the
         * connection is otherwise idle; we are far more constrained on
         * memory than a general-purpose host, so use a shorter bound and
         * treat expiry as an abort of a peer that is not finishing its
         * half of the close.
         */
        p->fin_wait2_until = get_ticks() + TCP_FIN_WAIT_2_TICKS;
    }

    /*
     * TCP-24: a SYN arriving for a connection we hold in TIME_WAIT is a
     * client reconnecting on the same 4-tuple.  It used to be silently
     * ignored, so the client was blackholed for the whole TIME_WAIT
     * (now 60 s, which makes this far more visible than it was at 1 s).
     * RFC 1122 4.2.2.13 permits accepting a new incarnation when its
     * sequence number is beyond what we have seen; we do not implement
     * that resurrection, so send the challenge ACK RFC 5961 4 prescribes.
     * The peer then learns the connection is not usable and resets, rather
     * than retrying into silence until its connect() times out.
     */
    if (p->state == TCP_TIME_WAIT && (flags & TCP_SYN)) {
        tcp_send_ctl(p, TCP_ACK);
        return;
    }

    /* LAST_ACK → CLOSED when the peer ACKs our FIN.  The final segment
     * of a passive close is a bare ACK (no FIN), so this is the only
     * path that completes LAST_ACK — without it the PCB lingered there
     * forever and leaked. */
    if (p->state == TCP_LAST_ACK && (flags & TCP_ACK) &&
        ack == p->snd_nxt && !p->unacked_head) {
        tcp_kill_pcb(p, 0);
        return;
    }

    /* CLOSING → TIME_WAIT when the peer ACKs our FIN (simultaneous
     * close: both sides sent FIN before either's was acknowledged). */
    if (p->state == TCP_CLOSING && (flags & TCP_ACK) &&
        ack == p->snd_nxt && !p->unacked_head) {
        p->state = TCP_TIME_WAIT;
        p->time_wait_until = get_ticks() + TCP_TIME_WAIT_TICKS;
        return;
    }

    /* Always ACK in-window data. */
    if (dlen && (p->state == TCP_ESTABLISHED ||
                 p->state == TCP_FIN_WAIT_1 ||
                 p->state == TCP_FIN_WAIT_2)) {
        tcp_send_ctl(p, TCP_ACK);
    }
}

void tcp_input(uint32_t saddr, uint32_t daddr,
               const uint8_t *seg, size_t len);

void tcp_input(uint32_t saddr, uint32_t daddr,
               const uint8_t *seg, size_t len)
{
    if (len < sizeof(struct tcphdr)) return;
    const struct tcphdr *th = (const struct tcphdr *)seg;
    uint16_t doff_flags = __builtin_bswap16(th->doff_flags);
    size_t   hlen       = ((doff_flags >> 12) & 0xF) * 4;
    if (hlen < sizeof(*th) || hlen > len) return;
    uint8_t  flags = (uint8_t)(doff_flags & 0xFF);
    uint16_t sport = __builtin_bswap16(th->source);
    uint16_t dport = __builtin_bswap16(th->dest);
    uint32_t seq   = __builtin_bswap32(th->seq);
    uint32_t ack   = __builtin_bswap32(th->ack_seq);
    size_t   dlen  = len - hlen;
    const uint8_t *payload = seg + hlen;

    /*
     * TCP-02: verify the segment checksum before acting on ANY of it.
     * tcp_csum() existed but had only output callers, and ip4_input
     * validates the IP header only -- which covers no payload -- so every
     * received seq/ack/flag/window/data byte was accepted with no
     * end-to-end check at all.  A single bit flip delivered corrupt stream
     * data or turned a data segment into a RST, and a blind off-path
     * attacker had one fewer field to get right.
     *
     * A checksum of zero means "not computed" for UDP but NOT for TCP,
     * where it is mandatory, so a zero field is simply a wrong checksum
     * unless the segment genuinely sums to zero -- which the standard
     * one's-complement check below handles correctly either way.
     */
    if (tcp_csum(saddr, daddr, seg, len) != 0) {
        /* Silently drop: replying would let a corrupt segment elicit
         * traffic, and RFC 793 requires no response to a bad checksum. */
        return;
    }

    tcp_pcb_t *p = tcp_find(saddr, sport, daddr, dport);
    if (!p) {
        tcp_send_rst(saddr, daddr, th, flags, seq, ack, dlen);
        return;
    }

    /*
     * TCP-04: only an ACK-bearing segment may move the send window, and
     * only when its acknowledgement is one we can actually accept.  This
     * used to take snd_wnd from EVERY segment, before the state dispatch,
     * with no ACK bit and no acceptability test -- so one forged packet
     * advertising window 0 parked the sender indefinitely, and a reordered
     * old segment "un-updated" the window to a stale value.  The SYN_SENT
     * and LISTEN states run their own handlers below and take the window
     * from the segment that establishes the connection.
     */
    if ((flags & TCP_ACK) && p->state != TCP_LISTEN && p->state != TCP_SYN_SENT) {
        /* Window updates track the highest ACK seen, so an old duplicate
         * cannot walk the window backwards. */
        if ((int32_t)(ack - p->snd_una) >= 0 &&
            (int32_t)(ack - p->snd_nxt) <= 0) {
            p->snd_wnd = __builtin_bswap16(th->window);
        }
    } else if (p->state == TCP_LISTEN || p->state == TCP_SYN_SENT) {
        p->snd_wnd = __builtin_bswap16(th->window);
    }

    switch (p->state) {
    case TCP_LISTEN:
        tcp_in_listen(p, saddr, daddr, sport, dport, seq, flags);
        return;
    case TCP_SYN_SENT:
        tcp_in_syn_sent(p, seq, ack, flags);
        return;
    case TCP_SYN_RECEIVED:
        tcp_in_syn_received(p, ack, flags);
        return;
    case TCP_ESTABLISHED:
    case TCP_FIN_WAIT_1:
    case TCP_FIN_WAIT_2:
    case TCP_CLOSE_WAIT:
    case TCP_CLOSING:
    case TCP_LAST_ACK:
    case TCP_TIME_WAIT:
        tcp_in_established(p, seq, ack, flags, payload, dlen);
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Public socket-layer API                                            */
/* ------------------------------------------------------------------ */

tcp_pcb_t *tcp_alloc(void) {
    tcp_pcb_t *p = (tcp_pcb_t *)kmalloc(sizeof(*p));
    if (!p) return NULL;
    memset(p, 0, sizeof(*p));
    p->state = TCP_CLOSED;
    p->rxbuf = (uint8_t *)kmalloc(TCP_RING_LEN);
    if (!p->rxbuf) { kfree(p, sizeof(*p)); return NULL; }
    p->rcv_wnd      = TCP_RING_LEN;
    p->recv_chan    = &p->rx_count;
    p->connect_chan = &p->state;
    p->accept_chan  = &p->accept_count;
    p->send_chan    = &p->snd_una;
    /* Link into the global PCB list atomically vs. RX. */
    uint32_t f = tcp_lock();
    p->next = g_tcp_pcbs;
    g_tcp_pcbs = p;
    tcp_unlock(f);
    tcp_timer_ensure();
    return p;
}

void tcp_free(tcp_pcb_t *p) {
    if (!p) return;
    /* Unlink under the lock so an RX interrupt can't be walking
     * g_tcp_pcbs (or holding a pointer it just tcp_find()'d) while
     * we splice the node out and free it. */
    uint32_t f = tcp_lock();
    tcp_pcb_t **link = &g_tcp_pcbs;
    while (*link && *link != p) link = &(*link)->next;
    if (*link == p) *link = p->next;
    /* If this is a listener, orphan any SYN_RECEIVED / not-yet-accepted
     * children so a later segment for one of them can't dereference a
     * freed parent in tcp_in_syn_received(). */
    if (p->listen)
        for (tcp_pcb_t *c = g_tcp_pcbs; c; c = c->next)
            if (c->parent == p) c->parent = NULL;
    tcp_unacked_free_all(p);
    tcp_unlock(f);
    if (p->rxbuf)    kfree(p->rxbuf, TCP_RING_LEN);
    if (p->accept_q) kfree(p->accept_q, sizeof(tcp_pcb_t *) * p->accept_cap);
    kfree(p, sizeof(*p));
}

int tcp_bind(tcp_pcb_t *p, uint32_t laddr, uint16_t lport) {
    p->laddr = laddr;
    p->lport = lport;
    return 0;
}

int tcp_listen(tcp_pcb_t *p, int backlog) {
    if (backlog < 1)  backlog = 1;
    if (backlog > 32) backlog = 32;
    /*
     * TCP-32: a second listen() overwrote p->accept_q with a fresh
     * allocation without freeing the old one -- leaking 8*accept_cap and
     * orphaning any children already queued on it, which then never got
     * accepted or reaped.  POSIX allows listen() on an already-listening
     * socket purely to change the backlog, so keep the queue when it is
     * already big enough and otherwise migrate the pending children.
     */
    if (p->accept_q) {
        if (backlog <= p->accept_cap) {
            p->accept_cap = backlog;
            if (p->accept_count > backlog) p->accept_count = backlog;
            p->state  = TCP_LISTEN;
            p->listen = 1;
            return 0;
        }
        tcp_pcb_t **nq = (tcp_pcb_t **)kmalloc(sizeof(tcp_pcb_t *) * backlog);
        if (!nq) return -ENOMEM;
        for (int i = 0; i < p->accept_count; i++) nq[i] = p->accept_q[i];
        kfree(p->accept_q, sizeof(tcp_pcb_t *) * p->accept_cap);
        p->accept_q   = nq;
        p->accept_cap = backlog;
        p->state      = TCP_LISTEN;
        p->listen     = 1;
        return 0;
    }
    p->accept_q = (tcp_pcb_t **)kmalloc(sizeof(tcp_pcb_t *) * backlog);
    if (!p->accept_q) return -ENOMEM;
    p->accept_cap = backlog;
    p->state      = TCP_LISTEN;
    p->listen     = 1;
    return 0;
}

/* Kick off the SYN.  Common to blocking and non-blocking connect.  */
/*
 * TCP-07: pick a local port that is random and demonstrably free.
 *
 * This was `static uint16_t next_eph = 32768; p->lport = ++next_eph;` --
 * sequential (so trivially predictable, which is half of what makes off-path
 * injection practical), non-atomic, wrapping to 0 and then into the reserved
 * range after 32766 connections, and never checked against the PCB list, so
 * two concurrent connect()s could share a 4-tuple and tcp_find would deliver
 * both streams to whichever PCB came first in the list.  af_inet.c fixed
 * exactly this class for UDP (NET-07 / UDP-05); TCP bypassed the helper.
 *
 * Draw a candidate from the CSPRNG in the IANA dynamic range and reject it
 * if any live PCB already holds it; sweep linearly from there so a busy
 * system still terminates.  Returns 0 when the range is exhausted.
 */
static int tcp_port_taken(const tcp_pcb_t *self, uint16_t port) {
    for (tcp_pcb_t *o = g_tcp_pcbs; o; o = o->next) {
        if (o == self || o->state == TCP_CLOSED) continue;
        if (o->lport == port) return 1;
    }
    return 0;
}

static uint16_t tcp_alloc_ephemeral(const tcp_pcb_t *self) {
    uint32_t r = 0;
    if (random_get_bytes(&r, sizeof(r)) != 0)
        r = (uint32_t)get_ticks();
    uint16_t base = (uint16_t)(49152u + (r % (65536u - 49152u)));
    for (uint32_t i = 0; i < (65536u - 49152u); i++) {
        uint16_t port = (uint16_t)(49152u + ((base - 49152u + i) % (65536u - 49152u)));
        if (!tcp_port_taken(self, port)) return port;
    }
    return 0;
}

static void tcp_connect_start(tcp_pcb_t *p, uint32_t raddr, uint16_t rport) {
    if (!p->lport) p->lport = tcp_alloc_ephemeral(p);
    if (!p->laddr) {
        /* Pick a source IP based on the destination.  127/8 traffic
         * MUST be sourced from a loopback address — otherwise the
         * reply comes back through loopback with saddr=daddr=
         * 127.0.0.1 and tcp_find can't match a PCB whose laddr is,
         * say, 10.0.0.5.  Same shape for IPv6 ::1 once we wire it.  */
        int want_lo = ((raddr & 0xFF) == 127);
        for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
            int is_lo = !!(d->flags & NETDEV_IFF_LOOPBACK);
            if (is_lo != want_lo) continue;
            if (d->ip4_addr) { p->laddr = d->ip4_addr; break; }
        }
    }
    p->raddr   = raddr;
    p->rport   = rport;
    p->iss     = tcp_new_iss();
    p->snd_una = p->iss;
    p->snd_nxt = p->iss;
    p->state   = TCP_SYN_SENT;
    /* Queue the SYN — the retx timer will resend it on RTO if the
     * server didn't get it.  */
    tcp_xmit_queue(p, TCP_SYN, NULL, 0);
}

int tcp_connect(tcp_pcb_t *p, uint32_t raddr, uint16_t rport) {
    int ret;
    tcp_hold(p);                                    /* TCP-01 */
    tcp_connect_start(p, raddr, rport);
    /* Wait — the retransmit kthread enforces the overall timeout via
     * TCP_MAX_RETX.  Loop on state changes.  */
    for (;;) {
        if (p->state == TCP_ESTABLISHED) { ret = 0; break; }
        if (p->state == TCP_CLOSED) {
            ret = p->so_error ? -p->so_error : -ECONNREFUSED;
            break;
        }
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep_until(p->connect_chan, get_ticks() + TCP_SLEEP_POLL);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask) {
            ret = -EINTR;
            break;
        }
    }
    tcp_unhold(p);
    return ret;
}

int tcp_connect_nb(tcp_pcb_t *p, uint32_t raddr, uint16_t rport) {
    tcp_connect_start(p, raddr, rport);
    if (p->state == TCP_ESTABLISHED) return 0;
    if (p->state == TCP_CLOSED) {
        return p->so_error ? -p->so_error : -ECONNREFUSED;
    }
    return -EINPROGRESS;
}

int tcp_poll(tcp_pcb_t *p, short events, void **wait_chan) {
    short revents = 0;
    if (!p) return POLLNVAL;
    if (p->state == TCP_LISTEN) {
        if ((events & POLLIN) && p->accept_count > 0) revents |= POLLIN;
        if (wait_chan && !(revents & POLLIN)) *wait_chan = p->accept_chan;
        return revents;
    }
    if (p->state == TCP_SYN_SENT || p->state == TCP_SYN_RECEIVED) {
        if (wait_chan) *wait_chan = p->connect_chan;
        return 0;
    }
    if (p->state == TCP_CLOSED) {
        return (events & POLLOUT ? POLLOUT : 0) |
               (events & POLLIN ? POLLIN : 0) | POLLHUP | POLLERR;
    }
    if (events & POLLIN) {
        if (p->rx_count > 0) {
            revents |= POLLIN;
        } else if (p->state == TCP_CLOSE_WAIT || p->state == TCP_CLOSING ||
                   p->state == TCP_LAST_ACK   || p->state == TCP_TIME_WAIT) {
            /*
             * Peer has sent FIN (receive side closed) and the receive
             * buffer is drained: a read() returns 0 (EOF) rather than
             * blocking, so the socket IS readable.  POSIX and Linux report
             * POLLIN here (not just POLLHUP).  libtirpc's svc_vc read_vc
             * loops `while ((revents & POLLIN) == 0)` and only treats a
             * poll *timeout* as fatal, so a POLLHUP-without-POLLIN fd makes
             * poll() return >0 forever and the RPC server spins at 100%
             * the moment a client disconnects — wedging ttsession /
             * rpc.ttdbserver and hanging the whole CDE/ToolTalk startup.
             */
            revents |= POLLIN;
        }
    }
    if (events & POLLOUT) {
        /* No tx buffering yet; treat as always-ready.  */
        revents |= POLLOUT;
    }
    if (p->state == TCP_CLOSE_WAIT) revents |= POLLHUP;
    /* If the caller asked for POLLIN but we don't have data yet,
     * advertise recv_chan so the poll layer can sleep on the right
     * queue.  Previously gated on `!revents`, which never fired
     * because POLLOUT-always-ready left revents non-zero — caller
     * then fell back to a different wait channel and missed the
     * recv wakeup entirely (the inetutils-telnet symptom). */
    if (wait_chan && (events & POLLIN) && !(revents & (POLLIN | POLLHUP)))
        *wait_chan = p->recv_chan;
    return revents;
}

/* True iff the PCB is a listening socket — lets the socket layer
 * reject accept() on a non-listening fd with EINVAL. */
int tcp_is_listening(const tcp_pcb_t *p) {
    return p && p->state == TCP_LISTEN;
}

/* Dequeue one established connection.  With nonblock set, returns NULL
 * immediately when the queue is empty (the caller maps that to
 * EAGAIN); otherwise blocks.  NULL from the blocking path means the
 * wait was interrupted by a signal. */
tcp_pcb_t *tcp_accept(tcp_pcb_t *listen_p, int nonblock) {
    tcp_pcb_t *ret = NULL;
    tcp_hold(listen_p);                             /* TCP-01 */
    for (;;) {
        /* accept_q / accept_count are appended by tcp_in_syn_received
         * in IRQ context — dequeue with IRQs off so the shift-down
         * can't race an enqueue. */
        uint32_t f = tcp_lock();
        if (listen_p->accept_count > 0) {
            tcp_pcb_t *c = listen_p->accept_q[0];
            for (int i = 1; i < listen_p->accept_count; i++)
                listen_p->accept_q[i - 1] = listen_p->accept_q[i];
            listen_p->accept_count--;
            c->parent = NULL;
            tcp_unlock(f);
            ret = c;
            break;
        }
        /*
         * TCP-19: a listener that has been closed can never produce another
         * connection, so waiting on it is waiting forever.  tcp_accept
         * tested only accept_count and tcp_close's LISTEN arm wakes nobody
         * on accept_chan, so a thread blocked in accept() when the socket
         * was closed from another thread never returned.  tcp_recv_nb
         * already handles the equivalent case.
         */
        if (listen_p->state != TCP_LISTEN || listen_p->detached) {
            tcp_unlock(f);
            break;
        }
        tcp_unlock(f);
        if (nonblock) break;
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep_until(listen_p->accept_chan, get_ticks() + TCP_SLEEP_POLL);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask)
            break;
    }
    tcp_unhold(listen_p);
    return ret;
}

static ssize_t tcp_send_impl(tcp_pcb_t *p, const void *buf, size_t len, int nonblock) {
    const uint8_t *b = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        if (p->state != TCP_ESTABLISHED && p->state != TCP_CLOSE_WAIT) {
            /* A connection that was up and then failed (RST ->
             * ECONNRESET, RTO -> ETIMEDOUT) reports EPIPE; one that
             * was never connected reports ENOTCONN. */
            if (sent) return (ssize_t)sent;
            return p->so_error ? -EPIPE : -ENOTCONN;
        }

        /* Flow control: the unacknowledged bytes in flight
         * (snd_nxt - snd_una) must never exceed the receive window
         * the peer last advertised.  Without this the sender blasted
         * the whole buffer into the unacked FIFO at once; the peer's
         * ring overflowed, the excess was dropped, and the transfer
         * limped along on retransmissions. */
        uint32_t in_flight = p->snd_nxt - p->snd_una;
        /*
         * TCP-10: the sender is bounded by min(cwnd, peer window).  Only the
         * peer's window was consulted, so nothing throttled us on a
         * congested path.  cwnd == 0 means a PCB that predates establishment
         * (or a pre-RFC one); treat that as "not yet limited".
         */
        uint32_t wnd       = p->snd_wnd;
        if (p->cwnd && p->cwnd < wnd) wnd = p->cwnd;
        uint32_t avail     = (wnd > in_flight) ? (wnd - in_flight) : 0;

        if (avail == 0) {
            /* Non-blocking sender: return what we managed to send (or
             * EAGAIN) instead of parking.  A single-threaded nonblocking
             * pump must be able to return from write() and go read() —
             * otherwise it can never drain the peer to reopen the window
             * and the transfer self-deadlocks. */
            if (nonblock) return sent ? (ssize_t)sent : -EAGAIN;
            if (in_flight == 0) {
                /* Zero window, nothing outstanding — emit a one-byte
                 * persist probe.  Its RTO retransmissions keep
                 * prodding the peer until it re-advertises a window,
                 * so no separate persist timer is needed. */
                int rc = tcp_xmit_queue(p, TCP_ACK | TCP_PSH, b + sent, 1);
                if (rc < 0) return sent ? (ssize_t)sent : rc;
                sent += 1;
                continue;
            }
            /* Data already in flight — its own retransmissions probe
             * the peer; wait for an ACK to reopen the window. */
            current_thread->flags |= THREAD_F_INTERRUPTIBLE;
            sched_sleep_until(p->send_chan, get_ticks() + TCP_SLEEP_POLL);
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
            if (current_thread->sig_pending & ~current_thread->sig_mask)
                return sent ? (ssize_t)sent : -EINTR;
            continue;
        }

        size_t chunk = len - sent;
        if (chunk > TCP_MSS)         chunk = TCP_MSS;
        if (chunk > avail)           chunk = avail;
        int rc = tcp_xmit_queue(p, TCP_ACK | TCP_PSH, b + sent, chunk);
        if (rc < 0) return sent ? (ssize_t)sent : rc;
        sent += chunk;
    }
    return (ssize_t)sent;
}

ssize_t tcp_send(tcp_pcb_t *p, const void *buf, size_t len) {
    return tcp_send_impl(p, buf, len, /*nonblock=*/0);
}
ssize_t tcp_send_nb(tcp_pcb_t *p, const void *buf, size_t len) {
    return tcp_send_impl(p, buf, len, /*nonblock=*/1);
}

size_t tcp_recv_avail(const tcp_pcb_t *p) {
    return p ? (size_t)p->rx_count : 0;
}

ssize_t tcp_recv_nb(tcp_pcb_t *p, void *buf, size_t len) {
    /* The rx ring (rxbuf, rx_head, rx_tail, rx_count) is written by
     * tcp_in_established() in IRQ context.  Drain it with IRQs off
     * so a segment landing mid-copy can't desync head/tail/count. */
    uint32_t lf = tcp_lock();
    if (p->shut_rd) {                /* shutdown(SHUT_RD): forced EOF */
        tcp_unlock(lf);
        return 0;
    }
    if (p->rx_count > 0) {
        size_t prev_count = p->rx_count;
        size_t n = p->rx_count < len ? p->rx_count : len;
        uint8_t *b = (uint8_t *)buf;
        for (size_t i = 0; i < n; i++) {
            b[i] = p->rxbuf[p->rx_tail];
            p->rx_tail = (p->rx_tail + 1) % TCP_RING_LEN;
        }
        p->rx_count -= n;
        tcp_unlock(lf);
        /* Window-update ACK: peer may have been throttled by our
         * shrinking window.  If we've freed at least one MSS of
         * receive space, fire a bare ACK so the peer knows it can
         * resume.  Without this the connection stalls until the
         * peer's zero-window-probe timer fires (10+ seconds),
         * which looks like a hang in interactive curl downloads.
         * Matches BSD's silly-window-syndrome avoidance shape.  */
        size_t old_wnd = TCP_RING_LEN - prev_count;
        size_t new_wnd = TCP_RING_LEN - p->rx_count;
        if (new_wnd >= old_wnd + TCP_MSS &&
            (p->state == TCP_ESTABLISHED ||
             p->state == TCP_FIN_WAIT_1  ||
             p->state == TCP_FIN_WAIT_2)) {
            tcp_send_ctl(p, TCP_ACK);
        }
        return (ssize_t)n;
    }
    /* EOF once the peer has closed its send side and the ring is
     * drained — every state reachable after the peer's FIN. */
    if (p->state == TCP_CLOSE_WAIT || p->state == TCP_CLOSING ||
        p->state == TCP_LAST_ACK   || p->state == TCP_TIME_WAIT ||
        p->state == TCP_CLOSED) {
        tcp_unlock(lf);
        return 0;
    }
    tcp_unlock(lf);
    return -EAGAIN;
}

ssize_t tcp_recv(tcp_pcb_t *p, void *buf, size_t len) {
    ssize_t ret;
    tcp_hold(p);                                    /* TCP-01 */
    for (;;) {
        ssize_t r = tcp_recv_nb(p, buf, len);
        if (r != -EAGAIN) { ret = r; break; }
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep_until(p->recv_chan, get_ticks() + TCP_SLEEP_POLL);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask) {
            ret = -EINTR;
            break;
        }
    }
    tcp_unhold(p);
    return ret;
}

/* MSG_PEEK: copy up to len bytes from the rx ring WITHOUT consuming them,
 * so a follow-up recv() still sees the same data.  Same EOF/EAGAIN
 * signalling as tcp_recv_nb. */
ssize_t tcp_peek_nb(tcp_pcb_t *p, void *buf, size_t len) {
    uint32_t lf = tcp_lock();
    if (p->shut_rd) { tcp_unlock(lf); return 0; }
    if (p->rx_count > 0) {
        size_t n = p->rx_count < len ? p->rx_count : len;
        uint8_t *b = (uint8_t *)buf;
        uint32_t tail = p->rx_tail;
        for (size_t i = 0; i < n; i++) {
            b[i] = p->rxbuf[tail];
            tail = (tail + 1) % TCP_RING_LEN;
        }
        tcp_unlock(lf);
        return (ssize_t)n;
    }
    if (p->state == TCP_CLOSE_WAIT || p->state == TCP_CLOSING ||
        p->state == TCP_LAST_ACK   || p->state == TCP_TIME_WAIT ||
        p->state == TCP_CLOSED) {
        tcp_unlock(lf);
        return 0;
    }
    tcp_unlock(lf);
    return -EAGAIN;
}

ssize_t tcp_peek(tcp_pcb_t *p, void *buf, size_t len) {
    for (;;) {
        ssize_t r = tcp_peek_nb(p, buf, len);
        if (r != -EAGAIN) return r;
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep_until(p->recv_chan, get_ticks() + TCP_SLEEP_POLL);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask)
            return -EINTR;
    }
}

int tcp_take_so_error(tcp_pcb_t *p) {
    if (!p) return 0;
    int err = p->so_error;
    p->so_error = 0;
    return err;
}

int tcp_close(tcp_pcb_t *p) {
    if (!p) return 0;
    /* Defensive: a tcp_pcb_t always lives in the kernel direct map
     * (>= 0xC0000000).  A low/garbage pointer here means the owning
     * socket's ->tcp field was corrupted; dereferencing it would
     * triple-fault.  Drop it instead so one bad socket can't take
     * the kernel down.  (The corruptor itself is a separate bug —
     * tracked via tests/lib/c/test_tcp.c, which reproduces it.) */
    if ((uintptr_t)p < 0xC0000000u) {
        kprintf("tcp_close: refusing bogus pcb %p — socket ->tcp corrupted\n", p);
        return 0;
    }
    /* Snapshot + transition state under the lock so a concurrent RX
     * (which may itself transition state or free the PCB) can't
     * interleave.  tcp_free for the already-dead states is done
     * inside the lock; the FIN-emitting paths drop the lock before
     * tcp_xmit_queue, which is itself lock-bracketed. */
    uint32_t f = tcp_lock();
    /* The owning socket is being destroyed — mark the PCB orphaned so
     * the timer reaps it once it reaches CLOSED, and so any data that
     * still arrives for it gets a RST (there is no socket to take it). */
    p->detached = 1;
    int st = p->state;
    switch (st) {
    case TCP_ESTABLISHED:
        p->state = TCP_FIN_WAIT_1;
        tcp_unlock(f);
        tcp_xmit_queue(p, TCP_FIN | TCP_ACK, NULL, 0);
        break;
    case TCP_CLOSE_WAIT:
        p->state = TCP_LAST_ACK;
        tcp_unlock(f);
        tcp_xmit_queue(p, TCP_FIN | TCP_ACK, NULL, 0);
        break;
    case TCP_LISTEN: {
        /* Closing a listener: every child PCB it spawned is now an
         * orphan.  Children sitting fully-established in the accept
         * queue (handshake completed, never accept()ed) and children
         * still mid-handshake (SYN_RECEIVED) are owned by this parent
         * and are NOT otherwise detached — without explicit teardown
         * they linger forever (established ones never reach CLOSED, so
         * the timer never reaps them) and their peers believe the
         * connection is up.  Reset and detach each so the peer is told
         * the connection is gone and the timer frees the PCB.
         *
         * A45: the RST is emitted INLINE, under the lock, exactly like
         * the retransmit timer's inline transmit (NET-02).  NET-05
         * makes ip4_output() non-sleeping while interrupts are disabled
         * (an ARP miss fires the request and drops the frame instead of
         * yielding), so tcp_xmit_raw() cannot block here.  The previous
         * design collected the children, dropped the lock, then called
         * tcp_send_ctl() on each after the unlock — but tcp_kill_pcb()
         * transitions each child to CLOSED+detached, precisely the state
         * the timer reaper (an ordinary preemptible kthread) tcp_free()s
         * on its next wake.  A preemption in the gap between the unlock
         * and the RST sends could free a child before its pointer was
         * dereferenced: a use-after-free.  Sending under the lock closes
         * the window (and drops the old 32-child cap). */
        for (tcp_pcb_t *q = g_tcp_pcbs; q; q = q->next) {
            if (q->parent != p) continue;
            q->parent   = NULL;     /* drop the dangling back-pointer */
            q->detached = 1;        /* timer reaps once CLOSED */
            int qst = q->state;
            /* Only an established/half-open child has a peer that needs
             * telling; a CLOSED/TIME_WAIT one is already torn down. */
            if (qst != TCP_CLOSED && qst != TCP_TIME_WAIT)
                tcp_send_ctl(q, TCP_RST | TCP_ACK);
            tcp_kill_pcb(q, ECONNRESET);   /* -> CLOSED, wakes waiters */
        }
        p->accept_count = 0;
        p->state = TCP_CLOSED;
        tcp_unlock(f);
        break;
    }
    case TCP_SYN_SENT:
    case TCP_SYN_RECEIVED:
        /* No established peer to FIN — drop straight to CLOSED.  The
         * reap is deferred to the timer (rather than an inline
         * tcp_free) so it cannot race a concurrent RX walk. */
        p->state = TCP_CLOSED;
        tcp_unlock(f);
        break;
    default:
        /* CLOSED, or already mid-close (FIN_WAIT, CLOSING, LAST_ACK,
         * TIME_WAIT) — the handshake finishes on its own and the
         * detached flag now marks it for reaping once it reaches
         * CLOSED. */
        tcp_unlock(f);
        break;
    }
    return 0;
}

/*
 * tcp_shutdown_wr — shutdown(fd, SHUT_WR): send a FIN to close the
 * send direction while the socket stays open for reading.  Unlike
 * tcp_close() the PCB is NOT detached — userspace still owns it and
 * may keep reading until the peer closes too.
 */
int tcp_shutdown_wr(tcp_pcb_t *p) {
    if (!p) return -ENOTCONN;
    uint32_t f = tcp_lock();
    switch (p->state) {
    case TCP_ESTABLISHED:
        p->state = TCP_FIN_WAIT_1;
        tcp_unlock(f);
        tcp_xmit_queue(p, TCP_FIN | TCP_ACK, NULL, 0);
        return 0;
    case TCP_CLOSE_WAIT:
        p->state = TCP_LAST_ACK;
        tcp_unlock(f);
        tcp_xmit_queue(p, TCP_FIN | TCP_ACK, NULL, 0);
        return 0;
    default:
        /* SYN_SENT has nothing established to FIN; the rest already
         * sent their FIN.  Idempotent either way. */
        tcp_unlock(f);
        return 0;
    }
}

/*
 * tcp_shutdown_rd — shutdown(fd, SHUT_RD): close the receive
 * direction.  recv() returns EOF from now on; a reader already
 * blocked in tcp_recv() is woken so it observes the new state.
 */
int tcp_shutdown_rd(tcp_pcb_t *p) {
    if (!p) return -ENOTCONN;
    p->shut_rd = 1;
    sched_wakeup(p->recv_chan);
    return 0;
}

/*
 * tcp_endpoints — expose a PCB's local/remote address+port to the
 * socket layer, which only holds an opaque tcp_pcb_t pointer.
 * laddr/raddr come out network-byte-order; lport/rport host-order.
 * Any out-pointer may be NULL.
 */
void tcp_endpoints(const tcp_pcb_t *p,
                   uint32_t *laddr, uint16_t *lport,
                   uint32_t *raddr, uint16_t *rport) {
    if (!p) return;
    if (laddr) *laddr = p->laddr;
    if (lport) *lport = p->lport;
    if (raddr) *raddr = p->raddr;
    if (rport) *rport = p->rport;
}
