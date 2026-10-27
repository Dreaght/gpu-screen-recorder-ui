#pragma once

#include "Tray.hpp"

#include <optional>
#include <vector>
#include <X11/Xlib.h>

typedef struct _PangoFontMap PangoFontMap;
typedef struct _PangoContext PangoContext;
typedef struct _PangoLayout PangoLayout;

namespace gsr {
    class TrayPopupMenu {
    public:
        TrayPopupMenu(Display *dpy);
        TrayPopupMenu(const TrayPopupMenu&) = delete;
        TrayPopupMenu& operator=(const TrayPopupMenu&) = delete;
        ~TrayPopupMenu();

        bool open(int pointer_x, int pointer_y);
        void handle_event(const XEvent &xev);

        Window get_window() const;
        bool is_closed() const;
        std::optional<TrayAction> get_selected_action() const;
    private:
        bool create_layouts();
        void grab_input();
        void close(std::optional<TrayAction> action);
        void set_hovered_item(int item_index);
        int get_item_at(int x, int y) const;
        void draw();
    private:
        Display *dpy = nullptr;
        Window window = None;
        bool input_grabbed = false;
        bool closed = false;
        std::optional<TrayAction> selected_action;

        PangoFontMap *font_map = nullptr;
        PangoContext *pango_context = nullptr;
        std::vector<PangoLayout*> layouts;

        int width = 0;
        int height = 0;
        int border_size = 0;
        int padding_x = 0;
        int item_height = 0;
        std::vector<int> text_offsets_y;
        int hovered_item = -1;
    };
}
