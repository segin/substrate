/*
 * sgreet — substrate graphical login greeter.
 *
 * Runs as root on an already-started Xfbdev (launched with -ac, so no
 * Xauthority is needed).  Draws a small login form, authenticates the
 * user against /etc/passwd + /etc/shadow via crypt(3) using the exact
 * same policy as bin/login, then drops privileges and execs the user's
 * X session (matwm2 + xterm by default).
 *
 *   build:  i386-unknown-substrate-gcc sgreet.c -o sgreet \
 *               -I<libX11>/include -I<xorgproto>/include \
 *               -L<libX11>/lib -L<libxcb>/lib -L<libXau>/lib \
 *               -lX11 -lxcb -lXau -lcrypt
 *   run:    DISPLAY=:0 sgreet            (as root, under sdm)
 *
 * On a successful login it fork()s: the child becomes the user and
 * execs the session; the parent waits for the session to end and then
 * exits, so the supervising sdm loop can restart a fresh greeter.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <crypt.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/reboot.h>

#define MAXFIELD 128
#define SESSION_SCRIPT "/etc/X11/Xsession"

/* Hostname for the "<host> login:" prompt, filled by load_hostname(). */
static char g_host[64] = "substrate";

/* Prefer gethostname(2); but if it returns the unconfigured kernel
 * default (e.g. the display manager started before rc.d ran the
 * hostname script), fall back to /etc/hostname so the prompt still
 * shows the real system name. */
static void load_hostname(void) {
    if (gethostname(g_host, sizeof g_host) == 0) {
        g_host[sizeof g_host - 1] = '\0';
        if (g_host[0] && strcmp(g_host, "localhost") != 0 &&
            strcmp(g_host, "(none)") != 0)
            return;
    }
    FILE *f = fopen("/etc/hostname", "r");
    if (f) {
        if (fgets(g_host, sizeof g_host, f)) {
            char *nl = strpbrk(g_host, "\r\n");
            if (nl) *nl = '\0';
        }
        fclose(f);
    }
    if (!g_host[0]) strncpy(g_host, "substrate", sizeof g_host - 1);
    g_host[sizeof g_host - 1] = '\0';
}

/* ---- authentication (mirrors bin/login/login.c) ------------------- */

/* Stored shadow hash for `user`, or NULL if no entry.  Points into a
 * static buffer reused on each call. */
static const char *lookup_shadow(const char *user) {
    static char line[512];
    FILE *f = fopen("/etc/shadow", "r");
    size_t ulen;
    if (!f) return NULL;
    ulen = strlen(user);
    while (fgets(line, sizeof line, f)) {
        char *colon;
        if (strncmp(line, user, ulen) != 0 || line[ulen] != ':') continue;
        colon = strchr(line + ulen + 1, ':');
        if (colon) *colon = '\0';
        else { char *nl = strchr(line, '\n'); if (nl) *nl = '\0'; }
        fclose(f);
        return line + ulen + 1;
    }
    fclose(f);
    return NULL;
}

static int shadow_matches(const char *stored, const char *attempt) {
    char *hashed;
    if (!stored) return 0;
    if (stored[0] == '\0') return 1;                 /* no password */
    if (stored[0] == '*' || stored[0] == '!') return 0; /* locked */
    hashed = crypt(attempt, stored);
    if (!hashed) return 0;
    return strcmp(hashed, stored) == 0;
}

/* Returns the passwd entry on success, NULL on failure. */
static struct passwd *authenticate(const char *user, const char *pass) {
    struct passwd *pw = getpwnam(user);
    const char *stored;
    if (!pw) return NULL;
    stored = lookup_shadow(user);
    if (!stored && pw->pw_passwd && pw->pw_passwd[0] != 'x')
        stored = pw->pw_passwd;
    return shadow_matches(stored, pass) ? pw : NULL;
}

/* ---- session launch ----------------------------------------------- */

static void exec_session(const struct passwd *pw, const char *display) {
    /* setgid + initgroups BEFORE setuid, or we lose the privilege to
     * install the supplementary group list. */
    if (setgid(pw->pw_gid) != 0) _exit(1);
    initgroups(pw->pw_name, pw->pw_gid);
    if (setuid(pw->pw_uid) != 0) _exit(1);

    setenv("HOME", pw->pw_dir, 1);
    setenv("SHELL", pw->pw_shell[0] ? pw->pw_shell : "/bin/sh", 1);
    setenv("USER", pw->pw_name, 1);
    setenv("LOGNAME", pw->pw_name, 1);
    setenv("PATH",
           pw->pw_uid == 0
               ? "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
               : "/usr/local/bin:/usr/bin:/bin",
           1);
    setenv("DISPLAY", display, 1);
    setenv("TERM", "xterm", 1);
    /* Default the session to the system UTF-8 locale so X clients
     * (xterm/luit) start in UTF-8 mode.  The login shell would set
     * this from /etc/profile, but X clients are spawned before any
     * shell runs, so seed it here too. */
    setenv("LANG", "en_US.UTF-8", 1);

    if (chdir(pw->pw_dir) != 0) (void)chdir("/");
    setsid();

    execl("/bin/sh", "sh", SESSION_SCRIPT, (char *)NULL);
    /* Fallback if the session script is missing. */
    execl("/usr/bin/xterm", "xterm", (char *)NULL);
    _exit(127);
}

/* ---- greeter UI --------------------------------------------------- */

typedef struct {
    Display *dpy;
    Window   win;
    GC       gc;
    XFontStruct *font;
    int      w, h;
    int      line_h;
} ui_t;

/* Bottom-row action buttons.  Graceful shutdown is a signal to init
 * (PID 1): SIGUSR2 = reboot, SIGUSR1 = power off -- the same path
 * /sbin/reboot and /sbin/poweroff take.  If init can't be signalled we
 * fall back to the reboot(2) syscall directly (the greeter runs as root). */
#define NBUTTONS 2
static const char *const btn_label[NBUTTONS]   = { "Reboot", "Power off" };
static const int         btn_initsig[NBUTTONS] = { SIGUSR2, SIGUSR1 };
static const int         btn_rbcmd[NBUTTONS]   = { RB_AUTOBOOT, RB_POWER_OFF };

/* Pixel rectangle of button `i` within the greeter window. */
static void button_geom(const ui_t *u, int i, int *bx, int *by, int *bw, int *bh) {
    const int pad = 16;
    *bw = (u->w - pad * (NBUTTONS + 1)) / NBUTTONS;
    *bh = u->line_h + 10;
    *by = u->h - *bh - pad;
    *bx = pad + i * (*bw + pad);
}

static void do_power_action(int i) {
    if (kill(1, btn_initsig[i]) == 0)
        return;                  /* init runs the graceful shutdown_sequence */
    reboot(btn_rbcmd[i]);        /* fallback: terminate via the kernel */
}

static void draw(ui_t *u, const char *user, int passlen, int field,
                 const char *msg) {
    int x = 24;
    int y = 16 + u->font->ascent;
    char buf[MAXFIELD + 16];
    int i;

    static const char *title = "Welcome to Substrate!";
    XClearWindow(u->dpy, u->win);
    XDrawString(u->dpy, u->win, u->gc, x, y, title, (int)strlen(title));
    y += u->line_h * 2;

    snprintf(buf, sizeof buf, "%s login: %s%s", g_host, user,
             field == 0 ? "_" : "");
    XDrawString(u->dpy, u->win, u->gc, x, y, buf, (int)strlen(buf));
    y += u->line_h;

    /* password shown as asterisks */
    strcpy(buf, "Password: ");
    for (i = 0; i < passlen && i < MAXFIELD; i++) strcat(buf, "*");
    if (field == 1) strcat(buf, "_");
    XDrawString(u->dpy, u->win, u->gc, x, y, buf, (int)strlen(buf));
    y += u->line_h * 2;

    if (msg && msg[0])
        XDrawString(u->dpy, u->win, u->gc, x, y, msg, (int)strlen(msg));

    /* Bottom-row action buttons: outlined boxes with centered labels. */
    for (i = 0; i < NBUTTONS; i++) {
        int bx, by, bw, bh, tw, tx, ty;
        button_geom(u, i, &bx, &by, &bw, &bh);
        XDrawRectangle(u->dpy, u->win, u->gc, bx, by, bw, bh);
        tw = XTextWidth(u->font, btn_label[i], (int)strlen(btn_label[i]));
        tx = bx + (bw - tw) / 2;
        ty = by + (bh + u->font->ascent - u->font->descent) / 2;
        XDrawString(u->dpy, u->win, u->gc, tx, ty,
                    btn_label[i], (int)strlen(btn_label[i]));
    }

    XFlush(u->dpy);
}

int main(void) {
    const char *display = getenv("DISPLAY");
    ui_t u;
    int screen, tries;

    if (!display || !display[0]) display = ":0";

    load_hostname();

    /* The X server may still be coming up — retry the connection. */
    u.dpy = NULL;
    for (tries = 0; tries < 50 && !u.dpy; tries++) {
        u.dpy = XOpenDisplay(display);
        if (!u.dpy) usleep(200000);
    }
    if (!u.dpy) {
        fprintf(stderr, "sgreet: cannot open display %s\n", display);
        return 1;
    }

    screen = DefaultScreen(u.dpy);
    u.font = XLoadQueryFont(u.dpy, "9x15");
    if (!u.font) u.font = XLoadQueryFont(u.dpy, "fixed");
    if (!u.font) {
        fprintf(stderr, "sgreet: no usable font\n");
        return 1;
    }
    u.line_h = u.font->ascent + u.font->descent + 4;

    u.w = 360;
    u.h = 215;   /* extra room for the bottom-row action buttons */
    int sw = DisplayWidth(u.dpy, screen);
    int sh = DisplayHeight(u.dpy, screen);
    int px = (sw - u.w) / 2, py = (sh - u.h) / 2;
    if (px < 0) px = 0;
    if (py < 0) py = 0;

    unsigned long black = BlackPixel(u.dpy, screen);
    unsigned long white = WhitePixel(u.dpy, screen);

    XSetWindowAttributes wa;
    wa.background_pixel = black;
    wa.border_pixel = white;
    wa.override_redirect = True;   /* no WM is running during the greeter */
    wa.event_mask = KeyPressMask | ButtonPressMask | ExposureMask;
    u.win = XCreateWindow(u.dpy, RootWindow(u.dpy, screen), px, py, u.w, u.h,
                          2, CopyFromParent, InputOutput, CopyFromParent,
                          CWBackPixel | CWBorderPixel | CWOverrideRedirect |
                          CWEventMask, &wa);

    u.gc = XCreateGC(u.dpy, u.win, 0, NULL);
    XSetForeground(u.dpy, u.gc, white);
    XSetBackground(u.dpy, u.gc, black);
    XSetFont(u.dpy, u.gc, u.font->fid);

    XMapRaised(u.dpy, u.win);
    /* Grab the keyboard so every keystroke reaches the greeter even
     * with no window manager to assign focus. */
    for (tries = 0; tries < 50; tries++) {
        if (XGrabKeyboard(u.dpy, u.win, True, GrabModeAsync, GrabModeAsync,
                          CurrentTime) == GrabSuccess)
            break;
        usleep(100000);
    }
    XSetInputFocus(u.dpy, u.win, RevertToPointerRoot, CurrentTime);

    char user[MAXFIELD] = "";
    char pass[MAXFIELD] = "";
    int  ulen = 0, plen = 0;
    int  field = 0;          /* 0 = username, 1 = password */
    const char *msg = "";

    for (;;) {
        XEvent ev;
        XNextEvent(u.dpy, &ev);

        if (ev.type == Expose) {
            draw(&u, user, plen, field, msg);
            continue;
        }
        if (ev.type == ButtonPress) {
            int i;
            for (i = 0; i < NBUTTONS; i++) {
                int bx, by, bw, bh;
                button_geom(&u, i, &bx, &by, &bw, &bh);
                if (ev.xbutton.x >= bx && ev.xbutton.x < bx + bw &&
                    ev.xbutton.y >= by && ev.xbutton.y < by + bh) {
                    /* Drop the keyboard grab and let init bring the
                     * system down; do_power_action does not return on
                     * success.  Repaint with a notice if it somehow does. */
                    XUngrabKeyboard(u.dpy, CurrentTime);
                    XFlush(u.dpy);
                    do_power_action(i);
                    msg = "Shutting down...";
                    draw(&u, user, plen, field, msg);
                    break;
                }
            }
            continue;
        }
        if (ev.type != KeyPress) continue;

        char kbuf[16];
        KeySym ks;
        int n = XLookupString(&ev.xkey, kbuf, sizeof kbuf, &ks, NULL);

        if (ks == XK_Return || ks == XK_KP_Enter) {
            if (field == 0) {
                if (ulen > 0) { field = 1; msg = ""; }
            } else {
                struct passwd *pw = authenticate(user, pass);
                if (pw) {
                    /* Tear down the greeter, then run the session. */
                    XUngrabKeyboard(u.dpy, CurrentTime);
                    XDestroyWindow(u.dpy, u.win);
                    XCloseDisplay(u.dpy);

                    pid_t kid = fork();
                    if (kid == 0) {
                        exec_session(pw, display);
                        _exit(127);
                    }
                    if (kid > 0) {
                        int st;
                        while (waitpid(kid, &st, 0) < 0) /* retry */;
                    }
                    return 0;   /* sdm restarts a fresh greeter */
                }
                /* failed — reset */
                memset(pass, 0, sizeof pass);
                memset(user, 0, sizeof user);
                ulen = plen = 0;
                field = 0;
                msg = "Login incorrect";
            }
        } else if (ks == XK_BackSpace || ks == XK_Delete) {
            if (field == 0) { if (ulen) user[--ulen] = '\0'; }
            else            { if (plen) pass[--plen] = '\0'; }
        } else if (ks == XK_Tab) {
            if (field == 0 && ulen > 0) field = 1;
        } else if (ks == XK_Escape) {
            memset(user, 0, sizeof user); memset(pass, 0, sizeof pass);
            ulen = plen = 0; field = 0; msg = "";
        } else if (n == 1 && (unsigned char)kbuf[0] >= 32 &&
                   (unsigned char)kbuf[0] < 127) {
            if (field == 0) {
                if (ulen < MAXFIELD - 1) { user[ulen++] = kbuf[0]; user[ulen] = '\0'; }
            } else {
                if (plen < MAXFIELD - 1) { pass[plen++] = kbuf[0]; pass[plen] = '\0'; }
            }
        }
        draw(&u, user, plen, field, msg);
    }
}
