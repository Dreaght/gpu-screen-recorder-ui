#ifndef KEYBOARD_EVENT_H
#define KEYBOARD_EVENT_H

/* Read keyboard input from linux /dev/input/eventN devices, with hotplug support */

#include "hotplug.h"

/* C stdlib */
#include <stdbool.h>
#include <stdint.h>

/* POSIX */
#include <sys/poll.h>

/* Linux */
#include <linux/input-event-codes.h>

/* System */
#include <X11/Xlib.h>

#define MAX_EVENT_POLLS 32

typedef enum {
    KEYBOARD_MODKEY_LALT   = 1 << 0,
    KEYBOARD_MODKEY_RALT   = 1 << 2,
    KEYBOARD_MODKEY_SUPER  = 1 << 3,
    KEYBOARD_MODKEY_CTRL   = 1 << 4,
    KEYBOARD_MODKEY_SHIFT  = 1 << 5
} keyboard_modkeys;

typedef enum {
    KEYBOARD_BUTTON_RELEASED,
    KEYBOARD_BUTTON_PRESSED
} keyboard_button_state;

typedef struct {
    int dev_input_id;
    bool grabbed;
    unsigned char *key_states;
    int num_keys_pressed;
} event_extra_data;

typedef enum {
    KEYBOARD_GRAB_TYPE_ALL,
    KEYBOARD_GRAB_TYPE_VIRTUAL
} keyboard_grab_type;

typedef struct {
    struct pollfd event_polls[MAX_EVENT_POLLS];      /* Current size is |num_event_polls| */
    event_extra_data event_extra_data[MAX_EVENT_POLLS]; /* Current size is |num_event_polls| */
    int num_event_polls;

    int stdout_event_index;
    int hotplug_event_index;
    int uinput_fd;
    bool stdout_failed;
    keyboard_grab_type grab_type;
    Display *display;

    hotplug_event hotplug_ev;

    keyboard_button_state lshift_button_state;
    keyboard_button_state rshift_button_state;
    keyboard_button_state lctrl_button_state;
    keyboard_button_state rctrl_button_state;
    keyboard_button_state lalt_button_state;
    keyboard_button_state ralt_button_state;
    keyboard_button_state lmeta_button_state;
    keyboard_button_state rmeta_button_state;
} keyboard_event;

/* |key| is a KEY_ from linux/input-event-codes.h. |modifiers| is a bitmask of keyboard_modkeys. |press_status| is 0 for released, 1 for pressed and 2 for repeat */
/* Return true to allow other applications to receive the key input (when using exclusive grab) */
typedef bool (*key_callback)(uint32_t key, uint32_t modifiers, int press_status, void *userdata);

bool keyboard_event_init(keyboard_event *self, bool poll_stdout_error, bool exclusive_grab, keyboard_grab_type grab_type, Display *display);
void keyboard_event_deinit(keyboard_event *self);

/* If |timeout_milliseconds| is -1 then wait until an event is received */
void keyboard_event_poll_events(keyboard_event *self, int timeout_milliseconds, key_callback callback, void *userdata);
bool keyboard_event_stdout_has_failed(const keyboard_event *self);

#endif /* KEYBOARD_EVENT_H */
