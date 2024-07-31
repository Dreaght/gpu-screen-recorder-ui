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
    bool is_gpu_screen_recorder_running(pid_t &gsr_pid, GsrMode &mode);
}