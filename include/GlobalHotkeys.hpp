#pragma once

#include <stdint.h>
#include <functional>
#include <string>

namespace gsr {
    struct Hotkey {
        uint64_t key = 0;
        uint32_t modifiers = 0;
    };

    using GlobalHotkeyCallback = std::function<void(const std::string &id)>;

    class GlobalHotkeys {
    public:
        GlobalHotkeys() = default;
        GlobalHotkeys(const GlobalHotkeys&) = delete;
        GlobalHotkeys& operator=(const GlobalHotkeys&) = delete;
        virtual ~GlobalHotkeys() = default;

        virtual bool bind_key_press(Hotkey hotkey, const std::string &id, GlobalHotkeyCallback callback) = 0;
        virtual void unbind_key_press(const std::string &id) = 0;
        virtual void unbind_all_keys() = 0;
        virtual void poll_events() = 0;
    };
}