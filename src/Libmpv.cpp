#include "../include/Libmpv.hpp"

#include <cmath>
#include <dlfcn.h>
#include <stdio.h>

extern "C" {
#include <mgl/mgl.h>
}

namespace gsr {
    namespace {
        template<typename T>
        bool load_symbol(SharedLibrary &library, T &func, const char *name, std::string &error) {
            func = library.get_symbol<T>(name);
            if(func)
                return true;

            error = "failed to load libmpv symbol '" + std::string(name) + "': " + library.get_error();
            return false;
        }
    }

    Libmpv::Libmpv() {
        load_library();
    }

    Libmpv::~Libmpv() {
        reset();
    }

    bool Libmpv::is_available() const {
        return library.is_loaded() && mpv_create_fn && mpv_initialize_fn && mpv_destroy_fn;
    }

    bool Libmpv::is_initialized() const {
        return handle != nullptr;
    }

    bool Libmpv::has_render_context() const {
        return render_context != nullptr;
    }

    bool Libmpv::is_loaded() const {
        return library.is_loaded();
    }

    bool Libmpv::is_file_loaded() const {
        return file_loaded;
    }

    bool Libmpv::is_shutdown() const {
        return shutdown;
    }

    bool Libmpv::needs_render_update() const {
        return render_update_requested;
    }

    bool Libmpv::is_eof_reached() const {
        return get_property_flag("eof-reached", false);
    }

    const std::string& Libmpv::get_error() const {
        return error;
    }

    const std::string& Libmpv::get_library_path() const {
        return library.get_path();
    }

    bool Libmpv::initialize() {
        if(handle)
            return true;

        if(!load_library())
            return false;

        handle = mpv_create_fn();
        if(!handle)
            return set_error("failed to create libmpv instance");

        if(!set_option_string("terminal", "no") ||
           !set_option_string("msg-level", "all=error") ||
           !set_option_string("config", "no") ||
           !set_option_string("vo", "libmpv") ||
           !set_option_string("keep-open", "yes") ||
           !set_option_string("osc", "no") ||
           !set_option_string("input-default-bindings", "no") ||
           !set_option_string("input-vo-keyboard", "no") ||
           !set_option_string("audio-display", "no")) {
            reset();
            return false;
        }

        if(!set_mpv_error(mpv_initialize_fn(handle), "initialize libmpv")) {
            reset();
            return false;
        }

        mpv_set_wakeup_callback_fn(handle, wakeup_callback, this);
        error.clear();
        shutdown = false;
        file_loaded = false;
        return true;
    }

    void Libmpv::reset() {
        if(render_context && !can_destroy_render_context()) {
            fprintf(stderr, "Warning: leaking libmpv state because the owning GL context is no longer current during teardown\n");
            if(handle)
                mpv_set_wakeup_callback_fn(handle, nullptr, nullptr);
            if(render_context)
                mpv_render_context_set_update_callback_fn(render_context, nullptr, nullptr);
            library.release();
            handle = nullptr;
            render_context = nullptr;
            render_window = nullptr;
            return;
        }

        destroy_render_context();

        if(handle) {
            mpv_set_wakeup_callback_fn(handle, nullptr, nullptr);
            mpv_destroy_fn(handle);
            handle = nullptr;
        }

        file_loaded = false;
        shutdown = false;
        active_seek_command_userdata = 0;
        next_async_command_userdata = 1;
        pending_seek = false;
        render_update_requested = false;
    }

    void Libmpv::clear_file() {
        if(!initialize())
            return;

        const char *args[] = {
            "stop",
            nullptr
        };
        command(args);
        file_loaded = false;
    }

    bool Libmpv::create_render_context() {
        if(render_context)
            return true;

        if(!initialize())
            return false;

        mgl_context *context = mgl_get_context();
        if(!context)
            return set_error("mgl context is not available");

        mpv_opengl_init_params opengl_init_params = {
            .get_proc_address = get_proc_address,
            .get_proc_address_ctx = context,
        };

        int advanced_control = 0;
        const char *api_type = MPV_RENDER_API_TYPE_OPENGL;
        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_API_TYPE, (void*)api_type },
            { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &opengl_init_params },
            { MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced_control },
            { context->window_system == MGL_WINDOW_SYSTEM_WAYLAND ? MPV_RENDER_PARAM_WL_DISPLAY : MPV_RENDER_PARAM_X11_DISPLAY, context->connection },
            { MPV_RENDER_PARAM_INVALID, nullptr }
        };

        if(!set_mpv_error(mpv_render_context_create_fn(&render_context, handle, params), "create libmpv render context"))
            return false;

        render_window = context->current_window;
        mpv_render_context_set_update_callback_fn(render_context, render_update_callback, this);
        render_update_requested.store(true);
        error.clear();
        return true;
    }

    void Libmpv::destroy_render_context() {
        if(render_context && !can_destroy_render_context()) {
            fprintf(stderr, "Warning: refusing to destroy libmpv render context because the owning GL context is no longer current\n");
            return;
        }

        if(render_context) {
            mpv_render_context_set_update_callback_fn(render_context, nullptr, nullptr);
            mpv_render_context_free_fn(render_context);
            render_context = nullptr;
        }

        render_window = nullptr;
        render_update_requested.store(false);
    }

    bool Libmpv::render(int framebuffer_id, int width, int height, bool flip_y) {
        if(!render_context)
            return set_error("libmpv render context has not been created");

        render_update_requested.store(false);

        mpv_opengl_fbo opengl_fbo = {
            .fbo = framebuffer_id,
            .w = width,
            .h = height,
            .internal_format = 0,
        };
        int flip_y_int = flip_y ? 1 : 0;

        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_OPENGL_FBO, &opengl_fbo },
            { MPV_RENDER_PARAM_FLIP_Y, &flip_y_int },
            { MPV_RENDER_PARAM_INVALID, nullptr }
        };

        if(!set_mpv_error(mpv_render_context_render_fn(render_context, params), "render libmpv frame"))
            return false;

        error.clear();
        return true;
    }

    bool Libmpv::update_render_context() {
        if(!render_context)
            return false;

        const uint64_t flags = mpv_render_context_update_fn(render_context);
        const bool update_needed = (flags & MPV_RENDER_UPDATE_FRAME) != 0;
        render_update_requested.store(update_needed);
        return update_needed;
    }

    bool Libmpv::load_file(const std::string &path) {
        if(!create_render_context())
            return false;

        const char *args[] = {
            "loadfile",
            path.c_str(),
            "replace",
            nullptr
        };

        file_loaded = false;
        return command_async(args);
    }

    bool Libmpv::set_pause(bool pause) {
        if(!initialize())
            return false;

        return set_mpv_error(mpv_set_property_string_fn(handle, "pause", pause ? "yes" : "no"), pause ? "pause playback" : "resume playback");
    }

    bool Libmpv::play() {
        return set_pause(false);
    }

    bool Libmpv::pause() {
        return set_pause(true);
    }

    bool Libmpv::seek_to_ms(int64_t position_ms, bool exact) {
        if(!initialize())
            return false;

        if(active_seek_command_userdata != 0) {
            pending_seek = true;
            pending_seek_position_ms = position_ms;
            pending_seek_exact = exact;
            return true;
        }

        return dispatch_seek_to_ms(position_ms, exact);
    }

    bool Libmpv::dispatch_seek_to_ms(int64_t position_ms, bool exact) {

        const std::string seconds_str = std::to_string((double)position_ms / 1000.0);
        const char *args[] = {
            "seek",
            seconds_str.c_str(),
            "absolute",
            exact ? "exact" : "keyframes",
            nullptr
        };
        const uint64_t reply_userdata = next_async_command_userdata++;
        if(!command_async(args, reply_userdata))
            return false;

        active_seek_command_userdata = reply_userdata;
        return true;
    }

    bool Libmpv::seek_relative_ms(int64_t offset_ms) {
        if(!initialize())
            return false;

        const std::string seconds_str = std::to_string((double)offset_ms / 1000.0);
        const char *args[] = {
            "seek",
            seconds_str.c_str(),
            "relative",
            "exact",
            nullptr
        };
        return command_async(args);
    }

    bool Libmpv::get_pause() const {
        return get_property_flag("pause", false);
    }

    int64_t Libmpv::get_position_ms() const {
        return (int64_t)std::llround(get_property_double("time-pos", 0.0) * 1000.0);
    }

    int64_t Libmpv::get_duration_ms() const {
        return (int64_t)std::llround(get_property_double("duration", 0.0) * 1000.0);
    }

    void Libmpv::process_events() {
        if(!handle)
            return;

        for(;;) {
            const mpv_event *event = mpv_wait_event_fn(handle, 0.0);
            if(!event || event->event_id == MPV_EVENT_NONE)
                break;

            handle_event_state(event);
        }
    }

    const mpv_event* Libmpv::wait_event(double timeout_seconds) {
        if(!handle)
            return nullptr;

        const mpv_event *event = mpv_wait_event_fn(handle, timeout_seconds);
        handle_event_state(event);
        return event;
    }

    void Libmpv::set_wakeup_callback(std::function<void()> callback) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        wakeup_handler = std::move(callback);
    }

    void Libmpv::set_render_update_callback(std::function<void()> callback) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        render_update_handler = std::move(callback);
    }

    bool Libmpv::load_library() {
        if(is_available())
            return true;

        static const char *library_names[] = {
            "libmpv.so.2",
            "libmpv.so",
            nullptr
        };

        for(int i = 0; library_names[i]; ++i) {
            if(!library.load(library_names[i]))
                continue;

            if(load_symbols())
                return true;

            library.close();
        }

        if(error.empty())
            error = "failed to load libmpv shared library";
        return false;
    }

    bool Libmpv::load_symbols() {
        return load_symbol(library, mpv_error_string_fn, "mpv_error_string", error) &&
               load_symbol(library, mpv_free_fn, "mpv_free", error) &&
               load_symbol(library, mpv_create_fn, "mpv_create", error) &&
               load_symbol(library, mpv_initialize_fn, "mpv_initialize", error) &&
               load_symbol(library, mpv_destroy_fn, "mpv_destroy", error) &&
               load_symbol(library, mpv_set_option_string_fn, "mpv_set_option_string", error) &&
               load_symbol(library, mpv_command_fn, "mpv_command", error) &&
               load_symbol(library, mpv_command_async_fn, "mpv_command_async", error) &&
               load_symbol(library, mpv_set_property_string_fn, "mpv_set_property_string", error) &&
               load_symbol(library, mpv_get_property_fn, "mpv_get_property", error) &&
               load_symbol(library, mpv_get_property_string_fn, "mpv_get_property_string", error) &&
               load_symbol(library, mpv_wait_event_fn, "mpv_wait_event", error) &&
               load_symbol(library, mpv_set_wakeup_callback_fn, "mpv_set_wakeup_callback", error) &&
               load_symbol(library, mpv_render_context_create_fn, "mpv_render_context_create", error) &&
               load_symbol(library, mpv_render_context_set_update_callback_fn, "mpv_render_context_set_update_callback", error) &&
               load_symbol(library, mpv_render_context_update_fn, "mpv_render_context_update", error) &&
               load_symbol(library, mpv_render_context_render_fn, "mpv_render_context_render", error) &&
               load_symbol(library, mpv_render_context_free_fn, "mpv_render_context_free", error);
    }

    bool Libmpv::can_destroy_render_context() const {
        if(!render_context)
            return true;

        if(!mgl_is_connected_to_display_server())
            return false;

        mgl_context *context = mgl_get_context();
        if(!context)
            return false;

        return context->current_window && context->current_window == render_window;
    }

    void Libmpv::handle_event_state(const mpv_event *event) {
        if(!event)
            return;

        switch(event->event_id) {
            case MPV_EVENT_SHUTDOWN:
                shutdown = true;
                break;
            case MPV_EVENT_COMMAND_REPLY:
                if(event->reply_userdata == active_seek_command_userdata) {
                    active_seek_command_userdata = 0;
                    if(pending_seek) {
                        const int64_t position_ms = pending_seek_position_ms;
                        const bool exact = pending_seek_exact;
                        pending_seek = false;
                        dispatch_seek_to_ms(position_ms, exact);
                    }
                }
                break;
            case MPV_EVENT_FILE_LOADED:
                file_loaded = true;
                break;
            case MPV_EVENT_END_FILE: {
                const auto *end_file = static_cast<const mpv_event_end_file*>(event->data);
                if(end_file && end_file->reason == MPV_END_FILE_REASON_ERROR) {
                    file_loaded = false;
                    const char *error_str = mpv_error_string_fn ? mpv_error_string_fn(end_file->error) : "unknown error";
                    error = std::string("failed to load video: ") + error_str;
                }
                break;
            }
            default:
                break;
        }
    }

    bool Libmpv::set_option_string(const char *name, const char *value) {
        return set_mpv_error(mpv_set_option_string_fn(handle, name, value), std::string("set libmpv option '") + name + "'");
    }

    bool Libmpv::command(const char **args) {
        return set_mpv_error(mpv_command_fn(handle, args), std::string("run libmpv command '") + (args && args[0] ? args[0] : "") + "'");
    }

    bool Libmpv::command_async(const char **args, uint64_t reply_userdata) {
        return set_mpv_error(mpv_command_async_fn(handle, reply_userdata, args), std::string("run async libmpv command '") + (args && args[0] ? args[0] : "") + "'");
    }

    bool Libmpv::set_error(const std::string &error_message) {
        error = error_message;
        return false;
    }

    bool Libmpv::set_mpv_error(int error_code, const std::string &action) {
        if(error_code >= 0) {
            error.clear();
            return true;
        }

        const char *error_str = mpv_error_string_fn ? mpv_error_string_fn(error_code) : "unknown error";
        error = std::string("failed to ") + action + ": " + error_str;
        return false;
    }

    bool Libmpv::get_property_flag(const char *name, bool default_value) const {
        if(!handle)
            return default_value;

        int value = default_value ? 1 : 0;
        if(mpv_get_property_fn(handle, name, MPV_FORMAT_FLAG, &value) < 0)
            return default_value;
        return value != 0;
    }

    double Libmpv::get_property_double(const char *name, double default_value) const {
        if(!handle)
            return default_value;

        double value = default_value;
        if(mpv_get_property_fn(handle, name, MPV_FORMAT_DOUBLE, &value) < 0)
            return default_value;
        return value;
    }

    void Libmpv::wakeup_callback(void *userdata) {
        auto *self = static_cast<Libmpv*>(userdata);
        if(!self)
            return;

        std::function<void()> handler;
        {
            std::lock_guard<std::mutex> lock(self->callback_mutex);
            handler = self->wakeup_handler;
        }

        if(handler)
            handler();
    }

    void Libmpv::render_update_callback(void *userdata) {
        auto *self = static_cast<Libmpv*>(userdata);
        if(!self)
            return;

        self->render_update_requested.store(true);

        std::function<void()> handler;
        {
            std::lock_guard<std::mutex> lock(self->callback_mutex);
            handler = self->render_update_handler;
        }

        if(handler)
            handler();
    }

    void* Libmpv::get_proc_address(void *ctx, const char *name) {
        auto *context = static_cast<mgl_context*>(ctx);
        if(!context || !name)
            return nullptr;

        if(context->gl.eglGetProcAddress) {
            void *proc = (void*)context->gl.eglGetProcAddress(name);
            if(proc)
                return proc;
        }

        if(context->gl.glXGetProcAddress) {
            void *proc = (void*)context->gl.glXGetProcAddress((const unsigned char*)name);
            if(proc)
                return proc;
        }

        if(context->gl.gl_library) {
            void *proc = dlsym(context->gl.gl_library, name);
            if(proc)
                return proc;
        }

        if(context->gl.glx_library) {
            void *proc = dlsym(context->gl.glx_library, name);
            if(proc)
                return proc;
        }

        if(context->gl.egl_library) {
            void *proc = dlsym(context->gl.egl_library, name);
            if(proc)
                return proc;
        }

        return nullptr;
    }
}
