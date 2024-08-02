
#include "../include/gui/StaticPage.hpp"
#include "../include/gui/ScrollablePage.hpp"
#include "../include/gui/DropdownButton.hpp"
#include "../include/gui/Button.hpp"
#include "../include/gui/ComboBox.hpp"
#include "../include/gui/Label.hpp"
#include "../include/Process.hpp"
#include "../include/Theme.hpp"
#include "../include/GsrInfo.hpp"

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

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <mglpp/mglpp.hpp>
#include <mglpp/graphics/Font.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/MemoryMappedFile.hpp>
#include <mglpp/system/Clock.hpp>

// TODO: If no compositor is running but a fullscreen application is running (on the focused monitor)
// then use xcomposite to get that window as a texture and use that as a background because then the background can update.
// That case can also happen when using a compositor but when the compositor gets turned off when running a fullscreen application.
// This can also happen after this overlay has started, in which case we want to update the background capture method.
// TODO: Update position when workspace changes (in dwm, sticky property handles it in real workspaces).
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

const mgl::Color bg_color(0, 0, 0, 160);

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

static Bool make_window_always_on_top(Display* display, Window window) {
    Atom net_wm_state_above_atom = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    if(!net_wm_state_above_atom) {
        fprintf(stderr, "Error: failed to find atom _NET_WM_STATE_ABOVE\n");
        return False;
    }
    
    return set_window_wm_state(display, window, net_wm_state_above_atom);
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

int main(int argc, char **argv) {
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

    gsr::init_theme(gsr_info);

    std::string program_root_dir = dirname(argv[0]);
    if(!program_root_dir.empty() && program_root_dir.back() != '/')
        program_root_dir += '/';
    program_root_dir += "../../../";

    mgl::Init init;
    Display *display = (Display*)mgl_get_context()->connection;

    // TODO: Put window on the focused monitor right side and update when monitor changes resolution or other modes.
    // Use monitor size instead of screen size.
    // mgl now has monitor events so this can be handled directly with mgl.

    mgl::vec2i target_monitor_size = { WidthOfScreen(DefaultScreenOfDisplay(display)), HeightOfScreen(DefaultScreenOfDisplay(display)) };

    const mgl::vec2i window_size = { target_monitor_size.x, target_monitor_size.y };
    const mgl::vec2i window_pos = { 0, 0 };

    mgl::Window::CreateParams window_create_params;
    window_create_params.size = window_size;
    window_create_params.min_size = window_size;
    window_create_params.max_size = window_size;
    window_create_params.position = window_pos;
    window_create_params.hidden = true;
    //window_create_params.override_redirect = true;
    window_create_params.background_color = bg_color;
    window_create_params.support_alpha = true;
    window_create_params.window_type = MGL_WINDOW_TYPE_DIALOG;

    mgl::Window window;
    if(!window.create("gsr overlay", window_create_params))
        startup_error("failed to create window");

    unsigned char data = 2; // Prefer being composed to allow transparency
    XChangeProperty(display, window.get_system_handle(), XInternAtom(display, "_NET_WM_BYPASS_COMPOSITOR", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

    data = 1;
    XChangeProperty(display, window.get_system_handle(), XInternAtom(display, "GAMESCOPE_EXTERNAL_OVERLAY", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

    mgl::MemoryMappedFile title_font_file;
    if(!title_font_file.load("/usr/share/fonts/noto/NotoSans-Bold.ttf", mgl::MemoryMappedFile::LoadOptions{true, false}))
        startup_error("failed to load file: fonts/Orbitron-Bold.ttf");

    mgl::MemoryMappedFile font_file;
    if(!font_file.load("/usr/share/fonts/noto/NotoSans-Regular.ttf", mgl::MemoryMappedFile::LoadOptions{true, false}))
        startup_error("failed to load file: fonts/Orbitron-Regular.ttf");

    mgl::Font top_bar_font;
    if(!top_bar_font.load_from_file(title_font_file, window_create_params.size.y * 0.03f))
        startup_error("failed to load font: fonts/Orbitron-Bold.ttf");

    mgl::Font title_font;
    if(!title_font.load_from_file(title_font_file, window_create_params.size.y * 0.019f))
        startup_error("failed to load font: fonts/Orbitron-Bold.ttf");

    mgl::Font font;
    if(!font.load_from_file(font_file, window_create_params.size.y * 0.015f))
        startup_error("failed to load font: fonts/Orbitron-Regular.ttf");

    mgl::Texture replay_button_texture;
    if(!replay_button_texture.load_from_file((program_root_dir + "images/replay.png").c_str()))
        startup_error("failed to load texture: images/replay.png");

    mgl::Texture record_button_texture;
    if(!record_button_texture.load_from_file((program_root_dir + "images/record.png").c_str()))
        startup_error("failed to load texture: images/record.png");

    mgl::Texture stream_button_texture;
    if(!stream_button_texture.load_from_file((program_root_dir + "images/stream.png").c_str()))
        startup_error("failed to load texture: images/stream.png");

    // TODO: Get size from monitor, get region specific to the monitor
    mgl::Texture screenshot_texture;
    if(!is_compositor_running(display, 0)) {
        XImage *img = XGetImage(display, DefaultRootWindow(display), window_pos.x, window_pos.y, window_size.x, window_size.y, AllPlanes, ZPixmap);
        if(!img)
            fprintf(stderr, "Error: failed to take a screenshot\n");

        if(img) {
            screenshot_texture = texture_from_ximage(img);
            XDestroyImage(img);
            img = NULL;
        }
    }

    mgl::Sprite screenshot_sprite;
    if(screenshot_texture.is_valid())
        screenshot_sprite.set_texture(&screenshot_texture);

    mgl::Rectangle bg_screenshot_overlay(window.get_size().to_vec2f());
    bg_screenshot_overlay.set_color(bg_color);

    gsr::StaticPage front_page(window_size.to_vec2f());

    const mgl::vec2f settings_page_size(window_size.x * 0.3333f, window_size.y * 0.7f);
    const mgl::vec2f settings_page_position = (window_size.to_vec2f() * 0.5f - settings_page_size * 0.5f).floor();

    auto replay_settings_content = std::make_unique<gsr::ScrollablePage>(settings_page_size);
    gsr::ScrollablePage *replay_settings_content_ptr = replay_settings_content.get();
    replay_settings_content->set_position(settings_page_position);

    auto record_settings_content = std::make_unique<gsr::ScrollablePage>(settings_page_size);
    gsr::ScrollablePage *record_settings_content_ptr = record_settings_content.get();
    record_settings_content->set_position(settings_page_position);

    auto stream_settings_content = std::make_unique<gsr::ScrollablePage>(settings_page_size);
    gsr::ScrollablePage *stream_settings_content_ptr = stream_settings_content.get();
    stream_settings_content->set_position(settings_page_position);

    gsr::StaticPage replay_settings_page(window_size.to_vec2f());
    replay_settings_page.add_widget(std::move(replay_settings_content));

    gsr::StaticPage record_settings_page(window_size.to_vec2f());
    record_settings_page.add_widget(std::move(record_settings_content));

    gsr::StaticPage stream_settings_page(window_size.to_vec2f());
    stream_settings_page.add_widget(std::move(stream_settings_content));

    gsr::Page *current_page = &front_page;

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

    const int button_height = window_create_params.size.y / 5.0f;
    const int button_width = button_height;

    std::vector<MainButton> main_buttons;

    for(int i = 0; i < num_frontpage_buttons; ++i) {
        auto button = std::make_unique<gsr::DropdownButton>(&title_font, &font, titles[i], descriptions_on[i], descriptions_off[i], textures[i], mgl::vec2f(button_width, button_height));
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

        const mgl::vec2i main_buttons_start_pos = mgl::vec2i(window_create_params.size.x*0.5f, window_create_params.size.y*0.25f) - overlay_desired_size/2;
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
            current_page = &replay_settings_page;
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
            current_page = &record_settings_page;
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
            const char *notification_args[] = {
                "gpu-screen-recorder-notification", "--text", "Recording has been saved", "--timeout", "3.0",
                "--icon", "./images/record.png",
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
        const char *notification_args[] = {
            "gpu-screen-recorder-notification", "--text", "Recording has started", "--timeout", "3.0",
            "--icon", "./images/record.png",
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
            current_page = &stream_settings_page;
            return;
        }
    };
    main_buttons[2].mode = gsr::GsrMode::Stream;

    update_overlay_shape();

    window.set_visible(true);
    make_window_always_on_top(display, window.get_system_handle());
    make_window_sticky(display, window.get_system_handle());

    Cursor default_cursor = XCreateFontCursor(display, XC_arrow);

    // TODO: Retry if these fail.
    // TODO: Hmm, these dont work in owlboy. Maybe owlboy uses xi2 and that breaks this (does it?).
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

    mgl::Rectangle top_bar_background(mgl::vec2f(window.get_size().x, window.get_size().y*0.06f).floor());
    top_bar_background.set_color(mgl::Color(0, 0, 0, 180));

    mgl::Text top_bar_text("GPU Screen Recorder", top_bar_font);
    //top_bar_text.set_color(gsr::get_theme().tint_color);
    top_bar_text.set_position((top_bar_background.get_position() + top_bar_background.get_size()*0.5f - top_bar_text.get_bounds().size*0.5f).floor());

    const int num_settings_pages = 3;

    gsr::Page *settings_pages[num_settings_pages] = {
        &replay_settings_page,
        &record_settings_page,
        &stream_settings_page
    };

    gsr::Page *settings_content_pages[num_settings_pages] = {
        replay_settings_content_ptr,
        record_settings_content_ptr,
        stream_settings_content_ptr
    };

    const auto settings_back_button_callback = [&] {
        current_page = &front_page;
    };

    const mgl::vec2f page_widget_spacing{window_size.x / 100.0f, window_size.y / 40.0f};
    for(int i = 0; i < num_settings_pages; ++i) {
        mgl::vec2f settings_page_widget_pos{50.0f, 50.0f};
        gsr::Page *settings_page = settings_pages[i];
        gsr::Page *settings_content_page = settings_content_pages[i];

        auto back_button = std::make_unique<gsr::Button>(&title_font, "Back", mgl::vec2f(window_size.x / 10, window_size.y / 15), gsr::get_theme().scrollable_page_bg_color);
        back_button->set_position(settings_page_position + mgl::vec2f(settings_page_size.x + window_size.x / 50, 0.0f).floor());
        back_button->on_click = settings_back_button_callback;
        settings_page->add_widget(std::move(back_button));

        auto record_area_label = std::make_unique<gsr::Label>(&title_font, "Record area:", gsr::get_theme().text_color);
        record_area_label->set_position(settings_page_widget_pos);
        settings_page_widget_pos.y += record_area_label->get_size().y + page_widget_spacing.y / 3;

        auto record_area_box = std::make_unique<gsr::ComboBox>(&title_font);
        record_area_box->set_position(settings_page_widget_pos);
        // TODO: Show options not supported but disable them
        if(gsr_info.supported_capture_options.window)
            record_area_box->add_item("Window", "window");
        if(gsr_info.supported_capture_options.focused)
            record_area_box->add_item("Focused window", "focused");
        if(gsr_info.supported_capture_options.screen)
            record_area_box->add_item("All monitors", "screen");
        for(const auto &monitor : gsr_info.supported_capture_options.monitors) {
            char name[256];
            snprintf(name, sizeof(name), "%s (%dx%d)", monitor.name.c_str(), monitor.size.x, monitor.size.y);
            record_area_box->add_item(name, monitor.name);
        }
        if(gsr_info.supported_capture_options.portal)
            record_area_box->add_item("Desktop portal", "portal");
        settings_page_widget_pos.y += record_area_box->get_size().y + page_widget_spacing.y;

        auto audio_device_box = std::make_unique<gsr::ComboBox>(&title_font);
        audio_device_box->set_position(settings_page_widget_pos);
        for(const auto &audio_device : audio_devices) {
            audio_device_box->add_item(audio_device.description, audio_device.name);
        }
        settings_page_widget_pos.y += audio_device_box->get_size().y + page_widget_spacing.y;

        auto framerate_label = std::make_unique<gsr::Label>(&title_font, "Frame rate:", gsr::get_theme().text_color);
        framerate_label->set_position(settings_page_widget_pos);
        settings_page_widget_pos.y += framerate_label->get_size().y + page_widget_spacing.y / 3;

        auto framerate_box = std::make_unique<gsr::ComboBox>(&title_font);
        framerate_box->set_position(settings_page_widget_pos);
        framerate_box->add_item("60", "60");
        framerate_box->add_item("30", "30");
        settings_page_widget_pos.y += framerate_box->get_size().y + page_widget_spacing.y;

        auto video_quality_label = std::make_unique<gsr::Label>(&title_font, "Video quality:", gsr::get_theme().text_color);
        video_quality_label->set_position(settings_page_widget_pos);
        const auto video_quality_label_pos = video_quality_label->get_position();
        settings_page_widget_pos.y += video_quality_label->get_size().y + page_widget_spacing.y / 3;

        auto video_quality_box = std::make_unique<gsr::ComboBox>(&title_font);
        video_quality_box->set_position(settings_page_widget_pos);
        video_quality_box->add_item("Medium", "medium");
        video_quality_box->add_item("High (Recommended for live streaming)", "high");
        video_quality_box->add_item("Very High (Recommended)", "very_high");
        video_quality_box->add_item("Ultra", "ultra");
        const auto video_quality_box_pos = video_quality_box->get_position();
        const mgl::vec2f video_quality_box_size = video_quality_box->get_size();
        settings_page_widget_pos.y += video_quality_box_size.y + page_widget_spacing.y;

        auto color_range_label = std::make_unique<gsr::Label>(&title_font, "Color range:", gsr::get_theme().text_color);
        color_range_label->set_position(video_quality_label_pos + mgl::vec2f(video_quality_box_size.x + page_widget_spacing.x, 0.0f).floor());

        auto color_range_box = std::make_unique<gsr::ComboBox>(&title_font);
        color_range_box->set_position(video_quality_box_pos + mgl::vec2f(video_quality_box_size.x + page_widget_spacing.x, 0.0f).floor());
        color_range_box->add_item("Limited", "limited");
        color_range_box->add_item("Full", "full");

        auto video_codec_label = std::make_unique<gsr::Label>(&title_font, "Video codec:", gsr::get_theme().text_color);
        video_codec_label->set_position(settings_page_widget_pos);
        settings_page_widget_pos.y += video_codec_label->get_size().y + page_widget_spacing.y / 3;

        auto video_codec_box = std::make_unique<gsr::ComboBox>(&title_font);
        video_codec_box->set_position(settings_page_widget_pos);
        // TODO: Show options not supported but disable them
        video_codec_box->add_item("Auto (Recommended)", "auto");
        if(gsr_info.supported_video_codecs.h264)
            video_codec_box->add_item("H264", "h264");
        if(gsr_info.supported_video_codecs.hevc)
            video_codec_box->add_item("HEVC", "hevc");
        if(gsr_info.supported_video_codecs.av1)
            video_codec_box->add_item("AV1", "av1");
        if(gsr_info.supported_video_codecs.vp8)
            video_codec_box->add_item("VP8", "vp8");
        if(gsr_info.supported_video_codecs.vp9)
            video_codec_box->add_item("VP9", "vp9");
        // TODO: Add hdr options
        video_codec_box->add_item("H264 Software Encoder (Slow, not recommended)", "h264_software");
        settings_page_widget_pos.y += video_codec_box->get_size().y + page_widget_spacing.y;

        auto audio_codec_label = std::make_unique<gsr::Label>(&title_font, "Audio codec:", gsr::get_theme().text_color);
        audio_codec_label->set_position(settings_page_widget_pos);
        settings_page_widget_pos.y += audio_codec_label->get_size().y + page_widget_spacing.y / 3;

        auto audio_codec_box = std::make_unique<gsr::ComboBox>(&title_font);
        audio_codec_box->set_position(settings_page_widget_pos);
        audio_codec_box->add_item("Opus (Recommended)", "opus");
        audio_codec_box->add_item("AAC", "aac");
        settings_page_widget_pos.y += audio_codec_box->get_size().y + page_widget_spacing.y;

        auto framerate_mode_label = std::make_unique<gsr::Label>(&title_font, "Frame rate mode:", gsr::get_theme().text_color);
        framerate_mode_label->set_position(settings_page_widget_pos);
        settings_page_widget_pos.y += framerate_mode_label->get_size().y + page_widget_spacing.y / 3;

        auto framerate_mode_box = std::make_unique<gsr::ComboBox>(&title_font);
        framerate_mode_box->set_position(settings_page_widget_pos);
        framerate_mode_box->add_item("Auto (Recommended)", "auto");
        framerate_mode_box->add_item("Constant", "cfr");
        framerate_mode_box->add_item("Variable", "vfr");
        settings_page_widget_pos.y += framerate_mode_box->get_size().y + page_widget_spacing.y;

        auto container_label = std::make_unique<gsr::Label>(&title_font, "Container:", gsr::get_theme().text_color);
        container_label->set_position(settings_page_widget_pos);
        settings_page_widget_pos.y += container_label->get_size().y + page_widget_spacing.y / 3;

        auto container_box = std::make_unique<gsr::ComboBox>(&title_font);
        container_box->set_position(settings_page_widget_pos);
        container_box->add_item("mp4", "mp4");
        container_box->add_item("mkv", "matroska");
        container_box->add_item("flv", "flv");
        container_box->add_item("mov", "mov");
        container_box->add_item("ts", "mpegts");
        container_box->add_item("m3u8", "hls");
        settings_page_widget_pos.y += container_box->get_size().y + page_widget_spacing.y;

        settings_content_page->add_widget(std::move(record_area_label));
        settings_content_page->add_widget(std::move(record_area_box));
        settings_content_page->add_widget(std::move(audio_device_box));
        settings_content_page->add_widget(std::move(framerate_label));
        settings_content_page->add_widget(std::move(framerate_box));
        settings_content_page->add_widget(std::move(video_quality_label));
        settings_content_page->add_widget(std::move(video_quality_box));
        settings_content_page->add_widget(std::move(color_range_label));
        settings_content_page->add_widget(std::move(color_range_box));
        settings_content_page->add_widget(std::move(video_codec_label));
        settings_content_page->add_widget(std::move(video_codec_box));
        settings_content_page->add_widget(std::move(audio_codec_label));
        settings_content_page->add_widget(std::move(audio_codec_box));
        settings_content_page->add_widget(std::move(framerate_mode_label));
        settings_content_page->add_widget(std::move(framerate_mode_box));
        settings_content_page->add_widget(std::move(container_label));
        settings_content_page->add_widget(std::move(container_box));
    }

    mgl::Texture close_texture;
    if(!close_texture.load_from_file("images/cross.png"))
        startup_error("failed to load texture: images/cross.png");

    mgl::Sprite close_sprite(&close_texture);
    close_sprite.set_height(int(top_bar_background.get_size().y * 0.3f));
    close_sprite.set_position(mgl::vec2f(window.get_size().x - close_sprite.get_size().x - 50.0f, top_bar_background.get_size().y * 0.5f - close_sprite.get_size().y * 0.5f).floor());

    mgl::Texture logo_texture;
    if(!logo_texture.load_from_file("images/gpu_screen_recorder_logo.png"))
        startup_error("failed to load texture: images/gpu_screen_recorder_logo.png");

    mgl::Sprite logo_sprite(&logo_texture);
    logo_sprite.set_height((int)(top_bar_background.get_size().y * 0.65f));

    logo_sprite.set_position(mgl::vec2f(
        (top_bar_background.get_size().y - logo_sprite.get_size().y) * 0.5f,
        top_bar_background.get_size().y * 0.5f - logo_sprite.get_size().y * 0.5f
    ).floor());

    // mgl::Clock state_update_timer;
    // const double state_update_timeout_sec = 2.0;

    mgl::Event event;

    event.type = mgl::Event::MouseMoved;
    event.mouse_move.x = window.get_mouse_position().x;
    event.mouse_move.y = window.get_mouse_position().y;
    current_page->on_event(event, window, mgl::vec2f(0.0f, 0.0f));

    const auto render = [&] {
        window.clear(bg_color);
        if(screenshot_texture.is_valid()) {
            window.draw(screenshot_sprite);
            window.draw(bg_screenshot_overlay);
        }
        window.draw(top_bar_background);
        window.draw(top_bar_text);
        window.draw(logo_sprite);
        window.draw(close_sprite);
        current_page->draw(window, mgl::vec2f(0.0f, 0.0f));
        window.display();
    };

    while(window.is_open()) {
        if(!running) {
            window.set_visible(false);
            window.close();
            break;
        }

        while(window.poll_event(event)) {
            current_page->on_event(event, window, mgl::vec2f(0.0f, 0.0f));
            if(event.type == mgl::Event::KeyPressed) {
                if(event.key.code == mgl::Keyboard::Escape) {
                    window.set_visible(false);
                    window.close();
                    break;
                }
            }
        }

        // if(state_update_timer.get_elapsed_time_seconds() >= state_update_timeout_sec) {
        //     state_update_timer.restart();
        //     update_overlay_shape();
        // }

        render();
    }

    fprintf(stderr, "shutting down!\n");

    if(gpu_screen_recorder_process != -1) {
        kill(gpu_screen_recorder_process, SIGINT);
        int status;
        if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
            perror("waitpid failed");
            /* Ignore... */
        }
        gpu_screen_recorder_process = -1;
    }

    gsr::deinit_theme();
    return 0;
}
