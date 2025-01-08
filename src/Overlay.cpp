#include "../include/Overlay.hpp"
#include "../include/Theme.hpp"
#include "../include/Config.hpp"
#include "../include/Process.hpp"
#include "../include/Utils.hpp"
#include "../include/gui/StaticPage.hpp"
#include "../include/gui/DropdownButton.hpp"
#include "../include/gui/CustomRendererWidget.hpp"
#include "../include/gui/SettingsPage.hpp"
#include "../include/gui/GlobalSettingsPage.hpp"
#include "../include/gui/Utils.hpp"
#include "../include/gui/PageStack.hpp"
#include "../include/gui/GsrPage.hpp"
#include "../include/WindowUtils.hpp"
#include "../include/GlobalHotkeys.hpp"

#include <string.h>
#include <assert.h>
#include <sys/wait.h>
#include <limits.h>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/shape.h>
#include <X11/Xcursor/Xcursor.h>
#include <mglpp/system/Rect.hpp>
#include <mglpp/window/Event.hpp>

extern "C" {
#include <mgl/mgl.h>
}

namespace gsr {
    static const mgl::Color bg_color(0, 0, 0, 100);
    static const double force_window_on_top_timeout_seconds = 1.0;
    static const double replay_status_update_check_timeout_seconds = 1.0;

    static mgl::Texture texture_from_ximage(XImage *img) {
        uint8_t *texture_data = (uint8_t*)malloc(img->width * img->height * 3);
        // TODO:

        for(int y = 0; y < img->height; ++y) {
            for(int x = 0; x < img->width; ++x) {
                unsigned long pixel = XGetPixel(img, x, y);
                unsigned char red = (pixel & img->red_mask) >> 16;
                unsigned char green = (pixel & img->green_mask) >> 8;
                unsigned char blue = pixel & img->blue_mask;

                const size_t texture_data_index = (x + y * img->width) * 3;
                texture_data[texture_data_index + 0] = red;
                texture_data[texture_data_index + 1] = green;
                texture_data[texture_data_index + 2] = blue;
            }
        }

        mgl::Texture texture;
        // TODO:
        texture.load_from_memory(texture_data, img->width, img->height, MGL_IMAGE_FORMAT_RGB);
        free(texture_data);
        return texture;
    }

    static bool texture_from_x11_cursor(XcursorImage *x11_cursor_image, bool *visible, mgl::vec2i *hotspot, mgl::Texture &texture) {
        uint8_t *cursor_data = NULL;
        uint8_t *out = NULL;
        const unsigned int *pixels = NULL;
        *visible = false;

        if(!x11_cursor_image)
            return false;

        if(!x11_cursor_image->pixels)
            return false;

        hotspot->x = x11_cursor_image->xhot;
        hotspot->y = x11_cursor_image->yhot;

        pixels = x11_cursor_image->pixels;
        cursor_data = (uint8_t*)malloc((int)x11_cursor_image->width * (int)x11_cursor_image->height * 4);
        if(!cursor_data)
            return false;

        out = cursor_data;
        /* Un-premultiply alpha */
        for(uint32_t y = 0; y < x11_cursor_image->height; ++y) {
            for(uint32_t x = 0; x < x11_cursor_image->width; ++x) {
                uint32_t pixel = *pixels++;
                uint8_t *in = (uint8_t*)&pixel;
                uint8_t alpha = in[3];
                if(alpha == 0) {
                    alpha = 1;
                } else {
                    *visible = true;
                }

                out[0] = (float)in[2] * 255.0/(float)alpha;
                out[1] = (float)in[1] * 255.0/(float)alpha;
                out[2] = (float)in[0] * 255.0/(float)alpha;
                out[3] = in[3];
                out += 4;
                in += 4;
            }
        }

        texture.load_from_memory(cursor_data, x11_cursor_image->width, x11_cursor_image->height, MGL_IMAGE_FORMAT_RGBA);
        free(cursor_data);
        return true;
    }

    static char hex_value_to_str(uint8_t v) {
        if(v <= 9)
            return '0' + v;
        else if(v >= 10 && v <= 15)
            return 'A' + (v - 10);
        else
            return '0';
    }

    // Excludes alpha
    static std::string color_to_hex_str(mgl::Color color) {
        std::string result;
        result.resize(6);

        result[0] = hex_value_to_str((color.r & 0xF0) >> 4);
        result[1] = hex_value_to_str(color.r & 0x0F);

        result[2] = hex_value_to_str((color.g & 0xF0) >> 4);
        result[3] = hex_value_to_str(color.g & 0x0F);

        result[4] = hex_value_to_str((color.b & 0xF0) >> 4);
        result[5] = hex_value_to_str(color.b & 0x0F);

        return result;
    }

    struct DrawableGeometry {
        int x, y, width, height;
    };

    static bool get_drawable_geometry(Display *display, Drawable drawable, DrawableGeometry *geometry) {
        geometry->x = 0;
        geometry->y = 0;
        geometry->width = 0;
        geometry->height = 0;

        Window root_window;
        unsigned int w, h;
        unsigned int dummy_border, dummy_depth;
        Status s = XGetGeometry(display, drawable, &root_window, &geometry->x, &geometry->y, &w, &h, &dummy_border, &dummy_depth);

        geometry->width = w;
        geometry->height = h;
        return s != Success;
    }

    static bool diff_int(int a, int b, int difference) {
        return std::abs(a - b) <= difference;
    }

    static bool is_window_fullscreen_on_monitor(Display *display, Window window, const mgl_monitor *monitor) {
        if(!window)
            return false;

        DrawableGeometry geometry;
        if(!get_drawable_geometry(display, window, &geometry))
            return false;

        const int margin = 2;
        return diff_int(geometry.x, monitor->pos.x, margin) && diff_int(geometry.y, monitor->pos.y, margin)
            && diff_int(geometry.width, monitor->size.x, margin) && diff_int(geometry.height, monitor->size.y, margin);
    }

    /*static bool is_window_fullscreen_on_monitor(Display *display, Window window, const mgl_monitor *monitors, int num_monitors) {
        if(!window)
            return false;

        DrawableGeometry geometry;
        if(!get_drawable_geometry(display, window, &geometry))
            return false;

        const int margin = 2;
        for(int i = 0; i < num_monitors; ++i) {
            const mgl_monitor *mon = &monitors[i];
            if(diff_int(geometry.x, mon->pos.x, margin) && diff_int(geometry.y, mon->pos.y, margin)
                && diff_int(geometry.width, mon->size.x, margin) && diff_int(geometry.height, mon->size.y, margin))
            {
                return true;
            }
        }

        return false;
    }*/

    static bool window_is_fullscreen(Display *display, Window window) {
        const Atom wm_state_atom = XInternAtom(display, "_NET_WM_STATE", False);
        const Atom wm_state_fullscreen_atom = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);

        Atom type = None;
        int format = 0;
        unsigned long num_items = 0;
        unsigned long bytes_after = 0;
        unsigned char *properties = nullptr;
        if(XGetWindowProperty(display, window, wm_state_atom, 0, 1024, False, XA_ATOM, &type, &format, &num_items, &bytes_after, &properties) < Success) {
            fprintf(stderr, "Failed to get window wm state property\n");
            return false;
        }

        if(!properties)
            return false;

        bool is_fullscreen = false;
        Atom *atoms = (Atom*)properties;
        for(unsigned long i = 0; i < num_items; ++i) {
            if(atoms[i] == wm_state_fullscreen_atom) {
                is_fullscreen = true;
                break;
            }
        }

        XFree(properties);
        return is_fullscreen;
    }

    static void set_focused_window(Display *dpy, Window window) {
        XSetInputFocus(dpy, window, RevertToPointerRoot, CurrentTime);

        const Atom net_active_window_atom = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
        XChangeProperty(dpy, DefaultRootWindow(dpy), net_active_window_atom, XA_WINDOW, 32, PropModeReplace, (const unsigned char*)&window, 1);

        XFlush(dpy);
    }

    #define _NET_WM_STATE_REMOVE  0
    #define _NET_WM_STATE_ADD     1
    #define _NET_WM_STATE_TOGGLE  2

    static Bool set_window_wm_state(Display *dpy, Window window, Atom atom) {
        const Atom net_wm_state_atom = XInternAtom(dpy, "_NET_WM_STATE", False);

        XClientMessageEvent xclient;
        memset(&xclient, 0, sizeof(xclient));

        xclient.type = ClientMessage;
        xclient.window = window;
        xclient.message_type = net_wm_state_atom;
        xclient.format = 32;
        xclient.data.l[0] = _NET_WM_STATE_ADD;
        xclient.data.l[1] = atom;
        xclient.data.l[2] = 0;
        xclient.data.l[3] = 0;
        xclient.data.l[4] = 0;

        XSendEvent(dpy, DefaultRootWindow(dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, (XEvent*)&xclient);
        XFlush(dpy);
        return True;
    }

    static void make_window_click_through(Display *display, Window window) {
        XRectangle rect;
        memset(&rect, 0, sizeof(rect));
        XserverRegion region = XFixesCreateRegion(display, &rect, 1);
        XFixesSetWindowShapeRegion(display, window, ShapeInput, 0, 0, region);
        XFixesDestroyRegion(display, region);
    }

    static Bool make_window_sticky(Display *dpy, Window window) {
        return set_window_wm_state(dpy, window, XInternAtom(dpy, "_NET_WM_STATE_STICKY", False));
    }

    static Bool hide_window_from_taskbar(Display *dpy, Window window) {
        return set_window_wm_state(dpy, window, XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False));
    }

    // Returns the first monitor if not found. Assumes there is at least one monitor connected.
    static const mgl_monitor* find_monitor_at_position(mgl::Window &window, mgl::vec2i pos) {
        const mgl_window *win = window.internal_window();
        assert(win->num_monitors > 0);
        for(int i = 0; i < win->num_monitors; ++i) {
            const mgl_monitor *mon = &win->monitors[i];
            if(mgl::IntRect({ mon->pos.x, mon->pos.y }, { mon->size.x, mon->size.y }).contains(pos))
                return mon;
        }
        return &win->monitors[0];
    }

    static std::string get_power_supply_online_filepath() {
        std::string result;
        const char *paths[] = {
            "/sys/class/power_supply/ADP0/online",
            "/sys/class/power_supply/ADP1/online",
            "/sys/class/power_supply/AC/online",
            "/sys/class/power_supply/ACAD/online"
        };
        for(const char *power_supply_online_filepath : paths) {
            if(access(power_supply_online_filepath, F_OK) == 0) {
                result = power_supply_online_filepath;
                break;
            }
        }
        return result;
    }

    static bool power_supply_is_connected(const char *power_supply_online_filepath) {
        int fd = open(power_supply_online_filepath, O_RDONLY);
        if(fd == -1)
            return false;

        char buf[1];
        const bool is_connected = read(fd, buf, 1) == 1 && buf[0] == '1';
        close(fd);
        return is_connected;
    }

    static bool xinput_is_supported(Display *dpy, int *xi_opcode) {
        *xi_opcode = 0;
        int query_event = 0;
        int query_error = 0;
        if(!XQueryExtension(dpy, "XInputExtension", xi_opcode, &query_event, &query_error)) {
            fprintf(stderr, "gsr-ui error: X Input extension not available\n");
            return false;
        }

        int major = 2;
        int minor = 1;
        int retval = XIQueryVersion(dpy, &major, &minor);
        if (retval != Success) {
            fprintf(stderr, "gsr-ui error: XInput 2.1 is not supported\n");
            return false;
        }

        return true;
    }

    Overlay::Overlay(std::string resources_path, GsrInfo gsr_info, SupportedCaptureOptions capture_options, egl_functions egl_funcs) :
        resources_path(std::move(resources_path)),
        gsr_info(std::move(gsr_info)),
        egl_funcs(egl_funcs),
        config(capture_options),
        bg_screenshot_overlay({0.0f, 0.0f}),
        top_bar_background({0.0f, 0.0f}),
        close_button_widget({0.0f, 0.0f})
    {
        key_bindings[0].key_event.code = mgl::Keyboard::Escape;
        key_bindings[0].key_event.alt = false;
        key_bindings[0].key_event.control = false;
        key_bindings[0].key_event.shift = false;
        key_bindings[0].key_event.system = false;
        key_bindings[0].callback = [this]() {
            page_stack.pop();
        };

        memset(&window_texture, 0, sizeof(window_texture));

        std::optional<Config> new_config = read_config(capture_options);
        if(new_config)
            config = std::move(new_config.value());

        init_color_theme(config, this->gsr_info);

        power_supply_online_filepath = get_power_supply_online_filepath();

        if(config.replay_config.turn_on_replay_automatically_mode == "turn_on_at_system_startup")
            on_press_start_replay(true);
    }

    Overlay::~Overlay() {
        hide();

        if(notification_process > 0) {
            kill(notification_process, SIGKILL);
            int status;
            if(waitpid(notification_process, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            }
            notification_process = -1;
        }

        if(gpu_screen_recorder_process > 0) {
            kill(gpu_screen_recorder_process, SIGINT);
            int status;
            if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            }
            gpu_screen_recorder_process = -1;
        }

        close_gpu_screen_recorder_output();
        deinit_color_theme();
    }

    void Overlay::xi_setup() {
        xi_display = XOpenDisplay(nullptr);
        if(!xi_display) {
            fprintf(stderr, "gsr-ui error: failed to setup XI connection\n");
            return;
        }

        if(!xinput_is_supported(xi_display, &xi_opcode))
            goto error;

        xi_input_xev = (XEvent*)calloc(1, sizeof(XEvent));
        if(!xi_input_xev)
            throw std::runtime_error("gsr-ui error: failed to allocate XEvent data");

        xi_output_xev = (XEvent*)calloc(1, sizeof(XEvent));
        if(!xi_output_xev)
            throw std::runtime_error("gsr-ui error: failed to allocate XEvent data");

        unsigned char mask[XIMaskLen(XI_LASTEVENT)];
        memset(mask, 0, sizeof(mask));
        XISetMask(mask, XI_Motion);
        //XISetMask(mask, XI_RawMotion);
        XISetMask(mask, XI_ButtonPress);
        XISetMask(mask, XI_ButtonRelease);
        XISetMask(mask, XI_KeyPress);
        XISetMask(mask, XI_KeyRelease);

        XIEventMask xi_masks;
        xi_masks.deviceid = XIAllMasterDevices;
        xi_masks.mask_len = sizeof(mask);
        xi_masks.mask = mask;
        if(XISelectEvents(xi_display, DefaultRootWindow(xi_display), &xi_masks, 1) != Success) {
            fprintf(stderr, "gsr-ui error: XISelectEvents failed\n");
            goto error;
        }

        XFlush(xi_display);
        return;

        error:
        free(xi_input_xev);
        xi_input_xev = nullptr;
        free(xi_output_xev);
        xi_output_xev = nullptr;
        if(xi_display) {
            XCloseDisplay(xi_display);
            xi_display = nullptr;
        }
    }

    void Overlay::close_gpu_screen_recorder_output() {
        if(gpu_screen_recorder_process_output_file) {
            fclose(gpu_screen_recorder_process_output_file);
            gpu_screen_recorder_process_output_file = nullptr;
        }

        if(gpu_screen_recorder_process_output_fd > 0) {
            close(gpu_screen_recorder_process_output_fd);
            gpu_screen_recorder_process_output_fd = -1;
        }
    }

    void Overlay::handle_xi_events() {
        if(!xi_display)
            return;

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        while(XPending(xi_display)) {
            XNextEvent(xi_display, xi_input_xev);
            XGenericEventCookie *cookie = &xi_input_xev->xcookie;
            if(cookie->type == GenericEvent && cookie->extension == xi_opcode && XGetEventData(xi_display, cookie)) {
                const XIDeviceEvent *de = (XIDeviceEvent*)cookie->data;
                if(cookie->evtype == XI_Motion) {
                    memset(xi_output_xev, 0, sizeof(*xi_output_xev));
                    xi_output_xev->type = MotionNotify;
                    xi_output_xev->xmotion.display = display;
                    xi_output_xev->xmotion.window = window->get_system_handle();
                    xi_output_xev->xmotion.subwindow = window->get_system_handle();
                    xi_output_xev->xmotion.x = de->root_x - window_pos.x;
                    xi_output_xev->xmotion.y = de->root_y - window_pos.y;
                    xi_output_xev->xmotion.x_root = de->root_x;
                    xi_output_xev->xmotion.y_root = de->root_y;
                    //xi_output_xev->xmotion.state = // modifiers // TODO:
                    if(window->inject_x11_event(xi_output_xev, event))
                        on_event(event);
                } else if(cookie->evtype == XI_ButtonPress || cookie->evtype == XI_ButtonRelease) {
                    memset(xi_output_xev, 0, sizeof(*xi_output_xev));
                    xi_output_xev->type = cookie->evtype == XI_ButtonPress ? ButtonPress : ButtonRelease;
                    xi_output_xev->xbutton.display = display;
                    xi_output_xev->xbutton.window = window->get_system_handle();
                    xi_output_xev->xbutton.subwindow = window->get_system_handle();
                    xi_output_xev->xbutton.x = de->root_x - window_pos.x;
                    xi_output_xev->xbutton.y = de->root_y - window_pos.y;
                    xi_output_xev->xbutton.x_root = de->root_x;
                    xi_output_xev->xbutton.y_root = de->root_y;
                    //xi_output_xev->xbutton.state = // modifiers // TODO:
                    xi_output_xev->xbutton.button = de->detail;
                    if(window->inject_x11_event(xi_output_xev, event))
                        on_event(event);
                } else if(cookie->evtype == XI_KeyPress || cookie->evtype == XI_KeyRelease) {
                    memset(xi_output_xev, 0, sizeof(*xi_output_xev));
                    xi_output_xev->type = cookie->evtype == XI_KeyPress ? KeyPress : KeyRelease;
                    xi_output_xev->xkey.display = display;
                    xi_output_xev->xkey.window = window->get_system_handle();
                    xi_output_xev->xkey.subwindow = window->get_system_handle();
                    xi_output_xev->xkey.x = de->root_x - window_pos.x;
                    xi_output_xev->xkey.y = de->root_y - window_pos.y;
                    xi_output_xev->xkey.x_root = de->root_x;
                    xi_output_xev->xkey.y_root = de->root_y;
                    xi_output_xev->xkey.state = de->mods.effective;
                    xi_output_xev->xkey.keycode = de->detail;
                    if(window->inject_x11_event(xi_output_xev, event))
                        on_event(event);
                }
                //fprintf(stderr, "got xi event: %d\n", cookie->evtype);
                XFreeEventData(xi_display, cookie);
            }
        }
    }

    static uint32_t key_event_to_bitmask(mgl::Event::KeyEvent key_event) {
        return ((uint32_t)key_event.alt     << (uint32_t)0)
            |  ((uint32_t)key_event.control << (uint32_t)1)
            |  ((uint32_t)key_event.shift   << (uint32_t)2)
            |  ((uint32_t)key_event.system  << (uint32_t)3);
    }

    void Overlay::process_key_bindings(mgl::Event &event) {
        if(event.type != mgl::Event::KeyReleased)
            return;

        const uint32_t event_key_bitmask = key_event_to_bitmask(event.key);
        for(const KeyBinding &key_binding : key_bindings) {
            if(event.key.code == key_binding.key_event.code && event_key_bitmask == key_event_to_bitmask(key_binding.key_event))
                key_binding.callback();
        }
    }

    void Overlay::handle_events(gsr::GlobalHotkeys *global_hotkeys) {
        if(!visible || !window)
            return;

        handle_xi_events();

        while(window->poll_event(event)) {
            if(global_hotkeys) {
                if(!global_hotkeys->on_event(event))
                    continue;
            }
            on_event(event);
        }
    }

    void Overlay::on_event(mgl::Event &event) {
        if(!visible || !window)
            return;

        if(!close_button_widget.on_event(event, *window, mgl::vec2f(0.0f, 0.0f)))
            return;

        if(!page_stack.on_event(event, *window, mgl::vec2f(0.0f, 0.0f)))
            return;

        process_key_bindings(event);
    }

    bool Overlay::draw() {
        update_notification_process_status();
        update_gsr_replay_save();
        update_gsr_process_status();
        replay_status_update_status();

        if(!visible)
            return false;

        if(page_stack.empty()) {
            hide();
            return false;
        }

        if(!window)
            return false;

        grab_mouse_and_keyboard();

        //force_window_on_top();

        window->clear(bg_color);

        if(window_texture_sprite.get_texture() && window_texture.texture_id) {
            window->draw(window_texture_sprite);
            window->draw(bg_screenshot_overlay);
        } else if(screenshot_texture.is_valid()) {
            window->draw(screenshot_sprite);
            window->draw(bg_screenshot_overlay);
        }

        window->draw(top_bar_background);
        window->draw(top_bar_text);
        window->draw(logo_sprite);

        close_button_widget.draw(*window, mgl::vec2f(0.0f, 0.0f));
        page_stack.draw(*window, mgl::vec2f(0.0f, 0.0f));

        if(cursor_texture.is_valid()) {
            cursor_sprite.set_position((window->get_mouse_position() - cursor_hotspot).to_vec2f());
            window->draw(cursor_sprite);
        }

        window->display();

        if(!drawn_first_frame) {
            drawn_first_frame = true;
            mgl::Event event;
            event.type = mgl::Event::MouseMoved;
            event.mouse_move.x = window->get_mouse_position().x;
            event.mouse_move.y = window->get_mouse_position().y;
            on_event(event);
        }

        return true;
    }

    void Overlay::grab_mouse_and_keyboard() {
        // TODO: Remove these grabs when debugging with a debugger, or your X11 session will appear frozen.
        // There should be a debug mode to not use these
        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;
        XGrabPointer(display, window->get_system_handle(), True,
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
            Button1MotionMask | Button2MotionMask | Button3MotionMask | Button4MotionMask | Button5MotionMask |
            ButtonMotionMask,
            GrabModeAsync, GrabModeAsync, None, default_cursor, CurrentTime);
        // TODO: This breaks global hotkeys (when using x11 global hotkeys)
        XGrabKeyboard(display, window->get_system_handle(), True, GrabModeAsync, GrabModeAsync, CurrentTime);
        XFlush(display);
    }

    void Overlay::xi_setup_fake_cursor() {
        if(!xi_display)
            return;

        XFixesHideCursor(xi_display, DefaultRootWindow(xi_display));
        XFlush(xi_display);

        // TODO: XCURSOR_SIZE and XCURSOR_THEME environment variables
        const char *cursor_theme = XcursorGetTheme(xi_display);
        if(!cursor_theme) {
            //fprintf(stderr, "Warning: failed to get cursor theme, using \"default\" theme instead\n");
            cursor_theme = "default";
        }

        int cursor_size = XcursorGetDefaultSize(xi_display);
        if(cursor_size <= 1)
            cursor_size = 24;

        XcursorImage *cursor_image = XcursorShapeLoadImage(XC_left_ptr, cursor_theme, cursor_size);
        if(!cursor_image) {
            fprintf(stderr, "Error: failed to get cursor\n");
            return;
        }

        bool cursor_visible = false;
        texture_from_x11_cursor(cursor_image, &cursor_visible, &cursor_hotspot, cursor_texture);
        if(cursor_texture.is_valid())
            cursor_sprite.set_texture(&cursor_texture);

        XcursorImageDestroy(cursor_image);
    }

    void Overlay::xi_grab_all_devices() {
        if(!xi_display)
            return;

        int num_devices = 0;
        XIDeviceInfo *info = XIQueryDevice(xi_display, XIAllDevices, &num_devices);
        if(!info)
            return;

        unsigned char mask[XIMaskLen(XI_LASTEVENT)];
        memset(mask, 0, sizeof(mask));
        XISetMask(mask, XI_Motion);
        //XISetMask(mask, XI_RawMotion);
        XISetMask(mask, XI_ButtonPress);
        XISetMask(mask, XI_ButtonRelease);
        XISetMask(mask, XI_KeyPress);
        XISetMask(mask, XI_KeyRelease);

        for (int i = 0; i < num_devices; ++i) {
            const XIDeviceInfo *dev = &info[i];
            XIEventMask xi_masks;
            xi_masks.deviceid = dev->deviceid;
            xi_masks.mask_len = sizeof(mask);
            xi_masks.mask = mask;
            XIGrabDevice(xi_display, dev->deviceid, window->get_system_handle(), CurrentTime, None, XIGrabModeAsync, XIGrabModeAsync, XIOwnerEvents, &xi_masks);
        }

        XFlush(xi_display);
        XIFreeDeviceInfo(info);
    }

    void Overlay::show() {
        if(visible)
            return;

        drawn_first_frame = false;
        window.reset();
        window = std::make_unique<mgl::Window>();
        deinit_theme();

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        const std::string wm_name = get_window_manager_name(display);
        const bool is_kwin = wm_name == "KWin";

        // The cursor position is wrong on wayland if an x11 window is not focused. On wayland we instead create a window and get the position where the wayland compositor puts it
        Window x11_cursor_window = None;
        const mgl::vec2i cursor_position = get_cursor_position(display, &x11_cursor_window);
        const mgl::vec2i monitor_position_query_value = (x11_cursor_window || gsr_info.system_info.display_server != DisplayServer::WAYLAND) ? cursor_position : create_window_get_center_position(display);

        // Wayland doesn't allow XGrabPointer/XGrabKeyboard when a wayland application is focused.
        // If the focused window is a wayland application then don't use override redirect and instead create
        // a fullscreen window for the ui.
        const bool prevent_game_minimizing = gsr_info.system_info.display_server != DisplayServer::WAYLAND || x11_cursor_window;

        window_size = { 32, 32 };
        window_pos = { 0, 0 };

        mgl::Window::CreateParams window_create_params;
        window_create_params.size = window_size;
        if(prevent_game_minimizing) {
            window_create_params.min_size = window_size;
            window_create_params.max_size = window_size;
        }
        window_create_params.position = window_pos;
        window_create_params.hidden = prevent_game_minimizing;
        window_create_params.override_redirect = prevent_game_minimizing;
        window_create_params.background_color = bg_color;
        window_create_params.support_alpha = true;
        window_create_params.hide_decorations = true;
        // MGL_WINDOW_TYPE_DIALOG is needed for kde plasma wayland in some cases, otherwise the window will pop up on another activity
        // or may not be visible at all
        window_create_params.window_type = (is_kwin && gsr_info.system_info.display_server == DisplayServer::WAYLAND) ? MGL_WINDOW_TYPE_DIALOG : MGL_WINDOW_TYPE_NORMAL;
        window_create_params.render_api = MGL_RENDER_API_EGL;

        if(!window->create("gsr ui", window_create_params))
            fprintf(stderr, "error: failed to create window\n");

        unsigned char data = 2; // Prefer being composed to allow transparency
        XChangeProperty(display, window->get_system_handle(), XInternAtom(display, "_NET_WM_BYPASS_COMPOSITOR", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

        data = 1;
        XChangeProperty(display, window->get_system_handle(), XInternAtom(display, "GAMESCOPE_EXTERNAL_OVERLAY", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

        if(!init_theme(resources_path)) {
            fprintf(stderr, "Error: failed to load theme\n");
            ::exit(1);
        }

        mgl_window *win = window->internal_window();
        if(win->num_monitors == 0) {
            fprintf(stderr, "gsr warning: no monitors found, not showing overlay\n");
            window.reset();
            return;
        }

        const mgl_monitor *focused_monitor = find_monitor_at_position(*window, monitor_position_query_value);
        window_pos = {focused_monitor->pos.x, focused_monitor->pos.y};
        window_size = {focused_monitor->size.x, focused_monitor->size.y};
        get_theme().set_window_size(window_size);

        if(prevent_game_minimizing) {
            window->set_size(window_size);
            window->set_size_limits(window_size, window_size);
            window->set_position(window_pos);
        }

        win->cursor_position.x = cursor_position.x - window_pos.x;
        win->cursor_position.y = cursor_position.y - window_pos.y;

        update_compositor_texture(focused_monitor);

        bg_screenshot_overlay = mgl::Rectangle(mgl::vec2f(get_theme().window_width, get_theme().window_height));
        top_bar_background = mgl::Rectangle(mgl::vec2f(get_theme().window_width, get_theme().window_height*0.06f).floor());
        top_bar_text = mgl::Text("GPU Screen Recorder", get_theme().top_bar_font);
        logo_sprite = mgl::Sprite(&get_theme().logo_texture);
        close_button_widget.set_size(mgl::vec2f(top_bar_background.get_size().y * 0.35f, top_bar_background.get_size().y * 0.35f).floor());

        bg_screenshot_overlay.set_color(bg_color);
        top_bar_background.set_color(mgl::Color(0, 0, 0, 180));
        //top_bar_text.set_color(get_color_theme().tint_color);
        top_bar_text.set_position((top_bar_background.get_position() + top_bar_background.get_size()*0.5f - top_bar_text.get_bounds().size*0.5f).floor());

        logo_sprite.set_height((int)(top_bar_background.get_size().y * 0.65f));
        logo_sprite.set_position(mgl::vec2f(
            (top_bar_background.get_size().y - logo_sprite.get_size().y) * 0.5f,
            top_bar_background.get_size().y * 0.5f - logo_sprite.get_size().y * 0.5f
        ).floor());

        close_button_widget.set_position(mgl::vec2f(get_theme().window_width - close_button_widget.get_size().x - logo_sprite.get_position().x, top_bar_background.get_size().y * 0.5f - close_button_widget.get_size().y * 0.5f).floor());

        while(!page_stack.empty()) {
            page_stack.pop();
        }

        auto front_page = std::make_unique<StaticPage>(window_size.to_vec2f());
        StaticPage *front_page_ptr = front_page.get();
        page_stack.push(std::move(front_page));

        const int button_height = window_size.y / 5.0f;
        const int button_width = button_height;

        auto main_buttons_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        List * main_buttons_list_ptr = main_buttons_list.get();
        main_buttons_list->set_spacing(0.0f);
        {
            auto button = std::make_unique<DropdownButton>(&get_theme().title_font, &get_theme().body_font, "Instant Replay", "Off", &get_theme().replay_button_texture,
                mgl::vec2f(button_width, button_height));
            replay_dropdown_button_ptr = button.get();
            button->add_item("Turn on", "start", "Alt+Shift+F10");
            button->add_item("Save", "save", "Alt+F10");
            button->add_item("Settings", "settings");
            button->set_item_icon("start", &get_theme().play_texture);
            button->set_item_icon("save", &get_theme().save_texture);
            button->set_item_icon("settings", &get_theme().settings_small_texture);
            button->on_click = [this](const std::string &id) {
                if(id == "settings") {
                    auto replay_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::REPLAY, &gsr_info, config, &page_stack);
                    replay_settings_page->on_config_changed = [this]() {
                        if(recording_status == RecordingStatus::REPLAY)
                            show_notification("Replay settings have been modified.\nYou may need to restart replay to apply the changes.", 5.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);
                    };
                    page_stack.push(std::move(replay_settings_page));
                } else if(id == "save") {
                    on_press_save_replay();
                } else if(id == "start") {
                    on_press_start_replay(false);
                }
            };
            main_buttons_list->add_widget(std::move(button));
        }
        {
            auto button = std::make_unique<DropdownButton>(&get_theme().title_font, &get_theme().body_font, "Record", "Not recording", &get_theme().record_button_texture,
                mgl::vec2f(button_width, button_height));
            record_dropdown_button_ptr = button.get();
            button->add_item("Start", "start", "Alt+F9");
            button->add_item("Pause", "pause", "Alt+F7");
            button->add_item("Settings", "settings");
            button->set_item_icon("start", &get_theme().play_texture);
            button->set_item_icon("pause", &get_theme().pause_texture);
            button->set_item_icon("settings", &get_theme().settings_small_texture);
            button->on_click = [this](const std::string &id) {
                if(id == "settings") {
                    auto record_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::RECORD, &gsr_info, config, &page_stack);
                    record_settings_page->on_config_changed = [this]() {
                        if(recording_status == RecordingStatus::RECORD)
                            show_notification("Recording settings have been modified.\nYou may need to restart recording to apply the changes.", 5.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);
                    };
                    page_stack.push(std::move(record_settings_page));
                } else if(id == "pause") {
                    toggle_pause();
                } else if(id == "start") {
                    on_press_start_record();
                }
            };
            main_buttons_list->add_widget(std::move(button));
        }
        {
            auto button = std::make_unique<DropdownButton>(&get_theme().title_font, &get_theme().body_font, "Livestream", "Not streaming", &get_theme().stream_button_texture,
                mgl::vec2f(button_width, button_height));
            stream_dropdown_button_ptr = button.get();
            button->add_item("Start", "start", "Alt+F8");
            button->add_item("Settings", "settings");
            button->set_item_icon("start", &get_theme().play_texture);
            button->set_item_icon("settings", &get_theme().settings_small_texture);
            button->on_click = [this](const std::string &id) {
                if(id == "settings") {
                    auto stream_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::STREAM, &gsr_info, config, &page_stack);
                    stream_settings_page->on_config_changed = [this]() {
                        if(recording_status == RecordingStatus::STREAM)
                            show_notification("Streaming settings have been modified.\nYou may need to restart streaming to apply the changes.", 5.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::STREAM);
                    };
                    page_stack.push(std::move(stream_settings_page));
                } else if(id == "start") {
                    on_press_start_stream();
                }
            };
            main_buttons_list->add_widget(std::move(button));
        }

        const mgl::vec2f main_buttons_list_size = main_buttons_list->get_size();
        main_buttons_list->set_position((mgl::vec2f(window_size.x * 0.5f, window_size.y * 0.25f) - main_buttons_list_size * 0.5f).floor());
        front_page_ptr->add_widget(std::move(main_buttons_list));

        {
            const mgl::vec2f main_buttons_size = main_buttons_list_ptr->get_size();
            const int settings_button_size = main_buttons_size.y * 0.2f;
            auto button = std::make_unique<Button>(&get_theme().title_font, "", mgl::vec2f(settings_button_size, settings_button_size), mgl::Color(0, 0, 0, 180));
            button->set_position((main_buttons_list_ptr->get_position() + main_buttons_size - mgl::vec2f(0.0f, settings_button_size) + mgl::vec2f(settings_button_size * 0.333f, 0.0f)).floor());
            button->set_bg_hover_color(mgl::Color(0, 0, 0, 255));
            button->set_icon(&get_theme().settings_small_texture);
            button->on_click = [&]() {
                auto settings_page = std::make_unique<GlobalSettingsPage>(&gsr_info, config, &page_stack);
                settings_page->on_startup_changed = [&](bool enable, int exit_status) {
                    if(exit_status == 0)
                        return;

                    if(exit_status == 127) {
                        if(enable)
                            show_notification("Failed to add GPU Screen Recorder to system startup.\nThis option only works on systems that use systemd.\nYou have to manually add \"gsr-ui\" to system startup on systems that uses another init system.", 10.0, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::NONE);
                    } else {
                        if(enable)
                            show_notification("Failed to add GPU Screen Recorder to system startup", 3.0, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::NONE);
                        else
                            show_notification("Failed to remove GPU Screen Recorder from system startup", 3.0, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::NONE);
                    }
                };
                settings_page->on_click_exit_program_button = [&](const char *reason) {
                    do_exit = true;
                    exit_reason = reason;
                };
                page_stack.push(std::move(settings_page));
            };
            front_page_ptr->add_widget(std::move(button));
        }

        close_button_widget.draw_handler = [&](mgl::Window &window, mgl::vec2f pos, mgl::vec2f size) {
            const int border_size = std::max(1.0f, 0.0015f * get_theme().window_height);
            const float padding_size = std::max(1.0f, 0.003f * get_theme().window_height);
            const mgl::vec2f padding(padding_size, padding_size);
            if(mgl::FloatRect(pos, size).contains(window.get_mouse_position().to_vec2f()))
                draw_rectangle_outline(window, pos.floor(), size.floor(), get_color_theme().tint_color, border_size);

            mgl::Sprite close_sprite(&get_theme().close_texture);
            close_sprite.set_position(pos + padding);
            close_sprite.set_size(size - padding * 2.0f);
            window.draw(close_sprite);
        };

        close_button_widget.event_handler = [&](mgl::Event &event, mgl::Window&, mgl::vec2f pos, mgl::vec2f size) {
            if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
                close_button_pressed_inside = mgl::FloatRect(pos, size).contains(mgl::vec2f(event.mouse_button.x, event.mouse_button.y));
            } else if(event.type == mgl::Event::MouseButtonReleased && event.mouse_button.button == mgl::Mouse::Left && close_button_pressed_inside) {
                if(mgl::FloatRect(pos, size).contains(mgl::vec2f(event.mouse_button.x, event.mouse_button.y))) {
                    while(!page_stack.empty()) {
                        page_stack.pop();
                    }
                    return false;
                }
            }
            return true;
        };

        // The focused application can be an xwayland application but the cursor can hover over a wayland application.
        // This is even the case when hovering over the titlebar of the xwayland application.
        if(prevent_game_minimizing)
            xi_setup();

        //window->set_fullscreen(true);
        if(gsr_info.system_info.display_server == DisplayServer::X11)
            make_window_click_through(display, window->get_system_handle());

        window->set_visible(true);

        make_window_sticky(display, window->get_system_handle());
        hide_window_from_taskbar(display, window->get_system_handle());

        if(default_cursor) {
            XFreeCursor(display, default_cursor);
            default_cursor = 0;
        }
        default_cursor = XCreateFontCursor(display, XC_left_ptr);
        XFlush(display);

        grab_mouse_and_keyboard();

        // The real cursor doesn't move when all devices are grabbed, so we create our own cursor and diplay that while grabbed
        xi_setup_fake_cursor();

        // We want to grab all devices to prevent any other application below the UI from receiving events.
        // Owlboy seems to use xi events and XGrabPointer doesn't prevent owlboy from receiving events.
        xi_grab_all_devices();

        // if(gsr_info.system_info.display_server == DisplayServer::WAYLAND) {
        //     set_focused_window(display, window->get_system_handle());
        //     XFlush(display);
        // }

        window->set_fullscreen(true);

        visible = true;

        if(gpu_screen_recorder_process > 0) {
            switch(recording_status) {
                case RecordingStatus::NONE:
                    break;
                case RecordingStatus::REPLAY:
                    update_ui_replay_started();
                    break;
                case RecordingStatus::RECORD:
                    update_ui_recording_started();
                    break;
                case RecordingStatus::STREAM:
                    update_ui_streaming_started();
                    break;
            }
        }

        if(paused)
            update_ui_recording_paused();
    }

    void Overlay::hide() {
        if(!visible)
            return;

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        while(!page_stack.empty()) {
            page_stack.pop();
        }

        if(default_cursor) {
            XFreeCursor(display, default_cursor);
            default_cursor = 0;
        }

        XUngrabKeyboard(display, CurrentTime);
        XUngrabPointer(display, CurrentTime);
        XFlush(display);

        if(xi_display) {
            cursor_texture.clear();
            cursor_sprite.set_texture(nullptr);
        }

        window_texture_deinit(&window_texture);
        window_texture_sprite.set_texture(nullptr);
        screenshot_texture.clear();
        screenshot_sprite.set_texture(nullptr);

        visible = false;
        drawn_first_frame = false;

        if(xi_input_xev) {
            free(xi_input_xev);
            xi_input_xev = nullptr;
        }

        if(xi_output_xev) {
            free(xi_output_xev);
            xi_output_xev = nullptr;
        }

        if(xi_display) {
            XCloseDisplay(xi_display);
            xi_display = nullptr;

            if(window) {
                mgl_context *context = mgl_get_context();
                Display *display = (Display*)context->connection;

                const mgl::vec2i new_cursor_position = mgl::vec2i(window->internal_window()->pos.x, window->internal_window()->pos.y) + window->get_mouse_position();
                XWarpPointer(display, DefaultRootWindow(display), DefaultRootWindow(display), 0, 0, 0, 0, new_cursor_position.x, new_cursor_position.y);
                XFlush(display);

                XFixesShowCursor(display, DefaultRootWindow(display));
                XFlush(display);
            }
        }

        if(window) {
            window->set_visible(false);
            window.reset();
        }

        deinit_theme();
    }

    void Overlay::toggle_show() {
        if(visible) {
            //hide();
            // We dont want to hide immediately because hide is called in mgl event callback, in which it destroys the mgl window.
            // Instead remove all pages and wait until next iteration to close the UI (which happens when there are no pages to render).
            while(!page_stack.empty()) {
                page_stack.pop();
            }
        } else {
            show();
        }
    }

    void Overlay::toggle_record() {
        on_press_start_record();
    }

    void Overlay::toggle_pause() {
        if(recording_status != RecordingStatus::RECORD || gpu_screen_recorder_process <= 0)
            return;

        if(paused) {
            update_ui_recording_unpaused();
            show_notification("Recording has been unpaused", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);
        } else {
            update_ui_recording_paused();
            show_notification("Recording has been paused", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);
        }

        kill(gpu_screen_recorder_process, SIGUSR2);
        paused = !paused;
    }

    void Overlay::toggle_stream() {
        on_press_start_stream();
    }

    void Overlay::toggle_replay() {
        on_press_start_replay(false);
    }

    void Overlay::save_replay() {
        on_press_save_replay();
    }

    static const char* notification_type_to_string(NotificationType notification_type) {
        switch(notification_type) {
            case NotificationType::NONE:   return nullptr;
            case NotificationType::RECORD: return "record";
            case NotificationType::REPLAY: return "replay";
            case NotificationType::STREAM: return "stream";
        }
        return nullptr;
    }

    void Overlay::show_notification(const char *str, double timeout_seconds, mgl::Color icon_color, mgl::Color bg_color, NotificationType notification_type) {
        char timeout_seconds_str[32];
        snprintf(timeout_seconds_str, sizeof(timeout_seconds_str), "%f", timeout_seconds);

        const std::string icon_color_str = color_to_hex_str(icon_color);
        const std::string bg_color_str = color_to_hex_str(bg_color);
        const char *notification_args[12] = {
            "gsr-notify", "--text", str, "--timeout", timeout_seconds_str,
            "--icon-color", icon_color_str.c_str(), "--bg-color", bg_color_str.c_str(),
        };

        const char *notification_type_str = notification_type_to_string(notification_type);
        if(notification_type_str) {
            notification_args[9] = "--icon";
            notification_args[10] = notification_type_str;
            notification_args[11] = nullptr;
        } else {
            notification_args[9] = nullptr;
        }

        if(notification_process > 0) {
            kill(notification_process, SIGKILL);
            int status = 0;
            waitpid(notification_process, &status, 0);
        }

        notification_process = exec_program(notification_args, NULL);
    }

    bool Overlay::is_open() const {
        return visible;
    }

    bool Overlay::should_exit(std::string &reason) const {
        reason.clear();
        if(do_exit)
            reason = exit_reason;
        return do_exit;
    }

    void Overlay::exit() {
        do_exit = true;
    }

    const Config& Overlay::get_config() const {
        return config;
    }

    void Overlay::update_notification_process_status() {
        if(notification_process <= 0)
            return;

        int status;
        if(waitpid(notification_process, &status, WNOHANG) == 0) {
            // Still running
            return;
        }

        notification_process = -1;
    }

    static void string_replace_characters(char *str, const char *characters_to_replace, char new_character) {
        for(; *str != '\0'; ++str) {
            for(const char *p = characters_to_replace; *p != '\0'; ++p) {
                if(*str == *p)
                    *str = new_character;
            }
        }
    }

    static std::string filepath_get_directory(const char *filepath) {
        std::string result = filepath;
        const size_t last_slash_index = result.rfind('/');
        if(last_slash_index == std::string::npos)
            result = ".";
        else
            result.erase(last_slash_index);
        return result;
    }

    static std::string filepath_get_filename(const char *filepath) {
        std::string result = filepath;
        const size_t last_slash_index = result.rfind('/');
        if(last_slash_index != std::string::npos)
            result.erase(0, last_slash_index + 1);
        return result;
    }

    static void truncate_string(std::string &str, int max_length) {
        if((int)str.size() > max_length)
            str.replace(str.begin() + max_length, str.end(), "...");
    }

    void Overlay::save_video_in_current_game_directory(const char *video_filepath, NotificationType notification_type) {
        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;
        const std::string video_filename = filepath_get_filename(video_filepath);

        std::string focused_window_name = get_focused_window_name(display, WindowCaptureType::FOCUSED);
        if(focused_window_name.empty())
            focused_window_name = "Game";

        string_replace_characters(focused_window_name.data(), "/\\", '_');

        std::string video_directory = filepath_get_directory(video_filepath) + "/" + focused_window_name;
        create_directory_recursive(video_directory.data());

        const std::string new_video_filepath = video_directory + "/" + video_filename;
        rename(video_filepath, new_video_filepath.c_str());

        truncate_string(focused_window_name, 20);
        std::string text;
        switch(notification_type) {
            case NotificationType::RECORD: {
                if(!config.record_config.show_video_saved_notifications)
                    return;
                text = "Saved recording to '" + focused_window_name + "/" + video_filename + "'";
                break;
            }
            case NotificationType::REPLAY: {
                if(!config.replay_config.show_replay_saved_notifications)
                    return;
                text = "Saved replay to '" + focused_window_name + "/" + video_filename + "'";
                break;
            }
            case NotificationType::NONE:
            case NotificationType::STREAM:
                break;
        }
        show_notification(text.c_str(), 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, notification_type);
    }

    void Overlay::update_gsr_replay_save() {
        if(gpu_screen_recorder_process_output_file) {
            char buffer[1024];
            char *replay_saved_filepath = fgets(buffer, sizeof(buffer), gpu_screen_recorder_process_output_file);
            if(!replay_saved_filepath || replay_saved_filepath[0] == '\0')
                return;

            const int line_len = strlen(replay_saved_filepath);
            if(replay_saved_filepath[line_len - 1] == '\n')
                replay_saved_filepath[line_len - 1] = '\0';

            if(config.replay_config.save_video_in_game_folder) {
                save_video_in_current_game_directory(replay_saved_filepath, NotificationType::REPLAY);
            } else {
                const std::string text = "Saved replay to '" + filepath_get_filename(replay_saved_filepath) + "'";
                show_notification(text.c_str(), 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);
            }
        } else if(gpu_screen_recorder_process_output_fd > 0) {
            char buffer[1024];
            read(gpu_screen_recorder_process_output_fd, buffer, sizeof(buffer));
        }
    }

    void Overlay::update_gsr_process_status() {
        if(gpu_screen_recorder_process <= 0)
            return;

        int status;
        if(waitpid(gpu_screen_recorder_process, &status, WNOHANG) == 0) {
            // Still running
            return;
        }

        close_gpu_screen_recorder_output();

        int exit_code = -1;
        if(WIFEXITED(status))
            exit_code = WEXITSTATUS(status);

        switch(recording_status) {
            case RecordingStatus::NONE:
                break;
            case RecordingStatus::REPLAY: {
                update_ui_replay_stopped();
                if(exit_code == 0) {
                    if(config.replay_config.show_replay_stopped_notifications)
                        show_notification("Replay stopped", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);
                } else {
                    fprintf(stderr, "Warning: gpu-screen-recorder (%d) exited with exit status %d\n", (int)gpu_screen_recorder_process, exit_code);
                    show_notification("Replay stopped because of an error. Verify if settings are correct", 3.0, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::REPLAY);
                }
                break;
            }
            case RecordingStatus::RECORD: {
                update_ui_recording_stopped();
                on_stop_recording(exit_code);
                break;
            }
            case RecordingStatus::STREAM: {
                update_ui_streaming_stopped();
                if(exit_code == 0) {
                    if(config.streaming_config.show_streaming_stopped_notifications)
                        show_notification("Streaming has stopped", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::STREAM);
                } else {
                    fprintf(stderr, "Warning: gpu-screen-recorder (%d) exited with exit status %d\n", (int)gpu_screen_recorder_process, exit_code);
                    show_notification("Streaming stopped because of an error. Verify if settings are correct", 3.0, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::STREAM);
                }
                break;
            }
        }

        gpu_screen_recorder_process = -1;
        recording_status = RecordingStatus::NONE;
    }

    void Overlay::replay_status_update_status() {
        if(replay_status_update_clock.get_elapsed_time_seconds() < replay_status_update_check_timeout_seconds)
            return;

        replay_status_update_clock.restart();
        update_focused_fullscreen_status();
        update_power_supply_status();
    }

    void Overlay::update_focused_fullscreen_status() {
        if(config.replay_config.turn_on_replay_automatically_mode != "turn_on_at_fullscreen")
            return;

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        const Window focused_window = get_focused_window(display, WindowCaptureType::FOCUSED);
        if(window && focused_window == window->get_system_handle())
            return;

        const bool prev_focused_window_is_fullscreen = focused_window_is_fullscreen;
        focused_window_is_fullscreen = focused_window != 0 && window_is_fullscreen(display, focused_window);
        if(focused_window_is_fullscreen != prev_focused_window_is_fullscreen) {
            if(recording_status == RecordingStatus::NONE && focused_window_is_fullscreen)
                on_press_start_replay(false);
            else if(recording_status == RecordingStatus::REPLAY && !focused_window_is_fullscreen)
                on_press_start_replay(true);
        }
    }

    // TODO: Instead of checking power supply status periodically listen to power supply event
    void Overlay::update_power_supply_status() {
        if(config.replay_config.turn_on_replay_automatically_mode != "turn_on_at_power_supply_connected")
            return;

        const bool prev_power_supply_status = power_supply_connected;
        power_supply_connected = power_supply_online_filepath.empty() || power_supply_is_connected(power_supply_online_filepath.c_str());
        if(power_supply_connected != prev_power_supply_status) {
            if(recording_status == RecordingStatus::NONE && power_supply_connected)
                on_press_start_replay(false);
            else if(recording_status == RecordingStatus::REPLAY && !power_supply_connected)
                on_press_start_replay(false);
        }
    }

    void Overlay::on_stop_recording(int exit_code) {
        if(exit_code == 0) {
            if(config.record_config.save_video_in_game_folder) {
                save_video_in_current_game_directory(record_filepath.c_str(), NotificationType::RECORD);
            } else {
                const std::string text = "Saved recording to '" + filepath_get_filename(record_filepath.c_str()) + "'";
                show_notification(text.c_str(), 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);
            }
        } else {
            fprintf(stderr, "Warning: gpu-screen-recorder (%d) exited with exit status %d\n", (int)gpu_screen_recorder_process, exit_code);
            show_notification("Failed to start/save recording. Verify if settings are correct", 3.0, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::RECORD);
        }
    }

    void Overlay::update_ui_recording_paused() {
        if(!visible || recording_status != RecordingStatus::RECORD)
            return;

        record_dropdown_button_ptr->set_description("Paused");
        record_dropdown_button_ptr->set_item_label("pause", "Unpause");
        record_dropdown_button_ptr->set_item_icon("pause", &get_theme().play_texture);
    }

    void Overlay::update_ui_recording_unpaused() {
        if(!visible || recording_status != RecordingStatus::RECORD)
            return;

        record_dropdown_button_ptr->set_description("Recording");
        record_dropdown_button_ptr->set_item_label("pause", "Pause");
        record_dropdown_button_ptr->set_item_icon("pause", &get_theme().pause_texture);
    }

    void Overlay::update_ui_recording_started() {
        if(!visible)
            return;

        record_dropdown_button_ptr->set_item_label("start", "Stop and save");
        record_dropdown_button_ptr->set_activated(true);
        record_dropdown_button_ptr->set_description("Recording");
        record_dropdown_button_ptr->set_item_icon("start", &get_theme().stop_texture);
    }

    void Overlay::update_ui_recording_stopped() {
        if(!visible)
            return;

        record_dropdown_button_ptr->set_item_label("start", "Start");
        record_dropdown_button_ptr->set_activated(false);
        record_dropdown_button_ptr->set_description("Not recording");
        record_dropdown_button_ptr->set_item_icon("start", &get_theme().play_texture);

        record_dropdown_button_ptr->set_item_label("pause", "Pause");
        record_dropdown_button_ptr->set_item_icon("pause", &get_theme().pause_texture);
        paused = false;
    }

    void Overlay::update_ui_streaming_started() {
        if(!visible)
            return;

        stream_dropdown_button_ptr->set_item_label("start", "Stop");
        stream_dropdown_button_ptr->set_activated(true);
        stream_dropdown_button_ptr->set_description("Streaming");
        stream_dropdown_button_ptr->set_item_icon("start", &get_theme().stop_texture);
    }

    void Overlay::update_ui_streaming_stopped() {
        if(!visible)
            return;

        stream_dropdown_button_ptr->set_item_label("start", "Start");
        stream_dropdown_button_ptr->set_activated(false);
        stream_dropdown_button_ptr->set_description("Not streaming");
        stream_dropdown_button_ptr->set_item_icon("start", &get_theme().play_texture);
    }

    void Overlay::update_ui_replay_started() {
        if(!visible)
            return;

        replay_dropdown_button_ptr->set_item_label("start", "Turn off");
        replay_dropdown_button_ptr->set_activated(true);
        replay_dropdown_button_ptr->set_description("On");
        replay_dropdown_button_ptr->set_item_icon("start", &get_theme().stop_texture);
    }

    void Overlay::update_ui_replay_stopped() {
        if(!visible)
            return;

        replay_dropdown_button_ptr->set_item_label("start", "Turn on");
        replay_dropdown_button_ptr->set_activated(false);
        replay_dropdown_button_ptr->set_description("Off");
        replay_dropdown_button_ptr->set_item_icon("start", &get_theme().play_texture);
    }

    static std::string get_date_str() {
        char str[128];
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        strftime(str, sizeof(str)-1, "%Y-%m-%d_%H-%M-%S", t);
        return str;
    }

    static std::string container_to_file_extension(const std::string &container) {
        if(container == "matroska")
            return "mkv";
        else if(container == "mpegts")
            return "ts";
        else if(container == "hls")
            return "m3u8";
        else
            return container;
    }

    static bool starts_with(std::string_view str, const char *substr) {
        size_t len = strlen(substr);
        return str.size() >= len && memcmp(str.data(), substr, len) == 0;
    }

    static std::vector<std::string> create_audio_tracks_real_names(const std::vector<std::string> &audio_tracks, bool application_audio_invert, const GsrInfo &gsr_info) {
        std::vector<std::string> result;
        for(const std::string &audio_track : audio_tracks) {
            std::string audio_track_name = audio_track;
            const bool is_app_audio = starts_with(audio_track_name, "app:");
            if(is_app_audio && !gsr_info.system_info.supports_app_audio)
                continue;

            if(is_app_audio && application_audio_invert)
                audio_track_name.replace(0, 4, "app-inverse:");

            result.push_back(std::move(audio_track_name));
        }
        return result;
    }

    static std::string merge_audio_tracks(const std::vector<std::string> &audio_tracks) {
        std::string result;
        for(size_t i = 0; i < audio_tracks.size(); ++i) {
            if(i > 0)
                result += "|";
            result += audio_tracks[i];
        }
        return result;
    }

    static void add_common_gpu_screen_recorder_args(std::vector<const char*> &args, const RecordOptions &record_options, const std::vector<std::string> &audio_tracks, const std::string &video_bitrate, const char *region, const std::string &audio_devices_merged) {
        if(record_options.video_quality == "custom") {
            args.push_back("-bm");
            args.push_back("cbr");
            args.push_back("-q");
            args.push_back(video_bitrate.c_str());
        } else {
            args.push_back("-q");
            args.push_back(record_options.video_quality.c_str());
        }

        if(record_options.record_area_option == "focused" || record_options.change_video_resolution) {
            args.push_back("-s");
            args.push_back(region);
        }

        if(record_options.merge_audio_tracks) {
            if(!audio_devices_merged.empty()) {
                args.push_back("-a");
                args.push_back(audio_devices_merged.c_str());
            }
        } else {
            for(const std::string &audio_track : audio_tracks) {
                args.push_back("-a");
                args.push_back(audio_track.c_str());
            }
        }

        if(record_options.restore_portal_session) {
            args.push_back("-restore-portal-session");
            args.push_back("yes");
        }
    }

    static bool validate_capture_target(const GsrInfo &gsr_info, const std::string &capture_target) {
        const SupportedCaptureOptions capture_options = get_supported_capture_options(gsr_info);
        // TODO: Also check x11 window when enabled (check if capture_target is a decminal/hex number)
        if(capture_target == "focused") {
            return capture_options.focused;
        } else if(capture_target == "portal") {
            return capture_options.portal;
        } else {
            for(const GsrMonitor &monitor : capture_options.monitors) {
                if(capture_target == monitor.name)
                    return true;
            }
            return false;
        }
    }

    void Overlay::on_press_save_replay() {
        if(recording_status != RecordingStatus::REPLAY || gpu_screen_recorder_process <= 0)
            return;

        kill(gpu_screen_recorder_process, SIGUSR1);
    }

    void Overlay::on_press_start_replay(bool disable_notification) {
        switch(recording_status) {
            case RecordingStatus::NONE:
            case RecordingStatus::REPLAY:
                break;
            case RecordingStatus::RECORD:
                show_notification("Unable to start replay when recording.\nStop recording before starting replay.", 5.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);
                return;
            case RecordingStatus::STREAM:
                show_notification("Unable to start replay when streaming.\nStop streaming before starting replay.", 5.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::STREAM);
                return;
        }

        paused = false;

        // window->close();
        // usleep(1000 * 50); // 50 milliseconds

        close_gpu_screen_recorder_output();

        if(gpu_screen_recorder_process > 0) {
            kill(gpu_screen_recorder_process, SIGINT);
            int status;
            if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            }

            gpu_screen_recorder_process = -1;
            recording_status = RecordingStatus::NONE;
            update_ui_replay_stopped();

            // TODO: Show this with a slight delay to make sure it doesn't show up in the video
            if(!disable_notification && config.replay_config.show_replay_stopped_notifications)
                show_notification("Replay stopped", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);
            return;
        }

        if(!validate_capture_target(gsr_info, config.replay_config.record_options.record_area_option)) {
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Failed to start replay, capture target \"%s\" is invalid. Please change capture target in settings", config.replay_config.record_options.record_area_option.c_str());
            show_notification(err_msg, 3.0, mgl::Color(255, 0, 0, 0), mgl::Color(255, 0, 0, 0), NotificationType::REPLAY);
            return;
        }

        // TODO: Validate input, fallback to valid values
        const std::string fps = std::to_string(config.replay_config.record_options.fps);
        const std::string video_bitrate = std::to_string(config.replay_config.record_options.video_bitrate);
        const std::string output_directory = config.replay_config.save_directory;
        const std::vector<std::string> audio_tracks = create_audio_tracks_real_names(config.replay_config.record_options.audio_tracks, config.replay_config.record_options.application_audio_invert, gsr_info);
        const std::string audio_tracks_merged = merge_audio_tracks(audio_tracks);
        const std::string framerate_mode = config.replay_config.record_options.framerate_mode == "auto" ? "vfr" : config.replay_config.record_options.framerate_mode;
        const std::string replay_time = std::to_string(config.replay_config.replay_time);
        const char *video_codec = config.replay_config.record_options.video_codec.c_str();
        const char *encoder = "gpu";
        if(strcmp(video_codec, "h264_software") == 0) {
            video_codec = "h264";
            encoder = "cpu";
        }

        char region[64];
        region[0] = '\0';
        if(config.replay_config.record_options.record_area_option == "focused")
            snprintf(region, sizeof(region), "%dx%d", (int)config.replay_config.record_options.record_area_width, (int)config.replay_config.record_options.record_area_height);

        if(config.replay_config.record_options.record_area_option != "focused" && config.replay_config.record_options.change_video_resolution)
            snprintf(region, sizeof(region), "%dx%d", (int)config.replay_config.record_options.video_width, (int)config.replay_config.record_options.video_height);

        std::vector<const char*> args = {
            "gpu-screen-recorder", "-w", config.replay_config.record_options.record_area_option.c_str(),
            "-c", config.replay_config.container.c_str(),
            "-ac", config.replay_config.record_options.audio_codec.c_str(),
            "-cursor", config.replay_config.record_options.record_cursor ? "yes" : "no",
            "-cr", config.replay_config.record_options.color_range.c_str(),
            "-fm", framerate_mode.c_str(),
            "-k", video_codec,
            "-encoder", encoder,
            "-f", fps.c_str(),
            "-r", replay_time.c_str(),
            "-v", "no",
            "-o", output_directory.c_str()
        };

        add_common_gpu_screen_recorder_args(args, config.replay_config.record_options, audio_tracks, video_bitrate, region, audio_tracks_merged);

        args.push_back(nullptr);

        gpu_screen_recorder_process = exec_program(args.data(), &gpu_screen_recorder_process_output_fd);
        if(gpu_screen_recorder_process == -1) {
            // TODO: Show notification failed to start
        } else {
            recording_status = RecordingStatus::REPLAY;
            update_ui_replay_started();
        }

        const int fdl = fcntl(gpu_screen_recorder_process_output_fd, F_GETFL);
        fcntl(gpu_screen_recorder_process_output_fd, F_SETFL, fdl | O_NONBLOCK);
        gpu_screen_recorder_process_output_file = fdopen(gpu_screen_recorder_process_output_fd, "r");
        if(gpu_screen_recorder_process_output_file)
            gpu_screen_recorder_process_output_fd = -1;

        // TODO: Start recording after this notification has disappeared to make sure it doesn't show up in the video.
        // Make clear to the user that the recording starts after the notification is gone.
        // Maybe have the option in notification to show timer until its getting hidden, then the notification can say:
        // Starting recording in 3...
        // 2...
        // 1...
        // TODO: Do not run this is a daemon. Instead get the pid and when launching another notification close the current notification
        // program and start another one. This can also be used to check when the notification has finished by checking with waitpid NOWAIT
        // to see when the program has exit.
        if(!disable_notification && config.replay_config.show_replay_started_notifications)
            show_notification("Replay has started", 3.0, get_color_theme().tint_color, get_color_theme().tint_color, NotificationType::REPLAY);
    }

    void Overlay::on_press_start_record() {
        switch(recording_status) {
            case RecordingStatus::NONE:
            case RecordingStatus::RECORD:
                break;
            case RecordingStatus::REPLAY:
                show_notification("Unable to start recording when replay is turned on.\nTurn off replay before starting recording.", 5.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);
                return;
            case RecordingStatus::STREAM:
                show_notification("Unable to start recording when streaming.\nStop streaming before starting recording.", 5.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::STREAM);
                return;
        }

        paused = false;

        // window->close();
        // usleep(1000 * 50); // 50 milliseconds

        if(gpu_screen_recorder_process > 0) {
            kill(gpu_screen_recorder_process, SIGINT);
            int status;
            if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            } else {
                int exit_code = -1;
                if(WIFEXITED(status))
                    exit_code = WEXITSTATUS(status);
                on_stop_recording(exit_code);
            }

            gpu_screen_recorder_process = -1;
            recording_status = RecordingStatus::NONE;
            update_ui_recording_stopped();
            record_filepath.clear();
            return;
        }

        if(!validate_capture_target(gsr_info, config.record_config.record_options.record_area_option)) {
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Failed to start recording, capture target \"%s\" is invalid. Please change capture target in settings", config.record_config.record_options.record_area_option.c_str());
            show_notification(err_msg, 3.0, mgl::Color(255, 0, 0, 0), mgl::Color(255, 0, 0, 0), NotificationType::RECORD);
            return;
        }

        record_filepath.clear();

        // TODO: Validate input, fallback to valid values
        const std::string fps = std::to_string(config.record_config.record_options.fps);
        const std::string video_bitrate = std::to_string(config.record_config.record_options.video_bitrate);
        const std::string output_file = config.record_config.save_directory + "/Video_" + get_date_str() + "." + container_to_file_extension(config.record_config.container.c_str());
        const std::vector<std::string> audio_tracks = create_audio_tracks_real_names(config.record_config.record_options.audio_tracks, config.record_config.record_options.application_audio_invert, gsr_info);
        const std::string audio_tracks_merged = merge_audio_tracks(audio_tracks);
        const std::string framerate_mode = config.record_config.record_options.framerate_mode == "auto" ? "vfr" : config.record_config.record_options.framerate_mode;
        const char *video_codec = config.record_config.record_options.video_codec.c_str();
        const char *encoder = "gpu";
        if(strcmp(video_codec, "h264_software") == 0) {
            video_codec = "h264";
            encoder = "cpu";
        }

        char region[64];
        region[0] = '\0';
        if(config.record_config.record_options.record_area_option == "focused")
            snprintf(region, sizeof(region), "%dx%d", (int)config.record_config.record_options.record_area_width, (int)config.record_config.record_options.record_area_height);

        if(config.record_config.record_options.record_area_option != "focused" && config.record_config.record_options.change_video_resolution)
            snprintf(region, sizeof(region), "%dx%d", (int)config.record_config.record_options.video_width, (int)config.record_config.record_options.video_height);

        std::vector<const char*> args = {
            "gpu-screen-recorder", "-w", config.record_config.record_options.record_area_option.c_str(),
            "-c", config.record_config.container.c_str(),
            "-ac", config.record_config.record_options.audio_codec.c_str(),
            "-cursor", config.record_config.record_options.record_cursor ? "yes" : "no",
            "-cr", config.record_config.record_options.color_range.c_str(),
            "-fm", framerate_mode.c_str(),
            "-k", video_codec,
            "-encoder", encoder,
            "-f", fps.c_str(),
            "-v", "no",
            "-o", output_file.c_str()
        };

        add_common_gpu_screen_recorder_args(args, config.record_config.record_options, audio_tracks, video_bitrate, region, audio_tracks_merged);

        args.push_back(nullptr);

        record_filepath = output_file;
        gpu_screen_recorder_process = exec_program(args.data(), nullptr);
        if(gpu_screen_recorder_process == -1) {
            // TODO: Show notification failed to start
        } else {
            recording_status = RecordingStatus::RECORD;
            update_ui_recording_started();
        }

        // TODO: Start recording after this notification has disappeared to make sure it doesn't show up in the video.
        // Make clear to the user that the recording starts after the notification is gone.
        // Maybe have the option in notification to show timer until its getting hidden, then the notification can say:
        // Starting recording in 3...
        // 2...
        // 1...
        if(config.record_config.show_recording_started_notifications)
            show_notification("Recording has started", 3.0, get_color_theme().tint_color, get_color_theme().tint_color, NotificationType::RECORD);
    }

    static std::string streaming_get_url(const Config &config) {
        std::string url;
        if(config.streaming_config.streaming_service == "twitch") {
            url += "rtmp://live.twitch.tv/app/";
            url += config.streaming_config.twitch.stream_key;
        } else if(config.streaming_config.streaming_service == "youtube") {
            url += "rtmp://a.rtmp.youtube.com/live2/";
            url += config.streaming_config.youtube.stream_key;
        } else if(config.streaming_config.streaming_service == "custom") {
            url = config.streaming_config.custom.url;
            if(url.size() >= 7 && strncmp(url.c_str(), "rtmp://", 7) == 0)
            {}
            else if(url.size() >= 8 && strncmp(url.c_str(), "rtmps://", 8) == 0)
            {}
            else if(url.size() >= 7 && strncmp(url.c_str(), "rtsp://", 7) == 0)
            {}
            else if(url.size() >= 6 && strncmp(url.c_str(), "srt://", 6) == 0)
            {}
            else if(url.size() >= 7 && strncmp(url.c_str(), "http://", 7) == 0)
            {}
            else if(url.size() >= 8 && strncmp(url.c_str(), "https://", 8) == 0)
            {}
            else if(url.size() >= 6 && strncmp(url.c_str(), "tcp://", 6) == 0)
            {}
            else if(url.size() >= 6 && strncmp(url.c_str(), "udp://", 6) == 0)
            {}
            else
                url = "rtmp://" + url;
        }
        return url;
    }

    void Overlay::on_press_start_stream() {
        switch(recording_status) {
            case RecordingStatus::NONE:
            case RecordingStatus::STREAM:
                break;
            case RecordingStatus::REPLAY:
                show_notification("Unable to start streaming when replay is turned on.\nTurn off replay before starting streaming.", 5.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);
                return;
            case RecordingStatus::RECORD:
                show_notification("Unable to start streaming when recording.\nStop recording before starting streaming.", 5.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);
                return;
        }

        paused = false;

        // window->close();
        // usleep(1000 * 50); // 50 milliseconds

        if(gpu_screen_recorder_process > 0) {
            kill(gpu_screen_recorder_process, SIGINT);
            int status;
            if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            }

            gpu_screen_recorder_process = -1;
            recording_status = RecordingStatus::NONE;
            update_ui_streaming_stopped();

            // TODO: Show this with a slight delay to make sure it doesn't show up in the video
            if(config.streaming_config.show_streaming_stopped_notifications)
                show_notification("Streaming has stopped", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::STREAM);
            return;
        }

        if(!validate_capture_target(gsr_info, config.streaming_config.record_options.record_area_option)) {
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Failed to start streaming, capture target \"%s\" is invalid. Please change capture target in settings", config.streaming_config.record_options.record_area_option.c_str());
            show_notification(err_msg, 3.0, mgl::Color(255, 0, 0, 0), mgl::Color(255, 0, 0, 0), NotificationType::STREAM);
            return;
        }

        // TODO: Validate input, fallback to valid values
        const std::string fps = std::to_string(config.streaming_config.record_options.fps);
        const std::string video_bitrate = std::to_string(config.streaming_config.record_options.video_bitrate);
        const std::vector<std::string> audio_tracks = create_audio_tracks_real_names(config.streaming_config.record_options.audio_tracks, config.streaming_config.record_options.application_audio_invert, gsr_info);
        const std::string audio_tracks_merged = merge_audio_tracks(audio_tracks);
        const std::string framerate_mode = config.streaming_config.record_options.framerate_mode == "auto" ? "vfr" : config.streaming_config.record_options.framerate_mode;
        const char *video_codec = config.streaming_config.record_options.video_codec.c_str();
        const char *encoder = "gpu";
        if(strcmp(video_codec, "h264_software") == 0) {
            video_codec = "h264";
            encoder = "cpu";
        }

        std::string container = "flv";
        if(config.streaming_config.streaming_service == "custom")
            container = config.streaming_config.custom.container;

        const std::string url = streaming_get_url(config);

        char region[64];
        region[0] = '\0';
        if(config.record_config.record_options.record_area_option == "focused")
            snprintf(region, sizeof(region), "%dx%d", (int)config.streaming_config.record_options.record_area_width, (int)config.streaming_config.record_options.record_area_height);

        if(config.record_config.record_options.record_area_option != "focused" && config.streaming_config.record_options.change_video_resolution)
            snprintf(region, sizeof(region), "%dx%d", (int)config.streaming_config.record_options.video_width, (int)config.streaming_config.record_options.video_height);

        std::vector<const char*> args = {
            "gpu-screen-recorder", "-w", config.streaming_config.record_options.record_area_option.c_str(),
            "-c", container.c_str(),
            "-ac", config.streaming_config.record_options.audio_codec.c_str(),
            "-cursor", config.streaming_config.record_options.record_cursor ? "yes" : "no",
            "-cr", config.streaming_config.record_options.color_range.c_str(),
            "-fm", framerate_mode.c_str(),
            "-encoder", encoder,
            "-f", fps.c_str(),
            "-v", "no",
            "-o", url.c_str()
        };

        config.streaming_config.record_options.merge_audio_tracks = true;
        add_common_gpu_screen_recorder_args(args, config.streaming_config.record_options, audio_tracks, video_bitrate, region, audio_tracks_merged);

        args.push_back(nullptr);

        gpu_screen_recorder_process = exec_program(args.data(), nullptr);
        if(gpu_screen_recorder_process == -1) {
            // TODO: Show notification failed to start
        } else {
            recording_status = RecordingStatus::STREAM;
            update_ui_streaming_started();
        }

        // TODO: Start recording after this notification has disappeared to make sure it doesn't show up in the video.
        // Make clear to the user that the recording starts after the notification is gone.
        // Maybe have the option in notification to show timer until its getting hidden, then the notification can say:
        // Starting recording in 3...
        // 2...
        // 1...
        // TODO: Do not run this is a daemon. Instead get the pid and when launching another notification close the current notification
        // program and start another one. This can also be used to check when the notification has finished by checking with waitpid NOWAIT
        // to see when the program has exit.
        if(config.streaming_config.show_streaming_started_notifications)
            show_notification("Streaming has started", 3.0, get_color_theme().tint_color, get_color_theme().tint_color, NotificationType::STREAM);
    }

    bool Overlay::update_compositor_texture(const mgl_monitor *monitor) {
        window_texture_deinit(&window_texture);
        window_texture_sprite.set_texture(nullptr);
        screenshot_texture.clear();
        screenshot_sprite.set_texture(nullptr);

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        if(gsr_info.system_info.display_server != DisplayServer::X11 || is_compositor_running(display, 0))
            return false;

        bool window_texture_loaded = false;
        Window focused_window = get_focused_window(display, WindowCaptureType::CURSOR);
        if(!focused_window)
            focused_window = get_focused_window(display, WindowCaptureType::FOCUSED);
        if(focused_window && is_window_fullscreen_on_monitor(display, focused_window, monitor))
            window_texture_loaded = window_texture_init(&window_texture, display, mgl_window_get_egl_display(window->internal_window()), focused_window, egl_funcs) == 0;

        if(window_texture_loaded && window_texture.texture_id) {
            window_texture_texture = mgl::Texture(window_texture.texture_id, MGL_TEXTURE_FORMAT_RGB);
            window_texture_sprite.set_texture(&window_texture_texture);
        } else {
            XImage *img = XGetImage(display, DefaultRootWindow(display), monitor->pos.x, monitor->pos.y, monitor->size.x, monitor->size.y, AllPlanes, ZPixmap);
            if(!img)
                fprintf(stderr, "Error: failed to take a screenshot\n");

            if(img) {
                screenshot_texture = texture_from_ximage(img);
                if(screenshot_texture.is_valid())
                    screenshot_sprite.set_texture(&screenshot_texture);
                XDestroyImage(img);
                img = NULL;
            }
        }

        return true;
    }

    void Overlay::force_window_on_top() {
        if(force_window_on_top_clock.get_elapsed_time_seconds() >= force_window_on_top_timeout_seconds) {
            force_window_on_top_clock.restart();

            mgl_context *context = mgl_get_context();
            Display *display = (Display*)context->connection;
            XRaiseWindow(display, window->get_system_handle());
            XFlush(display);
        }
    }
}