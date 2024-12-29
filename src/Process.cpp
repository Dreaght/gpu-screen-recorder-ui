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

    static bool is_number(const char *str) {
        for(int i = 0; str[i]; ++i) {
            char c = str[i];
            if(c < '0' || c > '9')
                return false;
        }
        return true;
    }

    static int count_num_args(const char **args) {
        int num_args = 0;
        while(*args) {
            ++num_args;
            ++args;
        }
        return num_args;
    }

    bool exec_program_daemonized(const char **args) {
        /* 1 argument */
        if(args[0] == nullptr)
            return false;

        debug_print_args(args);

        const pid_t pid = vfork();
        if(pid == -1) {
            perror("Failed to vfork");
            return false;
        } else if(pid == 0) { /* child */
            setsid();
            signal(SIGHUP, SIG_IGN);

            // Daemonize child to make the parent the init process which will reap the zombie child
            const pid_t second_child = vfork();
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

        const pid_t pid = vfork();
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

    int exec_program_get_stdout(const char **args, std::string &result) {
        result.clear();
        int read_fd = -1;
        const pid_t process_id = exec_program(args, &read_fd);
        if(process_id == -1)
            return -1;

        int exit_status = 0;
        char buffer[8192];
        for(;;) {
            ssize_t bytes_read = read(read_fd, buffer, sizeof(buffer));
            if(bytes_read == 0) {
                break;
            } else if(bytes_read == -1) {
                fprintf(stderr, "Failed to read from pipe to program %s, error: %s\n", args[0], strerror(errno));
                exit_status = -1;
                break;
            }

            buffer[bytes_read] = '\0';
            result.append(buffer, bytes_read);
        }

        if(exit_status != 0)
            kill(process_id, SIGKILL);

        int status = 0;
        if(waitpid(process_id, &status, 0) == -1) {
            perror("waitpid failed");
            exit_status = -1;
        }

        if(!WIFEXITED(status))
            exit_status = -1;

        if(exit_status == 0)
            exit_status = WEXITSTATUS(status);

        close(read_fd);
        return exit_status;
    }

    int exec_program_on_host_get_stdout(const char **args, std::string &result) {
        if(count_num_args(args) > 64 - 3) {
            fprintf(stderr, "Error: too many arguments when trying to launch \"%s\"\n", args[0]);
            return -1;
        }

        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
        if(inside_flatpak) {
            // Assumes programs wont need more than 64 - 3 args
            const char *modified_args[64] = { "flatpak-spawn", "--host", "--" };
            for(int i = 3; i < 64; ++i) {
                const char *arg = args[i - 3];
                if(!arg) {
                    modified_args[i] = nullptr;
                    break;
                }
                modified_args[i] = arg;
            }
            return exec_program_get_stdout(modified_args, result);
        } else {
            return exec_program_get_stdout(args, result);
        }
    }

    // |output_buffer| should be at least PATH_MAX in size
    bool read_cmdline_arg0(const char *filepath, char *output_buffer, int output_buffer_size) {
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

        if((arg0_end - buffer) + 1 <= output_buffer_size) {
            memcpy(output_buffer, buffer, arg0_end - buffer);
            output_buffer[arg0_end - buffer] = '\0';
            close(fd);
            return true;
        }

        err:
        close(fd);
        return false;
    }

    pid_t pidof(const char *process_name) {
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
            if(read_cmdline_arg0(cmdline_filepath, arg0, sizeof(arg0)) && strcmp(process_name, arg0) == 0) {
                result = atoi(entry->d_name);
                break;
            }
        }

        closedir(dir);
        return result;
    }
}