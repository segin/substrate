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
#include <kern/random.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <sys/random.h>
#include <net/inet.h>
#include <pm/pm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/kthread.h>
#include <sys/netdev.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <vm/vm_kmem.h>

/*
 * Netstack synchronisation.  tcp_input() and everything it calls mutate
 * the same state as the socket-layer entry points (tcp_alloc/free/close/
 * recv/accept/connect/send) and the timer: the g_tcp_pcbs list, per-PCB
 * rx ring + counters, accept queues, the unacked send queue.  With no
 * mutual exclusion one side landing mid-update corrupts the PCB -- the
 * crash class behind "tcp_close called with p=0x28" and the scattered
 * afi_sock damage.
 *
 * tcp_lock() disables local interrupts, which on a uniprocessor makes a
 * critical section atomic against every other context: no IRQ can arrive
 * and, without the timer IRQ, nothing can preempt it.  EVERY side has to
 * take it, the RX side included.  It is NOT safe to assume RX runs in an
 * ISR with interrupts already off: a NIC's RX interrupt does, but the
 * loopback device delivers from a kthread with interrupts enabled
 * (loopback.c restores them before netdev_rx()), so every segment sent
 * to 127.0.0.1 ran all of tcp_input() preemptibly, and process context
 * could run -- and take tcp_lock() -- in the middle of it.  tcp_input()
 * now holds the lock across the PCB lookup and the whole state dispatch.
 * intr_disable()/intr_restore() nest, so the callees' own tcp_lock()
 * pairs stay correct, and on a genuine ISR path the outer lock is free.
 * (SMP would need a real spinlock here too.)
 */
static inline uint32_t tcp_lock(void)   { return intr_disable(); }
static inline void     tcp_unlock(uint32_t f) { intr_restore(f); }

#define IPPROTO_TCP        6
#define TCP_RING_LEN       (32 * 1024)
#define TCP_MSS            1460
#define TCP_DEFAULT_PEER_MSS 536         /* TCP-HDR-02: RFC 1122 4.2.2.6 */
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
/* TCP-SM-01: how long CLOSING or LAST_ACK may sit with nothing left to
 * retransmit before the timer completes the close itself. */
#define TCP_CLOSING_TICKS    TCP_MSL_TICKS
#define TCP_DUP_ACK_FAST   3             /* fast-retx trigger */
/* TCP-WIN-13: the connection-level user timeout (RFC 793 3.8/3.9) when the
 * application has not set TCP_USER_TIMEOUT -- RFC 1122 4.2.3.5's R2, which
 * must be at least 100 s. */
#define TCP_USER_TIMEOUT_TICKS (300 * HZ)
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
    uint8_t   probe;          /* TCP-WIN-01: sent as a zero-window probe */
    uint8_t   fast_retx;      /* TCP-WIN-04: fast retransmits so far */
    struct tcp_seg *next;
    uint8_t   data[];         /* flex array; dlen bytes */
} tcp_seg_t;

/* TCP-WIN-08: one segment held for reassembly above RCV.NXT. */
typedef struct tcp_ooo {
    uint32_t  seq;
    uint16_t  len;
    uint8_t   fin;
    struct tcp_ooo *next;
    uint8_t   data[];
} tcp_ooo_t;

typedef struct tcp_pcb {
    int       state;
    uint32_t  laddr, raddr;        /* network byte order */
    uint16_t  lport, rport;        /* host order */
    uint32_t  iss;                 /* initial send seq */
    uint32_t  snd_una;             /* oldest unack */
    uint32_t  snd_nxt;             /* next seq to send */
    uint32_t  snd_max;             /* TCP-MEM-07: highest seq actually sent */
    int       snd_max_valid;
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
    uint64_t  closing_until;  /* TCP-SM-01: CLOSING/LAST_ACK reaper deadline */
    uint8_t   pollout_wait;   /* TCP-WIN-02: poll() saw no POLLOUT */
    uint32_t  last_adv_wnd;   /* TCP-WIN-03: window in our last segment */
    uint32_t  rcv_adv_edge;   /* TCP-WIN-07: RCV.NXT + RCV.WND advertised */
    uint8_t   rcv_adv_edge_valid;
    tcp_ooo_t *ooo_head;      /* TCP-WIN-08: reassembly queue, by seq */
    int       ooo_segs;
    uint32_t  ooo_bytes;
    uint8_t   seg_wnd_same;   /* TCP-WIN-05: segment repeats the window */
    uint32_t  max_snd_wnd;    /* TCP-WIN-06: largest window the peer offered */
    uint32_t  snd_wl1, snd_wl2; /* TCP-WIN-12: SEG.SEQ/ACK of the last window update */
    uint8_t   snd_wl_valid;
    uint32_t  user_timeout_ms; /* TCP-WIN-13: TCP_USER_TIMEOUT, 0 = default */
    uint64_t  ut_deadline;    /* TCP-WIN-13: abort if no progress by then */
    uint32_t  srtt8, rttvar4; /* TCP-WIN-14: RFC 6298 estimator, scaled */
    uint32_t  rto;            /* TCP-WIN-14: current RTO in ticks, 0 = initial */
    uint8_t   rtt_valid;
    uint16_t  snd_mss;        /* TCP-HDR-02: peer's MSS; 0 until known */
    uint16_t  syn_mss;        /* TCP-HDR-02: MSS option of the last SYN seen */
    uint16_t  mtu_mss;        /* TCP-HDR-04: egress MTU less headers; 0 = none */
    /* TCP-URG-01: the urgent mechanism, receive side (BSD out-of-line). */
    uint32_t  rcv_up;         /* RCV.UP: sequence number after the urgent octet */
    uint8_t   urg_have;       /* rcv_up is valid */
    uint8_t   urg_extract;    /* the urgent octet has not arrived yet */
    uint8_t   oob_valid;      /* oob_byte holds the urgent octet */
    uint8_t   oob_byte;
    uint8_t   urg_mark_valid; /* the ring holds the mark */
    uint8_t   urg_sig_pending; /* SIGURG owed to the owner (timer delivers) */
    uint32_t  urg_mark_left;  /* ring octets still ahead of the mark */
    int       owner;          /* F_SETOWN: pid, or -pgrp; 0 = none */
    uint32_t  snd_up;         /* TCP-URG-02: SND.UP, after the urgent octet */
    uint8_t   snd_up_valid;
    struct ip4_txopts txo;    /* UDP-API-12: the socket's IP_TTL/IP_TOS */
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

/*
 * TCP-HDR-05: RFC 6528 -- ISN = M + F(localip, localport, remoteip,
 * remoteport, secretkey).  A bare CSPRNG draw per connection (above) is
 * unpredictable but has no clock component, so successive incarnations of
 * the same 4-tuple got unrelated ISNs and RFC 793 3.3's guarantee -- a new
 * incarnation's sequence numbers lie beyond the old one's, so its stray
 * segments cannot be taken for new data -- was lost.
 *
 * M is the ~4 us clock 793 3.3 describes (get_uptime_ns() / 4096).  F is ChaCha20
 * used as a PRF: keyed by a secret drawn once from the kernel CSPRNG, with
 * the 4-tuple as the nonce.  Until the CSPRNG can supply the key, F falls
 * back to advancing the old LCG (never to a constant).
 */
static uint8_t tcp_isn_key[CHACHA20_KEY_SIZE];
static int     tcp_isn_key_ready;

static uint32_t tcp_new_iss(uint32_t laddr, uint16_t lport,
                            uint32_t raddr, uint16_t rport) {
    /* M: a clock ticking every 4.096 us.  The 4 ms timer tick alone was too
     * coarse: a reconnect within one tick got the same M, so a reused
     * 4-tuple's new ISN could sit below the old connection's last sequence
     * number and the peer, still holding it, rejected the SYN. */
    uint32_t m = (uint32_t)(get_uptime_ns() >> 12);
    /* random_get_bytes() returns the byte count, not 0, on success.  The
     * old ISN code tested "== 0", so it never used the CSPRNG at all: every
     * ISS came from the fixed-seed LCG and was the same on every boot. */
    if (!tcp_isn_key_ready &&
        random_get_bytes(tcp_isn_key, sizeof(tcp_isn_key)) ==
            (int)sizeof(tcp_isn_key))
        tcp_isn_key_ready = 1;
    if (!tcp_isn_key_ready) {
        tcp_iss_seed = tcp_iss_seed * 1103515245 + 12345;
        return m + tcp_iss_seed;
    }
    uint8_t nonce[CHACHA20_NONCE_SIZE];
    memcpy(nonce, &laddr, 4);
    memcpy(nonce + 4, &raddr, 4);
    memcpy(nonce + 8, &lport, 2);
    memcpy(nonce + 10, &rport, 2);
    struct chacha20_ctx ctx;
    chacha20_init(&ctx, tcp_isn_key, nonce);
    chacha20_block(&ctx);
    uint32_t f;
    memcpy(&f, ctx.block, 4);
    memset(&ctx, 0, sizeof(ctx));       /* no key material left on the stack */
    return m + f;
}

static uint16_t tcp_csum(uint32_t saddr, uint32_t daddr,
                         const void *seg, size_t len) {
    return inet_csum_pseudo4(saddr, daddr, IPPROTO_TCP, (uint16_t)len, seg);
}

/*
 * TCP-WIN-07: the receive window to advertise.  Receiver silly-window
 * avoidance (RFC 793 3.7, RFC 1122 4.2.3.3): offer nothing until at least
 * min(MSS, ring/2) is free, so the peer is not invited to send tinygrams --
 * the raw free-space count was advertised, so a reader freeing 512 octets
 * after a zero window offered exactly 512.  And never move the right edge
 * (RCV.NXT + RCV.WND) left of where it was last advertised: the peer may
 * already be sending into it.
 */
static uint32_t tcp_rcv_wnd_calc(const tcp_pcb_t *p) {
    uint32_t free = TCP_RING_LEN - p->rx_count;
    uint32_t thresh = TCP_MSS < TCP_RING_LEN / 2 ? TCP_MSS : TCP_RING_LEN / 2;
    return free >= thresh ? free : 0;
}

static uint32_t tcp_rcv_wnd_adv(const tcp_pcb_t *p) {
    uint32_t adv = tcp_rcv_wnd_calc(p);
    if (p->rcv_adv_edge_valid) {
        int32_t keep = (int32_t)(p->rcv_adv_edge - p->rcv_nxt);
        if (keep > (int32_t)adv) adv = (uint32_t)keep;
    }
    return adv;
}

/* Write a TCP segment out via ip4_output.  Does NOT touch snd_nxt /
 * the unacked queue — the queuing layer below does that.  */
static int tcp_xmit_raw(tcp_pcb_t *p, uint32_t seq, uint8_t flags,
                        const void *data, size_t dlen) {
    /* TCP-HDR-03: a SYN carries our MSS option, <kind=2,len=4,mss>.  It
     * was never sent, so every peer fell back to RFC 1122's 536.  The four
     * octets keep the header 32-bit aligned; no padding is needed. */
    size_t optlen = (flags & TCP_SYN) ? 4u : 0u;
    uint8_t buf[TCP_MSS + sizeof(struct tcphdr) + 4];
    if (dlen > TCP_MSS) dlen = TCP_MSS;
    struct tcphdr *th = (struct tcphdr *)buf;
    th->source     = __builtin_bswap16(p->lport);
    th->dest       = __builtin_bswap16(p->rport);
    th->seq        = __builtin_bswap32(seq);
    /* TCP-WIN-11: RCV.NXT and the window are sampled together under the
     * lock (filled in below).  This runs unlocked from process context
     * while the RX path advances rcv_nxt and rx_count, so a torn pair
     * could advertise an edge left of the last one. */
    uint32_t wf    = tcp_lock();
    uint32_t rnxt  = p->rcv_nxt;
    uint32_t adv   = tcp_rcv_wnd_adv(p);
    p->last_adv_wnd = adv;          /* TCP-WIN-03: what the peer now believes */
    p->rcv_adv_edge = rnxt + adv;                          /* TCP-WIN-07 */
    p->rcv_adv_edge_valid = 1;
    /* TCP-URG-02: while urgent data is outstanding every segment below
     * SND.UP carries URG and the pointer -- in the BSD form, the offset of
     * the octet FOLLOWING the urgent data (TCP-URG-06). */
    uint16_t up = 0;
    if (p->snd_up_valid && !(flags & TCP_RST) &&
        (int32_t)(p->snd_up - seq) > 0) {
        uint32_t off = p->snd_up - seq;
        up = off > 0xFFFFu ? 0xFFFFu : (uint16_t)off;
        flags |= TCP_URG;
    }
    tcp_unlock(wf);
    th->doff_flags = __builtin_bswap16((uint16_t)(((5u + optlen / 4u) << 12) | flags));
    th->ack_seq    = __builtin_bswap32(rnxt);
    th->window     = __builtin_bswap16((uint16_t)adv);
    th->check      = 0;
    th->urg_ptr    = __builtin_bswap16(up);
    if (optlen) {
        uint8_t *o = buf + sizeof(*th);
        o[0] = 2;                       /* MSS */
        o[1] = 4;
        uint16_t our = p->mtu_mss ? p->mtu_mss : TCP_MSS;     /* TCP-HDR-04 */
        o[2] = (uint8_t)(our >> 8);
        o[3] = (uint8_t)(our & 0xFF);
    }
    if (dlen && data) memcpy(buf + sizeof(*th) + optlen, data, dlen);
    /*
     * TCP-29: the pseudo-header source must be the address ip4_output will
     * put in the IP header, not p->laddr.  On a multihomed host they differ,
     * and when p->laddr is still 0 (a client socket that never bound) EVERY
     * segment shipped an invalid checksum -- silently, since we never see
     * the peer's discard.  ip4_source_for() is the same routing decision
     * ip4_output makes, and is what the UDP path uses since UDP-03.
     */
    uint32_t csum_src = p->laddr ? p->laddr : ip4_source_for(p->raddr);
    th->check = tcp_csum(csum_src, p->raddr, buf, sizeof(*th) + optlen + dlen);
    /* TCP-HDR-01: and the IP header must carry that same source.  TCP-29
     * fixed only laddr == 0; ip4_output() re-chose the source by routing,
     * so every segment of a socket whose laddr differed from the egress
     * device's address -- bound, or connected over loopback to a local NIC
     * address -- went out with a checksum the peer discarded. */
    return ip4_output_opts(csum_src, p->raddr, IPPROTO_TCP, buf,
                           sizeof(*th) + optlen + dlen, &p->txo);
}

/* TCP-HDR-02: the largest segment we may send the peer -- its MSS option
 * (536 if it sent none, RFC 1122 4.2.2.6), capped by our own buffer. */
static uint32_t tcp_eff_mss(const tcp_pcb_t *p) {
    uint32_t m = TCP_MSS;
    if (p->snd_mss && p->snd_mss < m) m = p->snd_mss;
    if (p->mtu_mss && p->mtu_mss < m) m = p->mtu_mss;   /* TCP-HDR-04 */
    return m;
}

/*
 * TCP-HDR-04: the MSS the egress interface allows -- its MTU less the IP and
 * TCP headers -- resolved once when the connection is set up.  TCP_MSS was
 * used whatever the MTU, so on a smaller-MTU link every full segment failed
 * EMSGSIZE in ip4_output() (UDP-IP-01) on every retransmission and the
 * transfer stalled.  It also bounds the MSS we advertise.
 */
static void tcp_set_mtu_mss(tcp_pcb_t *p) {
    uint32_t mtu = ip4_path_mtu(p->raddr);
    uint32_t hdr = sizeof(struct iphdr) + sizeof(struct tcphdr);
    p->mtu_mss = 0;
    if (mtu > hdr && mtu - hdr < TCP_MSS)
        p->mtu_mss = (uint16_t)(mtu - hdr);
}

static void tcp_take_peer_mss(tcp_pcb_t *p, uint16_t syn_mss) {
    p->snd_mss = syn_mss ? syn_mss : TCP_DEFAULT_PEER_MSS;
}

/* Sequence space a segment occupies: its data plus one for SYN and FIN. */
static uint32_t tcp_seg_cost(uint8_t flags, size_t dlen) {
    return (uint32_t)dlen + ((flags & TCP_SYN) ? 1u : 0u) +
           ((flags & TCP_FIN) ? 1u : 0u);
}

/* TCP-MEM-07: note that sequence space up to `end` has been transmitted.
 * snd_nxt is advanced when a segment is QUEUED, before it goes out, so it
 * over-states what the peer can have seen; ACK acceptability is bounded by
 * this instead. */
static void tcp_note_sent(tcp_pcb_t *p, uint32_t end) {
    uint32_t f = tcp_lock();
    if (!p->snd_max_valid || (int32_t)(end - p->snd_max) > 0) {
        p->snd_max = end;
        p->snd_max_valid = 1;
    }
    tcp_unlock(f);
}

/* The upper bound for an acceptable ACK: the highest sequence sent. */
static uint32_t tcp_ack_limit(const tcp_pcb_t *p) {
    return p->snd_max_valid ? p->snd_max : p->snd_nxt;
}

/* Pure-ACK / pure-RST segments don't enter the retx queue.  Use this
 * for the "I want to acknowledge what I just received" pattern.  */
static void tcp_send_ctl(tcp_pcb_t *p, uint8_t flags) {
    tcp_xmit_raw(p, p->snd_nxt, flags, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* Send queue management                                              */
/* ------------------------------------------------------------------ */

/* Allocate a tcp_seg with `dlen` bytes of payload and copy `data` in.
 * Nothing is sequenced or linked yet; that is tcp_seg_link_locked(). */
static tcp_seg_t *tcp_seg_alloc(uint8_t flags, const void *data, size_t dlen) {
    tcp_seg_t *s = (tcp_seg_t *)kmalloc(sizeof(*s) + dlen);
    if (!s) return NULL;
    s->flags     = flags;
    s->dlen      = (uint16_t)dlen;
    s->sent_tick = get_ticks();
    s->retx      = 0;
    s->probe     = 0;
    s->fast_retx = 0;
    s->next      = NULL;
    if (dlen && data) memcpy(s->data, data, dlen);
    return s;
}

/* Assign `s` its sequence number, advance snd_nxt by the segment's
 * sequence cost (SYN/FIN count as 1, data counts as dlen), and link it
 * onto the unacked FIFO so the timer can retransmit.  Caller holds
 * tcp_lock.  Returns the sequence number assigned.
 *
 * Ordering is load-bearing: on loopback tcp_xmit_raw() delivers the
 * segment synchronously, the peer ACKs it, and that ACK is processed
 * (tcp_unacked_prune) before tcp_xmit_raw() even returns.  If the
 * segment were appended afterwards the ACK could never prune it -- it
 * would sit at unacked_head forever, RTO-retransmitted until ETIMEDOUT
 * killed the connection, and its permanent presence would block the
 * FIN_WAIT_1 -> FIN_WAIT_2 transition (which requires !unacked_head), so
 * the connection could never close cleanly either. */
/* TCP-WIN-13: how long data may sit unacknowledged before the connection
 * is aborted. */
static uint64_t tcp_ut_ticks(const tcp_pcb_t *p) {
    if (p->user_timeout_ms)
        return (uint64_t)p->user_timeout_ms * HZ / 1000u + 1u;
    return TCP_USER_TIMEOUT_TICKS;
}

static uint32_t tcp_seg_link_locked(tcp_pcb_t *p, tcp_seg_t *s) {
    uint32_t seq = p->snd_nxt;
    if (!p->unacked_head)               /* TCP-WIN-13: queue was empty */
        p->ut_deadline = get_ticks() + tcp_ut_ticks(p);
    s->seq = seq;
    p->snd_nxt += tcp_seg_cost(s->flags, s->dlen);
    if (p->unacked_tail) p->unacked_tail->next = s;
    else                 p->unacked_head = s;
    p->unacked_tail = s;
    return seq;
}

/* Transmit a segment already linked by tcp_seg_link_locked(), with IRQs
 * enabled (tcp_xmit_raw -> ip4_output may ARP-wait, which needs IRQs on
 * to receive the reply).  A failed transmit leaves the segment queued;
 * the RTO timer retransmits it -- which is the correct response to a
 * transient send error.
 *
 * TCP-MEM-07: the segment itself is never touched here.  Once the lock is
 * dropped an ACK can prune and kfree() it, so reading s->seq and s->data
 * would be a use-after-free.  The sequence number was captured under the
 * lock, and the caller's buffer holds exactly the bytes copied into s. */
static void tcp_seg_emit(tcp_pcb_t *p, uint32_t seq, uint8_t flags,
                         const void *data, size_t dlen) {
    if (tcp_xmit_raw(p, seq, flags, data, dlen) >= 0)
        tcp_note_sent(p, seq + tcp_seg_cost(flags, dlen));
}

/* Queue and transmit one segment.  Returns 0 on success or -ENOMEM if
 * allocation failed (in which case nothing was transmitted).  */
static int tcp_xmit_queue(tcp_pcb_t *p, uint8_t flags,
                          const void *data, size_t dlen) {
    if (dlen > tcp_eff_mss(p)) dlen = tcp_eff_mss(p);  /* TCP-HDR-02 */
    tcp_seg_t *s = tcp_seg_alloc(flags, data, dlen);
    if (!s) return -ENOMEM;
    uint32_t f = tcp_lock();
    uint32_t seq = tcp_seg_link_locked(p, s);
    tcp_unlock(f);
    tcp_seg_emit(p, seq, flags, data, dlen);
    return 0;
}

/* Drop every segment from the unacked FIFO whose entire seq range
 * is <= ack — i.e. the peer has confirmed they got it.  Returns
 * the number of segments freed.  */
/*
 * TCP-WIN-14: RFC 6298 2.2-2.3.  SRTT is kept scaled by 8 and RTTVAR by 4
 * (the BSD fixed-point form), in ticks.  RTO = SRTT + max(G, 4*RTTVAR),
 * clamped to [1 s, TCP_RTO_MAX_TICKS]; the timer applies its backoff on
 * top.  The RTO used to be the fixed 1 s initial value forever, so any path
 * with an RTT near or above a second retransmitted every segment.
 */
static void tcp_rtt_sample(tcp_pcb_t *p, uint32_t r) {
    if (r == 0) r = 1;
    if (!p->rtt_valid) {
        p->srtt8   = r << 3;                    /* SRTT <- R */
        p->rttvar4 = r << 1;                    /* RTTVAR <- R/2 */
        p->rtt_valid = 1;
    } else {
        int32_t err = (int32_t)r - (int32_t)(p->srtt8 >> 3);
        uint32_t aerr = err < 0 ? (uint32_t)-err : (uint32_t)err;
        /* RTTVAR <- 3/4 RTTVAR + 1/4 |SRTT - R|; SRTT <- 7/8 SRTT + 1/8 R */
        p->rttvar4 = p->rttvar4 - (p->rttvar4 >> 2) + aerr;
        p->srtt8   = (uint32_t)((int32_t)p->srtt8 + err);
    }
    uint32_t var = p->rttvar4;                  /* 4 * RTTVAR */
    if (var < 1) var = 1;                       /* G: one tick */
    uint64_t rto = (uint64_t)(p->srtt8 >> 3) + var;
    if (rto < TCP_RTO_BASE_TICKS) rto = TCP_RTO_BASE_TICKS;
    if (rto > TCP_RTO_MAX_TICKS)  rto = TCP_RTO_MAX_TICKS;
    p->rto = (uint32_t)rto;
}

static int tcp_unacked_prune(tcp_pcb_t *p, uint32_t ack) {
    int freed = 0;
    uint64_t sample_tick = 0;
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
        /* TCP-WIN-14: Karn -- only a segment sent exactly once measures
         * the RTT; take the newest one this ACK covers. */
        if (s->retx == 0 && s->fast_retx == 0)
            sample_tick = s->sent_tick;
        kfree(s, sizeof(*s) + s->dlen);
        freed++;
    }
    if (sample_tick)
        tcp_rtt_sample(p, (uint32_t)(get_ticks() - sample_tick));
    /* TCP-WIN-13: progress re-arms the user timeout; an empty queue has
     * none (an idle connection is never timed out, RFC 1122 4.2.3.6). */
    if (freed)
        p->ut_deadline = p->unacked_head ? get_ticks() + tcp_ut_ticks(p) : 0;
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
 * fast-retx).
 *
 * TCP-WIN-04: only a timer expiry (fast == 0) advances retx -- which is
 * both the RTO backoff exponent and the abort budget -- and restarts the
 * RTO.  Fast retransmits shared both, so a few duplicate-ACK episodes on a
 * lossy but healthy link pushed the next RTO to the 60 s cap and then
 * aborted the connection.  They are capped per segment instead, so a peer
 * cannot drive unbounded retransmission with duplicate ACKs. */
#define TCP_FAST_RETX_MAX 3
static void tcp_retx_head(tcp_pcb_t *p, int fast) {
    tcp_seg_t *s = p->unacked_head;
    if (!s) return;
    if (fast && s->fast_retx >= TCP_FAST_RETX_MAX) return;
    /* TCP-WIN-09: a partially acknowledged segment stays queued whole
     * (tcp_unacked_prune frees whole segments only, and kfree needs the
     * allocated size), so resend just [SND.UNA, end): the acknowledged
     * prefix was sent again every time, from below SND.UNA. */
    uint32_t seq = s->seq, skip = 0;
    uint8_t flags = s->flags;
    if (s->dlen && (int32_t)(p->snd_una - s->seq) > 0 &&
        (int32_t)(p->snd_una - (s->seq + s->dlen)) < 0) {
        skip = p->snd_una - s->seq;
        if (flags & TCP_SYN) {          /* the SYN was the first octet acked */
            flags &= (uint8_t)~TCP_SYN;
            skip--;
        }
        seq = p->snd_una;
    }
    if (tcp_xmit_raw(p, seq, flags, s->data + skip, s->dlen - skip) >= 0)
        tcp_note_sent(p, s->seq + s->dlen + ((s->flags & TCP_SYN) ? 1u : 0u) +
                         ((s->flags & TCP_FIN) ? 1u : 0u));   /* TCP-MEM-07 */
    if (fast) {
        s->fast_retx++;
        return;
    }
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
    /* TCP-API-01: nothing queued survives the connection; a re-open would
     * otherwise retransmit it ahead of the new SYN. */
    tcp_unacked_free_all(p);
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
    /* TCP-URG-01: SIGURG is owed from the RX path, where psignal()'s locks
     * cannot be taken; collect the owners here and signal after unlock.
     * Any beyond the batch stay pending for the next tick. */
    int urg_owner[16];
    int nurg = 0;
    uint32_t f = tcp_lock();
    for (tcp_pcb_t *p = g_tcp_pcbs, *next; p; p = next) {
        next = p->next;
        if (p->urg_sig_pending && nurg < 16) {
            p->urg_sig_pending = 0;
            if (p->owner) urg_owner[nurg++] = p->owner;
        }
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
        /*
         * TCP-SM-01: CLOSING and LAST_ACK are completed only by the peer's
         * ACK of our FIN.  Once that FIN has left the unacked queue there is
         * nothing for the retransmit check below to do, so if the ACK that
         * should complete the close is ever missed, no reaper reaches the
         * PCB.  Bound it: arm a deadline the first time we see the queue
         * empty, and complete the close when it expires.  Not immediately --
         * tcp_close() publishes the state before it queues the FIN.
         */
        /* TCP-API-19: FIN-WAIT-1 too.  With its FIN queued it is bounded by
         * the retransmit budget, and an ACK of the FIN moves it on at once;
         * one with an empty queue has no FIN at all and nothing else would
         * ever end it. */
        if ((p->state == TCP_CLOSING || p->state == TCP_LAST_ACK ||
             p->state == TCP_FIN_WAIT_1) &&
            !p->unacked_head) {
            if (!p->closing_until) {
                p->closing_until = now + TCP_CLOSING_TICKS;
            } else if (now >= p->closing_until) {
                if (p->state == TCP_FIN_WAIT_1) {
                    tcp_kill_pcb(p, ETIMEDOUT);
                } else if (p->state == TCP_LAST_ACK) {
                    tcp_kill_pcb(p, 0);
                } else {
                    p->time_wait_until = now + TCP_TIME_WAIT_TICKS;   /* TCP-MEM-09: deadline first */
                    p->state = TCP_TIME_WAIT;
                }
                continue;
            }
        }
        /* TCP-MEM-09: and never expire on an unarmed (zero) deadline, which
         * a tick landing between the two stores used to see as long past. */
        if (p->state == TCP_TIME_WAIT && p->time_wait_until &&
            now >= p->time_wait_until) {
            /* Drop to CLOSED now; freed on the next tick once no RX
             * can still be matching a late segment against it.
             * TCP-SM-14: and tell anyone still waiting on the socket. */
            p->state = TCP_CLOSED;
            sched_wakeup(p->recv_chan);
            sched_wakeup(p->send_chan);
            continue;
        }
        tcp_seg_t *head = p->unacked_head;
        if (!head) continue;
        /*
         * TCP-WIN-01: a zero-window probe is retransmitted for as long as
         * the peer keeps its window shut -- a receiver that is alive but
         * not reading is not a failed path, and RFC 793 3.7 says to keep
         * probing.  Its retransmissions used to count toward TCP_MAX_RETX,
         * so a reader that paused for ~2 minutes got the connection
         * aborted.  Once the peer opens the window the segment is ordinary
         * data again, and its abort countdown starts afresh.
         */
        if (head->probe && p->snd_wnd != 0) {
            head->probe = 0;
            head->retx  = 0;
        }
        /*
         * TCP-WIN-13: RFC 793 3.9 USER TIMEOUT -- a connection-level bound
         * on unacknowledged data, which the per-segment retransmit budget
         * is not: that restarts whenever a new segment reaches the head, so
         * a path on which every segment eventually got through after many
         * retransmissions never aborted, and nothing let the application
         * choose the bound.  A zero-window probe the peer keeps answering
         * is exempt under the default (RFC 1122 4.2.2.17), but not from a
         * timeout the application set explicitly (RFC 5482).
         */
        if (p->ut_deadline && now >= p->ut_deadline &&
            !(head->probe && p->snd_wnd == 0 && !p->user_timeout_ms)) {
            tcp_kill_pcb(p, ETIMEDOUT);
            continue;
        }
        /* TCP-06: back the RTO off exponentially per attempt rather than
         * retrying at a flat interval forever.  The shift is clamped: a
         * probe's retx is unbounded, and 2^6 s already exceeds the cap. */
        unsigned shift = head->retx > 6 ? 6u : (unsigned)head->retx;
        uint64_t rto = (uint64_t)(p->rto ? p->rto : TCP_RTO_BASE_TICKS) << shift;   /* TCP-WIN-14 */
        if (rto > TCP_RTO_MAX_TICKS) rto = TCP_RTO_MAX_TICKS;
        if (now - head->sent_tick < rto) continue;
        if (!head->probe && head->retx >= TCP_MAX_RETX) {
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
        tcp_retx_head(p, 0);
    }
    tcp_unlock(f);
    for (int i = 0; i < nurg; i++) {
        if (urg_owner[i] > 0) {
            process_t *target = proc_find(urg_owner[i]);
            if (target) psignal(target, SIGURG);
        } else {
            pgsignal(-urg_owner[i], SIGURG);
        }
    }
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
    /* Then a LISTEN socket on the local port.
     *
     * TCP-API-05: the most specific one (RFC 793 2.2), not the first in
     * the list.  The list is prepended, so with a wildcard and an
     * address-specific listener on one port the NEWEST won every SYN --
     * a later [::]:22 (bound as the v4 wildcard) captured sshd's
     * 10.0.0.5:22.  An address-specific listener beats a wildcard, and a
     * fully specified passive open (a foreign socket named on the
     * listener) matches only its own peer and beats both. */
    tcp_pcb_t *best = NULL;
    int best_score = -1;
    for (tcp_pcb_t *p = g_tcp_pcbs; p; p = p->next) {
        if (p->state != TCP_LISTEN || p->lport != dport) continue;
        if (p->laddr && p->laddr != daddr) continue;
        if (p->raddr && (p->raddr != saddr || p->rport != sport)) continue;
        int score = (p->laddr ? 1 : 0) + (p->raddr ? 2 : 0);
        if (score > best_score) { best = p; best_score = score; }
    }
    return best;
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
    /* TCP-HDR-01: answer from the address the segment was sent to, which
     * is the source the checksum above covers. */
    ip4_output_from(daddr, saddr, IPPROTO_TCP, r, sizeof(*r));
}

/* ----- per-state handlers ---------------------------------------- */

static void tcp_in_listen(tcp_pcb_t *p, uint32_t saddr, uint32_t daddr,
                          const struct tcphdr *th,
                          uint16_t sport, uint16_t dport, uint32_t seq,
                          uint32_t ack, uint8_t flags, size_t dlen) {
    /*
     * TCP-28: only a CLEAN SYN may open a connection.  The test was
     * `!(flags & TCP_SYN)`, so SYN|RST and SYN|ACK both spawned a child PCB
     * -- a segment that RFC 793 3.9 says a listener must answer with a RST
     * (SYN|ACK) or discard outright (anything with RST) instead created
     * half-open state, which is free work for an attacker and wrong for a
     * confused peer.  A SYN|FIN is equally nonsense here.
     */
    if (flags & TCP_RST) return;                 /* RFC 793: discard */
    if (flags & TCP_ACK) {
        /* An ACK arriving at a LISTEN socket refers to a connection that
         * does not exist here: RFC 793 3.9 says answer it with
         * <SEQ=SEG.ACK><CTL=RST>.  TCP-SM-07: this used to return, trusting
         * "the unmatched-segment path" to send the RST -- but tcp_find()
         * matched the listener, so that path never ran and the peer got
         * silence. */
        tcp_send_rst(saddr, daddr, th, flags, seq, ack, dlen);
        return;
    }
    if (!(flags & TCP_SYN)) return;
    if (flags & TCP_FIN) return;
    /* Respect the listen backlog.  Count children that already exist
     * for this listener (handshaking SYN_RECEIVED ones plus those
     * sitting fully-established in the accept queue); if that is at or
     * past the backlog, drop the SYN.  The peer's SYN retransmit will
     * get in once accept() drains a slot — and if it never does, the
     * peer's connect() times out, which is the correct backlog-full
     * behaviour instead of establishing an un-acceptable connection.
     *
     * TCP-API-21: count EVERY child still attached to this listener, in
     * any state.  Only SYN_RECEIVED ones (plus the accept queue) were
     * counted, so a child killed by the peer's RST -- CLOSED, holding its
     * 32 KiB ring until the timer reaps it -- no longer counted, and a
     * SYN+RST flood allocated a fresh child per pair faster than the
     * reaper could free them.  Accept-queued children keep ->parent until
     * accept() takes them, so this also counts them exactly once. */
    int pending = 0;
    for (tcp_pcb_t *q = g_tcp_pcbs; q; q = q->next)
        if (q->parent == p)
            pending++;
    if (pending >= p->accept_cap)
        return;
    /* Spawn a child PCB in SYN_RECEIVED.  */
    tcp_pcb_t *c = (tcp_pcb_t *)kmalloc(sizeof(*c));
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->txo = p->txo;                 /* UDP-API-12: inherit the listener's */
    c->user_timeout_ms = p->user_timeout_ms;          /* TCP-WIN-13 */
    tcp_take_peer_mss(c, p->syn_mss);                  /* TCP-HDR-02 */
    c->state   = TCP_SYN_RECEIVED;
    c->laddr   = daddr;
    c->raddr   = saddr;
    tcp_set_mtu_mss(c);                                /* TCP-HDR-04 */
    c->lport   = dport;
    c->rport   = sport;
    c->iss     = tcp_new_iss(c->laddr, c->lport, c->raddr, c->rport);   /* TCP-HDR-05 */
    c->snd_una = c->iss;
    c->snd_nxt = c->iss;
    c->rcv_nxt = seq + 1;
    c->rcv_adv_edge_valid = 0;          /* TCP-WIN-07: new sequence space */
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
    /* A71: RFC 793 SYN-SENT requires validating the ACK before
     * proceeding.  The segment's ACK must acknowledge our SYN,
     * i.e. ISS < SEG.ACK <= SND.NXT (in SYN_SENT snd_una == ISS
     * and snd_nxt == ISS+1).  An ack that is at/below snd_una or
     * beyond snd_nxt is unacceptable: reply with a reset
     * (<SEQ=SEG.ACK><CTL=RST>, as the RFC prescribes) and drop the
     * segment rather than establishing with a stale/forged send
     * state (which also left the SYN un-pruned and snd_una wrong).
     *
     * TCP-SM-10: for every ACK-bearing segment, not only a SYN|ACK -- the
     * first check in 3.9's SYN-SENT precedes the SYN test.  A bare ACK for
     * something we never sent (a stale half of an old connection on this
     * 4-tuple) was dropped silently, so the peer never learned to reset. */
    if ((flags & TCP_ACK) &&
        ((int32_t)(ack - p->snd_una) <= 0 ||
         (int32_t)(ack - p->snd_nxt) > 0)) {
        tcp_xmit_raw(p, ack, TCP_RST, NULL, 0);
        return;
    }
    if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
        tcp_take_peer_mss(p, p->syn_mss);               /* TCP-HDR-02 */
        p->rcv_nxt = seq + 1;
        p->rcv_adv_edge_valid = 0;      /* TCP-WIN-07 */
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
        return;
    }
    /*
     * TCP-SM-11: simultaneous open (RFC 793 3.4 figure 8, 3.9 SYN-SENT
     * fourth check).  A SYN without ACK means the peer is opening toward
     * us at the same moment.  It used to be dropped, so two ends that
     * dialled each other never connected.  Take its sequence number, move
     * to SYN-RECEIVED, and turn the SYN already queued at ISS into the
     * SYN|ACK (retransmitted as such until acknowledged) rather than
     * queueing a second one; tcp_in_syn_received() completes the open.
     */
    if (flags & TCP_SYN) {
        tcp_take_peer_mss(p, p->syn_mss);               /* TCP-HDR-02 */
        p->rcv_nxt = seq + 1;
        p->rcv_adv_edge_valid = 0;      /* TCP-WIN-07 */
        p->state   = TCP_SYN_RECEIVED;
        if (p->unacked_head && (p->unacked_head->flags & TCP_SYN)) {
            p->unacked_head->flags |= TCP_ACK;
            tcp_retx_head(p, 1);   /* now, not a timeout */
        }
    }
}

static int tcp_seq_in_rcv_window(const tcp_pcb_t *p, uint32_t seq);
static int tcp_seg_check(tcp_pcb_t *p, uint32_t *seqp, uint8_t *flagsp,
                         const uint8_t **payloadp, size_t *dlenp);

/* Returns 1 when the segment completed the handshake and its text and FIN
 * (possibly trimmed, through the pointers) remain to be processed by
 * tcp_in_established(); 0 when it has been consumed. */
static int tcp_in_syn_received(tcp_pcb_t *p, uint32_t *seqp, uint32_t ack,
                               uint8_t *flagsp, const uint8_t **payloadp,
                               size_t *dlenp) {
    uint32_t seq = *seqp;
    uint8_t flags = *flagsp;
    /* NET-06: a RST for a half-open child aborts it.  Tear the child
     * down (tcp_kill_pcb -> TCP_CLOSED) instead of silently dropping the
     * segment; the retransmit-timer reaper then frees the never-accepted
     * PCB — see NET-04.
     *
     * TCP-SM-04: but only a RST whose sequence number is valid.  Any RST on
     * the 4-tuple was honoured, so a blind attacker who knew the tuple
     * killed every embryonic connection.  Apply the same RFC 5961 3.2 test
     * as the synchronized states: outside the window drop it, off RCV.NXT
     * send a challenge ACK, exactly at RCV.NXT reset. */
    if (flags & TCP_RST) {
        if (!tcp_seq_in_rcv_window(p, seq))
            return 0;
        if (seq != p->rcv_nxt) {
            tcp_send_ctl(p, TCP_ACK);   /* challenge ACK */
            return 0;
        }
        tcp_kill_pcb(p, ECONNRESET);
        return 0;
    }
    /*
     * TCP-SM-09: RFC 793 3.9 for SYN-RECEIVED.  None of this was checked:
     * any segment carrying ACK == SND.NXT completed the handshake whatever
     * its sequence number, an unacceptable ACK was silently ignored, and
     * the text and FIN of the third segment were thrown away (a client
     * that sends its request with the handshake ACK had to wait for an RTO
     * to get it through).
     *
     * A SYN at IRS is the peer's SYN again: a retransmission (the queued
     * SYN-ACK's retransmission answers it, as before), or in a simultaneous
     * open (TCP-SM-11) the peer's SYN|ACK, whose ACK completes the open.
     * Strip the SYN, as BSD does, and process the rest; RFC 793's own
     * acceptability test would reject that SYN|ACK, which is a known
     * defect of its figure 8.  Any other SYN is ignored.
     */
    if (flags & TCP_SYN) {
        if (seq != p->rcv_nxt - 1u)
            return 0;
        flags &= (uint8_t)~TCP_SYN;
        *flagsp = flags;
        *seqp = seq = p->rcv_nxt;
    }
    /* First check: sequence number (answered with an ACK and dropped). */
    if (!tcp_seg_check(p, seqp, flagsp, payloadp, dlenp))
        return 0;
    flags = *flagsp;
    /* Fifth check: no ACK, drop; an ACK outside (SND.UNA, SND.NXT] draws
     * <SEQ=SEG.ACK><CTL=RST> and the embryo stays as it was. */
    if (!(flags & TCP_ACK))
        return 0;
    if (!((int32_t)(ack - p->snd_una) > 0 && (int32_t)(ack - p->snd_nxt) <= 0)) {
        tcp_xmit_raw(p, ack, TCP_RST, NULL, 0);
        return 0;
    }
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
            /* TCP-MEM-05: write the slot, THEN publish it.  The
             * one-statement form compiled to the count store first
             * (confirmed in tcp.o), so a reader between the two stores
             * took an unwritten slot as a PCB pointer.  tcp_input()'s
             * lock (TCP-MEM-01) now excludes that reader; the order is
             * kept right regardless, with a compiler barrier. */
            par->accept_q[par->accept_count] = p;
            __asm__ volatile ("" ::: "memory");
            par->accept_count++;
            sched_wakeup(par->accept_chan);
        }
    } else {
        sched_wakeup(p->connect_chan);   /* TCP-SM-11: an active open */
    }
    return 1;
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

/*
 * TCP-SM-02 / TCP-SM-05: RFC 793 3.9 SEGMENT ARRIVES, "first check sequence
 * number", for the synchronized states.
 *
 * Acceptability is the 3.3 table, with RCV.WND the free receive space:
 *
 *   SEG.LEN  RCV.WND  acceptable when
 *      0        0     SEG.SEQ = RCV.NXT
 *      0       >0     RCV.NXT =< SEG.SEQ < RCV.NXT+RCV.WND
 *     >0        0     never
 *     >0       >0     either end of the segment inside the window
 *
 * where SEG.LEN counts the FIN.  An unacceptable segment is answered with an
 * ACK carrying our current state and dropped -- unless it is a RST, which is
 * dropped silently.  Nothing was answered before, so a peer's keepalive (one
 * octet below RCV.NXT) or a zero-window probe met total silence and the peer
 * eventually gave up.  3.9 also asks for "special allowance ... to accept
 * valid ACKs, URGs and RSTs" while the window is zero: a segment exactly at
 * RCV.NXT keeps its control bits, loses its text and FIN, and is answered.
 * In TIME-WAIT the only expected arrival is a retransmission of the peer's
 * FIN, which lies below RCV.NXT: acknowledge it and restart the 2*MSL wait.
 *
 * An acceptable segment is then trimmed to the window, so it "begins at
 * RCV.NXT and does not exceed the window".  Without the left trim, a segment
 * straddling RCV.NXT -- e.g. a retransmission of one we had half-accepted
 * into a nearly full ring -- failed the seq == rcv_nxt test forever and the
 * connection stalled.  A FIN beyond the right edge is dropped with the text.
 *
 * Returns 1 to go on processing the (possibly trimmed) segment, 0 if it has
 * been consumed.
 */
static int tcp_seg_check(tcp_pcb_t *p, uint32_t *seqp, uint8_t *flagsp,
                         const uint8_t **payloadp, size_t *dlenp)
{
    uint32_t seq = *seqp;
    uint8_t  flags = *flagsp;
    size_t   dlen = *dlenp;
    uint32_t wnd = (uint32_t)TCP_RING_LEN - p->rx_count;
    uint32_t seglen = (uint32_t)dlen + ((flags & TCP_FIN) ? 1u : 0u);
    int acceptable;

    if (seglen == 0) {
        acceptable = (wnd == 0) ? seq == p->rcv_nxt
                                : (uint32_t)(seq - p->rcv_nxt) < wnd;
    } else if (wnd == 0) {
        acceptable = 0;
    } else {
        acceptable = (uint32_t)(seq - p->rcv_nxt) < wnd ||
                     (uint32_t)(seq + seglen - 1u - p->rcv_nxt) < wnd;
    }

    if (!acceptable) {
        if (flags & TCP_RST)
            return 0;
        if (wnd == 0 && seq == p->rcv_nxt) {
            /* Zero-window allowance: keep ACK/URG/RST, drop text and FIN. */
            *flagsp = flags & (uint8_t)~TCP_FIN;
            *dlenp = 0;
            tcp_send_ctl(p, TCP_ACK);
            return 1;
        }
        if (p->state == TCP_TIME_WAIT && (flags & TCP_FIN))
            p->time_wait_until = get_ticks() + TCP_TIME_WAIT_TICKS;
        tcp_send_ctl(p, TCP_ACK);
        return 0;
    }

    /* Left trim: drop what we already have. */
    if ((int32_t)(p->rcv_nxt - seq) > 0) {
        uint32_t d = p->rcv_nxt - seq;
        if (d > dlen) d = (uint32_t)dlen;       /* only the FIN remains */
        *payloadp += d;
        dlen -= d;
        seq += d;
    }
    /* Right trim: nothing beyond RCV.NXT + RCV.WND, FIN included. */
    if ((uint32_t)(seq - p->rcv_nxt) + dlen > wnd) {
        dlen = wnd - (uint32_t)(seq - p->rcv_nxt);
        flags &= (uint8_t)~TCP_FIN;
    }
    *seqp = seq;
    *flagsp = flags;
    *dlenp = dlen;
    return 1;
}

/* Copy up to `n` octets into the receive ring; returns how many fit. */
static uint32_t tcp_rx_copy(tcp_pcb_t *p, const uint8_t *data, uint32_t n) {
    if (n > TCP_RING_LEN - p->rx_count)
        n = TCP_RING_LEN - p->rx_count;
    for (uint32_t i = 0; i < n; i++) {
        p->rxbuf[p->rx_head] = data[i];
        p->rx_head = (p->rx_head + 1) % TCP_RING_LEN;
    }
    p->rx_count += n;
    return n;
}

/*
 * Deliver in-order text starting at sequence number `seq` into the ring.
 * Returns how many sequence octets were consumed.
 *
 * TCP-URG-01: the urgent octet (RCV.UP - 1, the BSD pointer convention) is
 * lifted out of the stream into oob_byte -- BSD's default out-of-line
 * semantics -- and the mark is recorded as the number of ring octets still
 * ahead of it, so reads stop at the mark and SIOCATMARK can report it.
 */
static uint32_t tcp_rx_put(tcp_pcb_t *p, uint32_t seq, const uint8_t *data,
                           uint32_t n) {
    uint32_t done = 0;
    if (p->urg_extract && n) {
        uint32_t off = (p->rcv_up - 1u) - seq;
        if (off < n) {
            uint32_t got = tcp_rx_copy(p, data, off);
            if (got < off)
                return got;                     /* ring full before the mark */
            p->oob_byte       = data[off];
            p->oob_valid      = 1;
            p->urg_extract    = 0;
            p->urg_mark_left  = p->rx_count;
            p->urg_mark_valid = 1;
            done = off + 1;
            sched_wakeup(p->recv_chan);
        }
    }
    return done + tcp_rx_copy(p, data + done, n - done);
}

/*
 * TCP-WIN-08: out-of-order reassembly.  Segments that arrive above RCV.NXT
 * (already trimmed to the window by tcp_seg_check) are kept, sorted by
 * sequence number, until the gap below them fills.  Bounded by segment
 * count and by one ring's worth of octets, so a peer cannot grow it; what
 * does not fit is dropped and simply retransmitted, as before.
 */
#define TCP_OOO_MAX_SEGS 32

static void tcp_ooo_insert(tcp_pcb_t *p, uint32_t seq, const uint8_t *data,
                           size_t dlen, int fin) {
    tcp_ooo_t **pp = &p->ooo_head;
    while (*pp && (int32_t)((*pp)->seq - seq) < 0)
        pp = &(*pp)->next;
    if (*pp && (*pp)->seq == seq && (*pp)->len >= dlen && ((*pp)->fin || !fin))
        return;                                     /* already have it */
    if (p->ooo_segs >= TCP_OOO_MAX_SEGS ||
        p->ooo_bytes + dlen > TCP_RING_LEN)
        return;
    tcp_ooo_t *o = (tcp_ooo_t *)kmalloc(sizeof(*o) + dlen);
    if (!o) return;
    o->seq  = seq;
    o->len  = (uint16_t)dlen;
    o->fin  = fin ? 1 : 0;
    if (dlen) memcpy(o->data, data, dlen);
    o->next = *pp;
    *pp = o;
    p->ooo_segs++;
    p->ooo_bytes += dlen;
}

static void tcp_ooo_unlink_head(tcp_pcb_t *p) {
    tcp_ooo_t *o = p->ooo_head;
    p->ooo_head = o->next;
    p->ooo_segs--;
    p->ooo_bytes -= o->len;
    kfree(o, sizeof(*o) + o->len);
}

/* Move every queued segment that RCV.NXT has reached into the ring.
 * Returns 1 when a queued FIN is now in order (the caller processes it). */
static int tcp_ooo_drain(tcp_pcb_t *p) {
    while (p->ooo_head && (int32_t)(p->ooo_head->seq - p->rcv_nxt) <= 0) {
        tcp_ooo_t *o = p->ooo_head;
        uint32_t skip = p->rcv_nxt - o->seq;        /* overlap already held */
        if (skip < o->len) {
            uint32_t want = o->len - skip;
            uint32_t got = tcp_rx_put(p, o->seq + skip, o->data + skip, want);
            p->rcv_nxt += got;
            if (got < want) return 0;               /* ring full: keep it */
        } else if (skip > o->len) {
            tcp_ooo_unlink_head(p);                 /* wholly stale */
            continue;
        }
        int fin = o->fin;
        tcp_ooo_unlink_head(p);
        if (fin) return 1;
    }
    return 0;
}

static void tcp_ooo_free_all(tcp_pcb_t *p) {
    while (p->ooo_head)
        tcp_ooo_unlink_head(p);
}

static void tcp_in_established(tcp_pcb_t *p, uint32_t seq, uint32_t ack,
                               uint8_t flags, const uint8_t *payload,
                               size_t dlen, uint32_t urg_end) {
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
     *
     * TCP-SM-06: and the same in every synchronized state (RFC 793 3.9's
     * fourth check).  The test only ran in TIME_WAIT, and after the FIN
     * processing, so an in-window SYN on any other state was answered with
     * nothing -- or had its text and FIN processed.  The challenge ACK is
     * preferred over 793's RST: a forged SYN cannot then reset us.
     */
    if (flags & TCP_SYN) {
        tcp_send_ctl(p, TCP_ACK);
        return;
    }

    /* TCP-SM-12: RFC 793 3.9 fifth check -- "if the ACK bit is off drop
     * the segment and return".  Text and FIN were taken from segments
     * without it. */
    if (!(flags & TCP_ACK))
        return;

    /* Data for a detached PCB has nowhere to land — the owning socket
     * is gone.  RST the peer so a process still writing to this
     * connection fails promptly instead of having its bytes silently
     * black-holed forever.  Pure ACK/FIN segments (dlen==0) still flow
     * through so the close handshake can finish.
     *
     * TCP-SM-15: only data beyond RCV.NXT may abort.  tcp_seg_check()
     * guarantees that here: a retransmission of data already consumed lies
     * below the window and was ACKed and dropped there, and a straddling
     * one was trimmed to its new octets.  (Before it existed, a peer that
     * merely lost our ACK was reset.) */
    if (p->detached && dlen > 0) {
        tcp_send_ctl(p, TCP_RST | TCP_ACK);
        tcp_kill_pcb(p, 0);
        return;
    }

    /* Process ACK: prune unacked segments and run dup-ACK fast-retx.
     *
     * TCP-SM-13: before the text (RFC 793 3.9 fifth check, then seventh).
     * The text used to be committed to the ring first, so a segment whose
     * ACK acknowledged something never sent -- which 3.9 says to answer
     * with an ACK and DROP -- still had its data delivered and RCV.NXT
     * advanced. */
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
        /* TCP-MEM-07: bounded by what was actually transmitted, not by the
         * pre-advanced snd_nxt. */
        if ((int32_t)(ack - tcp_ack_limit(p)) > 0) {
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
            /* TCP-URG-02: the urgent data is acknowledged -- stop flagging. */
            if (p->snd_up_valid && (int32_t)(ack - p->snd_up) >= 0)
                p->snd_up_valid = 0;
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
        } else if (ack == p->last_ack && p->unacked_head &&
                   /* TCP-WIN-05: RFC 5681 2's whole definition of a
                    * duplicate: no data, no SYN/FIN, ACK = SND.UNA with
                    * data outstanding, and the window unchanged.  Any
                    * segment repeating the ACK used to count, so the
                    * peer's own data (or a window update) fired spurious
                    * fast retransmits and cwnd cuts. */
                   dlen == 0 && !(flags & (TCP_SYN | TCP_FIN)) &&
                   ack == p->snd_una && p->snd_una != p->snd_nxt &&
                   p->seg_wnd_same) {
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
                tcp_retx_head(p, 1);
                p->dup_ack = 0;
            }
        } else {
            p->last_ack = ack;
        }
        /* An ACK frees in-flight bytes and carries the peer's latest
         * window (tcp_input() already stored it in snd_wnd) — wake any
         * sender parked in tcp_send() waiting for the window to open. */
        sched_wakeup(p->send_chan);
        if (p->pollout_wait) {          /* TCP-WIN-02: and a POLLOUT poller */
            p->pollout_wait = 0;
            sched_wakeup(p->recv_chan);
        }
    }

    /* Accept data if seq matches rcv_nxt and we have room.
     *
     * TCP-SM-03: and only in a state that can still receive text.  Once the
     * peer's FIN has been taken (CLOSE_WAIT, CLOSING, LAST_ACK, TIME_WAIT)
     * RCV.NXT sits just past it, so text "after the FIN" looked in order
     * and was delivered to read() -- RFC 793 3.9's seventh step says to
     * ignore it. */
    int can_rx = (p->state == TCP_ESTABLISHED || p->state == TCP_FIN_WAIT_1 ||
                  p->state == TCP_FIN_WAIT_2);
    /*
     * TCP-URG-01: RFC 793 3.9 sixth step, check the URG bit -- which was
     * never looked at.  RCV.UP <- max(RCV.UP, SEG.UP); if it moved ahead of
     * data not yet received, the user is signalled (SIGURG, POLLPRI) and
     * the urgent octet is lifted out when it arrives.  urg_end was computed
     * from the untrimmed segment, with SEG.UP clamped to its length.
     */
    if ((flags & TCP_URG) && urg_end && can_rx && !p->shut_rd &&
        (!p->urg_have || (int32_t)(urg_end - p->rcv_up) > 0) &&
        (int32_t)(urg_end - 1u - p->rcv_nxt) >= 0) {
        p->rcv_up          = urg_end;
        p->urg_have        = 1;
        p->urg_extract     = 1;
        p->oob_valid       = 0;         /* a new mark supersedes the old byte */
        p->urg_sig_pending = 1;
        sched_wakeup(p->recv_chan);
    }
    /* TCP-WIN-08: text (or a FIN) beyond RCV.NXT is queued for reassembly
     * and answered with an immediate duplicate ACK (RFC 5681 4.2), instead
     * of being dropped for the peer to retransmit after an RTO. */
    if (can_rx && (dlen || (flags & TCP_FIN)) &&
        (int32_t)(seq - p->rcv_nxt) > 0) {
        if (!p->shut_rd)                    /* TCP-WIN-10: nothing to keep */
            tcp_ooo_insert(p, seq, payload, dlen, flags & TCP_FIN);
        tcp_send_ctl(p, TCP_ACK);
        return;
    }
    if (dlen && seq == p->rcv_nxt && can_rx) {
        /* TCP-WIN-10: after SHUT_RD, consume without storing. */
        uint32_t accept_n = p->shut_rd ? (uint32_t)dlen
                                       : tcp_rx_put(p, seq, payload, dlen);
        p->rcv_nxt  += accept_n;
        /* TCP-WIN-08: the gap this filled may release queued segments --
         * and a FIN queued behind them. */
        if (accept_n == dlen && !(flags & TCP_FIN) && tcp_ooo_drain(p)) {
            flags |= TCP_FIN;
            seq = p->rcv_nxt;
            dlen = 0;
        }
        sched_wakeup(p->recv_chan);
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
     * TCP-13 / TCP-SM-01: a RETRANSMITTED FIN -- one whose sequence number
     * rcv_nxt has already passed -- is handled by tcp_seg_check(): it lies
     * below the window, so it is answered with an ACK (restarting the
     * 2*MSL wait in TIME_WAIT) and dropped before any field of it is used.
     * It used to be answered here after the ACK field had been processed,
     * and then return before the CLOSING/LAST_ACK completion tests, which
     * wedged both states.
     */

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
                p->time_wait_until = get_ticks() + TCP_TIME_WAIT_TICKS;   /* TCP-MEM-09: deadline first */
                p->state = TCP_TIME_WAIT;
            } else {
                p->state = TCP_CLOSING;
            }
            /* TCP-SM-14: the FIN is end-of-file for a reader (RFC 793 3.9
             * eighth check, "signal the user") -- only ESTABLISHED woke
             * it; a half-closed reader waited out its poll backstop. */
            sched_wakeup(p->recv_chan);
            tcp_send_ctl(p, TCP_ACK);
            break;
        case TCP_FIN_WAIT_2:
            p->time_wait_until = get_ticks() + TCP_TIME_WAIT_TICKS;   /* TCP-MEM-09: deadline first */
            p->state = TCP_TIME_WAIT;
            sched_wakeup(p->recv_chan);                               /* TCP-SM-14 */
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
        p->time_wait_until = get_ticks() + TCP_TIME_WAIT_TICKS;   /* TCP-MEM-09: deadline first */
        p->state = TCP_TIME_WAIT;
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

/*
 * The PCB-touching half of tcp_input(): lookup, window update and state
 * dispatch.  Always entered with tcp_lock() held -- see the note at the
 * top of this file for why the RX side must take it too.
 */
static void tcp_input_locked(uint32_t saddr, uint32_t daddr,
                             const struct tcphdr *th, uint8_t flags,
                             uint16_t sport, uint16_t dport,
                             uint32_t seq, uint32_t ack,
                             const uint8_t *payload, size_t dlen,
                             uint16_t syn_mss)
{
    tcp_pcb_t *p = tcp_find(saddr, sport, daddr, dport);
    if (p && (flags & TCP_SYN))
        p->syn_mss = syn_mss;           /* TCP-HDR-02: for the handlers */
    if (!p) {
        tcp_send_rst(saddr, daddr, th, flags, seq, ack, dlen);
        return;
    }

    /* TCP-URG-01: where the urgent data ends, from the untrimmed segment.
     * SEG.UP is wire-controlled: clamp it to the segment's text (URG-06). */
    uint32_t urg_end = 0;
    if (flags & TCP_URG) {
        uint16_t up = __builtin_bswap16(th->urg_ptr);
        if (up > dlen) up = (uint16_t)dlen;
        if (up) urg_end = seq + up;
    }

    /* TCP-SM-02/-05: sequence check and trim first, before any field of an
     * unacceptable segment -- its window included -- is believed. */
    switch (p->state) {
    case TCP_ESTABLISHED:
    case TCP_FIN_WAIT_1:
    case TCP_FIN_WAIT_2:
    case TCP_CLOSE_WAIT:
    case TCP_CLOSING:
    case TCP_LAST_ACK:
    case TCP_TIME_WAIT:
        if (!tcp_seg_check(p, &seq, &flags, &payload, &dlen))
            return;
        break;
    default:
        break;
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
    /* TCP-WIN-05: whether this segment repeats the window last seen. */
    p->seg_wnd_same = (__builtin_bswap16(th->window) == p->snd_wnd);
    if ((flags & TCP_ACK) && p->state != TCP_LISTEN && p->state != TCP_SYN_SENT) {
        /* Window updates track the highest ACK seen, so an old duplicate
         * cannot walk the window backwards. */
        /*
         * TCP-WIN-12: and, among segments carrying the same ACK, only the
         * newest (by SEG.SEQ, then SEG.ACK) -- RFC 793 3.9's SND.WL1 /
         * SND.WL2 test.  A pure window update carries the same ACK as the
         * one before it, so a reordered older segment used to overwrite a
         * newer window: a stale "open" undid a real zero window, or a stale
         * zero parked the sender.  The ACK's lower bound stays SND.UNA =<
         * SEG.ACK, as RFC 1122 4.2.2.20(g) corrects 793's "<" -- with "<"
         * a window update could never reopen a zero window.
         */
        if ((int32_t)(ack - p->snd_una) >= 0 &&
            (int32_t)(ack - tcp_ack_limit(p)) <= 0 &&           /* TCP-MEM-07 */
            (!p->snd_wl_valid || (int32_t)(p->snd_wl1 - seq) < 0 ||
             (p->snd_wl1 == seq && (int32_t)(p->snd_wl2 - ack) <= 0))) {
            p->snd_wnd = __builtin_bswap16(th->window);
            if (p->snd_wnd > p->max_snd_wnd) p->max_snd_wnd = p->snd_wnd;   /* TCP-WIN-06 */
            p->snd_wl1 = seq;
            p->snd_wl2 = ack;
            p->snd_wl_valid = 1;
        }
    } else if (p->state == TCP_LISTEN || p->state == TCP_SYN_SENT) {
        p->snd_wnd = __builtin_bswap16(th->window);
        if (p->snd_wnd > p->max_snd_wnd) p->max_snd_wnd = p->snd_wnd;   /* TCP-WIN-06 */
        if (p->state == TCP_SYN_SENT) {         /* TCP-WIN-12: from the SYN */
            p->snd_wl1 = seq;
            p->snd_wl2 = ack;
            p->snd_wl_valid = 1;
        }
    }

    switch (p->state) {
    case TCP_LISTEN:
        tcp_in_listen(p, saddr, daddr, th, sport, dport, seq, ack, flags, dlen);
        return;
    case TCP_SYN_SENT:
        tcp_in_syn_sent(p, seq, ack, flags);
        return;
    case TCP_SYN_RECEIVED:
        if (tcp_in_syn_received(p, &seq, ack, &flags, &payload, &dlen))
            tcp_in_established(p, seq, ack, flags, payload, dlen, urg_end);   /* TCP-SM-09 */
        return;
    case TCP_ESTABLISHED:
    case TCP_FIN_WAIT_1:
    case TCP_FIN_WAIT_2:
    case TCP_CLOSE_WAIT:
    case TCP_CLOSING:
    case TCP_LAST_ACK:
    case TCP_TIME_WAIT:
        tcp_in_established(p, seq, ack, flags, payload, dlen, urg_end);
        return;
    default:
        return;
    }
}

/*
 * TCP-HDR-02: walk the options of a SYN and return the peer's MSS, or 0 if
 * it sent none.  Options were never parsed.  Bounded against the header:
 * EOL (0) ends the list, NOP (1) is one octet, and every other kind needs a
 * length octet in [2, remaining] -- a zero length would otherwise loop
 * forever and an over-long one read past the header.  An MSS is kind 2,
 * length 4.
 */
static uint16_t tcp_parse_mss(const uint8_t *o, size_t n) {
    uint16_t mss = 0;
    size_t i = 0;
    while (i < n) {
        uint8_t kind = o[i];
        if (kind == 0) break;                       /* EOL */
        if (kind == 1) { i++; continue; }           /* NOP */
        if (n - i < 2) break;
        uint8_t olen = o[i + 1];
        if (olen < 2 || olen > n - i) break;        /* malformed: stop */
        if (kind == 2 && olen == 4)
            mss = (uint16_t)((o[i + 2] << 8) | o[i + 3]);
        i += olen;
    }
    return mss;
}

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

    uint16_t syn_mss = (flags & TCP_SYN)
        ? tcp_parse_mss(seg + sizeof(*th), hlen - sizeof(*th)) : 0;

    uint32_t f = tcp_lock();
    tcp_input_locked(saddr, daddr, th, flags, sport, dport, seq, ack,
                     payload, dlen, syn_mss);
    tcp_unlock(f);
}

/* ------------------------------------------------------------------ */
/* Public socket-layer API                                            */
/* ------------------------------------------------------------------ */

tcp_pcb_t *tcp_alloc(void) {
    tcp_pcb_t *p = (tcp_pcb_t *)kmalloc(sizeof(*p));
    if (!p) return NULL;
    memset(p, 0, sizeof(*p));
    p->state = TCP_CLOSED;
    ip4_txopts_init(&p->txo);
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
    tcp_ooo_free_all(p);                                 /* TCP-WIN-08 */
    if (p->rxbuf)    kfree(p->rxbuf, TCP_RING_LEN);
    if (p->accept_q) kfree(p->accept_q, sizeof(tcp_pcb_t *) * p->accept_cap);
    kfree(p, sizeof(*p));
}

/*
 * TCP-API-04: RFC 793 2.7 -- a connection is identified by its pair of
 * sockets, so the local socket must be unique among the PCBs that can
 * still receive.  The only EADDRINUSE test was the socket layer's, which
 * sees sockets, not PCBs: accepted children (never marked bound), and the
 * PCBs a closed socket leaves behind in FIN-WAIT/LAST-ACK/TIME-WAIT, were
 * invisible to it.  A daemon restarting a second after it closed found
 * nothing taken, and tcp_find() then matched a client's SYN to the old
 * LAST-ACK PCB instead of the new listener: the client blackholed.
 *
 * Caller holds tcp_lock.  Any non-CLOSED PCB on the same port whose local
 * address overlaps (either wildcard, or equal) conflicts, except with
 * SO_REUSEADDR: then a PCB that has a foreign socket (an accepted child, a
 * client, or the remains of a closed connection, TIME-WAIT included) does
 * not block a new local socket -- the BSD rule that lets a daemon restart
 * while old connections drain -- and a LISTEN PCB is left to the socket
 * layer's consent test (UDP-API-01: both flags, same owner).
 */
static int tcp_local_conflict_locked(const tcp_pcb_t *self, uint32_t laddr,
                                     uint16_t lport, int reuseaddr) {
    for (const tcp_pcb_t *o = g_tcp_pcbs; o; o = o->next) {
        if (o == self || o->state == TCP_CLOSED || o->lport != lport) continue;
        if (o->laddr && laddr && o->laddr != laddr) continue;   /* disjoint */
        if (reuseaddr && (o->raddr != 0 || o->state == TCP_LISTEN)) continue;
        return 1;
    }
    return 0;
}

int tcp_bind(tcp_pcb_t *p, uint32_t laddr, uint16_t lport, int reuseaddr) {
    uint32_t f = tcp_lock();
    if (lport && tcp_local_conflict_locked(p, laddr, lport, reuseaddr)) {
        tcp_unlock(f);
        return -EADDRINUSE;
    }
    p->laddr = laddr;
    p->lport = lport;
    tcp_unlock(f);
    return 0;
}

int tcp_listen(tcp_pcb_t *p, int backlog) {
    if (backlog < 1)  backlog = 1;
    if (backlog > 32) backlog = 32;
    /* TCP-API-06: a passive OPEN is legal only from CLOSED (RFC 793 3.9:
     * any other state is "connection already exists"), plus the
     * already-LISTEN backlog change below.  The state was overwritten from
     * anywhere, so listen() on a connected socket turned it into a
     * listener whose 4-tuple still matched the peer's segments -- which
     * tcp_in_listen() then discarded, blackholing the peer with no RST. */
    if (p->state != TCP_CLOSED && p->state != TCP_LISTEN)
        return -EINVAL;
    /*
     * TCP-32: a second listen() overwrote p->accept_q with a fresh
     * allocation without freeing the old one -- leaking 8*accept_cap and
     * orphaning any children already queued on it, which then never got
     * accepted or reaped.  POSIX allows listen() on an already-listening
     * socket purely to change the backlog, so migrate the pending children
     * into a queue of the new size.
     *
     * TCP-MEM-11: and do it under tcp_lock -- the RX path appends to this
     * queue from interrupt context -- with the new array allocated before
     * the lock is taken and the old one freed after.  A shrink resets and
     * detaches the children beyond the new cap, as tcp_close()'s LISTEN arm
     * does for all of them; merely truncating accept_count left them
     * established on the peer's side and never accepted, reset or reaped.
     * The array is always reallocated at exactly the new size, since
     * tcp_free() frees it by accept_cap.
     */
    tcp_pcb_t **nq = (tcp_pcb_t **)kmalloc(sizeof(tcp_pcb_t *) * backlog);
    if (!nq) return -ENOMEM;
    /* TCP-MEM-05: never let an unwritten slot hold a stale heap word. */
    memset(nq, 0, sizeof(tcp_pcb_t *) * backlog);

    uint32_t f = tcp_lock();
    if (p->state != TCP_CLOSED && p->state != TCP_LISTEN) {
        tcp_unlock(f);                          /* TCP-API-06: raced an open */
        kfree(nq, sizeof(tcp_pcb_t *) * backlog);
        return -EINVAL;
    }
    tcp_pcb_t **oq = p->accept_q;
    int ocap = p->accept_cap;
    int keep = p->accept_count < backlog ? p->accept_count : backlog;
    for (int i = 0; i < keep; i++) nq[i] = oq[i];
    for (int i = keep; oq && i < p->accept_count; i++) {
        tcp_pcb_t *q = oq[i];
        q->parent   = NULL;
        q->detached = 1;            /* timer reaps once CLOSED */
        if (q->state != TCP_CLOSED && q->state != TCP_TIME_WAIT)
            tcp_send_ctl(q, TCP_RST | TCP_ACK);
        tcp_kill_pcb(q, ECONNRESET);
    }
    p->accept_q     = nq;
    p->accept_cap   = backlog;
    p->accept_count = keep;
    p->state        = TCP_LISTEN;
    p->listen       = 1;
    tcp_unlock(f);
    if (oq) kfree(oq, sizeof(tcp_pcb_t *) * ocap);
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
#define TCP_EPH_LO    49152u
#define TCP_EPH_SPAN  (65536u - TCP_EPH_LO)

/* TCP-MEM-12: one bit per dynamic-range port, rebuilt under tcp_lock by
 * each allocation. */
static uint32_t tcp_eph_map[TCP_EPH_SPAN / 32];

/* Caller holds tcp_lock, and assigns the returned port before dropping it.
 * `r` is the random starting point, drawn before the lock was taken.
 *
 * TCP-MEM-12: this walked the PCB list once per candidate with no lock
 * from preemptible process context, and the chosen port was assigned
 * only afterwards, so a concurrent connect() could claim the same port in
 * between.  Under the lock one pass over the list marks every port in use,
 * and the candidates are then tested against that bitmap, so the locked
 * work is O(PCBs + range) rather than O(PCBs x candidates). */
/* TCP-API-09: a port is unavailable for a connection to (raddr, rport) only
 * if a live PCB already uses it toward that same peer (with an overlapping
 * local address), or a listener owns it wholesale.  Keying on the local
 * port alone made every connection anywhere consume a port from the
 * 16384-wide range, so a busy client exhausted it after 16384 concurrent
 * connections however many peers they were spread over. */
static uint16_t tcp_alloc_ephemeral_locked(const tcp_pcb_t *self, uint32_t r,
                                           uint32_t raddr, uint16_t rport) {
    memset(tcp_eph_map, 0, sizeof(tcp_eph_map));
    for (tcp_pcb_t *o = g_tcp_pcbs; o; o = o->next) {
        if (o == self || o->state == TCP_CLOSED) continue;
        if (o->state != TCP_LISTEN &&
            (o->raddr != raddr || o->rport != rport ||
             (o->laddr && self->laddr && o->laddr != self->laddr)))
            continue;
        if (o->lport >= TCP_EPH_LO) {
            uint32_t i = o->lport - TCP_EPH_LO;
            tcp_eph_map[i / 32] |= 1u << (i % 32);
        }
    }
    uint32_t base = r % TCP_EPH_SPAN;
    for (uint32_t k = 0; k < TCP_EPH_SPAN; k++) {
        uint32_t i = (base + k) % TCP_EPH_SPAN;
        if (!(tcp_eph_map[i / 32] & (1u << (i % 32))))
            return (uint16_t)(TCP_EPH_LO + i);
    }
    return 0;
}

/*
 * TCP-API-02 / TCP-API-03: RFC 793 3.8 OPEN, by state.  An active open is
 * legal only from CLOSED.  connect() used to be gated on the socket
 * layer's `connected` flag, which is set only once the handshake has
 * completed (or, non-blocking, when it was started): a second connect()
 * while the first was still in SYN-SENT restarted the handshake with a new
 * ISS, and connect() on a LISTEN socket silently turned the listener into
 * a client, orphaning every child it had.
 */
static int tcp_open_check_locked(const tcp_pcb_t *p) {
    switch (p->state) {
    case TCP_CLOSED:       return 0;
    case TCP_LISTEN:       return -EOPNOTSUPP;   /* what POSIX and BSD do */
    case TCP_SYN_SENT:
    case TCP_SYN_RECEIVED: return -EALREADY;
    default:               return -EISCONN;
    }
}

/*
 * TCP-API-01: a PCB that reached CLOSED through a failed or finished
 * connection still holds that connection's state.  Re-opening it used to
 * queue the new SYN behind the old one, so the retransmit timer resent the
 * OLD SYN (old ISS) and the new connection never formed; so_error, the
 * congestion state and the receive side all leaked into the new attempt
 * too.  Return everything to the freshly allocated state.  Caller holds
 * tcp_lock and has checked the state is CLOSED.
 */
static void tcp_reset_for_open_locked(tcp_pcb_t *p) {
    tcp_unacked_free_all(p);
    tcp_ooo_free_all(p);
    p->so_error  = 0;
    p->last_ack  = 0;
    p->dup_ack   = 0;
    p->cwnd      = 0;
    p->ssthresh  = 0;
    p->rcv_nxt   = 0;
    p->rx_head   = p->rx_tail = 0;
    p->rx_count  = 0;
    p->snd_wnd   = 0;
    p->max_snd_wnd = 0;
    p->rto       = 0;
    p->rtt_valid = 0;
    p->ut_deadline     = 0;
    p->closing_until   = 0;
    p->time_wait_until = 0;
    p->fin_wait2_until = 0;
    p->shut_rd   = 0;
    p->urg_have  = p->urg_extract = p->oob_valid = 0;
    p->urg_mark_valid = p->urg_sig_pending = 0;
    p->snd_up_valid   = 0;
    p->pollout_wait   = 0;
}

static int tcp_connect_start(tcp_pcb_t *p, uint32_t raddr, uint16_t rport) {
    uint32_t f = tcp_lock();
    int rc = tcp_open_check_locked(p);
    if (rc == 0) tcp_reset_for_open_locked(p);     /* TCP-API-01 */
    tcp_unlock(f);
    if (rc) return rc;
    /* random_get_bytes() returns the byte count on success, not 0.  The
     * test was inverted, so the candidate ALWAYS came from get_ticks(): two
     * connects within one 4 ms tick started from the same port, and a port
     * whose last connection had just closed was handed straight back out --
     * onto a 4-tuple the peer still held in TIME-WAIT, which answered the
     * new SYN with a RST.  Ports were predictable as well (TCP-07). */
    uint32_t r = 0;
    if (random_get_bytes(&r, sizeof(r)) != (int)sizeof(r))
        r = (uint32_t)get_ticks();
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
    /* TCP-MEM-12: choose the port and publish it -- with the state that
     * makes the next allocation's scan count it -- in one locked section.
     * tcp_alloc_ephemeral_locked() skips CLOSED PCBs, so a port assigned
     * while the PCB was still CLOSED was invisible to a concurrent
     * connect(). */
    f = tcp_lock();
    if (p->state != TCP_CLOSED) {
        /* Another thread opened it between the two locked sections. */
        tcp_unlock(f);
        return -EALREADY;
    }
    if (!p->lport) p->lport = tcp_alloc_ephemeral_locked(p, r, raddr, rport);
    if (!p->lport) {
        /* TCP-API-08: the range is exhausted.  This was discarded and the
         * SYN went out from port 0. */
        tcp_unlock(f);
        return -EADDRNOTAVAIL;
    }
    /* TCP-API-04: and the 4-tuple itself must be unique before the SYN
     * goes out.  A bound socket keeps its port here, so two connects from
     * the same local socket to the same peer built byte-identical tuples
     * and tcp_find() fed both streams to whichever PCB came first. */
    for (const tcp_pcb_t *o = g_tcp_pcbs; o; o = o->next) {
        if (o == p || o->state == TCP_CLOSED) continue;
        if (o->lport == p->lport && o->rport == rport && o->raddr == raddr &&
            (o->laddr == 0 || p->laddr == 0 || o->laddr == p->laddr)) {
            tcp_unlock(f);
            return -EADDRINUSE;
        }
    }
    p->raddr   = raddr;
    p->rport   = rport;
    tcp_set_mtu_mss(p);                                /* TCP-HDR-04 */
    p->iss     = tcp_new_iss(p->laddr, p->lport, raddr, rport);   /* TCP-HDR-05 */
    p->snd_una = p->iss;
    p->snd_nxt = p->iss;
    p->snd_max_valid = 0;             /* TCP-MEM-07: a new ISS, nothing sent */
    p->rcv_adv_edge_valid = 0;        /* TCP-WIN-07: no peer sequence yet */
    p->snd_wl_valid = 0;              /* TCP-WIN-12 */
    p->state   = TCP_SYN_SENT;
    tcp_unlock(f);
    /* Queue the SYN — the retx timer will resend it on RTO if the
     * server didn't get it.  */
    rc = tcp_xmit_queue(p, TCP_SYN, NULL, 0);
    if (rc) {
        /* TCP-API-08: no memory for the SYN.  The PCB used to be left in
         * SYN-SENT with nothing queued, so nothing would ever retransmit
         * or time it out.  Back to CLOSED, where a retry is legal. */
        f = tcp_lock();
        p->state = TCP_CLOSED;
        tcp_unlock(f);
        return rc;
    }
    return 0;
}

int tcp_connect(tcp_pcb_t *p, uint32_t raddr, uint16_t rport) {
    int ret;
    tcp_hold(p);                                    /* TCP-01 */
    ret = tcp_connect_start(p, raddr, rport);
    if (ret) { tcp_unhold(p); return ret; }
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
    int rc = tcp_connect_start(p, raddr, rport);
    if (rc) return rc;
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
    if ((events & POLLPRI) && p->oob_valid)
        revents |= POLLPRI;                     /* TCP-URG-01 */
    if (events & POLLOUT) {
        /*
         * TCP-WIN-02: writable means a write() would make progress.  There
         * is no send buffer, so that is room in min(cwnd, peer window) --
         * or a zero window with nothing in flight, where a write sends the
         * persist probe.  POLLOUT used to be unconditional, so a
         * non-blocking sender facing a closed window spun on poll() and
         * EAGAIN.  A connection whose send side is finished never blocks a
         * write (it fails), so it stays writable.
         */
        if (p->state == TCP_ESTABLISHED || p->state == TCP_CLOSE_WAIT) {
            uint32_t in_flight = p->snd_nxt - p->snd_una;
            uint32_t wnd = p->snd_wnd;
            if (p->cwnd && p->cwnd < wnd) wnd = p->cwnd;
            uint32_t avail = wnd > in_flight ? wnd - in_flight : 0;
            /* TCP-WIN-06: and room the silly-window rule would let a
             * full-sized write use, so poll() and a write() that holds a
             * tinygram back cannot disagree and spin. */
            if (in_flight == 0 || avail >= tcp_eff_mss(p) ||
                (avail && p->max_snd_wnd && avail >= p->max_snd_wnd / 2))
                revents |= POLLOUT;
            else
                p->pollout_wait = 1;    /* the ACK path wakes the poller */
        } else {
            revents |= POLLOUT;
        }
    }
    if (p->state == TCP_CLOSE_WAIT) revents |= POLLHUP;
    /* TCP-API-13: once both directions are closed (our FIN and theirs),
     * nothing more can move either way: hang-up.  POLLOUT stays set after
     * the local FIN, as on Linux and the BSDs -- a write then fails at once
     * with EPIPE/SIGPIPE, which is how a poller learns the send side is
     * gone, instead of never being woken for it. */
    if (p->state == TCP_CLOSING || p->state == TCP_LAST_ACK ||
        p->state == TCP_TIME_WAIT)
        revents |= POLLHUP;
    /* If the caller asked for POLLIN but we don't have data yet,
     * advertise recv_chan so the poll layer can sleep on the right
     * queue.  Previously gated on `!revents`, which never fired
     * because POLLOUT-always-ready left revents non-zero — caller
     * then fell back to a different wait channel and missed the
     * recv wakeup entirely (the inetutils-telnet symptom). */
    if (wait_chan && (events & POLLIN) && !(revents & (POLLIN | POLLHUP)))
        *wait_chan = p->recv_chan;
    /* TCP-WIN-02: an fd has one wait channel, and it is recv_chan whenever
     * POLLIN is also pending, so the ACK that opens the window wakes
     * recv_chan as well when pollout_wait is set. */
    if (wait_chan && !*wait_chan && (events & POLLOUT) && !(revents & POLLOUT))
        *wait_chan = p->recv_chan;
    return revents;
}

/* TCP-API-22: the socket layer's view of "connected" comes from here, not
 * from a flag it set when a non-blocking connect() merely started.
 * Synchronized: both SYNs acknowledged (ESTABLISHED and every closing
 * state) -- what getpeername() requires. */
int tcp_is_synchronized(const tcp_pcb_t *p) {
    if (!p) return 0;
    switch (p->state) {
    case TCP_ESTABLISHED: case TCP_FIN_WAIT_1: case TCP_FIN_WAIT_2:
    case TCP_CLOSE_WAIT:  case TCP_CLOSING:    case TCP_LAST_ACK:
    case TCP_TIME_WAIT:
        return 1;
    default:
        return 0;
    }
}

/* Has a connection, open or still opening -- what shutdown() requires. */
int tcp_has_connection(const tcp_pcb_t *p) {
    return p && p->state != TCP_CLOSED && p->state != TCP_LISTEN;
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

/*
 * TCP-WIN-06: sender silly-window avoidance and Nagle (RFC 793 3.7's
 * suggestions as RFC 1122 4.2.3.4 makes them precise).  Send a segment of
 * `chunk` octets only when
 *   - it is a full MSS, or
 *   - nothing is unacknowledged (so interactive traffic is not delayed), or
 *   - the usable window is at least half the largest the peer has
 *     offered, or
 *   - it finishes the user's write and no small segment is still unacked.
 * Otherwise every ACK that freed a few octets drew a segment of exactly
 * that size, and a window-limited transfer degenerated into tinygrams.
 */
static int tcp_sws_ok(tcp_pcb_t *p, size_t chunk, size_t remaining,
                      uint32_t avail, uint32_t in_flight) {
    if (chunk >= tcp_eff_mss(p) || in_flight == 0)          /* TCP-HDR-02 */
        return 1;
    if (p->max_snd_wnd && avail >= p->max_snd_wnd / 2)
        return 1;
    if (chunk == remaining) {
        int small = 0;
        uint32_t f = tcp_lock();
        for (tcp_seg_t *s = p->unacked_head; s; s = s->next)
            if (s->dlen && s->dlen < tcp_eff_mss(p)) { small = 1; break; }
        tcp_unlock(f);
        return !small;
    }
    return 0;
}

static ssize_t tcp_send_body(tcp_pcb_t *p, const void *buf, size_t len, int nonblock,
                             uint64_t deadline) {
    const uint8_t *b = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        /* TCP-API-14: RFC 793 3.9 SEND in SYN-SENT/SYN-RECEIVED queues the
         * data for transmission once the connection is established.  It
         * failed ENOTCONN instead, so a client that wrote right after a
         * non-blocking connect() lost its first write.  Wait for the
         * handshake (a blocking socket) or report EAGAIN (non-blocking);
         * the state check below then decides as usual. */
        if (p->state == TCP_SYN_SENT || p->state == TCP_SYN_RECEIVED) {
            if (nonblock) return sent ? (ssize_t)sent : -EAGAIN;
            if (deadline && get_ticks() >= deadline)
                return sent ? (ssize_t)sent : -EAGAIN;
            current_thread->flags |= THREAD_F_INTERRUPTIBLE;
            sched_sleep_until(p->connect_chan, get_ticks() + TCP_SLEEP_POLL);
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
            if (current_thread->sig_pending & ~current_thread->sig_mask)
                return sent ? (ssize_t)sent : -EINTR;
            continue;
        }
        if (p->state != TCP_ESTABLISHED && p->state != TCP_CLOSE_WAIT) {
            /* A connection that was up and then failed (RST ->
             * ECONNRESET, RTO -> ETIMEDOUT) reports EPIPE; one that
             * was never connected reports ENOTCONN.
             *
             * TCP-API-13: and so do the five closing states -- RFC 793
             * 3.9 SEND: "error: connection closing".  They have so_error
             * 0 after an ordinary shutdown(SHUT_WR) or close, so a write
             * after the local FIN reported ENOTCONN, the code for a
             * connection that never existed. */
            if (sent) return (ssize_t)sent;
            switch (p->state) {
            case TCP_FIN_WAIT_1:
            case TCP_FIN_WAIT_2:
            case TCP_CLOSING:
            case TCP_LAST_ACK:
            case TCP_TIME_WAIT:
                return -EPIPE;
            default:
                return p->so_error ? -EPIPE : -ENOTCONN;
            }
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
            if (in_flight == 0) {
                /* Zero window, nothing outstanding — emit a one-byte
                 * persist probe.  Its RTO retransmissions keep
                 * prodding the peer until it re-advertises a window,
                 * so no separate persist timer is needed.
                 *
                 * TCP-WIN-01: marked as a probe, so the timer does not
                 * charge those retransmissions to the abort budget while
                 * the window stays shut (RFC 793 3.7: keep probing). */
                tcp_seg_t *ps = tcp_seg_alloc(TCP_ACK | TCP_PSH, b + sent, 1);
                if (!ps) return sent ? (ssize_t)sent : -ENOMEM;
                ps->probe = 1;
                uint32_t lf = tcp_lock();
                uint32_t pseq = tcp_seg_link_locked(p, ps);
                tcp_unlock(lf);
                tcp_seg_emit(p, pseq, TCP_ACK | TCP_PSH, b + sent, 1);
                sent += 1;
                continue;
            }
            /* Non-blocking sender: return what we managed to send (or
             * EAGAIN) instead of parking.  A single-threaded nonblocking
             * pump must be able to return from write() and go read() —
             * otherwise it can never drain the peer to reopen the window
             * and the transfer self-deadlocks.
             *
             * TCP-WIN-02: after the probe above, not before it.  A
             * non-blocking sender facing a zero window with nothing in
             * flight got EAGAIN and sent nothing, so no probe ever went
             * out and nothing would reopen the window. */
            if (nonblock) return sent ? (ssize_t)sent : -EAGAIN;
            /* UDP-API-04: SO_SNDTIMEO -- give up once the deadline has
             * passed, reporting what was sent (or EAGAIN). */
            if (deadline && get_ticks() >= deadline)
                return sent ? (ssize_t)sent : -EAGAIN;
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
        if (chunk > tcp_eff_mss(p))  chunk = tcp_eff_mss(p);   /* TCP-HDR-02 */
        if (chunk > avail)           chunk = avail;
        if (!tcp_sws_ok(p, chunk, len - sent, avail, in_flight)) {
            /* TCP-WIN-06: hold the tail back until an ACK makes it worth
             * a segment -- exactly the zero-window wait above. */
            if (nonblock) return sent ? (ssize_t)sent : -EAGAIN;
            if (deadline && get_ticks() >= deadline)
                return sent ? (ssize_t)sent : -EAGAIN;
            current_thread->flags |= THREAD_F_INTERRUPTIBLE;
            sched_sleep_until(p->send_chan, get_ticks() + TCP_SLEEP_POLL);
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
            if (current_thread->sig_pending & ~current_thread->sig_mask)
                return sent ? (ssize_t)sent : -EINTR;
            continue;
        }
        int rc = tcp_xmit_queue(p, TCP_ACK | TCP_PSH, b + sent, chunk);
        if (rc < 0) return sent ? (ssize_t)sent : rc;
        sent += chunk;
    }
    return (ssize_t)sent;
}

/*
 * TCP-MEM-03: the send loop sleeps for window and re-reads p->state,
 * snd_nxt and snd_una on every wake.  Without a hold, a close() on another
 * thread plus a peer RST can drive the PCB to CLOSED and let the reaper free
 * it -- and its ring -- while this thread is asleep.  Pin it for the whole
 * call, exactly as tcp_recv() does (TCP-01).
 */
static ssize_t tcp_send_impl(tcp_pcb_t *p, const void *buf, size_t len, int nonblock,
                             uint64_t deadline) {
    tcp_hold(p);
    ssize_t r = tcp_send_body(p, buf, len, nonblock, deadline);
    tcp_unhold(p);
    return r;
}

ssize_t tcp_send(tcp_pcb_t *p, const void *buf, size_t len) {
    return tcp_send_impl(p, buf, len, /*nonblock=*/0, 0);
}
ssize_t tcp_send_until(tcp_pcb_t *p, const void *buf, size_t len, uint64_t deadline) {
    return tcp_send_impl(p, buf, len, /*nonblock=*/0, deadline);
}
ssize_t tcp_send_nb(tcp_pcb_t *p, const void *buf, size_t len) {
    return tcp_send_impl(p, buf, len, /*nonblock=*/1, 0);
}

/*
 * TCP-URG-02: send(..., MSG_OOB) -- RFC 793 3.9 SEND with the urgent flag:
 * SND.UP <- the end of this data, so its last octet is the urgent one.  The
 * pointer is set before the data is queued so every segment carrying it is
 * flagged; tcp_xmit_raw() keeps setting URG until SND.UNA passes SND.UP.
 * The send path could never set URG before.
 */
ssize_t tcp_send_urg_until(tcp_pcb_t *p, const void *buf, size_t len,
                           int nonblock, uint64_t deadline) {
    if (len == 0)
        return tcp_send_impl(p, buf, len, nonblock, deadline);
    uint32_t f = tcp_lock();
    p->snd_up       = p->snd_nxt + (uint32_t)len;
    p->snd_up_valid = 1;
    tcp_unlock(f);
    return tcp_send_impl(p, buf, len, nonblock, deadline);
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
        size_t n = p->rx_count < len ? p->rx_count : len;
        /* TCP-URG-01: a read stops at the urgent mark (BSD), so the reader
         * sees SIOCATMARK true there; the next read goes past it. */
        if (p->urg_mark_valid) {
            if (p->urg_mark_left > 0 && n > p->urg_mark_left)
                n = p->urg_mark_left;
            if (p->urg_mark_left > 0)
                p->urg_mark_left -= (uint32_t)n;
            else
                p->urg_mark_valid = 0;          /* read past the mark */
        }
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
         * Matches BSD's silly-window-syndrome avoidance shape.
         *
         * TCP-WIN-03: measured against the window last put on the wire,
         * not against the window at entry to this call.  An application
         * reading in pieces smaller than an MSS never freed an MSS in one
         * call, so no update went out at all, and the sender sat on its
         * zero window until its probe timer.  A 0 -> non-zero transition
         * is always announced. */
        /* TCP-WIN-07: compare what would now be advertised. */
        uint32_t new_wnd = tcp_rcv_wnd_calc(p);
        uint32_t adv = p->last_adv_wnd;
        uint32_t step = TCP_MSS < TCP_RING_LEN / 2 ? TCP_MSS : TCP_RING_LEN / 2;
        if ((new_wnd >= adv + step || (adv == 0 && new_wnd > 0)) &&
            (p->state == TCP_ESTABLISHED ||
             p->state == TCP_FIN_WAIT_1  ||
             p->state == TCP_FIN_WAIT_2)) {
            tcp_send_ctl(p, TCP_ACK);
        }
        return (ssize_t)n;
    }
    /*
     * TCP-SM-08: a connection that died -- RST (ECONNRESET) or an exhausted
     * retransmission budget (ETIMEDOUT) -- reaches CLOSED with so_error set
     * by tcp_kill_pcb().  Treating that CLOSED like the post-FIN states
     * below reported it as a clean end-of-file, so a reader took a reset
     * for a complete reply.  Report the error once the queued data is
     * gone, and consume it, so the next read sees EOF (as BSD and Linux
     * do).  An orderly close reaches CLOSED with so_error 0.
     */
    if (p->state == TCP_CLOSED && p->so_error) {
        int err = p->so_error;
        p->so_error = 0;
        tcp_unlock(lf);
        return -err;
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

/* UDP-API-04: deadline is an absolute tick count (SO_RCVTIMEO), 0 for none;
 * past it an empty receive returns -EAGAIN. */
ssize_t tcp_recv_until(tcp_pcb_t *p, void *buf, size_t len, uint64_t deadline) {
    ssize_t ret;
    tcp_hold(p);                                    /* TCP-01 */
    for (;;) {
        ssize_t r = tcp_recv_nb(p, buf, len);
        if (r != -EAGAIN) { ret = r; break; }
        if (deadline && get_ticks() >= deadline) { ret = -EAGAIN; break; }
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

ssize_t tcp_recv(tcp_pcb_t *p, void *buf, size_t len) {
    return tcp_recv_until(p, buf, len, 0);
}

/* MSG_PEEK: copy up to len bytes from the rx ring WITHOUT consuming them,
 * so a follow-up recv() still sees the same data.  Same EOF/EAGAIN
 * signalling as tcp_recv_nb. */
ssize_t tcp_peek_nb(tcp_pcb_t *p, void *buf, size_t len) {
    uint32_t lf = tcp_lock();
    if (p->shut_rd) { tcp_unlock(lf); return 0; }
    if (p->rx_count > 0) {
        size_t n = p->rx_count < len ? p->rx_count : len;
        if (p->urg_mark_valid && p->urg_mark_left > 0 && n > p->urg_mark_left)
            n = p->urg_mark_left;               /* TCP-URG-01: as tcp_recv_nb */
        uint8_t *b = (uint8_t *)buf;
        uint32_t tail = p->rx_tail;
        for (size_t i = 0; i < n; i++) {
            b[i] = p->rxbuf[tail];
            tail = (tail + 1) % TCP_RING_LEN;
        }
        tcp_unlock(lf);
        return (ssize_t)n;
    }
    /* TCP-SM-08: as tcp_recv_nb(), but a peek leaves the error pending
     * for the read that follows it. */
    if (p->state == TCP_CLOSED && p->so_error) {
        int err = p->so_error;
        tcp_unlock(lf);
        return -err;
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

/*
 * TCP-MEM-04: tcp_recv()'s twin minus the hold.  It sleeps and then copies
 * out of p->rxbuf, and tcp_free() releases rxbuf before the PCB, so a
 * reaped PCB meant copying a freed 32 KiB ring to userspace.  Pin it for the
 * whole call, as tcp_recv() does (TCP-01).
 */
ssize_t tcp_peek_until(tcp_pcb_t *p, void *buf, size_t len, uint64_t deadline) {
    ssize_t ret;
    tcp_hold(p);
    for (;;) {
        ssize_t r = tcp_peek_nb(p, buf, len);
        if (r != -EAGAIN) { ret = r; break; }
        if (deadline && get_ticks() >= deadline) { ret = -EAGAIN; break; }
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

ssize_t tcp_peek(tcp_pcb_t *p, void *buf, size_t len) {
    return tcp_peek_until(p, buf, len, 0);
}

/* UDP-API-12: the socket layer's IP_TTL/IP_TOS for this connection. */
void tcp_set_txopts(tcp_pcb_t *p, const struct ip4_txopts *o) {
    if (!p || !o) return;
    uint32_t f = tcp_lock();
    p->txo = *o;
    tcp_unlock(f);
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
    /* TCP-API-12: RFC 1122 4.2.2.13 -- closing a connection whose received
     * data the application never read is an abort, not a CLOSE.  That data
     * was acknowledged and is about to be thrown away; an orderly FIN would
     * tell the peer it was all consumed (a request "fully delivered" to a
     * server that never saw it).  Send the RST that says otherwise. */
    {
        uint32_t uf = tcp_lock();
        int unread = (p->rx_count > 0 || p->ooo_head) &&
                     (p->state == TCP_ESTABLISHED || p->state == TCP_CLOSE_WAIT ||
                      p->state == TCP_FIN_WAIT_1  || p->state == TCP_FIN_WAIT_2 ||
                      p->state == TCP_SYN_RECEIVED);
        tcp_unlock(uf);
        if (unread)
            return tcp_abort(p);
    }
    /* Snapshot + transition state under the lock so a concurrent RX
     * (which may itself transition state or free the PCB) can't
     * interleave.  tcp_free for the already-dead states is done
     * inside the lock; the FIN-emitting paths drop the lock before
     * tcp_xmit_queue, which is itself lock-bracketed.
     *
     * TCP-MEM-10: the FIN's sequence number is reserved and the segment
     * linked under the same lock that publishes FIN_WAIT_1/LAST_ACK; only
     * the transmit happens after the unlock.  Publishing the state first
     * and sequencing the FIN later let a segment processed in the gap see
     * the closing state with snd_nxt not yet covering the FIN and an empty
     * unacked queue -- so an ACK of our data alone moved FIN_WAIT_1 to
     * FIN_WAIT_2, or LAST_ACK straight to CLOSED, before any FIN existed. */
    tcp_seg_t *fin = tcp_seg_alloc(TCP_FIN | TCP_ACK, NULL, 0);
    uint32_t fin_seq = 0;
    if (!fin) {
        /* TCP-API-19: no memory for the FIN.  The state used to move to
         * FIN-WAIT-1/LAST-ACK with nothing queued: no FIN ever went out,
         * nothing retransmitted, and no deadline reaped the PCB.  A
         * connection that cannot be closed gracefully is aborted -- the
         * RST needs no allocation. */
        uint32_t af = tcp_lock();
        int live = p->state == TCP_ESTABLISHED || p->state == TCP_CLOSE_WAIT ||
                   p->state == TCP_SYN_RECEIVED;
        tcp_unlock(af);
        if (live)
            return tcp_abort(p);
    }
    uint32_t f = tcp_lock();
    /* The owning socket is being destroyed — mark the PCB orphaned so
     * the timer reaps it once it reaches CLOSED, and so any data that
     * still arrives for it gets a RST (there is no socket to take it). */
    p->detached = 1;
    int st = p->state;
    switch (st) {
    case TCP_ESTABLISHED:
        p->state = TCP_FIN_WAIT_1;
        if (fin) fin_seq = tcp_seg_link_locked(p, fin);
        tcp_unlock(f);
        if (fin) tcp_seg_emit(p, fin_seq, TCP_FIN | TCP_ACK, NULL, 0);
        return 0;
    case TCP_CLOSE_WAIT:
        p->state = TCP_LAST_ACK;
        if (fin) fin_seq = tcp_seg_link_locked(p, fin);
        tcp_unlock(f);
        if (fin) tcp_seg_emit(p, fin_seq, TCP_FIN | TCP_ACK, NULL, 0);
        return 0;
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
    case TCP_SYN_RECEIVED:
        /* TCP-API-20: RFC 793 3.9 CLOSE in SYN-RECEIVED: "form a FIN
         * segment and send it, and enter FIN-WAIT-1", as ESTABLISHED does.
         * It dropped straight to CLOSED with neither FIN nor RST, leaving
         * the peer -- which may already be ESTABLISHED -- with a
         * connection this side had silently forgotten. */
        p->state = TCP_FIN_WAIT_1;
        if (fin) fin_seq = tcp_seg_link_locked(p, fin);
        tcp_unlock(f);
        if (fin) tcp_seg_emit(p, fin_seq, TCP_FIN | TCP_ACK, NULL, 0);
        return 0;
    case TCP_SYN_SENT:
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
    if (fin) kfree(fin, sizeof(*fin));   /* no FIN to send */
    return 0;
}

/*
 * TCP-API-11: RFC 793 3.9 ABORT.  Tear the connection down at once: queued
 * data is discarded, a synchronized peer is sent <SEQ=SND.NXT><CTL=RST>,
 * and waiters get ECONNRESET.  There was no ABORT at all -- close() was
 * the only way out, always graceful -- so SO_LINGER {1, 0} (the POSIX way
 * to ask for an abortive close) reported success and did nothing.
 *
 * Like tcp_close(), this consumes the socket's reference: the PCB is
 * detached and the timer reaps it.  A listener is handled by tcp_close()'s
 * LISTEN arm, which already resets every child.
 */
int tcp_abort(tcp_pcb_t *p) {
    if (!p) return -ENOTCONN;
    uint32_t f = tcp_lock();
    if (p->state == TCP_LISTEN) {
        tcp_unlock(f);
        return tcp_close(p);
    }
    p->detached = 1;
    switch (p->state) {
    case TCP_SYN_RECEIVED:
    case TCP_ESTABLISHED:
    case TCP_FIN_WAIT_1:
    case TCP_FIN_WAIT_2:
    case TCP_CLOSE_WAIT:
        /* Inline under the lock, as tcp_close()'s LISTEN arm does (A45):
         * ip4_output() does not sleep with interrupts off (NET-05). */
        tcp_xmit_raw(p, p->snd_nxt, TCP_RST, NULL, 0);
        tcp_ooo_free_all(p);
        tcp_kill_pcb(p, ECONNRESET);         /* frees the send queue */
        break;
    case TCP_SYN_SENT:
    case TCP_CLOSING:
    case TCP_LAST_ACK:
    case TCP_TIME_WAIT:
        /* No RST: SYN-SENT has no synchronized peer, and in the last three
         * the peer has already closed its side ("delete the TCB"). */
        tcp_ooo_free_all(p);
        tcp_kill_pcb(p, ECONNRESET);
        break;
    default:
        break;                                /* CLOSED: nothing to do */
    }
    tcp_unlock(f);
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
    /* TCP-MEM-10: sequence the FIN under the lock that publishes the
     * closing state (see tcp_close). */
    tcp_seg_t *fin = tcp_seg_alloc(TCP_FIN | TCP_ACK, NULL, 0);
    uint32_t fin_seq = 0;
    uint32_t f = tcp_lock();
    if (!fin && (p->state == TCP_ESTABLISHED || p->state == TCP_SYN_RECEIVED ||
                 p->state == TCP_CLOSE_WAIT)) {
        /* TCP-API-19: no memory for the FIN.  Leave the state as it is so
         * the caller can retry, rather than entering a closing state with
         * no FIN to send. */
        tcp_unlock(f);
        return -ENOMEM;
    }
    switch (p->state) {
    case TCP_ESTABLISHED:
    case TCP_SYN_RECEIVED:
        /* TCP-API-15: SYN-RECEIVED sends its FIN too (RFC 793 3.9 CLOSE:
         * "If no SENDs have been issued ... form a FIN segment and send
         * it, and enter FIN-WAIT-1").  It used to be ignored, so the
         * connection came up and never closed its send side. */
        p->state = TCP_FIN_WAIT_1;
        break;
    case TCP_CLOSE_WAIT:
        p->state = TCP_LAST_ACK;
        break;
    case TCP_SYN_SENT:
        /* TCP-API-15: nothing is synchronized to FIN, and RFC 793 3.9 CLOSE
         * in SYN-SENT deletes the TCB with "error: closing" (as Linux and
         * the BSDs do).  It was discarded: the handshake carried on and
         * the connection came up with its send side still open. */
        tcp_ooo_free_all(p);
        tcp_kill_pcb(p, ECONNABORTED);
        tcp_unlock(f);
        if (fin) kfree(fin, sizeof(*fin));
        return 0;
    default:
        /* The rest already sent their FIN (or are gone): idempotent. */
        tcp_unlock(f);
        if (fin) kfree(fin, sizeof(*fin));
        return 0;
    }
    if (fin) fin_seq = tcp_seg_link_locked(p, fin);
    tcp_unlock(f);
    if (fin) tcp_seg_emit(p, fin_seq, TCP_FIN | TCP_ACK, NULL, 0);
    return 0;
}

/*
 * tcp_shutdown_rd — shutdown(fd, SHUT_RD): close the receive
 * direction.  recv() returns EOF from now on; a reader already
 * blocked in tcp_recv() is woken so it observes the new state.
 */
/* TCP-URG-01: SIOCATMARK -- the next octet to be read is the one that
 * followed the urgent octet. */
int tcp_sockatmark(tcp_pcb_t *p) {
    if (!p) return 0;
    uint32_t f = tcp_lock();
    int at = p->urg_mark_valid && p->urg_mark_left == 0;
    tcp_unlock(f);
    return at;
}

/*
 * TCP-URG-04: recv(..., MSG_OOB) -- the urgent octet taken out of the
 * stream by tcp_rx_put(), as BSD returns it.  -EWOULDBLOCK while the peer
 * has announced urgent data that has not arrived yet, -EINVAL when there is
 * none (or it was already read).  MSG_PEEK leaves it in place.
 */
ssize_t tcp_recv_oob(tcp_pcb_t *p, void *buf, size_t len, int peek) {
    if (!p) return -ENOTCONN;
    if (len == 0) return 0;
    uint32_t f = tcp_lock();
    if (!p->oob_valid) {
        int pending = p->urg_extract;
        tcp_unlock(f);
        return pending ? -EWOULDBLOCK : -EINVAL;
    }
    *(uint8_t *)buf = p->oob_byte;
    if (!peek) p->oob_valid = 0;
    tcp_unlock(f);
    return 1;
}

/* TCP-URG-01: F_SETOWN / F_GETOWN -- who receives SIGURG: a pid, or a
 * process group as -pgrp. */
void tcp_set_owner(tcp_pcb_t *p, int owner) {
    if (p) p->owner = owner;
}

int tcp_get_owner(const tcp_pcb_t *p) {
    return p ? p->owner : 0;
}

/* TCP-WIN-13: TCP_USER_TIMEOUT, in milliseconds; 0 restores the default.
 * Takes effect from the next time the unacknowledged queue is (re)armed. */
int tcp_set_user_timeout(tcp_pcb_t *p, uint32_t ms) {
    if (!p) return -ENOTCONN;
    uint32_t f = tcp_lock();
    p->user_timeout_ms = ms;
    if (p->unacked_head)
        p->ut_deadline = get_ticks() + tcp_ut_ticks(p);
    tcp_unlock(f);
    return 0;
}

uint32_t tcp_get_user_timeout(const tcp_pcb_t *p) {
    return p ? p->user_timeout_ms : 0;
}

int tcp_shutdown_rd(tcp_pcb_t *p) {
    if (!p) return -ENOTCONN;
    /* TCP-WIN-10: nothing will ever read the ring again, so empty it (and
     * the reassembly queue) and from now on acknowledge and discard what
     * arrives, as BSD does.  The data used to be buffered regardless,
     * pinning the advertised window at zero once the ring filled -- the
     * peer's sends then stalled for good. */
    uint32_t f = tcp_lock();
    p->shut_rd = 1;
    p->rx_count = 0;
    p->rx_tail  = p->rx_head;
    tcp_ooo_free_all(p);
    int reopen = p->last_adv_wnd < TCP_RING_LEN &&
                 (p->state == TCP_ESTABLISHED || p->state == TCP_FIN_WAIT_1 ||
                  p->state == TCP_FIN_WAIT_2);
    tcp_unlock(f);
    if (reopen)
        tcp_send_ctl(p, TCP_ACK);       /* announce the reopened window */
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
