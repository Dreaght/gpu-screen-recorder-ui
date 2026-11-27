#pragma once

#include "UplinkSource.hpp"

class UplinkSocketReceiver : public UplinkSource {
public:
    explicit UplinkSocketReceiver(int socket_fd);
    ~UplinkSocketReceiver() override;

    int poll_fd() const override { return socket_fd_; }
    UplinkEvent poll_next() override;

private:
    int socket_fd_ = -1;
};
