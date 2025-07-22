#include "../../include/GlobalHotkeys/GlobalHotkeysJoystick.hpp"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/eventfd.h>

namespace gsr {
    static constexpr int button_pressed = 1;
    static constexpr int cross_button = 0;
    static constexpr int triangle_button = 2;
    static constexpr int options_button = 9;
    static constexpr int playstation_button = 10;
    static constexpr int l3_button = 11;
    static constexpr int r3_button = 12;
    static constexpr int axis_up_down = 7;
    static constexpr int axis_left_right = 6;

    struct DeviceId {
        uint16_t vendor;
        uint16_t product;
    };

    static bool read_file_hex_number(const char *path, unsigned int *value) {
        *value = 0;
        FILE *f = fopen(path, "rb");
        if(!f)
            return false;

        fscanf(f, "%x", value);
        fclose(f);
        return true;
    }

    static DeviceId joystick_get_device_id(const char *path) {
        DeviceId device_id;
        device_id.vendor = 0;
        device_id.product = 0;

        const char *js_path_id = nullptr;
        const int len = strlen(path);
        for(int i = len - 1; i >= 0; --i) {
            if(path[i] == '/') {
                js_path_id = path + i + 1;
                break;
            }
        }

        if(!js_path_id)
            return device_id;

        unsigned int vendor = 0;
        unsigned int product = 0;
        char path_buf[1024];

        snprintf(path_buf, sizeof(path_buf), "/sys/class/input/%s/device/id/vendor", js_path_id);
        if(!read_file_hex_number(path_buf, &vendor))
            return device_id;

        snprintf(path_buf, sizeof(path_buf), "/sys/class/input/%s/device/id/product", js_path_id);
        if(!read_file_hex_number(path_buf, &product))
            return device_id;

        device_id.vendor = vendor;
        device_id.product = product;
        return device_id;
    }

    static bool is_ps4_controller(DeviceId device_id) {
        return device_id.vendor == 0x054C && (device_id.product == 0x09CC || device_id.product == 0x0BA0 || device_id.product == 0x05C4);
    }

    static bool is_ps5_controller(DeviceId device_id) {
        return device_id.vendor == 0x054C && (device_id.product == 0x0DF2 || device_id.product == 0x0CE6);
    }

    static bool is_stadia_controller(DeviceId device_id) {
        return device_id.vendor == 0x18D1 && (device_id.product == 0x9400);
    }

    // Returns -1 on error
    static int get_js_dev_input_id_from_filepath(const char *dev_input_filepath) {
        if(strncmp(dev_input_filepath, "/dev/input/js", 13) != 0)
            return -1;

        int dev_input_id = -1;
        if(sscanf(dev_input_filepath + 13, "%d", &dev_input_id) == 1)
            return dev_input_id;
        return -1;
    }

    GlobalHotkeysJoystick::~GlobalHotkeysJoystick() {
        if(event_fd > 0) {
            const uint64_t exit = 1;
            write(event_fd, &exit, sizeof(exit));
        }

        if(read_thread.joinable())
            read_thread.join();

        if(event_fd > 0)
            close(event_fd);

        for(int i = 0; i < num_poll_fd; ++i) {
            if(poll_fd[i].fd > 0)
                close(poll_fd[i].fd);
        }
    }

    bool GlobalHotkeysJoystick::start() {
        if(num_poll_fd > 0)
            return false;

        event_fd = eventfd(0, 0);
        if(event_fd <= 0)
            return false;

        event_index = num_poll_fd;
        poll_fd[num_poll_fd] = {
            event_fd,
            POLLIN,
            0
        };
        extra_data[num_poll_fd] = {
            -1
        };
        ++num_poll_fd;

        if(!hotplug.start()) {
            fprintf(stderr, "Warning: failed to setup hotplugging\n");
        } else {
            hotplug_poll_index = num_poll_fd;
            poll_fd[num_poll_fd] = {
                hotplug.steal_fd(),
                POLLIN,
                0
            };
            extra_data[num_poll_fd] = {
                -1
            };
            ++num_poll_fd;
        }

        char dev_input_path[128];
        for(int i = 0; i < 8; ++i) {
            snprintf(dev_input_path, sizeof(dev_input_path), "/dev/input/js%d", i);
            add_device(dev_input_path, false);
        }

        if(num_poll_fd == 0)
            fprintf(stderr, "Info: no joysticks found, assuming they might be connected later\n");

        read_thread = std::thread(&GlobalHotkeysJoystick::read_events, this);
        return true;
    }

    bool GlobalHotkeysJoystick::bind_action(const std::string &id, GlobalHotkeyCallback callback) {
        if(num_poll_fd == 0)
            return false;
        return bound_actions_by_id.insert(std::make_pair(id, std::move(callback))).second;
    }

    void GlobalHotkeysJoystick::poll_events() {
        if(num_poll_fd == 0)
            return;

        if(save_replay) {
            save_replay = false;
            auto it = bound_actions_by_id.find("save_replay");
            if(it != bound_actions_by_id.end())
                it->second("save_replay");
        }

        if(save_1_min_replay) {
            save_1_min_replay = false;
            auto it = bound_actions_by_id.find("save_1_min_replay");
            if(it != bound_actions_by_id.end())
                it->second("save_1_min_replay");
        }

        if(save_10_min_replay) {
            save_10_min_replay = false;
            auto it = bound_actions_by_id.find("save_10_min_replay");
            if(it != bound_actions_by_id.end())
                it->second("save_10_min_replay");
        }

        if(take_screenshot) {
            take_screenshot = false;
            auto it = bound_actions_by_id.find("take_screenshot");
            if(it != bound_actions_by_id.end())
                it->second("take_screenshot");
        }

        if(toggle_record) {
            toggle_record = false;
            auto it = bound_actions_by_id.find("toggle_record");
            if(it != bound_actions_by_id.end())
                it->second("toggle_record");
        }

        if(toggle_replay) {
            toggle_replay = false;
            auto it = bound_actions_by_id.find("toggle_replay");
            if(it != bound_actions_by_id.end())
                it->second("toggle_replay");
        }

        if(toggle_show) {
            toggle_show = false;
            auto it = bound_actions_by_id.find("toggle_show");
            if(it != bound_actions_by_id.end())
                it->second("toggle_show");
        }
    }

    void GlobalHotkeysJoystick::read_events() {
        js_event event;
        while(poll(poll_fd, num_poll_fd, -1) > 0) {
            for(int i = 0; i < num_poll_fd; ++i) {
                if(poll_fd[i].revents & (POLLHUP|POLLERR|POLLNVAL)) {
                    if(i == event_index)
                        goto done;

                    char dev_input_filepath[256];
                    snprintf(dev_input_filepath, sizeof(dev_input_filepath), "/dev/input/js%d", extra_data[i].dev_input_id);
                    fprintf(stderr, "Info: removed joystick: %s\n", dev_input_filepath);
                    if(remove_poll_fd(i))
                        --i; // This item was removed so we want to repeat the same index to continue to the next item

                    continue;
                }

                if(!(poll_fd[i].revents & POLLIN))
                    continue;

                if(i == event_index) {
                    goto done;
                } else if(i == hotplug_poll_index) {
                    hotplug.process_event_data(poll_fd[i].fd, [&](HotplugAction hotplug_action, const char *devname) {
                        switch(hotplug_action) {
                            case HotplugAction::ADD: {
                                add_device(devname);
                                break;
                            }
                            case HotplugAction::REMOVE: {
                                if(remove_device(devname))
                                    --i; // This item was removed so we want to repeat the same index to continue to the next item
                                break;
                            }
                        }
                    });
                } else {
                    process_js_event(poll_fd[i].fd, event);
                }
            }
        }

        done:
        ;
    }

    void GlobalHotkeysJoystick::process_js_event(int fd, js_event &event) {
        if(read(fd, &event, sizeof(event)) != sizeof(event))
            return;

        if((event.type & JS_EVENT_BUTTON) == JS_EVENT_BUTTON) {
            switch(event.number) {
                case playstation_button: {
                    // Workaround weird steam input (in-game) behavior where steam triggers playstation button + options when pressing both l3 and r3 at the same time
                    playstation_button_pressed = (event.value == button_pressed) && !l3_button_pressed && !r3_button_pressed;
                    break;
                }
                case options_button: {
                    if(playstation_button_pressed && event.value == button_pressed)
                        toggle_show = true;
                    break;
                }
                case cross_button: {
                    if(playstation_button_pressed && event.value == button_pressed)
                        save_1_min_replay = true;
                    break;
                }
                case triangle_button: {
                    if(playstation_button_pressed && event.value == button_pressed)
                        save_10_min_replay = true;
                    break;
                }
                case l3_button: {
                    l3_button_pressed = event.value == button_pressed;
                    break;
                }
                case r3_button: {
                    r3_button_pressed = event.value == button_pressed;
                    break;
                }
            }
        } else if((event.type & JS_EVENT_AXIS) == JS_EVENT_AXIS && playstation_button_pressed) {
            const int trigger_threshold = 16383;
            const bool prev_up_pressed = up_pressed;
            const bool prev_down_pressed = down_pressed;
            const bool prev_left_pressed = left_pressed;
            const bool prev_right_pressed = right_pressed;

            if(event.number == axis_up_down) {
                up_pressed = event.value <= -trigger_threshold;
                down_pressed = event.value >= trigger_threshold;
            } else if(event.number == axis_left_right) {
                left_pressed = event.value <= -trigger_threshold;
                right_pressed = event.value >= trigger_threshold;
            }

            if(up_pressed && !prev_up_pressed)
                take_screenshot = true;
            else if(down_pressed && !prev_down_pressed)
                save_replay = true;
            else if(left_pressed && !prev_left_pressed)
                toggle_record = true;
            else if(right_pressed && !prev_right_pressed)
                toggle_replay = true;
        }
    }

    bool GlobalHotkeysJoystick::add_device(const char *dev_input_filepath, bool print_error) {
        if(num_poll_fd >= max_js_poll_fd) {
            fprintf(stderr, "Warning: failed to add joystick device %s, too many joysticks have been added\n", dev_input_filepath);
            return false;
        }

        const int dev_input_id = get_js_dev_input_id_from_filepath(dev_input_filepath);
        if(dev_input_id == -1)
            return false;

        const int fd = open(dev_input_filepath, O_RDONLY);
        if(fd <= 0) {
            if(print_error)
                fprintf(stderr, "Error: failed to add joystick %s, error: %s\n", dev_input_filepath, strerror(errno));
            return false;
        }

        poll_fd[num_poll_fd] = {
            fd,
            POLLIN,
            0
        };

        extra_data[num_poll_fd] = {
            dev_input_id
        };

        //const DeviceId device_id = joystick_get_device_id(dev_input_filepath);

        ++num_poll_fd;
        fprintf(stderr, "Info: added joystick: %s\n", dev_input_filepath);
        return true;
    }

    bool GlobalHotkeysJoystick::remove_device(const char *dev_input_filepath) {
        const int dev_input_id = get_js_dev_input_id_from_filepath(dev_input_filepath);
        if(dev_input_id == -1)
            return false;

        const int poll_fd_index = get_poll_fd_index_by_dev_input_id(dev_input_id);
        if(poll_fd_index == -1)
            return false;

        fprintf(stderr, "Info: removed joystick: %s\n", dev_input_filepath);
        return remove_poll_fd(poll_fd_index);
    }

    bool GlobalHotkeysJoystick::remove_poll_fd(int index) {
        if(index < 0 || index >= num_poll_fd)
            return false;

        if(poll_fd[index].fd > 0)
            close(poll_fd[index].fd);

        for(int i = index + 1; i < num_poll_fd; ++i) {
            poll_fd[i - 1] = poll_fd[i];
            extra_data[i - 1] = extra_data[i];
        }
        --num_poll_fd;
        return true;
    }

    int GlobalHotkeysJoystick::get_poll_fd_index_by_dev_input_id(int dev_input_id) const {
        for(int i = 0; i < num_poll_fd; ++i) {
            if(dev_input_id == extra_data[i].dev_input_id)
                return i;
        }
        return -1;
    }
}
