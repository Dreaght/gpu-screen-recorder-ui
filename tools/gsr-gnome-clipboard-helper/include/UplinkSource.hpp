#pragma once

#include "UniqueFd.hpp"

enum class UplinkEventType {
    Empty,
    SetFd,
    Clear,
    Eof,
    Error
};

struct UplinkEvent {
    UplinkEventType type = UplinkEventType::Empty;
    UniqueFd fd;
};

class UplinkSource {
public:
    virtual ~UplinkSource() = default;
    virtual int poll_fd() const = 0;
    virtual UplinkEvent poll_next() = 0;
};
