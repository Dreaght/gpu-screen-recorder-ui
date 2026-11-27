#pragma once

#include "DownlinkSink.hpp"
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <atomic>

class StubDownlinkSink : public DownlinkSink {
public:
    StubDownlinkSink() {
        instance_ = this;
        signal(SIGUSR1, &StubDownlinkSink::on_sigusr1);
    }

    int poll_fd() const override { return -1; }
    void process_events() override {}
    bool is_ready() const override { return ready_.load(); }

    void send_fd(UniqueFd fd) override {
        struct stat st = {};
        fstat(fd.get(), &st);
        fprintf(stderr, "Info: StubDownlinkSink: consumed FD=%d (%lld bytes)\n",
                fd.get(), (long long)st.st_size);
    }

    void send_clear() override {
        fprintf(stderr, "Info: StubDownlinkSink: consumed CLEAR\n");
    }

private:
    static void on_sigusr1(int) {
        if(instance_)
            instance_->ready_.store(!instance_->ready_.load());
    }

    std::atomic<bool> ready_{false};
    static inline StubDownlinkSink *instance_ = nullptr;
};
