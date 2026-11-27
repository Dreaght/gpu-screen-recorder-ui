#pragma once

#include "UniqueFd.hpp"

class DownlinkSink {
public:
    virtual ~DownlinkSink() = default;
    virtual int poll_fd() const = 0;
    virtual void process_events() = 0;
    virtual bool is_ready() const = 0;
    virtual void send_fd(UniqueFd fd) = 0;
    virtual void send_clear() = 0;
};
