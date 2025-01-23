#include "../include/GlobalHotkeysLinux.hpp"
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>

extern "C" {
#include <mgl/mgl.h>
}
#include <X11/Xlib.h>
#include <linux/input-event-codes.h>

#define PIPE_READ 0
#define PIPE_WRITE 1

namespace gsr {
    static const char* grab_type_to_arg(GlobalHotkeysLinux::GrabType grab_type) {
        switch(grab_type) {
            case GlobalHotkeysLinux::GrabType::ALL:     return "--all";
            case GlobalHotkeysLinux::GrabType::VIRTUAL: return "--virtual";
        }
        return "--all";
    }

    static inline uint8_t x11_keycode_to_linux_keycode(uint8_t code) {
        return code - 8;
    }

    static std::vector<uint8_t> modifiers_to_linux_keys(uint32_t modifiers) {
        std::vector<uint8_t> result;
        if(modifiers & HOTKEY_MOD_LSHIFT)
            result.push_back(KEY_LEFTSHIFT);
        if(modifiers & HOTKEY_MOD_RSHIFT)
            result.push_back(KEY_RIGHTSHIFT);
        if(modifiers & HOTKEY_MOD_LCTRL)
            result.push_back(KEY_LEFTCTRL);
        if(modifiers & HOTKEY_MOD_RCTRL)
            result.push_back(KEY_RIGHTCTRL);
        if(modifiers & HOTKEY_MOD_LALT)
            result.push_back(KEY_LEFTALT);
        if(modifiers & HOTKEY_MOD_RALT)
            result.push_back(KEY_RIGHTALT);
        if(modifiers & HOTKEY_MOD_LSUPER)
            result.push_back(KEY_LEFTMETA);
        if(modifiers & HOTKEY_MOD_RSUPER)
            result.push_back(KEY_RIGHTMETA);
        return result;
    }

    static std::string linux_keys_to_command_string(const uint8_t *keys, size_t size) {
        std::string result;
        for(size_t i = 0; i < size; ++i) {
            if(!result.empty())
                result += "+";
            result += std::to_string(keys[i]);
        }
        return result;
    }

    GlobalHotkeysLinux::GlobalHotkeysLinux(GrabType grab_type) : grab_type(grab_type) {
        for(int i = 0; i < 2; ++i) {
            read_pipes[i] = -1;
            write_pipes[i] = -1;
        }
    }

    GlobalHotkeysLinux::~GlobalHotkeysLinux() {
        for(int i = 0; i < 2; ++i) {
            if(read_pipes[i] > 0)
                close(read_pipes[i]);

            if(write_pipes[i] > 0)
                close(write_pipes[i]);
        }

        if(read_file)
            fclose(read_file);

        if(process_id > 0) {
            kill(process_id, SIGKILL);
            int status;
            waitpid(process_id, &status, 0);
        }
    }

    bool GlobalHotkeysLinux::start() {
        const char *grab_type_arg = grab_type_to_arg(grab_type);
        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
        const char *user_homepath = getenv("HOME");
        if(!user_homepath)
            user_homepath = "/tmp";

        char gsr_global_hotkeys_flatpak[PATH_MAX];
        snprintf(gsr_global_hotkeys_flatpak, sizeof(gsr_global_hotkeys_flatpak), "%s/.local/share/gpu-screen-recorder/gsr-global-hotkeys", user_homepath);

        const char *display = getenv("DISPLAY");
        if(!display)
            display = ":0";
        char env_arg[256];
        snprintf(env_arg, sizeof(env_arg), "--env=DISPLAY=%s", display);

        if(process_id > 0)
            return false;

        if(pipe(read_pipes) == -1)
            return false;

        if(pipe(write_pipes) == -1) {
            for(int i = 0; i < 2; ++i) {
                close(read_pipes[i]);
                read_pipes[i] = -1;
            }
            return false;
        }

        const pid_t pid = vfork();
        if(pid == -1) {
            perror("Failed to vfork");
            for(int i = 0; i < 2; ++i) {
                close(read_pipes[i]);
                close(write_pipes[i]);
                read_pipes[i] = -1;
                write_pipes[i] = -1;
            }
            return false;
        } else if(pid == 0) { /* child */
            dup2(read_pipes[PIPE_WRITE], STDOUT_FILENO);
            for(int i = 0; i < 2; ++i) {
                close(read_pipes[i]);
            }

            dup2(write_pipes[PIPE_READ], STDIN_FILENO);
            for(int i = 0; i < 2; ++i) {
                close(write_pipes[i]);
            }

            if(inside_flatpak) {
                const char *args[] = { "flatpak-spawn", "--host", env_arg, "--", gsr_global_hotkeys_flatpak, grab_type_arg, nullptr };
                execvp(args[0], (char* const*)args);
            } else {
                const char *args[] = { "gsr-global-hotkeys", grab_type_arg, nullptr };
                execvp(args[0], (char* const*)args);
            }

            perror("gsr-global-hotkeys");
            _exit(127);
        } else { /* parent */
            process_id = pid;

            close(read_pipes[PIPE_WRITE]);
            read_pipes[PIPE_WRITE] = -1;

            close(write_pipes[PIPE_READ]);
            write_pipes[PIPE_READ] = -1;

            fcntl(read_pipes[PIPE_READ], F_SETFL, fcntl(read_pipes[PIPE_READ], F_GETFL) | O_NONBLOCK);
            read_file = fdopen(read_pipes[PIPE_READ], "r");
            if(read_file)
                read_pipes[PIPE_READ] = -1;
            else
                fprintf(stderr, "fdopen failed for read, error: %s\n", strerror(errno));
        }

        return true;
    }

    bool GlobalHotkeysLinux::bind_key_press(Hotkey hotkey, const std::string &id, GlobalHotkeyCallback callback) {
        if(process_id <= 0)
            return false;

        if(bound_actions_by_id.find(id) != bound_actions_by_id.end())
            return false;

        if(id.find(' ') != std::string::npos || id.find('\n') != std::string::npos) {
            fprintf(stderr, "Error: GlobalHotkeysLinux::bind_key_press: id \"%s\" contains either space or newline\n", id.c_str());
            return false;
        }

        if(hotkey.key == 0) {
            //fprintf(stderr, "Error: GlobalHotkeysLinux::bind_key_press: hotkey requires a key\n");
            return false;
        }

        if(hotkey.modifiers == 0) {
            //fprintf(stderr, "Error: GlobalHotkeysLinux::bind_key_press: hotkey requires a modifier\n");
            return false;
        }

        mgl_context *context = mgl_get_context();
        Display *display = (Display*)context->connection;
        const uint8_t keycode = x11_keycode_to_linux_keycode(XKeysymToKeycode(display, hotkey.key));
        const std::vector<uint8_t> modifiers = modifiers_to_linux_keys(hotkey.modifiers);
        const std::string modifiers_command = linux_keys_to_command_string(modifiers.data(), modifiers.size());

        char command[256];
        const int command_size = snprintf(command, sizeof(command), "bind %s %d+%s\n", id.c_str(), (int)keycode, modifiers_command.c_str());
        if(write(write_pipes[PIPE_WRITE], command, command_size) != command_size) {
            fprintf(stderr, "Error: GlobalHotkeysLinux::bind_key_press: failed to write command to gsr-global-hotkeys, error: %s\n", strerror(errno));
            return false;
        }

        bound_actions_by_id[id] = std::move(callback);
        return true;
    }

    void GlobalHotkeysLinux::unbind_all_keys() {
        if(process_id <= 0)
            return;

        if(bound_actions_by_id.empty())
            return;

        char command[32];
        const int command_size = snprintf(command, sizeof(command), "unbind_all\n");
        if(write(write_pipes[PIPE_WRITE], command, command_size) != command_size) {
            fprintf(stderr, "Error: GlobalHotkeysLinux::unbind_all_keys: failed to write command to gsr-global-hotkeys, error: %s\n", strerror(errno));
        }
        bound_actions_by_id.clear();
    }

    void GlobalHotkeysLinux::poll_events() {
        if(process_id <= 0) {
            //fprintf(stderr, "error: GlobalHotkeysLinux::poll_events failed, process has not been started yet. Use GlobalHotkeysLinux::start to start the process first\n");
            return;
        }

        if(!read_file) {
            //fprintf(stderr, "error: GlobalHotkeysLinux::poll_events failed, read file hasn't opened\n");
            return;
        }

        std::string action;
        char buffer[256];
        while(true) {
            char *line = fgets(buffer, sizeof(buffer), read_file);
            if(!line)
                break;

            int line_len = strlen(line);
            if(line_len == 0)
                continue;

            if(line[line_len - 1] == '\n') {
                line[line_len - 1] = '\0';
                --line_len;
            }

            action = line;
            auto it = bound_actions_by_id.find(action);
            if(it != bound_actions_by_id.end())
                it->second(action);
        }
    }
}
