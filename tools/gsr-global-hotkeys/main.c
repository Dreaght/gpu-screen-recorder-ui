#include "keyboard_event.h"

/* C stdlib */
#include <stdio.h>
#include <string.h>
#include <locale.h>

/* POSIX */
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: gsr-global-hotkeys [--all|--virtual]\n");
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "  --all        Grab all devices.\n");
    fprintf(stderr, "  --virtual    Grab all virtual devices only.\n");
}

static bool is_gsr_global_hotkeys_already_running(void) {
    FILE *f = fopen("/proc/bus/input/devices", "rb");
    if(!f)
        return false;

    bool virtual_keyboard_running = false;
    char line[1024];
    while(fgets(line, sizeof(line), f)) {
        if(strstr(line, "gsr-ui virtual keyboard")) {
            virtual_keyboard_running = true;
            break;
        }
    }

    fclose(f);
    return virtual_keyboard_running;
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "C"); /* Sigh... stupid C */

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

    if(is_gsr_global_hotkeys_already_running()) {
        fprintf(stderr, "Error: gsr-global-hotkeys is already running\n");
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
    if(!keyboard_event_init(&keyboard_ev, true, grab_type)) {
        fprintf(stderr, "Error: failed to setup hotplugging and no keyboard input devices were found\n");
        setuid(user_id);
        return 1;
    }

    fprintf(stderr, "Info: global hotkeys setup, waiting for hotkeys to be pressed\n");

    for(;;) {
        keyboard_event_poll_events(&keyboard_ev, -1);
        if(keyboard_event_stdin_has_failed(&keyboard_ev)) {
            fprintf(stderr, "Info: stdin closed (parent process likely closed this process), exiting...\n");
            break;
        }
    }

    keyboard_event_deinit(&keyboard_ev);
    setuid(user_id);
    return 0;
}
