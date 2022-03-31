#include "../include/Process.hpp"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <fcntl.h>

namespace gsr {
    bool exec_program_daemonized(const char **args) {
        /* 1 argument */
        if(args[0] == nullptr)
            return -1;

        pid_t pid = vfork();
        if(pid == -1) {
            perror("Failed to vfork");
            return false;
        } else if(pid == 0) { /* child */
            setsid();
            signal(SIGHUP, SIG_IGN);

            // Daemonize child to make the parent the init process which will reap the zombie child
            pid_t second_child = vfork();
            if(second_child == 0) { // child
                execvp(args[0], (char* const*)args);
                perror("execvp");
                _exit(127);
            } else if(second_child != -1) {
                _exit(0);
            }
        } else { /* parent */
            waitpid(pid, nullptr, 0);
        }
        return true;
    }

    static const char *pid_file = "/tmp/gpu-screen-recorder";

    static bool is_process_running_program(pid_t pid, const char *program_name) {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "/proc/%ld/exe", (long)pid);

        char resolved_path[PATH_MAX];
        const ssize_t resolved_path_len = readlink(filepath, resolved_path, sizeof(resolved_path) - 1);
        if(resolved_path_len == -1)
            return false;
        
        resolved_path[resolved_path_len] = '\0';

        const int program_name_len = strlen(program_name);
        return resolved_path_len >= program_name_len && memcmp(resolved_path + resolved_path_len - program_name_len, program_name, program_name_len) == 0;
    }

    bool is_gpu_screen_recorder_running(int &gsr_pid, GsrMode &mode) {
        gsr_pid = -1;
        mode = GsrMode::Unknown;

        char buffer[256];
        int fd = open(pid_file, O_RDONLY);
        if(fd == -1)
            return false;

        ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        if(bytes_read < 0) {
            perror("failed to read gpu-screen-recorder pid file");
            close(fd);
            return true;
        }
        buffer[bytes_read] = '\0';
        close(fd);

        long pid = 0;
        if(sscanf(buffer, "%ld %120s", &pid, buffer) == 2) {
            gsr_pid = pid;
            if(is_process_running_program(pid, "gpu-screen-recorder")) {
                if(strcmp(buffer, "replay") == 0)
                    mode = GsrMode::Replay;
                else if(strcmp(buffer, "record") == 0)
                    mode = GsrMode::Record;
                else if(strcmp(buffer, "stream") == 0)
                    mode = GsrMode::Stream;
                else
                    mode = GsrMode::Unknown;
                return true;
            }
        } else {
            fprintf(stderr, "Warning: gpu-screen-recorder pid file is in incorrect format, it's possible that its corrupt. Ignoring...\n");
        }
        return false;
    }
}