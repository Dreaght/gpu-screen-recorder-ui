#include "../include/Process.hpp"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>

namespace gsr {
    bool exec_program_daemonized(const char **args) {
        /* 1 argument */
        if(args[0] == nullptr)
            return false;

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
                // TODO:
                _exit(0);
            }
        } else { /* parent */
            waitpid(pid, nullptr, 0);
        }

        return true;
    }

    static bool is_number(const char *str) {
        while(*str) {
            char c = *str;
            if(c < '0' || c > '9')
                return false;
            ++str;
        }
        return true;
    }

    static bool read_cmdline(const char *filepath, char *output_buffer) {
        const char *arg0_end = NULL;
        int fd = open(filepath, O_RDONLY);
        if(fd == -1)
            return false;

        char buffer[PATH_MAX];
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
        if(bytes_read == -1)
            goto err;

        arg0_end = (const char*)memchr(buffer, '\0', bytes_read);
        if(!arg0_end)
            goto err;

        memcpy(output_buffer, buffer, arg0_end - buffer);
        output_buffer[arg0_end - buffer] = '\0';
        close(fd);
        return true;

        err:
        close(fd);
        return false;
    }

    static pid_t pidof(const char *process_name) {
        pid_t result = -1;
        DIR *dir = opendir("/proc");
        if(!dir)
            return -1;

        char cmdline_filepath[PATH_MAX];
        char arg0[PATH_MAX];

        struct dirent *entry;
        while((entry = readdir(dir)) != NULL) {
            if(!is_number(entry->d_name))
                continue;

            snprintf(cmdline_filepath, sizeof(cmdline_filepath), "/proc/%s/cmdline", entry->d_name);
            if(read_cmdline(cmdline_filepath, arg0) && strcmp(process_name, arg0) == 0) {
                result = atoi(entry->d_name);
                break;
            }
        }

        closedir(dir);
        return result;
    }

    bool is_gpu_screen_recorder_running(pid_t &gsr_pid, GsrMode &mode) {
        // TODO: Set |mode| by checking cmdline
        gsr_pid = pidof("gpu-screen-recorder");
        if(gsr_pid == -1) {
            mode = GsrMode::Unknown;
            return false;
        } else {
            mode = GsrMode::Record;
            return true;
        }
    }
}