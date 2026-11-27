#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

static constexpr int UPLINK_FD = 3;

enum class RecvResult {
    Ok,
    Empty,
    Eof,
    Error
};

static void close_ancillary_fds(struct msghdr &msg) {
    for(struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if(cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS && cmsg->cmsg_len >= CMSG_LEN(0)) {
            const size_t fd_count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            const int *fds = reinterpret_cast<const int *>(CMSG_DATA(cmsg));
            for(size_t i = 0; i < fd_count; ++i) {
                if(fds[i] >= 0)
                    close(fds[i]);
            }
        }
    }
}

static RecvResult recv_clipboard_fd(int socket_fd, bool &is_clear, int &out_fd) {
    is_clear = false;
    out_fd = -1;

    uint8_t op = 0;
    struct iovec iov = {};
    iov.iov_base = &op;
    iov.iov_len = sizeof(op);

    alignas(struct cmsghdr) char cmsg_buf[CMSG_SPACE(sizeof(int))] = {};
    struct msghdr msg = {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    ssize_t n = -1;
    while(true) {
        n = recvmsg(socket_fd, &msg, MSG_CMSG_CLOEXEC);
        if(n >= 0)
            break;
        if(errno == EINTR)
            continue;
        if(errno == EAGAIN || errno == EWOULDBLOCK)
            return RecvResult::Empty;
        return RecvResult::Error;
    }

    if(n == 0)
        return RecvResult::Eof;

    if(n != 1 || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
        close_ancillary_fds(msg);
        return RecvResult::Error;
    }

    if(op == 0) {
        if(msg.msg_controllen != 0) {
            close_ancillary_fds(msg);
            return RecvResult::Error;
        }
        is_clear = true;
        return RecvResult::Ok;
    }

    if(op != 1) {
        close_ancillary_fds(msg);
        return RecvResult::Error;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if(!cmsg || CMSG_NXTHDR(&msg, cmsg) != nullptr ||
        cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS ||
        cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
        close_ancillary_fds(msg);
        return RecvResult::Error;
        }

    memcpy(&out_fd, CMSG_DATA(cmsg), sizeof(int));
    return out_fd >= 0 ? RecvResult::Ok : RecvResult::Error;
}


int main() {
    fcntl(UPLINK_FD, F_SETFD, FD_CLOEXEC);

    int cached_fd = -1;

    struct pollfd pfd = {};
    pfd.fd = UPLINK_FD;
    pfd.events = POLLIN;

    while(true) {
        int ret = poll(&pfd, 1, -1);
        if(ret < 0) {
            if(errno == EINTR)
                continue;
            perror("Error: gsr-gnome-clipboard-helper: poll");
            break;
        }

        if(pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
            while(true) {
                bool is_clear = false;
                int new_fd = -1;
                RecvResult res = recv_clipboard_fd(UPLINK_FD, is_clear, new_fd);

                if(res == RecvResult::Empty)
                    break;

                if(res == RecvResult::Eof || res == RecvResult::Error) {
                    if(cached_fd >= 0)
                        close(cached_fd);
                    return res == RecvResult::Eof ? 0 : 1;
                }

                if(cached_fd >= 0) {
                    close(cached_fd);
                    cached_fd = -1;
                }

                if(is_clear) {
                    fprintf(stderr, "Info: gsr-gnome-clipboard-helper: received CLEAR\n");
                } else if (new_fd >= 0) {
                    cached_fd = new_fd;
                    struct stat st = {};
                    fstat(cached_fd, &st);
                    fprintf(stderr, "Info: gsr-gnome-clipboard-helper: received FD=%d (%lld bytes)\n",
                            cached_fd, (long long)st.st_size);
                }
            }
        }
    }

    if(cached_fd >= 0)
        close(cached_fd);
    return 0;
}
