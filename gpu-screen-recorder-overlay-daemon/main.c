#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

bool exec_program(const char **args, pid_t *process_id) {
    *process_id = -1;

    /* 1 argument */
    if(args[0] == NULL)
        return false;

    pid_t pid = vfork();
    if(pid == -1) {
        perror("Failed to vfork");
        return false;
    } else if(pid == 0) { /* child */
        execvp(args[0], (char* const*)args);
        perror("execvp");
        _exit(127);
    } else { /* parent */
        *process_id = pid;
    }

    return true;
}

static int ignore_xerror(Display *display, XErrorEvent *ee) {
    (void)display;
    (void)ee;
    return 0;
}

static void sigterm_handler(int dummy) {
    (void)dummy;
}

static const KeySym toggle_overlay_key = XK_Z;
static void grab_keys(Display *display) {
    unsigned int numlockmask = 0; 
    KeyCode numlock_keycode = XKeysymToKeycode(display, XK_Num_Lock);
    XModifierKeymap *modmap = XGetModifierMapping(display);
    for(int i = 0; i < 8; ++i) {
        for(int j = 0; j < modmap->max_keypermod; ++j) {
            if(modmap->modifiermap[i * modmap->max_keypermod + j] == numlock_keycode)
                numlockmask = (1 << i); 
        }
    }   
    XFreeModifiermap(modmap);

    XErrorHandler prev_error_handler = XSetErrorHandler(ignore_xerror);
    
    Window root_window = DefaultRootWindow(display);
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    for(int i = 0; i < 4; ++i) {
        XGrabKey(display, XKeysymToKeycode(display, toggle_overlay_key), Mod1Mask|modifiers[i], root_window, False, GrabModeAsync, GrabModeAsync);
    }    
    
    XSync(display, False);
    XSetErrorHandler(prev_error_handler);
}

static bool window_has_atom(Display *display, Window window, Atom atom) {
    Atom type;
    unsigned long len, bytes_left;
    int format;
    unsigned char *properties = NULL;
    if(XGetWindowProperty(display, window, atom, 0, 1024, False, AnyPropertyType, &type, &format, &len, &bytes_left, &properties) < Success)
        return false;

    if(properties)
        XFree(properties);

    return type != None;
}

static Window window_get_target_window_child(Display *display, Window window) {
    if(window == None)
        return None;

    Atom wm_state_atom = XInternAtom(display, "_NET_WM_STATE", False);
    if(!wm_state_atom)
        return None;

    if(window_has_atom(display, window, wm_state_atom))
        return window;

    Window root;
    Window parent;
    Window *children = NULL;
    unsigned int num_children = 0;
    if(!XQueryTree(display, window, &root, &parent, &children, &num_children) || !children)
        return None;

    Window found_window = None;
    for(int i = num_children - 1; i >= 0; --i) {
        if(children[i] && window_has_atom(display, children[i], wm_state_atom)) {
            found_window = children[i];
            goto finished;
        }
    }

    for(int i = num_children - 1; i >= 0; --i) {
        if(children[i]) {
            Window win = window_get_target_window_child(display, children[i]);
            if(win) {
                found_window = win;
                goto finished;
            }
        }
    }

    finished:
    XFree(children);
    return found_window;
}

static Window window_get_target_window_parent(Display *display, Window window) {
    fprintf(stderr, "window: %ld\n", window);

    if(window == None || window == DefaultRootWindow(display))
        return None;

    Atom wm_state_atom = XInternAtom(display, "WM_STATE", False);
    if(!wm_state_atom)
        return None;

    if(window_has_atom(display, window, wm_state_atom))
        return window;

    Window root;
    Window parent = None;
    Window *children = NULL;
    unsigned int num_children = 0;
    if(!XQueryTree(display, window, &root, &parent, &children, &num_children))
        return None;

    if(children)
        XFree(children);

    if(!parent)
        return None;

    return window_get_target_window_parent(display, parent);
}

static Window get_input_focus(Display *display) {
    Window focused_window = None;

    Atom net_active_window_atom = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    Atom type;
    unsigned long len, bytes_left;
    int format;
    unsigned char *properties = NULL;
    if(XGetWindowProperty(display, DefaultRootWindow(display), net_active_window_atom, 0, 1024, False, XA_WINDOW, &type, &format, &len, &bytes_left, &properties) == Success) {
        if(properties) {
            if(len > 0)
                focused_window = *(Window*)properties;
            XFree(properties);
        }
    }

    if(!focused_window) {
        int rev;
        if(!XGetInputFocus(display, &focused_window, &rev))
            focused_window = None;
    }

    return focused_window;
}

static Window get_window_with_input_focus(Display *display) {
    Window window = get_input_focus(display);
    return window_get_target_window_parent(display, window);
}

int main() {
    Display *display = XOpenDisplay(NULL);
    if(!display) {
        fprintf(stderr, "Error: XOpenDisplay failed\n");
        return 1;
    }

    grab_keys(display);
    const KeyCode overlay_keycode = XKeysymToKeycode(display, toggle_overlay_key);

    XSetErrorHandler(ignore_xerror);
    /* Killing gpu-screen-recorder with SIGTERM also gives us SIGTERM. We want to ignore that as that has no meaning here */
    signal(SIGTERM, sigterm_handler);

    pid_t overlay_pid = -1;

    XEvent xev;
    for(;;) {
        XNextEvent(display, &xev);
        if(xev.type == KeyPress && xev.xkey.keycode == overlay_keycode) {
            if(overlay_pid != -1) {
                int status;
                if(waitpid(overlay_pid, &status, WNOHANG) == 0)
                    continue; // GPU Screen Recorder overlay is still running
                
                overlay_pid = -1;
            }

            Window window_with_input_focus = get_window_with_input_focus(display);
            fprintf(stderr, "window with focus: %ld\n", window_with_input_focus);
            if(window_with_input_focus && window_with_input_focus != DefaultRootWindow(display) && overlay_pid == -1) {
                fprintf(stderr, "launch overlay\n");
                // TODO: window_with_input_focus
                const char *args[] = {
                    "/home/dec05eba/git/gpu-screen-recorder-overlay/sibs-build/linux_x86_64/debug/gpu-screen-recorder-overlay",
                    NULL
                };
                exec_program(args, &overlay_pid);
            }
        }
    }
    return 0;
}
