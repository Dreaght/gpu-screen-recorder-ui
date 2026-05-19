#pragma once

#include <dlfcn.h>
#include <string>

namespace gsr {
    class SharedLibrary {
    public:
        SharedLibrary() = default;
        explicit SharedLibrary(const char *path, int flags = default_flags);
        SharedLibrary(const SharedLibrary&) = delete;
        SharedLibrary& operator=(const SharedLibrary&) = delete;
        SharedLibrary(SharedLibrary &&other) noexcept;
        SharedLibrary& operator=(SharedLibrary &&other) noexcept;
        ~SharedLibrary();

        bool load(const char *path, int flags = default_flags);
        void close();
        void release();

        bool is_loaded() const;
        const std::string& get_error() const;
        const std::string& get_path() const;

        void* get_symbol(const char *name);

        template<typename T>
        T get_symbol(const char *name) {
            return reinterpret_cast<T>(get_symbol(name));
        }

        static constexpr int default_flags = RTLD_NOW | RTLD_LOCAL;
    private:
        void *handle = nullptr;
        std::string path;
        std::string error;
    };
}
