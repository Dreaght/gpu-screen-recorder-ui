#include "../include/UplinkSocketReceiver.hpp"
#include "../include/StubDownlinkSink.hpp"
#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <memory>
#include <utility>

static constexpr int UPLINK_FD = 3;

static int run_bridge(UplinkSource &uplink, DownlinkSink &downlink) {
    UniqueFd pending_fd;
    bool pending_clear = false;

    while(true) {
        if(downlink.is_ready()) {
            if(pending_fd) {
                downlink.send_fd(std::move(pending_fd));
                pending_clear = false;
            } else if (pending_clear) {
                downlink.send_clear();
                pending_clear = false;
            }
        }

        struct pollfd pfds[2] = {};
        nfds_t nfds = 0;

        pfds[nfds].fd = uplink.poll_fd();
        pfds[nfds].events = POLLIN;
        const nfds_t uplink_idx = nfds++;

        const int dl_fd = downlink.poll_fd();
        nfds_t downlink_idx = static_cast<nfds_t>(-1);
        if (dl_fd >= 0) {
            pfds[nfds].fd = dl_fd;
            pfds[nfds].events = POLLIN;
            downlink_idx = nfds++;
        }

        const int ret = poll(pfds, nfds, -1);
        if(ret < 0) {
            if(errno == EINTR)
                continue;
            perror("Error: gsr-gnome-clipboard-helper: poll");
            return 1;
        }

        if(downlink_idx != static_cast<nfds_t>(-1) &&
            (pfds[downlink_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
            downlink.process_events();
        }

        if(pfds[uplink_idx].revents & (POLLIN | POLLHUP | POLLERR)) {
            while(true) {
                UplinkEvent ev = uplink.poll_next();
                if(ev.type == UplinkEventType::Empty)
                    break;
                if(ev.type == UplinkEventType::Eof)
                    return 0;
                if(ev.type == UplinkEventType::Error)
                    return 1;

                if(ev.type == UplinkEventType::SetFd) {
                    pending_fd = std::move(ev.fd);
                    pending_clear = false;
                } else if (ev.type == UplinkEventType::Clear) {
                    pending_fd.reset();
                    pending_clear = true;
                }
            }
        }
    }
}

int main() {
    std::unique_ptr<UplinkSource> uplink = std::make_unique<UplinkSocketReceiver>(UPLINK_FD);
    std::unique_ptr<DownlinkSink> downlink = std::make_unique<StubDownlinkSink>();

    return run_bridge(*uplink, *downlink);
}
