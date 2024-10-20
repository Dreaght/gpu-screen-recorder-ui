#include "../include/GsrInfo.hpp"
#include "../include/Theme.hpp"
#include "../include/window_texture.h"
#include "../include/Overlay.hpp"
#include "../include/GlobalHotkeysX11.hpp"
#include "../include/gui/Utils.hpp"

#include <unistd.h>
#include <signal.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#define XK_LATIN1
#include <X11/keysymdef.h>
#include <mglpp/mglpp.hpp>
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

static sig_atomic_t running = 1;
static void sigint_handler(int signal) {
    (void)signal;
    running = 0;
}

int main(int argc, char **argv) {
    (void)argv;
    if(argc != 1)
        usage();

    signal(SIGINT, sigint_handler);

    gsr::GsrInfo gsr_info;
    // TODO: Show the error in ui
    gsr::GsrInfoExitStatus gsr_info_exit_status = gsr::get_gpu_screen_recorder_info(&gsr_info);
    if(gsr_info_exit_status != gsr::GsrInfoExitStatus::OK) {
        fprintf(stderr, "error: failed to get gpu-screen-recorder info, error: %d\n", (int)gsr_info_exit_status);
        exit(1);
    }

    if(gsr_info.system_info.display_server == gsr::DisplayServer::WAYLAND) {
        fprintf(stderr, "error: Wayland is currently not supported\n");
        exit(1);
    }

    std::string resources_path;
    if(access("images/gpu_screen_recorder_logo.png", F_OK) == 0) {
        resources_path = "./";
    } else {
#ifdef GSR_UI_RESOURCES_PATH
        resources_path = GSR_UI_RESOURCES_PATH "/";
#else
        resources_path = "/usr/share/gsr-ui/";
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
    if(!window.create("gsr ui", window_create_params))
        startup_error("failed to create window");

    unsigned char data = 2; // Prefer being composed to allow transparency
    XChangeProperty(display, window.get_system_handle(), XInternAtom(display, "_NET_WM_BYPASS_COMPOSITOR", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

    data = 1;
    XChangeProperty(display, window.get_system_handle(), XInternAtom(display, "GAMESCOPE_EXTERNAL_OVERLAY", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

    if(!gsr::init_theme(gsr_info, resources_path)) {
        fprintf(stderr, "Error: failed to load theme\n");
        exit(1);
    }

    gsr::Overlay overlay(window, resources_path, gsr_info, egl_funcs, bg_color);
    overlay.show();

    // gsr::GlobalHotkeysX11 global_hotkeys;
    // global_hotkeys.bind_key_press({ XK_z, Mod1Mask }, "open/hide", [&](const std::string &id) {
    //     fprintf(stderr, "pressed %s\n", id.c_str());
    //     overlay.toggle_show();
    // });

    //fprintf(stderr, "info: gsr ui is now ready, waiting for inputs. Press alt+z to show/hide the overlay\n");

    mgl::Event event;
    mgl::Clock frame_delta_clock;
    while(window.is_open() && overlay.is_open() && running) {
        const double frame_delta_seconds = frame_delta_clock.get_elapsed_time_seconds();
        frame_delta_clock.restart();
        gsr::set_frame_delta_seconds(frame_delta_seconds);

        //global_hotkeys.poll_events();        
        while(window.poll_event(event)) {
            overlay.on_event(event, window);
        }

        window.clear(bg_color);
        overlay.draw(window);
        window.display();
    }

    fprintf(stderr, "shutting down!\n");
    gsr::deinit_theme();
    window.close();

    return 0;
}
