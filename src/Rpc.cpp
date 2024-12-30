#include "../include/Rpc.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/fcntl.h>

namespace gsr {
    static void get_runtime_filepath(char *buffer, size_t buffer_size, const char *filename) {
        char dir[PATH_MAX];

        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if(runtime_dir)
            snprintf(dir, sizeof(dir), "%s", runtime_dir);
        else
            snprintf(dir, sizeof(dir), "/run/user/%d", geteuid());

        if(access(dir, F_OK) != 0)
            snprintf(dir, sizeof(dir), "/tmp");

        snprintf(buffer, buffer_size, "%s/%s", dir, filename);
    }

    Rpc::~Rpc() {
        if(fd > 0)
            close(fd);

        if(file)
            fclose(file);

        if(!fifo_filepath.empty())
            remove(fifo_filepath.c_str());
    }

    bool Rpc::create(const char *name) {
        if(file) {
            fprintf(stderr, "Error: Rpc::create: already created/opened\n");
            return false;
        }

        char fifo_filepath_tmp[PATH_MAX];
        get_runtime_filepath(fifo_filepath_tmp, sizeof(fifo_filepath_tmp), name);
        fifo_filepath = fifo_filepath_tmp;
        remove(fifo_filepath.c_str());

        if(mkfifo(fifo_filepath.c_str(), 0600) != 0) {
            fprintf(stderr, "Error: mkfifo failed, error: %s, %s\n", strerror(errno), fifo_filepath.c_str());
            return false;
        }

        if(!open_filepath(fifo_filepath.c_str())) {
            remove(fifo_filepath.c_str());
            fifo_filepath.clear();
            return false;
        }

        return true;
    }

    bool Rpc::open(const char *name) {
        if(file) {
            fprintf(stderr, "Error: Rpc::open: already created/opened\n");
            return false;
        }

        char fifo_filepath_tmp[PATH_MAX];
        get_runtime_filepath(fifo_filepath_tmp, sizeof(fifo_filepath_tmp), name);
        return open_filepath(fifo_filepath_tmp);
    }

    bool Rpc::open_filepath(const char *filepath) {
        fd = ::open(filepath, O_RDWR | O_NONBLOCK);
        if(fd <= 0)
            return false;

        file = fdopen(fd, "r+");
        if(!file) {
            close(fd);
            fd = 0;
            return false;
        }
        fd = 0;
        return true;
    }

    bool Rpc::write(const char *str, size_t size) {
        if(!file) {
            fprintf(stderr, "Error: Rpc::write: fifo not created/opened yet\n");
            return false;
        }

        ssize_t offset = 0;
        while(offset < (ssize_t)size) {
            const ssize_t bytes_written = fwrite(str + offset, 1, size - offset, file);
            fflush(file);
            if(bytes_written > 0)
                offset += bytes_written;
        }
        return true;
    }

    void Rpc::poll() {
        if(!file) {
            //fprintf(stderr, "Error: Rpc::poll: fifo not created/opened yet\n");
            return;
        }

        std::string name;
        char line[1024];
        while(fgets(line, sizeof(line), file)) {
            int line_len = strlen(line);
            if(line_len == 0)
                continue;

            if(line[line_len - 1] == '\n') {
                line[line_len - 1] = '\0';
                --line_len;
            }

            name = line;
            auto it = handlers_by_name.find(name);
            if(it != handlers_by_name.end())
                it->second(name);
        }
    }

    bool Rpc::add_handler(const std::string &name, RpcCallback callback) {
        return handlers_by_name.insert(std::make_pair(name, std::move(callback))).second;
    }
}