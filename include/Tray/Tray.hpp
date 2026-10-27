#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <stdint.h>

namespace gsr {
    class TraySni;
    class TrayXEmbed;

    enum class TrayIcon {
        IDLE,
        RECORDING,
        PAUSED
    };

    enum class TrayAction {
        SHOW_UI,
        QUIT
    };

    struct TrayMenuItem {
        int32_t id;
        const char *label;
        TrayAction action;
    };

    inline constexpr TrayMenuItem tray_menu_items[] = {
        { 1, "Show the UI", TrayAction::SHOW_UI },
        { 2, "Quit",        TrayAction::QUIT    },
    };

    struct TrayPixmap {
        int32_t width = 0;
        int32_t height = 0;
        std::vector<unsigned char> argb_data;
    };

    class Tray {
    public:
        Tray();
        Tray(const Tray&) = delete;
        Tray& operator=(const Tray&) = delete;
        ~Tray();

        bool init(const std::string &resources_path);
        void poll();
        void set_icon(TrayIcon icon);

        std::function<void()> on_show_ui;
        std::function<void()> on_quit;
    private:
        void update_backend();
        void run_action(TrayAction action);
        const TrayPixmap* get_current_pixmap() const;
    private:
        std::unique_ptr<TraySni> sni;
        std::unique_ptr<TrayXEmbed> xembed;
        bool xembed_failed = false;
        TrayPixmap pixmaps[3];
        TrayIcon icon = TrayIcon::IDLE;
    };
}
