#include "../include/gui/Button.hpp"
#include "../include/window_texture.h"
#include "../include/Process.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/extensions/shape.h>

#include <mglpp/mglpp.hpp>
#include <mglpp/graphics/Font.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/MemoryMappedFile.hpp>

#include <vector>

extern "C" {
#include <mgl/mgl.h>
}

struct Config {
    float scale = 1.0f;
};

static Config& get_config() {
    static Config config;
    return config;
}

static void usage() {
    fprintf(stderr, "usage: window-overlay <window>\n");
    exit(1);
}

static void startup_error(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}

static bool string_to_i64(const char *str, int64_t *result) {
    errno = 0;
    char *endptr = nullptr;
    int64_t res = strtoll(str, &endptr, 0);
    if(endptr == str || errno != 0)
        return false;

    *result = res;
    return true;
}

static void window_texture_get_size_or(WindowTexture *window_texture, int *width, int *height, int fallback_width, int fallback_height) {
    Window root_window;
    int x, y;
    unsigned int w = 0, h = 0;
    unsigned int border_width, depth;
    if(!XGetGeometry(window_texture->display, window_texture->pixmap, &root_window, &x, &y, &w, &h, &border_width, &depth) || w == 0 || h == 0) {
        *width = fallback_width;
        *height = fallback_height;
    } else {
        *width = w;
        *height = h;
    }
}

int main(int argc, char **argv) {
    if(argc != 2)
        usage();

    std::string program_root_dir = dirname(argv[0]);
    if(!program_root_dir.empty() && program_root_dir.back() != '/')
        program_root_dir += '/';
    program_root_dir += "../../../";

    int64_t target_window;
    if(!string_to_i64(argv[1], &target_window)) {
        fprintf(stderr, "Error: invalid number '%s' was specific for window argument\n", argv[1]);
        return 1;
    }

    mgl::Init init;
    Display *display = (Display*)mgl_get_context()->connection;

    XWindowAttributes target_win_attr;
    memset(&target_win_attr, 0, sizeof(target_win_attr));
    if(!XGetWindowAttributes(display, target_window, &target_win_attr)) {
        fprintf(stderr, "Error: window argument %s is not a valid window\n", argv[1]);
        return 1;
    }

    XSelectInput(display, target_window, StructureNotifyMask | VisibilityChangeMask);

    mgl::vec2i target_window_size = { target_win_attr.width, target_win_attr.height };
    get_config().scale = std::min(1.0, (double)target_win_attr.width / 1920.0);

    mgl::Window::CreateParams window_create_params;
    window_create_params.size = target_window_size;
    window_create_params.parent_window = target_window;
    window_create_params.hidden = true;

    mgl::Window window;
    if(!window.create("mglpp", window_create_params))
        startup_error("failed to create window");

    mgl::MemoryMappedFile title_font_file;
    if(!title_font_file.load((program_root_dir + "fonts/Orbitron-Bold.ttf").c_str(), mgl::MemoryMappedFile::LoadOptions{true, false}))
        startup_error("failed to load file: fonts/Orbitron-Bold.ttf");

    mgl::MemoryMappedFile font_file;
    if(!font_file.load((program_root_dir + "fonts/Orbitron-Regular.ttf").c_str(), mgl::MemoryMappedFile::LoadOptions{true, false}))
        startup_error("failed to load file: fonts/Orbitron-Regular.ttf");

    mgl::Font title_font;
    if(!title_font.load_from_file(title_font_file, 32 * get_config().scale))
        startup_error("failed to load font: fonts/Orbitron-Bold.ttf");

    mgl::Font font;
    if(!font.load_from_file(font_file, 20 * get_config().scale))
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
        gsr::Button button;
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
    std::vector<XRectangle> shapes;

    for(int i = 0; i < 3; ++i) {
        mgl::Text title(titles[i], {0.0f, 0.0f}, title_font);
        title.set_color(mgl::Color(255, 255, 255));

        mgl::Text description(descriptions_off[i], {0.0f, 0.0f}, font);
        description.set_color(mgl::Color(255, 255, 255, 150));

        mgl::Sprite sprite(textures[i]);
        sprite.set_scale(get_config().scale);
        gsr::Button button(mgl::vec2f(425 * get_config().scale, 300 * get_config().scale).floor());

        MainButton main_button = {
            std::move(title),
            std::move(description),
            std::move(sprite),
            std::move(button),
            gsr::GsrMode::Unknown
        };

        main_buttons.push_back(std::move(main_button));
        shapes.push_back({});
    }

    // Settings button
    shapes.push_back({});

    // Replay
    main_buttons[0].button.on_click = [&]() {
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
    main_buttons[1].button.on_click = [&]() {
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
    };
    main_buttons[1].mode = gsr::GsrMode::Record;

    main_buttons[2].mode = gsr::GsrMode::Stream;

    const int per_button_width = 425 * get_config().scale;
    const mgl::vec2i overlay_desired_size(per_button_width * (int)main_buttons.size(), 300 * get_config().scale);

    XGCValues xgcv;
    xgcv.foreground = WhitePixel(display, DefaultScreen(display));
    xgcv.line_width = 1;
    xgcv.line_style = LineSolid;

    Pixmap shape_pixmap = None;
    GC shape_gc = None;

    auto update_overlay_shape = [&]() {
        const int main_button_margin = 20 * get_config().scale;
        const mgl::vec2i main_buttons_start_pos = target_window_size/2 - overlay_desired_size/2;
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

            main_buttons[i].button.set_position(main_button_pos.to_vec2f());
            shapes[i] = {
                (short)main_button_pos.x, (short)main_button_pos.y, (unsigned short)per_button_width, (unsigned short)overlay_desired_size.y
            };
            main_button_pos.x += per_button_width;
        }

        const mgl::vec2i settings_button_size(128 * get_config().scale, 128 * get_config().scale);
        shapes[main_buttons.size()] = {
            (short)(main_buttons_start_pos.x + overlay_desired_size.x), (short)(main_buttons_start_pos.y - settings_button_size.y),
            (unsigned short)settings_button_size.x, (unsigned short)settings_button_size.y
        };

        if(shape_pixmap) {
            XFreePixmap(display, shape_pixmap);
            shape_pixmap = None;
        }

        if(shape_gc) {
            XFreeGC(display, shape_gc);
            shape_gc = None;
        }

        shape_pixmap = XCreatePixmap(display, window.get_system_handle(), target_window_size.x, target_window_size.y, 1);
        if(!shape_pixmap)
            fprintf(stderr, "Error: failed to create shape pixmap\n");

        shape_gc = XCreateGC(display, shape_pixmap, 0, &xgcv);

        XSetForeground(display, shape_gc, 0);
        XFillRectangle(display, shape_pixmap, shape_gc, 0, 0, target_window_size.x, target_window_size.y);

        XSetForeground(display, shape_gc, 1);
        XDrawRectangles(display, shape_pixmap, shape_gc, shapes.data(), shapes.size());
        XFillRectangles(display, shape_pixmap, shape_gc, shapes.data(), shapes.size());

        XShapeCombineMask(display, window.get_system_handle(), ShapeBounding, 0, 0, shape_pixmap, ShapeSet);
    };

    update_overlay_shape();
    window.set_visible(true);

    Cursor default_cursor = XCreateFontCursor(display, XC_arrow);

    // TODO: Retry if these fail
    XGrabPointer(display, window.get_system_handle(), True, ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, default_cursor, CurrentTime);
    XGrabKeyboard(display, window.get_system_handle(), True, GrabModeAsync, GrabModeAsync, CurrentTime);

    XEvent xev;
    mgl::Event event;

    while(window.is_open()) {
        if(XCheckTypedWindowEvent(display, target_window, DestroyNotify, &xev)) {
            window.close();
            break;
        }

        if(XCheckTypedWindowEvent(display, target_window, VisibilityNotify, &xev)) {
            if(xev.xvisibility.state) {
                
            }
        }

        if(XCheckTypedWindowEvent(display, target_window, ConfigureNotify, &xev) && (xev.xconfigure.width != target_window_size.x || xev.xconfigure.height != target_window_size.y)) {
            while(XCheckTypedWindowEvent(display, target_window, ConfigureNotify, &xev)) {}
            target_window_size.x = xev.xconfigure.width;
            target_window_size.y = xev.xconfigure.height;
            window.set_size(target_window_size);
            update_overlay_shape();
        }

        if(window.poll_event(event)) {
            for(auto &main_button : main_buttons) {
                main_button.button.on_event(event, window);
            }

            if(event.type == mgl::Event::KeyReleased) {
                if(event.key.code == mgl::Keyboard::Escape) {
                    window.close();
                    break;
                }
            }
        }

        window.clear(mgl::Color(37, 43, 47));
        for(auto &main_button : main_buttons) {
            main_button.button.draw(window);
            window.draw(main_button.icon);
            window.draw(main_button.title);
            window.draw(main_button.description);
        }
        window.display();
    }

    return 0;
}
