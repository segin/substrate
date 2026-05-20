/*
 * lib/c/src/posix_extra3.c — sockets API surface.
 *
 * Three blocks:
 *
 *   1. arpa/inet.h leftovers — inet_addr / inet_ntoa.  Pure
 *      userspace ASCII conversion on top of the existing
 *      inet_pton / inet_ntop.
 *
 *   2. netdb.h legacy resolver entries — local-file lookups
 *      against /etc/hosts, /etc/services, /etc/protocols,
 *      /etc/networks.  Substrate has no DNS yet, so the
 *      gethostbyname path only returns answers that appear in
 *      /etc/hosts.  The setXent/getXent/endXent iterator API is
 *      backed by per-family FILE* state; thread-unsafe (matches
 *      the spec, which says callers must serialize).
 *
 *   3. (historical) accept4 + sockatmark used to live here; the
 *      whole socket family now sits in src/socket.c as real
 *      wrappers over the kernel AF_UNIX / AF_INET socket syscalls.
 *
 * Memory model for the resolver entries: each get*ent / get*by*
 * call returns a pointer to a STATIC struct + STATIC char buffers
 * — the next call overwrites both.  This matches glibc's
 * non-_r behaviour.  Reentrancy needs the _r variants (not yet
 * implemented).
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ============================================================
 * arpa/inet.h — inet_addr / inet_ntoa.
 * ============================================================ */

in_addr_t inet_addr(const char *cp) {
    struct in_addr a;
    if (inet_pton(AF_INET, cp, &a) != 1) return (in_addr_t)-1;
    return a.s_addr;
}

char *inet_ntoa(struct in_addr in) {
    static char buf[16];   /* "xxx.xxx.xxx.xxx\0" = 16 */
    if (inet_ntop(AF_INET, &in, buf, sizeof(buf)) == NULL) return NULL;
    return buf;
}

/* ============================================================
 * netdb.h — local-file resolvers.
 *
 * Shared parsing pattern:
 *   - one entry per non-comment, non-blank line
 *   - whitespace-separated columns
 *   - '#' starts a line- or trailing-comment
 * ============================================================ */

int h_errno = 0;

const char *hstrerror(int err)
{
    switch (err) {
    case 0:              return "Resolver Error 0 (no error)";
    case HOST_NOT_FOUND: return "Unknown host";
    case TRY_AGAIN:      return "Host name lookup failure";
    case NO_RECOVERY:    return "Unknown server error";
    case NO_DATA:        return "No address associated with name";
    default:             return "Unknown resolver error";
    }
}

void herror(const char *s)
{
    if (s && *s) fprintf(stderr, "%s: %s\n", s, hstrerror(h_errno));
    else         fprintf(stderr, "%s\n",    hstrerror(h_errno));
}

/* Per-family FILE* state.  NULL = not opened; reopened lazily on
 * first call.  setXent(1) "stay open" — we always stay open
 * because closing and reopening doesn't buy substrate anything
 * (no caching layer behind it). */
static FILE *g_hosts_fp = NULL;
static FILE *g_serv_fp  = NULL;
static FILE *g_proto_fp = NULL;
static FILE *g_net_fp   = NULL;

/* Trim a line in place to the part before '#' / '\n' / '\r'. */
static void rstrip_comment(char *p) {
    for (char *s = p; *s; s++) {
        if (*s == '#' || *s == '\n' || *s == '\r') { *s = '\0'; return; }
    }
}

/* Split a line on whitespace, return NULL-terminated token array.
 * Tokens point into the original buffer (in-place mutation).
 * Caps at `max` tokens; remainder discarded.  Returns count. */
static int tokenize(char *line, char **out, int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        out[n++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

/* ---- /etc/hosts: gethostent / gethostbyname / gethostbyaddr -- */

#define MAX_ALIASES 16
#define MAX_ADDRS 8

static struct hostent  g_hostent;
static char            g_hostent_line[1024];
static char           *g_hostent_aliases[MAX_ALIASES + 1];
static char            g_hostent_addr_buf[MAX_ADDRS * sizeof(struct in_addr)];
static char           *g_hostent_addr_list[MAX_ADDRS + 1];

void sethostent(int stayopen) {
    (void)stayopen;
    if (g_hosts_fp) rewind(g_hosts_fp);
    else g_hosts_fp = fopen("/etc/hosts", "r");
}

void endhostent(void) {
    if (g_hosts_fp) { fclose(g_hosts_fp); g_hosts_fp = NULL; }
}

struct hostent *gethostent(void) {
    if (!g_hosts_fp) {
        g_hosts_fp = fopen("/etc/hosts", "r");
        if (!g_hosts_fp) { h_errno = NO_RECOVERY; return NULL; }
    }
    char *line;
    while ((line = fgets(g_hostent_line, sizeof(g_hostent_line), g_hosts_fp))) {
        rstrip_comment(line);
        char *tok[MAX_ALIASES + 2];
        int n = tokenize(line, tok, MAX_ALIASES + 2);
        if (n < 2) continue;   /* need at least IP + name */

        struct in_addr a;
        if (inet_pton(AF_INET, tok[0], &a) != 1) continue;
        memcpy(g_hostent_addr_buf, &a, sizeof(a));
        g_hostent_addr_list[0] = g_hostent_addr_buf;
        g_hostent_addr_list[1] = NULL;

        g_hostent.h_name = tok[1];
        for (int i = 2, j = 0; i < n && j < MAX_ALIASES; i++, j++) {
            g_hostent_aliases[j] = tok[i];
        }
        g_hostent_aliases[(n - 2 < MAX_ALIASES) ? (n - 2) : MAX_ALIASES] = NULL;

        g_hostent.h_aliases   = g_hostent_aliases;
        g_hostent.h_addrtype  = AF_INET;
        g_hostent.h_length    = sizeof(struct in_addr);
        g_hostent.h_addr_list = g_hostent_addr_list;
        return &g_hostent;
    }
    h_errno = HOST_NOT_FOUND;
    return NULL;
}

/* DNS resolver — queries each nameserver from /etc/resolv.conf in
 * order, sends a UDP/53 A-record query, parses the first answer.
 *
 * Implementation notes:
 *   - Minimal RFC 1035 parser; only IN/A and IN/AAAA records.
 *   - No retransmission inside one nameserver, but moves on to the
 *     next nameserver on timeout/parse-failure.
 *   - Single-shot: blocks up to ~2s per nameserver before giving up.
 */

static int dns_encode_name(uint8_t *out, size_t outsz, const char *name) {
    size_t pos = 0;
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t lab = dot ? (size_t)(dot - p) : strlen(p);
        if (lab == 0 || lab > 63) return -1;
        if (pos + 1 + lab >= outsz) return -1;
        out[pos++] = (uint8_t)lab;
        memcpy(out + pos, p, lab);
        pos += lab;
        if (!dot) break;
        p = dot + 1;
    }
    if (pos + 1 > outsz) return -1;
    out[pos++] = 0;  /* root label */
    return (int)pos;
}

static int dns_skip_name(const uint8_t *pkt, size_t pktlen, size_t pos) {
    while (pos < pktlen) {
        uint8_t l = pkt[pos];
        if (l == 0) return (int)(pos + 1);
        if ((l & 0xC0) == 0xC0) return (int)(pos + 2);
        pos += 1 + l;
    }
    return -1;
}

/* Returns 1 on success, 0 on not-found, -1 on error. */
static int dns_query_v4(const struct sockaddr_in *server,
                        const char *name,
                        uint8_t addr_out[4]) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    uint8_t req[512];
    uint16_t txid = (uint16_t)(rand() & 0xFFFF);
    req[0] = txid >> 8; req[1] = txid & 0xFF;
    req[2] = 0x01; req[3] = 0x00;          /* RD set */
    req[4] = 0;    req[5] = 1;             /* qdcount=1 */
    req[6] = req[7] = req[8] = req[9] = req[10] = req[11] = 0;
    int n = dns_encode_name(req + 12, sizeof(req) - 12, name);
    if (n < 0) { close(s); return -1; }
    size_t off = 12 + n;
    if (off + 4 > sizeof(req)) { close(s); return -1; }
    req[off++] = 0; req[off++] = 1;        /* QTYPE=A */
    req[off++] = 0; req[off++] = 1;        /* QCLASS=IN */
    if (sendto(s, req, off, 0, (const struct sockaddr *)server,
               sizeof(*server)) != (ssize_t)off) { close(s); return -1; }

    /* Pass MSG_DONTWAIT on every recv so the substrate AF_INET
     * socket layer returns -EAGAIN immediately when no datagram
     * has arrived, rather than parking the calling thread in a
     * non-interruptible sleep.  (substrate's afinet_recvfrom only
     * honors the per-call MSG_DONTWAIT flag; an fcntl(O_NONBLOCK)
     * on the fd has no effect there.)  Loop at ~20 ms cadence
     * for a 2 s budget, then give up.  Without this, ^C cannot
     * reach the thread and any unresolvable host wedges ping(8)
     * forever. */
    uint8_t resp[1500];
    int got = -1;
    for (int i = 0; i < 100; i++) {
        ssize_t r = recv(s, resp, sizeof(resp), MSG_DONTWAIT);
        if (r > 0) { got = (int)r; break; }
        if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                     errno != EINTR) {
            break;
        }
        usleep(20000);
    }
    close(s);
    if (got <= 0) return -1;
    if (got < 12) return -1;
    if (resp[0] != req[0] || resp[1] != req[1]) return -1;
    int qd = (resp[4] << 8) | resp[5];
    int an = (resp[6] << 8) | resp[7];
    int pos = 12;
    for (int i = 0; i < qd; i++) {
        pos = dns_skip_name(resp, got, pos);
        if (pos < 0 || pos + 4 > got) return -1;
        pos += 4;
    }
    for (int i = 0; i < an; i++) {
        pos = dns_skip_name(resp, got, pos);
        if (pos < 0 || pos + 10 > got) return -1;
        int type   = (resp[pos] << 8) | resp[pos + 1];
        int rdlen  = (resp[pos + 8] << 8) | resp[pos + 9];
        pos += 10;
        if (pos + rdlen > got) return -1;
        if (type == 1 && rdlen == 4) {
            memcpy(addr_out, resp + pos, 4);
            return 1;
        }
        pos += rdlen;
    }
    return 0;
}

/*
 * Parse a /etc/resolv.conf-style file once into a parallel pair of
 * lists: server IPs (in network byte order) and search domains.
 * Both are caller-bounded.  `search` and `domain` are treated as
 * synonyms — POSIX-style "either, last one wins" semantics; the
 * caller's `searches` array receives every label.
 *
 * Returns the number of nameservers found, or -1 on file open
 * failure.  search_count_out is filled in unconditionally.
 */
static int resolv_parse(struct sockaddr_in *servers, int srv_max,
                        char (*searches)[256], int search_max,
                        int *search_count_out) {
    int srv_n = 0;
    int srch_n = 0;
    FILE *f = fopen("/etc/resolv.conf", "r");
    if (!f) { *search_count_out = 0; return -1; }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (strncmp(p, "nameserver", 10) == 0 &&
            (p[10] == ' ' || p[10] == '\t')) {
            p += 10;
            while (*p == ' ' || *p == '\t') p++;
            char *end = p;
            while (*end && *end != '\n' && *end != ' ' && *end != '\t' && *end != '#') end++;
            *end = '\0';
            if (srv_n < srv_max) {
                struct sockaddr_in *srv = &servers[srv_n];
                memset(srv, 0, sizeof(*srv));
                srv->sin_family = AF_INET;
                srv->sin_port = __builtin_bswap16(53);
                if (inet_pton(AF_INET, p, &srv->sin_addr) == 1) {
                    srv_n++;
                }
            }
            continue;
        }

        /* Both `search` and `domain` populate the search list.
         * `search` accepts up to 6 whitespace-separated labels per
         * RFC 1123; `domain` takes a single label. */
        const char *kw = NULL;
        int kwlen = 0;
        if (strncmp(p, "search", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            kw = "search"; kwlen = 6;
        } else if (strncmp(p, "domain", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            kw = "domain"; kwlen = 6;
        }
        if (kw) {
            p += kwlen;
            while (*p == ' ' || *p == '\t') p++;
            /* Tokenize whitespace-separated labels until EOL/#. */
            while (*p && *p != '\n' && *p != '#' && srch_n < search_max) {
                char *tok = p;
                while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '#') p++;
                size_t n = (size_t)(p - tok);
                if (n > 0 && n < sizeof(searches[0])) {
                    memcpy(searches[srch_n], tok, n);
                    searches[srch_n][n] = '\0';
                    srch_n++;
                }
                while (*p == ' ' || *p == '\t') p++;
            }
            continue;
        }
    }
    fclose(f);
    *search_count_out = srch_n;
    return srv_n;
}

/* Try every nameserver in `servers` for one `name`; first hit wins. */
static int dns_try_all_servers(struct sockaddr_in *servers, int srv_n,
                               const char *name, uint8_t addr_out[4]) {
    for (int i = 0; i < srv_n; i++) {
        int rc = dns_query_v4(&servers[i], name, addr_out);
        if (rc == 1) return 1;
    }
    return 0;
}

static int dns_lookup_via_resolv(const char *name, uint8_t addr_out[4]) {
    struct sockaddr_in servers[8];
    char               searches[8][256];
    int                search_n = 0;
    int srv_n = resolv_parse(servers, 8, searches, 8, &search_n);
    if (srv_n <= 0) return srv_n;  /* -1 = no file, 0 = no nameservers */

    /* RFC 1535 §6: "If the supplied name contains a dot, try as-is
     * first; only fall back to the search list if that fails."  For
     * unqualified names (no dot), the search list is consulted
     * first, then the bare name.  Substrate adopts the more common
     * absolute-first behaviour for safety. */
    int has_dot = (strchr(name, '.') != NULL);

    if (has_dot) {
        if (dns_try_all_servers(servers, srv_n, name, addr_out) == 1) return 1;
        /* Fall through to search-list expansion. */
    }

    /* Search-list expansion: append each search domain, retry. */
    for (int i = 0; i < search_n; i++) {
        char fqdn[512];
        int n = snprintf(fqdn, sizeof(fqdn), "%s.%s", name, searches[i]);
        if (n <= 0 || (size_t)n >= sizeof(fqdn)) continue;
        if (dns_try_all_servers(servers, srv_n, fqdn, addr_out) == 1) return 1;
    }

    /* Last resort for unqualified names: try as-is. */
    if (!has_dot) {
        if (dns_try_all_servers(servers, srv_n, name, addr_out) == 1) return 1;
    }
    return 0;
}

struct hostent *gethostbyname(const char *name) {
    if (!name) { h_errno = HOST_NOT_FOUND; return NULL; }
    /* First check /etc/hosts. */
    sethostent(1);
    struct hostent *he;
    while ((he = gethostent()) != NULL) {
        if (strcmp(he->h_name, name) == 0) return he;
        for (char **a = he->h_aliases; *a; a++) {
            if (strcmp(*a, name) == 0) return he;
        }
    }
    /* Fall back to DNS. */
    uint8_t addr[4];
    if (dns_lookup_via_resolv(name, addr) == 1) {
        memcpy(g_hostent_addr_buf, addr, 4);
        g_hostent_addr_list[0] = g_hostent_addr_buf;
        g_hostent_addr_list[1] = NULL;
        snprintf(g_hostent_line, sizeof(g_hostent_line), "%s", name);
        g_hostent.h_name      = g_hostent_line;
        g_hostent.h_aliases   = (char *[]){ NULL };
        g_hostent.h_addrtype  = AF_INET;
        g_hostent.h_length    = 4;
        g_hostent.h_addr_list = g_hostent_addr_list;
        return &g_hostent;
    }
    h_errno = HOST_NOT_FOUND;
    return NULL;
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type) {
    if (type != AF_INET || len != sizeof(struct in_addr) || !addr) {
        h_errno = NO_RECOVERY; return NULL;
    }
    in_addr_t want = ((const struct in_addr *)addr)->s_addr;
    sethostent(1);
    struct hostent *he;
    while ((he = gethostent()) != NULL) {
        in_addr_t got;
        memcpy(&got, he->h_addr_list[0], sizeof(got));
        if (got == want) return he;
    }
    h_errno = HOST_NOT_FOUND;
    return NULL;
}

/* ---- /etc/services: getservent / getservbyname / getservbyport */

static struct servent g_servent;
static char           g_servent_line[512];
static char          *g_servent_aliases[MAX_ALIASES + 1];

void setservent(int stayopen) {
    (void)stayopen;
    if (g_serv_fp) rewind(g_serv_fp);
    else g_serv_fp = fopen("/etc/services", "r");
}

void endservent(void) {
    if (g_serv_fp) { fclose(g_serv_fp); g_serv_fp = NULL; }
}

struct servent *getservent(void) {
    if (!g_serv_fp) {
        g_serv_fp = fopen("/etc/services", "r");
        if (!g_serv_fp) return NULL;
    }
    char *line;
    while ((line = fgets(g_servent_line, sizeof(g_servent_line), g_serv_fp))) {
        rstrip_comment(line);
        char *tok[MAX_ALIASES + 4];
        int n = tokenize(line, tok, MAX_ALIASES + 4);
        if (n < 2) continue;   /* name + port/proto */

        /* tok[1] = "<port>/<proto>" */
        char *slash = strchr(tok[1], '/');
        if (!slash) continue;
        *slash = '\0';
        int port = atoi(tok[1]);
        char *proto = slash + 1;

        g_servent.s_name  = tok[0];
        g_servent.s_port  = htons(port);
        g_servent.s_proto = proto;

        for (int i = 2, j = 0; i < n && j < MAX_ALIASES; i++, j++) {
            g_servent_aliases[j] = tok[i];
        }
        int na = n - 2 < MAX_ALIASES ? n - 2 : MAX_ALIASES;
        g_servent_aliases[na] = NULL;
        g_servent.s_aliases = g_servent_aliases;
        return &g_servent;
    }
    return NULL;
}

struct servent *getservbyname(const char *name, const char *proto) {
    if (!name) return NULL;
    setservent(1);
    struct servent *s;
    while ((s = getservent()) != NULL) {
        if (proto && strcmp(s->s_proto, proto) != 0) continue;
        if (strcmp(s->s_name, name) == 0) return s;
        for (char **a = s->s_aliases; *a; a++) {
            if (strcmp(*a, name) == 0) return s;
        }
    }
    return NULL;
}

struct servent *getservbyport(int port, const char *proto) {
    setservent(1);
    struct servent *s;
    while ((s = getservent()) != NULL) {
        if (s->s_port != port) continue;
        if (proto && strcmp(s->s_proto, proto) != 0) continue;
        return s;
    }
    return NULL;
}

/* ---- /etc/protocols: getprotoent / getprotobyname / getprotobynumber */

static struct protoent g_protoent;
static char            g_protoent_line[512];
static char           *g_protoent_aliases[MAX_ALIASES + 1];

void setprotoent(int stayopen) {
    (void)stayopen;
    if (g_proto_fp) rewind(g_proto_fp);
    else g_proto_fp = fopen("/etc/protocols", "r");
}

void endprotoent(void) {
    if (g_proto_fp) { fclose(g_proto_fp); g_proto_fp = NULL; }
}

struct protoent *getprotoent(void) {
    if (!g_proto_fp) {
        g_proto_fp = fopen("/etc/protocols", "r");
        if (!g_proto_fp) return NULL;
    }
    char *line;
    while ((line = fgets(g_protoent_line, sizeof(g_protoent_line), g_proto_fp))) {
        rstrip_comment(line);
        char *tok[MAX_ALIASES + 4];
        int n = tokenize(line, tok, MAX_ALIASES + 4);
        if (n < 2) continue;   /* name + number */

        g_protoent.p_name  = tok[0];
        g_protoent.p_proto = atoi(tok[1]);
        for (int i = 2, j = 0; i < n && j < MAX_ALIASES; i++, j++) {
            g_protoent_aliases[j] = tok[i];
        }
        int na = n - 2 < MAX_ALIASES ? n - 2 : MAX_ALIASES;
        g_protoent_aliases[na] = NULL;
        g_protoent.p_aliases = g_protoent_aliases;
        return &g_protoent;
    }
    return NULL;
}

struct protoent *getprotobyname(const char *name) {
    if (!name) return NULL;
    setprotoent(1);
    struct protoent *p;
    while ((p = getprotoent()) != NULL) {
        if (strcmp(p->p_name, name) == 0) return p;
        for (char **a = p->p_aliases; *a; a++) {
            if (strcmp(*a, name) == 0) return p;
        }
    }
    return NULL;
}

struct protoent *getprotobynumber(int proto) {
    setprotoent(1);
    struct protoent *p;
    while ((p = getprotoent()) != NULL) {
        if (p->p_proto == proto) return p;
    }
    return NULL;
}

/* ---- /etc/networks: getnetent / getnetbyname / getnetbyaddr -- */

static struct netent g_netent;
static char          g_netent_line[512];
static char         *g_netent_aliases[MAX_ALIASES + 1];

void setnetent(int stayopen) {
    (void)stayopen;
    if (g_net_fp) rewind(g_net_fp);
    else g_net_fp = fopen("/etc/networks", "r");
}

void endnetent(void) {
    if (g_net_fp) { fclose(g_net_fp); g_net_fp = NULL; }
}

struct netent *getnetent(void) {
    if (!g_net_fp) {
        g_net_fp = fopen("/etc/networks", "r");
        if (!g_net_fp) return NULL;
    }
    char *line;
    while ((line = fgets(g_netent_line, sizeof(g_netent_line), g_net_fp))) {
        rstrip_comment(line);
        char *tok[MAX_ALIASES + 4];
        int n = tokenize(line, tok, MAX_ALIASES + 4);
        if (n < 2) continue;   /* name + network */

        /* Network value: dotted-quad (truncated forms allowed:
         * "192.168" → 0xC0A80000).  Accept what inet_pton accepts
         * after right-padding with zeros. */
        char padded[32];
        snprintf(padded, sizeof(padded), "%s", tok[1]);
        int dots = 0;
        for (char *q = padded; *q; q++) if (*q == '.') dots++;
        while (dots < 3) {
            size_t l = strlen(padded);
            if (l + 2 >= sizeof(padded)) break;
            padded[l++] = '.'; padded[l++] = '0'; padded[l] = '\0';
            dots++;
        }
        struct in_addr a;
        if (inet_pton(AF_INET, padded, &a) != 1) continue;

        g_netent.n_name     = tok[0];
        g_netent.n_addrtype = AF_INET;
        g_netent.n_net      = ntohl(a.s_addr);
        for (int i = 2, j = 0; i < n && j < MAX_ALIASES; i++, j++) {
            g_netent_aliases[j] = tok[i];
        }
        int na = n - 2 < MAX_ALIASES ? n - 2 : MAX_ALIASES;
        g_netent_aliases[na] = NULL;
        g_netent.n_aliases = g_netent_aliases;
        return &g_netent;
    }
    return NULL;
}

struct netent *getnetbyname(const char *name) {
    if (!name) return NULL;
    setnetent(1);
    struct netent *n;
    while ((n = getnetent()) != NULL) {
        if (strcmp(n->n_name, name) == 0) return n;
        for (char **a = n->n_aliases; *a; a++) {
            if (strcmp(*a, name) == 0) return n;
        }
    }
    return NULL;
}

struct netent *getnetbyaddr(uint32_t net, int type) {
    if (type != AF_INET) return NULL;
    setnetent(1);
    struct netent *n;
    while ((n = getnetent()) != NULL) {
        if (n->n_net == net) return n;
    }
    return NULL;
}

/* accept4 and sockatmark live in src/socket.c — they're real
 * wrappers around the SYS_ACCEPT4 syscall (and a no-op return for
 * sockatmark, since AF_UNIX has no OOB data). */

/*
 * getpass — POSIX-obsolete password prompt.  Disable echo on the
 * controlling terminal, prompt, read up to 127 chars or newline,
 * restore echo, return a pointer to a static buffer.  Returns NULL
 * on any I/O or termios error.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

char *getpass(const char *prompt) {
    static char buf[128];
    int tty = open("/dev/tty", O_RDWR);
    int fd_in  = tty < 0 ? 0 : tty;
    int fd_out = tty < 0 ? 2 : tty;

    struct termios saved, t;
    int have_tcio = (tcgetattr(fd_in, &saved) == 0);
    if (have_tcio) {
        t = saved;
        t.c_lflag &= ~(unsigned)(ECHO | ECHOE | ECHOK | ECHONL);
        tcsetattr(fd_in, TCSANOW, &t);
    }

    if (prompt) (void)write(fd_out, prompt, strlen(prompt));

    int n = 0;
    while (n + 1 < (int)sizeof(buf)) {
        char c;
        int r = read(fd_in, &c, 1);
        if (r <= 0) { buf[0] = '\0'; break; }
        if (c == '\n' || c == '\r') break;
        buf[n++] = c;
    }
    buf[n] = '\0';

    if (have_tcio) tcsetattr(fd_in, TCSANOW, &saved);
    (void)write(fd_out, "\n", 1);
    if (tty >= 0) close(tty);
    return buf;
}
