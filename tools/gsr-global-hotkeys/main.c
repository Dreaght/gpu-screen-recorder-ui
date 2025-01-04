#include "keyboard_event.h"

/* C stdlib */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* POSIX */
#include <unistd.h>

typedef struct {
    uint32_t key;
    uint32_t modifiers; /* keyboard_modkeys bitmask */
    const char *action;
} global_hotkey;

#define NUM_GLOBAL_HOTKEYS 6
static global_hotkey global_hotkeys[NUM_GLOBAL_HOTKEYS] = {
    { .key = KEY_Z,   .modifiers = KEYBOARD_MODKEY_ALT,                         .action = "show_hide"    },
    { .key = KEY_F9,  .modifiers = KEYBOARD_MODKEY_ALT,                         .action = "record"       },
    { .key = KEY_F7,  .modifiers = KEYBOARD_MODKEY_ALT,                         .action = "pause"        },
    { .key = KEY_F8,  .modifiers = KEYBOARD_MODKEY_ALT,                         .action = "stream"       },
    { .key = KEY_F10, .modifiers = KEYBOARD_MODKEY_ALT | KEYBOARD_MODKEY_SHIFT, .action = "replay_start" },
    { .key = KEY_F10, .modifiers = KEYBOARD_MODKEY_ALT,                         .action = "replay_save"  }
};

static bool on_key_callback(uint32_t key, uint32_t modifiers, int press_status, void *userdata) {
    (void)userdata;
    for(int i = 0; i < NUM_GLOBAL_HOTKEYS; ++i) {
        if(key == global_hotkeys[i].key && modifiers == global_hotkeys[i].modifiers) {
            if(press_status == 1) { /* 1 == Pressed */
                puts(global_hotkeys[i].action);
                fflush(stdout);
            }
            return false;
        }
    }
    return true;
}

static void usage(void) {
    fprintf(stderr, "usage: gsr-global-hotkeys [--all|--virtual]\n");
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "  --all        Grab all devices.\n");
    fprintf(stderr, "  --virtual    Grab all virtual devices only.\n");
}

int main(int argc, char **argv) {
    keyboard_grab_type grab_type = KEYBOARD_GRAB_TYPE_ALL;
    if(argc == 2) {
        const char *grab_type_arg = argv[1];
        if(strcmp(grab_type_arg, "--all") == 0) {
            grab_type = KEYBOARD_GRAB_TYPE_ALL;
        } else if(strcmp(grab_type_arg, "--virtual") == 0) {
            grab_type = KEYBOARD_GRAB_TYPE_VIRTUAL;
        } else {
            fprintf(stderr, "Error: expected --all or --virtual, got %s\n", grab_type_arg);
            usage();
            return 1;
        }
    } else if(argc != 1) {
        fprintf(stderr, "Error: expected 0 or 1 arguments, got %d argument(s)\n", argc);
        usage();
        return 1;
    }

    const uid_t user_id = getuid();
    if(geteuid() != 0) {
        if(setuid(0) == -1) {
            fprintf(stderr, "Error: failed to change user to root\n");
            return 1;
        }
    }

    keyboard_event keyboard_ev;
    if(!keyboard_event_init(&keyboard_ev, true, true, grab_type)) {
        fprintf(stderr, "Error: failed to setup hotplugging and no keyboard input devices were found\n");
        setuid(user_id);
        return 1;
    }

    fprintf(stderr, "Info: global hotkeys setup, waiting for hotkeys to be pressed\n");

    for(;;) {
        keyboard_event_poll_events(&keyboard_ev, -1, on_key_callback, NULL);
        if(keyboard_event_stdout_has_failed(&keyboard_ev)) {
            fprintf(stderr, "Info: stdout closed (parent process likely closed this process), exiting...\n");
            break;
        }
    }

    keyboard_event_deinit(&keyboard_ev);
    setuid(user_id);
    return 0;
}
