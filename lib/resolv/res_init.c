/*
 * res_init.c — parse /etc/resolv.conf into _res.
 *
 * Supported directives:
 *   nameserver <ip>      — repeatable up to MAXNS
 *   domain <name>        — sets defdname
 *   search <list>        — space-separated, fills dnsrch
 *   options ndots:<n>    — only ndots is honoured
 *   options timeout:<n>  — overrides RES_TIMEOUT
 *   options attempts:<n> — overrides RES_DFLRETRY
 *
 * Anything else is silently ignored.  IPv4 only; IPv6 nameservers
 * require a sockaddr_in6 list which the BIND-historical struct
 * __res_state doesn't carry.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static struct __res_state g_res;

struct __res_state *__res_state(void) {
    return &g_res;
}

static void trim_inplace(char *s) {
    if (!s) return;
    char *p = s + strlen(s);
    while (p > s && (p[-1] == '\n' || p[-1] == '\r' || p[-1] == ' ' || p[-1] == '\t'))
        *--p = '\0';
}

static char *next_token(char **state) {
    if (!state || !*state) return NULL;
    char *s = *state;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0' || *s == '#') { *state = s; return NULL; }
    char *start = s;
    while (*s && *s != ' ' && *s != '\t' && *s != '#') s++;
    if (*s) { *s++ = '\0'; }
    *state = s;
    return start;
}

int res_init(void) {
    struct __res_state *r = &g_res;
    if (r->options & RES_INIT) return 0;

    memset(r, 0, sizeof(*r));
    r->retrans = RES_TIMEOUT;
    r->retry   = RES_DFLRETRY;
    r->options = RES_INIT | RES_RECURSE | RES_DEFNAMES | RES_DNSRCH;
    r->ndots   = 1;
    r->nscount = 0;

    FILE *f = fopen("/etc/resolv.conf", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            trim_inplace(line);
            char *cur = line;
            char *kw = next_token(&cur);
            if (!kw) continue;

            if (strcmp(kw, "nameserver") == 0) {
                char *ip = next_token(&cur);
                if (!ip || r->nscount >= MAXNS) continue;
                struct sockaddr_in *sin = &r->nsaddr_list[r->nscount];
                memset(sin, 0, sizeof(*sin));
                sin->sin_family = AF_INET;
                sin->sin_port = htons(NS_DEFAULTPORT);
                if (inet_pton(AF_INET, ip, &sin->sin_addr) == 1) r->nscount++;
            } else if (strcmp(kw, "domain") == 0) {
                char *dom = next_token(&cur);
                if (dom) {
                    strlcpy(r->defdname, dom, sizeof(r->defdname));
                }
            } else if (strcmp(kw, "search") == 0) {
                int n = 0;
                char *d;
                while (n < MAXDNSRCH && (d = next_token(&cur)) != NULL) {
                    r->dnsrch[n++] = strdup(d);
                }
                r->dnsrch[n] = NULL;
            } else if (strcmp(kw, "options") == 0) {
                char *opt;
                while ((opt = next_token(&cur)) != NULL) {
                    if (strncmp(opt, "ndots:", 6) == 0) {
                        r->ndots = (unsigned)atoi(opt + 6);
                    } else if (strncmp(opt, "timeout:", 8) == 0) {
                        r->retrans = atoi(opt + 8);
                    } else if (strncmp(opt, "attempts:", 9) == 0) {
                        r->retry = atoi(opt + 9);
                    }
                }
            }
        }
        fclose(f);
    }

    /* If /etc/resolv.conf had no nameserver entries, fall back to
     * localhost so callers fail with ECONNREFUSED (clearer than
     * "no resolver") instead of an obscure error. */
    if (r->nscount == 0) {
        struct sockaddr_in *sin = &r->nsaddr_list[0];
        memset(sin, 0, sizeof(*sin));
        sin->sin_family = AF_INET;
        sin->sin_port = htons(NS_DEFAULTPORT);
        sin->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        r->nscount = 1;
    }

    /* PID-mixed initial query ID. */
    r->id = (unsigned short)(0xdead);

    return 0;
}
