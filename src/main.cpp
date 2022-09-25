
#include "../include/gui/WidgetContainer.hpp"
#include "../include/gui/Button.hpp"
#include "../include/gui/ComboBox.hpp"
#include "../include/Process.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>

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

#include <vector>

extern "C" {
#include <mgl/mgl.h>
}

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
    Atom net_wm_state_atom = XInternAtom(display, "_NET_WM_STATE", True);
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
    Atom net_wm_state_above_atom = XInternAtom(display, "_NET_WM_STATE_ABOVE", True);
    if(!net_wm_state_above_atom) {
        fprintf(stderr, "Error: failed to find atom _NET_WM_STATE_ABOVE\n");
        return False;
    }
    
    return set_window_wm_state(display, window, net_wm_state_above_atom);
}

static Bool make_window_sticky(Display* display, Window window) {
    Atom net_wm_state_sticky_atom = XInternAtom(display, "_NET_WM_STATE_STICKY", True);
    if(!net_wm_state_sticky_atom) {
        fprintf(stderr, "Error: failed to find atom _NET_WM_STATE_STICKY\n");
        return False;
    }
    
    return set_window_wm_state(display, window, net_wm_state_sticky_atom);
}

int main(int argc, char **argv) {
    if(argc != 1)
        usage();

    std::string program_root_dir = dirname(argv[0]);
    if(!program_root_dir.empty() && program_root_dir.back() != '/')
        program_root_dir += '/';
    program_root_dir += "../../../";

    mgl::Init init;
    Display *display = (Display*)mgl_get_context()->connection;

    // TODO: Put window on the focused monitor right side and update when monitor changes resolution or other modes.
    // Use monitor size instead of screen size

    mgl::vec2i target_monitor_pos = { 0, 0 };
    mgl::vec2i target_monitor_size = { WidthOfScreen(DefaultScreenOfDisplay(display)), HeightOfScreen(DefaultScreenOfDisplay(display)) };

    const mgl::vec2i window_size = { target_monitor_size.x, target_monitor_size.y };
    const mgl::vec2i window_target_pos = { target_monitor_pos.x, target_monitor_pos.y };
    const mgl::vec2i window_start_pos = { window_target_pos.x, window_target_pos.y };

    mgl::Window::CreateParams window_create_params;
    window_create_params.size = window_size;
    window_create_params.position = window_start_pos;
    window_create_params.hidden = true;
    window_create_params.override_redirect = true;

    mgl::Window window;
    if(!window.create("gsr overlay", window_create_params))
        startup_error("failed to create window");

    Atom type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom value = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(display, DefaultRootWindow(display), type, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&value), 1);

    mgl::MemoryMappedFile title_font_file;
    if(!title_font_file.load((program_root_dir + "fonts/Orbitron-Bold.ttf").c_str(), mgl::MemoryMappedFile::LoadOptions{true, false}))
        startup_error("failed to load file: fonts/Orbitron-Bold.ttf");

    mgl::MemoryMappedFile font_file;
    if(!font_file.load((program_root_dir + "fonts/Orbitron-Regular.ttf").c_str(), mgl::MemoryMappedFile::LoadOptions{true, false}))
        startup_error("failed to load file: fonts/Orbitron-Regular.ttf");

    mgl::Font top_bar_font;
    if(!top_bar_font.load_from_file(title_font_file, window_create_params.size.y * 0.03f))
        startup_error("failed to load font: fonts/Orbitron-Bold.ttf");

    mgl::Font title_font;
    if(!title_font.load_from_file(title_font_file, window_create_params.size.y * 0.012f))
        startup_error("failed to load font: fonts/Orbitron-Bold.ttf");

    mgl::Font font;
    if(!font.load_from_file(font_file, window_create_params.size.y * 0.008f))
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

    struct MainButton {
        mgl::Text title;
        mgl::Text description;
        mgl::Sprite icon;
        std::unique_ptr<gsr::Button> button;
        gsr::GsrMode mode;
    };

    const char *titles[] = {
        "Instant Replay",
        "Record",
        "Livestream"
    };

    const char *descriptions_off[] = {
        "Off",
        "Not recording",
        "Not streaming"
    };

    const char *descriptions_on[] = {
        "On",
        "Recording",
        "Streaming"
    };

    mgl::Texture *textures[] = {
        &replay_button_texture,
        &record_button_texture,
        &stream_button_texture
    };

    std::vector<MainButton> main_buttons;

    for(int i = 0; i < 3; ++i) {
        mgl::Text title(titles[i], {0.0f, 0.0f}, title_font);
        title.set_color(mgl::Color(255, 255, 255));

        mgl::Text description(descriptions_off[i], {0.0f, 0.0f}, font);
        description.set_color(mgl::Color(150, 150, 150));

        const int button_height = window_create_params.size.y * 0.125f;
        const int button_width = button_height * 1.125f;

        mgl::Sprite sprite(textures[i]);
        sprite.set_scale(window_create_params.size.y / 2500.0f);
        auto button = std::make_unique<gsr::Button>(mgl::vec2f(button_width, button_height));

        MainButton main_button = {
            std::move(title),
            std::move(description),
            std::move(sprite),
            std::move(button),
            gsr::GsrMode::Unknown
        };

        main_buttons.push_back(std::move(main_button));
    }

    // Replay
    main_buttons[0].button->on_click = [&]() {
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

    // Record
    main_buttons[1].button->on_click = [&]() {
        #if 0
        int gpu_screen_recorder_process = -1;
        gsr::GsrMode gsr_mode = gsr::GsrMode::Unknown;
        if(gsr::is_gpu_screen_recorder_running(gpu_screen_recorder_process, gsr_mode) && gpu_screen_recorder_process > 0) {
            kill(gpu_screen_recorder_process, SIGINT);
            int status;
            if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            }
            exit(0);
        }

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
        exit(0);
        #endif
    };
    main_buttons[1].mode = gsr::GsrMode::Record;

    main_buttons[2].mode = gsr::GsrMode::Stream;

    auto update_overlay_shape = [&]() {
        const int main_button_margin = 20;// * get_config().scale;
        const int spacing = 0;// * get_config().scale;
        const int combined_spacing = spacing * std::max(0, (int)main_buttons.size() - 1);

        const int per_button_width = main_buttons[0].button->get_size().x;// * get_config().scale;
        const mgl::vec2i overlay_desired_size(per_button_width * (int)main_buttons.size() + combined_spacing, main_buttons[0].button->get_size().y);

        const mgl::vec2i main_buttons_start_pos = mgl::vec2i(window_create_params.size.x*0.5f, window_create_params.size.y*0.25f) - overlay_desired_size/2;
        mgl::vec2i main_button_pos = main_buttons_start_pos;

        int gpu_screen_recorder_process = -1;
        gsr::GsrMode gsr_mode = gsr::GsrMode::Unknown;
        gsr::is_gpu_screen_recorder_running(gpu_screen_recorder_process, gsr_mode);

        for(size_t i = 0; i < main_buttons.size(); ++i) {
            if(main_buttons[i].mode != gsr::GsrMode::Unknown && main_buttons[i].mode == gsr_mode) {
                main_buttons[i].description.set_string(descriptions_on[i]);
                main_buttons[i].description.set_color(mgl::Color(118, 185, 0));
                main_buttons[i].icon.set_color(mgl::Color(118, 185, 0));
            } else {
                main_buttons[i].description.set_string(descriptions_off[i]);
                main_buttons[i].description.set_color(mgl::Color(255, 255, 255));
                main_buttons[i].icon.set_color(mgl::Color(255, 255, 255));
            }

            main_buttons[i].title.set_position(
                mgl::vec2f(
                    main_button_pos.x + per_button_width * 0.5f - main_buttons[i].title.get_bounds().size.x * 0.5f,
                    main_button_pos.y + main_button_margin).floor());

            main_buttons[i].description.set_position(
                mgl::vec2f(
                    main_button_pos.x + per_button_width * 0.5f - main_buttons[i].description.get_bounds().size.x * 0.5f,
                    main_button_pos.y + overlay_desired_size.y - main_buttons[i].description.get_bounds().size.y - main_button_margin).floor());

            main_buttons[i].icon.set_position(
                mgl::vec2f(
                    main_button_pos.x + per_button_width * 0.5f - main_buttons[i].icon.get_texture()->get_size().x * main_buttons[i].icon.get_scale().x * 0.5f,
                    main_button_pos.y + overlay_desired_size.y * 0.5f - main_buttons[i].icon.get_texture()->get_size().y * main_buttons[i].icon.get_scale().y * 0.5f).floor());

            main_buttons[i].button->set_position(main_button_pos.to_vec2f());
            main_button_pos.x += per_button_width + combined_spacing;
        }
    };

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

    //XGrabServer(display);

    mgl::Rectangle top_bar_background(mgl::vec2f(window.get_size().x, window.get_size().y*0.05f).floor());
    top_bar_background.set_color(mgl::Color(0, 0, 0, 250));

    mgl::Text top_bar_text("GPU Screen Recorder", top_bar_font);
    top_bar_text.set_color(mgl::Color(118, 185, 0));
    top_bar_text.set_position((top_bar_background.get_position() + top_bar_background.get_size()*0.5f - top_bar_text.get_bounds().size*0.5f).floor());

    gsr::ComboBox record_area_box(&title_font);
    record_area_box.set_position(mgl::vec2f(300.0f, 300.0f));
    record_area_box.add_item("Window", "window");
    record_area_box.add_item("Focused window", "focused");
    record_area_box.add_item("All monitors (NvFBC)", "all");
    record_area_box.add_item("All monitors, direct mode (NvFBC, VRR workaround)", "all-direct");
    record_area_box.add_item("Monitor DP-0 (3840x2160, NvFBC)", "DP-0");

    mgl::Text record_area_title("Record area", title_font);
    record_area_title.set_position(mgl::vec2f(record_area_box.get_position().x, record_area_box.get_position().y - title_font.get_character_size() - 10.0f));

    gsr::ComboBox audio_input_box(&title_font);
    audio_input_box.set_position(mgl::vec2f(record_area_box.get_position().x, record_area_box.get_position().y + record_area_box.get_size().y + title_font.get_character_size()*2.0f + title_font.get_character_size() + 10.0f));
    audio_input_box.add_item("Monitor of Starship/Matissee HD Audio Controller Analog Stereo", "starship.ablalba.monitor");
    audio_input_box.add_item("Monitor of GP104 High Definition Audio Controller Digital Stereo (HDMI 2)", "starship.ablalba.monitor");

    mgl::Text audio_input_title("Audio input", title_font);
    audio_input_title.set_position(mgl::vec2f(audio_input_box.get_position().x, audio_input_box.get_position().y - title_font.get_character_size() - 10.0f));

    gsr::ComboBox video_quality_box(&title_font);
    video_quality_box.set_position(mgl::vec2f(audio_input_box.get_position().x, audio_input_box.get_position().y + audio_input_box.get_size().y + title_font.get_character_size()*2.0f + title_font.get_character_size() + 10.0f));
    video_quality_box.add_item("High", "starship.ablalba.monitor");
    video_quality_box.add_item("Ultra", "starship.ablalba.monitor");
    video_quality_box.add_item("Placebo", "starship.ablalba.monitor");

    mgl::Text video_quality_title("Video quality", title_font);
    video_quality_title.set_position(mgl::vec2f(video_quality_box.get_position().x, video_quality_box.get_position().y - title_font.get_character_size() - 10.0f));

    gsr::ComboBox framerate_box(&title_font);
    framerate_box.set_position(mgl::vec2f(video_quality_box.get_position().x, video_quality_box.get_position().y + video_quality_box.get_size().y + title_font.get_character_size()*2.0f + title_font.get_character_size() + 10.0f));
    framerate_box.add_item("60", "starship.ablalba.monitor");

    mgl::Text framerate_title("Frame rate", title_font);
    framerate_title.set_position(mgl::vec2f(framerate_box.get_position().x, framerate_box.get_position().y - title_font.get_character_size() - 10.0f));

    gsr::WidgetContainer &widget_container = gsr::WidgetContainer::get_instance();

    mgl::Event event;

    event.type = mgl::Event::MouseMoved;
    event.mouse_move.x = window.get_mouse_position().x;
    event.mouse_move.y = window.get_mouse_position().y;
    widget_container.on_event(event, window);

    auto render = [&] {
        window.clear(mgl::Color(0, 0, 0, 175));
        window.draw(record_area_title);
        window.draw(audio_input_title);
        window.draw(video_quality_title);
        window.draw(framerate_title);
        widget_container.draw(window);
        for(auto &main_button : main_buttons) {
            window.draw(main_button.icon);
            window.draw(main_button.title);
            window.draw(main_button.description);
        }
        window.draw(top_bar_background);
        window.draw(top_bar_text);
        window.display();
    };

    while(window.is_open()) {
        if(window.poll_event(event)) {
            widget_container.on_event(event, window);
            if(event.type == mgl::Event::KeyReleased) {
                if(event.key.code == mgl::Keyboard::Escape) {
                    window.close();
                    break;
                }
            }
        }

        render();
    }

    return 0;
}
