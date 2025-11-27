#include "../include/WindowSelector.hpp"
#include "../include/WindowUtils.hpp"

#include <stdio.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

namespace gsr {
    static const int rectangle_border_size = 2;

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

    static unsigned long mgl_color_to_x11_color(mgl::Color color) {
        if(color.a == 0)
            return 0;
        return ((uint32_t)color.a << 24) | (((uint32_t)color.r * color.a / 0xFF) << 16) | (((uint32_t)color.g * color.a / 0xFF) << 8) | ((uint32_t)color.b * color.a / 0xFF);
    }

    static Window get_cursor_window(Display *dpy) {
        Window root_window = None;
        Window window = None;
        int dummy_i;
        unsigned int dummy_u;
        mgl::vec2i root_pos;
        XQueryPointer(dpy, DefaultRootWindow(dpy), &root_window, &window, &root_pos.x, &root_pos.y, &dummy_i, &dummy_i, &dummy_u);
        return window;
    }

    static void get_window_geometry(Display *dpy, Window window, mgl::vec2i &pos, mgl::vec2i &size) {
        Window root_window;
        int x = 0;
        int y = 0;
        unsigned int w = 0;
        unsigned int h = 0;
        unsigned int dummy_border, dummy_depth;
        XGetGeometry(dpy, window, &root_window, &x, &y, &w, &h, &dummy_border, &dummy_depth);
        pos.x = x;
        pos.y = y;
        size.x = w;
        size.y = h;
    }

    WindowSelector::WindowSelector() {
        
    }

    WindowSelector::~WindowSelector() {
        stop();
    }

    bool WindowSelector::start(mgl::Color border_color) {
        if(dpy)
            return false;

        const unsigned long border_color_x11 = mgl_color_to_x11_color(border_color);
        dpy = XOpenDisplay(nullptr);
        if(!dpy) {
            fprintf(stderr, "Error: WindowSelector::start: failed to connect to the X11 server\n");
            return false;
        }

        const Window cursor_window = get_cursor_window(dpy);
        mgl::vec2i cursor_window_pos, cursor_window_size;
        get_window_geometry(dpy, cursor_window, cursor_window_pos, cursor_window_size);

        XVisualInfo vinfo;
        memset(&vinfo, 0, sizeof(vinfo));
        XMatchVisualInfo(dpy, DefaultScreen(dpy), 32, TrueColor, &vinfo);
        border_window_colormap = XCreateColormap(dpy, DefaultRootWindow(dpy), vinfo.visual, AllocNone);

        XSetWindowAttributes window_attr;
        window_attr.background_pixel = border_color_x11;
        window_attr.border_pixel = 0;
        window_attr.override_redirect = true;
        window_attr.event_mask = StructureNotifyMask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask;
        window_attr.colormap = border_window_colormap;

        Screen *screen = XDefaultScreenOfDisplay(dpy);
        border_window = XCreateWindow(dpy, DefaultRootWindow(dpy), 0, 0, XWidthOfScreen(screen), XHeightOfScreen(screen), 0,
            vinfo.depth, InputOutput, vinfo.visual, CWBackPixel | CWBorderPixel | CWOverrideRedirect | CWEventMask | CWColormap, &window_attr);
        if(!border_window) {
            fprintf(stderr, "Error: WindowSelector::start: failed to create region window\n");
            stop();
            return false;
        }
        set_window_size_not_resizable(dpy, border_window, XWidthOfScreen(screen), XHeightOfScreen(screen));

        unsigned char data = 2; // Prefer being composed to allow transparency. Do this to prevent the compositor from getting turned on/off when taking a screenshot
        XChangeProperty(dpy, border_window, XInternAtom(dpy, "_NET_WM_BYPASS_COMPOSITOR", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

        if(cursor_window && cursor_window != DefaultRootWindow(dpy))
            set_region_rectangle(dpy, border_window, cursor_window_pos.x, cursor_window_pos.y, cursor_window_size.x, cursor_window_size.y, rectangle_border_size);
        else
            set_region_rectangle(dpy, border_window, 0, 0, 0, 0, 0);
        make_window_click_through(dpy, border_window);
        XMapWindow(dpy, border_window);

        crosshair_cursor = XCreateFontCursor(dpy, XC_crosshair);
        XGrabPointer(dpy, DefaultRootWindow(dpy), True, PointerMotionMask | ButtonPressMask | ButtonReleaseMask | ButtonMotionMask, GrabModeAsync, GrabModeAsync, None, crosshair_cursor, CurrentTime);
        XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync, GrabModeAsync, CurrentTime);
        XFlush(dpy);

        selected = false;
        canceled = false;
        selected_window = None;
        return true;
    }

    void WindowSelector::stop() {
        if(!dpy)
            return;

        XUngrabPointer(dpy, CurrentTime);
        XUngrabKeyboard(dpy, CurrentTime);

        if(border_window_colormap) {
            XFreeColormap(dpy, border_window_colormap);
            border_window_colormap = 0;
        }

        if(border_window) {
            XDestroyWindow(dpy, border_window);
            border_window = 0;
        }

        if(crosshair_cursor) {
            XFreeCursor(dpy, crosshair_cursor);
            crosshair_cursor = None;
        }

        XFlush(dpy);
        XSync(dpy, False);

        XCloseDisplay(dpy);
        dpy = nullptr;
    }

    bool WindowSelector::is_started() const {
        return dpy != nullptr;
    }

    bool WindowSelector::failed() const {
        return !dpy;
    }

    bool WindowSelector::poll_events() {
        if(!dpy || selected)
            return false;

        XEvent xev;
        while(XPending(dpy)) {
            XNextEvent(dpy, &xev);

            if(xev.type == MotionNotify) {
                const Window motion_window = xev.xmotion.subwindow;
                mgl::vec2i motion_window_pos, motion_window_size;
                get_window_geometry(dpy, motion_window, motion_window_pos, motion_window_size);
                if(motion_window && motion_window != DefaultRootWindow(dpy))
                    set_region_rectangle(dpy, border_window, motion_window_pos.x, motion_window_pos.y, motion_window_size.x, motion_window_size.y, rectangle_border_size);
                else
                    set_region_rectangle(dpy, border_window, 0, 0, 0, 0, 0);
                XFlush(dpy);
            } else if(xev.type == ButtonRelease && xev.xbutton.button == Button1) {
                selected_window = xev.xbutton.subwindow;
                const Window clicked_window_real = window_get_target_window_child(dpy, selected_window);
                if(clicked_window_real)
                    selected_window = clicked_window_real;
                selected = true;

                stop();
                break;
            } else if(xev.type == KeyRelease && XKeycodeToKeysym(dpy, xev.xkey.keycode, 0) == XK_Escape) {
                canceled = true;
                selected = false;
                stop();
                break;
            }
        }
        return true;
    }

    bool WindowSelector::take_selection() {
        const bool result = selected;
        selected = false;
        return result;
    }

    bool WindowSelector::take_canceled() {
        const bool result = canceled;
        canceled = false;
        return result;
    }

    Window WindowSelector::get_selection() const {
        return selected_window;
    }
}