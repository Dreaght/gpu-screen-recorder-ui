#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <unistd.h>

static int x11_error_handler(Display *dpy, XErrorEvent *ev) {
    return 0;
}

static void usage() {
    fprintf(stderr, "usage: window-overlay <window>\n");
    exit(1);
}

static bool string_to_i64(const char *str, int64_t *result) {
    errno = 0;
    char *endptr = nullptr;
    int64_t res = strtoll(str, &endptr, 0);
    if(endptr == str || errno != 0)
        return false;

    *result = res;
    return true;
}

int main(int argc, char **argv) {
    if(argc != 2)
        usage();

    int64_t target_window;
    if(!string_to_i64(argv[1], &target_window)) {
        fprintf(stderr, "Error: invalid number '%s' was specific for window argument\n", argv[1]);
        return 1;
    }

    XSetErrorHandler(x11_error_handler);

    Display *display = XOpenDisplay(NULL);
    if(!display) {
        fprintf(stderr, "Error: XOpenDisplay failed\n");
        return 1;
    }

    //const Window root_window = DefaultRootWindow(display);
    const int screen = DefaultScreen(display);

    XSetWindowAttributes attr;
    attr.background_pixel = XWhitePixel(display, screen);
    attr.override_redirect = True;

    Window overlay_window = XCreateWindow(display, target_window, 0, 0, 256, 256, 0, DefaultDepth(display, screen), InputOutput, XDefaultVisual(display, screen), CWBackPixel|CWOverrideRedirect, &attr);
    if(!overlay_window) {
        fprintf(stderr, "Error: failed to create overlay window\n");
        return 1;
    }

    Cursor default_cursor = XCreateFontCursor(display, XC_arrow);

    XMapWindow(display, overlay_window);
    XGrabPointer(display, overlay_window, True, ButtonPressMask|ButtonReleaseMask, GrabModeSync, GrabModeSync, None, default_cursor, CurrentTime);
    sleep(3);

    /*XEvent xev;
    for(;;) {
        XNextEvent(display, &xev);
    }*/

    return 0;
}
