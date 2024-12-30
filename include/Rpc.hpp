#pragma once

#include <stddef.h>
#include <functional>
#include <unordered_map>
#include <string>

typedef struct _IO_FILE FILE;

namespace gsr {
    using RpcCallback = std::function<void(const std::string &name)>;

    class Rpc {
    public:
        Rpc() = default;
        Rpc(const Rpc&) = delete;
        Rpc& operator=(const Rpc&) = delete;
        ~Rpc();

        bool create(const char *name);
        bool open(const char *name);
        bool write(const char *str, size_t size);
        void poll();

        bool add_handler(const std::string &name, RpcCallback callback);
    private:
        bool open_filepath(const char *filepath);
    private:
        int fd = 0;
        FILE *file = nullptr;
        std::string fifo_filepath;
        std::unordered_map<std::string, RpcCallback> handlers_by_name;
    };
}