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
 *   3. sys/socket.h missing entries — accept4 + sockatmark
 *      stubs (substrate has no in-kernel sockets layer yet;
 *      socket(), bind(), etc. are ENOSYS stubs in socket_stubs.c
 *      already).
 *
 * Memory model for the resolver entries: each get*ent / get*by*
 * call returns a pointer to a STATIC struct + STATIC char buffers
 * — the next call overwrites both.  This matches glibc's
 * non-_r behaviour.  Reentrancy needs the _r variants (not yet
 * implemented).
 */

#include <arpa/inet.h>
#include <errno.h>
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

struct hostent *gethostbyname(const char *name) {
    if (!name) { h_errno = HOST_NOT_FOUND; return NULL; }
    sethostent(1);
    struct hostent *he;
    while ((he = gethostent()) != NULL) {
        if (strcmp(he->h_name, name) == 0) return he;
        for (char **a = he->h_aliases; *a; a++) {
            if (strcmp(*a, name) == 0) return he;
        }
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

/* ============================================================
 * sys/socket.h missing entries.  No kernel socket layer yet —
 * stubs match the pattern in socket_stubs.c.
 * ============================================================ */

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
    (void)sockfd; (void)addr; (void)addrlen; (void)flags;
    errno = ENOSYS; return -1;
}

int sockatmark(int sockfd) {
    (void)sockfd;
    errno = ENOSYS; return -1;
}
