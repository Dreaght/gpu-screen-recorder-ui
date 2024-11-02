#include "../include/Overlay.hpp"
#include "../include/Theme.hpp"
#include "../include/Config.hpp"
#include "../include/Process.hpp"
#include "../include/Utils.hpp"
#include "../include/gui/StaticPage.hpp"
#include "../include/gui/DropdownButton.hpp"
#include "../include/gui/CustomRendererWidget.hpp"
#include "../include/gui/SettingsPage.hpp"
#include "../include/gui/Utils.hpp"
#include "../include/gui/PageStack.hpp"

#include <string.h>
#include <assert.h>
#include <sys/wait.h>
#include <limits.h>
#include <stdexcept>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <mglpp/system/Rect.hpp>
#include <mglpp/window/Event.hpp>

extern "C" {
#include <mgl/mgl.h>
}

namespace gsr {
    static const mgl::Color bg_color(0, 0, 0, 100);
    static const double force_window_on_top_timeout_seconds = 1.0;

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

    static Window get_window_at_cursor_position(Display *display) {
        Window root_window = None;
        Window window = None;
        int dummy_i;
        unsigned int dummy_u;
        int cursor_pos_x = 0;
        int cursor_pos_y = 0;
        XQueryPointer(display, DefaultRootWindow(display), &root_window, &window, &dummy_i, &dummy_i, &cursor_pos_x, &cursor_pos_y, &dummy_u);
        return window;
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

    #define _NET_WM_STATE_REMOVE  0
    #define _NET_WM_STATE_ADD     1
    #define _NET_WM_STATE_TOGGLE  2

    static Bool set_window_wm_state(Display *display, Window window, Atom atom) {
        Atom net_wm_state_atom = XInternAtom(display, "_NET_WM_STATE", False);
        if(!net_wm_state_atom) {
            fprintf(stderr, "Error: failed to find atom _NET_WM_STATE\n");
            return False;
        }

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

        XSendEvent(display, DefaultRootWindow(display), False, SubstructureRedirectMask | SubstructureNotifyMask, (XEvent*)&xclient);
        XFlush(display);
        return True;
    }

    static Bool make_window_sticky(Display* display, Window window) {
        Atom net_wm_state_sticky_atom = XInternAtom(display, "_NET_WM_STATE_STICKY", False);
        if(!net_wm_state_sticky_atom) {
            fprintf(stderr, "Error: failed to find atom _NET_WM_STATE_STICKY\n");
            return False;
        }
        
        return set_window_wm_state(display, window, net_wm_state_sticky_atom);
    }

    // Returns the first monitor if not found. Assumes there is at least one monitor connected.
    static const mgl_monitor* find_monitor_by_cursor_position(mgl::Window &window) {
        const mgl_window *win = window.internal_window();
        assert(win->num_monitors > 0);
        for(int i = 0; i < win->num_monitors; ++i) {
            const mgl_monitor *mon = &win->monitors[i];
            if(mgl::IntRect({ mon->pos.x, mon->pos.y }, { mon->size.x, mon->size.y }).contains({ win->cursor_position.x, win->cursor_position.y }))
                return mon;
        }
        return &win->monitors[0];
    }

    static bool is_compositor_running(Display *dpy, int screen) {
        char prop_name[20];
        snprintf(prop_name, sizeof(prop_name), "_NET_WM_CM_S%d", screen);
        Atom prop_atom = XInternAtom(dpy, prop_name, False);
        return XGetSelectionOwner(dpy, prop_atom) != None;
    }

    Overlay::Overlay(std::string resources_path, GsrInfo gsr_info, egl_functions egl_funcs) :
        resources_path(std::move(resources_path)),
        gsr_info(gsr_info),
        egl_funcs(egl_funcs),
        bg_screenshot_overlay({0.0f, 0.0f}),
        top_bar_background({0.0f, 0.0f}),
        close_button_widget({0.0f, 0.0f}),
        config(gsr_info)
    {
        memset(&window_texture, 0, sizeof(window_texture));

        key_bindings[0].key_event.code = mgl::Keyboard::Escape;
        key_bindings[0].key_event.alt = false;
        key_bindings[0].key_event.control = false;
        key_bindings[0].key_event.shift = false;
        key_bindings[0].key_event.system = false;
        key_bindings[0].callback = [this]() {
            page_stack.pop();
        };

        std::optional<Config> new_config = read_config(gsr_info);
        if(new_config)
            config = std::move(new_config.value());

        gsr::init_color_theme(gsr_info);

        if(config.replay_config.start_replay_automatically)
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

            // TODO: Show this with a slight delay to make sure it doesn't show up in the video
            if(recording_status == RecordingStatus::RECORD && config.record_config.show_video_saved_notifications)
                show_notification("Recording has been saved", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);
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

    void Overlay::handle_events() {
        if(!visible || !window)
            return;

        while(window->poll_event(event)) {
            on_event(event);
        }
    }

    void Overlay::on_event(mgl::Event &event) {
        if(!visible || !window)
            return;

        close_button_widget.on_event(event, *window, mgl::vec2f(0.0f, 0.0f));
        if(!page_stack.on_event(event, *window, mgl::vec2f(0.0f, 0.0f)))
            return;

        process_key_bindings(event);
    }

    bool Overlay::draw() {
        update_notification_process_status();
        update_gsr_process_status();

        if(!visible)
            return false;

        if(page_stack.empty()) {
            hide();
            return false;
        }

        if(!window)
            return false;

        force_window_on_top();

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

        window->display();

        return true;
    }

    void Overlay::show() {
        window.reset();
        window = std::make_unique<mgl::Window>();
        gsr::deinit_theme();

        mgl::vec2i window_size = { 1280, 720 };
        mgl::vec2i window_pos = { 0, 0 };

        mgl::Window::CreateParams window_create_params;
        window_create_params.size = window_size;
        window_create_params.min_size = window_size;
        window_create_params.max_size = window_size;
        window_create_params.position = window_pos;
        window_create_params.hidden = true;
        window_create_params.override_redirect = true;
        window_create_params.background_color = bg_color;
        window_create_params.support_alpha = true;
        window_create_params.window_type = MGL_WINDOW_TYPE_NOTIFICATION;
        window_create_params.render_api = MGL_RENDER_API_EGL;

        if(!window->create("gsr ui", window_create_params))
            fprintf(stderr, "error: failed to create window\n");

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        unsigned char data = 2; // Prefer being composed to allow transparency
        XChangeProperty(display, window->get_system_handle(), XInternAtom(display, "_NET_WM_BYPASS_COMPOSITOR", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

        data = 1;
        XChangeProperty(display, window->get_system_handle(), XInternAtom(display, "GAMESCOPE_EXTERNAL_OVERLAY", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

        if(!gsr::init_theme(resources_path)) {
            fprintf(stderr, "Error: failed to load theme\n");
            exit(1);
        }

        mgl_window *win = window->internal_window();
        if(win->num_monitors == 0) {
            fprintf(stderr, "gsr warning: no monitors found, not showing overlay\n");
            return;
        }

        const mgl_monitor *focused_monitor = find_monitor_by_cursor_position(*window);
        window_pos = {focused_monitor->pos.x, focused_monitor->pos.y};
        window_size = {focused_monitor->size.x, focused_monitor->size.y};
        get_theme().set_window_size(window_size);

        window->set_size(window_size);
        window->set_size_limits(window_size, window_size);
        window->set_position(window_pos);

        update_compositor_texture(focused_monitor);

        audio_devices = get_audio_devices();

        top_bar_text = mgl::Text("GPU Screen Recorder", get_theme().top_bar_font);
        logo_sprite = mgl::Sprite(&get_theme().logo_texture);

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
            button->on_click = [this](const std::string &id) {
                if(id == "settings") {
                    auto replay_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::REPLAY, gsr_info, audio_devices, config, &page_stack);
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
            button->on_click = [this](const std::string &id) {
                if(id == "settings") {
                    auto record_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::RECORD, gsr_info, audio_devices, config, &page_stack);
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
            button->on_click = [this](const std::string &id) {
                if(id == "settings") {
                    auto stream_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::STREAM, gsr_info, audio_devices, config, &page_stack);
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

        window->set_fullscreen(true);
        window->set_visible(true);
        make_window_sticky(display, window->get_system_handle());

        if(default_cursor) {
            XFreeCursor(display, default_cursor);
            default_cursor = 0;
        }
        default_cursor = XCreateFontCursor(display, XC_arrow);

        // TODO: Retry if these fail.
        // TODO: Hmm, these dont work in owlboy. Maybe owlboy uses xi2 and that breaks this (does it?).
        // Remove these grabs when debugging with a debugger, or your X11 session will appear frozen

        // XGrabPointer(display, window->get_system_handle(), True,
        //     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
        //     Button1MotionMask | Button2MotionMask | Button3MotionMask | Button4MotionMask | Button5MotionMask |
        //     ButtonMotionMask,
        //     GrabModeAsync, GrabModeAsync, None, default_cursor, CurrentTime);
        // TODO: This breaks global hotkeys
        //XGrabKeyboard(display, window->get_system_handle(), True, GrabModeAsync, GrabModeAsync, CurrentTime);

        XSetInputFocus(display, window->get_system_handle(), RevertToParent, CurrentTime);
        XFlush(display);

        //window->set_fullscreen(true);

        visible = true;

        mgl::Event event;
        event.type = mgl::Event::MouseMoved;
        event.mouse_move.x = window->get_mouse_position().x;
        event.mouse_move.y = window->get_mouse_position().y;
        on_event(event);

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

        window_texture_deinit(&window_texture);
        window_texture_sprite.set_texture(nullptr);
        screenshot_texture.clear();
        screenshot_sprite.set_texture(nullptr);

        visible = false;
        if(window) {
            window->set_visible(false);
            window.reset();
        }

        gsr::deinit_theme();
    }

    void Overlay::toggle_show() {
        if(visible)
            hide();
        else
            show();
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
            notification_args[10] = "record",
            notification_args[11] = nullptr;
        } else {
            notification_args[9] = nullptr;
        }

        if(notification_process > 0) {
            kill(notification_process, SIGKILL);
            int status = 0;
            waitpid(notification_process, &status, 0);
        }

        notification_process = exec_program(notification_args);
    }

    bool Overlay::is_open() const {
        return visible;
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

    void Overlay::update_gsr_process_status() {
        if(gpu_screen_recorder_process <= 0)
            return;

        errno = 0;
        int status;
        if(waitpid(gpu_screen_recorder_process, &status, WNOHANG) == 0) {
            // Still running
            return;
        }

        int exit_code = -1;
        // The process is no longer a child process since gsr ui has restarted
        if(errno == ECHILD) {
            errno = 0;
            kill(gpu_screen_recorder_process, 0);
            if(errno != ESRCH) {
                // Still running
                return;
            }
            // We cant know the exit status, so we assume it succeeded
            exit_code = 0;
        } else {
            if(WIFEXITED(status))
                exit_code = WEXITSTATUS(status);
        }

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
                    show_notification("Replay stopped because of an error", 3.0, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::REPLAY);
                }
                break;
            }
            case RecordingStatus::RECORD: {
                update_ui_recording_stopped();
                if(exit_code == 0) {
                    if(config.record_config.show_video_saved_notifications)
                        show_notification("Recording has been saved", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);
                } else {
                    fprintf(stderr, "Warning: gpu-screen-recorder (%d) exited with exit status %d\n", (int)gpu_screen_recorder_process, exit_code);
                    show_notification("Failed to start/save recording", 3.0, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::RECORD);
                }
                break;
            }
            case RecordingStatus::STREAM: {
                update_ui_streaming_stopped();
                if(exit_code == 0) {
                    if(config.streaming_config.show_streaming_stopped_notifications)
                        show_notification("Streaming has stopped", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::STREAM);
                } else {
                    fprintf(stderr, "Warning: gpu-screen-recorder (%d) exited with exit status %d\n", (int)gpu_screen_recorder_process, exit_code);
                    show_notification("Streaming stopped because of an error", 3.0, mgl::Color(255, 0, 0), mgl::Color(255, 0, 0), NotificationType::STREAM);
                }
                break;
            }
        }

        gpu_screen_recorder_process = -1;
        recording_status = RecordingStatus::NONE;
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

    static std::string merge_audio_tracks(const std::vector<std::string> &audio_tracks) {
        std::string result;
        for(size_t i = 0; i < audio_tracks.size(); ++i) {
            if(i > 0)
                result += "|";
            result += audio_tracks[i];
        }
        return result;
    }

    void Overlay::on_press_save_replay() {
        if(recording_status != RecordingStatus::REPLAY || gpu_screen_recorder_process <= 0)
            return;

        kill(gpu_screen_recorder_process, SIGUSR1);
        if(config.replay_config.show_replay_saved_notifications)
            show_notification("Replay saved", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);
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
            if(config.replay_config.show_replay_stopped_notifications)
                show_notification("Replay stopped", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::REPLAY);
            return;
        }

        audio_devices = get_audio_devices();

        // TODO: Validate input, fallback to valid values
        const std::string fps = std::to_string(config.replay_config.record_options.fps);
        const std::string video_bitrate = std::to_string(config.replay_config.record_options.video_bitrate);
        const std::string output_directory = config.replay_config.save_directory;
        const std::string audio_tracks_merged = merge_audio_tracks(config.replay_config.record_options.audio_tracks);
        const std::string framerate_mode = config.replay_config.record_options.framerate_mode == "auto" ? "vfr" : config.replay_config.record_options.framerate_mode;
        const std::string replay_time = std::to_string(config.replay_config.replay_time);

        char region[64];
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
            "-k", config.replay_config.record_options.video_codec.c_str(),
            "-f", fps.c_str(),
            "-r", replay_time.c_str(),
            "-v", "no",
            "-o", output_directory.c_str()
        };

        if(config.replay_config.record_options.video_quality == "custom") {
            args.push_back("-bm");
            args.push_back("cbr");
            args.push_back("-q");
            args.push_back(video_bitrate.c_str());
        } else {
            args.push_back("-q");
            args.push_back(config.replay_config.record_options.video_quality.c_str());
        }

        if(config.replay_config.record_options.record_area_option == "focused" || config.replay_config.record_options.change_video_resolution) {
            args.push_back("-s");
            args.push_back(region);
        }

        if(config.replay_config.record_options.merge_audio_tracks) {
            args.push_back("-a");
            args.push_back(audio_tracks_merged.c_str());
        } else {
            for(const std::string &audio_track : config.replay_config.record_options.audio_tracks) {
                args.push_back("-a");
                args.push_back(audio_track.c_str());
            }
        }

        args.push_back(nullptr);

        gpu_screen_recorder_process = exec_program(args.data());
        if(gpu_screen_recorder_process == -1) {
            // TODO: Show notification failed to start
        } else {
            recording_status = RecordingStatus::REPLAY;
            update_ui_replay_started();
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
            }
            // window->set_visible(false);
            // window->close();
            // return;
            //exit(0);
            gpu_screen_recorder_process = -1;
            recording_status = RecordingStatus::NONE;
            update_ui_recording_stopped();

            // TODO: Show this with a slight delay to make sure it doesn't show up in the video
            if(config.record_config.show_video_saved_notifications)
                show_notification("Recording has been saved", 3.0, mgl::Color(255, 255, 255), get_color_theme().tint_color, NotificationType::RECORD);
            return;
        }

        audio_devices = get_audio_devices();

        // TODO: Validate input, fallback to valid values
        const std::string fps = std::to_string(config.record_config.record_options.fps);
        const std::string video_bitrate = std::to_string(config.record_config.record_options.video_bitrate);
        const std::string output_file = config.record_config.save_directory + "/Video_" + get_date_str() + "." + container_to_file_extension(config.record_config.container.c_str());
        const std::string audio_tracks_merged = merge_audio_tracks(config.record_config.record_options.audio_tracks);
        const std::string framerate_mode = config.record_config.record_options.framerate_mode == "auto" ? "vfr" : config.record_config.record_options.framerate_mode;

        char region[64];
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
            "-k", config.record_config.record_options.video_codec.c_str(),
            "-f", fps.c_str(),
            "-v", "no",
            "-o", output_file.c_str()
        };

        if(config.record_config.record_options.video_quality == "custom") {
            args.push_back("-bm");
            args.push_back("cbr");
            args.push_back("-q");
            args.push_back(video_bitrate.c_str());
        } else {
            args.push_back("-q");
            args.push_back(config.record_config.record_options.video_quality.c_str());
        }

        if(config.record_config.record_options.record_area_option == "focused" || config.record_config.record_options.change_video_resolution) {
            args.push_back("-s");
            args.push_back(region);
        }

        if(config.record_config.record_options.merge_audio_tracks) {
            args.push_back("-a");
            args.push_back(audio_tracks_merged.c_str());
        } else {
            for(const std::string &audio_track : config.record_config.record_options.audio_tracks) {
                args.push_back("-a");
                args.push_back(audio_track.c_str());
            }
        }

        args.push_back(nullptr);

        gpu_screen_recorder_process = exec_program(args.data());
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
        // TODO: Do not run this is a daemon. Instead get the pid and when launching another notification close the current notification
        // program and start another one. This can also be used to check when the notification has finished by checking with waitpid NOWAIT
        // to see when the program has exit.
        if(config.record_config.show_recording_started_notifications)
            show_notification("Recording has started", 3.0, get_color_theme().tint_color, get_color_theme().tint_color, NotificationType::RECORD);
        //exit(0);
        // window->set_visible(false);
        // window->close();

        // TODO: Show notification with args:
        // "Recording has started" 3.0 ./images/record.png 76b900
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

        audio_devices = get_audio_devices();

        // TODO: Validate input, fallback to valid values
        const std::string fps = std::to_string(config.streaming_config.record_options.fps);
        const std::string video_bitrate = std::to_string(config.streaming_config.record_options.video_bitrate);
        const std::string audio_tracks_merged = merge_audio_tracks(config.streaming_config.record_options.audio_tracks);
        const std::string framerate_mode = config.streaming_config.record_options.framerate_mode == "auto" ? "vfr" : config.streaming_config.record_options.framerate_mode;

        std::string container = "flv";
        if(config.streaming_config.streaming_service == "custom")
            container = config.streaming_config.custom.container;

        const std::string url = streaming_get_url(config);

        char region[64];
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
            "-k", config.streaming_config.record_options.video_codec.c_str(),
            "-f", fps.c_str(),
            "-v", "no",
            "-o", url.c_str()
        };

        if(config.streaming_config.record_options.video_quality == "custom") {
            args.push_back("-bm");
            args.push_back("cbr");
            args.push_back("-q");
            args.push_back(video_bitrate.c_str());
        } else {
            args.push_back("-q");
            args.push_back(config.streaming_config.record_options.video_quality.c_str());
        }

        if(config.streaming_config.record_options.record_area_option == "focused" || config.streaming_config.record_options.change_video_resolution) {
            args.push_back("-s");
            args.push_back(region);
        }

        if(config.streaming_config.record_options.merge_audio_tracks) {
            args.push_back("-a");
            args.push_back(audio_tracks_merged.c_str());
        } else {
            for(const std::string &audio_track : config.streaming_config.record_options.audio_tracks) {
                args.push_back("-a");
                args.push_back(audio_track.c_str());
            }
        }

        args.push_back(nullptr);

        gpu_screen_recorder_process = exec_program(args.data());
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

        if(is_compositor_running(display, 0))
            return false;

        bool window_texture_loaded = false;
        const Window window_at_cursor_position = get_window_at_cursor_position(display);
        if(is_window_fullscreen_on_monitor(display, window_at_cursor_position, monitor) && window_at_cursor_position)
            window_texture_loaded = window_texture_init(&window_texture, display, mgl_window_get_egl_display(window->internal_window()), window_at_cursor_position, egl_funcs) == 0;

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