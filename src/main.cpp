#include "../include/GsrInfo.hpp"
#include "../include/Theme.hpp"
#include "../include/Overlay.hpp"
#include "../include/GlobalHotkeysX11.hpp"
#include "../include/GlobalHotkeysLinux.hpp"
#include "../include/gui/Utils.hpp"
#include "../include/Process.hpp"
#include "../include/Rpc.hpp"

#include <unistd.h>
#include <signal.h>
#include <thread>
#include <string.h>

#include <X11/keysym.h>
#include <mglpp/mglpp.hpp>
#include <mglpp/system/Clock.hpp>

// TODO: Make keyboard/controller controllable for steam deck (and other controllers).
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

static std::unique_ptr<gsr::GlobalHotkeysX11> register_x11_hotkeys(gsr::Overlay *overlay) {
    auto global_hotkeys = std::make_unique<gsr::GlobalHotkeysX11>();
    const bool show_hotkey_registered = global_hotkeys->bind_key_press({ XK_z, Mod1Mask }, "show_hide", [overlay](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_show();
    });

    const bool record_hotkey_registered = global_hotkeys->bind_key_press({ XK_F9, Mod1Mask }, "record", [overlay](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_record();
    });

    const bool pause_hotkey_registered = global_hotkeys->bind_key_press({ XK_F7, Mod1Mask }, "pause", [overlay](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_pause();
    });

    const bool stream_hotkey_registered = global_hotkeys->bind_key_press({ XK_F8, Mod1Mask }, "stream", [overlay](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_stream();
    });

    const bool replay_hotkey_registered = global_hotkeys->bind_key_press({ XK_F10, ShiftMask | Mod1Mask }, "replay_start", [overlay](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_replay();
    });

    const bool replay_save_hotkey_registered = global_hotkeys->bind_key_press({ XK_F10, Mod1Mask }, "replay_save", [overlay](const std::string &id) {
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

    if(!show_hotkey_registered || !record_hotkey_registered || !pause_hotkey_registered || !stream_hotkey_registered || !replay_hotkey_registered || !replay_save_hotkey_registered)
        return nullptr;

    return global_hotkeys;
}

static std::unique_ptr<gsr::GlobalHotkeysLinux> register_linux_hotkeys(gsr::Overlay *overlay) {
    auto global_hotkeys = std::make_unique<gsr::GlobalHotkeysLinux>();
    if(!global_hotkeys->start())
        fprintf(stderr, "error: failed to start global hotkeys\n");

    global_hotkeys->bind_action("show_hide", [overlay](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_show();
    });

    global_hotkeys->bind_action("record", [overlay](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_record();
    });

    global_hotkeys->bind_action("pause", [overlay](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_pause();
    });

    global_hotkeys->bind_action("stream", [overlay](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_stream();
    });

    global_hotkeys->bind_action("replay_start", [overlay](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->toggle_replay();
    });

    global_hotkeys->bind_action("replay_save", [overlay](const std::string &id) {
        fprintf(stderr, "pressed %s\n", id.c_str());
        overlay->save_replay();
    });

    return global_hotkeys;
}

static bool is_gsr_ui_virtual_keyboard_running() {
    FILE *f = fopen("/proc/bus/input/devices", "rb");
    if(!f)
        return false;

    bool virtual_keyboard_running = false;
    char line[1024];
    while(fgets(line, sizeof(line), f)) {
        if(strstr(line, "gsr-ui virtual keyboard")) {
            virtual_keyboard_running = true;
            break;
        }
    }

    fclose(f);
    return virtual_keyboard_running;
}

static void usage() {
    printf("usage: gsr-ui [action]\n");
    printf("OPTIONS:\n");
    printf("  action  The launch action. Should be either \"launch-show\" or \"launch-hide\". Optional, defaults to \"launch-hide\".\n");
    printf("          If \"launch-show\" is used then the program starts and the UI is immediately opened and can be shown/hidden with Alt+Z.\n");
    printf("          If \"launch-hide\" is used then the program starts but the UI is not opened until Alt+Z is pressed.\n");
    exit(1);
}

enum class LaunchAction {
    LAUNCH_SHOW,
    LAUNCH_HIDE
};

int main(int argc, char **argv) {
    setlocale(LC_ALL, "C"); // Sigh... stupid C

    if(geteuid() == 0) {
        fprintf(stderr, "Error: don't run gsr-ui as the root user\n");
        return 1;
    }

    LaunchAction launch_action = LaunchAction::LAUNCH_HIDE;
    if(argc == 1) {
        launch_action = LaunchAction::LAUNCH_HIDE;
    } else if(argc == 2) {
        const char *launch_action_opt = argv[1];
        if(strcmp(launch_action_opt, "launch-show") == 0) {
            launch_action = LaunchAction::LAUNCH_SHOW;
        } else if(strcmp(launch_action_opt, "launch-hide") == 0) {
            launch_action = LaunchAction::LAUNCH_HIDE;
        } else {
            printf("error: invalid action \"%s\", expected \"launch-show\" or \"launch-hide\".\n", launch_action_opt);
            usage();
        }
    } else {
        usage();
    }

    // TODO: This is a shitty method to detect if multiple instances of gsr-ui is running but this will work properly even in flatpak
    // that uses pid sandboxing. Replace this with a better method once we no longer rely on linux global hotkeys on some platform.
    if(is_gsr_ui_virtual_keyboard_running()) {
        gsr::Rpc rpc;
        if(rpc.open("gsr-ui") && rpc.write("show_ui\n", 8)) {
            fprintf(stderr, "Error: another instance of gsr-ui is already running, opening that one instead\n");
        } else {
            fprintf(stderr, "Error: failed to send command to running gsr-ui instance, user will have to open the UI manually with Alt+Z\n");
            const char *args[] = { "gsr-notify", "--text", "Another instance of GPU Screen Recorder UI is already running.\nPress Alt+Z to open the UI.", "--timeout", "5.0", "--icon-color", "ff0000", "--bg-color", "ff0000", nullptr };
            gsr::exec_program_daemonized(args);
        }
        return 1;
    }
    // const pid_t gsr_ui_pid = gsr::pidof("gsr-ui");
    // if(gsr_ui_pid != -1) {
    //     const char *args[] = { "gsr-notify", "--text", "Another instance of GPU Screen Recorder UI is already running", "--timeout", "5.0", "--icon-color", "ff0000", "--bg-color", "ff0000", nullptr };
    //     gsr::exec_program_daemonized(args);
    //     return 1;
    // }

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

    if(mgl_init() != 0) {
        fprintf(stderr, "Error: failed to initialize mgl. Failed to either connect to the X11 server or setup opengl\n");
        exit(1);
    }

    gsr::GsrInfo gsr_info;
    // TODO: Show the error in ui
    gsr::GsrInfoExitStatus gsr_info_exit_status = gsr::get_gpu_screen_recorder_info(&gsr_info);
    if(gsr_info_exit_status != gsr::GsrInfoExitStatus::OK) {
        fprintf(stderr, "Error: failed to get gpu-screen-recorder info, error: %d\n", (int)gsr_info_exit_status);
        exit(1);
    }

    const gsr::DisplayServer display_server = gsr_info.system_info.display_server;
    if(display_server == gsr::DisplayServer::WAYLAND)
        fprintf(stderr, "Warning: Wayland support is experimental and requires XWayland. Things may not work as expected.\n");

    gsr::SupportedCaptureOptions capture_options = gsr::get_supported_capture_options(gsr_info);

    std::string resources_path;
    if(access("sibs-build/linux_x86_64/debug/gsr-ui", F_OK) == 0) {
        resources_path = "./";
    } else {
#ifdef GSR_UI_RESOURCES_PATH
        resources_path = GSR_UI_RESOURCES_PATH "/";
#else
        resources_path = "/usr/share/gsr-ui/";
#endif
    }

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

    fprintf(stderr, "Info: gsr ui is now ready, waiting for inputs. Press alt+z to show/hide the overlay\n");

    auto rpc = std::make_unique<gsr::Rpc>();
    if(!rpc->create("gsr-ui"))
        fprintf(stderr, "Error: Failed to create rpc, commands won't be received\n");

    auto overlay = std::make_unique<gsr::Overlay>(resources_path, std::move(gsr_info), std::move(capture_options), egl_funcs);

    rpc->add_handler("show_ui", [&](const std::string&) {
        overlay->show();
    });

    std::unique_ptr<gsr::GlobalHotkeys> global_hotkeys = register_linux_hotkeys(overlay.get());

    if(launch_action == LaunchAction::LAUNCH_SHOW)
        overlay->show();

    // TODO: Add hotkeys in Overlay when using x11 global hotkeys. The hotkeys in Overlay should duplicate each key that is used for x11 global hotkeys.

    std::string exit_reason;
    mgl::Clock frame_delta_clock;

    while(running && mgl_is_connected_to_display_server() && !overlay->should_exit(exit_reason)) {
        const double frame_delta_seconds = frame_delta_clock.restart();
        gsr::set_frame_delta_seconds(frame_delta_seconds);

        rpc->poll();
        global_hotkeys->poll_events();
        overlay->handle_events(global_hotkeys.get());
        if(!overlay->draw()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            mgl_ping_display_server();
        }
    }

    fprintf(stderr, "Info: shutting down!\n");
    rpc.reset();
    global_hotkeys.reset();
    overlay.reset();
    gsr::deinit_theme();
    gsr::deinit_color_theme();
    mgl_deinit();

    if(exit_reason == "back-to-old-ui") {
        const char *args[] = { "gpu-screen-recorder-gtk", "use-old-ui", nullptr };
        execvp(args[0], (char* const*)args);
    }

    return 0;
}
