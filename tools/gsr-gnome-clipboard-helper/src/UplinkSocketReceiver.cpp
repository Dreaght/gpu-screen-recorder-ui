#include "../include/UplinkSocketReceiver.hpp"

#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

namespace {

void close_ancillary_fds(struct msghdr &msg) {
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

}

UplinkSocketReceiver::UplinkSocketReceiver(int socket_fd) : socket_fd_(socket_fd) {
    if(socket_fd_ >= 0) {
        fcntl(socket_fd_, F_SETFD, FD_CLOEXEC);
    }
}

UplinkSocketReceiver::~UplinkSocketReceiver() {
    if(socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

UplinkEvent UplinkSocketReceiver::poll_next() {
    if(socket_fd_ < 0)
        return { UplinkEventType::Error, {} };

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
        n = recvmsg(socket_fd_, &msg, MSG_CMSG_CLOEXEC);
        if(n >= 0)
            break;
        if(errno == EINTR)
            continue;
        if(errno == EAGAIN || errno == EWOULDBLOCK)
            return { UplinkEventType::Empty, {} };
        return { UplinkEventType::Error, {} };
    }

    if(n == 0)
        return { UplinkEventType::Eof, {} };

    if(n != 1 || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
        close_ancillary_fds(msg);
        return { UplinkEventType::Error, {} };
    }

    if(op == 0) {
        if(msg.msg_controllen != 0) {
            close_ancillary_fds(msg);
            return { UplinkEventType::Error, {} };
        }
        return { UplinkEventType::Clear, {} };
    }

    if(op != 1) {
        close_ancillary_fds(msg);
        return { UplinkEventType::Error, {} };
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if(!cmsg || CMSG_NXTHDR(&msg, cmsg) != nullptr ||
        cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS ||
        cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
        close_ancillary_fds(msg);
        return { UplinkEventType::Error, {} };
    }

    int received_fd = -1;
    memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(int));
    if(received_fd < 0)
        return { UplinkEventType::Error, {} };

    return { UplinkEventType::SetFd, UniqueFd(received_fd) };
}
