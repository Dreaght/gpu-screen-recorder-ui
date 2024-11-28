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

#define PIPE_READ 0
#define PIPE_WRITE 1

namespace gsr {
    static void debug_print_args(const char **args) {
        fprintf(stderr, "gsr-ui info: running command:");
        while(*args) {
            fprintf(stderr, " %s", *args);
            ++args;
        }
        fprintf(stderr, "\n");
    }

    bool exec_program_daemonized(const char **args) {
        /* 1 argument */
        if(args[0] == nullptr)
            return false;

        debug_print_args(args);

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

    pid_t exec_program(const char **args, int *read_fd) {
        if(read_fd)
            *read_fd = -1;

        /* 1 argument */
        if(args[0] == nullptr)
            return -1;

        int fds[2] = {-1, -1};
        if(pipe(fds) == -1)
            return -1;

        debug_print_args(args);

        pid_t pid = vfork();
        if(pid == -1) {
            close(fds[PIPE_READ]);
            close(fds[PIPE_WRITE]);
            perror("Failed to vfork");
            return -1;
        } else if(pid == 0) { /* child */
            dup2(fds[PIPE_WRITE], STDOUT_FILENO);
            close(fds[PIPE_READ]);
            close(fds[PIPE_WRITE]);

            execvp(args[0], (char* const*)args);
            perror("execvp");
            _exit(127);
        } else { /* parent */
            close(fds[PIPE_WRITE]);
            if(read_fd)
                *read_fd = fds[PIPE_READ];
            else
                close(fds[PIPE_READ]);
            return pid;
        }
    }

    bool read_cmdline_arg0(const char *filepath, char *output_buffer) {
        output_buffer[0] = '\0';

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
}