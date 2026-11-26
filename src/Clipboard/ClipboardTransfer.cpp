#include "../../include/Clipboard/ClipboardTransfer.hpp"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>

namespace gsr {
    namespace {
        static bool set_fd_cloexec(int fd) {
            const int flags = fcntl(fd, F_GETFD);
            if(flags < 0)
                return false;

            if(flags & FD_CLOEXEC)
                return true;

            return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
        }

        static bool wait_until_writable(int fd, int timeout_ms) {
            pollfd poll_fd;
            poll_fd.fd = fd;
            poll_fd.events = POLLOUT;
            poll_fd.revents = 0;

            while(true) {
                const int poll_result = poll(&poll_fd, 1, timeout_ms);
                if(poll_result > 0)
                    return (poll_fd.revents & (POLLOUT | POLLERR | POLLHUP)) == POLLOUT;

                if(poll_result == 0) {
                    errno = ETIMEDOUT;
                    return false;
                }

                if(errno != EINTR)
                    return false;
            }
        }
    }

    ClipboardTransferFile::ClipboardTransferFile(int fd, uint64_t size) : fd(fd), size(size) {}

    ClipboardTransferFile::~ClipboardTransferFile() {
        if(fd >= 0)
            close(fd);
    }

    ClipboardTransferFilePtr create_clipboard_transfer_file(const std::string &filepath) {
        if(filepath.empty()) {
            errno = EINVAL;
            return {};
        }

        const int input_fd = open(filepath.c_str(), O_RDONLY | O_CLOEXEC);
        if(input_fd < 0)
            return {};

        if(!set_fd_cloexec(input_fd)) {
            close(input_fd);
            return {};
        }

        struct stat stat;
        if(fstat(input_fd, &stat) == -1) {
            close(input_fd);
            return {};
        }

        return std::make_shared<ClipboardTransferFile>(input_fd, stat.st_size);
    }

    bool set_fd_nonblocking(int fd) {
        const int flags = fcntl(fd, F_GETFL);
        if(flags < 0)
            return false;

        if(flags & O_NONBLOCK)
            return true;

        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    bool read_clipboard_transfer_chunk(const ClipboardTransferFilePtr &file, uint64_t offset, unsigned char *buffer, size_t buffer_size, ssize_t *bytes_read) {
        if(!file || file->fd < 0 || !buffer || !bytes_read) {
            errno = EINVAL;
            return false;
        }

        while(true) {
            *bytes_read = pread(file->fd, buffer, buffer_size, offset);
            if(*bytes_read < 0 && errno == EINTR)
                continue;
            return *bytes_read >= 0;
        }
    }

    bool write_clipboard_transfer_data(int fd, const void *data, size_t size, int timeout_ms) {
        const unsigned char *ptr = (const unsigned char*)data;
        while(size > 0) {
            const ssize_t bytes_written = write(fd, ptr, size);
            if(bytes_written < 0) {
                if(errno == EINTR)
                    continue;

                if(errno == EAGAIN || errno == EWOULDBLOCK) {
                    if(wait_until_writable(fd, timeout_ms))
                        continue;
                }

                return false;
            }

            ptr += bytes_written;
            size -= bytes_written;
        }
        return true;
    }

    bool transfer_clipboard_transfer_file(const ClipboardTransferFilePtr &file, int output_fd, int timeout_ms) {
        unsigned char buffer[clipboard_transfer_chunk_size];
        uint64_t offset = 0;
        while(true) {
            ssize_t bytes_read = 0;
            if(!read_clipboard_transfer_chunk(file, offset, buffer, sizeof(buffer), &bytes_read))
                return false;

            if(bytes_read == 0)
                return true;

            if(!write_clipboard_transfer_data(output_fd, buffer, bytes_read, timeout_ms))
                return false;

            offset += bytes_read;
        }
    }

    bool send_clipboard_transfer_fd(int socket_fd, const ClipboardTransferFilePtr &file) {
        if(socket_fd < 0) {
            errno = EINVAL;
            return false;
        }

        uint8_t op = (file && file->fd >= 0) ? 1 : 0;
        struct iovec iov = {};
        iov.iov_base = &op;
        iov.iov_len = sizeof(op);

        struct msghdr msg = {};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        alignas(struct cmsghdr) char cmsg_buf[CMSG_SPACE(sizeof(int))] = {};
        if(op == 1) {
            msg.msg_control = cmsg_buf;
            msg.msg_controllen = sizeof(cmsg_buf);

            struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            cmsg->cmsg_len = CMSG_LEN(sizeof(int));
            memcpy(CMSG_DATA(cmsg), &file->fd, sizeof(int));
        }

        while(true) {
            const ssize_t sent = sendmsg(socket_fd, &msg, MSG_DONTWAIT | MSG_NOSIGNAL);
            if(sent >= 0)
                return true;

            if(errno == EINTR)
                continue;

            return false;
        }
    }
}
