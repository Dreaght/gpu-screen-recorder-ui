#include "keyboard_event.h"

/* C stdlib */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

/* POSIX */
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/poll.h>

/* LINUX */
#include <linux/input.h>

/*
    We could get initial keyboard state with:
    unsigned char key_states[KEY_MAX/8 + 1];
    ioctl(fd, EVIOCGKEY(sizeof(key_states)), key_states), but ignore that for now
*/

static void keyboard_event_process_input_event_data(keyboard_event *self, int fd, key_callback callback, void *userdata) {
    struct input_event event;
    if(read(fd, &event, sizeof(event)) != sizeof(event)) {
        fprintf(stderr, "Error: failed to read input event data\n");
        return;
    }

    // value = 1 == key pressed
    //if(event.type == EV_KEY && event.code == KEY_A && event.value == 1) {
        //fprintf(stderr, "fd: %d, type: %d, pressed %d, value: %d\n", fd, event.type, event.code, event.value);
    //}

    if(event.type == EV_KEY) {
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

                callback(event.code, modifiers, event.value, userdata);
                break;
            }
        }
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
        if(self->dev_input_ids[i] == dev_input_id)
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
    if(evbit & (1 << EV_KEY)) {
        unsigned char key_bits[KEY_MAX/8 + 1] = {0};
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), &key_bits);

        /* Test if device supports KEY_A since not all devices that support EV_KEY are keyboards, for example even a power button is an EV_KEY */
        const int key_test = KEY_A;
        const bool supports_key_events = key_bits[key_test/8] & (1 << (key_test % 8));
        if(supports_key_events) {
            if(self->num_event_polls < MAX_EVENT_POLLS) {
                //fprintf(stderr, "%s (%s) supports key inputs\n", dev_input_filepath, device_name);
                self->event_polls[self->num_event_polls] = (struct pollfd) {
                    .fd = fd,
                    .events = POLLIN,
                    .revents = 0
                };
                self->dev_input_ids[self->num_event_polls] = dev_input_id;

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

    close(self->event_polls[index].fd);
    for(int j = index + 1; j < self->num_event_polls; ++j) {
        self->event_polls[j - 1] = self->event_polls[j];
        self->dev_input_ids[j - 1] = self->dev_input_ids[j];
    }
    --self->num_event_polls;
}

bool keyboard_event_init(keyboard_event *self, bool poll_stdout_error) {
    memset(self, 0, sizeof(*self));
    self->stdout_event_index = -1;
    self->hotplug_event_index = -1;

    if(poll_stdout_error) {
        self->event_polls[self->num_event_polls] = (struct pollfd) {
            .fd = STDOUT_FILENO,
            .events = 0,
            .revents = 0
        };
        self->dev_input_ids[self->num_event_polls] = -1;

        self->stdout_event_index = self->num_event_polls;
        ++self->num_event_polls;
    }

    if(hotplug_event_init(&self->hotplug_ev)) {
        self->event_polls[self->num_event_polls] = (struct pollfd) {
            .fd = hotplug_event_steal_fd(&self->hotplug_ev),
            .events = POLLIN,
            .revents = 0
        };
        self->dev_input_ids[self->num_event_polls] = -1;

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
    for(int i = 0; i < self->num_event_polls; ++i) {
        close(self->event_polls[i].fd);
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
        if(i == self->stdout_event_index && self->event_polls[i].revents & (POLLHUP|POLLERR))
            self->stdout_failed = true;

        if(self->event_polls[i].revents & POLLHUP) { /* TODO: What if this is the hotplug fd? */
            keyboard_event_remove_event(self, i);
            --i; /* Repeat same index since the current element has been removed */
            continue;
        }

        if(!(self->event_polls[i].revents & POLLIN))
            continue;

        if(i == self->hotplug_event_index)
            /* Device is added to end of |event_polls| so it's ok to add while iterating it via index */
            hotplug_event_process_event_data(&self->hotplug_ev, self->event_polls[i].fd, on_device_added_callback, self);
        else
            keyboard_event_process_input_event_data(self, self->event_polls[i].fd, callback, userdata);
    }
}

bool keyboard_event_stdout_has_failed(keyboard_event *self) {
    return self->stdout_failed;
}
