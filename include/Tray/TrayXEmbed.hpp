#pragma once

#include "Tray.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <X11/Xlib.h>

namespace gsr {
    class TrayPopupMenu;

    class TrayXEmbed {
    public:
        TrayXEmbed();
        TrayXEmbed(const TrayXEmbed&) = delete;
        TrayXEmbed& operator=(const TrayXEmbed&) = delete;
        ~TrayXEmbed();

        bool init(const TrayPixmap *pixmap);
        void poll();
        void set_pixmap(const TrayPixmap *pixmap);

        std::function<void(TrayAction action)> on_action;
    private:
        void handle_event(XEvent &xev);
        void find_tray_manager();
        void get_tray_visual(Visual **visual, int *depth);
        void dock();
        void destroy_icon_window();
        void draw();
    private:
        Display *dpy = nullptr;
        Atom selection_atom = None;
        Atom opcode_atom = None;
        Atom manager_atom = None;
        Atom visual_atom = None;
        Atom xembed_info_atom = None;

        Window manager_window = None;
        Window icon_window = None;
        Visual *icon_visual = nullptr;
        Colormap icon_colormap = None;
        int icon_width = 0;
        int icon_height = 0;

        const TrayPixmap *pixmap = nullptr;
        std::unique_ptr<TrayPopupMenu> popup_menu;
        std::optional<TrayAction> pending_action;
    };
}
