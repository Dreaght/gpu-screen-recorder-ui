#pragma once

#include <unistd.h>

class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if(this != &other)
            reset(other.release());
        return *this;
    }

    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }

    int release() {
        const int tmp = fd_;
        fd_ = -1;
        return tmp;
    }

    void reset(int new_fd = -1) {
        if(fd_ >= 0)
            close(fd_);
        fd_ = new_fd;
    }

private:
    int fd_ = -1;
};
