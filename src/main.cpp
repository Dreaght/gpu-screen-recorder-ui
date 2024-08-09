
#include "../include/gui/StaticPage.hpp"
#include "../include/gui/DropdownButton.hpp"
#include "../include/gui/CustomRendererWidget.hpp"
#include "../include/gui/SettingsPage.hpp"
#include "../include/gui/Utils.hpp"
#include "../include/Process.hpp"
#include "../include/Theme.hpp"
#include "../include/GsrInfo.hpp"
#include "../include/window_texture.h"
#include "../include/Config.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <sys/wait.h>
#include <optional>
#include <signal.h>
#include <assert.h>
#include <stack>

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <mglpp/mglpp.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/Clock.hpp>

// TODO: Make keyboard controllable for steam deck (and other controllers).
// TODO: Keep track of gpu screen recorder run by other programs to not allow recording at the same time, or something.
// TODO: Remove gpu-screen-recorder-overlay-daemon and handle that alt+z global hotkey here instead, to show/hide the window
// without restaring the program. Or make the daemon handle gpu screen recorder program state and pass that to the overlay.
// TODO: Add systray by using org.kde.StatusNotifierWatcher/etc dbus directly.
// TODO: Dont allow replay and record/stream at the same time. If we want to allow that then do that in gpu screen recorder instead
// to make it more efficient by doing record/replay/stream with the same encoded packets.
// TODO: Make sure the overlay always stays on top. Test with starting the overlay and then opening youtube in fullscreen.

#include <vector>

extern "C" {
#include <mgl/mgl.h>
}

const mgl::Color bg_color(0, 0, 0, 100);

static void usage() {
    fprintf(stderr, "usage: window-overlay\n");
    exit(1);
}

static void startup_error(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
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

static bool is_compositor_running(Display *dpy, int screen) {
    char prop_name[20];
    snprintf(prop_name, sizeof(prop_name), "_NET_WM_CM_S%d", screen);
    Atom prop_atom = XInternAtom(dpy, prop_name, False);
    return XGetSelectionOwner(dpy, prop_atom) != None;
}

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

static sig_atomic_t running = 1;
static void sigint_handler(int dummy) {
    (void)dummy;
    running = 0;
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

int main(int argc, char **argv) {
    (void)argv;
    if(argc != 1)
        usage();

    signal(SIGINT, sigint_handler);

    gsr::GsrInfo gsr_info;
    // TODO:
    gsr::GsrInfoExitStatus gsr_info_exit_status = gsr::get_gpu_screen_recorder_info(&gsr_info);
    if(gsr_info_exit_status != gsr::GsrInfoExitStatus::OK) {
        fprintf(stderr, "error: failed to get gpu-screen-recorder info\n");
        exit(1);
    }

    const std::vector<gsr::AudioDevice> audio_devices = gsr::get_audio_devices();

    std::string resources_path;
    if(access("images/gpu_screen_recorder_logo.png", F_OK) == 0) {
        resources_path = "./";
    } else {
#ifdef GSR_OVERLAY_RESOURCES_PATH
        resources_path = GSR_OVERLAY_RESOURCES_PATH "/";
#else
        resources_path = "/usr/share/gsr-overlay/";
#endif
    }

    mgl::Init init;
    mgl_context *context = mgl_get_context();
    Display *display = (Display*)context->connection;

    egl_functions egl_funcs;
    egl_funcs.eglGetError = (decltype(egl_funcs.eglGetError))context->gl.eglGetProcAddress("eglGetError");
    egl_funcs.eglCreateImage = (decltype(egl_funcs.eglCreateImage))context->gl.eglGetProcAddress("eglCreateImage");
    egl_funcs.eglDestroyImage = (decltype(egl_funcs.eglDestroyImage))context->gl.eglGetProcAddress("eglDestroyImage");
    egl_funcs.glEGLImageTargetTexture2DOES = (decltype(egl_funcs.glEGLImageTargetTexture2DOES))context->gl.eglGetProcAddress("glEGLImageTargetTexture2DOES");

    if(!egl_funcs.eglGetError || !egl_funcs.eglCreateImage || !egl_funcs.eglDestroyImage || !egl_funcs.glEGLImageTargetTexture2DOES) {
        fprintf(stderr, "Error: required opengl functions not available on your system\n");
        exit(1);
    }

    const bool compositor_running = is_compositor_running(display, DefaultScreen(display));

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

    mgl::Window window;
    if(!window.create("gsr overlay", window_create_params))
        startup_error("failed to create window");

    mgl_window *win = window.internal_window();
    if(win->num_monitors == 0)
        startup_error("no monitors found");

    const mgl_monitor *focused_monitor = find_monitor_by_cursor_position(window);
    window_pos.x = focused_monitor->pos.x;
    window_pos.y = focused_monitor->pos.y;
    window_size.x = focused_monitor->size.x;
    window_size.y = focused_monitor->size.y;

    window.set_size_limits(window_size, window_size);
    window.set_size(window_size);
    window.set_position(window_pos);

    if(!gsr::init_theme(gsr_info, window_size, resources_path)) {
        fprintf(stderr, "Error: failed to load theme\n");
        exit(1);
    }

    unsigned char data = 2; // Prefer being composed to allow transparency
    XChangeProperty(display, window.get_system_handle(), XInternAtom(display, "_NET_WM_BYPASS_COMPOSITOR", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

    data = 1;
    XChangeProperty(display, window.get_system_handle(), XInternAtom(display, "GAMESCOPE_EXTERNAL_OVERLAY", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

    mgl::Texture replay_button_texture;
    if(!replay_button_texture.load_from_file((resources_path + "images/replay.png").c_str()))
        startup_error("failed to load texture: images/replay.png");

    mgl::Texture record_button_texture;
    if(!record_button_texture.load_from_file((resources_path + "images/record.png").c_str()))
        startup_error("failed to load texture: images/record.png");

    mgl::Texture stream_button_texture;
    if(!stream_button_texture.load_from_file((resources_path + "images/stream.png").c_str()))
        startup_error("failed to load texture: images/stream.png");

    WindowTexture window_texture;
    bool window_texture_loaded = false;

    mgl_texture window_texture_tex;
    memset(&window_texture_tex, 0, sizeof(window_texture_tex));
    mgl::Texture window_texture_texture;
    mgl::Sprite window_texture_sprite;

    mgl::Texture screenshot_texture;
    if(!compositor_running) {
        const Window window_at_cursor_position = get_window_at_cursor_position(display);
        if(is_window_fullscreen_on_monitor(display, window_at_cursor_position, focused_monitor) && window_at_cursor_position)
            window_texture_loaded = window_texture_init(&window_texture, display, mgl_window_get_egl_display(window.internal_window()), window_at_cursor_position, egl_funcs) == 0;

        if(window_texture_loaded && window_texture.texture_id) {
            DrawableGeometry geometry;
            get_drawable_geometry(display, (Drawable)window_texture.pixmap, &geometry);

            window_texture_tex.id = window_texture.texture_id;
            window_texture_tex.width = geometry.width;
            window_texture_tex.height = geometry.height;
            window_texture_tex.format = MGL_TEXTURE_FORMAT_RGB;
            window_texture_tex.max_width = 1 << 15;
            window_texture_tex.max_height = 1 << 15;
            window_texture_tex.pixel_coordinates = false;
            window_texture_tex.mipmap = false;
            window_texture_texture = mgl::Texture::reference(window_texture_tex);
            window_texture_sprite.set_texture(&window_texture_texture);
        } else {
            XImage *img = XGetImage(display, DefaultRootWindow(display), window_pos.x, window_pos.y, window_size.x, window_size.y, AllPlanes, ZPixmap);
            if(!img)
                fprintf(stderr, "Error: failed to take a screenshot\n");

            if(img) {
                screenshot_texture = texture_from_ximage(img);
                XDestroyImage(img);
                img = NULL;
            }
        }
    }

    mgl::Sprite screenshot_sprite;
    if(screenshot_texture.is_valid())
        screenshot_sprite.set_texture(&screenshot_texture);

    mgl::Rectangle bg_screenshot_overlay(window_size.to_vec2f());
    bg_screenshot_overlay.set_color(bg_color);

    gsr::StaticPage front_page(window_size.to_vec2f());

    std::stack<gsr::Page*> page_stack;
    page_stack.push(&front_page);

    const auto settings_back_button_callback = [&] {
        page_stack.top()->on_navigate_away_from_page();
        page_stack.pop();
    };

    std::optional<gsr::Config> config = gsr::read_config();

    gsr::SettingsPage replay_settings_page(gsr::SettingsPage::Type::REPLAY, gsr_info, audio_devices, config);
    replay_settings_page.on_back_button_handler = settings_back_button_callback;

    gsr::SettingsPage record_settings_page(gsr::SettingsPage::Type::RECORD, gsr_info, audio_devices, config);
    record_settings_page.on_back_button_handler = settings_back_button_callback;

    gsr::SettingsPage stream_settings_page(gsr::SettingsPage::Type::STREAM, gsr_info, audio_devices, config);
    stream_settings_page.on_back_button_handler = settings_back_button_callback;

    if(!config) {
        replay_settings_page.save();
        record_settings_page.save();
        stream_settings_page.save();
    }

    struct MainButton {
        gsr::DropdownButton* button;
        gsr::GsrMode mode;
    };

    const int num_frontpage_buttons = 3;

    const char *titles[num_frontpage_buttons] = {
        "Instant Replay",
        "Record",
        "Livestream"
    };

    const char *descriptions_off[num_frontpage_buttons] = {
        "Off",
        "Not recording",
        "Not streaming"
    };

    const char *descriptions_on[num_frontpage_buttons] = {
        "On",
        "Recording",
        "Streaming"
    };

    mgl::Texture *textures[num_frontpage_buttons] = {
        &replay_button_texture,
        &record_button_texture,
        &stream_button_texture
    };

    const int button_height = window_size.y / 5.0f;
    const int button_width = button_height;

    std::vector<MainButton> main_buttons;

    for(int i = 0; i < num_frontpage_buttons; ++i) {
        auto button = std::make_unique<gsr::DropdownButton>(&gsr::get_theme().title_font, &gsr::get_theme().body_font, titles[i], descriptions_on[i], descriptions_off[i], textures[i], mgl::vec2f(button_width, button_height));
        button->add_item("Start", "start");
        button->add_item("Settings", "settings");
        gsr::DropdownButton *button_ptr = button.get();
        front_page.add_widget(std::move(button));

        MainButton main_button = {
            button_ptr,
            gsr::GsrMode::Unknown
        };

        main_buttons.push_back(std::move(main_button));
    }

    gsr::GsrMode gsr_mode = gsr::GsrMode::Unknown;

    auto update_overlay_shape = [&]() {
        fprintf(stderr, "update overlay shape!\n");
        const int spacing = 0;// * get_config().scale;
        const int combined_spacing = spacing * std::max(0, (int)main_buttons.size() - 1);

        const int per_button_width = main_buttons[0].button->get_size().x;// * get_config().scale;
        const mgl::vec2i overlay_desired_size(per_button_width * (int)main_buttons.size() + combined_spacing, main_buttons[0].button->get_size().y);

        const mgl::vec2i main_buttons_start_pos = mgl::vec2i(window_size.x*0.5f, window_size.y*0.25f) - overlay_desired_size/2;
        mgl::vec2i main_button_pos = main_buttons_start_pos;

        // if(!gsr_mode.has_value()) {
        //     gsr_mode = gsr::GsrMode::Unknown;
        //     pid_t gpu_screen_recorder_process = -1;
        //     gsr::is_gpu_screen_recorder_running(gpu_screen_recorder_process, gsr_mode.value());
        // }

        for(size_t i = 0; i < main_buttons.size(); ++i) {
            if(main_buttons[i].mode != gsr::GsrMode::Unknown && main_buttons[i].mode == gsr_mode) {
                main_buttons[i].button->set_activated(true);
            } else {
                main_buttons[i].button->set_activated(false);
            }

            main_buttons[i].button->set_position(main_button_pos.to_vec2f());
            main_button_pos.x += per_button_width + combined_spacing;
        }
    };

    // Replay
    main_buttons[0].button->on_click = [&](const std::string &id) {
        if(id == "settings") {
            page_stack.push(&replay_settings_page);
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
        gsr::exec_program_daemonized(args);
        */
    };
    main_buttons[0].mode = gsr::GsrMode::Replay;

    // TODO: Monitor /tmp/gpu-screen-recorder and update ui to match state

    pid_t gpu_screen_recorder_process = -1;
    // Record
    main_buttons[1].button->on_click = [&](const std::string &id) {
        if(id == "settings") {
            page_stack.push(&record_settings_page);
            return;
        }

        if(id != "start")
            return;

        // window.close();
        // usleep(1000 * 50); // 50 milliseconds

        const std::string tint_color_as_hex = color_to_hex_str(gsr::get_theme().tint_color);

        if(gpu_screen_recorder_process != -1) {
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
            gsr_mode = gsr::GsrMode::Unknown;
            update_overlay_shape();
            main_buttons[1].button->set_item_label(id, "Start");

            // TODO: Show this with a slight delay to make sure it doesn't show up in the video
            const std::string record_image_filepath = resources_path + "images/record.png";
            const char *notification_args[] = {
                "gsr-notify", "--text", "Recording has been saved", "--timeout", "3.0",
                "--icon", record_image_filepath.c_str(),
                "--icon-color", "ffffff", "--bg-color", tint_color_as_hex.c_str(),
                nullptr
            };
            gsr::exec_program_daemonized(notification_args);
            return;
        }

        const char *args[] = {
            "gpu-screen-recorder", "-w", "screen",
            "-c", "mp4",
            "-f", "60",
            "-o", "/home/dec05eba/Videos/gpu-screen-recorder.mp4",
            nullptr
        };
        gpu_screen_recorder_process = gsr::exec_program(args);
        if(gpu_screen_recorder_process == -1) {
            // TODO: Show notification failed to start
        } else {
            gsr_mode = gsr::GsrMode::Record;
            update_overlay_shape();
            main_buttons[1].button->set_item_label(id, "Stop");
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
        gsr::exec_program_daemonized(notification_args);
        //exit(0);
        // window.set_visible(false);
        // window.close();

        // TODO: Show notification with args:
        // "Recording has started" 3.0 ./images/record.png 76b900
    };
    main_buttons[1].mode = gsr::GsrMode::Record;

    // Stream
    main_buttons[2].button->on_click = [&](const std::string &id) {
        if(id == "settings") {
            page_stack.push(&stream_settings_page);
            return;
        }
    };
    main_buttons[2].mode = gsr::GsrMode::Stream;

    update_overlay_shape();

    window.set_visible(true);
    make_window_sticky(display, window.get_system_handle());

    Cursor default_cursor = XCreateFontCursor(display, XC_arrow);

    // TODO: Retry if these fail.
    // TODO: Hmm, these dont work in owlboy. Maybe owlboy uses xi2 and that breaks this (does it?).
    // Remove these grabs when debugging with a debugger, or your X11 session will appear frozen
    XGrabPointer(display, window.get_system_handle(), True,
       ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
       Button1MotionMask | Button2MotionMask | Button3MotionMask | Button4MotionMask | Button5MotionMask |
       ButtonMotionMask,
       GrabModeAsync, GrabModeAsync, None, default_cursor, CurrentTime);
    XGrabKeyboard(display, window.get_system_handle(), True, GrabModeAsync, GrabModeAsync, CurrentTime);

    XSetInputFocus(display, window.get_system_handle(), RevertToParent, CurrentTime);
    XFlush(display);

    window.set_fullscreen(true);

    //XGrabServer(display);

    mgl::Rectangle top_bar_background(mgl::vec2f(window_size.x, window_size.y*0.06f).floor());
    top_bar_background.set_color(mgl::Color(0, 0, 0, 180));

    mgl::Text top_bar_text("GPU Screen Recorder", gsr::get_theme().top_bar_font);
    //top_bar_text.set_color(gsr::get_theme().tint_color);
    top_bar_text.set_position((top_bar_background.get_position() + top_bar_background.get_size()*0.5f - top_bar_text.get_bounds().size*0.5f).floor());

    mgl::Texture close_texture;
    if(!close_texture.load_from_file((resources_path + "images/cross.png").c_str()))
        startup_error("failed to load texture: images/cross.png");

    gsr::CustomRendererWidget close_button_widget(mgl::vec2f(top_bar_background.get_size().y * 0.3f, top_bar_background.get_size().y * 0.3f).floor());
    close_button_widget.set_position(mgl::vec2f(window_size.x - close_button_widget.get_size().x - 50.0f, top_bar_background.get_size().y * 0.5f - close_button_widget.get_size().y * 0.5f).floor());
    close_button_widget.draw_handler = [&](mgl::Window &window, mgl::vec2f pos, mgl::vec2f size) {
        if(mgl::FloatRect(pos, size).contains(window.get_mouse_position().to_vec2f())) {
            const float border_scale = 0.0015f;
            const int border_size = std::max(1.0f, border_scale * gsr::get_theme().window_height);
            gsr::draw_rectangle_outline(window, pos, size, gsr::get_theme().tint_color, border_size);
        }

        mgl::Sprite close_sprite(&close_texture);
        close_sprite.set_position(pos);
        close_sprite.set_size(size);
        window.draw(close_sprite);
    };
    bool close_button_pressed_inside = false;
    close_button_widget.event_handler = [&](mgl::Event &event, mgl::Window&, mgl::vec2f pos, mgl::vec2f size) {
        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            close_button_pressed_inside = mgl::FloatRect(pos, size).contains(mgl::vec2f(event.mouse_button.x, event.mouse_button.y));
        } else if(event.type == mgl::Event::MouseButtonReleased && event.mouse_button.button == mgl::Mouse::Left && close_button_pressed_inside) {
            if(mgl::FloatRect(pos, size).contains(mgl::vec2f(event.mouse_button.x, event.mouse_button.y))) {
                running = false;
                return false;
            }
        }
        return true;
    };

    mgl::Texture logo_texture;
    if(!logo_texture.load_from_file((resources_path + "images/gpu_screen_recorder_logo.png").c_str()))
        startup_error("failed to load texture: images/gpu_screen_recorder_logo.png");

    mgl::Sprite logo_sprite(&logo_texture);
    logo_sprite.set_height((int)(top_bar_background.get_size().y * 0.65f));

    logo_sprite.set_position(mgl::vec2f(
        (top_bar_background.get_size().y - logo_sprite.get_size().y) * 0.5f,
        top_bar_background.get_size().y * 0.5f - logo_sprite.get_size().y * 0.5f
    ).floor());

    mgl::Event event;

    event.type = mgl::Event::MouseMoved;
    event.mouse_move.x = window.get_mouse_position().x;
    event.mouse_move.y = window.get_mouse_position().y;
    page_stack.top()->on_event(event, window, mgl::vec2f(0.0f, 0.0f));

    const auto render = [&] {
        window.clear(bg_color);
        if(window_texture_loaded && window_texture.texture_id) {
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
        page_stack.top()->draw(window, mgl::vec2f(0.0f, 0.0f));
        window.display();
    };

    while(window.is_open()) {
        if(page_stack.empty() || !running) {
            running = false;
            goto quit;
        }

        while(window.poll_event(event)) {
            page_stack.top()->on_event(event, window, mgl::vec2f(0.0f, 0.0f));
            close_button_widget.on_event(event, window, mgl::vec2f(0.0f, 0.0f));
            if(event.type == mgl::Event::KeyReleased) {
                if(event.key.code == mgl::Keyboard::Escape && !page_stack.empty()) {
                    page_stack.top()->on_navigate_away_from_page();
                    page_stack.pop();
                }
            }

            if(page_stack.empty())
                break;
        }

        // if(state_update_timer.get_elapsed_time_seconds() >= state_update_timeout_sec) {
        //     state_update_timer.restart();
        //     update_overlay_shape();
        // }

        if(!page_stack.empty())
            render();
    }

    quit:
    fprintf(stderr, "shutting down!\n");
    if(window_texture_loaded)
        window_texture_deinit(&window_texture);
    gsr::deinit_theme();
    window.close();

    if(gpu_screen_recorder_process != -1) {
        kill(gpu_screen_recorder_process, SIGINT);
        int status;
        if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
            perror("waitpid failed");
            /* Ignore... */
        }
        gpu_screen_recorder_process = -1;
    }

    return 0;
}
