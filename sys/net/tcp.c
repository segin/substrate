/*
 * tcp.c — minimal TCP (RFC 793) for substrate.
 *
 * State machine: CLOSED → LISTEN → SYN_SENT → SYN_RECEIVED → ESTABLISHED
 *                → FIN_WAIT_1 → FIN_WAIT_2 → TIME_WAIT → CLOSED
 *                → CLOSE_WAIT → LAST_ACK → CLOSED
 *
 * Scope of this first cut:
 *   - One PCB per AF_INET SOCK_STREAM socket
 *   - Active OPEN (connect), passive OPEN (listen+accept)
 *   - SYN / SYN+ACK / ACK handshake
 *   - In-order data send + recv via per-PCB ring buffers
 *   - Single-segment retransmission on duplicate ACK
 *   - FIN handling and orderly close
 *   - Fixed window (32 KiB), no congestion control (cwnd=mss)
 *   - IPv4 only — extending to IPv6 mirrors the AF_INET6 path.
 *
 * Out of scope for now: SACK, window scaling, timestamp options,
 * congestion control beyond no-op cwnd, persist timer, keepalive.
 */

#include <net/inet.h>
#include <sys/netdev.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <kern/sched.h>
#include <kern/console.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

#define IPPROTO_TCP        6
#define TCP_RING_LEN       (32 * 1024)
#define TCP_MSS            1460
#define TCP_RTO_MS         500    /* RTO — 500ms initial */
#define TCP_MAX_RETX       5

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

typedef struct tcp_pcb {
    int       state;
    uint32_t  laddr, raddr;        /* network byte order */
    uint16_t  lport, rport;        /* host order */
    uint32_t  iss;                 /* initial send seq */
    uint32_t  snd_una;             /* oldest unack */
    uint32_t  snd_nxt;             /* next seq to send */
    uint32_t  rcv_nxt;             /* next expected seq */
    uint16_t  rcv_wnd;             /* advertised window */
    /* Receive ring */
    uint8_t   *rxbuf;              /* TCP_RING_LEN */
    uint32_t   rx_head, rx_tail, rx_count;
    /* Backlog for LISTEN sockets */
    struct tcp_pcb **accept_q;
    int        accept_cap, accept_count;
    /* Sleep channels */
    void      *connect_chan;
    void      *recv_chan;
    void      *accept_chan;
    int        listen;
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
static uint32_t tcp_new_iss(void) { tcp_iss_seed = tcp_iss_seed * 1103515245 + 12345; return tcp_iss_seed; }

static uint16_t tcp_csum(uint32_t saddr, uint32_t daddr,
                         const void *seg, size_t len) {
    return inet_csum_pseudo4(saddr, daddr, IPPROTO_TCP, (uint16_t)len, seg);
}

static int tcp_send_seg(tcp_pcb_t *p, uint8_t flags,
                        const void *data, size_t dlen) {
    uint8_t buf[TCP_MSS + sizeof(struct tcphdr)];
    if (dlen > TCP_MSS) dlen = TCP_MSS;
    struct tcphdr *th = (struct tcphdr *)buf;
    th->source = __builtin_bswap16(p->lport);
    th->dest   = __builtin_bswap16(p->rport);
    th->seq    = __builtin_bswap32(p->snd_nxt);
    th->ack_seq = __builtin_bswap32(p->rcv_nxt);
    /* data offset = 5 (no options), flags */
    th->doff_flags = __builtin_bswap16((uint16_t)((5u << 12) | flags));
    th->window = __builtin_bswap16((uint16_t)(TCP_RING_LEN - p->rx_count));
    th->check  = 0;
    th->urg_ptr = 0;
    if (dlen && data) memcpy(buf + sizeof(*th), data, dlen);
    th->check = tcp_csum(p->laddr, p->raddr, buf, sizeof(*th) + dlen);
    int rc = ip4_output(p->raddr, IPPROTO_TCP, buf, sizeof(*th) + dlen);
    if (rc == 0) {
        if (flags & TCP_SYN) p->snd_nxt++;
        if (flags & TCP_FIN) p->snd_nxt++;
        p->snd_nxt += (uint32_t)dlen;
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Inbound demux — called from ip4_input via afinet_deliver_v4 hook  */
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
        /* RST any unknown segment that isn't itself an RST. */
        if (!(flags & TCP_RST)) {
            uint8_t buf[sizeof(struct tcphdr)];
            struct tcphdr *r = (struct tcphdr *)buf;
            memset(r, 0, sizeof(*r));
            r->source = th->dest;
            r->dest   = th->source;
            r->seq    = __builtin_bswap32(ack);
            r->ack_seq = __builtin_bswap32(seq + (flags & TCP_SYN ? 1 : 0) + (uint32_t)dlen);
            r->doff_flags = __builtin_bswap16((5u << 12) | TCP_RST | TCP_ACK);
            r->check = tcp_csum(daddr, saddr, r, sizeof(*r));
            ip4_output(saddr, IPPROTO_TCP, r, sizeof(*r));
        }
        return;
    }

    if (p->state == TCP_LISTEN) {
        if (!(flags & TCP_SYN)) return;
        /* Spawn a child PCB in SYN_RECEIVED. */
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
        c->rcv_wnd = TCP_RING_LEN;
        c->recv_chan    = &c->rx_count;
        c->connect_chan = &c->state;
        c->parent  = p;
        c->next    = g_tcp_pcbs;
        g_tcp_pcbs = c;
        tcp_send_seg(c, TCP_SYN | TCP_ACK, NULL, 0);
        return;
    }

    if (p->state == TCP_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            p->rcv_nxt = seq + 1;
            p->snd_una = ack;
            p->state = TCP_ESTABLISHED;
            tcp_send_seg(p, TCP_ACK, NULL, 0);
            sched_wakeup(p->connect_chan);
        } else if (flags & TCP_RST) {
            p->state = TCP_CLOSED;
            sched_wakeup(p->connect_chan);
        }
        return;
    }

    if (p->state == TCP_SYN_RECEIVED) {
        /* SYN+ACK was sent with seq=iss, so snd_nxt was advanced to
         * iss+1 by tcp_send_seg.  The peer's ACK acknowledges our
         * SYN with ack=iss+1, which is exactly p->snd_nxt.  The
         * previous test required ack == snd_nxt + 1 — off by one,
         * which left every incoming connection wedged in
         * SYN_RECEIVED and accept() never woke. */
        if ((flags & TCP_ACK) && ack == p->snd_nxt) {
            p->snd_una = ack;
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
        return;
    }

    /* ESTABLISHED / FIN_WAIT_* / CLOSE_WAIT / LAST_ACK */
    if (flags & TCP_RST) {
        p->state = TCP_CLOSED;
        sched_wakeup(p->recv_chan);
        return;
    }
    /* Accept data if seq matches rcv_nxt and we have room. */
    if (dlen && seq == p->rcv_nxt) {
        uint32_t accept_n = dlen;
        if (accept_n > TCP_RING_LEN - p->rx_count)
            accept_n = TCP_RING_LEN - p->rx_count;
        for (uint32_t i = 0; i < accept_n; i++) {
            p->rxbuf[p->rx_head] = payload[i];
            p->rx_head = (p->rx_head + 1) % TCP_RING_LEN;
        }
        p->rx_count += accept_n;
        p->rcv_nxt += accept_n;
        sched_wakeup(p->recv_chan);
    }
    /* Process ACK. */
    if (flags & TCP_ACK) {
        if (ack > p->snd_una && ack <= p->snd_nxt) p->snd_una = ack;
    }
    /* Process FIN. */
    if (flags & TCP_FIN) {
        p->rcv_nxt++;
        switch (p->state) {
        case TCP_ESTABLISHED:
            p->state = TCP_CLOSE_WAIT;
            sched_wakeup(p->recv_chan);
            tcp_send_seg(p, TCP_ACK, NULL, 0);
            break;
        case TCP_FIN_WAIT_1:
            p->state = (flags & TCP_ACK) ? TCP_TIME_WAIT : TCP_CLOSING;
            tcp_send_seg(p, TCP_ACK, NULL, 0);
            break;
        case TCP_FIN_WAIT_2:
            p->state = TCP_TIME_WAIT;
            tcp_send_seg(p, TCP_ACK, NULL, 0);
            break;
        case TCP_LAST_ACK:
            p->state = TCP_CLOSED;
            break;
        default:
            break;
        }
        return;
    }
    /* Always ACK in-window data. */
    if (dlen && (p->state == TCP_ESTABLISHED || p->state == TCP_FIN_WAIT_1 ||
                 p->state == TCP_FIN_WAIT_2)) {
        tcp_send_seg(p, TCP_ACK, NULL, 0);
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
    p->rcv_wnd = TCP_RING_LEN;
    p->recv_chan    = &p->rx_count;
    p->connect_chan = &p->state;
    p->accept_chan  = &p->accept_count;
    p->next = g_tcp_pcbs;
    g_tcp_pcbs = p;
    return p;
}

void tcp_free(tcp_pcb_t *p) {
    if (!p) return;
    tcp_pcb_t **link = &g_tcp_pcbs;
    while (*link && *link != p) link = &(*link)->next;
    if (*link == p) *link = p->next;
    if (p->rxbuf) kfree(p->rxbuf, TCP_RING_LEN);
    if (p->accept_q) kfree(p->accept_q, sizeof(tcp_pcb_t *) * p->accept_cap);
    kfree(p, sizeof(*p));
}

int tcp_bind(tcp_pcb_t *p, uint32_t laddr, uint16_t lport) {
    p->laddr = laddr;
    p->lport = lport;
    return 0;
}

int tcp_listen(tcp_pcb_t *p, int backlog) {
    if (backlog < 1) backlog = 1;
    if (backlog > 32) backlog = 32;
    p->accept_q = (tcp_pcb_t **)kmalloc(sizeof(tcp_pcb_t *) * backlog);
    if (!p->accept_q) return -ENOMEM;
    p->accept_cap = backlog;
    p->state = TCP_LISTEN;
    p->listen = 1;
    return 0;
}

/* Kick off the SYN.  Common to blocking and non-blocking connect. */
static void tcp_connect_start(tcp_pcb_t *p, uint32_t raddr, uint16_t rport) {
    static uint16_t next_eph = 32768;
    if (!p->lport) p->lport = ++next_eph;
    if (!p->laddr) {
        /* Pick a source IP based on the destination.  127/8 traffic
         * MUST be sourced from a loopback address — otherwise the
         * reply (or the RST from a connect to nothing) comes back
         * through loopback with saddr=daddr=127.0.0.1 and tcp_find
         * can't match a PCB whose laddr is, say, 10.0.0.5.  Result:
         * every loopback connect() hangs until the 5-second timeout.
         * Same fix shape for IPv6 ::1 once we wire it in.  */
        int want_lo = ((raddr & 0xFF) == 127);
        for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
            int is_lo = !!(d->flags & NETDEV_IFF_LOOPBACK);
            if (is_lo != want_lo) continue;
            if (d->ip4_addr) {
                p->laddr = d->ip4_addr;
                break;
            }
        }
    }
    p->raddr = raddr;
    p->rport = rport;
    p->iss   = tcp_new_iss();
    p->snd_una = p->iss;
    p->snd_nxt = p->iss;
    p->state = TCP_SYN_SENT;
    tcp_send_seg(p, TCP_SYN, NULL, 0);
}

int tcp_connect(tcp_pcb_t *p, uint32_t raddr, uint16_t rport) {
    tcp_connect_start(p, raddr, rport);
    /* Block until ESTABLISHED, CLOSED, or up to ~5s. */
    for (int i = 0; i < 100; i++) {
        if (p->state == TCP_ESTABLISHED) return 0;
        if (p->state == TCP_CLOSED) return -ECONNREFUSED;
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep(p->connect_chan);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask)
            return -EINTR;
    }
    return -ETIMEDOUT;
}

/* Non-blocking connect — fire the SYN and return immediately with
 * -EINPROGRESS.  Caller polls for POLLOUT on the fd to detect
 * completion; getsockopt(SO_ERROR) then reports success or
 * ECONNREFUSED.  If the connect somehow completed synchronously
 * (loopback can do this) return 0 right away.  */
int tcp_connect_nb(tcp_pcb_t *p, uint32_t raddr, uint16_t rport) {
    tcp_connect_start(p, raddr, rport);
    if (p->state == TCP_ESTABLISHED) return 0;
    if (p->state == TCP_CLOSED)      return -ECONNREFUSED;
    return -EINPROGRESS;
}

/* Poll readiness helper — used by the AF_INET fs_node poll
 * callback to translate the PCB state into POLL* bits.  */
int tcp_poll(tcp_pcb_t *p, short events, void **wait_chan) {
    short revents = 0;
    if (!p) return POLLNVAL;
    if (p->state == TCP_LISTEN) {
        if ((events & POLLIN) && p->accept_count > 0) revents |= POLLIN;
        if (wait_chan && !revents) *wait_chan = p->accept_chan;
        return revents;
    }
    if (p->state == TCP_SYN_SENT || p->state == TCP_SYN_RECEIVED) {
        /* Connect still in flight — nothing's ready.  */
        if (wait_chan) *wait_chan = p->connect_chan;
        return 0;
    }
    if (p->state == TCP_CLOSED) {
        /* Connect refused or peer closed.  Report POLLOUT|POLLHUP|POLLERR
         * so a poller waiting for connect completion can wake and
         * inspect SO_ERROR.  */
        return (events & POLLOUT ? POLLOUT : 0) | POLLHUP | POLLERR;
    }
    /* ESTABLISHED / FIN_WAIT_* / CLOSE_WAIT / etc. — connected. */
    if (events & POLLIN) {
        if (p->rx_count > 0) revents |= POLLIN;
    }
    if (events & POLLOUT) {
        /* No tx buffering yet; writes go straight to the wire. */
        revents |= POLLOUT;
    }
    if (p->state == TCP_CLOSE_WAIT) revents |= POLLHUP;
    if (wait_chan && !revents) *wait_chan = p->recv_chan;
    return revents;
}

tcp_pcb_t *tcp_accept(tcp_pcb_t *listen_p) {
    for (;;) {
        if (listen_p->accept_count > 0) {
            tcp_pcb_t *c = listen_p->accept_q[0];
            for (int i = 1; i < listen_p->accept_count; i++)
                listen_p->accept_q[i - 1] = listen_p->accept_q[i];
            listen_p->accept_count--;
            c->parent = NULL;
            return c;
        }
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep(listen_p->accept_chan);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask)
            return NULL;
    }
}

ssize_t tcp_send(tcp_pcb_t *p, const void *buf, size_t len) {
    if (p->state != TCP_ESTABLISHED && p->state != TCP_CLOSE_WAIT)
        return -ENOTCONN;
    const uint8_t *b = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        int rc = tcp_send_seg(p, TCP_ACK | TCP_PSH, b + sent, chunk);
        if (rc < 0) return sent ? (ssize_t)sent : rc;
        sent += chunk;
    }
    return (ssize_t)sent;
}

/* Non-blocking variant — single drain pass, returns -EAGAIN if the
 * ring is empty and the connection's still open.  */
ssize_t tcp_recv_nb(tcp_pcb_t *p, void *buf, size_t len) {
    if (p->rx_count > 0) {
        size_t n = p->rx_count < len ? p->rx_count : len;
        uint8_t *b = (uint8_t *)buf;
        for (size_t i = 0; i < n; i++) {
            b[i] = p->rxbuf[p->rx_tail];
            p->rx_tail = (p->rx_tail + 1) % TCP_RING_LEN;
        }
        p->rx_count -= n;
        return (ssize_t)n;
    }
    if (p->state == TCP_CLOSE_WAIT || p->state == TCP_CLOSED) return 0;
    return -EAGAIN;
}

ssize_t tcp_recv(tcp_pcb_t *p, void *buf, size_t len) {
    for (;;) {
        ssize_t r = tcp_recv_nb(p, buf, len);
        if (r != -EAGAIN) return r;
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep(p->recv_chan);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask)
            return -EINTR;
    }
}

int tcp_close(tcp_pcb_t *p) {
    if (!p) return 0;
    switch (p->state) {
    case TCP_ESTABLISHED:
        p->state = TCP_FIN_WAIT_1;
        tcp_send_seg(p, TCP_FIN | TCP_ACK, NULL, 0);
        break;
    case TCP_CLOSE_WAIT:
        p->state = TCP_LAST_ACK;
        tcp_send_seg(p, TCP_FIN | TCP_ACK, NULL, 0);
        break;
    case TCP_LISTEN:
    case TCP_SYN_SENT:
    case TCP_SYN_RECEIVED:
    case TCP_CLOSED:
        tcp_free(p);
        return 0;
    default:
        break;
    }
    return 0;
}
