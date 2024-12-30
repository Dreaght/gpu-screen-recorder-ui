#include "../include/GlobalHotkeysLinux.hpp"
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>

#define PIPE_READ 0
#define PIPE_WRITE 1

namespace gsr {
    GlobalHotkeysLinux::GlobalHotkeysLinux() {
        for(int i = 0; i < 2; ++i) {
            pipes[i] = -1;
        }
    }

    GlobalHotkeysLinux::~GlobalHotkeysLinux() {
        for(int i = 0; i < 2; ++i) {
            if(pipes[i] > 0)
                close(pipes[i]);
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
        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
        const char *user_homepath = getenv("HOME");
        if(!user_homepath)
            user_homepath = "/tmp";

        char gsr_global_hotkeys_flatpak[PATH_MAX];
        snprintf(gsr_global_hotkeys_flatpak, sizeof(gsr_global_hotkeys_flatpak), "%s/.local/share/gpu-screen-recorder/gsr-global-hotkeys", user_homepath);

        if(process_id > 0)
            return false;

        if(pipe(pipes) == -1)
            return false;

        const pid_t pid = vfork();
        if(pid == -1) {
            perror("Failed to vfork");
            for(int i = 0; i < 2; ++i) {
                close(pipes[i]);
                pipes[i] = -1;
            }
            return false;
        } else if(pid == 0) { /* child */
            dup2(pipes[PIPE_WRITE], STDOUT_FILENO);
            for(int i = 0; i < 2; ++i) {
                close(pipes[i]);
            }

            if(inside_flatpak) {
                const char *args[] = { "flatpak-spawn", "--host", "--", gsr_global_hotkeys_flatpak, NULL };
                execvp(args[0], (char* const*)args);
            } else {
                const char *args[] = { "gsr-global-hotkeys", NULL };
                execvp(args[0], (char* const*)args);
            }

            perror("execvp");
            _exit(127);
        } else { /* parent */
            process_id = pid;
            close(pipes[PIPE_WRITE]);
            pipes[PIPE_WRITE] = -1;

            const int fdl = fcntl(pipes[PIPE_READ], F_GETFL);
            fcntl(pipes[PIPE_READ], F_SETFL, fdl | O_NONBLOCK);

            read_file = fdopen(pipes[PIPE_READ], "r");
            if(read_file)
                pipes[PIPE_READ] = -1;
            else
                fprintf(stderr, "fdopen failed, error: %s\n", strerror(errno));
        }

        return true;
    }

    bool GlobalHotkeysLinux::bind_action(const std::string &id, GlobalHotkeyCallback callback) {
        return bound_actions_by_id.insert(std::make_pair(id, std::move(callback))).second;
    }

    void GlobalHotkeysLinux::poll_events() {
        if(process_id <= 0) {
            fprintf(stderr, "error: GlobalHotkeysLinux::poll_events failed, process has not been started yet. Use GlobalHotkeysLinux::start to start the process first\n");
            return;
        }

        if(!read_file) {
            fprintf(stderr, "error: GlobalHotkeysLinux::poll_events failed, read file hasn't opened\n");
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
