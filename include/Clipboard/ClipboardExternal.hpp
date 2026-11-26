#pragma once

#include "Clipboard.hpp"
#include "ClipboardTransfer.hpp"

namespace gsr {
    class ClipboardExternal : public Clipboard {
    public:
        explicit ClipboardExternal(std::string helper_name);
        ~ClipboardExternal() override;
        ClipboardExternal(const ClipboardExternal&) = delete;
        ClipboardExternal& operator=(const ClipboardExternal&) = delete;

        void set_current_file(const std::string &filepath, FileType file_type) override;
    private:
        bool create_socket(int sv[2]);
        bool spawn_helper(const std::string &helper_name, int child_fd);
        void shutdown();
    private:
        int socket_fd = -1;
        pid_t process_id = -1;
    };
}
