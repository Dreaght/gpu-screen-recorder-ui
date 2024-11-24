#include "../include/GsrInfo.hpp"
#include "../include/Theme.hpp"
#include "../include/window_texture.h"
#include "../include/Overlay.hpp"
#include "../include/GlobalHotkeysX11.hpp"
#include "../include/GlobalHotkeysLinux.hpp"
#include "../include/gui/Utils.hpp"

#include <unistd.h>
#include <signal.h>
#include <thread>

#include <X11/keysym.h>
#include <mglpp/mglpp.hpp>
#include <mglpp/system/Clock.hpp>

// TODO: Make keyboard controllable for steam deck (and other controllers).
// TODO: Keep track of gpu screen recorder run by other programs to not allow recording at the same time, or something.
// TODO: Add systray by using org.kde.StatusNotifierWatcher/etc dbus directly.
// TODO: Make sure the overlay always stays on top. Test with starting the overlay and then opening youtube in fullscreen.
//       This is done in Overlay::force_window_on_top, but it's not called right now. It cant be used because the overlay will be on top of
//       notifications.

extern "C" {
#include <mgl/mgl.h>
}

static sig_atomic_t running = 1;
static void sigint_handler(int signal) {
    (void)signal;
    running = 0;
}

static void disable_prime_run() {
    unsetenv("__NV_PRIME_RENDER_OFFLOAD");
    unsetenv("__NV_PRIME_RENDER_OFFLOAD_PROVIDER");
    unsetenv("__GLX_VENDOR_LIBRARY_NAME");
    unsetenv("__VK_LAYER_NV_optimus");
}

int main(void) {
    setlocale(LC_ALL, "C"); // Sigh... stupid C

    // Cant get window texture when prime-run is used
    disable_prime_run();

    // Stop nvidia driver from buffering frames
    setenv("__GL_MaxFramesAllowed", "1", true);
    // If this is set to 1 then cuGraphicsGLRegisterImage will fail for egl context with error: invalid OpenGL or DirectX context,
    // so we overwrite it
    setenv("__GL_THREADED_OPTIMIZATIONS", "0", true);
    // Some people set this to force all applications to vsync on nvidia, but this makes eglSwapBuffers never return.
    unsetenv("__GL_SYNC_TO_VBLANK");
    // Same as above, but for amd/intel
    unsetenv("vblank_mode");

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
    if(access("sibs-build", F_OK) == 0) {
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

    egl_functions egl_funcs;
    egl_funcs.eglGetError = (decltype(egl_funcs.eglGetError))context->gl.eglGetProcAddress("eglGetError");
    egl_funcs.eglCreateImage = (decltype(egl_funcs.eglCreateImage))context->gl.eglGetProcAddress("eglCreateImage");
    egl_funcs.eglDestroyImage = (decltype(egl_funcs.eglDestroyImage))context->gl.eglGetProcAddress("eglDestroyImage");
    egl_funcs.glEGLImageTargetTexture2DOES = (decltype(egl_funcs.glEGLImageTargetTexture2DOES))context->gl.eglGetProcAddress("glEGLImageTargetTexture2DOES");

    if(!egl_funcs.eglGetError || !egl_funcs.eglCreateImage || !egl_funcs.eglDestroyImage || !egl_funcs.glEGLImageTargetTexture2DOES) {
        fprintf(stderr, "Error: required opengl functions not available on your system\n");
        exit(1);
    }

    fprintf(stderr, "info: gsr ui is now ready, waiting for inputs. Press alt+z to show/hide the overlay\n");

    auto overlay = std::make_unique<gsr::Overlay>(resources_path, gsr_info, egl_funcs);
    //overlay.show();

    // gsr::GlobalHotkeysX11 global_hotkeys;
    // const bool show_hotkey_registered = global_hotkeys.bind_key_press({ XK_z, Mod1Mask }, "show_hide", [&](const std::string &id) {
    //     fprintf(stderr, "pressed %s\n", id.c_str());
    //     overlay->toggle_show();
    // });

    // const bool record_hotkey_registered = global_hotkeys.bind_key_press({ XK_F9, Mod1Mask }, "record", [&](const std::string &id) {
    //     fprintf(stderr, "pressed %s\n", id.c_str());
    //     overlay->toggle_record();
    // });

    // const bool pause_hotkey_registered = global_hotkeys.bind_key_press({ XK_F7, Mod1Mask }, "pause", [&](const std::string &id) {
    //     fprintf(stderr, "pressed %s\n", id.c_str());
    //     overlay->toggle_pause();
    // });

    // const bool stream_hotkey_registered = global_hotkeys.bind_key_press({ XK_F8, Mod1Mask }, "stream", [&](const std::string &id) {
    //     fprintf(stderr, "pressed %s\n", id.c_str());
    //     overlay->toggle_stream();
    // });

    // const bool replay_hotkey_registered = global_hotkeys.bind_key_press({ XK_F10, ShiftMask | Mod1Mask }, "replay start", [&](const std::string &id) {
    //     fprintf(stderr, "pressed %s\n", id.c_str());
    //     overlay->toggle_replay();
    // });

    // const bool replay_save_hotkey_registered = global_hotkeys.bind_key_press({ XK_F10, Mod1Mask }, "replay save", [&](const std::string &id) {
    //     fprintf(stderr, "pressed %s\n", id.c_str());
    //     overlay->save_replay();
    // });

    gsr::GlobalHotkeysLinux global_hotkeys;
    if(!global_hotkeys.start())
        fprintf(stderr, "error: failed to start global hotkeys\n");

    const bool show_hotkey_registered = global_hotkeys.bind_action("show_hide", [&](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_show();
    });

    const bool record_hotkey_registered = global_hotkeys.bind_action("record", [&](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_record();
    });

    const bool pause_hotkey_registered = global_hotkeys.bind_action("pause", [&](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_pause();
    });

    const bool stream_hotkey_registered = global_hotkeys.bind_action("stream", [&](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_stream();
    });

    const bool replay_hotkey_registered = global_hotkeys.bind_action("replay_start", [&](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_replay();
    });

    const bool replay_save_hotkey_registered = global_hotkeys.bind_action("replay_save", [&](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->save_replay();
    });

    if(!show_hotkey_registered)
        fprintf(stderr, "error: failed to register hotkey alt+z for showing the overlay because the hotkey is registered by another program\n");

    if(!record_hotkey_registered)
        fprintf(stderr, "error: failed to register hotkey alt+f9 for recording because the hotkey is registered by another program\n");

    if(!pause_hotkey_registered)
        fprintf(stderr, "error: failed to register hotkey alt+f7 for pausing because the hotkey is registered by another program\n");

    if(!stream_hotkey_registered)
        fprintf(stderr, "error: failed to register hotkey alt+f8 for streaming because the hotkey is registered by another program\n");

    if(!replay_hotkey_registered)
        fprintf(stderr, "error: failed to register hotkey alt+shift+f10 for starting replay because the hotkey is registered by another program\n");

    if(!replay_save_hotkey_registered)
        fprintf(stderr, "error: failed to register hotkey alt+f10 for saving replay because the hotkey is registered by another program\n");

    mgl::Clock frame_delta_clock;
    while(running) {
        const double frame_delta_seconds = frame_delta_clock.get_elapsed_time_seconds();
        frame_delta_clock.restart();
        gsr::set_frame_delta_seconds(frame_delta_seconds);

        global_hotkeys.poll_events();
        overlay->handle_events();
        if(!overlay->draw())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fprintf(stderr, "info: shutting down!\n");
    overlay.reset();
    gsr::deinit_theme();
    gsr::deinit_color_theme();

    return 0;
}
