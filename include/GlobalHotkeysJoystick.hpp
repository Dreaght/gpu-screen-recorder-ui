#pragma once

#include "GlobalHotkeys.hpp"
#include "Hotplug.hpp"
#include <unordered_map>
#include <thread>
#include <poll.h>
#include <mglpp/system/Clock.hpp>
#include <linux/joystick.h>

namespace gsr {
    static constexpr int max_js_poll_fd = 16;

    class GlobalHotkeysJoystick : public GlobalHotkeys {
        class GlobalHotkeysJoystickHotplugDelegate;
    public:
        GlobalHotkeysJoystick() = default;
        GlobalHotkeysJoystick(const GlobalHotkeysJoystick&) = delete;
        GlobalHotkeysJoystick& operator=(const GlobalHotkeysJoystick&) = delete;
        ~GlobalHotkeysJoystick() override;

        bool start();
        bool bind_action(const std::string &id, GlobalHotkeyCallback callback) override;
        void poll_events() override;
    private:
        void read_events();
        void process_js_event(int fd, js_event &event);
        bool add_device(const char *dev_input_filepath, bool print_error = true);
        bool remove_device(const char *dev_input_filepath);
        bool remove_poll_fd(int index);
        // Returns -1 if not found
        int get_poll_fd_index_by_dev_input_id(int dev_input_id) const;
    private:
        struct ExtraData {
            int dev_input_id = 0;
        };

        std::unordered_map<std::string, GlobalHotkeyCallback> bound_actions_by_id;
        std::thread read_thread;

        pollfd poll_fd[max_js_poll_fd];
        ExtraData extra_data[max_js_poll_fd];
        int num_poll_fd = 0;
        int event_fd = -1;
        int event_index = -1;

        mgl::Clock double_click_clock;
        int num_times_clicked = 0;
        bool save_replay = false;
        int hotplug_poll_index = -1;
        Hotplug hotplug;
    };
}