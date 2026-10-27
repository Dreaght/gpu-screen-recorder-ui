#include "../../include/Tray/TrayPopupMenu.hpp"
#include "../../include/Theme.hpp"
#include "../../include/Translation.hpp"
#include "../../include/WindowUtils.hpp"

#include <mglpp/graphics/Text.hpp>

#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <pango/pangoft2.h>

#include <algorithm>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace gsr {
    static constexpr double default_dpi = 96.0;
    static constexpr int font_size_points = 10;

    struct PixelColor {
        float r;
        float g;
        float b;
    };

    static PixelColor to_pixel_color(mgl::Color color) {
        return { (float)color.r, (float)color.g, (float)color.b };
    }

    static PixelColor blend(PixelColor dst, PixelColor src, float alpha) {
        return { dst.r + (src.r - dst.r) * alpha, dst.g + (src.g - dst.g) * alpha, dst.b + (src.b - dst.b) * alpha };
    }

    static unsigned long scale_channel_to_mask(float value, unsigned long mask) {
        if(mask == 0)
            return 0;

        const int shift = __builtin_ctzl(mask);
        const unsigned long max_value = mask >> shift;
        const unsigned long channel = (unsigned long)lroundf(std::clamp(value, 0.0f, 255.0f) * (float)max_value / 255.0f);
        return channel << shift;
    }

    static unsigned long pixel_color_to_pixel(const Visual *visual, PixelColor color) {
        return scale_channel_to_mask(color.r, visual->red_mask) | scale_channel_to_mask(color.g, visual->green_mask) | scale_channel_to_mask(color.b, visual->blue_mask);
    }

    static double get_xft_dpi(Display *dpy) {
        const char *resource_string = XResourceManagerString(dpy);
        if(!resource_string)
            return default_dpi;

        XrmInitialize();
        XrmDatabase database = XrmGetStringDatabase(resource_string);
        if(!database)
            return default_dpi;

        double dpi = default_dpi;
        char *type = nullptr;
        XrmValue value;
        if(XrmGetResource(database, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr) {
            const double resource_dpi = atof(value.addr);
            if(resource_dpi > 0.0)
                dpi = resource_dpi;
        }

        XrmDestroyDatabase(database);
        return dpi;
    }

    static Monitor get_monitor_at(Display *dpy, int x, int y) {
        for(const Monitor &monitor : get_monitors(dpy)) {
            if(x >= monitor.position.x && x < monitor.position.x + monitor.size.x && y >= monitor.position.y && y < monitor.position.y + monitor.size.y)
                return monitor;
        }
        return { mgl::vec2i(0, 0), mgl::vec2i(DisplayWidth(dpy, DefaultScreen(dpy)), DisplayHeight(dpy, DefaultScreen(dpy))), "" };
    }

    static int get_popup_position(int pointer_pos, int popup_size, int monitor_pos, int monitor_size) {
        int pos = pointer_pos + 1;
        if(pos + popup_size > monitor_pos + monitor_size)
            pos = pointer_pos - popup_size - 1;
        return std::max(monitor_pos, std::min(pos, monitor_pos + monitor_size - popup_size));
    }

    TrayPopupMenu::TrayPopupMenu(Display *dpy) : dpy(dpy) {}

    TrayPopupMenu::~TrayPopupMenu() {
        if(input_grabbed) {
            XUngrabPointer(dpy, CurrentTime);
            XUngrabKeyboard(dpy, CurrentTime);
        }

        if(window)
            XDestroyWindow(dpy, window);

        for(PangoLayout *layout : layouts) {
            g_object_unref(layout);
        }

        if(pango_context)
            g_object_unref(pango_context);

        if(font_map)
            g_object_unref(font_map);

        XFlush(dpy);
    }

    bool TrayPopupMenu::open(int pointer_x, int pointer_y) {
        if(!create_layouts())
            return false;

        const Monitor monitor = get_monitor_at(dpy, pointer_x, pointer_y);
        const int x = get_popup_position(pointer_x, width, monitor.position.x, monitor.size.x);
        const int y = get_popup_position(pointer_y, height, monitor.position.y, monitor.size.y);

        XSetWindowAttributes window_attr = {};
        window_attr.override_redirect = True;
        window_attr.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask;
        window_attr.background_pixel = pixel_color_to_pixel(DefaultVisual(dpy, DefaultScreen(dpy)), to_pixel_color(get_color_theme().page_bg_color));
        window = XCreateWindow(dpy, DefaultRootWindow(dpy), x, y, width, height, 0, CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWEventMask | CWBackPixel, &window_attr);
        if(!window) {
            fprintf(stderr, "Error: TrayPopupMenu::open: failed to create window\n");
            return false;
        }

        const Atom window_type_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
        const Atom window_type_popup_menu_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
        XChangeProperty(dpy, window, window_type_atom, XA_ATOM, 32, PropModeReplace, (const unsigned char*)&window_type_popup_menu_atom, 1);

        XMapRaised(dpy, window);
        XFlush(dpy);
        return true;
    }

    void TrayPopupMenu::handle_event(const XEvent &xev) {
        if(closed)
            return;

        switch(xev.type) {
            case MapNotify: {
                grab_input();
                break;
            }
            case Expose: {
                if(xev.xexpose.count == 0)
                    draw();
                break;
            }
            case MotionNotify: {
                set_hovered_item(get_item_at(xev.xmotion.x, xev.xmotion.y));
                break;
            }
            case ButtonPress: {
                const bool inside = xev.xbutton.x >= 0 && xev.xbutton.x < width && xev.xbutton.y >= 0 && xev.xbutton.y < height;
                if(!inside)
                    close(std::nullopt);
                break;
            }
            case ButtonRelease: {
                const int item_index = get_item_at(xev.xbutton.x, xev.xbutton.y);
                if(item_index != -1)
                    close(tray_menu_items[item_index].action);
                break;
            }
            case KeyPress: {
                XKeyEvent key_event = xev.xkey;
                const KeySym keysym = XLookupKeysym(&key_event, 0);
                const int num_items = (int)layouts.size();
                if(keysym == XK_Escape) {
                    close(std::nullopt);
                } else if(keysym == XK_Down) {
                    set_hovered_item(hovered_item == -1 ? 0 : (hovered_item + 1) % num_items);
                } else if(keysym == XK_Up) {
                    set_hovered_item(hovered_item == -1 ? num_items - 1 : (hovered_item - 1 + num_items) % num_items);
                } else if((keysym == XK_Return || keysym == XK_KP_Enter) && hovered_item != -1) {
                    close(tray_menu_items[hovered_item].action);
                }
                break;
            }
        }
    }

    Window TrayPopupMenu::get_window() const {
        return window;
    }

    bool TrayPopupMenu::is_closed() const {
        return closed;
    }

    std::optional<TrayAction> TrayPopupMenu::get_selected_action() const {
        return selected_action;
    }

    bool TrayPopupMenu::create_layouts() {
        const double dpi = get_xft_dpi(dpy);
        const double scale = dpi / default_dpi;

        font_map = pango_ft2_font_map_new();
        if(!font_map) {
            fprintf(stderr, "Error: TrayPopupMenu: failed to create font map\n");
            return false;
        }
        pango_ft2_font_map_set_resolution(PANGO_FT2_FONT_MAP(font_map), dpi, dpi);

        pango_context = pango_font_map_create_context(font_map);
        if(!pango_context) {
            fprintf(stderr, "Error: TrayPopupMenu: failed to create pango context\n");
            return false;
        }

        std::string font_name = mgl::Text::get_default_font_name();
        if(font_name.empty())
            font_name = "Sans";
        PangoFontDescription *font_description = pango_font_description_from_string((font_name + " " + std::to_string(font_size_points)).c_str());

        int max_text_width = 0;
        int max_text_height = 0;
        std::vector<PangoRectangle> logical_rects;
        for(const TrayMenuItem &menu_item : tray_menu_items) {
            PangoLayout *layout = pango_layout_new(pango_context);
            pango_layout_set_font_description(layout, font_description);
            pango_layout_set_text(layout, TR(menu_item.label), -1);
            layouts.push_back(layout);

            PangoRectangle logical_rect;
            pango_layout_get_pixel_extents(layout, nullptr, &logical_rect);
            logical_rects.push_back(logical_rect);
            max_text_width = std::max(max_text_width, logical_rect.width);
            max_text_height = std::max(max_text_height, logical_rect.height);
        }
        pango_font_description_free(font_description);

        border_size = std::max(1, (int)lround(scale));
        padding_x = (int)lround(12.0 * scale);
        const int padding_y = (int)lround(6.0 * scale);
        item_height = max_text_height + padding_y * 2;
        width = std::max(max_text_width + padding_x * 2, (int)lround(120.0 * scale)) + border_size * 2;
        height = item_height * (int)layouts.size() + border_size * 2;

        for(const PangoRectangle &logical_rect : logical_rects) {
            text_offsets_y.push_back((item_height - logical_rect.height) / 2 - logical_rect.y);
        }
        return true;
    }

    void TrayPopupMenu::grab_input() {
        if(input_grabbed)
            return;

        const unsigned int pointer_event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
        if(XGrabPointer(dpy, window, False, pointer_event_mask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess) {
            fprintf(stderr, "Warning: TrayPopupMenu: failed to grab the pointer, closing the menu\n");
            close(std::nullopt);
            return;
        }

        if(XGrabKeyboard(dpy, window, False, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
            fprintf(stderr, "Warning: TrayPopupMenu: failed to grab the keyboard\n");

        input_grabbed = true;
    }

    void TrayPopupMenu::close(std::optional<TrayAction> action) {
        closed = true;
        selected_action = action;
    }

    void TrayPopupMenu::set_hovered_item(int item_index) {
        if(item_index == hovered_item)
            return;

        hovered_item = item_index;
        draw();
    }

    int TrayPopupMenu::get_item_at(int x, int y) const {
        if(x < border_size || x >= width - border_size || y < border_size || y >= height - border_size)
            return -1;

        const int item_index = (y - border_size) / item_height;
        return item_index < (int)layouts.size() ? item_index : -1;
    }

    void TrayPopupMenu::draw() {
        if(!window || closed)
            return;

        const ColorTheme &color_theme = get_color_theme();
        const PixelColor background_color = to_pixel_color(color_theme.page_bg_color);
        const PixelColor hover_color = to_pixel_color(color_theme.tint_color);
        const PixelColor text_color = to_pixel_color(color_theme.text_color);
        const PixelColor border_color = blend(background_color, text_color, 0.15f);

        std::vector<unsigned char> text_alpha((size_t)width * (size_t)height, 0);
        FT_Bitmap bitmap;
        memset(&bitmap, 0, sizeof(bitmap));
        bitmap.rows = height;
        bitmap.width = width;
        bitmap.pitch = width;
        bitmap.buffer = text_alpha.data();
        bitmap.num_grays = 256;
        bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
        for(size_t i = 0; i < layouts.size(); ++i) {
            pango_ft2_render_layout(&bitmap, layouts[i], border_size + padding_x, border_size + (int)i * item_height + text_offsets_y[i]);
        }

        Visual *visual = DefaultVisual(dpy, DefaultScreen(dpy));
        XImage *image = XCreateImage(dpy, visual, DefaultDepth(dpy, DefaultScreen(dpy)), ZPixmap, 0, nullptr, width, height, 32, 0);
        if(!image)
            return;

        image->data = (char*)malloc((size_t)image->bytes_per_line * (size_t)height);
        if(!image->data) {
            XDestroyImage(image);
            return;
        }

        for(int y = 0; y < height; ++y) {
            const bool on_border_y = y < border_size || y >= height - border_size;
            const int item_index = on_border_y ? -1 : (y - border_size) / item_height;
            for(int x = 0; x < width; ++x) {
                PixelColor color;
                if(on_border_y || x < border_size || x >= width - border_size)
                    color = border_color;
                else if(item_index == hovered_item)
                    color = hover_color;
                else
                    color = background_color;

                const unsigned char alpha = text_alpha[(size_t)y * width + x];
                if(alpha > 0)
                    color = blend(color, text_color, alpha / 255.0f);

                XPutPixel(image, x, y, pixel_color_to_pixel(visual, color));
            }
        }

        const GC gc = XCreateGC(dpy, window, 0, nullptr);
        XPutImage(dpy, window, gc, image, 0, 0, 0, 0, width, height);
        XFreeGC(dpy, gc);
        XDestroyImage(image);
        XFlush(dpy);
    }
}
