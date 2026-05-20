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

#include <net/inet.h>
#include <sys/netdev.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/console.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include "../arch/i386/intr.h"

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
#define TCP_RTO_TICKS      64            /* ~500ms at HZ=128 */
#define TCP_MAX_RETX       6
#define TCP_TIMER_PERIOD   32            /* ~250ms kthread wake interval */
#define TCP_TIME_WAIT_TICKS 128          /* 1s */
#define TCP_DUP_ACK_FAST   3             /* fast-retx trigger */
/* Safety-net poll interval for the blocking recv/accept/connect waits.
 * sched_sleep() is not race-free against sched_wakeup() — a wakeup that
 * fires between the readiness re-check and the sleep is lost.  Sleeping
 * with this deadline guarantees the waiter re-checks even if its wakeup
 * was missed, turning a permanent wedge into at most this much latency.
 * ~64ms at HZ=128; the wakeup still drives the common fast path. */
#define TCP_SLEEP_POLL     8

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
    /* SO_ERROR (cleared by getsockopt).  */
    int       so_error;
    /* Backlog for LISTEN sockets */
    struct tcp_pcb **accept_q;
    int        accept_cap, accept_count;
    /* Sleep channels */
    void      *connect_chan;
    void      *recv_chan;
    void      *accept_chan;
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
    /* Linked list */
    struct tcp_pcb *next;
} tcp_pcb_t;

static tcp_pcb_t *g_tcp_pcbs;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static uint32_t tcp_iss_seed = 0xC0DE1234u;
static uint32_t tcp_new_iss(void) {
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
    th->check = tcp_csum(p->laddr, p->raddr, buf, sizeof(*th) + dlen);
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
}

static void tcp_timer_tick(uint64_t now) {
    /* The timer kthread is the single reaper of orphaned PCBs.  Walk
     * the list with IRQs off (tcp_lock) so an RX interrupt can't free
     * or splice a node underneath us; collect the retransmit victims
     * and do the actual transmits after dropping the lock, since
     * tcp_xmit_raw -> ip4_output may need IRQs enabled (ARP wait). */
    tcp_pcb_t *retx_list[32];
    int nretx = 0;
    uint32_t f = tcp_lock();
    for (tcp_pcb_t *p = g_tcp_pcbs, *next; p; p = next) {
        next = p->next;
        if (p->state == TCP_CLOSED) {
            /* Terminal.  Reap if orphaned — tcp_find() never returns a
             * CLOSED PCB, so no RX path can be holding this pointer. */
            if (p->detached) tcp_free(p);
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
        if (now - head->sent_tick < TCP_RTO_TICKS) continue;
        if (head->retx >= TCP_MAX_RETX) {
            tcp_kill_pcb(p, ETIMEDOUT);
            continue;
        }
        if (nretx < 32) retx_list[nretx++] = p;
    }
    tcp_unlock(f);
    for (int i = 0; i < nretx; i++) tcp_retx_head(retx_list[i]);
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
static void tcp_timer_ensure(void) {
    if (tcp_timer_started) return;
    tcp_timer_started = 1;
    thread_t *t = NULL;
    kthread_create(tcp_timer_thread, NULL, &t, "tcpretx");
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
    r->seq        = __builtin_bswap32(ack);
    r->ack_seq    = __builtin_bswap32(seq + ((flags & TCP_SYN) ? 1 : 0) + (uint32_t)dlen);
    r->doff_flags = __builtin_bswap16((5u << 12) | TCP_RST | TCP_ACK);
    r->check      = tcp_csum(daddr, saddr, r, sizeof(*r));
    ip4_output(saddr, IPPROTO_TCP, r, sizeof(*r));
}

/* ----- per-state handlers ---------------------------------------- */

static void tcp_in_listen(tcp_pcb_t *p, uint32_t saddr, uint32_t daddr,
                          uint16_t sport, uint16_t dport, uint32_t seq,
                          uint8_t flags) {
    if (!(flags & TCP_SYN)) return;
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
    c->parent       = p;
    c->next         = g_tcp_pcbs;
    g_tcp_pcbs      = c;
    tcp_xmit_queue(c, TCP_SYN | TCP_ACK, NULL, 0);
}

static void tcp_in_syn_sent(tcp_pcb_t *p, uint32_t seq, uint32_t ack,
                            uint8_t flags) {
    if (flags & TCP_RST) {
        tcp_kill_pcb(p, ECONNREFUSED);
        return;
    }
    if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
        p->rcv_nxt = seq + 1;
        /* The peer's ACK confirms our SYN.  Prune it from the
         * unacked queue and advance snd_una.  */
        if ((int32_t)(ack - p->snd_una) > 0) {
            p->snd_una = ack;
            tcp_unacked_prune(p, ack);
        }
        p->state = TCP_ESTABLISHED;
        tcp_send_ctl(p, TCP_ACK);
        sched_wakeup(p->connect_chan);
    }
}

static void tcp_in_syn_received(tcp_pcb_t *p, uint32_t ack, uint8_t flags) {
    if ((flags & TCP_ACK) && ack == p->snd_nxt) {
        if ((int32_t)(ack - p->snd_una) > 0) {
            p->snd_una = ack;
            tcp_unacked_prune(p, ack);
        }
        p->state = TCP_ESTABLISHED;
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

static void tcp_in_established(tcp_pcb_t *p, uint32_t seq, uint32_t ack,
                               uint8_t flags, const uint8_t *payload,
                               size_t dlen) {
    if (flags & TCP_RST) {
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
        if ((int32_t)(ack - p->snd_una) > 0) {
            p->snd_una = ack;
            tcp_unacked_prune(p, ack);
            p->dup_ack = 0;
            p->last_ack = ack;
        } else if (ack == p->last_ack && p->unacked_head) {
            /* Duplicate ACK — peer's still waiting on our oldest
             * unacked seg.  After 3 in a row, retransmit it without
             * waiting for the RTO.  */
            p->dup_ack++;
            if (p->dup_ack == TCP_DUP_ACK_FAST) tcp_retx_head(p);
        } else {
            p->last_ack = ack;
        }
    }

    /* Process FIN — but only when it is in order.  A FIN occupies the
     * sequence number right after this segment's data (seq + dlen); it
     * may only be consumed once everything up to it has been received,
     * i.e. seq + dlen == rcv_nxt.  Acting on an out-of-order FIN (one
     * that raced ahead of still-in-flight data — common here because
     * the sender has no real send-window throttle and the receiver
     * drops ring overflow, recovered by retransmission) would tear the
     * receive side down early and silently truncate the stream. */
    if ((flags & TCP_FIN) && seq + (uint32_t)dlen == p->rcv_nxt) {
        p->rcv_nxt++;
        switch (p->state) {
        case TCP_ESTABLISHED:
            p->state = TCP_CLOSE_WAIT;
            sched_wakeup(p->recv_chan);
            tcp_send_ctl(p, TCP_ACK);
            break;
        case TCP_FIN_WAIT_1:
            if (flags & TCP_ACK) {
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

    tcp_pcb_t *p = tcp_find(saddr, sport, daddr, dport);
    if (!p) {
        tcp_send_rst(saddr, daddr, th, flags, seq, ack, dlen);
        return;
    }

    /* Track peer's advertised window for our flow-control sketch.  */
    p->snd_wnd = __builtin_bswap16(th->window);

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
    p->accept_q = (tcp_pcb_t **)kmalloc(sizeof(tcp_pcb_t *) * backlog);
    if (!p->accept_q) return -ENOMEM;
    p->accept_cap = backlog;
    p->state      = TCP_LISTEN;
    p->listen     = 1;
    return 0;
}

/* Kick off the SYN.  Common to blocking and non-blocking connect.  */
static void tcp_connect_start(tcp_pcb_t *p, uint32_t raddr, uint16_t rport) {
    static uint16_t next_eph = 32768;
    if (!p->lport) p->lport = ++next_eph;
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
    tcp_connect_start(p, raddr, rport);
    /* Wait — the retransmit kthread enforces the overall timeout via
     * TCP_MAX_RETX.  Loop on state changes.  */
    for (;;) {
        if (p->state == TCP_ESTABLISHED) return 0;
        if (p->state == TCP_CLOSED) {
            int err = p->so_error ? -p->so_error : -ECONNREFUSED;
            return err;
        }
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep_until(p->connect_chan, get_ticks() + TCP_SLEEP_POLL);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask)
            return -EINTR;
    }
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
        return (events & POLLOUT ? POLLOUT : 0) | POLLHUP | POLLERR;
    }
    if (events & POLLIN) {
        if (p->rx_count > 0) revents |= POLLIN;
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
            return c;
        }
        tcp_unlock(f);
        if (nonblock) return NULL;
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep_until(listen_p->accept_chan, get_ticks() + TCP_SLEEP_POLL);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask)
            return NULL;
    }
}

ssize_t tcp_send(tcp_pcb_t *p, const void *buf, size_t len) {
    if (p->state != TCP_ESTABLISHED && p->state != TCP_CLOSE_WAIT) {
        /* A connection that was up and then failed (RST -> ECONNRESET,
         * RTO -> ETIMEDOUT) reports EPIPE, matching what a write to a
         * broken pipe/socket gives elsewhere; a socket that was never
         * connected reports ENOTCONN. */
        return p->so_error ? -EPIPE : -ENOTCONN;
    }
    const uint8_t *b = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        int rc = tcp_xmit_queue(p, TCP_ACK | TCP_PSH, b + sent, chunk);
        if (rc < 0) return sent ? (ssize_t)sent : rc;
        sent += chunk;
    }
    return (ssize_t)sent;
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
    for (;;) {
        ssize_t r = tcp_recv_nb(p, buf, len);
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
        extern int kprintf(const char *, ...);
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
    case TCP_LISTEN:
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
