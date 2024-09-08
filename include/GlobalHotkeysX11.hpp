#pragma once

#include "GlobalHotkeys.hpp"
#include <unordered_map>
#include <X11/Xlib.h>

namespace gsr {
    class GlobalHotkeysX11 : public GlobalHotkeys {
    public:
        GlobalHotkeysX11();
        GlobalHotkeysX11(const GlobalHotkeysX11&) = delete;
        GlobalHotkeysX11& operator=(const GlobalHotkeysX11&) = delete;
        ~GlobalHotkeysX11() override;

        bool bind_key_press(Hotkey hotkey, const std::string &id, GlobalHotkeyCallback callback) override;
        void unbind_key_press(const std::string &id) override;
        void unbind_all_keys() override;
        void poll_events() override;
    private:
        void call_hotkey_callback(Hotkey hotkey) const;
    private:
        struct HotkeyData {
            Hotkey hotkey;
            GlobalHotkeyCallback callback;
        };

        Display *dpy = nullptr;
        XEvent xev;
        std::unordered_map<std::string, HotkeyData> bound_keys_by_id;
    };
}