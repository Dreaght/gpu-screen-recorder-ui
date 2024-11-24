#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <poll.h>

#include <libudev.h>
#include <libinput.h>
#include <libevdev/libevdev.h>
#include <xkbcommon/xkbcommon.h>

typedef struct {
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
} key_mapper;

typedef enum {
    MODKEY_ALT   = 1 << 0,
    MODKEY_SUPER = 1 << 1,
    MODKEY_CTRL  = 1 << 2,
    MODKEY_SHIFT = 1 << 3
} modkeys;

typedef struct {
    uint32_t key;
    uint32_t modifiers; /* modkeys */
    const char *action;
} global_hotkey;

#define NUM_GLOBAL_HOTKEYS 6
static global_hotkey global_hotkeys[NUM_GLOBAL_HOTKEYS] = {
    { .key = XKB_KEY_z,   .modifiers = MODKEY_ALT,                .action = "show_hide"    },
    { .key = XKB_KEY_F9,  .modifiers = MODKEY_ALT,                .action = "record"       },
    { .key = XKB_KEY_F7,  .modifiers = MODKEY_ALT,                .action = "pause"        },
    { .key = XKB_KEY_F8,  .modifiers = MODKEY_ALT,                .action = "stream"       },
    { .key = XKB_KEY_F10, .modifiers = MODKEY_ALT | MODKEY_SHIFT, .action = "replay_start" },
    { .key = XKB_KEY_F10, .modifiers = MODKEY_ALT,                .action = "replay_save"  }
};

static int open_restricted(const char *path, int flags, void *user_data) {
    (void)user_data;
    int fd = open(path, flags);
    if(fd < 0)
        fprintf(stderr, "Failed to open %s, error: %s\n", path, strerror(errno));
    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data) {
    (void)user_data;
    close(fd);
}

static const struct libinput_interface interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static bool is_mod_key(xkb_keycode_t xkb_key_code) {
    return xkb_key_code >= XKB_KEY_Shift_L && xkb_key_code <= XKB_KEY_Hyper_R;
}

typedef struct {
    const char *modname;
    modkeys key;
} modname_to_modkey_map;

static uint32_t xkb_state_to_modifiers(struct xkb_state *xkb_state) {
    const modname_to_modkey_map modifier_keys[] = {
        { .modname = XKB_MOD_NAME_ALT,   .key = MODKEY_ALT   },
        { .modname = XKB_MOD_NAME_LOGO,  .key = MODKEY_SUPER },
        { .modname = XKB_MOD_NAME_SHIFT, .key = MODKEY_SHIFT },
        { .modname = XKB_MOD_NAME_CTRL,  .key = MODKEY_CTRL  }
    };

    uint32_t modifiers = 0;
    for(int i = 0; i < 4; ++i) {
        if(xkb_state_mod_name_is_active(xkb_state, modifier_keys[i].modname, XKB_STATE_MODS_EFFECTIVE) > 0)
            modifiers |= modifier_keys[i].key;
    }
    return modifiers;
}

#define KEY_CODE_EV_TO_XKB(key) ((key) + 8)

static int print_key_event(struct libinput_event *event, key_mapper *mapper) {
    struct libinput_event_keyboard *keyboard = libinput_event_get_keyboard_event(event);
    const uint32_t key_code = libinput_event_keyboard_get_key(keyboard);
    enum libinput_key_state state_code = libinput_event_keyboard_get_key_state(keyboard);

    const xkb_keycode_t xkb_key_code = KEY_CODE_EV_TO_XKB(key_code);
    xkb_state_update_key(mapper->xkb_state, xkb_key_code, state_code == LIBINPUT_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
    xkb_keysym_t xkb_key_sym = xkb_state_key_get_one_sym(mapper->xkb_state, xkb_key_code);
    // char main_key[128];
    // main_key[0] = '\0';

    // if(xkb_state_mod_name_is_active(mapper->xkb_state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)
    //     strcat(main_key, "Super+");
    // if(xkb_state_mod_name_is_active(mapper->xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
    //     strcat(main_key, "Ctrl+");
    // if(xkb_state_mod_name_is_active(mapper->xkb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0 && strcmp(main_key, "Meta") != 0)
    //     strcat(main_key, "Alt+");
    // if(xkb_state_mod_name_is_active(mapper->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
    //     strcat(main_key, "Shift+");

    // if(!is_mod_key(xkb_key_sym)) {
    //     char reg_key[64];
    //     reg_key[0] = '\0';
    //     xkb_keysym_get_name(xkb_key_sym, reg_key, sizeof(reg_key));
    //     strcat(main_key, reg_key);
    // }

    if(state_code != LIBINPUT_KEY_STATE_PRESSED)
        return 0;

    const uint32_t current_modifiers = xkb_state_to_modifiers(mapper->xkb_state);
    for(int i = 0; i < NUM_GLOBAL_HOTKEYS; ++i) {
        if(xkb_key_sym == global_hotkeys[i].key && current_modifiers == global_hotkeys[i].modifiers) {
            puts(global_hotkeys[i].action);
            fflush(stdout);
            break;
        }
    }

    return 0;
}

static int handle_events(struct libinput *libinput, key_mapper *mapper) {
    int result = -1;
    struct libinput_event *event;

    if(libinput_dispatch(libinput) < 0)
        return result;

    while((event = libinput_get_event(libinput)) != NULL) {
        if(libinput_event_get_type(event) == LIBINPUT_EVENT_KEYBOARD_KEY)
            print_key_event(event, mapper);

        libinput_event_destroy(event);
        result = 0;
    }

    return result;
}

static int run_mainloop(struct libinput *libinput, key_mapper *mapper) {
    struct pollfd fd;
    fd.fd = libinput_get_fd(libinput);
    fd.events = POLLIN;
    fd.revents = 0;

    if(handle_events(libinput, mapper) != 0) {
        fprintf(stderr, "error: didn't receive device added events. Is this program not running as root?\n");
        return -1;
    }

    while(poll(&fd, 1, -1) >= 0) {
        handle_events(libinput, mapper);
    }

    return 0;
}

static bool mapper_refresh_keymap(key_mapper *mapper) {
    if(mapper->xkb_keymap != NULL) {
        xkb_keymap_unref(mapper->xkb_keymap);
        mapper->xkb_keymap = NULL;
    }

    // TODO:
    struct xkb_rule_names names = {
        NULL, NULL,
        NULL,//keymap_is_default(mapper->layout) ? NULL : mapper->layout,
        NULL,//keymap_is_default(mapper->variant) ? NULL : mapper->variant,
        NULL
    };
    mapper->xkb_keymap = xkb_keymap_new_from_names(mapper->xkb_context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if(mapper->xkb_keymap == NULL) {
        fprintf(stderr, "Failed to create XKB keymap.\n");
        return false;
    }

    if(mapper->xkb_state != NULL) {
        xkb_state_unref(mapper->xkb_state);
        mapper->xkb_state = NULL;
    }

    mapper->xkb_state = xkb_state_new(mapper->xkb_keymap);
    if(mapper->xkb_state == NULL) {
        fprintf(stderr, "Failed to create XKB state.\n");
        return false;
    }

    return true;
}

int main(void) {
    struct udev *udev = udev_new();
    if(!udev) {
        fprintf(stderr, "error: udev_new failed\n");
        return 1;
    }

    struct libinput *libinput = libinput_udev_create_context(&interface, NULL, udev);
    if(!libinput) {
        fprintf(stderr, "error: libinput_udev_create_context failed\n");
        return 1;
    }

    if(libinput_udev_assign_seat(libinput, "seat0") != 0) {
        fprintf(stderr, "error: libinput_udev_assign_seat with seat0 failed\n");
        return 1;
    }

    key_mapper mapper;
    mapper.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if(!mapper.xkb_context) {
        fprintf(stderr, "error: xkb_context_new failed\n");
        return 1;
    }

    if(!mapper_refresh_keymap(&mapper)) {
        fprintf(stderr, "error: key mapper failed\n");
        return 1;
    }

    if(run_mainloop(libinput, &mapper) < 0) {
        fprintf(stderr, "error: failed to start main loop\n");
        return 1;
    }

    libinput_unref(libinput);
    udev_unref(udev);
    return 0;
}
