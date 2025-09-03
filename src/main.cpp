#include "../include/GsrInfo.hpp"
#include "../include/Overlay.hpp"
#include "../include/gui/Utils.hpp"
#include "../include/Process.hpp"
#include "../include/Rpc.hpp"

#include <signal.h>
#include <string.h>
#include <limits.h>
#include <malloc.h>

#include <mglpp/mglpp.hpp>
#include <mglpp/system/Clock.hpp>

// TODO: Make keyboard/controller controllable for steam deck (and other controllers).
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

static void signal_ignore(int) {

}

static void disable_prime_run() {
    unsetenv("__NV_PRIME_RENDER_OFFLOAD");
    unsetenv("__NV_PRIME_RENDER_OFFLOAD_PROVIDER");
    unsetenv("__GLX_VENDOR_LIBRARY_NAME");
    unsetenv("__VK_LAYER_NV_optimus");
    unsetenv("DRI_PRIME");
}

static void rpc_add_commands(gsr::Rpc *rpc, gsr::Overlay *overlay) {
    rpc->add_handler("show_ui", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->show();
    });

    rpc->add_handler("toggle-show", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_show();
    });

    rpc->add_handler("toggle-record", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_record();
    });

    rpc->add_handler("toggle-pause", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_pause();
    });

    rpc->add_handler("toggle-stream", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_stream();
    });

    rpc->add_handler("toggle-replay", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_replay();
    });

    rpc->add_handler("replay-save", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->save_replay();
    });

    rpc->add_handler("replay-save-1-min", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->save_replay_1_min();
    });

    rpc->add_handler("replay-save-10-min", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->save_replay_10_min();
    });

    rpc->add_handler("take-screenshot", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->take_screenshot();
    });

    rpc->add_handler("take-screenshot-region", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->take_screenshot_region();
    });

    rpc->add_handler("take-screenshot-window", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->take_screenshot_window();
    });
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

static void install_flatpak_systemd_service() {
    const bool systemd_service_exists = system(
        "data_home=$(flatpak-spawn --host -- /bin/sh -c 'echo \"${XDG_DATA_HOME:-$HOME/.local/share}\"') && "
        "flatpak-spawn --host -- ls \"$data_home/systemd/user/gpu-screen-recorder-ui.service\"") == 0;
    if(systemd_service_exists)
        return;

    bool service_install_successful = (system(
        "data_home=$(flatpak-spawn --host -- /bin/sh -c 'echo \"${XDG_DATA_HOME:-$HOME/.local/share}\"') && "
        "flatpak-spawn --host -- install -Dm644 /var/lib/flatpak/app/com.dec05eba.gpu_screen_recorder/current/active/files/share/gpu-screen-recorder/gpu-screen-recorder-ui.service \"$data_home/systemd/user/gpu-screen-recorder-ui.service\"") == 0);
    service_install_successful &= (system("flatpak-spawn --host -- systemctl --user daemon-reload") == 0);
    if(service_install_successful)
        fprintf(stderr, "Info: the systemd service file was missing. It has now been installed\n");
    else
        fprintf(stderr, "Error: the systemd service file is missing and failed to install it again\n");
}

static void remove_flatpak_systemd_service() {
    char systemd_service_path[PATH_MAX];
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if(xdg_data_home) {
        snprintf(systemd_service_path, sizeof(systemd_service_path), "%s/systemd/user/gpu-screen-recorder-ui.service", xdg_data_home);
    } else if(home) {
        snprintf(systemd_service_path, sizeof(systemd_service_path), "%s/.local/share/systemd/user/gpu-screen-recorder-ui.service", home);
    } else {
        fprintf(stderr, "Error: failed to get user home directory\n");
        return;
    }

    if(access(systemd_service_path, F_OK) != 0)
        return;

    remove(systemd_service_path);
    system("systemctl --user daemon-reload");
    fprintf(stderr, "Info: conflicting flatpak version of the systemd service for gsr-ui was found at \"%s\", it has now been removed\n", systemd_service_path);
}

static bool is_flatpak() {
    return getenv("FLATPAK_ID") != nullptr;
}

static void set_display_server_environment_variables() {
    // Some users dont have properly setup environments (no display manager that does systemctl --user import-environment DISPLAY WAYLAND_DISPLAY)
    const char *display = getenv("DISPLAY");
    if(!display) {
        display = ":0";
        setenv("DISPLAY", display, true);
    }

    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if(!wayland_display) {
        wayland_display = "wayland-1";
        setenv("WAYLAND_DISPLAY", wayland_display, true);
    }
}

static void usage() {
    printf("usage: gsr-ui [action]\n");
    printf("OPTIONS:\n");
    printf("  action  The launch action. Should be either \"launch-show\", \"launch-hide\" or \"launch-daemon\". Optional, defaults to \"launch-hide\".\n");
    printf("          If \"launch-show\" is used then the program starts and the UI is immediately opened and can be shown/hidden with Alt+Z.\n");
    printf("          If \"launch-hide\" is used then the program starts but the UI is not opened until Alt+Z is pressed. The UI will be opened if the program is already running in another process.\n");
    printf("          If \"launch-daemon\" is used then the program starts but the UI is not opened until Alt+Z is pressed. The UI will not be opened if the program is already running in another process.\n");
    exit(1);
}

enum class LaunchAction {
    LAUNCH_SHOW,
    LAUNCH_HIDE,
    LAUNCH_DAEMON
};

int main(int argc, char **argv) {
    setlocale(LC_ALL, "C"); // Sigh... stupid C
#ifdef __GLIBC__
    mallopt(M_MMAP_THRESHOLD, 65536);
#endif

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
        } else if(strcmp(launch_action_opt, "launch-daemon") == 0) {
            launch_action = LaunchAction::LAUNCH_DAEMON;
        } else {
            printf("error: invalid action \"%s\", expected \"launch-show\", \"launch-hide\" or \"launch-daemon\".\n", launch_action_opt);
            usage();
        }
    } else {
        usage();
    }

    set_display_server_environment_variables();

    auto rpc = std::make_unique<gsr::Rpc>();
    const bool rpc_created = rpc->create("gsr-ui");
    if(!rpc_created)
        fprintf(stderr, "Error: Failed to create rpc\n");

    if(is_gsr_ui_virtual_keyboard_running() || !rpc_created) {
        if(launch_action == LaunchAction::LAUNCH_DAEMON)
            return 1;

        rpc = std::make_unique<gsr::Rpc>();
        if(rpc->open("gsr-ui") && rpc->write("show_ui\n", 8)) {
            fprintf(stderr, "Error: another instance of gsr-ui is already running, opening that one instead\n");
        } else {
            fprintf(stderr, "Error: failed to send command to running gsr-ui instance, user will have to open the UI manually with Alt+Z\n");
            const char *args[] = { "gsr-notify", "--text", "Another instance of GPU Screen Recorder UI is already running.\nPress Alt+Z to open the UI.", "--timeout", "5.0", "--icon-color", "ff0000", "--bg-color", "ff0000", nullptr };
            gsr::exec_program_daemonized(args);
        }
        return 1;
    }

    if(gsr::pidof("gpu-screen-recorder", -1) != -1) {
        const char *args[] = { "gsr-notify", "--text", "GPU Screen Recorder is already running in another process.\nPlease close it before using GPU Screen Recorder UI.", "--timeout", "5.0", "--icon-color", "ff0000", "--bg-color", "ff0000", nullptr };
        gsr::exec_program_daemonized(args);
    }

    if(mgl_init(MGL_WINDOW_SYSTEM_X11) != 0) {
        fprintf(stderr, "Error: failed to initialize mgl. Failed to either connect to the X11 server or setup opengl\n");
        return 1;
    }

    if(is_flatpak())
        install_flatpak_systemd_service();
    else
        remove_flatpak_systemd_service();

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
    signal(SIGTERM, sigint_handler);
    signal(SIGUSR1, signal_ignore);
    signal(SIGUSR2, signal_ignore);
    signal(SIGRTMIN, signal_ignore);
    signal(SIGRTMIN+1, signal_ignore);
    signal(SIGRTMIN+2, signal_ignore);
    signal(SIGRTMIN+3, signal_ignore);
    signal(SIGRTMIN+4, signal_ignore);
    signal(SIGRTMIN+5, signal_ignore);
    signal(SIGRTMIN+6, signal_ignore);

    gsr::GsrInfo gsr_info;
    // TODO: Show the error in ui
    gsr::GsrInfoExitStatus gsr_info_exit_status = gsr::get_gpu_screen_recorder_info(&gsr_info);
    if(gsr_info_exit_status != gsr::GsrInfoExitStatus::OK) {
        fprintf(stderr, "Error: failed to get gpu-screen-recorder info, error: %d\n", (int)gsr_info_exit_status);
        exit(1);
    }

    const gsr::DisplayServer display_server = gsr_info.system_info.display_server;
    if(display_server == gsr::DisplayServer::WAYLAND) {
        fprintf(stderr, "Warning: Wayland doesn't support this program properly and XWayland is required. Things may not work as expected. Use X11 if you experience issues.\n");
    } else {
        // Cant get window texture when prime-run is used
        disable_prime_run();
    }

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

    auto overlay = std::make_unique<gsr::Overlay>(resources_path, std::move(gsr_info), std::move(capture_options), egl_funcs);
    if(launch_action == LaunchAction::LAUNCH_SHOW)
        overlay->show();

    rpc_add_commands(rpc.get(), overlay.get());

    // TODO: Add hotkeys in Overlay when using x11 global hotkeys. The hotkeys in Overlay should duplicate each key that is used for x11 global hotkeys.

    std::string exit_reason;
    mgl::Clock frame_delta_clock;

    while(running && mgl_is_connected_to_display_server() && !overlay->should_exit(exit_reason)) {
        const double frame_delta_seconds = frame_delta_clock.restart();
        gsr::set_frame_delta_seconds(frame_delta_seconds);

        rpc->poll();
        overlay->handle_events();
        if(!overlay->draw()) {
            usleep(100 * 1000); // 100ms
            mgl_ping_display_server();
        }
    }

    fprintf(stderr, "Info: shutting down!\n");
    rpc.reset();
    overlay.reset();
    mgl_deinit();

    if(exit_reason == "back-to-old-ui") {
        const char *args[] = { "gpu-screen-recorder-gtk", "use-old-ui", nullptr };
        execvp(args[0], (char* const*)args);
        return 0;
    } else if(exit_reason == "exit") {
        return 0;
    }

    return mgl_is_connected_to_display_server() ? 0 : 1;
}
