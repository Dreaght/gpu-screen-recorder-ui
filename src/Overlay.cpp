#include "../include/Overlay.hpp"
#include "../include/Theme.hpp"
#include "../include/Config.hpp"
#include "../include/Process.hpp"
#include "../include/gui/StaticPage.hpp"
#include "../include/gui/DropdownButton.hpp"
#include "../include/gui/CustomRendererWidget.hpp"
#include "../include/gui/SettingsPage.hpp"
#include "../include/gui/Utils.hpp"
#include "../include/gui/PageStack.hpp"

#include <string.h>
#include <assert.h>
// TODO: Remove
#include <signal.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <mglpp/system/Rect.hpp>
#include <mglpp/window/Event.hpp>

extern "C" {
#include <mgl/mgl.h>
}

namespace gsr {
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

    Overlay::Overlay(mgl::Window &window, std::string resources_path, GsrInfo gsr_info, egl_functions egl_funcs, mgl::Color bg_color) :
        window(window),
        resources_path(std::move(resources_path)),
        gsr_info(std::move(gsr_info)),
        egl_funcs(egl_funcs),
        bg_color(bg_color),
        bg_screenshot_overlay({0.0f, 0.0f}),
        top_bar_background({0.0f, 0.0f}),
        top_bar_text("GPU Screen Recorder", get_theme().top_bar_font),
        logo_sprite(&get_theme().logo_texture),
        close_button_widget({0.0f, 0.0f})
    {
        memset(&window_texture, 0, sizeof(window_texture));
    }

    Overlay::~Overlay() {
        hide();
        if(gpu_screen_recorder_process > 0) {
            kill(gpu_screen_recorder_process, SIGINT);
            int status;
            if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            }
            gpu_screen_recorder_process = -1;
        }
    }

    void Overlay::on_event(mgl::Event &event, mgl::Window &window) {
        if(!visible)
            return;

        close_button_widget.on_event(event, window, mgl::vec2f(0.0f, 0.0f));
        page_stack.on_event(event, window, mgl::vec2f(0.0f, 0.0f));
        if(event.type == mgl::Event::KeyReleased) {
            if(event.key.code == mgl::Keyboard::Escape)
                page_stack.pop();
        }
    }

    void Overlay::draw(mgl::Window &window) {
        if(!visible)
            return;

        if(page_stack.empty()) {
            hide();
            return;
        }

        if(window_texture_sprite.get_texture() && window_texture.texture_id) {
            window.draw(window_texture_sprite);
            window.draw(bg_screenshot_overlay);
        } else if(screenshot_texture.is_valid()) {
            window.draw(screenshot_sprite);
            window.draw(bg_screenshot_overlay);
        }

        window.draw(top_bar_background);
        window.draw(top_bar_text);
        window.draw(logo_sprite);

        close_button_widget.draw(window, mgl::vec2f(0.0f, 0.0f));
        page_stack.draw(window, mgl::vec2f(0.0f, 0.0f));
    }

    void Overlay::show() {
        mgl_window *win = window.internal_window();
        if(win->num_monitors == 0) {
            fprintf(stderr, "gsr warning: no monitors found, not showing overlay\n");
            return;
        }

        const mgl_monitor *focused_monitor = find_monitor_by_cursor_position(window);
        const mgl::vec2i window_pos(focused_monitor->pos.x, focused_monitor->pos.y);
        const mgl::vec2i window_size(focused_monitor->size.x, focused_monitor->size.y);
        get_theme().set_window_size(window_size);

        window.set_size(window_size);
        window.set_size_limits(window_size, window_size);
        window.set_position(window_pos);

        update_compositor_texture(focused_monitor);

        audio_devices = get_audio_devices();
        config = read_config();

        bg_screenshot_overlay = mgl::Rectangle(mgl::vec2f(get_theme().window_width, get_theme().window_height));
        top_bar_background = mgl::Rectangle(mgl::vec2f(get_theme().window_width, get_theme().window_height*0.06f).floor());
        top_bar_text = mgl::Text("GPU Screen Recorder", get_theme().top_bar_font);
        logo_sprite = mgl::Sprite(&get_theme().logo_texture);
        close_button_widget.set_size(mgl::vec2f(top_bar_background.get_size().y * 0.3f, top_bar_background.get_size().y * 0.3f).floor());

        bg_screenshot_overlay.set_color(bg_color);
        top_bar_background.set_color(mgl::Color(0, 0, 0, 180));
        //top_bar_text.set_color(get_theme().tint_color);
        top_bar_text.set_position((top_bar_background.get_position() + top_bar_background.get_size()*0.5f - top_bar_text.get_bounds().size*0.5f).floor());
        close_button_widget.set_position(mgl::vec2f(get_theme().window_width - close_button_widget.get_size().x - 50.0f, top_bar_background.get_size().y * 0.5f - close_button_widget.get_size().y * 0.5f).floor());

        logo_sprite.set_height((int)(top_bar_background.get_size().y * 0.65f));
        logo_sprite.set_position(mgl::vec2f(
            (top_bar_background.get_size().y - logo_sprite.get_size().y) * 0.5f,
            top_bar_background.get_size().y * 0.5f - logo_sprite.get_size().y * 0.5f
        ).floor());

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
            auto button = std::make_unique<DropdownButton>(&get_theme().title_font, &get_theme().body_font, "Instant Replay", "On", "Off", &get_theme().replay_button_texture,
                mgl::vec2f(button_width, button_height));
            button->add_item("Start", "start");
            button->add_item("Settings", "settings");
            button->on_click = [&](const std::string &id) {
                if(id == "settings") {
                    auto replay_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::REPLAY, gsr_info, audio_devices, config, &page_stack);
                    page_stack.push(std::move(replay_settings_page));
                    return;
                }
                /*
                char window_to_record_str[32];
                snprintf(window_to_record_str, sizeof(window_to_record_str), "%ld", target_window);

                const char *args[] = {
                    "gpu-screen-recorder", "-w", window_to_record_str,
                    "-c", "mp4",
                    "-f", "60",
                    "-o", "/home/dec05eba/Videos/gpu-screen-recorder.mp4",
                    nullptr
                };
                exec_program_daemonized(args);
                */
            };
            main_buttons_list->add_widget(std::move(button));
        }
        {
            auto button = std::make_unique<DropdownButton>(&get_theme().title_font, &get_theme().body_font, "Record", "Recording", "Not recording", &get_theme().record_button_texture,
                mgl::vec2f(button_width, button_height));
            DropdownButton *button_ptr = button.get();
            button->add_item("Start", "start");
            button->add_item("Settings", "settings");
            button->on_click = [&, button_ptr](const std::string &id) {
                if(id == "settings") {
                    auto record_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::RECORD, gsr_info, audio_devices, config, &page_stack);
                    page_stack.push(std::move(record_settings_page));
                    return;
                }

                if(id != "start")
                    return;

                // window.close();
                // usleep(1000 * 50); // 50 milliseconds

                const std::string tint_color_as_hex = color_to_hex_str(get_theme().tint_color);

                if(gpu_screen_recorder_process > 0) {
                    kill(gpu_screen_recorder_process, SIGINT);
                    int status;
                    if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
                        perror("waitpid failed");
                        /* Ignore... */
                    }
                    // window.set_visible(false);
                    // window.close();
                    // return;
                    //exit(0);
                    gpu_screen_recorder_process = -1;
                    button_ptr->set_item_label(id, "Start");

                    // TODO: Show this with a slight delay to make sure it doesn't show up in the video
                    const std::string record_image_filepath = resources_path + "images/record.png";
                    const char *notification_args[] = {
                        "gsr-notify", "--text", "Recording has been saved", "--timeout", "3.0",
                        "--icon", record_image_filepath.c_str(),
                        "--icon-color", "ffffff", "--bg-color", tint_color_as_hex.c_str(),
                        nullptr
                    };
                    exec_program_daemonized(notification_args);
                    return;
                }

                const char *args[] = {
                    "gpu-screen-recorder", "-w", "screen",
                    "-c", "mp4",
                    "-f", "60",
                    "-o", "/home/dec05eba/Videos/gpu-screen-recorder.mp4",
                    nullptr
                };
                gpu_screen_recorder_process = exec_program(args);
                if(gpu_screen_recorder_process == -1) {
                    // TODO: Show notification failed to start
                } else {
                    button_ptr->set_item_label(id, "Stop");
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
                const std::string record_image_filepath = resources_path + "images/record.png";
                const char *notification_args[] = {
                    "gsr-notify", "--text", "Recording has started", "--timeout", "3.0",
                    "--icon", record_image_filepath.c_str(),
                    "--icon-color", tint_color_as_hex.c_str(), "--bg-color", tint_color_as_hex.c_str(),
                    nullptr
                };
                exec_program_daemonized(notification_args);
                //exit(0);
                // window.set_visible(false);
                // window.close();

                // TODO: Show notification with args:
                // "Recording has started" 3.0 ./images/record.png 76b900
            };
            main_buttons_list->add_widget(std::move(button));
        }
        {
            auto button = std::make_unique<DropdownButton>(&get_theme().title_font, &get_theme().body_font, "Livestream", "Streaming", "Not streaming", &get_theme().stream_button_texture,
                mgl::vec2f(button_width, button_height));
            button->add_item("Start", "start");
            button->add_item("Settings", "settings");
            button->on_click = [&](const std::string &id) {
                if(id == "settings") {
                    auto stream_settings_page = std::make_unique<SettingsPage>(SettingsPage::Type::STREAM, gsr_info, audio_devices, config, &page_stack);
                    page_stack.push(std::move(stream_settings_page));
                    return;
                }
            };
            main_buttons_list->add_widget(std::move(button));
        }

        const mgl::vec2f main_buttons_list_size = main_buttons_list->get_size();
        main_buttons_list->set_position((mgl::vec2f(window_size.x * 0.5f, window_size.y * 0.25f) - main_buttons_list_size * 0.5f).floor());
        front_page_ptr->add_widget(std::move(main_buttons_list));

        close_button_widget.draw_handler = [&](mgl::Window &window, mgl::vec2f pos, mgl::vec2f size) {
            if(mgl::FloatRect(pos, size).contains(window.get_mouse_position().to_vec2f())) {
                const float border_scale = 0.0015f;
                const int border_size = std::max(1.0f, border_scale * get_theme().window_height);
                draw_rectangle_outline(window, pos, size, get_theme().tint_color, border_size);
            }

            mgl::Sprite close_sprite(&get_theme().close_texture);
            close_sprite.set_position(pos);
            close_sprite.set_size(size);
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

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        window.set_fullscreen(true);
        window.set_visible(true);
        make_window_sticky(display, window.get_system_handle());

        if(default_cursor) {
            XFreeCursor(display, default_cursor);
            default_cursor = 0;
        }
        default_cursor = XCreateFontCursor(display, XC_arrow);

        // TODO: Retry if these fail.
        // TODO: Hmm, these dont work in owlboy. Maybe owlboy uses xi2 and that breaks this (does it?).
        // Remove these grabs when debugging with a debugger, or your X11 session will appear frozen

        XGrabPointer(display, window.get_system_handle(), True,
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
            Button1MotionMask | Button2MotionMask | Button3MotionMask | Button4MotionMask | Button5MotionMask |
            ButtonMotionMask,
            GrabModeAsync, GrabModeAsync, None, default_cursor, CurrentTime);
        // TODO: This breaks global hotkeys
        XGrabKeyboard(display, window.get_system_handle(), True, GrabModeAsync, GrabModeAsync, CurrentTime);

        XSetInputFocus(display, window.get_system_handle(), RevertToParent, CurrentTime);
        XFlush(display);

        //window.set_fullscreen(true);

        visible = true;

        mgl::Event event;
        event.type = mgl::Event::MouseMoved;
        event.mouse_move.x = window.get_mouse_position().x;
        event.mouse_move.y = window.get_mouse_position().y;
        on_event(event, window);
    }

    void Overlay::hide() {
        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;

        if(default_cursor) {
            XFreeCursor(display, default_cursor);
            default_cursor = 0;
        }

        XUngrabKeyboard(display, CurrentTime);
        XUngrabPointer(display, CurrentTime);
        XFlush(display);

        window_texture_deinit(&window_texture);
        visible = false;
        window.set_visible(false);
    }

    void Overlay::toggle_show() {
        if(visible)
            hide();
        else
            show();
    }

    bool Overlay::is_open() const {
        return visible;
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
            window_texture_loaded = window_texture_init(&window_texture, display, mgl_window_get_egl_display(window.internal_window()), window_at_cursor_position, egl_funcs) == 0;

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
}