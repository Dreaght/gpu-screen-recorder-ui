#include "../include/WindowUtils.hpp"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <mglpp/system/Utf8.hpp>

extern "C" {
#include <mgl/window/window.h>
}

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>

#include <optional>

#define MAX_PROPERTY_VALUE_LEN 4096

namespace gsr {
    static unsigned char* window_get_property(Display *dpy, Window window, Atom property_type, const char *property_name, unsigned int *property_size) {
        Atom ret_property_type = None;
        int ret_format = 0;
        unsigned long num_items = 0;
        unsigned long num_remaining_bytes = 0;
        unsigned char *data = nullptr;
        const Atom atom = XInternAtom(dpy, property_name, False);
        if(XGetWindowProperty(dpy, window, atom, 0, MAX_PROPERTY_VALUE_LEN / 4, False, property_type, &ret_property_type, &ret_format, &num_items, &num_remaining_bytes, &data) != Success || !data) {
            return nullptr;
        }

        if(ret_property_type != property_type) {
            XFree(data);
            return nullptr;
        }

        *property_size = (ret_format / (32 / sizeof(long))) * num_items;
        return data;
    }

    static bool window_has_atom(Display *dpy, Window window, Atom atom) {
        Atom type;
        unsigned long len, bytes_left;
        int format;
        unsigned char *properties = NULL;
        if(XGetWindowProperty(dpy, window, atom, 0, 1024, False, AnyPropertyType, &type, &format, &len, &bytes_left, &properties) < Success)
            return false;

        if(properties)
            XFree(properties);

        return type != None;
    }

    static bool window_is_user_program(Display *dpy, Window window) {
        const Atom net_wm_state_atom = XInternAtom(dpy, "_NET_WM_STATE", False);
        const Atom wm_state_atom = XInternAtom(dpy, "WM_STATE", False);
        return window_has_atom(dpy, window, net_wm_state_atom) || window_has_atom(dpy, window, wm_state_atom);
    }

    static Window window_get_target_window_child(Display *display, Window window) {
        if(window == None)
            return None;

        if(window_is_user_program(display, window))
            return window;

        Window root;
        Window parent;
        Window *children = nullptr;
        unsigned int num_children = 0;
        if(!XQueryTree(display, window, &root, &parent, &children, &num_children) || !children)
            return None;

        Window found_window = None;
        for(int i = num_children - 1; i >= 0; --i) {
            if(children[i] && window_is_user_program(display, children[i])) {
                found_window = children[i];
                goto finished;
            }
        }

        for(int i = num_children - 1; i >= 0; --i) {
            if(children[i]) {
                Window win = window_get_target_window_child(display, children[i]);
                if(win) {
                    found_window = win;
                    goto finished;
                }
            }
        }

        finished:
        XFree(children);
        return found_window;
    }

    mgl::vec2i get_cursor_position(Display *dpy, Window *window) {
        Window root_window = None;
        *window = None;
        int dummy_i;
        unsigned int dummy_u;
        mgl::vec2i root_pos;
        XQueryPointer(dpy, DefaultRootWindow(dpy), &root_window, window, &root_pos.x, &root_pos.y, &dummy_i, &dummy_i, &dummy_u);
        *window = window_get_target_window_child(dpy, *window);
        return root_pos;
    }

    Window get_focused_window(Display *dpy, WindowCaptureType cap_type) {
        //const Atom net_active_window_atom = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
        Window focused_window = None;

        if(cap_type == WindowCaptureType::FOCUSED) {
            // Atom type = None;
            // int format = 0;
            // unsigned long num_items = 0;
            // unsigned long bytes_left = 0;
            // unsigned char *data = NULL;
            // XGetWindowProperty(dpy, DefaultRootWindow(dpy), net_active_window_atom, 0, 1, False, XA_WINDOW, &type, &format, &num_items, &bytes_left, &data);

            // if(type == XA_WINDOW && num_items == 1 && data)
            //     focused_window = *(Window*)data;

            // if(data)
            //     XFree(data);

            // if(focused_window)
            //     return focused_window;

            int revert_to = 0;
            XGetInputFocus(dpy, &focused_window, &revert_to);
            if(focused_window && focused_window != DefaultRootWindow(dpy) && window_is_user_program(dpy, focused_window))
                return focused_window;
        }

        get_cursor_position(dpy, &focused_window);
        if(focused_window && focused_window != DefaultRootWindow(dpy))
            return focused_window;

        return None;
    }

    static std::string utf8_sanitize(const uint8_t *str, int size) {
        const uint32_t zero_width_space_codepoint = 0x200b; // Some games such as the finals has zero-width space characters
        std::string result;
        for(int i = 0; i < size;) {
            // Some games such as the finals has utf8-bom between each character, wtf?
            if(i + 3 < size && memcmp(str + i, "\xEF\xBB\xBF", 3) == 0) {
                i += 3;
                continue;
            }

            uint32_t codepoint = 0;
            size_t codepoint_length = 1;
            if(mgl::utf8_decode(str + i, size - i, &codepoint, &codepoint_length) && codepoint != zero_width_space_codepoint)
                result.append((const char*)str + i, codepoint_length);
            i += codepoint_length;
        }
        return result;
    }

    static std::optional<std::string> get_window_title(Display *dpy, Window window) {
        const Atom net_wm_name_atom = XInternAtom(dpy, "_NET_WM_NAME", False);
        const Atom wm_name_atom = XInternAtom(dpy, "WM_NAME", False);
        const Atom utf8_string_atom = XInternAtom(dpy, "UTF8_STRING", False);

        Atom type = None;
        int format = 0;
        unsigned long num_items = 0;
        unsigned long bytes_left = 0;
        unsigned char *data = NULL;
        XGetWindowProperty(dpy, window, net_wm_name_atom, 0, 1024, False, utf8_string_atom, &type, &format, &num_items, &bytes_left, &data);

        if(type == utf8_string_atom && format == 8 && data)
            return utf8_sanitize(data, num_items);

        type = None;
        format = 0;
        num_items = 0;
        bytes_left = 0;
        data = NULL;
        XGetWindowProperty(dpy, window, wm_name_atom, 0, 1024, False, 0, &type, &format, &num_items, &bytes_left, &data);

        if((type == XA_STRING || type == utf8_string_atom) && data)
            return utf8_sanitize(data, num_items);

        return std::nullopt;
    }

    static std::string strip(const std::string &str) {
        int start_index = 0;
        int str_len = str.size();

        for(int i = 0; i < str_len; ++i) {
            if(str[i] != ' ') {
                start_index += i;
                str_len -= i;
                break;
            }
        }

        for(int i = str_len - 1; i >= 0; --i) {
            if(str[i] != ' ') {
                str_len = i + 1;
                break;
            }
        }

        return str.substr(start_index, str_len);
    }

    std::string get_focused_window_name(Display *dpy, WindowCaptureType window_capture_type) {
        std::string result;
        const Window focused_window = get_focused_window(dpy, window_capture_type);
        if(focused_window == None)
            return result;

        // Window title is not always ideal (for example for a browser), but for games its pretty much required
        const std::optional<std::string> window_title = get_window_title(dpy, focused_window);
        if(window_title) {
            result = strip(window_title.value());
            return result;
        }

        XClassHint class_hint = {nullptr, nullptr};
        XGetClassHint(dpy, focused_window, &class_hint);
        if(class_hint.res_class) {
            result = strip(class_hint.res_class);
            return result;
        }

        return result;
    }

    std::string get_window_name_at_position(Display *dpy, mgl::vec2i position, Window ignore_window) {
        std::string result;

        Window root;
        Window parent;
        Window *children = nullptr;
        unsigned int num_children = 0;
        if(!XQueryTree(dpy, DefaultRootWindow(dpy), &root, &parent, &children, &num_children) || !children)
            return result;

        for(int i = (int)num_children - 1; i >= 0; --i) {
            if(children[i] == ignore_window)
                continue;

            XWindowAttributes attr;
            memset(&attr, 0, sizeof(attr));
            XGetWindowAttributes(dpy, children[i], &attr);
            if(attr.override_redirect || attr.c_class != InputOutput)
                continue;

            if(position.x >= attr.x && position.x <= attr.x + attr.width && position.y >= attr.y && position.y <= attr.y + attr.height && window_is_user_program(dpy, children[i])) {
                const std::optional<std::string> window_title = get_window_title(dpy, children[i]);
                if(window_title)
                    result = strip(window_title.value());
                break;
            }
        }

        XFree(children);
        return result;
    }

    std::string get_window_name_at_cursor_position(Display *dpy, Window ignore_window) {
        Window cursor_window;
        const mgl::vec2i cursor_position = get_cursor_position(dpy, &cursor_window);
        return get_window_name_at_position(dpy, cursor_position, ignore_window);
    }

    typedef struct {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
        long input_mode;
        unsigned long status;
    } MotifHints;

    #define MWM_HINTS_DECORATIONS   2

    #define MWM_DECOR_NONE          0
    #define MWM_DECOR_ALL           1

    static void window_set_decorations_visible(Display *display, Window window, bool visible) {
        const Atom motif_wm_hints_atom = XInternAtom(display, "_MOTIF_WM_HINTS", False);
        MotifHints motif_hints;
        memset(&motif_hints, 0, sizeof(motif_hints));
        motif_hints.flags = MWM_HINTS_DECORATIONS;
        motif_hints.decorations = visible ? MWM_DECOR_ALL : MWM_DECOR_NONE;
        XChangeProperty(display, window, motif_wm_hints_atom, motif_wm_hints_atom, 32, PropModeReplace, (unsigned char*)&motif_hints, sizeof(motif_hints) / sizeof(long));
    }

    static bool create_window_get_center_position_kde(Display *display, mgl::vec2i &position) {
        const int size = 1;
        XSetWindowAttributes window_attr;
        window_attr.event_mask = StructureNotifyMask;
        window_attr.background_pixel = 0;
        const Window window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, size, size, 0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixel | CWEventMask, &window_attr);
        if(!window)
            return false;

        const Atom net_wm_window_type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
        const Atom net_wm_window_type_notification_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
        const Atom net_wm_window_type_utility = XInternAtom(display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
        const Atom net_wm_window_opacity = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);

        const Atom window_type_atoms[2] = {
            net_wm_window_type_notification_atom,
            net_wm_window_type_utility
        };
        XChangeProperty(display, window, net_wm_window_type_atom, XA_ATOM, 32, PropModeReplace, (const unsigned char*)window_type_atoms, 2L);

        const double alpha = 0.0;
        const unsigned long opacity = (unsigned long)(0xFFFFFFFFul * alpha);
        XChangeProperty(display, window, net_wm_window_opacity, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&opacity, 1L);

        window_set_decorations_visible(display, window, false);

        XSizeHints *size_hints = XAllocSizeHints();
        size_hints->width = size;
        size_hints->height = size;
        size_hints->min_width = size;
        size_hints->min_height = size;
        size_hints->max_width = size;
        size_hints->max_height = size;
        size_hints->flags = PSize | PMinSize | PMaxSize;
        XSetWMNormalHints(display, window, size_hints);
        XFree(size_hints);

        XMapWindow(display, window);
        XFlush(display);

        bool got_data = false;
        const int x_fd = XConnectionNumber(display);
        XEvent xev;
        while(true) {
            struct pollfd poll_fd;
            poll_fd.fd = x_fd;
            poll_fd.events = POLLIN;
            poll_fd.revents = 0;
            const int fds_ready = poll(&poll_fd, 1, 200);
            if(fds_ready == 0) {
                fprintf(stderr, "Error: timed out waiting for ConfigureNotify after XCreateWindow\n");
                break;
            } else if(fds_ready == -1 || !(poll_fd.revents & POLLIN)) {
                continue;
            }

            while(XPending(display)) {
                XNextEvent(display, &xev);
                if(xev.type == ConfigureNotify && xev.xconfigure.window == window) {
                    got_data = xev.xconfigure.x > 0 && xev.xconfigure.y > 0;
                    position.x = xev.xconfigure.x + xev.xconfigure.width / 2;
                    position.y = xev.xconfigure.y + xev.xconfigure.height / 2;
                    goto done;
                }
            }
        }

        done:
        XDestroyWindow(display, window);
        XFlush(display);

        return got_data;
    }

    static bool create_window_get_center_position_gnome(Display *display, mgl::vec2i &position) {
        const int size = 32;
        XSetWindowAttributes window_attr;
        window_attr.event_mask = StructureNotifyMask | ExposureMask;
        window_attr.background_pixel = 0;
        const Window window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, size, size, 0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixel | CWEventMask, &window_attr);
        if(!window)
            return false;

        const Atom net_wm_window_opacity = XInternAtom(display, "_NET_WM_WINDOW_OPACITY", False);
        const double alpha = 0.0;
        const unsigned long opacity = (unsigned long)(0xFFFFFFFFul * alpha);
        XChangeProperty(display, window, net_wm_window_opacity, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&opacity, 1L);

        window_set_decorations_visible(display, window, false);

        XSizeHints *size_hints = XAllocSizeHints();
        size_hints->width = size;
        size_hints->height = size;
        size_hints->min_width = size;
        size_hints->min_height = size;
        size_hints->max_width = size;
        size_hints->max_height = size;
        size_hints->flags = PSize | PMinSize | PMaxSize;
        XSetWMNormalHints(display, window, size_hints);
        XFree(size_hints);

        XMapWindow(display, window);
        XFlush(display);

        bool got_data = false;
        const int x_fd = XConnectionNumber(display);
        XEvent xev;
        while(true) {
            struct pollfd poll_fd;
            poll_fd.fd = x_fd;
            poll_fd.events = POLLIN;
            poll_fd.revents = 0;
            const int fds_ready = poll(&poll_fd, 1, 200);
            if(fds_ready == 0) {
                fprintf(stderr, "Error: timed out waiting for MapNotify/ConfigureNotify after XCreateWindow\n");
                break;
            } else if(fds_ready == -1 || !(poll_fd.revents & POLLIN)) {
                continue;
            }

            while(XPending(display)) {
                XNextEvent(display, &xev);
                if(xev.type == MapNotify && xev.xmap.window == window) {
                    int x = 0;
                    int y = 0;
                    Window w = None;
                    XTranslateCoordinates(display, window, DefaultRootWindow(display), 0, 0, &x, &y, &w);

                    got_data = x > 0 && y > 0;
                    position.x = x + size / 2;
                    position.y = y + size / 2;
                    if(got_data)
                        goto done;
                } else if(xev.type == ConfigureNotify && xev.xconfigure.window == window) {
                    got_data = xev.xconfigure.x > 0 && xev.xconfigure.y > 0;
                    position.x = xev.xconfigure.x + xev.xconfigure.width / 2;
                    position.y = xev.xconfigure.y + xev.xconfigure.height / 2;
                    if(got_data)
                        goto done;
                }
            }
        }

        done:
        XDestroyWindow(display, window);
        XFlush(display);

        return got_data;
    }

    mgl::vec2i create_window_get_center_position(Display *display) {
        mgl::vec2i pos;
        if(!create_window_get_center_position_kde(display, pos)) {
            pos.x = 0;
            pos.y = 0;
            create_window_get_center_position_gnome(display, pos);
        }
        return pos;
    }

    std::string get_window_manager_name(Display *display) {
        std::string wm_name;
        unsigned int property_size = 0;
        Window window = None;

        unsigned char *net_supporting_wm_check = window_get_property(display, DefaultRootWindow(display), XA_WINDOW, "_NET_SUPPORTING_WM_CHECK", &property_size);
        if(net_supporting_wm_check) {
            if(property_size == 8)
                window = *(Window*)net_supporting_wm_check;
            XFree(net_supporting_wm_check);
        }

        if(!window) {
            unsigned char *win_supporting_wm_check = window_get_property(display, DefaultRootWindow(display), XA_WINDOW, "_WIN_SUPPORTING_WM_CHECK", &property_size);
            if(win_supporting_wm_check) {
                if(property_size == 8)
                    window = *(Window*)win_supporting_wm_check;
                XFree(win_supporting_wm_check);
            }
        }

        if(!window)
            return wm_name;

        const std::optional<std::string> window_title = get_window_title(display, window);
        if(window_title)
            wm_name = strip(window_title.value());

        return wm_name;
    }

    bool is_compositor_running(Display *dpy, int screen) {
        char prop_name[20];
        snprintf(prop_name, sizeof(prop_name), "_NET_WM_CM_S%d", screen);
        const Atom prop_atom = XInternAtom(dpy, prop_name, False);
        return XGetSelectionOwner(dpy, prop_atom) != None;
    }

    static void get_monitors_callback(const mgl_monitor *monitor, void *userdata) {
        std::vector<Monitor> *monitors = (std::vector<Monitor>*)userdata;
        monitors->push_back({mgl::vec2i(monitor->pos.x, monitor->pos.y), mgl::vec2i(monitor->size.x, monitor->size.y)});
    }

    std::vector<Monitor> get_monitors(Display *dpy) {
        std::vector<Monitor> monitors;
        mgl_for_each_active_monitor_output(dpy, get_monitors_callback, &monitors);
        return monitors;
    }
}