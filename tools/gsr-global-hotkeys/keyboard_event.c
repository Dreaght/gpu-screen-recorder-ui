#include "keyboard_event.h"

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
#include <sys/poll.h>

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

static void keyboard_event_process_key_state_change(keyboard_event *self, struct input_event event, event_extra_data *extra_data, int fd) {
    if(event.type != EV_KEY)
        return;

    if(!extra_data->key_states || event.code >= KEY_STATES_SIZE * 8)
        return;

    const unsigned int byte_index = event.code / 8;
    const unsigned char bit_index = event.code % 8;
    unsigned char key_byte_state = extra_data->key_states[byte_index];
    const bool prev_key_pressed = (key_byte_state & (1 << bit_index)) != KEY_RELEASE;

    if(event.value == KEY_RELEASE) {
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

static void keyboard_event_process_input_event_data(keyboard_event *self, event_extra_data *extra_data, int fd, key_callback callback, void *userdata) {
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

    if(event.type == EV_KEY) {
        keyboard_event_process_key_state_change(self, event, extra_data, fd);

        switch(event.code) {
            case KEY_LEFTSHIFT:
                self->lshift_button_state = event.value >= 1 ? KEYBOARD_BUTTON_PRESSED : KEYBOARD_BUTTON_RELEASED;
                break;
            case KEY_RIGHTSHIFT:
                self->rshift_button_state = event.value >= 1 ? KEYBOARD_BUTTON_PRESSED : KEYBOARD_BUTTON_RELEASED;
                break;
            case KEY_LEFTCTRL:
                self->lctrl_button_state = event.value >= 1 ? KEYBOARD_BUTTON_PRESSED : KEYBOARD_BUTTON_RELEASED;
                break;
            case KEY_RIGHTCTRL:
                self->rctrl_button_state = event.value >= 1 ? KEYBOARD_BUTTON_PRESSED : KEYBOARD_BUTTON_RELEASED;
                break;
            case KEY_LEFTALT:
                self->lalt_button_state = event.value >= 1 ? KEYBOARD_BUTTON_PRESSED : KEYBOARD_BUTTON_RELEASED;
                break;
            case KEY_RIGHTALT:
                self->ralt_button_state = event.value >= 1 ? KEYBOARD_BUTTON_PRESSED : KEYBOARD_BUTTON_RELEASED;
                break;
            case KEY_LEFTMETA:
                self->lmeta_button_state = event.value >= 1 ? KEYBOARD_BUTTON_PRESSED : KEYBOARD_BUTTON_RELEASED;
                break;
            case KEY_RIGHTMETA:
                self->rmeta_button_state = event.value >= 1 ? KEYBOARD_BUTTON_PRESSED : KEYBOARD_BUTTON_RELEASED;
                break;
            default: {
                const bool shift_pressed = self->lshift_button_state == KEYBOARD_BUTTON_PRESSED || self->rshift_button_state == KEYBOARD_BUTTON_PRESSED;
                const bool ctrl_pressed = self->lctrl_button_state == KEYBOARD_BUTTON_PRESSED || self->rctrl_button_state == KEYBOARD_BUTTON_PRESSED;
                const bool alt_pressed = self->lalt_button_state == KEYBOARD_BUTTON_PRESSED || self->ralt_button_state == KEYBOARD_BUTTON_PRESSED;
                const bool meta_pressed = self->lmeta_button_state == KEYBOARD_BUTTON_PRESSED || self->rmeta_button_state == KEYBOARD_BUTTON_PRESSED;
                //fprintf(stderr, "pressed key: %d, state: %d, shift: %s, ctrl: %s, alt: %s, meta: %s\n", event.code, event.value,
                //   shift_pressed ? "yes" : "no", ctrl_pressed ? "yes" : "no", alt_pressed ? "yes" : "no", meta_pressed ? "yes" : "no");
                uint32_t modifiers = 0;
                if(shift_pressed)
                    modifiers |= KEYBOARD_MODKEY_SHIFT;
                if(ctrl_pressed)
                    modifiers |= KEYBOARD_MODKEY_CTRL;
                if(alt_pressed)
                    modifiers |= KEYBOARD_MODKEY_ALT;
                if(meta_pressed)
                    modifiers |= KEYBOARD_MODKEY_SUPER;

                if(!callback(event.code, modifiers, event.value, userdata))
                    return;

                break;
            }
        }
    }

    if(extra_data->grabbed) {
        /* TODO: What to do on error? */
        if(write(self->uinput_fd, &event, sizeof(event)) != sizeof(event))
            fprintf(stderr, "Error: failed to write event data to virtual keyboard for exclusively grabbed device\n");
    }
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

static bool keyboard_event_try_add_device_if_keyboard(keyboard_event *self, const char *dev_input_filepath) {
    const int dev_input_id = get_dev_input_id_from_filepath(dev_input_filepath);
    if(dev_input_id == -1)
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
    const bool is_keyboard = evbit & (1 << EV_KEY);

    if(is_keyboard && strcmp(device_name, GSR_UI_VIRTUAL_KEYBOARD_NAME) != 0) {
        unsigned char key_bits[KEY_MAX/8 + 1] = {0};
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), &key_bits);

        const bool supports_key_events      = key_bits[KEY_A/8]        & (1 << (KEY_A % 8));
        const bool supports_mouse_events    = key_bits[BTN_MOUSE/8]    & (1 << (BTN_MOUSE % 8));
        //const bool supports_touch_events    = key_bits[BTN_TOUCH/8]    & (1 << (BTN_TOUCH % 8));
        const bool supports_joystick_events = key_bits[BTN_JOYSTICK/8] & (1 << (BTN_JOYSTICK % 8));
        const bool supports_wheel_events    = key_bits[BTN_WHEEL/8]    & (1 << (BTN_WHEEL % 8));
        if(supports_key_events && !supports_mouse_events && !supports_joystick_events && !supports_wheel_events) {
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

                keyboard_event_fetch_update_key_states(self, &self->event_extra_data[self->num_event_polls], fd);

                ++self->num_event_polls;
                return true;
            } else {
                fprintf(stderr, "Warning: the maximum number of keyboard devices have been registered. The newly added keyboard will be ignored\n");
            }
        }
    }

    close(fd);
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

    for(int i = index + 1; i < self->num_event_polls; ++i) {
        self->event_polls[i - 1] = self->event_polls[i];
        free(self->event_extra_data[i - 1].key_states);
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
    for(int i = 1; i < KEY_MAX; ++i) {
        success &= (ioctl(fd, UI_SET_KEYBIT, i) != -1);
    }

    success &= (ioctl(fd, UI_SET_EVBIT, EV_REL) != -1);
    success &= (ioctl(fd, UI_SET_RELBIT, REL_X) != -1);
    success &= (ioctl(fd, UI_SET_RELBIT, REL_Y) != -1);
    success &= (ioctl(fd, UI_SET_RELBIT, REL_Z) != -1);

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

bool keyboard_event_init(keyboard_event *self, bool poll_stdout_error, bool exclusive_grab) {
    memset(self, 0, sizeof(*self));
    self->stdout_event_index = -1;
    self->hotplug_event_index = -1;

    if(exclusive_grab) {
        self->uinput_fd = setup_virtual_keyboard_input(GSR_UI_VIRTUAL_KEYBOARD_NAME);
        if(self->uinput_fd <= 0)
            fprintf(stderr, "Warning: failed to setup virtual keyboard input for exclusive grab. The focused application will receive keys used for global hotkeys\n");
    }

    if(poll_stdout_error) {
        self->event_polls[self->num_event_polls] = (struct pollfd) {
            .fd = STDOUT_FILENO,
            .events = 0,
            .revents = 0
        };

        self->event_extra_data[self->num_event_polls] = (event_extra_data) {
            .dev_input_id = -1,
            .grabbed = false,
            .key_states = NULL,
            .num_keys_pressed = 0
        };

        self->stdout_event_index = self->num_event_polls;
        ++self->num_event_polls;
    }

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
    if(self->uinput_fd > 0) {
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
}

static void on_device_added_callback(const char *devname, void *userdata) {
    keyboard_event *keyboard_ev = userdata;
    char dev_input_filepath[1024];
    snprintf(dev_input_filepath, sizeof(dev_input_filepath), "/dev/%s", devname);
    keyboard_event_try_add_device_if_keyboard(keyboard_ev, dev_input_filepath);
}

void keyboard_event_poll_events(keyboard_event *self, int timeout_milliseconds, key_callback callback, void *userdata) {
    if(poll(self->event_polls, self->num_event_polls, timeout_milliseconds) <= 0)
        return;

    for(int i = 0; i < self->num_event_polls; ++i) {
        if(i == self->stdout_event_index && (self->event_polls[i].revents & (POLLHUP|POLLERR)))
            self->stdout_failed = true;

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
        } else if(i == self->stdout_event_index) {
            /* Do nothing, this shouldn't happen anyways since we dont poll for input */
        } else {
            keyboard_event_process_input_event_data(self, &self->event_extra_data[i], self->event_polls[i].fd, callback, userdata);
        }
    }
}

bool keyboard_event_stdout_has_failed(const keyboard_event *self) {
    return self->stdout_failed;
}
