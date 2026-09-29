#pragma once

#include <string>
#include <sys/types.h>

namespace gsr {
    struct FocusedWindow {
        std::string title;
        // -1 if unknown
        pid_t pid = -1;
    };

    class DesktopEnvironment {
    public:
        DesktopEnvironment() = default;
        DesktopEnvironment(const DesktopEnvironment&) = delete;
        DesktopEnvironment& operator=(const DesktopEnvironment&) = delete;
        virtual ~DesktopEnvironment() = default;

        virtual bool start() = 0;
        virtual void update() = 0;
        // Returns an empty title if there is no focused window
        virtual FocusedWindow get_focused_window() = 0;
        // Return an empty string if unknown
        //virtual std::string get_focused_monitor_name() = 0;
    };
}