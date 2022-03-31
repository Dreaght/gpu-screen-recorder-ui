#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

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

static Window get_window_with_input_focus(Display *display) {
    Window window = None;
    int rev;
    if(!XGetInputFocus(display, &window, &rev))
        window = None;
    return window;
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

    XEvent xev;
    for(;;) {
        XNextEvent(display, &xev);
        if(xev.type == KeyRelease && xev.xkey.keycode == overlay_keycode) {
            Window window_with_input_focus = get_window_with_input_focus(display);
            if(window_with_input_focus && window_with_input_focus != DefaultRootWindow(display)) {
                char cmd[1024];
                snprintf(cmd, sizeof(cmd), "/home/dec05eba/git/gpu-screen-recorder-overlay/sibs-build/linux_x86_64/debug/gpu-screen-recorder-overlay %ld", window_with_input_focus);
                system(cmd);
            }
        }
    }
    return 0;
}
