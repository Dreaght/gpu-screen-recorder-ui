#pragma once

#include "LibmpvCompat.hpp"
#include "SharedLibrary.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

struct mgl_window;

namespace gsr {
    class Libmpv {
    public:
        Libmpv();
        Libmpv(const Libmpv&) = delete;
        Libmpv& operator=(const Libmpv&) = delete;
        ~Libmpv();

        bool is_available() const;
        bool is_initialized() const;
        bool has_render_context() const;
        bool is_loaded() const;
        bool is_file_loaded() const;
        bool is_shutdown() const;
        bool needs_render_update() const;
        bool is_eof_reached() const;

        const std::string& get_error() const;
        const std::string& get_library_path() const;

        bool initialize();
        void reset();
        void clear_file();

        bool create_render_context();
        void destroy_render_context();
        bool render(int framebuffer_id, int width, int height, bool flip_y = true);
        bool update_render_context();

        bool load_file(const std::string &path);
        bool set_pause(bool pause);
        bool play();
        bool pause();
        bool seek_to_ms(int64_t position_ms, bool exact = true);
        bool seek_relative_ms(int64_t offset_ms);

        bool get_pause() const;
        int64_t get_position_ms() const;
        int64_t get_duration_ms() const;

        void process_events();
        const mpv_event* wait_event(double timeout_seconds);

        void set_wakeup_callback(std::function<void()> callback);
        void set_render_update_callback(std::function<void()> callback);
    private:
        using get_proc_address_callback = void* (*)(void*, const char*);

        bool load_library();
        bool load_symbols();
        void handle_event_state(const mpv_event *event);
        bool can_destroy_render_context() const;
        bool set_option_string(const char *name, const char *value);
        bool command(const char **args);
        bool command_async(const char **args, uint64_t reply_userdata = 0);
        bool dispatch_seek_to_ms(int64_t position_ms, bool exact);
        bool set_error(const std::string &error_message);
        bool set_mpv_error(int error_code, const std::string &action);
        bool get_property_flag(const char *name, bool default_value) const;
        double get_property_double(const char *name, double default_value) const;

        static void wakeup_callback(void *userdata);
        static void render_update_callback(void *userdata);
        static void* get_proc_address(void *ctx, const char *name);
    private:
        SharedLibrary library;
        std::string error;
        mpv_handle *handle = nullptr;
        mpv_render_context *render_context = nullptr;
        mgl_window *render_window = nullptr;
        bool file_loaded = false;
        bool shutdown = false;
        uint64_t next_async_command_userdata = 1;
        uint64_t active_seek_command_userdata = 0;
        bool pending_seek = false;
        int64_t pending_seek_position_ms = 0;
        bool pending_seek_exact = true;
        std::atomic_bool render_update_requested { false };
        mutable std::mutex callback_mutex;
        std::function<void()> wakeup_handler;
        std::function<void()> render_update_handler;

        const char* (*mpv_error_string_fn)(int error) = nullptr;
        void (*mpv_free_fn)(void *data) = nullptr;
        mpv_handle* (*mpv_create_fn)(void) = nullptr;
        int (*mpv_initialize_fn)(mpv_handle *ctx) = nullptr;
        void (*mpv_destroy_fn)(mpv_handle *ctx) = nullptr;
        int (*mpv_set_option_string_fn)(mpv_handle *ctx, const char *name, const char *data) = nullptr;
        int (*mpv_command_fn)(mpv_handle *ctx, const char **args) = nullptr;
        int (*mpv_command_async_fn)(mpv_handle *ctx, uint64_t reply_userdata, const char **args) = nullptr;
        int (*mpv_set_property_string_fn)(mpv_handle *ctx, const char *name, const char *data) = nullptr;
        int (*mpv_get_property_fn)(mpv_handle *ctx, const char *name, int format, void *data) = nullptr;
        char* (*mpv_get_property_string_fn)(mpv_handle *ctx, const char *name) = nullptr;
        mpv_event* (*mpv_wait_event_fn)(mpv_handle *ctx, double timeout) = nullptr;
        void (*mpv_set_wakeup_callback_fn)(mpv_handle *ctx, void (*cb)(void *d), void *d) = nullptr;

        int (*mpv_render_context_create_fn)(mpv_render_context **res, mpv_handle *mpv, mpv_render_param *params) = nullptr;
        void (*mpv_render_context_set_update_callback_fn)(mpv_render_context *ctx, void (*callback)(void *), void *callback_ctx) = nullptr;
        uint64_t (*mpv_render_context_update_fn)(mpv_render_context *ctx) = nullptr;
        int (*mpv_render_context_render_fn)(mpv_render_context *ctx, mpv_render_param *params) = nullptr;
        void (*mpv_render_context_free_fn)(mpv_render_context *ctx) = nullptr;
    };
}
