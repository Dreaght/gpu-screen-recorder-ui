#include "../include/RegionSelector.hpp"

#include <stdio.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>

namespace gsr {
    static const int cursor_window_size = 32;
    static const int cursor_thickness = 5;
    static const int region_border_size = 2;

    static bool xinput_is_supported(Display *dpy, int *xi_opcode) {
        *xi_opcode = 0;
        int query_event = 0;
        int query_error = 0;
        if(!XQueryExtension(dpy, "XInputExtension", xi_opcode, &query_event, &query_error)) {
            fprintf(stderr, "error: RegionSelector: X Input extension not available\n");
            return false;
        }

        int major = 2;
        int minor = 1;
        int retval = XIQueryVersion(dpy, &major, &minor);
        if(retval != Success) {
            fprintf(stderr, "error: RegionSelector: XInput 2.1 is not supported\n");
            return false;
        }

        return true;
    }

    static int max_int(int a, int b) {
        return a >= b ? a : b;
    }

    static void set_region_rectangle(Display *dpy, Window window, int x, int y, int width, int height, int border_size) {
        if(width < 0) {
            x += width;
            width = abs(width);
        }

        if(height < 0) {
            y += height;
            height = abs(height);
        }

        XRectangle rectangles[] = {
            {
                (short)max_int(0, x),                              (short)max_int(0, y),
                (unsigned short)max_int(0, border_size),           (unsigned short)max_int(0, height)
            }, // Left
            {
                (short)max_int(0, x + width - border_size),        (short)max_int(0, y),
                (unsigned short)max_int(0, border_size),           (unsigned short)max_int(0, height)
            }, // Right
            {
                (short)max_int(0, x + border_size),                (short)max_int(0, y),
                (unsigned short)max_int(0, width - border_size*2), (unsigned short)max_int(0, border_size)
            }, // Top
            {
                (short)max_int(0, x + border_size),                (short)max_int(0, y + height - border_size),
                (unsigned short)max_int(0, width - border_size*2), (unsigned short)max_int(0, border_size)
            }, // Bottom
        };
        XShapeCombineRectangles(dpy, window, ShapeBounding, 0, 0, rectangles, 4, ShapeSet, Unsorted);
        XFlush(dpy);
    }

    static void set_window_shape_cross(Display *dpy, Window window, int window_width, int window_height, int thickness) {
        XRectangle rectangles[] = {
            {
                (short)(window_width / 2 - thickness / 2), (short)0,
                (unsigned short)thickness,                 (unsigned short)window_height
            }, // Vertical
            {
                (short)(0),                                (short)(window_height / 2 - thickness / 2),
                (unsigned short)window_width,              (unsigned short)thickness
            },     // Horizontal
        };
        XShapeCombineRectangles(dpy, window, ShapeBounding, 0, 0, rectangles, 2, ShapeSet, Unsorted);
        XFlush(dpy);
    }

    static void draw_rectangle(Display *dpy, Window window, GC gc, int x, int y, int width, int height) {
        if(width < 0) {
            x += width;
            width = abs(width);
        }

        if(height < 0) {
            y += height;
            height = abs(height);
        }

        XDrawRectangle(dpy, window, gc, x, y, width, height);
    }

    static Window create_cursor_window(Display *dpy, int width, int height, XVisualInfo *vinfo, unsigned long background_pixel) {
        XSetWindowAttributes window_attr;
        window_attr.background_pixel = background_pixel;
        window_attr.border_pixel = 0;
        window_attr.override_redirect = true;
        window_attr.event_mask = StructureNotifyMask | PointerMotionMask;
        window_attr.colormap = XCreateColormap(dpy, DefaultRootWindow(dpy), vinfo->visual, AllocNone);
        const Window window = XCreateWindow(dpy, DefaultRootWindow(dpy), 0, 0, width, height, 0, vinfo->depth, InputOutput, vinfo->visual, CWBackPixel | CWBorderPixel | CWOverrideRedirect | CWEventMask | CWColormap, &window_attr);
        if(window) {
            set_window_size_not_resizable(dpy, window, width, height);
            set_window_shape_cross(dpy, window, width, height, cursor_thickness);
            make_window_click_through(dpy, window);
        }
        return window;
    }

    static void draw_rectangle_around_selected_monitor(Display *dpy, Window window, GC region_gc, int region_border_size, bool is_wayland, const std::vector<Monitor> &monitors, mgl::vec2i cursor_pos) {
        const Monitor *focused_monitor = nullptr;
        for(const Monitor &monitor : monitors) {
            if(cursor_pos.x >= monitor.position.x && cursor_pos.x <= monitor.position.x + monitor.size.x
                && cursor_pos.y >= monitor.position.y && cursor_pos.y <= monitor.position.y + monitor.size.y)
            {
                focused_monitor = &monitor;
                break;
            }
        }

        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        if(focused_monitor) {
            x = focused_monitor->position.x;
            y = focused_monitor->position.y;
            width = focused_monitor->size.x;
            height = focused_monitor->size.y;
        }

        if(is_wayland)
            draw_rectangle(dpy, window, region_gc, x, y, width, height);
        else
            set_region_rectangle(dpy, window, x, y, width, height, region_border_size);
    }

    static void update_cursor_window(Display *dpy, Window window, Window cursor_window, bool is_wayland, int cursor_x, int cursor_y, int cursor_window_size, int thickness, GC cursor_gc) {
        if(is_wayland) {
            const int x = cursor_x - cursor_window_size / 2;
            const int y = cursor_y - cursor_window_size / 2;
            XFillRectangle(dpy, window, cursor_gc, x + cursor_window_size / 2 - thickness / 2 , y,                                          thickness,          cursor_window_size);
            XFillRectangle(dpy, window, cursor_gc, x,                                           y + cursor_window_size / 2 - thickness / 2, cursor_window_size, thickness);
        } else if(cursor_window) {
            XMoveWindow(dpy, cursor_window, cursor_x - cursor_window_size / 2, cursor_y - cursor_window_size / 2);
        }
        XFlush(dpy);
    }

    static bool is_xwayland(Display *dpy) {
        int opcode, event, error;
        return XQueryExtension(dpy, "XWAYLAND", &opcode, &event, &error);
    }

    static unsigned long mgl_color_to_x11_color(mgl::Color color) {
        if(color.a == 0)
            return 0;
        return ((uint32_t)color.a << 24) | (((uint32_t)color.r * color.a / 0xFF) << 16) | (((uint32_t)color.g * color.a / 0xFF) << 8) | ((uint32_t)color.b * color.a / 0xFF);
    }

    RegionSelector::RegionSelector() {
        
    }

    RegionSelector::~RegionSelector() {
        stop();
    }

    bool RegionSelector::start(mgl::Color border_color) {
        if(dpy)
            return false;

        const unsigned long border_color_x11 = mgl_color_to_x11_color(border_color);
        dpy = XOpenDisplay(nullptr);
        if(!dpy) {
            fprintf(stderr, "Error: RegionSelector::start: failed to connect to the X11 server\n");
            return false;
        }

        xi_opcode = 0;
        if(!xinput_is_supported(dpy, &xi_opcode)) {
            fprintf(stderr, "Error: RegionSelector::start: xinput not supported on your system\n");
            stop();
            return false;
        }

        is_wayland = is_xwayland(dpy);
        monitors = get_monitors(dpy);

        Window x11_cursor_window = None;
        cursor_pos = get_cursor_position(dpy, &x11_cursor_window);
        region.pos = {0, 0};
        region.size = {0, 0};

        XVisualInfo vinfo;
        memset(&vinfo, 0, sizeof(vinfo));
        XMatchVisualInfo(dpy, DefaultScreen(dpy), 32, TrueColor, &vinfo);
        region_window_colormap = XCreateColormap(dpy, DefaultRootWindow(dpy), vinfo.visual, AllocNone);

        XSetWindowAttributes window_attr;
        window_attr.background_pixel = is_wayland ? 0 : border_color_x11;
        window_attr.border_pixel = 0;
        window_attr.override_redirect = true;
        window_attr.event_mask = StructureNotifyMask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask;
        window_attr.colormap = region_window_colormap;

        Screen *screen = XDefaultScreenOfDisplay(dpy);
        region_window = XCreateWindow(dpy, DefaultRootWindow(dpy), 0, 0, XWidthOfScreen(screen), XHeightOfScreen(screen), 0,
            vinfo.depth, InputOutput, vinfo.visual, CWBackPixel | CWBorderPixel | CWOverrideRedirect | CWEventMask | CWColormap, &window_attr);
        if(!region_window) {
            fprintf(stderr, "Error: RegionSelector::start: failed to create region window\n");
            stop();
            return false;
        }
        set_window_size_not_resizable(dpy, region_window, XWidthOfScreen(screen), XHeightOfScreen(screen));

        unsigned char data = 2; // Prefer being composed to allow transparency. Do this to prevent the compositor from getting turned on/off when taking a screenshot
        XChangeProperty(dpy, region_window, XInternAtom(dpy, "_NET_WM_BYPASS_COMPOSITOR", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

        if(!is_wayland) {
            cursor_window = create_cursor_window(dpy, cursor_window_size, cursor_window_size, &vinfo, border_color_x11);
            if(!cursor_window)
                fprintf(stderr, "Warning: RegionSelector::start: failed to create cursor window\n");
            set_region_rectangle(dpy, region_window, 0, 0, 0, 0, 0);
        }

        XGCValues region_gc_values;
        memset(&region_gc_values, 0, sizeof(region_gc_values));
        region_gc_values.foreground = border_color_x11;
        region_gc_values.line_width = region_border_size;
        region_gc_values.line_style = LineSolid;
        region_gc = XCreateGC(dpy, region_window, GCForeground | GCLineWidth | GCLineStyle, &region_gc_values);

        XGCValues cursor_gc_values;
        memset(&cursor_gc_values, 0, sizeof(cursor_gc_values));
        cursor_gc_values.foreground = border_color_x11;
        cursor_gc_values.line_width = cursor_thickness;
        cursor_gc_values.line_style = LineSolid;
        cursor_gc = XCreateGC(dpy, region_window, GCForeground | GCLineWidth | GCLineStyle, &cursor_gc_values);

        if(!region_gc || !cursor_gc) {
            fprintf(stderr, "Error: RegionSelector::start: failed to create gc\n");
            stop();
            return false;
        }

        XMapWindow(dpy, region_window);
        make_window_sticky(dpy, region_window);
        hide_window_from_taskbar(dpy, region_window);
        XFixesHideCursor(dpy, region_window);
        XGrabPointer(dpy, DefaultRootWindow(dpy), True, ButtonPressMask | ButtonReleaseMask | ButtonMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync, CurrentTime);
        xi_grab_all_mouse_devices(dpy);
        XFlush(dpy);

        window_set_fullscreen(dpy, region_window, true);

        if(!is_wayland || x11_cursor_window)
            update_cursor_window(dpy, region_window, cursor_window, is_wayland, cursor_pos.x, cursor_pos.y, cursor_window_size, cursor_thickness, cursor_gc);

        if(cursor_window) {
            XMapWindow(dpy, cursor_window);
            make_window_sticky(dpy, cursor_window);
            hide_window_from_taskbar(dpy, cursor_window);
        }

        draw_rectangle_around_selected_monitor(dpy, region_window, region_gc, region_border_size, is_wayland, monitors, cursor_pos);

        XFlush(dpy);
        selected = false;
        canceled = false;
        return true;
    }

    void RegionSelector::stop() {
        if(!dpy)
            return;

        XWarpPointer(dpy, DefaultRootWindow(dpy), DefaultRootWindow(dpy), 0, 0, 0, 0, cursor_pos.x, cursor_pos.y);
        xi_warp_all_mouse_devices(dpy, cursor_pos);
        XFixesShowCursor(dpy, region_window);

        XUngrabPointer(dpy, CurrentTime);
        XUngrabKeyboard(dpy, CurrentTime);
        xi_ungrab_all_mouse_devices(dpy);
        XFlush(dpy);

        if(region_gc) {
            XFreeGC(dpy, region_gc);
            region_gc = nullptr;
        }

        if(cursor_gc) {
            XFreeGC(dpy, cursor_gc);
            cursor_gc = nullptr;
        }

        if(region_window_colormap) {
            XFreeColormap(dpy, region_window_colormap);
            region_window_colormap = 0;
        }

        if(region_window) {
            XDestroyWindow(dpy, region_window);
            region_window = 0;
        }

        XFlush(dpy);
        XSync(dpy, False);

        XCloseDisplay(dpy);
        dpy = nullptr;
        selecting_region = false;
    }

    bool RegionSelector::is_started() const {
        return dpy != nullptr;
    }

    bool RegionSelector::failed() const {
        return !dpy;
    }

    bool RegionSelector::poll_events() {
        if(!dpy || selected)
            return false;

        XEvent xev;
        while(XPending(dpy)) {
            XNextEvent(dpy, &xev);

            if(xev.type == KeyRelease && XKeycodeToKeysym(dpy, xev.xkey.keycode, 0) == XK_Escape) {
                canceled = true;
                selected = false;
                stop();
                break;
            }

            XGenericEventCookie *cookie = &xev.xcookie;
            if(cookie->type != GenericEvent || cookie->extension != xi_opcode || !XGetEventData(dpy, cookie))
                continue;

            const XIDeviceEvent *de = (XIDeviceEvent*)cookie->data;
            switch(cookie->evtype) {
                case XI_ButtonPress: {
                    on_button_press(de);
                    break;
                }
                case XI_ButtonRelease: {
                    on_button_release(de);
                    break;
                }
                case XI_Motion: {
                    on_mouse_motion(de);
                    break;
                }
            }
            XFreeEventData(dpy, cookie);

            if(selected) {
                stop();
                break;
            }
        }
        return true;
    }

    bool RegionSelector::take_selection() {
        const bool result = selected;
        selected = false;
        return result;
    }

    bool RegionSelector::take_canceled() {
        const bool result = canceled;
        canceled = false;
        return result;
    }

    Region RegionSelector::get_selection() const {
        return region;
    }

    void RegionSelector::on_button_press(const void *de) {
        const XIDeviceEvent *device_event = (XIDeviceEvent*)de;
        if(device_event->detail != Button1)
            return;

        region.pos = { (int)device_event->root_x, (int)device_event->root_y };
        selecting_region = true;
    }

    void RegionSelector::on_button_release(const void *de) {
        const XIDeviceEvent *device_event = (XIDeviceEvent*)de;
        if(device_event->detail != Button1)
            return;

        if(!selecting_region)
            return;

        if(is_wayland) {
            XClearWindow(dpy, region_window);
            XFlush(dpy);
        } else {
            set_region_rectangle(dpy, region_window, 0, 0, 0, 0, 0);
        }
        selecting_region = false;

        cursor_pos = region.pos + region.size;

        if(region.size.x < 0) {
            region.pos.x += region.size.x;
            region.size.x = abs(region.size.x);
        }

        if(region.size.y < 0) {
            region.pos.y += region.size.y;
            region.size.y = abs(region.size.y);
        }

        if(region.size.x > 0)
            region.size.x += 1;

        if(region.size.y > 0)
            region.size.y += 1;

        selected = true;
    }

    void RegionSelector::on_mouse_motion(const void *de) {
        const XIDeviceEvent *device_event = (XIDeviceEvent*)de;
        XClearWindow(dpy, region_window);
        if(selecting_region) {
            region.size.x = device_event->root_x - region.pos.x;
            region.size.y = device_event->root_y - region.pos.y;
            cursor_pos = region.pos + region.size;

            if(is_wayland)
                draw_rectangle(dpy, region_window, region_gc, region.pos.x, region.pos.y, region.size.x, region.size.y);
            else
                set_region_rectangle(dpy, region_window, region.pos.x, region.pos.y, region.size.x, region.size.y, region_border_size);
        } else {
            cursor_pos = { (int)device_event->root_x, (int)device_event->root_y };
            draw_rectangle_around_selected_monitor(dpy, region_window, region_gc, region_border_size, is_wayland, monitors, cursor_pos);
        }
        update_cursor_window(dpy, region_window, cursor_window, is_wayland, cursor_pos.x, cursor_pos.y, cursor_window_size, cursor_thickness, cursor_gc);
        XFlush(dpy);
    }
}