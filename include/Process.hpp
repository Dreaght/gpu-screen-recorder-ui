#pragma once

#include <sys/types.h>

namespace gsr {
    enum class GsrMode {
        Replay,
        Record,
        Stream,
        Unknown
    };

    // Arguments ending with NULL
    bool exec_program_daemonized(const char **args);
    // Arguments ending with NULL. |read_fd| can be NULL
    pid_t exec_program(const char **args, int *read_fd);
    // |output_buffer| should be at least PATH_MAX in size
    bool read_cmdline_arg0(const char *filepath, char *output_buffer);
}