#include "keyboard_event.h"

/* C stdlib */
#include <stdio.h>
#include <stdint.h>

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

int main(void) {
    const uid_t user_id = getuid();
    if(geteuid() != 0) {
        if(setuid(0) == -1) {
            fprintf(stderr, "Error: failed to change user to root\n");
            return 1;
        }
    }

    keyboard_event keyboard_ev;
    if(!keyboard_event_init(&keyboard_ev, true, true)) {
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
