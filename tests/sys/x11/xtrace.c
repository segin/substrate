/*
 * xtrace — minimal Xlib client that opens a window and prints a
 * blow-by-blow trace of every X round-trip it makes.  Drop into the
 * substrate rootfs alongside Xfbdev; with `init=/etc/startx` running
 * an X server, log in on another tty / telnet in, then:
 *
 *     DISPLAY=:0 /tests/xtrace 2>/var/log/xtrace.log
 *
 * If the X server crashes during one of these calls, the LAST line
 * in the log is the call that did it.
 *
 * Built for substrate via the cross toolchain — see Makefile.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define T(fmt, ...) do { \
    fprintf(stderr, "[xtrace] " fmt "\n", ##__VA_ARGS__); \
    fflush(stderr); \
} while (0)

static int xtrace_error(Display *dpy, XErrorEvent *err) {
    char buf[128];
    XGetErrorText(dpy, err->error_code, buf, sizeof(buf));
    T("X ERROR: code=%d (%s) req=%d.%d resourceid=0x%lx serial=%lu",
      err->error_code, buf, err->request_code, err->minor_code,
      (unsigned long)err->resourceid, (unsigned long)err->serial);
    return 0;
}

static int xtrace_io_error(Display *dpy) {
    T("X IO ERROR: display closed (server died?)");
    (void)dpy;
    fflush(stderr);
    _exit(2);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    T("xtrace start: pid=%d", (int)getpid());

    XSetErrorHandler(xtrace_error);
    XSetIOErrorHandler(xtrace_io_error);

    const char *dname = getenv("DISPLAY");
    T("DISPLAY=%s", dname ? dname : "(unset)");

    T("calling XOpenDisplay(...)");
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        T("XOpenDisplay returned NULL — server unreachable or rejected");
        return 1;
    }
    T("XOpenDisplay OK: dpy=%p", (void *)dpy);

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    int w = DisplayWidth(dpy, screen);
    int h = DisplayHeight(dpy, screen);
    int depth = DefaultDepth(dpy, screen);
    T("DefaultScreen=%d Root=0x%lx Size=%dx%d depth=%d",
      screen, (unsigned long)root, w, h, depth);

    unsigned long black = BlackPixel(dpy, screen);
    unsigned long white = WhitePixel(dpy, screen);
    T("BlackPixel=0x%lx WhitePixel=0x%lx", black, white);

    T("calling XCreateSimpleWindow(...)");
    Window win = XCreateSimpleWindow(dpy, root,
                                     100, 100,    /* x, y */
                                     320, 200,    /* w, h */
                                     2,           /* border */
                                     black, white);
    T("XCreateSimpleWindow returned win=0x%lx", (unsigned long)win);

    T("calling XStoreName(...)");
    XStoreName(dpy, win, "xtrace");
    T("XStoreName returned");

    T("calling XSelectInput(ExposureMask | KeyPressMask | StructureNotifyMask)");
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask);
    T("XSelectInput returned");

    T("calling XMapWindow(...)");
    XMapWindow(dpy, win);
    T("XMapWindow returned");

    T("calling XFlush(...)");
    XFlush(dpy);
    T("XFlush returned");

    T("entering event loop — press a key in the window to exit");
    XEvent ev;
    int events = 0;
    while (1) {
        T("calling XNextEvent (event #%d)", events);
        XNextEvent(dpy, &ev);
        events++;
        switch (ev.type) {
        case Expose:
            T("Expose: x=%d y=%d w=%d h=%d count=%d",
              ev.xexpose.x, ev.xexpose.y, ev.xexpose.width,
              ev.xexpose.height, ev.xexpose.count);
            {
                GC gc = XCreateGC(dpy, win, 0, NULL);
                XSetForeground(dpy, gc, black);
                XDrawString(dpy, win, gc, 20, 30, "xtrace", 6);
                XFreeGC(dpy, gc);
                XFlush(dpy);
            }
            break;
        case ConfigureNotify:
            T("ConfigureNotify: x=%d y=%d w=%d h=%d",
              ev.xconfigure.x, ev.xconfigure.y,
              ev.xconfigure.width, ev.xconfigure.height);
            break;
        case MapNotify:
            T("MapNotify");
            break;
        case ReparentNotify:
            T("ReparentNotify: parent=0x%lx",
              (unsigned long)ev.xreparent.parent);
            break;
        case KeyPress:
            T("KeyPress keycode=%u state=0x%x — exiting",
              ev.xkey.keycode, ev.xkey.state);
            XDestroyWindow(dpy, win);
            XCloseDisplay(dpy);
            T("clean exit");
            return 0;
        default:
            T("event type=%d (unhandled)", ev.type);
            break;
        }
    }
}
