#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

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
static unsigned int toggle_overlay_modifiers = Mod1Mask;

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
        XGrabKey(display, XKeysymToKeycode(display, toggle_overlay_key), toggle_overlay_modifiers|modifiers[i], root_window, False, GrabModeAsync, GrabModeAsync);
    }    
    
    XSync(display, False);
    XSetErrorHandler(prev_error_handler);
}

int main(void) {
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

    fprintf(stderr, "gsr ui is now ready, waiting for inputs. Press alt+z to show/hide the ui\n");

    XEvent xev;
    for(;;) {
        XNextEvent(display, &xev);
        if(xev.type == KeyPress && xev.xkey.keycode == overlay_keycode) {
            if(overlay_pid != -1) {
                int status;
                if(waitpid(overlay_pid, &status, WNOHANG) == 0) {
                    kill(overlay_pid, SIGINT);
                    int status;
                    if(waitpid(overlay_pid, &status, 0) == -1) {
                        perror("waitpid failed");
                        /* Ignore... */
                    }
                    overlay_pid = -1;
                    continue;
                } else {
                    overlay_pid = -1;
                }
            }

            if(overlay_pid == -1) {
                fprintf(stderr, "launch ui\n");
                // TODO: window_with_input_focus
                const char *args[] = { "gsr-ui", NULL };
                exec_program(args, &overlay_pid);
            }
        }
    }
    return 0;
}
