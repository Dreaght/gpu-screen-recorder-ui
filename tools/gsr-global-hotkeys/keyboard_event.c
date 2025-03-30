#include "keyboard_event.h"
#include "keys.h"

/* C stdlib */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

/* POSIX */
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>

/* LINUX */
#include <linux/input.h>
#include <linux/uinput.h>

#define GSR_UI_VIRTUAL_KEYBOARD_NAME "gsr-ui virtual keyboard"

#define KEY_RELEASE 0
#define KEY_PRESS 1
#define KEY_REPEAT 2

#define KEY_STATES_SIZE (KEY_MAX/8 + 1)

static inline int count_num_bits_set(unsigned char c) {
    int n = 0;
    n += (c & 1);
    c >>= 1;
    n += (c & 1);
    c >>= 1;
    n += (c & 1);
    c >>= 1;
    n += (c & 1);
    c >>= 1;
    n += (c & 1);
    c >>= 1;
    n += (c & 1);
    c >>= 1;
    n += (c & 1);
    c >>= 1;
    n += (c & 1);
    return n;
}

static inline bool keyboard_event_has_exclusive_grab(const keyboard_event *self) {
    return self->uinput_fd > 0;
}

static int keyboard_event_get_num_keys_pressed(const unsigned char *key_states) {
    if(!key_states)
        return 0;

    int num_keys_pressed = 0;
    for(int i = 0; i < KEY_STATES_SIZE; ++i) {
        num_keys_pressed += count_num_bits_set(key_states[i]);
    }
    return num_keys_pressed;
}

static void keyboard_event_fetch_update_key_states(keyboard_event *self, event_extra_data *extra_data, int fd) {
    fsync(fd);
    if(!extra_data->key_states)
        return;

    if(ioctl(fd, EVIOCGKEY(KEY_STATES_SIZE), extra_data->key_states) == -1)
        fprintf(stderr, "Warning: failed to fetch key states for device: /dev/input/event%d\n", extra_data->dev_input_id);

    if(!keyboard_event_has_exclusive_grab(self) || extra_data->grabbed)
        return;

    extra_data->num_keys_pressed = keyboard_event_get_num_keys_pressed(extra_data->key_states);
    if(extra_data->num_keys_pressed == 0) {
        extra_data->grabbed = ioctl(fd, EVIOCGRAB, 1) != -1;
        if(extra_data->grabbed)
            fprintf(stderr, "Info: grabbed device: /dev/input/event%d\n", extra_data->dev_input_id);
        else
            fprintf(stderr, "Warning: failed to exclusively grab device: /dev/input/event%d. The focused application may receive keys used for global hotkeys\n", extra_data->dev_input_id);
    }
}

static void keyboard_event_process_key_state_change(keyboard_event *self, const struct input_event *event, event_extra_data *extra_data, int fd) {
    if(event->type != EV_KEY)
        return;

    if(!extra_data->key_states || event->code >= KEY_STATES_SIZE * 8)
        return;

    const unsigned int byte_index = event->code / 8;
    const unsigned char bit_index = event->code % 8;
    unsigned char key_byte_state = extra_data->key_states[byte_index];
    const bool prev_key_pressed = (key_byte_state & (1 << bit_index)) != KEY_RELEASE;

    if(event->value == KEY_RELEASE) {
        key_byte_state &= ~(1 << bit_index);
        if(prev_key_pressed)
            --extra_data->num_keys_pressed;
    } else {
        key_byte_state |= (1 << bit_index);
        if(!prev_key_pressed)
            ++extra_data->num_keys_pressed;
    }

    extra_data->key_states[byte_index] = key_byte_state;

    if(!keyboard_event_has_exclusive_grab(self) || extra_data->grabbed)
        return;

    if(extra_data->num_keys_pressed == 0) {
        extra_data->grabbed = ioctl(fd, EVIOCGRAB, 1) != -1;
        if(extra_data->grabbed)
            fprintf(stderr, "Info: grabbed device: /dev/input/event%d\n", extra_data->dev_input_id);
        else
            fprintf(stderr, "Warning: failed to exclusively grab device: /dev/input/event%d. The focused application may receive keys used for global hotkeys\n", extra_data->dev_input_id);
    }
}

/* Return true if a global hotkey is assigned to the key combination */
static bool keyboard_event_on_key_pressed(keyboard_event *self, const struct input_event *event, uint32_t modifiers) {
    if(event->value != KEYBOARD_BUTTON_PRESSED)
        return false;

    bool global_hotkey_match = false;
    for(int i = 0; i < self->num_global_hotkeys; ++i) {
        if(event->code == self->global_hotkeys[i].key && modifiers == self->global_hotkeys[i].modifiers) {
            puts(self->global_hotkeys[i].action);
            fflush(stdout);
            global_hotkey_match = true;
        }
    }
    return global_hotkey_match;
}

static inline uint32_t set_bit(uint32_t value, uint32_t bit_flag, bool set) {
    if(set)
        return value | bit_flag;
    else
        return value & ~bit_flag;
}

static uint32_t keycode_to_modifier_bit(uint32_t keycode) {
    switch(keycode) {
        case KEY_LEFTSHIFT:  return KEYBOARD_MODKEY_LSHIFT;
        case KEY_RIGHTSHIFT: return KEYBOARD_MODKEY_RSHIFT;
        case KEY_LEFTCTRL:   return KEYBOARD_MODKEY_LCTRL;
        case KEY_RIGHTCTRL:  return KEYBOARD_MODKEY_RCTRL;
        case KEY_LEFTALT:    return KEYBOARD_MODKEY_LALT;
        case KEY_RIGHTALT:   return KEYBOARD_MODKEY_RALT;
        case KEY_LEFTMETA:   return KEYBOARD_MODKEY_LSUPER;
        case KEY_RIGHTMETA:  return KEYBOARD_MODKEY_RSUPER;
    }
    return 0;
}

static void keyboard_event_process_input_event_data(keyboard_event *self, event_extra_data *extra_data, int fd) {
    struct input_event event;
    if(read(fd, &event, sizeof(event)) != sizeof(event)) {
        fprintf(stderr, "Error: failed to read input event data\n");
        return;
    }

    if(event.type == EV_SYN && event.code == SYN_DROPPED) {
        /* TODO: Don't do this on every SYN_DROPPED to prevent spamming this, instead wait until the next event or wait for timeout */
        keyboard_event_fetch_update_key_states(self, extra_data, fd);
        return;
    }

    //if(event.type == EV_KEY && event.code == KEY_A && event.value == KEY_PRESS) {
        //fprintf(stderr, "fd: %d, type: %d, pressed %d, value: %d\n", fd, event.type, event.code, event.value);
    //}

    if(event.type == EV_KEY && is_keyboard_key(event.code)) {
        keyboard_event_process_key_state_change(self, &event, extra_data, fd);
        const uint32_t modifier_bit = keycode_to_modifier_bit(event.code);
        if(modifier_bit == 0) {
            if(keyboard_event_on_key_pressed(self, &event, self->modifier_button_states))
                return;
        } else {
            self->modifier_button_states = set_bit(self->modifier_button_states, modifier_bit, event.value >= 1);
        }
    }

    if(extra_data->grabbed) {
        /* TODO: What to do on error? */
        if(write(self->uinput_fd, &event, sizeof(event)) != sizeof(event))
            fprintf(stderr, "Error: failed to write event data to virtual keyboard for exclusively grabbed device\n");
    }
}

/* Retarded linux takes very long time to close /dev/input/eventN files, even though they are virtual and opened read-only */
static void* keyboard_event_close_fds_callback(void *userdata) {
    keyboard_event *self = userdata;
    while(self->running) {
        pthread_mutex_lock(&self->close_dev_input_mutex);
        for(int i = 0; i < self->num_close_fds; ++i) {
            close(self->close_fds[i]);
        }
        self->num_close_fds = 0;
        pthread_mutex_unlock(&self->close_dev_input_mutex);

        usleep(100 * 1000); /* 100 milliseconds */
    }
    return NULL;
}

static bool keyboard_event_try_add_close_fd(keyboard_event *self, int fd) {
    bool success = false;
    pthread_mutex_lock(&self->close_dev_input_mutex);
    if(self->num_close_fds < MAX_CLOSE_FDS) {
        self->close_fds[self->num_close_fds] = fd;
        ++self->num_close_fds;
        success = true;
    } else {
        success = false;
    }
    pthread_mutex_unlock(&self->close_dev_input_mutex);
    return success;
}

/* Returns -1 if invalid format. Expected |dev_input_filepath| to be in format /dev/input/eventN */
static int get_dev_input_id_from_filepath(const char *dev_input_filepath) {
    if(strncmp(dev_input_filepath, "/dev/input/event", 16) != 0)
        return -1;

    int dev_input_id = -1;
    if(sscanf(dev_input_filepath + 16, "%d", &dev_input_id) == 1)
        return dev_input_id;
    return -1;
}

static bool keyboard_event_has_event_with_dev_input_fd(keyboard_event *self, int dev_input_id) {
    for(int i = 0; i < self->num_event_polls; ++i) {
        if(self->event_extra_data[i].dev_input_id == dev_input_id)
            return true;
    }
    return false;
}

/* TODO: Is there a more efficient way to do this? */
static bool dev_input_is_virtual(int dev_input_id) {
    DIR *dir = opendir("/sys/devices/virtual/input");
    if(!dir)
        return false;

    bool is_virtual = false;
    char virtual_input_filepath[1024];
    for(;;) {
        struct dirent *entry = readdir(dir);
        if(!entry)
            break;

        if(strncmp(entry->d_name, "input", 5) != 0)
            continue;

        snprintf(virtual_input_filepath, sizeof(virtual_input_filepath), "/sys/devices/virtual/input/%s/event%d", entry->d_name, dev_input_id);
        if(access(virtual_input_filepath, F_OK) == 0) {
            is_virtual = true;
            break;
        }
    }

    closedir(dir);
    return is_virtual;
}

static bool keyboard_event_try_add_device_if_keyboard(keyboard_event *self, const char *dev_input_filepath) {
    const int dev_input_id = get_dev_input_id_from_filepath(dev_input_filepath);
    if(dev_input_id == -1)
        return false;

    const bool is_virtual_device = dev_input_is_virtual(dev_input_id);
    if(self->grab_type == KEYBOARD_GRAB_TYPE_VIRTUAL && !is_virtual_device)
        return false;

    if(keyboard_event_has_event_with_dev_input_fd(self, dev_input_id))
        return false;

    const int fd = open(dev_input_filepath, O_RDONLY);
    if(fd == -1)
        return false;

    char device_name[256];
    device_name[0] = '\0';
    ioctl(fd, EVIOCGNAME(sizeof(device_name)), device_name);

    unsigned long evbit = 0;
    ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit);
    const bool is_keyboard = (evbit & (1 << EV_SYN)) && (evbit & (1 << EV_KEY));

    if(is_keyboard && strcmp(device_name, GSR_UI_VIRTUAL_KEYBOARD_NAME) != 0) {
        unsigned char key_bits[KEY_MAX/8 + 1] = {0};
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), &key_bits);

        const bool supports_key_a           = key_bits[KEY_A/8]        & (1 << (KEY_A % 8));
        const bool supports_key_esc         = key_bits[KEY_ESC/8]      & (1 << (KEY_ESC % 8));
        const bool supports_key_volume_up   = key_bits[KEY_VOLUMEUP/8] & (1 << (KEY_VOLUMEUP % 8));
        const bool supports_key_events = supports_key_a || supports_key_esc || supports_key_volume_up;

        const bool supports_mouse_events    = key_bits[BTN_MOUSE/8]    & (1 << (BTN_MOUSE % 8));
        //const bool supports_touch_events    = key_bits[BTN_TOUCH/8]    & (1 << (BTN_TOUCH % 8));
        const bool supports_joystick_events = key_bits[BTN_JOYSTICK/8] & (1 << (BTN_JOYSTICK % 8));
        const bool supports_wheel_events    = key_bits[BTN_WHEEL/8]    & (1 << (BTN_WHEEL % 8));
        if(supports_key_events && (is_virtual_device || (!supports_joystick_events && !supports_wheel_events))) {
            unsigned char *key_states = calloc(1, KEY_STATES_SIZE);
            if(key_states && self->num_event_polls < MAX_EVENT_POLLS) {
                //fprintf(stderr, "%s (%s) supports key inputs\n", dev_input_filepath, device_name);
                self->event_polls[self->num_event_polls] = (struct pollfd) {
                    .fd = fd,
                    .events = POLLIN,
                    .revents = 0
                };

                self->event_extra_data[self->num_event_polls] = (event_extra_data) {
                    .dev_input_id = dev_input_id,
                    .grabbed = false,
                    .key_states = key_states,
                    .num_keys_pressed = 0
                };

                if(supports_mouse_events || supports_joystick_events || supports_wheel_events) {
                    fprintf(stderr, "Info: device not grabbed yet because it might be a mouse: /dev/input/event%d\n", dev_input_id);
                    fsync(fd);
                    if(ioctl(fd, EVIOCGKEY(KEY_STATES_SIZE), self->event_extra_data[self->num_event_polls].key_states) == -1)
                        fprintf(stderr, "Warning: failed to fetch key states for device: /dev/input/event%d\n", dev_input_id);
                } else {
                    keyboard_event_fetch_update_key_states(self, &self->event_extra_data[self->num_event_polls], fd);
                    if(self->event_extra_data[self->num_event_polls].num_keys_pressed > 0)
                        fprintf(stderr, "Info: device not grabbed yet because some keys are still being pressed: /dev/input/event%d\n", dev_input_id);
                }

                ++self->num_event_polls;
                return true;
            } else {
                fprintf(stderr, "Warning: the maximum number of keyboard devices have been registered. The newly added keyboard will be ignored\n");
            }
        }
    }

    if(!keyboard_event_try_add_close_fd(self, fd)) {
        fprintf(stderr, "Error: failed to add immediately, closing now\n");
        close(fd);
    }
    return false;
}

static bool keyboard_event_add_dev_input_devices(keyboard_event *self) {
    DIR *dir = opendir("/dev/input");
    if(!dir) {
        fprintf(stderr, "error: failed to open /dev/input, error: %s\n", strerror(errno));
        return false;
    }

    char dev_input_filepath[1024];
    for(;;) {
        struct dirent *entry = readdir(dir);
        if(!entry)
            break;

        if(strncmp(entry->d_name, "event", 5) != 0)
            continue;

        snprintf(dev_input_filepath, sizeof(dev_input_filepath), "/dev/input/%s", entry->d_name);
        keyboard_event_try_add_device_if_keyboard(self, dev_input_filepath);
    }

    closedir(dir);
    return true;
}

static void keyboard_event_remove_event(keyboard_event *self, int index) {
    if(index < 0 || index >= self->num_event_polls)
        return;

    ioctl(self->event_polls[index].fd, EVIOCGRAB, 0);
    close(self->event_polls[index].fd);
    free(self->event_extra_data[index].key_states);

    for(int i = index + 1; i < self->num_event_polls; ++i) {
        self->event_polls[i - 1] = self->event_polls[i];
        self->event_extra_data[i - 1] = self->event_extra_data[i];
    }
    --self->num_event_polls;
}

/* Returns the fd to the uinput */
/* Documented here: https://www.kernel.org/doc/html/v4.12/input/uinput.html */
static int setup_virtual_keyboard_input(const char *name) {
    /* TODO: O_NONBLOCK? */
    int fd = open("/dev/uinput", O_WRONLY);
    if(fd == -1) {
        fd = open("/dev/input/uinput", O_WRONLY);
        if(fd == -1) {
            fprintf(stderr, "Warning: failed to setup virtual device for exclusive grab (failed to open /dev/uinput or /dev/input/uinput), error: %s\n", strerror(errno));
            return -1;
        }
    }

    bool success = true;
    success &= (ioctl(fd, UI_SET_EVBIT, EV_SYN) != -1);
    success &= (ioctl(fd, UI_SET_EVBIT, EV_MSC) != -1);
    success &= (ioctl(fd, UI_SET_EVBIT, EV_KEY) != -1);
    success &= (ioctl(fd, UI_SET_EVBIT, EV_REP) != -1);
    success &= (ioctl(fd, UI_SET_EVBIT, EV_REL) != -1);
    success &= (ioctl(fd, UI_SET_EVBIT, EV_LED) != -1);

    success &= (ioctl(fd, UI_SET_MSCBIT, MSC_SCAN) != -1);
    for(int i = 1; i < KEY_MAX; ++i) {
        if(is_keyboard_key(i) || is_mouse_button(i))
            success &= (ioctl(fd, UI_SET_KEYBIT, i) != -1);
    }
    for(int i = 0; i < REL_MAX; ++i) {
        success &= (ioctl(fd, UI_SET_RELBIT, i) != -1);
    }
    for(int i = 0; i < LED_MAX; ++i) {
        success &= (ioctl(fd, UI_SET_LEDBIT, i) != -1);
    }

    // success &= (ioctl(fd, UI_SET_EVBIT, EV_ABS) != -1);
    // success &= (ioctl(fd, UI_SET_ABSBIT, ABS_X) != -1);
    // success &= (ioctl(fd, UI_SET_ABSBIT, ABS_Y) != -1);
    // success &= (ioctl(fd, UI_SET_ABSBIT, ABS_Z) != -1);

    int ui_version = 0;
    success &= (ioctl(fd, UI_GET_VERSION, &ui_version) != -1);

    if(ui_version >= 5) {
        struct uinput_setup usetup;
        memset(&usetup, 0, sizeof(usetup));
        usetup.id.bustype = BUS_USB;
        usetup.id.vendor = 0xdec0;
        usetup.id.product = 0x5eba;
        snprintf(usetup.name, sizeof(usetup.name), "%s", name);
        success &= (ioctl(fd, UI_DEV_SETUP, &usetup) != -1);
    } else {
        struct uinput_user_dev uud;
        memset(&uud, 0, sizeof(uud));
        snprintf(uud.name, UINPUT_MAX_NAME_SIZE, "%s", name);
        if(write(fd, &uud, sizeof(uud)) != sizeof(uud))
            success = false;
    }

    success &= (ioctl(fd, UI_DEV_CREATE) != -1);
    if(!success) {
        close(fd);
        return -1;
    }

    return fd;
}

bool keyboard_event_init(keyboard_event *self, bool exclusive_grab, keyboard_grab_type grab_type) {
    memset(self, 0, sizeof(*self));
    self->stdin_event_index = -1;
    self->hotplug_event_index = -1;
    self->grab_type = grab_type;
    self->running = true;

    pthread_mutex_init(&self->close_dev_input_mutex, NULL);
    if(pthread_create(&self->close_dev_input_fds_thread, NULL, keyboard_event_close_fds_callback, self) != 0) {
        self->close_dev_input_fds_thread = 0;
        fprintf(stderr, "Error: failed to create close fds thread\n");
        return false;
    }

    if(exclusive_grab) {
        self->uinput_fd = setup_virtual_keyboard_input(GSR_UI_VIRTUAL_KEYBOARD_NAME);
        if(self->uinput_fd <= 0)
            fprintf(stderr, "Warning: failed to setup virtual keyboard input for exclusive grab. The focused application will receive keys used for global hotkeys\n");
    }

    self->event_polls[self->num_event_polls] = (struct pollfd) {
        .fd = STDIN_FILENO,
        .events = POLLIN,
        .revents = 0
    };

    self->event_extra_data[self->num_event_polls] = (event_extra_data) {
        .dev_input_id = -1,
        .grabbed = false,
        .key_states = NULL,
        .num_keys_pressed = 0
    };

    self->stdin_event_index = self->num_event_polls;
    ++self->num_event_polls;

    if(hotplug_event_init(&self->hotplug_ev)) {
        self->event_polls[self->num_event_polls] = (struct pollfd) {
            .fd = hotplug_event_steal_fd(&self->hotplug_ev),
            .events = POLLIN,
            .revents = 0
        };

        self->event_extra_data[self->num_event_polls] = (event_extra_data) {
            .dev_input_id = -1,
            .grabbed = false,
            .key_states = NULL,
            .num_keys_pressed = 0
        };

        self->hotplug_event_index = self->num_event_polls;
        ++self->num_event_polls;
    } else {
        fprintf(stderr, "Warning: failed to setup hotplugging\n");
    }

    keyboard_event_add_dev_input_devices(self);

    /* Neither hotplugging nor any keyboard devices were found. We will never listen to keyboard events so might as well fail */
    if(self->num_event_polls == 0) {
        keyboard_event_deinit(self);
        return false;
    }

    return true;
}

void keyboard_event_deinit(keyboard_event *self) {
    self->running = false;

    for(int i = 0; i < self->num_global_hotkeys; ++i) {
        free(self->global_hotkeys[i].action);
    }
    self->num_global_hotkeys = 0;

    if(self->uinput_fd > 0) {
        ioctl(self->uinput_fd, UI_DEV_DESTROY);
        close(self->uinput_fd);
        self->uinput_fd = -1;
    }

    for(int i = 0; i < self->num_event_polls; ++i) {
        ioctl(self->event_polls[i].fd, EVIOCGRAB, 0);
        close(self->event_polls[i].fd);
        free(self->event_extra_data[i].key_states);
    }
    self->num_event_polls = 0;

    hotplug_event_deinit(&self->hotplug_ev);

    if(self->close_dev_input_fds_thread > 0) {
        pthread_join(self->close_dev_input_fds_thread, NULL);
        self->close_dev_input_fds_thread = 0;
    }

    pthread_mutex_destroy(&self->close_dev_input_mutex);
}

static void on_device_added_callback(const char *devname, void *userdata) {
    keyboard_event *keyboard_ev = userdata;
    char dev_input_filepath[256];
    snprintf(dev_input_filepath, sizeof(dev_input_filepath), "/dev/%s", devname);
    keyboard_event_try_add_device_if_keyboard(keyboard_ev, dev_input_filepath);
}

/* Returns -1 on error */
static int parse_u8(const char *str, int size) {
    if(size <= 0)
        return -1;

    int result = 0;
    for(int i = 0; i < size; ++i) {
        char c = str[i];
        if(c >= '0' && c <= '9') {
            result = result * 10 + (c - '0');
            if(result > 255)
                return -1;
        } else {
            return -1;
        }
    }
    return result;
}

static bool is_key_alpha_numerical(uint8_t key) {
    return (key >= KEY_1 && key <= KEY_0)
        || (key >= KEY_Q && key <= KEY_P)
        || (key >= KEY_A && key <= KEY_L)
        || (key >= KEY_Z && key <= KEY_M);
}

static bool keyboard_event_parse_bind_keys(const char *str, int size, uint8_t *key, uint32_t *modifiers) {
    *key = 0;
    *modifiers = 0;

    const char *number_start = str;
    const char *end = str + size;
    for(;;) {
        const char *next = strchr(number_start, '+');
        if(!next)
            next = end;

        const int number_len = next - number_start;
        const int number = parse_u8(number_start, number_len);
        if(number == -1) {
            fprintf(stderr, "Error: bind command keys \"%s\" is in invalid format\n", str);
            return false;
        }

        const uint32_t modifier_bit = keycode_to_modifier_bit(number);
        if(modifier_bit == 0) {
            if(*key != 0) {
                fprintf(stderr, "Error: can't bind hotkey with multiple non-modifier keys\n");
                return false;
            }
            *key = number;
        } else {
            *modifiers = set_bit(*modifiers, modifier_bit, true);
        }

        number_start = next + 1;
        if(next == end)
            break;
    }

    if(*key == 0) {
        fprintf(stderr, "Error: can't bind hotkey without a non-modifier key\n");
        return false;
    }

    if(*modifiers == 0 && is_key_alpha_numerical(*key)) {
        fprintf(stderr, "Error: can't bind hotkey without a modifier unless the key is a non alpha-numerical key\n");
        return false;
    }

    return true;
}

/* |command| is null-terminated */
static void keyboard_event_parse_stdin_command(keyboard_event *self, const char *command, int command_size) {
    if(strncmp(command, "bind ", 5) == 0) {
        /* Example: |bind show_hide 20+40| */
        if(self->num_global_hotkeys >= MAX_GLOBAL_HOTKEYS) {
            fprintf(stderr, "Error: can't add another hotkey. The maximum number of hotkeys (%d) has been reached\n", MAX_GLOBAL_HOTKEYS);
            return;
        }

        const char *action_name_end = strchr(command + 5, ' ');
        if(!action_name_end) {
            fprintf(stderr, "Error: command \"%s\" is in invalid format\n", command);
            return;
        }

        const char *action_name = command + 5;
        const int action_name_size = action_name_end - action_name;

        uint8_t key = 0;
        uint32_t modifiers = 0;
        const char *number_start = action_name_end + 1;
        const char *end = command + command_size;
        if(!keyboard_event_parse_bind_keys(number_start, end - number_start, &key, &modifiers))
            return;

        char *action = strndup(action_name, action_name_size);
        if(!action) {
            fprintf(stderr, "Error: failed to duplicate %.*s\n", action_name_size, action_name);
            return;
        }

        self->global_hotkeys[self->num_global_hotkeys] = (global_hotkey) {
            .action = action,
            .key = key,
            .modifiers = modifiers
        };
        ++self->num_global_hotkeys;
        fprintf(stderr, "Info: binded hotkey: %s\n", action);
    } else if(strncmp(command, "unbind_all", 10) == 0) {
        for(int i = 0; i < self->num_global_hotkeys; ++i) {
            free(self->global_hotkeys[i].action);
        }
        self->num_global_hotkeys = 0;
        fprintf(stderr, "Info: unbinded all hotkeys\n");
    } else {
        fprintf(stderr, "Warning: got invalid command: \"%s\", expected command to start with either \"bind\" or \"unbind_all\"\n", command);
    }
}

static void keyboard_event_process_stdin_command_data(keyboard_event *self, int fd) {
    const int num_bytes_to_read = sizeof(self->stdin_command_data) - self->stdin_command_data_size;
    if(num_bytes_to_read == 0) {
        fprintf(stderr, "Error: failed to read data from stdin, buffer is full. Clearing buffer\n");
        self->stdin_command_data_size = 0;
        return;
    }

    const ssize_t bytes_read = read(fd, self->stdin_command_data + self->stdin_command_data_size, num_bytes_to_read);
    if(bytes_read <= 0)
        return;

    const char *command_start = self->stdin_command_data;
    const char *search = self->stdin_command_data + self->stdin_command_data_size;
    const char *end = search + bytes_read;
    self->stdin_command_data_size += bytes_read;

    for(;;) {
        char *next = memchr(search, '\n', end - search);
        if(!next)
            break;

        *next = '\0';
        keyboard_event_parse_stdin_command(self, command_start, next - command_start);
        search = next + 1;
        command_start = search;
        if(next == end)
            break;
    }

    const int bytes_parsed = command_start - self->stdin_command_data;
    if(bytes_parsed > 0) {
        self->stdin_command_data_size -= bytes_parsed;
        memmove(self->stdin_command_data, command_start, self->stdin_command_data_size);
    }
}

void keyboard_event_poll_events(keyboard_event *self, int timeout_milliseconds) {
    if(poll(self->event_polls, self->num_event_polls, timeout_milliseconds) <= 0)
        return;

    if(self->stdin_failed)
        return;

    for(int i = 0; i < self->num_event_polls; ++i) {
        if(i == self->stdin_event_index && (self->event_polls[i].revents & (POLLHUP|POLLERR)))
            self->stdin_failed = true;

        if(self->event_polls[i].revents & POLLHUP) { /* TODO: What if this is the hotplug fd? */
            keyboard_event_remove_event(self, i);
            --i; /* Repeat same index since the current element has been removed */
            continue;
        }

        if(!(self->event_polls[i].revents & POLLIN))
            continue;

        if(i == self->hotplug_event_index) {
            /* Device is added to end of |event_polls| so it's ok to add while iterating it via index */
            hotplug_event_process_event_data(&self->hotplug_ev, self->event_polls[i].fd, on_device_added_callback, self);
        } else if(i == self->stdin_event_index) {
            keyboard_event_process_stdin_command_data(self, self->event_polls[i].fd);
        } else {
            keyboard_event_process_input_event_data(self, &self->event_extra_data[i], self->event_polls[i].fd);
        }
    }
}

bool keyboard_event_stdin_has_failed(const keyboard_event *self) {
    return self->stdin_failed;
}
