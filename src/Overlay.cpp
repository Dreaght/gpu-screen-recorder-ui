#include "../include/Overlay.hpp"
#include "../include/Theme.hpp"
#include "../include/Config.hpp"
#include "../include/Process.hpp"
#include "../include/Utils.hpp"
#include "../include/gui/StaticPage.hpp"
#include "../include/gui/DropdownButton.hpp"
#include "../include/gui/CustomRendererWidget.hpp"
#include "../include/gui/SettingsPage.hpp"
#include "../include/gui/ScreenshotSettingsPage.hpp"
#include "../include/gui/GlobalSettingsPage.hpp"
#include "../include/gui/Utils.hpp"
#include "../include/KwinWorkaround.hpp"
#include "../include/HyprlandWorkaround.hpp"
#include "../include/gui/PageStack.hpp"
#include "../include/WindowUtils.hpp"
#include "../include/GlobalHotkeys/GlobalHotkeys.hpp"
#include "../include/GlobalHotkeys/GlobalHotkeysLinux.hpp"
#include "../include/CursorTracker/CursorTrackerX11.hpp"
#include "../include/CursorTracker/CursorTrackerWayland.hpp"

#include <iomanip>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>
#include <limits.h>
#include <fcntl.h>
#include <poll.h>
#include <malloc.h>
#include <stdexcept>
#include <algorithm>
#include <inttypes.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/shapeconst.h>
#include <X11/Xcursor/Xcursor.h>

#include <wayland-client.h>

#include <mglpp/system/Rect.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/Utf8.hpp>

extern "C" {
#include <mgl/mgl.h>
}

namespace gsr {
    static const mgl::Color bg_color(0, 0, 0, 100);
    static const double force_window_on_top_timeout_seconds = 1.0;
    static const double replay_status_update_check_timeout_seconds = 1.5;
    static const double replay_saving_notification_timeout_seconds = 0.5;
    static const double short_notification_timeout_seconds = 2.0;
    static const double notification_timeout_seconds = 3.0;
    static const double notification_error_timeout_seconds = 5.0;
    static const double cursor_tracker_update_timeout_sec = 0.1;

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

    static bool is_window_fullscreen_on_monitor(Display *display, Window window, const Monitor &monitor) {
        if(!window)
            return false;

        DrawableGeometry geometry;
        if(!get_drawable_geometry(display, window, &geometry))
            return false;

        const int margin = 2;
        return diff_int(geometry.x, monitor.position.x, margin) && diff_int(geometry.y, monitor.position.y, margin)
            && diff_int(geometry.width, monitor.size.x, margin) && diff_int(geometry.height, monitor.size.y, margin);
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

    static const Monitor* find_monitor_at_position(const std::vector<Monitor> &monitors, mgl::vec2i pos) {
        for(const Monitor &monitor : monitors) {
            if(mgl::IntRect(monitor.position, monitor.size).contains(pos))
                return &monitor;
        }
        return nullptr;
    }

    static const Monitor* find_monitor_by_name(const std::vector<Monitor> &monitors, const std::string &name) {
        for(const Monitor &monitor : monitors) {
            if(monitor.name == name)
                return &monitor;
        }
        return nullptr;
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

    static bool is_hyprland_waybar_running_as_dock() {
        const char *args[] = { "hyprctl", "layers", nullptr };
        std::string stdout_str;
        if(exec_program_on_host_get_stdout(args, stdout_str) != 0)
            return false;

        int waybar_layer_level = -1;
        int current_layer_level = 0;
        string_split_char(stdout_str, '\n', [&](const std::string_view line) {
            if(line.find("Layer level 0") != std::string_view::npos)
                current_layer_level = 0;
            else if(line.find("Layer level 1") != std::string_view::npos)
                current_layer_level = 1;
            else if(line.find("Layer level 2") != std::string_view::npos)
                current_layer_level = 2;
            else if(line.find("Layer level 3") != std::string_view::npos)
                current_layer_level = 3;
            else if(line.find("namespace: waybar") != std::string_view::npos) {
                waybar_layer_level = current_layer_level;
                return false;
            }
            return true;
        });

        return waybar_layer_level >= 0 && waybar_layer_level <= 1;
    }

    static Hotkey config_hotkey_to_hotkey(ConfigHotkey config_hotkey) {
        return {
            (uint32_t)mgl::Keyboard::key_to_x11_keysym((mgl::Keyboard::Key)config_hotkey.key),
            config_hotkey.modifiers
        };
    }

    static void bind_linux_hotkeys(GlobalHotkeysLinux *global_hotkeys, Overlay *overlay) {
        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().main_config.show_hide_hotkey),
            "toggle_show", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->toggle_show();
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().record_config.start_stop_hotkey),
            "record", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->toggle_record(RecordForceType::NONE);
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().record_config.pause_unpause_hotkey),
            "pause", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->toggle_pause();
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().record_config.start_stop_region_hotkey),
            "record_region", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->toggle_record(RecordForceType::REGION);
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().record_config.start_stop_window_hotkey),
            "record_window", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->toggle_record(RecordForceType::WINDOW);
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().streaming_config.start_stop_hotkey),
            "stream", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->toggle_stream();
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().replay_config.start_stop_hotkey),
            "replay_start", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->toggle_replay();
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().replay_config.save_hotkey),
            "replay_save", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->save_replay();
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().replay_config.save_1_min_hotkey),
            "replay_save_1_min", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->save_replay_1_min();
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().replay_config.save_10_min_hotkey),
            "replay_save_10_min", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->save_replay_10_min();
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().screenshot_config.take_screenshot_hotkey),
            "take_screenshot", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->take_screenshot();
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().screenshot_config.take_screenshot_region_hotkey),
            "take_screenshot_region", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->take_screenshot_region();
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(overlay->get_config().screenshot_config.take_screenshot_window_hotkey),
            "take_screenshot_window", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->take_screenshot_window();
            });

        global_hotkeys->bind_key_press(
            config_hotkey_to_hotkey(ConfigHotkey{ mgl::Keyboard::Key::Escape, HOTKEY_MOD_LCTRL | HOTKEY_MOD_LSHIFT | HOTKEY_MOD_LALT }),
            "exit", [overlay](const std::string &id) {
                fprintf(stderr, "pressed %s\n", id.c_str());
                overlay->go_back_to_old_ui();
            });
    }

    static std::unique_ptr<GlobalHotkeysLinux> register_linux_hotkeys(Overlay *overlay, GlobalHotkeysLinux::GrabType grab_type) {
        auto global_hotkeys = std::make_unique<GlobalHotkeysLinux>(grab_type);
        if(!global_hotkeys->start())
            fprintf(stderr, "error: failed to start global hotkeys\n");

        bind_linux_hotkeys(global_hotkeys.get(), overlay);
        global_hotkeys->on_gsr_ui_virtual_keyboard_grabbed = [overlay]() {
            overlay->global_hotkeys_ungrab_keyboard = true;
        };
        return global_hotkeys;
    }

    static std::unique_ptr<GlobalHotkeysJoystick> register_joystick_hotkeys(Overlay *overlay) {
        auto global_hotkeys_js = std::make_unique<GlobalHotkeysJoystick>();
        if(!global_hotkeys_js->start())
            fprintf(stderr, "Warning: failed to start joystick hotkeys\n");

        global_hotkeys_js->bind_action("toggle_show", [overlay](const std::string &id) {
            fprintf(stderr, "pressed %s\n", id.c_str());
            overlay->toggle_show();
        });

        global_hotkeys_js->bind_action("save_replay", [overlay](const std::string &id) {
            fprintf(stderr, "pressed %s\n", id.c_str());
            overlay->save_replay();
        });

        global_hotkeys_js->bind_action("save_1_min_replay", [overlay](const std::string &id) {
            fprintf(stderr, "pressed %s\n", id.c_str());
            overlay->save_replay_1_min();
        });

        global_hotkeys_js->bind_action("save_10_min_replay", [overlay](const std::string &id) {
            fprintf(stderr, "pressed %s\n", id.c_str());
            overlay->save_replay_10_min();
        });

        global_hotkeys_js->bind_action("take_screenshot", [overlay](const std::string &id) {
            fprintf(stderr, "pressed %s\n", id.c_str());
            overlay->take_screenshot();
        });

        global_hotkeys_js->bind_action("toggle_record", [overlay](const std::string &id) {
            fprintf(stderr, "pressed %s\n", id.c_str());
            overlay->toggle_record(RecordForceType::NONE);
        });

        global_hotkeys_js->bind_action("toggle_replay", [overlay](const std::string &id) {
            fprintf(stderr, "pressed %s\n", id.c_str());
            overlay->toggle_replay();
        });

        return global_hotkeys_js;
    }

    static NotificationSpeed to_notification_speed(const std::string &notification_speed_str) {
        if(notification_speed_str == "normal")
            return NotificationSpeed::NORMAL;
        else if(notification_speed_str == "fast")
            return NotificationSpeed::FAST;
        else {
            assert(false);
            return NotificationSpeed::NORMAL;
        }
    }

    Overlay::Overlay(std::string resources_path, GsrInfo gsr_info, SupportedCaptureOptions capture_options, egl_functions egl_funcs) :
        resources_path(std::move(resources_path)),
        gsr_info(std::move(gsr_info)),
        egl_funcs(egl_funcs),
        config(capture_options),
        current_recording_config(capture_options),
        bg_screenshot_overlay({0.0f, 0.0f}),
        top_bar_background({0.0f, 0.0f}),
        close_button_widget({0.0f, 0.0f})
    {
        if(this->gsr_info.system_info.display_server == DisplayServer::WAYLAND) {
            wayland_dpy = wl_display_connect(nullptr);
            if(!wayland_dpy)
                fprintf(stderr, "Warning: failed to connect to the wayland server\n");
        } else {
            wayland_dpy = nullptr;
        }

        gsr_icon_path = this->resources_path + "images/gpu_screen_recorder_logo.png";

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
        replay_startup_mode = replay_startup_string_to_type(config.replay_config.turn_on_replay_automatically_mode.c_str());
        set_notification_speed(to_notification_speed(config.main_config.notification_speed));

        if(config.main_config.hotkeys_enable_option == "enable_hotkeys")
            global_hotkeys = register_linux_hotkeys(this, GlobalHotkeysLinux::GrabType::ALL);
        else if(config.main_config.hotkeys_enable_option == "enable_hotkeys_virtual_devices")
            global_hotkeys = register_linux_hotkeys(this, GlobalHotkeysLinux::GrabType::VIRTUAL);
        else if(config.main_config.hotkeys_enable_option == "enable_hotkeys_no_grab")
            global_hotkeys = register_linux_hotkeys(this, GlobalHotkeysLinux::GrabType::NO_GRAB);

        if(config.main_config.joystick_hotkeys_enable_option == "enable_hotkeys")
            global_hotkeys_js = register_joystick_hotkeys(this);

        x11_dpy = XOpenDisplay(nullptr);
        if(x11_dpy)
            XKeysymToKeycode(x11_dpy, XK_F1); // If we dont call we will never get a MappingNotify
        else
            fprintf(stderr, "Warning: XOpenDisplay failed to mapping notify\n");

        if(this->gsr_info.system_info.display_server == DisplayServer::X11)
            cursor_tracker = std::make_unique<CursorTrackerX11>((Display*)mgl_get_context()->connection);
        else if(this->gsr_info.system_info.display_server == DisplayServer::WAYLAND) {
            if(!this->gsr_info.gpu_info.card_path.empty())
                cursor_tracker = std::make_unique<CursorTrackerWayland>(this->gsr_info.gpu_info.card_path.c_str(), wayland_dpy);

            if(!config.main_config.wayland_warning_shown) {
                config.main_config.wayland_warning_shown = true;
                save_config(config);
                show_notification("Wayland doesn't support GPU Screen Recorder UI properly,\nthings may not work as expected. Use X11 if you experience issues.", notification_error_timeout_seconds, mgl::Color(255, 255, 255), mgl::Color(255, 0, 0), NotificationType::NOTICE, nullptr, NotificationLevel::ERROR);
            }

            const std::string wm_name = get_window_manager_name(x11_dpy);

            if (wm_name.find("Hyprland") != std::string::npos) {
                start_hyprland_listener_thread();
            }

            if (wm_name == "KWin") {
                start_kwin_helper_thread();
            }
        }

        update_led_indicator_after_settings_change();
    }

    Overlay::~Overlay() {
        hide();

        if(notification_process > 0) {
            kill(notification_process, SIGINT);
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

        if(gpu_screen_recorder_screenshot_process > 0) {
            kill(gpu_screen_recorder_screenshot_process, SIGINT);
            int status;
            if(waitpid(gpu_screen_recorder_screenshot_process, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            }
            gpu_screen_recorder_screenshot_process = -1;
        }

        led_indicator.reset();

        close_gpu_screen_recorder_output();
        deinit_color_theme();

        if(x11_dpy)
            XCloseDisplay(x11_dpy);

        if(wayland_dpy)
            wl_display_disconnect(wayland_dpy);
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
                    xi_output_xev->xmotion.window = (Window)window->get_system_handle();
                    xi_output_xev->xmotion.subwindow = (Window)window->get_system_handle();
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
                    xi_output_xev->xbutton.window = (Window)window->get_system_handle();
                    xi_output_xev->xbutton.subwindow = (Window)window->get_system_handle();
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
                    xi_output_xev->xkey.window = (Window)window->get_system_handle();
                    xi_output_xev->xkey.subwindow = (Window)window->get_system_handle();
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

    void Overlay::handle_keyboard_mapping_event() {
        if(!x11_dpy)
            return;

        bool mapping_updated = false;
        while(XPending(x11_dpy)) {
            XNextEvent(x11_dpy, &x11_mapping_xev);
            if(x11_mapping_xev.type == MappingNotify) {
                XRefreshKeyboardMapping(&x11_mapping_xev.xmapping);
                mapping_updated = true;
            }
        }

        if(mapping_updated)
            rebind_all_keyboard_hotkeys();
    }

    void Overlay::handle_events() {
        if(led_indicator)
            led_indicator->update();

        if(global_hotkeys_ungrab_keyboard) {
            global_hotkeys_ungrab_keyboard = false;
            show_notification(
                "Some keyboard remapping software conflicts with GPU Screen Recorder on your system.\n"
                "Keyboards have been ungrabbed, applications will now receive the hotkeys you press."
                , 7.0, mgl::Color(255, 255, 255), mgl::Color(255, 0, 0), NotificationType::NOTICE, nullptr, NotificationLevel::ERROR);

            config.main_config.hotkeys_enable_option = "enable_hotkeys_no_grab";
            save_config(config);
            recreate_global_hotkeys("enable_hotkeys_no_grab");
        }

        if(global_hotkeys)
            global_hotkeys->poll_events();

        if(global_hotkeys_js)
            global_hotkeys_js->poll_events();

        if(cursor_tracker_update_clock.get_elapsed_time_seconds() >= cursor_tracker_update_timeout_sec) {
            cursor_tracker_update_clock.restart();
            if(cursor_tracker)
                cursor_tracker->update();
        }

        handle_keyboard_mapping_event();
        region_selector.poll_events();
        if(region_selector.take_canceled()) {
            on_region_selected = nullptr;
        } else if(region_selector.take_selection() && on_region_selected) {
            on_region_selected();
            on_region_selected = nullptr;
        }

        window_selector.poll_events();
        if(window_selector.take_canceled()) {
            on_window_selected = nullptr;
        } else if(window_selector.take_selection() && on_window_selected) {
            mgl_context *context = mgl_get_context();
            Display *display = (Display*)context->connection;

            const Window selected_window = window_selector.get_selection();
            if(selected_window && selected_window != DefaultRootWindow(display)) {
                on_window_selected();
            } else {
                show_notification("No window selected", notification_timeout_seconds, mgl::Color(255, 255, 255), mgl::Color(255, 0, 0), NotificationType::NOTICE, nullptr, NotificationLevel::ERROR);
            }
            on_window_selected = nullptr;
        }

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
        remove_widgets_to_be_removed();

        update_notification_process_status();
        process_gsr_output();
        update_gsr_process_status();
        update_gsr_screenshot_process_status();
        replay_status_update_status();

        if(hide_ui) {
            hide_ui = false;
            hide();
            return false;
        }

        if(start_region_capture) {
            start_region_capture = false;
            hide();
            if(!region_selector.start(get_color_theme().tint_color)) {
                show_notification("Failed to start region capture", notification_error_timeout_seconds, mgl::Color(255, 255, 255), mgl::Color(255, 0, 0), NotificationType::NOTICE, nullptr, NotificationLevel::ERROR);
                on_region_selected = nullptr;
            }
        }

        if(start_window_capture) {
            start_window_capture = false;
            hide();
            if(!window_selector.start(get_color_theme().tint_color)) {
                show_notification("Failed to start window capture", notification_error_timeout_seconds, mgl::Color(255, 255, 255), mgl::Color(255, 0, 0), NotificationType::NOTICE, nullptr, NotificationLevel::ERROR);
                on_window_selected = nullptr;
            }
        }

        if(region_selector.is_started() || window_selector.is_started()) {
            usleep(5 * 1000); // 5 ms
            return true;
        }

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

        const bool draw_ui = show_overlay_clock.get_elapsed_time_seconds() >= show_overlay_timeout_seconds;

        window->clear(draw_ui ? bg_color : mgl::Color(0, 0, 0, 0));

        if(draw_ui) {
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
            draw_tooltip(*window);

            if(cursor_texture.is_valid()) {
                cursor_sprite.set_position((window->get_mouse_position() - cursor_hotspot).to_vec2f());
                window->draw(cursor_sprite);
            }

            if(!drawn_first_frame) {
                drawn_first_frame = true;
                mgl::Event event;
                event.type = mgl::Event::MouseMoved;
                event.mouse_move.x = window->get_mouse_position().x;
                event.mouse_move.y = window->get_mouse_position().y;
                on_event(event);
            }
        }

        window->display();

        return true;
    }

    void Overlay::grab_mouse_and_keyboard() {
        // TODO: Remove these grabs when debugging with a debugger, or your X11 session will appear frozen.
        // There should be a debug mode to not use these
        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;
        XGrabPointer(display, (Window)window->get_system_handle(), True,
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
            Button1MotionMask | Button2MotionMask | Button3MotionMask | Button4MotionMask | Button5MotionMask |
            ButtonMotionMask,
            GrabModeAsync, GrabModeAsync, None, default_cursor, CurrentTime);
        // TODO: This breaks global hotkeys (when using x11 global hotkeys)
        XGrabKeyboard(display, (Window)window->get_system_handle(), True, GrabModeAsync, GrabModeAsync, CurrentTime);
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

        XcursorImage *cursor_image = nullptr;
        for(int cursor_size_test : {cursor_size, 24}) {
            for(const char *cursor_theme_test : {cursor_theme, "default", "Adwaita"}) {
                for(unsigned int shape : {XC_left_ptr, XC_arrow}) {
                    cursor_image = XcursorShapeLoadImage(shape, cursor_theme_test, cursor_size_test);
                    if(cursor_image)
                        goto done;
                }
            }
        }

        done:
        if(!cursor_image) {
            fprintf(stderr, "Error: failed to get cursor, loading bundled default cursor instead\n");
            const std::string default_cursor_path = resources_path + "images/default.cur";
            for(int cursor_size_test : {cursor_size, 24}) {
                cursor_image = XcursorFilenameLoadImage(default_cursor_path.c_str(), cursor_size_test);
                if(cursor_image)
                    break;
            }
        }

        if(!cursor_image) {
            fprintf(stderr, "Error: failed to get cursor\n");
            XFixesShowCursor(xi_display, DefaultRootWindow(xi_display));
            XFlush(xi_display);
            return;
        }

        bool cursor_visible = false;
        texture_from_x11_cursor(cursor_image, &cursor_visible, &cursor_hotspot, cursor_texture);
        if(cursor_texture.is_valid())
            cursor_sprite.set_texture(&cursor_texture);

        XcursorImageDestroy(cursor_image);
    }

    void Overlay::show() {
        if(visible)
            return;

        if(region_selector.is_started() || window_selector.is_started())
            return;

        drawn_first_frame = false;
        window.reset();
        window = std::make_unique<mgl::Window>();
        deinit_theme();

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        const std::vector<Monitor> monitors = get_monitors(display);
        if(monitors.empty()) {
            fprintf(stderr, "gsr warning: no monitors found, not showing overlay\n");
            window.reset();
            return;
        }

        const std::string wm_name = get_window_manager_name(display);
        const bool is_kwin = wm_name == "KWin";
        const bool is_wlroots = wm_name.find("wlroots") != std::string::npos;
        const bool is_hyprland = wm_name.find("Hyprland") != std::string::npos;
        //const bool is_smithay = wm_name.find("Smithay") != std::string::npos;
        const bool hyprland_waybar_is_dock = is_hyprland && is_hyprland_waybar_running_as_dock();

        std::optional<CursorInfo> cursor_info;
        if(cursor_tracker) {
            cursor_tracker->update();
            cursor_info = cursor_tracker->get_latest_cursor_info();
        }

        // The cursor position is wrong on wayland if an x11 window is not focused. On wayland we instead create a window and get the position where the wayland compositor puts it
        Window x11_cursor_window = None;
        mgl::vec2i cursor_position = get_cursor_position(display, &x11_cursor_window);
        const Monitor *focused_monitor = nullptr;
        if(cursor_info) {
            focused_monitor = find_monitor_by_name(monitors, cursor_info->monitor_name);
            if(!focused_monitor)
                focused_monitor = &monitors.front();
            cursor_position = cursor_info->position;
        } else {
            const mgl::vec2i monitor_position_query_value = (x11_cursor_window || gsr_info.system_info.display_server != DisplayServer::WAYLAND) ? cursor_position : create_window_get_center_position(display);
            focused_monitor = find_monitor_at_position(monitors, monitor_position_query_value);
            if(!focused_monitor)
                focused_monitor = &monitors.front();
        }

        // Wayland doesn't allow XGrabPointer/XGrabKeyboard when a wayland application is focused.
        // If the focused window is a wayland application then don't use override redirect and instead create
        // a fullscreen window for the ui.
        const bool prevent_game_minimizing = gsr_info.system_info.display_server != DisplayServer::WAYLAND
            || (x11_cursor_window && is_window_fullscreen_on_monitor(display, x11_cursor_window, *focused_monitor) && get_focused_window(display, WindowCaptureType::FOCUSED, false) == x11_cursor_window)
            || is_wlroots
            || is_hyprland;

        if(prevent_game_minimizing) {
            window_pos = focused_monitor->position;
            window_size = focused_monitor->size;
        } else {
            window_pos = {0, 0};
            window_size = focused_monitor->size / 2;
        }

        mgl::Window::CreateParams window_create_params;
        window_create_params.size = window_size;
        if(prevent_game_minimizing) {
            window_create_params.min_size = window_size;
            window_create_params.max_size = window_size;
        }
        window_create_params.position = focused_monitor->position + focused_monitor->size / 2 - window_size / 2;
        window_create_params.hidden = prevent_game_minimizing;
        window_create_params.override_redirect = prevent_game_minimizing;
        window_create_params.background_color = mgl::Color(0, 0, 0, 0);
        window_create_params.support_alpha = true;
        window_create_params.hide_decorations = true;
        // MGL_WINDOW_TYPE_DIALOG is needed for kde plasma wayland in some cases, otherwise the window will pop up on another activity
        // or may not be visible at all
        window_create_params.window_type = (is_kwin && gsr_info.system_info.display_server == DisplayServer::WAYLAND) ? MGL_WINDOW_TYPE_DIALOG : MGL_WINDOW_TYPE_NORMAL;
        // Nvidia + Wayland + Egl doesn't work on some systems properly and it instead falls back to software rendering.
        // Use Glx on Wayland to workaround this issue. This is fine since Egl is only needed for x11 to reliably get the texture of the fullscreen window on Nvidia
        // when a compositor isn't running.
        window_create_params.graphics_api = gsr_info.system_info.display_server == DisplayServer::WAYLAND ? MGL_GRAPHICS_API_GLX : MGL_GRAPHICS_API_EGL;
        window_create_params.class_name = "gsr-ui";

        if(!window->create("gsr ui", window_create_params)) {
            fprintf(stderr, "error: failed to create window\n");
            window.reset();
            return;
        }
        //window->set_low_latency(true);

        unsigned char data = 2; // Prefer being composed to allow transparency
        XChangeProperty(display, (Window)window->get_system_handle(), XInternAtom(display, "_NET_WM_BYPASS_COMPOSITOR", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

        data = 1;
        XChangeProperty(display, (Window)window->get_system_handle(), XInternAtom(display, "GAMESCOPE_EXTERNAL_OVERLAY", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

        const auto original_window_size = window_size;
        window_pos = focused_monitor->position;
        window_size = focused_monitor->size;
        if(!init_theme(resources_path)) {
            fprintf(stderr, "Error: failed to load theme\n");
            window.reset();
            return;
        }
        get_theme().set_window_size(window_size);

        if(prevent_game_minimizing) {
            window->set_size(window_size);
            window->set_size_limits(window_size, window_size);
        }
        window->set_position(focused_monitor->position + focused_monitor->size / 2 - original_window_size / 2);

        mgl_window *win = window->internal_window();
        win->cursor_position.x = cursor_position.x - window_pos.x;
        win->cursor_position.y = cursor_position.y - window_pos.y;

        update_compositor_texture(*focused_monitor);

        create_frontpage_ui_components();

        // The focused application can be an xwayland application but the cursor can hover over a wayland application.
        // This is even the case when hovering over the titlebar of the xwayland application.
        const bool fake_cursor = is_wlroots ? x11_cursor_window != None : prevent_game_minimizing;
        if(fake_cursor)
            xi_setup();

        //window->set_fullscreen(true);
        if(gsr_info.system_info.display_server == DisplayServer::X11)
            make_window_click_through(display, (Window)window->get_system_handle());

        window->set_visible(true);

        make_window_sticky(display, (Window)window->get_system_handle());
        hide_window_from_taskbar(display, (Window)window->get_system_handle());

        if(default_cursor) {
            XFreeCursor(display, default_cursor);
            default_cursor = 0;
        }
        default_cursor = XCreateFontCursor(display, XC_left_ptr);
        XFlush(display);

        grab_mouse_and_keyboard();

        // The real cursor doesn't move when all devices are grabbed, so we create our own cursor and diplay that while grabbed
        cursor_hotspot = {0, 0};
        xi_setup_fake_cursor();
        if(cursor_info && gsr_info.system_info.display_server == DisplayServer::WAYLAND) {
            win->cursor_position.x += cursor_hotspot.x;
            win->cursor_position.y += cursor_hotspot.y;
        }

        // We want to grab all devices to prevent any other application below the UI from receiving events.
        // Owlboy seems to use xi events and XGrabPointer doesn't prevent owlboy from receiving events.
        xi_grab_all_mouse_devices(xi_display);

        if(!is_wlroots && !hyprland_waybar_is_dock)
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

        if(replay_recording)
            update_ui_recording_started();

        // Wayland compositors have retarded fullscreen animations that we cant disable in a proper way
        // without messing up window position.
        show_overlay_timeout_seconds = prevent_game_minimizing ? 0.0 : 0.15;
        show_overlay_clock.restart();
        draw();
    }

    void Overlay::recreate_global_hotkeys(const char *hotkey_option) {
        global_hotkeys.reset();
        if(strcmp(hotkey_option, "enable_hotkeys") == 0)
            global_hotkeys = register_linux_hotkeys(this, GlobalHotkeysLinux::GrabType::ALL);
        else if(strcmp(hotkey_option, "enable_hotkeys_virtual_devices") == 0)
            global_hotkeys = register_linux_hotkeys(this, GlobalHotkeysLinux::GrabType::VIRTUAL);
        else if(strcmp(hotkey_option, "enable_hotkeys_no_grab") == 0)
            global_hotkeys = register_linux_hotkeys(this, GlobalHotkeysLinux::GrabType::NO_GRAB);
        else if(strcmp(hotkey_option, "disable_hotkeys") == 0)
            global_hotkeys.reset();
    }

    void Overlay::update_led_indicator_after_settings_change() {
        if(config.record_config.record_options.use_led_indicator || config.replay_config.record_options.use_led_indicator || config.streaming_config.record_options.use_led_indicator || config.screenshot_config.use_led_indicator) {
            if(!led_indicator)
                led_indicator = std::make_unique<LedIndicator>();
        } else {
            led_indicator.reset();
        }
    }

    void Overlay::create_frontpage_ui_components() {
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
            button->add_item("Turn on", "start", config.replay_config.start_stop_hotkey.to_string(false, false));
            button->add_item("Save", "save", config.replay_config.save_hotkey.to_string(false, false));
            if(gsr_info.system_info.gsr_version >= GsrVersion{5, 4, 0}) {
                button->add_item("Save 1 min", "save_1_min", config.replay_config.save_1_min_hotkey.to_string(false, false));
                button->add_item("Save 10 min", "save_10_min", config.replay_config.save_10_min_hotkey.to_string(false, false));
            }
            button->add_item("Settings", "settings");
            button->set_item_icon("start", &get_theme().play_texture);
            button->set_item_icon("save", &get_theme().save_texture);
            button->set_item_icon("save_1_min", &get_theme().save_texture);
            button->set_item_icon("save_10_min", &get_theme().save_texture);
            button->set_item_icon("settings", &get_theme().settings_extra_small_texture);
            button->on_click = [this](const std::string &id) {
                if(id == "settings") {
                    auto replay_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::REPLAY, &gsr_info, config, &page_stack);
                    replay_settings_page->on_config_changed = [this]() {
                        replay_startup_mode = replay_startup_string_to_type(config.replay_config.turn_on_replay_automatically_mode.c_str());
                        if(recording_status == RecordingStatus::REPLAY)
                            show_notification("Replay settings have been modified.\nYou may need to restart replay to apply the changes.", notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);
                    };
                    page_stack.push(std::move(replay_settings_page));
                } else if(id == "save") {
                    on_press_save_replay();
                } else if(id == "save_1_min") {
                    on_press_save_replay_1_min_replay();
                } else if(id == "save_10_min") {
                    on_press_save_replay_10_min_replay();
                } else if(id == "start") {
                    on_press_start_replay(false, false);
                }
            };
            button->set_item_enabled("save", false);
            button->set_item_enabled("save_1_min", false);
            button->set_item_enabled("save_10_min", false);
            main_buttons_list->add_widget(std::move(button));
        }
        {
            auto button = std::make_unique<DropdownButton>(&get_theme().title_font, &get_theme().body_font, "Record", "Not recording", &get_theme().record_button_texture,
                mgl::vec2f(button_width, button_height));
            record_dropdown_button_ptr = button.get();
            button->add_item("Start", "start", config.record_config.start_stop_hotkey.to_string(false, false));
            button->add_item("Pause", "pause", config.record_config.pause_unpause_hotkey.to_string(false, false));
            button->add_item("Settings", "settings");
            button->set_item_icon("start", &get_theme().play_texture);
            button->set_item_icon("pause", &get_theme().pause_texture);
            button->set_item_icon("settings", &get_theme().settings_extra_small_texture);
            button->on_click = [this](const std::string &id) {
                if(id == "settings") {
                    auto record_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::RECORD, &gsr_info, config, &page_stack);
                    record_settings_page->on_config_changed = [this]() {
                        if(recording_status == RecordingStatus::RECORD)
                            show_notification("Recording settings have been modified.\nYou may need to restart recording to apply the changes.", notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);

                        update_led_indicator_after_settings_change();
                    };
                    page_stack.push(std::move(record_settings_page));
                } else if(id == "pause") {
                    toggle_pause();
                } else if(id == "start") {
                    on_press_start_record(false, RecordForceType::NONE);
                }
            };
            button->set_item_enabled("pause", false);
            main_buttons_list->add_widget(std::move(button));
        }
        {
            auto button = std::make_unique<DropdownButton>(&get_theme().title_font, &get_theme().body_font, "Livestream", "Not streaming", &get_theme().stream_button_texture,
                mgl::vec2f(button_width, button_height));
            stream_dropdown_button_ptr = button.get();
            button->add_item("Start", "start", config.streaming_config.start_stop_hotkey.to_string(false, false));
            button->add_item("Settings", "settings");
            button->set_item_icon("start", &get_theme().play_texture);
            button->set_item_icon("settings", &get_theme().settings_extra_small_texture);
            button->on_click = [this](const std::string &id) {
                if(id == "settings") {
                    auto stream_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::STREAM, &gsr_info, config, &page_stack);
                    stream_settings_page->on_config_changed = [this]() {
                        if(recording_status == RecordingStatus::STREAM)
                            show_notification("Streaming settings have been modified.\nYou may need to restart streaming to apply the changes.", notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::STREAM);

                        update_led_indicator_after_settings_change();
                    };
                    page_stack.push(std::move(stream_settings_page));
                } else if(id == "start") {
                    on_press_start_stream(false);
                }
            };
            main_buttons_list->add_widget(std::move(button));
        }

        const mgl::vec2f main_buttons_list_size = main_buttons_list->get_size();
        main_buttons_list->set_position((mgl::vec2f(window_size.x * 0.5f, window_size.y * 0.25f) - main_buttons_list_size * 0.5f).floor());
        front_page_ptr->add_widget(std::move(main_buttons_list));

        {
            const mgl::vec2f main_buttons_size = main_buttons_list_ptr->get_size();
            const int settings_button_size = main_buttons_size.y * 0.33f;
            auto button = std::make_unique<Button>(&get_theme().title_font, "", mgl::vec2f(settings_button_size, settings_button_size), mgl::Color(0, 0, 0, 180));
            button->set_position((main_buttons_list_ptr->get_position() + main_buttons_size - mgl::vec2f(0.0f, settings_button_size) + mgl::vec2f(settings_button_size * 0.333f, 0.0f)).floor());
            button->set_bg_hover_color(mgl::Color(0, 0, 0, 255));
            button->set_icon(&get_theme().settings_small_texture);
            button->on_click = [&]() {
                auto settings_page = std::make_unique<GlobalSettingsPage>(this, &gsr_info, config, &page_stack);

                settings_page->on_startup_changed = [&](bool enable, int exit_status) {
                    if(exit_status == 0)
                        return;

                    if(exit_status == 127) {
                        if(enable)
                            show_notification("Failed to add GPU Screen Recorder to system startup.\nThis option only works on systems that use systemd.\nYou have to manually add \"gsr-ui\" to system startup on systems that uses another init system.", 7.0, mgl::Color(255, 255, 255), mgl::Color(255, 0, 0), NotificationType::NOTICE, nullptr, NotificationLevel::ERROR);
                    } else {
                        if(enable)
                            show_notification("Failed to add GPU Screen Recorder to system startup", notification_timeout_seconds, mgl::Color(255, 255, 255), mgl::Color(255, 0, 0), NotificationType::NOTICE, nullptr, NotificationLevel::ERROR);
                        else
                            show_notification("Failed to remove GPU Screen Recorder from system startup", notification_timeout_seconds, mgl::Color(255, 255, 255), mgl::Color(255, 0, 0), NotificationType::NOTICE, nullptr, NotificationLevel::ERROR);
                    }
                };

                settings_page->on_click_exit_program_button = [this](const char *reason) {
                    do_exit = true;
                    exit_reason = reason;
                };

                settings_page->on_keyboard_hotkey_changed = [this](const char *hotkey_option) {
                    recreate_global_hotkeys(hotkey_option);
                };

                settings_page->on_joystick_hotkey_changed = [this](const char *hotkey_option) {
                    global_hotkeys_js.reset();
                    if(strcmp(hotkey_option, "enable_hotkeys") == 0)
                        global_hotkeys_js = register_joystick_hotkeys(this);
                    else if(strcmp(hotkey_option, "disable_hotkeys") == 0)
                        global_hotkeys_js.reset();
                };

                settings_page->on_page_closed = [this]() {
                    replay_dropdown_button_ptr->set_item_description("start", config.replay_config.start_stop_hotkey.to_string(false, false));
                    replay_dropdown_button_ptr->set_item_description("save", config.replay_config.save_hotkey.to_string(false, false));
                    replay_dropdown_button_ptr->set_item_description("save_1_min", config.replay_config.save_1_min_hotkey.to_string(false, false));
                    replay_dropdown_button_ptr->set_item_description("save_10_min", config.replay_config.save_10_min_hotkey.to_string(false, false));

                    record_dropdown_button_ptr->set_item_description("start", config.record_config.start_stop_hotkey.to_string(false, false));
                    record_dropdown_button_ptr->set_item_description("pause", config.record_config.pause_unpause_hotkey.to_string(false, false));

                    stream_dropdown_button_ptr->set_item_description("start", config.streaming_config.start_stop_hotkey.to_string(false, false));
                };

                page_stack.push(std::move(settings_page));
            };
            front_page_ptr->add_widget(std::move(button));
        }

        {
            const mgl::vec2f main_buttons_size = main_buttons_list_ptr->get_size();
            const int settings_button_size = main_buttons_size.y * 0.33f;
            auto button = std::make_unique<Button>(&get_theme().title_font, "", mgl::vec2f(settings_button_size, settings_button_size), mgl::Color(0, 0, 0, 180));
            button->set_position((main_buttons_list_ptr->get_position() + main_buttons_size - mgl::vec2f(0.0f, settings_button_size*2) + mgl::vec2f(settings_button_size * 0.333f, 0.0f)).floor());
            button->set_bg_hover_color(mgl::Color(0, 0, 0, 255));
            button->set_icon(&get_theme().screenshot_texture);
            button->set_icon_padding_scale(1.2f);
            button->on_click = [&]() {
                auto screenshot_settings_page = std::make_unique<ScreenshotSettingsPage>(&gsr_info, config, &page_stack);
                screenshot_settings_page->on_config_changed = [this]() {
                    update_led_indicator_after_settings_change();
                };
                page_stack.push(std::move(screenshot_settings_page));
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
    }

    void Overlay::hide() {
        if(!visible)
            return;

        hide_ui = false;

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        while(!page_stack.empty()) {
            page_stack.pop();
        }
        remove_widgets_to_be_removed();

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
        start_region_capture = false;
        start_window_capture = false;

        if(xi_input_xev) {
            free(xi_input_xev);
            xi_input_xev = nullptr;
        }

        if(xi_output_xev) {
            free(xi_output_xev);
            xi_output_xev = nullptr;
        }

        if(xi_display) {
            if(window) {
                mgl_context *context = mgl_get_context();
                Display *display = (Display*)context->connection;

                const mgl::vec2i new_cursor_position = mgl::vec2i(window->internal_window()->pos.x, window->internal_window()->pos.y) + window->get_mouse_position();
                XWarpPointer(display, DefaultRootWindow(display), DefaultRootWindow(display), 0, 0, 0, 0, new_cursor_position.x, new_cursor_position.y);
                xi_warp_all_mouse_devices(xi_display, new_cursor_position);
                XFlush(display);

                XFixesShowCursor(display, DefaultRootWindow(display));
                XFlush(display);
            }

            XCloseDisplay(xi_display);
            xi_display = nullptr;
        }

        if(window) {
            if(show_overlay_timeout_seconds > 0.0001) {
                window->clear(mgl::Color(0, 0, 0, 0));
                window->display();

                mgl_context *context = mgl_get_context();
                context->gl.glFlush();
                context->gl.glFinish();
                usleep(50 * 1000); // EGL doesn't do an immediate flush for some reason
            }

            window->set_visible(false);
            window.reset();
        }

        deinit_theme();
#ifdef __GLIBC__
        malloc_trim(0);
#endif
    }

    void Overlay::hide_next_frame() {
        hide_ui = true;
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

    void Overlay::toggle_record(RecordForceType force_type) {
        on_press_start_record(false, force_type);
    }

    void Overlay::toggle_pause() {
        if(recording_status != RecordingStatus::RECORD || gpu_screen_recorder_process <= 0)
            return;

        kill(gpu_screen_recorder_process, SIGUSR2);
        paused = !paused;

        if(paused) {
            paused_clock.restart();
            update_ui_recording_paused();
            if(config.record_config.record_options.show_notifications)
                show_notification("Recording has been paused", notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);
        } else {
            paused_total_time_seconds += paused_clock.get_elapsed_time_seconds();
            update_ui_recording_unpaused();
            if(config.record_config.record_options.show_notifications)
                show_notification("Recording has been unpaused", notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);
        }

        if(led_indicator && config.record_config.record_options.use_led_indicator)
            led_indicator->blink();
    }

    void Overlay::update_upause_status() {
        paused = false;
        paused_clock.restart();
        paused_total_time_seconds = 0.0;
    }

    void Overlay::toggle_stream() {
        on_press_start_stream(false);
    }

    void Overlay::toggle_replay() {
        on_press_start_replay(false, false);
    }

    void Overlay::save_replay() {
        on_press_save_replay();
    }

    void Overlay::save_replay_1_min() {
        on_press_save_replay_1_min_replay();
    }

    void Overlay::save_replay_10_min() {
        on_press_save_replay_10_min_replay();
    }

    void Overlay::take_screenshot() {
        on_press_take_screenshot(false, ScreenshotForceType::NONE);
    }

    void Overlay::take_screenshot_region() {
        on_press_take_screenshot(false, ScreenshotForceType::REGION);
    }

    void Overlay::take_screenshot_window() {
        on_press_take_screenshot(false, ScreenshotForceType::WINDOW);
    }

    const char* Overlay::notification_type_to_string(NotificationType notification_type) {
        switch(notification_type) {
            case NotificationType::NONE:       return nullptr;
            case NotificationType::RECORD:     return "record";
            case NotificationType::REPLAY:     return "replay";
            case NotificationType::STREAM:     return "stream";
            case NotificationType::SCREENSHOT: return "screenshot";
            case NotificationType::NOTICE:     return gsr_icon_path.c_str();
        }
        return nullptr;
    }

    static void truncate_string(std::string &str, int max_length) {
        int index = 0;
        size_t byte_index = 0;

        while(index < max_length && byte_index < str.size()) {
            uint32_t codepoint = 0;
            size_t codepoint_length = 0;
            mgl::utf8_decode((const unsigned char*)str.c_str() + byte_index, str.size() - byte_index, &codepoint, &codepoint_length);
            if(codepoint_length == 0)
                codepoint_length = 1;

            index += 1;
            byte_index += codepoint_length;
        }

        if(byte_index < str.size()) {
            str.erase(byte_index);
            str += "...";
        }
    }

    static bool is_hex_num(char c) {
        return (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
    }

    static bool contains_non_hex_number(const char *str) {
        bool hex_start = false;
        size_t len = strlen(str);
        if(len >= 2 && memcmp(str, "0x", 2) == 0) {
            str += 2;
            len -= 2;
            hex_start = true;
        }

        bool is_hex = false;
        for(size_t i = 0; i < len; ++i) {
            char c = str[i];
            if(c == '\0')
                return false;
            if(!is_hex_num(c))
                return true;
            if((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
                is_hex = true;
        }

        return is_hex && !hex_start;
    }

    static bool is_number(const char *str) {
        const char *p = str;
        while(*p) {
            char c = *p;
            if(c < '0' || c > '9')
                return false;
            ++p;
        }
        return true;
    }

    static bool is_capture_target_monitor(const char *capture_target) {
        return strcmp(capture_target, "window") != 0 && strcmp(capture_target, "focused") != 0 && strcmp(capture_target, "region") != 0 && strcmp(capture_target, "portal") != 0 && contains_non_hex_number(capture_target);
    }

    static std::string capture_target_get_notification_name(const char *capture_target, bool save) {
        std::string result;
        if(is_capture_target_monitor(capture_target)) {
            result = "this monitor";
        } else if(is_number(capture_target)) {
            mgl_context *context = mgl_get_context();
            Display *display = (Display*)context->connection;

            int64_t window_id = None;
            sscanf(capture_target, "%" PRIi64, &window_id);

            const std::optional<std::string> window_title = get_window_title(display, window_id);
            if(save) {
                result = "window";
            } else if(window_title) {
                result = strip(window_title.value());
                truncate_string(result, 30);
                result = "window \"" + result + "\"";
            } else {
                result = std::string("window ") + capture_target;
            }
        } else {
            result = capture_target;
        }
        return result;
    }

    static std::string get_valid_monitor_x11(const std::string &target_monitor_name, const std::vector<Monitor> &monitors) {
        std::string target_monitor_name_clean = target_monitor_name;
        if(starts_with(target_monitor_name_clean, "HDMI-A"))
            target_monitor_name_clean.replace(0, 6, "HDMI");

        for(const Monitor &monitor : monitors) {
            std::string monitor_name_clean = monitor.name;
            if(starts_with(monitor_name_clean, "HDMI-A"))
                monitor_name_clean.replace(0, 6, "HDMI");

            if(target_monitor_name_clean == monitor_name_clean)
                return monitor.name;
        }

        return "";
    }

    static std::string get_focused_monitor_by_cursor(CursorTracker *cursor_tracker, const GsrInfo &gsr_info, const std::vector<Monitor> &x11_monitors) {
        std::optional<CursorInfo> cursor_info;
        if(cursor_tracker) {
            cursor_tracker->update();
            cursor_info = cursor_tracker->get_latest_cursor_info();
        }

        std::string focused_monitor_name;
        if(cursor_info) {
            focused_monitor_name = std::move(cursor_info->monitor_name);
        } else {
            mgl_context *context = mgl_get_context();
            Display *display = (Display*)context->connection;

            Window x11_cursor_window = None;
            mgl::vec2i cursor_position = get_cursor_position(display, &x11_cursor_window);

            const mgl::vec2i monitor_position_query_value = (x11_cursor_window || gsr_info.system_info.display_server != DisplayServer::WAYLAND) ? cursor_position : create_window_get_center_position(display);
            const Monitor *focused_monitor = find_monitor_at_position(x11_monitors, monitor_position_query_value);
            if(focused_monitor)
                focused_monitor_name = focused_monitor->name;
        }

        return focused_monitor_name;
    }

    void Overlay::show_notification(const char *str, double timeout_seconds, mgl::Color icon_color, mgl::Color bg_color, NotificationType notification_type, const char *capture_target, NotificationLevel notification_level) {
        if(notification_level != NotificationLevel::ERROR)
            timeout_seconds *= notification_duration_multiplier;

        char timeout_seconds_str[32];
        snprintf(timeout_seconds_str, sizeof(timeout_seconds_str), "%f", timeout_seconds);

        const std::string icon_color_str = color_to_hex_str(icon_color);
        const std::string bg_color_str = color_to_hex_str(bg_color);
        const char *notification_args[14] = {
            "gsr-notify", "--text", str, "--timeout", timeout_seconds_str,
            "--icon-color", icon_color_str.c_str(), "--bg-color", bg_color_str.c_str(),
        };

        int arg_index = 9;
        const char *notification_type_str = notification_type_to_string(notification_type);
        if(notification_type_str) {
            notification_args[arg_index++] = "--icon";
            notification_args[arg_index++] = notification_type_str;
        }

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        std::string monitor_name;
        const auto monitors = get_monitors(display);

        if(capture_target && is_capture_target_monitor(capture_target))
            monitor_name = capture_target;
        else
            monitor_name = get_focused_monitor_by_cursor(cursor_tracker.get(), gsr_info, monitors);

        monitor_name = get_valid_monitor_x11(monitor_name, monitors);
        if(!monitor_name.empty()) {
            notification_args[arg_index++] = "--monitor";
            notification_args[arg_index++] = monitor_name.c_str();
        } else if(!monitors.empty()) {
            notification_args[arg_index++] = "--monitor";
            notification_args[arg_index++] = monitors.front().name.c_str();
        }

        notification_args[arg_index++] = nullptr;

        if(notification_process > 0) {
            kill(notification_process, SIGINT);
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

    void Overlay::go_back_to_old_ui() {
        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
        if(inside_flatpak)
            exit_reason = "back-to-old-ui";
        else
            exit_reason = "exit";

        const char *args[] = { "systemctl", "disable", "--user", "gpu-screen-recorder-ui", nullptr };
        std::string stdout_str;
        exec_program_on_host_get_stdout(args, stdout_str);
        exit();
    }

    const Config& Overlay::get_config() const {
        return config;
    }

    void Overlay::unbind_all_keyboard_hotkeys() {
        if(global_hotkeys)
            global_hotkeys->unbind_all_keys();
    }

    void Overlay::rebind_all_keyboard_hotkeys() {
        unbind_all_keyboard_hotkeys();
        // TODO: Check if type is GlobalHotkeysLinux
        if(global_hotkeys)
            bind_linux_hotkeys(static_cast<GlobalHotkeysLinux*>(global_hotkeys.get()), this);
    }

    void Overlay::set_notification_speed(NotificationSpeed notification_speed) {
        switch(notification_speed) {
            case NotificationSpeed::NORMAL:
                notification_duration_multiplier = 1.0;
                break;
            case NotificationSpeed::FAST:
                notification_duration_multiplier = 0.3;
                break;
        }
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

    static std::string to_duration_string(double duration_sec) {
        int seconds = ceil(duration_sec);

        const int hours = seconds / 60 / 60;
        seconds -= (hours * 60 * 60);

        const int minutes = seconds / 60;
        seconds -= (minutes * 60);

        std::string result;
        if(hours > 0)
            result += std::to_string(hours) + " hour" + (hours == 1 ? "" : "s");

        if(minutes > 0) {
            if(!result.empty())
                result += " ";
            result += std::to_string(minutes) + " minute" + (minutes == 1 ? "" : "s");
        }

        if(seconds > 0 || (hours == 0 && minutes == 0)) {
            if(!result.empty())
                result += " ";
            result += std::to_string(seconds) + " second" + (seconds == 1 ? "" : "s");
        }

        return result;
    }

    double Overlay::get_time_passed_in_replay_buffer_seconds() {
        double replay_duration_sec = replay_saved_duration_sec;
        if(replay_duration_sec > current_recording_config.replay_config.replay_time)
            replay_duration_sec = current_recording_config.replay_config.replay_time;
        if(replay_save_duration_min > 0 && replay_duration_sec > replay_save_duration_min * 60)
            replay_duration_sec = replay_save_duration_min * 60;
        return replay_duration_sec;
    }

    static ClipboardFile::FileType filename_to_clipboard_file_type(const std::string &filename) {
        if(ends_with(filename, ".jpg") || ends_with(filename, ".jpeg"))
            return ClipboardFile::FileType::JPG;
        else if(ends_with(filename, ".png"))
            return ClipboardFile::FileType::PNG;
        assert(false);
        return ClipboardFile::FileType::PNG;
    }

    void Overlay::save_video_in_current_game_directory(std::string &video_filepath, NotificationType notification_type) {
        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;
        const std::string video_filename = filepath_get_filename(video_filepath.c_str());

        const Window gsr_ui_window = window ? (Window)window->get_system_handle() : None;
        std::string focused_window_name = get_window_name_at_cursor_position(display, gsr_ui_window);
        
        const std::string wm_name = get_window_manager_name(display);
        const bool is_hyprland = wm_name.find("Hyprland") != std::string::npos;
        const bool is_kwin = wm_name == "KWin";

        if (is_hyprland) {
            focused_window_name = get_current_hyprland_window_title();
        } else if (is_kwin) {
            focused_window_name = get_current_kwin_window_title();
        } else {
            if(focused_window_name.empty())
                focused_window_name = get_focused_window_name(display, WindowCaptureType::FOCUSED, false);
            if(focused_window_name.empty())
                focused_window_name = "Game";
        }

        string_replace_characters(focused_window_name.data(), "/\\", ' ');

        std::string video_directory = filepath_get_directory(video_filepath.c_str()) + "/" + focused_window_name;
        create_directory_recursive(video_directory.data());

        const std::string new_video_filepath = video_directory + "/" + video_filename;
        rename(video_filepath.c_str(), new_video_filepath.c_str());
        video_filepath = new_video_filepath;

        truncate_string(focused_window_name, 40);
        const char *capture_target = nullptr;
        char msg[512];

        switch(notification_type) {
            case NotificationType::RECORD: {
                if(!config.record_config.record_options.show_notifications)
                    return;

                const std::string duration_str = to_duration_string(recording_duration_clock.get_elapsed_time_seconds() - paused_total_time_seconds - (paused ? paused_clock.get_elapsed_time_seconds() : 0.0));
                snprintf(msg, sizeof(msg), "Saved a %s recording of %s\nto \"%s\"",
                    duration_str.c_str(),
                    capture_target_get_notification_name(recording_capture_target.c_str(), true).c_str(), focused_window_name.c_str());
                capture_target = recording_capture_target.c_str();
                break;
            }
            case NotificationType::REPLAY: {
                if(!config.replay_config.record_options.show_notifications)
                    return;

                const std::string duration_str = to_duration_string(get_time_passed_in_replay_buffer_seconds());
                snprintf(msg, sizeof(msg), "Saved a %s replay of %s\nto \"%s\"",
                    duration_str.c_str(),
                    capture_target_get_notification_name(recording_capture_target.c_str(), true).c_str(), focused_window_name.c_str());
                capture_target = recording_capture_target.c_str();
                break;
            }
            case NotificationType::SCREENSHOT: {
                if(!config.screenshot_config.show_notifications)
                    return;

                snprintf(msg, sizeof(msg), "Saved a screenshot of %s\nto \"%s\"",
                    capture_target_get_notification_name(screenshot_capture_target.c_str(), true).c_str(), focused_window_name.c_str());
                capture_target = screenshot_capture_target.c_str();
                break;
            }
            case NotificationType::NONE:
            case NotificationType::STREAM:
            case NotificationType::NOTICE:
                break;
        }
        show_notification(msg, notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, notification_type, capture_target);
    }

    static NotificationType recording_status_to_notification_type(RecordingStatus recording_status) {
        switch(recording_status) {
            case RecordingStatus::NONE:   return NotificationType::NONE;
            case RecordingStatus::REPLAY: return NotificationType::REPLAY;
            case RecordingStatus::RECORD: return NotificationType::RECORD;
            case RecordingStatus::STREAM: return NotificationType::STREAM;
        }
        return NotificationType::NONE;
    }

    void Overlay::on_replay_saved(const char *replay_saved_filepath) {
        replay_save_show_notification = false;
        if(config.replay_config.save_video_in_game_folder) {
            std::string filepath = replay_saved_filepath;
            save_video_in_current_game_directory(filepath, NotificationType::REPLAY);
        } else if(config.replay_config.record_options.show_notifications) {
            const std::string duration_str = to_duration_string(get_time_passed_in_replay_buffer_seconds());

            char msg[512];
            snprintf(msg, sizeof(msg), "Saved a %s replay of %s",
                duration_str.c_str(),
                capture_target_get_notification_name(recording_capture_target.c_str(), true).c_str());
            show_notification(msg, notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY, recording_capture_target.c_str());
        }

        if(led_indicator && config.replay_config.record_options.use_led_indicator)
            led_indicator->blink();
    }

    void Overlay::process_gsr_output() {
        if(replay_save_show_notification && replay_save_clock.get_elapsed_time_seconds() >= replay_saving_notification_timeout_seconds) {
            replay_save_show_notification = false;
            if(config.replay_config.record_options.show_notifications)
                show_notification("Saving replay, this might take some time", notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);
        }

        if(gpu_screen_recorder_process_output_file) {
            char buffer[1024];
            char *line = fgets(buffer, sizeof(buffer), gpu_screen_recorder_process_output_file);
            if(!line || line[0] == '\0')
                return;

            const int line_len = strlen(line);
            if(line[line_len - 1] == '\n')
                line[line_len - 1] = '\0';

            if(starts_with({line, (size_t)line_len}, "Error: ")) {
                show_notification(line + 7, notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), recording_status_to_notification_type(recording_status), nullptr, NotificationLevel::ERROR);
                return;
            }

            const std::string video_filepath = filepath_get_filename(line);
            if(starts_with(video_filepath, "Video_")) {
                record_filepath = line;
                on_stop_recording(0, record_filepath);
                return;
            }

            switch(recording_status) {
                case RecordingStatus::NONE:
                    break;
                case RecordingStatus::REPLAY:
                    on_replay_saved(line);
                    break;
                case RecordingStatus::RECORD:
                    break;
                case RecordingStatus::STREAM:
                    break;
            }
        } else if(gpu_screen_recorder_process_output_fd > 0) {
            char buffer[1024];
            read(gpu_screen_recorder_process_output_fd, buffer, sizeof(buffer));
        }
    }

    void Overlay::on_gsr_process_error(int exit_code, NotificationType notification_type) {
        fprintf(stderr, "Warning: gpu-screen-recorder (%d) exited with exit status %d\n", (int)gpu_screen_recorder_process, exit_code);
        if(exit_code == 50) {
            show_notification("Desktop portal capture failed.\nEither you canceled the desktop portal or your Wayland compositor doesn't support desktop portal capture\nor it's incorrectly setup on your system.", notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), notification_type, nullptr, NotificationLevel::ERROR);
        } else if(exit_code == 51) {
            show_notification("Monitor capture failed.\nThe monitor you are trying to capture is invalid.\nPlease validate your capture settings.", notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), notification_type, nullptr, NotificationLevel::ERROR);
        } else if(exit_code == 52) {
            show_notification("Capture failed. Neither H264, HEVC nor AV1 video codecs are supported\non your system or you are trying to capture at a resolution higher than your\nsystem supports for each video codec.", 10.0, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), notification_type, nullptr, NotificationLevel::ERROR);
        } else if(exit_code == 53) {
            show_notification("Capture failed. Your system doesn't support the resolution you are trying to\nrecord at with the video codec you have chosen.\nChange capture resolution or video codec and try again.\nNote: AV1 supports the highest resolution, then HEVC and then H264.", 10.0, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), notification_type, nullptr, NotificationLevel::ERROR);
        } else if(exit_code == 54) {
            show_notification("Capture failed. Your system doesn't support the video codec you have chosen.\nChange video codec and try again.", notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), notification_type, nullptr, NotificationLevel::ERROR);
        } else if(exit_code == 60) {
            show_notification("Stopped capture because the user canceled the desktop portal", notification_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), notification_type, nullptr, NotificationLevel::ERROR);
        } else {
            const char *prefix = "";
            switch(notification_type) {
                case NotificationType::NONE:
                case NotificationType::NOTICE:
                    break;
                case NotificationType::SCREENSHOT:
                    prefix = "Failed to take a screenshot";
                    break;
                case NotificationType::RECORD:
                    prefix = "Failed to start/save recording";
                    break;
                case NotificationType::REPLAY:
                    prefix = "Replay stopped because of an error";
                    break;
                case NotificationType::STREAM:
                    prefix = "Streaming stopped because of an error";
                    break;
            }

            char msg[256];
            snprintf(msg, sizeof(msg), "%s. Verify if settings are correct", prefix);
            show_notification(msg, notification_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), notification_type, nullptr, NotificationLevel::ERROR);
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
                replay_save_duration_min = 0;
                update_ui_replay_stopped();
                if(exit_code == 0) {
                    if(config.replay_config.record_options.show_notifications)
                        show_notification("Replay stopped", short_notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);
                } else {
                    on_gsr_process_error(exit_code, NotificationType::REPLAY);
                }

                if(led_indicator)
                    led_indicator->set_led(false);

                break;
            }
            case RecordingStatus::RECORD: {
                update_ui_recording_stopped();
                on_stop_recording(exit_code, record_filepath);

                if(led_indicator)
                    led_indicator->set_led(false);
                break;
            }
            case RecordingStatus::STREAM: {
                update_ui_streaming_stopped();
                if(exit_code == 0) {
                    if(config.streaming_config.record_options.show_notifications)
                        show_notification("Streaming has stopped", short_notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::STREAM);
                } else {
                    on_gsr_process_error(exit_code, NotificationType::STREAM);
                }

                if(led_indicator)
                    led_indicator->set_led(false);
                break;
            }
        }

        gpu_screen_recorder_process = -1;
        recording_status = RecordingStatus::NONE;
    }

    void Overlay::update_gsr_screenshot_process_status() {
        if(gpu_screen_recorder_screenshot_process <= 0)
            return;

        int status;
        if(waitpid(gpu_screen_recorder_screenshot_process, &status, WNOHANG) == 0) {
            // Still running
            return;
        }

        int exit_code = -1;
        if(WIFEXITED(status))
            exit_code = WEXITSTATUS(status);

        if(exit_code == 0) {
            if(config.screenshot_config.save_screenshot_in_game_folder && config.screenshot_config.save_screenshot_to_disk) {
                save_video_in_current_game_directory(screenshot_filepath, NotificationType::SCREENSHOT);
            } else if(config.screenshot_config.show_notifications) {
                char msg[512];
                snprintf(msg, sizeof(msg), "Saved a screenshot of %s", capture_target_get_notification_name(screenshot_capture_target.c_str(), true).c_str());
                show_notification(msg, notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::SCREENSHOT, screenshot_capture_target.c_str());
            }

            if(config.screenshot_config.save_screenshot_to_clipboard)
                clipboard_file.set_current_file(screenshot_filepath, filename_to_clipboard_file_type(screenshot_filepath));

            if(led_indicator && config.screenshot_config.use_led_indicator)
                led_indicator->blink();

            if(!strip(config.screenshot_config.custom_script).empty()) {
                std::stringstream ss;
                ss << config.screenshot_config.custom_script << " " << std::quoted(screenshot_filepath);
                const std::string command = ss.str();
                const char *args[] = { "/bin/sh", "-c", command.c_str(), nullptr };
                exec_program_on_host_daemonized(args);
            }
        } else {
            fprintf(stderr, "Warning: gpu-screen-recorder (%d) exited with exit status %d\n", (int)gpu_screen_recorder_screenshot_process, exit_code);
            show_notification("Failed to take a screenshot. Verify if settings are correct", notification_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::SCREENSHOT, nullptr, NotificationLevel::ERROR);
        }

        gpu_screen_recorder_screenshot_process = -1;
    }

    static bool are_all_audio_tracks_available_to_capture(const std::vector<AudioTrack> &audio_tracks) {
        const auto audio_devices = get_audio_devices();
        for(const AudioTrack &audio_track : audio_tracks) {
            for(const std::string &audio_input : audio_track.audio_inputs) {
                std::string_view audio_track_name(audio_input.c_str());
                const bool is_app_audio = starts_with(audio_track_name, "app:");
                if(is_app_audio)
                    continue;

                if(starts_with(audio_track_name, "device:"))
                    audio_track_name.remove_prefix(7);

                auto it = std::find_if(audio_devices.begin(), audio_devices.end(), [&](const auto &audio_device) {
                    return audio_device.name == audio_track_name;
                });
                if(it == audio_devices.end()) {
                    //fprintf(stderr, "Audio not ready\n");
                    return false;
                }
            }
        }
        return true;
    }

    static bool is_webcam_available_to_capture(const RecordOptions &record_options) {
        if(record_options.webcam_source.empty())
            return true;

        const auto cameras = get_v4l2_devices();
        for(const GsrCamera &camera : cameras) {
            if(camera.path == record_options.webcam_source)
                return true;
        }
        return false;
    }

    void Overlay::replay_status_update_status() {
        if(replay_status_update_clock.get_elapsed_time_seconds() < replay_status_update_check_timeout_seconds)
            return;

        replay_status_update_clock.restart();
        update_focused_fullscreen_status();
        update_power_supply_status();
        update_system_startup_status();
    }

    void Overlay::update_focused_fullscreen_status() {
        if(replay_startup_mode != ReplayStartupMode::TURN_ON_AT_FULLSCREEN)
            return;

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        const Window focused_window = get_focused_window(display, WindowCaptureType::FOCUSED, false);
        if(window && focused_window == (Window)window->get_system_handle())
            return;

        const bool prev_focused_window_is_fullscreen = focused_window_is_fullscreen;
        focused_window_is_fullscreen = focused_window != 0 && window_is_fullscreen(display, focused_window);
        if(focused_window_is_fullscreen != prev_focused_window_is_fullscreen) {
            if(recording_status == RecordingStatus::NONE && focused_window_is_fullscreen) {
                if(are_all_audio_tracks_available_to_capture(config.replay_config.record_options.audio_tracks_list) && is_webcam_available_to_capture(config.replay_config.record_options))
                    on_press_start_replay(false, false);
            } else if(recording_status == RecordingStatus::REPLAY && !focused_window_is_fullscreen) {
                on_press_start_replay(true, false);
            }
        }
    }

    // TODO: Instead of checking power supply status periodically listen to power supply event
    void Overlay::update_power_supply_status() {
        if(replay_startup_mode != ReplayStartupMode::TURN_ON_AT_POWER_SUPPLY_CONNECTED)
            return;

        const bool prev_power_supply_status = power_supply_connected;
        power_supply_connected = power_supply_online_filepath.empty() || power_supply_is_connected(power_supply_online_filepath.c_str());
        if(power_supply_connected != prev_power_supply_status) {
            if(recording_status == RecordingStatus::NONE && power_supply_connected) {
                if(are_all_audio_tracks_available_to_capture(config.replay_config.record_options.audio_tracks_list) && is_webcam_available_to_capture(config.replay_config.record_options))
                    on_press_start_replay(false, false);
            } else if(recording_status == RecordingStatus::REPLAY && !power_supply_connected) {
                on_press_start_replay(false, false);
            }
        }
    }

    void Overlay::update_system_startup_status() {
        if(replay_startup_mode != ReplayStartupMode::TURN_ON_AT_SYSTEM_STARTUP || recording_status != RecordingStatus::NONE || !try_replay_startup)
            return;

        if(are_all_audio_tracks_available_to_capture(config.replay_config.record_options.audio_tracks_list) && is_webcam_available_to_capture(config.replay_config.record_options))
            on_press_start_replay(true, false);
    }

    void Overlay::on_stop_recording(int exit_code, std::string &video_filepath) {
        if(exit_code == 0) {
            if(config.record_config.save_video_in_game_folder) {
                save_video_in_current_game_directory(video_filepath, NotificationType::RECORD);
            } else if(config.record_config.record_options.show_notifications) {
                const std::string duration_str = to_duration_string(recording_duration_clock.get_elapsed_time_seconds() - paused_total_time_seconds - (paused ? paused_clock.get_elapsed_time_seconds() : 0.0));

                char msg[512];
                snprintf(msg, sizeof(msg), "Saved a %s recording of %s",
                    duration_str.c_str(),
                    capture_target_get_notification_name(recording_capture_target.c_str(), true).c_str());
                show_notification(msg, notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD, recording_capture_target.c_str());
            }

            if(led_indicator) {
                if(recording_status == RecordingStatus::REPLAY && !current_recording_config.replay_config.record_options.use_led_indicator)
                    led_indicator->set_led(false);
                else if(recording_status == RecordingStatus::STREAM && !current_recording_config.streaming_config.record_options.use_led_indicator)
                    led_indicator->set_led(false);
                else if(config.record_config.record_options.use_led_indicator)
                    led_indicator->blink();
            }
        } else {
            on_gsr_process_error(exit_code, NotificationType::RECORD);
        }

        update_ui_recording_stopped();
        replay_recording = false;
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
        record_dropdown_button_ptr->set_item_enabled("pause", recording_status == RecordingStatus::RECORD);
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
        record_dropdown_button_ptr->set_item_enabled("pause", false);
        update_upause_status();
        replay_recording = false;
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
        update_ui_recording_stopped();
    }

    void Overlay::update_ui_replay_started() {
        if(!visible)
            return;

        replay_dropdown_button_ptr->set_item_label("start", "Turn off");
        replay_dropdown_button_ptr->set_activated(true);
        replay_dropdown_button_ptr->set_description("On");
        replay_dropdown_button_ptr->set_item_icon("start", &get_theme().stop_texture);
        replay_dropdown_button_ptr->set_item_enabled("save", true);
        replay_dropdown_button_ptr->set_item_enabled("save_1_min", current_recording_config.replay_config.replay_time >= 60);
        replay_dropdown_button_ptr->set_item_enabled("save_10_min", current_recording_config.replay_config.replay_time >= 60 * 10);
    }

    void Overlay::update_ui_replay_stopped() {
        if(!visible)
            return;

        replay_dropdown_button_ptr->set_item_label("start", "Turn on");
        replay_dropdown_button_ptr->set_activated(false);
        replay_dropdown_button_ptr->set_description("Off");
        replay_dropdown_button_ptr->set_item_icon("start", &get_theme().play_texture);
        replay_dropdown_button_ptr->set_item_enabled("save", false);
        replay_dropdown_button_ptr->set_item_enabled("save_1_min", false);
        replay_dropdown_button_ptr->set_item_enabled("save_10_min", false);
        update_ui_recording_stopped();
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

    static std::vector<std::string> create_audio_tracks_cli_args(const std::vector<AudioTrack> &audio_tracks, const GsrInfo &gsr_info) {
        std::vector<std::string> result;
        result.reserve(audio_tracks.size());

        for(const AudioTrack &audio_track : audio_tracks) {
            std::string audio_track_merged;
            int num_app_audio = 0;

            for(const std::string &audio_input_name : audio_track.audio_inputs) {
                std::string new_audio_input_name = audio_input_name;
                const bool is_app_audio = starts_with(new_audio_input_name, "app:");
                if(is_app_audio && !gsr_info.system_info.supports_app_audio)
                    continue;

                if(is_app_audio && audio_track.application_audio_invert)
                    new_audio_input_name.replace(0, 4, "app-inverse:");

                if(is_app_audio)
                    ++num_app_audio;

                if(!audio_track_merged.empty())
                    audio_track_merged += "|";

                audio_track_merged += new_audio_input_name;
            }

            if(num_app_audio == 0 && audio_track.application_audio_invert) {
                if(!audio_track_merged.empty())
                    audio_track_merged += "|";

                audio_track_merged += "app-inverse:";
            }

            if(!audio_track_merged.empty())
                result.push_back(std::move(audio_track_merged));
        }

        return result;
    }

    void Overlay::add_region_command(std::vector<const char*> &args, char *region_str, int region_str_size) {
        Region region = region_selector.get_selection(x11_dpy, wayland_dpy);
        if(region.size.x <= 32 && region.size.y <= 32) {
            region.size.x = 0;
            region.size.y = 0;
        }
        snprintf(region_str, region_str_size, "%dx%d+%d+%d", region.size.x, region.size.y, region.pos.x, region.pos.y);
        args.push_back("-region");
        args.push_back(region_str);
    }

    void Overlay::add_common_gpu_screen_recorder_args(std::vector<const char*> &args, const RecordOptions &record_options, const std::vector<std::string> &audio_tracks, const std::string &video_bitrate, const char *region, char *region_str, int region_str_size, const std::string &region_area_option, RecordForceType force_type) {
        if(record_options.video_quality == "custom") {
            args.push_back("-bm");
            args.push_back("cbr");
            args.push_back("-q");
            args.push_back(video_bitrate.c_str());
        } else {
            args.push_back("-q");
            args.push_back(record_options.video_quality.c_str());
        }

        if(region_area_option == "focused" || record_options.change_video_resolution) {
            args.push_back("-s");
            args.push_back(region);
        }

        for(const std::string &audio_track : audio_tracks) {
            args.push_back("-a");
            args.push_back(audio_track.c_str());
        }

        if(record_options.restore_portal_session && force_type != RecordForceType::WINDOW) {
            args.push_back("-restore-portal-session");
            args.push_back("yes");
        }

        if(record_options.low_power_mode) {
            args.push_back("-low-power");
            args.push_back("yes");
        }

        if(region_area_option == "region")
            add_region_command(args, region_str, region_str_size);
    }

    static bool validate_capture_target(const std::string &capture_target, const SupportedCaptureOptions &capture_options) {
        if(capture_target == "window") {
            return capture_options.window;
        } else if(capture_target == "focused") {
            return capture_options.focused;
        } else if(capture_target == "region") {
            return capture_options.region;
        } else if(capture_target == "portal") {
            return capture_options.portal;
        } else if(capture_target == "focused_monitor") {
            return !capture_options.monitors.empty();
        } else {
            for(const GsrMonitor &monitor : capture_options.monitors) {
                if(capture_target == monitor.name)
                    return true;
            }
            return false;
        }
    }

    static std::string get_valid_capture_target(const std::string &capture_target, const SupportedCaptureOptions &capture_options) {
        std::string capture_target_clean = capture_target;
        if(starts_with(capture_target_clean, "HDMI-A"))
            capture_target_clean.replace(0, 6, "HDMI");

        for(const GsrMonitor &monitor : capture_options.monitors) {
            std::string monitor_name_clean = monitor.name;
            if(starts_with(monitor_name_clean, "HDMI-A"))
                monitor_name_clean.replace(0, 6, "HDMI");

            if(capture_target_clean == monitor_name_clean)
                return monitor.name;
        }

        return "";
    }

    std::string Overlay::get_capture_target(const std::string &capture_target, const SupportedCaptureOptions &capture_options) {
        if(capture_target == "window") {
            return std::to_string(window_selector.get_selection());
        } else if(capture_target == "focused_monitor") {
            std::optional<CursorInfo> cursor_info;
            if(cursor_tracker) {
                cursor_tracker->update();
                cursor_info = cursor_tracker->get_latest_cursor_info();
            }

            std::string focused_monitor_name;
            if(cursor_info) {
                focused_monitor_name = std::move(cursor_info->monitor_name);
            } else {
                mgl_context *context = mgl_get_context();
                Display *display = (Display*)context->connection;
                focused_monitor_name = get_focused_monitor_by_cursor(cursor_tracker.get(), gsr_info, get_monitors(display));
            }

            focused_monitor_name = get_valid_capture_target(focused_monitor_name, capture_options);
            if(!focused_monitor_name.empty())
                return focused_monitor_name;
            else if(!capture_options.monitors.empty())
                return capture_options.monitors.front().name;
            else
                return "";
        } else {
            return capture_target;
        }
    }

    void Overlay::prepare_gsr_output_for_reading() {
        if(gpu_screen_recorder_process_output_fd <= 0)
            return;

        const int fdl = fcntl(gpu_screen_recorder_process_output_fd, F_GETFL);
        fcntl(gpu_screen_recorder_process_output_fd, F_SETFL, fdl | O_NONBLOCK);
        gpu_screen_recorder_process_output_file = fdopen(gpu_screen_recorder_process_output_fd, "r");
        if(gpu_screen_recorder_process_output_file)
            gpu_screen_recorder_process_output_fd = -1;
    }

    void Overlay::on_press_save_replay() {
        if(recording_status != RecordingStatus::REPLAY || gpu_screen_recorder_process <= 0)
            return;

        replay_save_duration_min = 0;
        replay_save_show_notification = true;
        replay_save_clock.restart();
        replay_saved_duration_sec = replay_duration_clock.get_elapsed_time_seconds();
        if(replay_restart_on_save)
            replay_duration_clock.restart();

        kill(gpu_screen_recorder_process, SIGUSR1);
    }

    void Overlay::on_press_save_replay_1_min_replay() {
        if(recording_status != RecordingStatus::REPLAY || gpu_screen_recorder_process <= 0)
            return;

        if(current_recording_config.replay_config.replay_time < 60)
            return;

        replay_save_duration_min = 1;
        replay_save_show_notification = true;
        replay_save_clock.restart();
        replay_saved_duration_sec = replay_duration_clock.get_elapsed_time_seconds();
        kill(gpu_screen_recorder_process, SIGRTMIN+3);
    }

    void Overlay::on_press_save_replay_10_min_replay() {
        if(recording_status != RecordingStatus::REPLAY || gpu_screen_recorder_process <= 0)
            return;

        if(current_recording_config.replay_config.replay_time < 60 * 10)
            return;

        replay_save_duration_min = 10;
        replay_save_show_notification = true;
        replay_save_clock.restart();
        replay_saved_duration_sec = replay_duration_clock.get_elapsed_time_seconds();
        kill(gpu_screen_recorder_process, SIGRTMIN+5);
    }

    static const char* get_first_usable_hardware_video_codec_name(const GsrInfo &gsr_info) {
        if(gsr_info.supported_video_codecs.h264)
            return "h264";
        else if(gsr_info.supported_video_codecs.hevc)
            return "hevc";
        else if(gsr_info.supported_video_codecs.av1)
            return "av1";
        else if(gsr_info.supported_video_codecs.vp8)
            return "vp8";
        else if(gsr_info.supported_video_codecs.vp9)
            return "vp9";
        return nullptr;
    }

    static const char* change_container_if_codec_not_supported(const char *video_codec, const char *container) {
        if(strcmp(video_codec, "vp8") == 0 || strcmp(video_codec, "vp9") == 0) {
            if(strcmp(container, "webm") != 0 && strcmp(container, "matroska") != 0) {
                fprintf(stderr, "Warning: container '%s' is not compatible with video codec '%s', using webm container instead\n", container, video_codec);
                return "webm";
            }
        } else if(strcmp(container, "webm") == 0) {
            fprintf(stderr, "Warning: container webm is not compatible with video codec '%s', using mp4 container instead\n",  video_codec);
            return "mp4";
        }
        return container;
    }

    static void choose_video_codec_and_container_with_fallback(const GsrInfo &gsr_info, const char **video_codec, const char **container, const char **encoder) {
        *encoder = "gpu";
        if(strcmp(*video_codec, "h264_software") == 0) {
            *video_codec = "h264";
            *encoder = "cpu";
        } else if(strcmp(*video_codec, "auto") == 0) {
            if(!get_first_usable_hardware_video_codec_name(gsr_info)) {
                *video_codec = "h264";
                *encoder = "cpu";
            }
        }
        *container = change_container_if_codec_not_supported(*video_codec, *container);
    }

    static std::string get_framerate_mode_validate(const RecordOptions &record_options, const GsrInfo &gsr_info) {
        (void)gsr_info;
        std::string framerate_mode = record_options.framerate_mode;
        if(framerate_mode == "auto")
            framerate_mode = "vfr";
        return framerate_mode;
    }

    struct CameraAlignment {
        std::string halign;
        std::string valign;
        mgl::vec2i pos;
    };

    static CameraAlignment position_to_alignment(mgl::vec2i pos, mgl::vec2i size) {
        const mgl::vec2i pos_overflow = mgl::vec2i(100, 100) - (pos + size);
        if(pos_overflow.x < 0)
            pos.x += pos_overflow.x;
        if(pos_overflow.y < 0)
            pos.y += pos_overflow.y;

        if(pos.x < 0)
            pos.x = 0;
        if(pos.y < 0)
            pos.y = 0;

        CameraAlignment camera_alignment;
        const mgl::vec2i center = pos + size/2;

        if(center.x < 50) {
            camera_alignment.halign = "start";
            camera_alignment.pos.x = pos.x;
        } else {
            camera_alignment.halign = "end";
            camera_alignment.pos.x = -(100 - (pos.x + size.x));
        }

        if(center.y < 50) {
            camera_alignment.valign = "start";
            camera_alignment.pos.y = pos.y;
        } else {
            camera_alignment.valign = "end";
            camera_alignment.pos.y = -(100 - (pos.y + size.y));
        }

        return camera_alignment;
    }

    static std::string compose_capture_source_arg(const std::string &capture_target, const RecordOptions &record_options) {
        std::string capture_source_arg = capture_target;
        if(!record_options.webcam_source.empty()) {
            const mgl::vec2i webcam_size(record_options.webcam_width, record_options.webcam_height);
            const CameraAlignment camera_alignment = position_to_alignment(mgl::vec2i(record_options.webcam_x, record_options.webcam_y), webcam_size);

            capture_source_arg += "|" + record_options.webcam_source;
            capture_source_arg += ";halign=" + camera_alignment.halign;
            capture_source_arg += ";valign=" + camera_alignment.valign;
            capture_source_arg += ";x=" + std::to_string(camera_alignment.pos.x) + "%";
            capture_source_arg += ";y=" + std::to_string(camera_alignment.pos.y) + "%";
            capture_source_arg += ";width=" + std::to_string(webcam_size.x) + "%";
            capture_source_arg += ";height=" + std::to_string(webcam_size.y) + "%";
            capture_source_arg += ";pixfmt=" + record_options.webcam_video_format;
            capture_source_arg += ";camera_width=" + std::to_string(record_options.webcam_camera_width);
            capture_source_arg += ";camera_height=" + std::to_string(record_options.webcam_camera_height);
            capture_source_arg += ";camera_fps=" + std::to_string(record_options.webcam_camera_fps);
            if(record_options.webcam_flip_horizontally)
                capture_source_arg += ";hflip=true";
        }
        return capture_source_arg;
    }

    bool Overlay::on_press_start_replay(bool disable_notification, bool finished_selection) {
        if(region_selector.is_started() || window_selector.is_started())
            return false;

        switch(recording_status) {
            case RecordingStatus::NONE:
            case RecordingStatus::REPLAY:
                break;
            case RecordingStatus::RECORD:
                show_notification("Unable to start replay when recording.\nStop recording before starting replay.", notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::RECORD, nullptr, NotificationLevel::ERROR);
                return false;
            case RecordingStatus::STREAM:
                show_notification("Unable to start replay when streaming.\nStop streaming before starting replay.", notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::STREAM, nullptr, NotificationLevel::ERROR);
                return false;
        }

        update_upause_status();
        try_replay_startup = false;

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
            replay_save_duration_min = 0;
            update_ui_replay_stopped();

            if(led_indicator)
                led_indicator->set_led(false);

            // TODO: Show this with a slight delay to make sure it doesn't show up in the video
            if(!disable_notification && config.replay_config.record_options.show_notifications)
                show_notification("Replay stopped", notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);

            return true;
        }

        const SupportedCaptureOptions capture_options = get_supported_capture_options(gsr_info);
        recording_capture_target = get_capture_target(config.replay_config.record_options.record_area_option, capture_options);
        if(!validate_capture_target(config.replay_config.record_options.record_area_option, capture_options)) {
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Failed to start replay, capture target \"%s\" is invalid.\nPlease change capture target in settings", recording_capture_target.c_str());
            show_notification(err_msg, notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::REPLAY, nullptr, NotificationLevel::ERROR);
            return false;
        }

        if(config.replay_config.record_options.record_area_option == "region" && !finished_selection) {
            start_region_capture = true;
            on_region_selected = [disable_notification, this]() {
                on_press_start_replay(disable_notification, true);
            };
            return false;
        }

        if(config.replay_config.record_options.record_area_option == "window" && !finished_selection) {
            start_window_capture = true;
            on_window_selected = [disable_notification, this]() {
                on_press_start_replay(disable_notification, true);
            };
            return false;
        }

        // TODO: Validate input, fallback to valid values
        const std::string fps = std::to_string(config.replay_config.record_options.fps);
        const std::string video_bitrate = std::to_string(config.replay_config.record_options.video_bitrate);
        const std::string output_directory = config.replay_config.save_directory;
        const std::vector<std::string> audio_tracks = create_audio_tracks_cli_args(config.replay_config.record_options.audio_tracks_list, gsr_info);
        const std::string framerate_mode = get_framerate_mode_validate(config.replay_config.record_options, gsr_info);
        const std::string replay_time = std::to_string(config.replay_config.replay_time);
        const char *container = config.replay_config.container.c_str();
        const char *video_codec = config.replay_config.record_options.video_codec.c_str();
        const char *encoder = "gpu";
        choose_video_codec_and_container_with_fallback(gsr_info, &video_codec, &container, &encoder);

        char size[64];
        size[0] = '\0';
        if(config.replay_config.record_options.record_area_option == "focused")
            snprintf(size, sizeof(size), "%dx%d", (int)config.replay_config.record_options.record_area_width, (int)config.replay_config.record_options.record_area_height);

        if(config.replay_config.record_options.record_area_option != "focused" && config.replay_config.record_options.change_video_resolution)
            snprintf(size, sizeof(size), "%dx%d", (int)config.replay_config.record_options.video_width, (int)config.replay_config.record_options.video_height);

        const std::string capture_source_arg = compose_capture_source_arg(recording_capture_target, config.replay_config.record_options);

        std::vector<const char*> args = {
            "gpu-screen-recorder", "-w", capture_source_arg.c_str(),
            "-c", container,
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

        if(config.replay_config.restart_replay_on_save && gsr_info.system_info.gsr_version >= GsrVersion{5, 0, 3}) {
            args.push_back("-restart-replay-on-save");
            args.push_back("yes");
            replay_restart_on_save = true;
        } else {
            replay_restart_on_save = false;
        }

        if(gsr_info.system_info.gsr_version >= GsrVersion{5, 5, 0}) {
            args.push_back("-replay-storage");
            args.push_back(config.replay_config.replay_storage.c_str());
        }

        char region_str[128];
        add_common_gpu_screen_recorder_args(args, config.replay_config.record_options, audio_tracks, video_bitrate, size, region_str, sizeof(region_str), config.replay_config.record_options.record_area_option);

        if(gsr_info.system_info.gsr_version >= GsrVersion{5, 4, 0}) {
            args.push_back("-ro");
            args.push_back(config.record_config.save_directory.c_str());
        }

        args.push_back(nullptr);

        current_recording_config = config;

        gpu_screen_recorder_process = exec_program(args.data(), &gpu_screen_recorder_process_output_fd);
        if(gpu_screen_recorder_process == -1) {
            show_notification("Failed to launch gpu-screen-recorder to start replay", notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::REPLAY, nullptr, NotificationLevel::ERROR);
            return false;
        } else {
            recording_status = RecordingStatus::REPLAY;
            update_ui_replay_started();

            if(led_indicator && config.replay_config.record_options.use_led_indicator)
                led_indicator->set_led(true);
        }

        prepare_gsr_output_for_reading();

        // TODO: Start recording after this notification has disappeared to make sure it doesn't show up in the video.
        // Make clear to the user that the recording starts after the notification is gone.
        // Maybe have the option in notification to show timer until its getting hidden, then the notification can say:
        // Starting recording in 3...
        // 2...
        // 1...
        // TODO: Do not run this is a daemon. Instead get the pid and when launching another notification close the current notification
        // program and start another one. This can also be used to check when the notification has finished by checking with waitpid NOWAIT
        // to see when the program has exit.
        if(!disable_notification && config.replay_config.record_options.show_notifications) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Started replaying %s", capture_target_get_notification_name(recording_capture_target.c_str(), false).c_str());
            show_notification(msg, short_notification_timeout_seconds, get_color_theme().tint_color, get_color_theme().tint_color, NotificationType::REPLAY, recording_capture_target.c_str());
        }

        if(config.replay_config.record_options.record_area_option == "portal")
            hide_ui = true;

        // TODO: This will be incorrect if the user uses portal capture, as capture wont start until the user has
        // selected what to capture and accepted it.
        replay_duration_clock.restart();
        return true;
    }

    void Overlay::on_press_start_record(bool finished_selection, RecordForceType force_type) {
        if(region_selector.is_started() || window_selector.is_started())
            return;

        switch(recording_status) {
            case RecordingStatus::NONE:
            case RecordingStatus::RECORD:
                break;
            case RecordingStatus::REPLAY: {
                if(gpu_screen_recorder_process <= 0)
                    return;

                if(gsr_info.system_info.gsr_version >= GsrVersion{5, 4, 0}) {
                    if(!replay_recording) {
                        if(config.record_config.record_options.show_notifications)
                            show_notification("Started recording in the replay session", short_notification_timeout_seconds, get_color_theme().tint_color, get_color_theme().tint_color, NotificationType::RECORD);
                        update_ui_recording_started();

                        // TODO: This will be incorrect if the user uses portal capture, as capture wont start until the user has
                        // selected what to capture and accepted it.
                        recording_duration_clock.restart();
                        update_upause_status();

                        if(led_indicator) {
                            if(config.record_config.record_options.use_led_indicator) {
                                if(!current_recording_config.replay_config.record_options.use_led_indicator)
                                    led_indicator->set_led(true);
                                else
                                    led_indicator->blink();
                            }
                        }
                    }

                    replay_recording = true;
                    kill(gpu_screen_recorder_process, SIGRTMIN);
                } else {
                    show_notification("Unable to start recording when replay is turned on.\nTurn off replay before starting recording.", notification_error_timeout_seconds, mgl::Color(255, 0, 0), get_color_theme().tint_color, NotificationType::REPLAY, nullptr, NotificationLevel::ERROR);
                }
                return;
            }
            case RecordingStatus::STREAM: {
                if(gpu_screen_recorder_process <= 0)
                    return;

                if(gsr_info.system_info.gsr_version >= GsrVersion{5, 4, 0}) {
                    if(!replay_recording) {
                        if(config.record_config.record_options.show_notifications)
                            show_notification("Started recording in the streaming session", short_notification_timeout_seconds, get_color_theme().tint_color, get_color_theme().tint_color, NotificationType::RECORD);
                        update_ui_recording_started();

                        // TODO: This will be incorrect if the user uses portal capture, as capture wont start until the user has
                        // selected what to capture and accepted it.
                        recording_duration_clock.restart();
                        update_upause_status();

                        if(led_indicator) {
                            if(config.record_config.record_options.use_led_indicator) {
                                if(!current_recording_config.streaming_config.record_options.use_led_indicator)
                                    led_indicator->set_led(true);
                                else
                                    led_indicator->blink();
                            }
                        }
                    }

                    replay_recording = true;
                    kill(gpu_screen_recorder_process, SIGRTMIN);
                } else {
                    show_notification("Unable to start recording when streaming.\nStop streaming before starting recording.", notification_error_timeout_seconds, mgl::Color(255, 0, 0), get_color_theme().tint_color, NotificationType::STREAM, nullptr, NotificationLevel::ERROR);
                }
                return;
            }
        }

        close_gpu_screen_recorder_output();

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
                on_stop_recording(exit_code, record_filepath);
            }

            gpu_screen_recorder_process = -1;
            recording_status = RecordingStatus::NONE;
            update_ui_recording_stopped();
            update_upause_status();
            record_filepath.clear();

            if(led_indicator)
                led_indicator->set_led(false);
            return;
        }

        update_upause_status();

        std::string record_area_option;
        switch(force_type) {
            case RecordForceType::NONE:
                record_area_option = config.record_config.record_options.record_area_option;
                break;
            case RecordForceType::REGION:
                record_area_option = "region";
                break;
            case RecordForceType::WINDOW:
                record_area_option = gsr_info.system_info.display_server == DisplayServer::X11 ? "window" : "portal";
                break;
        }

        const SupportedCaptureOptions capture_options = get_supported_capture_options(gsr_info);
        recording_capture_target = get_capture_target(record_area_option, capture_options);
        if(!validate_capture_target(record_area_option, capture_options)) {
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Failed to start recording, capture target \"%s\" is invalid.\nPlease change capture target in settings", recording_capture_target.c_str());
            show_notification(err_msg, notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::RECORD, nullptr, NotificationLevel::ERROR);
            return;
        }

        if(record_area_option == "region" && !finished_selection) {
            start_region_capture = true;
            on_region_selected = [this, force_type]() {
                on_press_start_record(true, force_type);
            };
            return;
        }

        if(record_area_option == "window" && !finished_selection) {
            start_window_capture = true;
            on_window_selected = [this, force_type]() {
                on_press_start_record(true, force_type);
            };
            return;
        }

        record_filepath.clear();

        // TODO: Validate input, fallback to valid values
        const std::string fps = std::to_string(config.record_config.record_options.fps);
        const std::string video_bitrate = std::to_string(config.record_config.record_options.video_bitrate);
        const std::string output_file = config.record_config.save_directory + "/Video_" + get_date_str() + "." + container_to_file_extension(config.record_config.container.c_str());
        const std::vector<std::string> audio_tracks = create_audio_tracks_cli_args(config.record_config.record_options.audio_tracks_list, gsr_info);
        const std::string framerate_mode = get_framerate_mode_validate(config.record_config.record_options, gsr_info);
        const char *container = config.record_config.container.c_str();
        const char *video_codec = config.record_config.record_options.video_codec.c_str();
        const char *encoder = "gpu";
        choose_video_codec_and_container_with_fallback(gsr_info, &video_codec, &container, &encoder);

        char size[64];
        size[0] = '\0';
        if(record_area_option == "focused")
            snprintf(size, sizeof(size), "%dx%d", (int)config.record_config.record_options.record_area_width, (int)config.record_config.record_options.record_area_height);

        if(record_area_option != "focused" && config.record_config.record_options.change_video_resolution)
            snprintf(size, sizeof(size), "%dx%d", (int)config.record_config.record_options.video_width, (int)config.record_config.record_options.video_height);

        const std::string capture_source_arg = compose_capture_source_arg(recording_capture_target, config.record_config.record_options);

        std::vector<const char*> args = {
            "gpu-screen-recorder", "-w", capture_source_arg.c_str(),
            "-c", container,
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

        const std::string hotkey_window_capture_portal_session_token_filepath = get_config_dir() + "/gsr-ui-window-capture-token";
        if(record_area_option == "portal") {
            hide_ui = true;
            if(force_type == RecordForceType::WINDOW) {
                args.push_back("-portal-session-token-filepath");
                args.push_back(hotkey_window_capture_portal_session_token_filepath.c_str());
            }
        }

        char region_str[128];
        add_common_gpu_screen_recorder_args(args, config.record_config.record_options, audio_tracks, video_bitrate, size, region_str, sizeof(region_str), record_area_option, force_type);

        args.push_back(nullptr);

        current_recording_config = config;

        record_filepath = output_file;
        gpu_screen_recorder_process = exec_program(args.data(), &gpu_screen_recorder_process_output_fd);
        if(gpu_screen_recorder_process == -1) {
            show_notification("Failed to launch gpu-screen-recorder to start recording", notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::RECORD, nullptr, NotificationLevel::ERROR);
            return;
        } else {
            recording_status = RecordingStatus::RECORD;
            update_ui_recording_started();

            if(led_indicator && config.record_config.record_options.use_led_indicator)
                led_indicator->set_led(true);
        }

        prepare_gsr_output_for_reading();

        // TODO: Start recording after this notification has disappeared to make sure it doesn't show up in the video.
        // Make clear to the user that the recording starts after the notification is gone.
        // Maybe have the option in notification to show timer until its getting hidden, then the notification can say:
        // Starting recording in 3...
        // 2...
        // 1...
        if(config.record_config.record_options.show_notifications) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Started recording %s", capture_target_get_notification_name(recording_capture_target.c_str(), false).c_str());
            show_notification(msg, short_notification_timeout_seconds, get_color_theme().tint_color, get_color_theme().tint_color, NotificationType::RECORD, recording_capture_target.c_str());
        }

        // TODO: This will be incorrect if the user uses portal capture, as capture wont start until the user has
        // selected what to capture and accepted it.
        recording_duration_clock.restart();
    }

    static std::string streaming_get_url(const Config &config) {
        std::string url;
        fprintf(stderr, "streaming service: %s\n", config.streaming_config.streaming_service.c_str());
        if(config.streaming_config.streaming_service == "twitch") {
            url += "rtmp://live.twitch.tv/app/";
            url += config.streaming_config.twitch.stream_key;
        } else if(config.streaming_config.streaming_service == "youtube") {
            url += "rtmp://a.rtmp.youtube.com/live2/";
            url += config.streaming_config.youtube.stream_key;
        } else if(config.streaming_config.streaming_service == "rumble") {
            url += "rtmp://rtmp.rumble.com/live/";
            url += config.streaming_config.rumble.stream_key;
        } else if(config.streaming_config.streaming_service == "kick") {
            url += config.streaming_config.kick.stream_url;
            if(!url.empty() && url.back() != '/')
                url += "/";
            url += "app/";
            url += config.streaming_config.kick.stream_key;
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

            if(!url.empty() && url.back() != '/' && url.back() != '=' && !config.streaming_config.custom.key.empty())
                url += "/";

            url += config.streaming_config.custom.key;
        }
        return url;
    }

    void Overlay::on_press_start_stream(bool finished_selection) {
        if(region_selector.is_started() || window_selector.is_started())
            return;

        switch(recording_status) {
            case RecordingStatus::NONE:
            case RecordingStatus::STREAM:
                break;
            case RecordingStatus::REPLAY:
                show_notification("Unable to start streaming when replay is turned on.\nTurn off replay before starting streaming.", notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::REPLAY, nullptr, NotificationLevel::ERROR);
                return;
            case RecordingStatus::RECORD:
                show_notification("Unable to start streaming when recording.\nStop recording before starting streaming.", notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::RECORD, nullptr, NotificationLevel::ERROR);
                return;
        }

        update_upause_status();

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
            update_ui_streaming_stopped();

            if(led_indicator)
                led_indicator->set_led(false);

            // TODO: Show this with a slight delay to make sure it doesn't show up in the video
            if(config.streaming_config.record_options.show_notifications)
                show_notification("Streaming has stopped", notification_timeout_seconds, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::STREAM);
            return;
        }

        const SupportedCaptureOptions capture_options = get_supported_capture_options(gsr_info);
        recording_capture_target = get_capture_target(config.streaming_config.record_options.record_area_option, capture_options);
        if(!validate_capture_target(config.streaming_config.record_options.record_area_option, capture_options)) {
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Failed to start streaming, capture target \"%s\" is invalid.\nPlease change capture target in settings", recording_capture_target.c_str());
            show_notification(err_msg, notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::STREAM, nullptr, NotificationLevel::ERROR);
            return;
        }

        if(config.streaming_config.record_options.record_area_option == "region" && !finished_selection) {
            start_region_capture = true;
            on_region_selected = [this]() {
                on_press_start_stream(true);
            };
            return;
        }

        if(config.streaming_config.record_options.record_area_option == "window" && !finished_selection) {
            start_window_capture = true;
            on_window_selected = [this]() {
                on_press_start_stream(true);
            };
            return;
        }

        // TODO: Validate input, fallback to valid values
        const std::string fps = std::to_string(config.streaming_config.record_options.fps);
        const std::string video_bitrate = std::to_string(config.streaming_config.record_options.video_bitrate);
        std::vector<std::string> audio_tracks = create_audio_tracks_cli_args(config.streaming_config.record_options.audio_tracks_list, gsr_info);
        // This isn't possible unless the user modified the config file manually,
        // But we check it anyways as streaming on some sites can fail if there is more than one audio track
        if(audio_tracks.size() > 1)
            audio_tracks.resize(1);
        const std::string framerate_mode = get_framerate_mode_validate(config.streaming_config.record_options, gsr_info);
        const char *container = "flv";
        if(config.streaming_config.streaming_service == "custom")
            container = config.streaming_config.custom.container.c_str();
        const char *video_codec = config.streaming_config.record_options.video_codec.c_str();
        const char *encoder = "gpu";
        choose_video_codec_and_container_with_fallback(gsr_info, &video_codec, &container, &encoder);

        const std::string url = streaming_get_url(config);
        if(config.streaming_config.streaming_service == "rumble" || config.streaming_config.streaming_service == "kick") {
            fprintf(stderr, "Info: forcing video codec to h264 as rumble/kick supports only h264\n");
            video_codec = "h264";
        }

        char size[64];
        size[0] = '\0';
        if(config.streaming_config.record_options.record_area_option == "focused")
            snprintf(size, sizeof(size), "%dx%d", (int)config.streaming_config.record_options.record_area_width, (int)config.streaming_config.record_options.record_area_height);

        if(config.streaming_config.record_options.record_area_option != "focused" && config.streaming_config.record_options.change_video_resolution)
            snprintf(size, sizeof(size), "%dx%d", (int)config.streaming_config.record_options.video_width, (int)config.streaming_config.record_options.video_height);

        const std::string capture_source_arg = compose_capture_source_arg(recording_capture_target, config.streaming_config.record_options);

        std::vector<const char*> args = {
            "gpu-screen-recorder", "-w", capture_source_arg.c_str(),
            "-c", container,
            "-ac", config.streaming_config.record_options.audio_codec.c_str(),
            "-cursor", config.streaming_config.record_options.record_cursor ? "yes" : "no",
            "-cr", config.streaming_config.record_options.color_range.c_str(),
            "-fm", framerate_mode.c_str(),
            "-k", video_codec,
            "-encoder", encoder,
            "-f", fps.c_str(),
            "-v", "no",
            "-o", url.c_str()
        };

        char region_str[128];
        add_common_gpu_screen_recorder_args(args, config.streaming_config.record_options, audio_tracks, video_bitrate, size, region_str, sizeof(region_str), config.streaming_config.record_options.record_area_option);

        if(gsr_info.system_info.gsr_version >= GsrVersion{5, 4, 0}) {
            args.push_back("-ro");
            args.push_back(config.record_config.save_directory.c_str());
        }

        args.push_back(nullptr);

        current_recording_config = config;

        gpu_screen_recorder_process = exec_program(args.data(), &gpu_screen_recorder_process_output_fd);
        if(gpu_screen_recorder_process == -1) {
            show_notification("Failed to launch gpu-screen-recorder to start streaming", notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::STREAM, nullptr, NotificationLevel::ERROR);
            return;
        } else {
            recording_status = RecordingStatus::STREAM;
            update_ui_streaming_started();

            if(led_indicator && config.streaming_config.record_options.use_led_indicator)
                led_indicator->set_led(true);
        }

        prepare_gsr_output_for_reading();

        // TODO: Start recording after this notification has disappeared to make sure it doesn't show up in the video.
        // Make clear to the user that the recording starts after the notification is gone.
        // Maybe have the option in notification to show timer until its getting hidden, then the notification can say:
        // Starting recording in 3...
        // 2...
        // 1...
        // TODO: Do not run this is a daemon. Instead get the pid and when launching another notification close the current notification
        // program and start another one. This can also be used to check when the notification has finished by checking with waitpid NOWAIT
        // to see when the program has exit.
        if(config.streaming_config.record_options.show_notifications) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Started streaming %s", capture_target_get_notification_name(recording_capture_target.c_str(), false).c_str());
            show_notification(msg, short_notification_timeout_seconds, get_color_theme().tint_color, get_color_theme().tint_color, NotificationType::STREAM, recording_capture_target.c_str());
        }

        if(config.streaming_config.record_options.record_area_option == "portal")
            hide_ui = true;
    }

    void Overlay::on_press_take_screenshot(bool finished_selection, ScreenshotForceType force_type) {
        if(region_selector.is_started() || window_selector.is_started())
            return;

        if(gpu_screen_recorder_screenshot_process > 0) {
            fprintf(stderr, "Error: failed to take screenshot, another screenshot is currently being saved\n");
            return;
        }

        std::string record_area_option;
        switch(force_type) {
            case ScreenshotForceType::NONE:
                record_area_option = config.screenshot_config.record_area_option;
                break;
            case ScreenshotForceType::REGION:
                record_area_option = "region";
                break;
            case ScreenshotForceType::WINDOW:
                record_area_option = gsr_info.system_info.display_server == DisplayServer::X11 ? "window" : "portal";
                break;
        }

        const SupportedCaptureOptions capture_options = get_supported_capture_options(gsr_info);
        screenshot_capture_target = get_capture_target(record_area_option, capture_options);
        if(!validate_capture_target(record_area_option, capture_options)) {
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Failed to take a screenshot, capture target \"%s\" is invalid.\nPlease change capture target in settings", screenshot_capture_target.c_str());
            show_notification(err_msg, notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::SCREENSHOT, nullptr, NotificationLevel::ERROR);
            return;
        }

        if(record_area_option == "region" && !finished_selection) {
            start_region_capture = true;
            on_region_selected = [this, force_type]() {
                on_press_take_screenshot(true, force_type);
            };
            return;
        }

        if(record_area_option == "window" && !finished_selection) {
            start_window_capture = true;
            on_window_selected = [this, force_type]() {
                on_press_take_screenshot(true, force_type);
            };
            return;
        }

        // TODO: Validate input, fallback to valid values
        std::string output_file;
        if(config.screenshot_config.save_screenshot_to_disk)
            output_file = config.screenshot_config.save_directory + "/Screenshot_" + get_date_str() + "." + config.screenshot_config.image_format; // TODO: Validate image format
        else
            output_file = "/tmp/gsr_ui_clipboard_screenshot." + config.screenshot_config.image_format;

        const bool capture_cursor = force_type == ScreenshotForceType::NONE && config.screenshot_config.record_cursor;

        std::vector<const char*> args = {
            "gpu-screen-recorder", "-w", screenshot_capture_target.c_str(),
            "-cursor", capture_cursor ? "yes" : "no",
            "-v", "no",
            "-q", config.screenshot_config.image_quality.c_str(),
            "-o", output_file.c_str()
        };

        char size[64];
        size[0] = '\0';
        if(config.screenshot_config.change_image_resolution) {
            snprintf(size, sizeof(size), "%dx%d", (int)config.screenshot_config.image_width, (int)config.screenshot_config.image_height);
            args.push_back("-s");
            args.push_back(size);
        }

        if(config.screenshot_config.restore_portal_session && force_type != ScreenshotForceType::WINDOW) {
            args.push_back("-restore-portal-session");
            args.push_back("yes");
        }

        const std::string hotkey_window_capture_portal_session_token_filepath = get_config_dir() + "/gsr-ui-window-capture-token";
        if(record_area_option == "portal") {
            hide_ui = true;
            if(force_type == ScreenshotForceType::WINDOW) {
                args.push_back("-portal-session-token-filepath");
                args.push_back(hotkey_window_capture_portal_session_token_filepath.c_str());
            }
        }

        char region_str[128];
        if(record_area_option == "region")
            add_region_command(args, region_str, sizeof(region_str));

        args.push_back(nullptr);

        clipboard_file.set_current_file("", ClipboardFile::FileType::JPG);

        screenshot_filepath = output_file;
        gpu_screen_recorder_screenshot_process = exec_program(args.data(), nullptr);
        if(gpu_screen_recorder_screenshot_process == -1) {
            show_notification("Failed to launch gpu-screen-recorder to take a screenshot", notification_error_timeout_seconds, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::SCREENSHOT, nullptr, NotificationLevel::ERROR);
        }
    }

    bool Overlay::update_compositor_texture(const Monitor &monitor) {
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
            XImage *img = XGetImage(display, DefaultRootWindow(display), monitor.position.x, monitor.position.y, monitor.size.x, monitor.size.y, AllPlanes, ZPixmap);
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
            XRaiseWindow(display, (Window)window->get_system_handle());
            XFlush(display);
        }
    }
}
