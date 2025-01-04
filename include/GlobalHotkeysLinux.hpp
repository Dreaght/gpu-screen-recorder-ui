#pragma once

#include "GlobalHotkeys.hpp"
#include <unordered_map>
#include <sys/types.h>

namespace gsr {
    class GlobalHotkeysLinux : public GlobalHotkeys {
    public:
        enum class GrabType {
            ALL,
            VIRTUAL
        };

        GlobalHotkeysLinux(GrabType grab_type);
        GlobalHotkeysLinux(const GlobalHotkeysLinux&) = delete;
        GlobalHotkeysLinux& operator=(const GlobalHotkeysLinux&) = delete;
        ~GlobalHotkeysLinux() override;

        bool start();
        bool bind_action(const std::string &id, GlobalHotkeyCallback callback) override;
        void poll_events() override;
    private:
        pid_t process_id = 0;
        int pipes[2];
        FILE *read_file = nullptr;
        std::unordered_map<std::string, GlobalHotkeyCallback> bound_actions_by_id;
        GrabType grab_type;
    };
}