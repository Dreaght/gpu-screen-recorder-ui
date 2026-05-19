#include "../include/SharedLibrary.hpp"

#include <dlfcn.h>

namespace gsr {
    SharedLibrary::SharedLibrary(const char *path, int flags) {
        load(path, flags);
    }

    SharedLibrary::SharedLibrary(SharedLibrary &&other) noexcept {
        handle = other.handle;
        path = std::move(other.path);
        error = std::move(other.error);

        other.handle = nullptr;
        other.path.clear();
        other.error.clear();
    }

    SharedLibrary& SharedLibrary::operator=(SharedLibrary &&other) noexcept {
        if(this == &other)
            return *this;

        close();

        handle = other.handle;
        path = std::move(other.path);
        error = std::move(other.error);

        other.handle = nullptr;
        other.path.clear();
        other.error.clear();
        return *this;
    }

    SharedLibrary::~SharedLibrary() {
        close();
    }

    bool SharedLibrary::load(const char *path, int flags) {
        const std::string requested_path = path ? path : "";

        close();

        this->path = requested_path;
        error.clear();

        if(requested_path.empty()) {
            error = "shared library path is empty";
            return false;
        }

        dlerror();
        handle = dlopen(requested_path.c_str(), flags);
        if(!handle) {
            const char *dlerror_str = dlerror();
            error = dlerror_str ? dlerror_str : "dlopen failed";
            return false;
        }

        return true;
    }

    void SharedLibrary::close() {
        if(handle) {
            dlclose(handle);
            handle = nullptr;
        }

        path.clear();
        error.clear();
    }

    void SharedLibrary::release() {
        handle = nullptr;
        path.clear();
        error.clear();
    }

    bool SharedLibrary::is_loaded() const {
        return handle != nullptr;
    }

    const std::string& SharedLibrary::get_error() const {
        return error;
    }

    const std::string& SharedLibrary::get_path() const {
        return path;
    }

    void* SharedLibrary::get_symbol(const char *name) {
        if(!handle) {
            error = "shared library is not loaded";
            return nullptr;
        }

        if(!name || !name[0]) {
            error = "shared library symbol name is empty";
            return nullptr;
        }

        dlerror();
        void *symbol = dlsym(handle, name);
        const char *dlerror_str = dlerror();
        if(dlerror_str) {
            error = dlerror_str;
            return nullptr;
        }

        error.clear();
        return symbol;
    }
}
