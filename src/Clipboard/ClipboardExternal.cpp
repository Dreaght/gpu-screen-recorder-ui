#include "../../include/Clipboard/ClipboardExternal.hpp"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>

namespace gsr {

    ClipboardExternal::ClipboardExternal(std::string helper_name) {
        int sv[2] = { -1, -1 };
        if(!create_socket(sv))
            return;

        if(!spawn_helper(helper_name, sv[1])) {
            close(sv[0]);
            close(sv[1]);
            return;
        }

        close(sv[1]);
        socket_fd = sv[0];
    }

    ClipboardExternal::~ClipboardExternal() {
        shutdown();
    }

    bool ClipboardExternal::create_socket(int sv[2]) {
        if(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == -1) {
            perror("Error: ClipboardExternal: socketpair");
            return false;
        }
        ::shutdown(sv[0], SHUT_RD);
        return true;
    }

    bool ClipboardExternal::spawn_helper(const std::string &helper_name, int child_fd) {
        const pid_t pid = fork();
        if(pid == -1) {
            perror("Error: ClipboardExternal: fork");
            return false;
        }

        if(pid == 0) {
            if(child_fd != 3) {
                if(dup2(child_fd, 3) == -1)
                    _exit(1);
                close(child_fd);
            }
            fcntl(3, F_SETFD, 0);
            ::shutdown(3, SHUT_WR);

            const char *args[] = { helper_name.c_str(), NULL };
            execvp(args[0], const_cast<char *const *>(args));
            _exit(127);
        }

        process_id = pid;
        return true;
    }

    void ClipboardExternal::shutdown() {
        if(process_id > 0) {
            kill(process_id, SIGKILL);
            int status;
            if(waitpid(process_id, &status, 0) == -1) {
                perror("waitpid failed");
            }
            process_id = -1;
        }

        if(socket_fd >= 0) {
            close(socket_fd);
            socket_fd = -1;
        }
    }

    void ClipboardExternal::set_current_file(const std::string &filepath, FileType file_type) {
        (void)file_type;

        if(socket_fd < 0)
            return;

        if(filepath.empty()) {
            send_clipboard_transfer_fd(socket_fd, nullptr);
            return;
        }

        const ClipboardTransferFilePtr file = create_clipboard_transfer_file(filepath);
        if(!file) {
            fprintf(stderr, "gsr ui: error: ClipboardDbus: failed to open clipboard file %s, error: %s\n", filepath.c_str(), strerror(errno));
            return;
        }

        if(!send_clipboard_transfer_fd(socket_fd, file)) {
            fprintf(stderr, "gsr ui: error: ClipboardDbus: failed to send clipboard fd, error: %s\n", strerror(errno));
        }
    }
}
