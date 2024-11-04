#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    CAPTURE_TYPE_FOCUSED,
    CAPTURE_TYPE_CURSOR
} capture_type;

static bool window_has_atom(Display *dpy, Window window, Atom atom) {
    Atom type;
    unsigned long len, bytes_left;
    int format;
    unsigned char *properties = NULL;
    if(XGetWindowProperty(dpy, window, atom, 0, 1024, False, AnyPropertyType, &type, &format, &len, &bytes_left, &properties) < Success)
        return false;

    if(properties)
        XFree(properties);

    return type != None;
}

static bool window_is_user_program(Display *dpy, Window window) {
    const Atom net_wm_state_atom = XInternAtom(dpy, "_NET_WM_STATE", False);
    const Atom wm_state_atom = XInternAtom(dpy, "WM_STATE", False);
    return window_has_atom(dpy, window, net_wm_state_atom) || window_has_atom(dpy, window, wm_state_atom);
}

static Window get_window_at_cursor_position(Display *dpy) {
    Window root_window = None;
    Window window = None;
    int dummy_i;
    unsigned int dummy_u;
    int cursor_pos_x = 0;
    int cursor_pos_y = 0;
    XQueryPointer(dpy, DefaultRootWindow(dpy), &root_window, &window, &dummy_i, &dummy_i, &cursor_pos_x, &cursor_pos_y, &dummy_u);
    return window;
}

static Window get_focused_window(Display *dpy, capture_type cap_type) {
    const Atom net_active_window_atom = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    Window focused_window = None;

    if(cap_type == CAPTURE_TYPE_FOCUSED) {
        // Atom type = None;
        // int format = 0;
        // unsigned long num_items = 0;
        // unsigned long bytes_left = 0;
        // unsigned char *data = NULL;
        // XGetWindowProperty(dpy, DefaultRootWindow(dpy), net_active_window_atom, 0, 1, False, XA_WINDOW, &type, &format, &num_items, &bytes_left, &data);

        // fprintf(stderr, "focused window: %p\n", (void*)data);

        // if(type == XA_WINDOW && num_items == 1 && data)
        //     return *(Window*)data;

        int revert_to = 0;
        XGetInputFocus(dpy, &focused_window, &revert_to);
        if(focused_window && focused_window != DefaultRootWindow(dpy) && window_is_user_program(dpy, focused_window))
            return focused_window;
    }

    focused_window = get_window_at_cursor_position(dpy);
    if(focused_window && focused_window != DefaultRootWindow(dpy) && window_is_user_program(dpy, focused_window))
        return focused_window;

    return None;
}

static char* get_window_title(Display *dpy, Window window) {
    const Atom net_wm_name_atom = XInternAtom(dpy, "_NET_WM_NAME", False);
    const Atom wm_name_atom = XInternAtom(dpy, "_NET_WM_NAME", False);
    const Atom utf8_string_atom = XInternAtom(dpy, "UTF8_STRING", False);

    Atom type = None;
    int format = 0;
    unsigned long num_items = 0;
    unsigned long bytes_left = 0;
    unsigned char *data = NULL;
    XGetWindowProperty(dpy, window, net_wm_name_atom, 0, 1024, False, utf8_string_atom, &type, &format, &num_items, &bytes_left, &data);

    if(type == utf8_string_atom && format == 8 && data)
        return (char*)data;

    type = None;
    format = 0;
    num_items = 0;
    bytes_left = 0;
    data = NULL;
    XGetWindowProperty(dpy, window, wm_name_atom, 0, 1024, False, 0, &type, &format, &num_items, &bytes_left, &data);

    if((type == XA_STRING || type == utf8_string_atom) && data)
        return (char*)data;

    return NULL;
}

static const char* strip(const char *str, int *len) {
    int str_len = strlen(str);
    for(int i = 0; i < str_len; ++i) {
        if(str[i] != ' ') {
            str += i;
            str_len -= i;
            break;
        }
    }

    for(int i = str_len - 1; i >= 0; --i) {
        if(str[i] != ' ') {
            str_len = i + 1;
            break;
        }
    }

    *len = str_len;
    return str;
}

static void print_str_strip(const char *str) {
    int len = 0;
    str = strip(str, &len);
    printf("%.*s", len, str);
}

static int x11_ignore_error(Display *dpy, XErrorEvent *error_event) {
    (void)dpy;
    (void)error_event;
    return 0;
}

static void usage(void) {
    fprintf(stderr, "usage: gsr-window-name <focused|cursor>\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  focused    The class/name of the focused window is returned. If no window is focused then the window beneath the cursor is returned instead\n");
    fprintf(stderr, "  cursor     The class/name of the window beneath the cursor is returned\n");
    exit(1);
}

int main(int argc, char **argv) {
    if(argc != 2)
        usage();

    const char *cap_type_str = argv[1];
    capture_type cap_type = CAPTURE_TYPE_FOCUSED;
    if(strcmp(cap_type_str, "focused") == 0) {
        cap_type = CAPTURE_TYPE_FOCUSED;
    } else if(strcmp(cap_type_str, "cursor") == 0) {
        cap_type = CAPTURE_TYPE_CURSOR;
    } else {
        fprintf(stderr, "error: invalid option '%s', expected either 'focused' or 'cursor'\n", cap_type_str);
        usage();
    }

    Display *dpy = XOpenDisplay(NULL);
    if(!dpy) {
        fprintf(stderr, "Error: failed to connect to the X11 server\n");
        exit(1);
    }

    XSetErrorHandler(x11_ignore_error);

    const Window focused_window = get_focused_window(dpy, cap_type);
    if(focused_window == None)
        exit(2);

    // Window title is not always ideal (for example for a browser), but for games its pretty much required
    char *window_title = get_window_title(dpy, focused_window);
    if(window_title) {
        print_str_strip(window_title);
        exit(0);
    }

    XClassHint class_hint = {0};
    XGetClassHint(dpy, focused_window, &class_hint);
    if(class_hint.res_class) {
        print_str_strip(class_hint.res_class);
        exit(0);
    }

    return 2;
}
