#include "../../include/gui/VideoPlayer.hpp"
#include "../../include/Process.hpp"
#include "../../include/Utils.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/system/FloatRect.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/window/Window.hpp>

#include "include/GsrInfo.hpp"

extern "C" {
#include <mgl/mgl.h>
}

#include <dlfcn.h>
#include <GL/gl.h>
#include <sys/stat.h>

#include <algorithm>

namespace gsr {
    namespace {
        using FUNC_glPushAttrib = void (*)(unsigned int);
        using FUNC_glPopAttrib = void (*)(void);
        using FUNC_glPushClientAttrib = void (*)(unsigned int);
        using FUNC_glPopClientAttrib = void (*)(void);
        using FUNC_glGenFramebuffers = void (*)(int, unsigned int*);
        using FUNC_glDeleteFramebuffers = void (*)(int, const unsigned int*);
        using FUNC_glBindFramebuffer = void (*)(unsigned int, unsigned int);
        using FUNC_glFramebufferTexture2D = void (*)(unsigned int, unsigned int, unsigned int, unsigned int, int);
        using FUNC_glCheckFramebufferStatus = unsigned int (*)(unsigned int);

        static const float ui_scale = 2.0f;
        static const float seeker_horizontal_padding_scale = 0.018f * ui_scale;
        static const float seeker_vertical_padding_scale = 0.018f * ui_scale;
        static const float seeker_height_scale = 0.005f * ui_scale;
        static const float seeker_hitbox_padding_scale = 0.012f * ui_scale;

        static const float overlay_darkness_alpha = 110.0f;

        static const float center_button_size_scale = 0.065f * ui_scale;
        static const float center_button_min_size = 46.0f * ui_scale;
        static const float center_button_max_size = 88.0f * ui_scale;
        static const float control_proximity_padding_scale = 0.045f * ui_scale;
        static const float control_proximity_min_padding = 28.0f;

        static std::string build_proxy_video_path(const std::string &video_path) {
            struct stat st;
            std::string key = video_path;
            if(stat(video_path.c_str(), &st) == 0)
                key += ":" + std::to_string((int64_t)st.st_mtim.tv_sec) + ":" + std::to_string((int64_t)st.st_size);

            std::string proxy_dir = get_cache_dir() + "/video-proxies";
            char proxy_dir_buffer[4096];
            snprintf(proxy_dir_buffer, sizeof(proxy_dir_buffer), "%s", proxy_dir.c_str());
            create_directory_recursive(proxy_dir_buffer);
            return proxy_dir + "/" + std::to_string(std::hash<std::string>{}(key)) + ".mkv";
        }

        static float clamp_float(float value, float min_value, float max_value) {
            if(value < min_value)
                return min_value;
            if(value > max_value)
                return max_value;
            return value;
        }

        static void* get_gl_symbol(mgl_context *context, const char *name) {
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

    VideoPlayer::VideoPlayer(const GsrInfo *gsr_info, mgl::vec2f size, std::string video_path, PreviewSource preview_source) :
        gsr_info(gsr_info),
        size(size),
        preview_source(preview_source),
        video_path(std::move(video_path)),
        video_sprite(&video_texture),
        status_text("", get_theme().body_font_desc.c_str())
    {
        libmpv.set_render_update_callback([this]() {
            render_update_pending.store(true);
        });
        proxy_worker_thread = std::thread([this]() { proxy_worker_loop(); });
        if(this->preview_source == PreviewSource::PROXY_FAST && !this->video_path.empty())
            queue_proxy_generation();
        refresh_playback_state();
        update_status_text();
    }

    VideoPlayer::~VideoPlayer() {
        libmpv.set_render_update_callback(nullptr);

        {
            std::lock_guard<std::mutex> lock(proxy_mutex);
            stop_proxy_worker = true;
        }
        proxy_cv.notify_one();
        if(proxy_worker_thread.joinable())
            proxy_worker_thread.join();

        destroy_render_target();
    }

    bool VideoPlayer::ensure_render_target(mgl::Window&, mgl::vec2f item_size) {
        mgl_context *context = mgl_get_context();
        if(!context)
            return false;

        const mgl::vec2i desired_size(std::max(1, (int)item_size.x), std::max(1, (int)item_size.y));
        if(video_texture_id != 0 && video_framebuffer_id != 0 && render_target_size.x == desired_size.x && render_target_size.y == desired_size.y)
            return true;

        destroy_render_target();

        const auto glGenFramebuffersFn = (FUNC_glGenFramebuffers)get_gl_symbol(context, "glGenFramebuffers");
        const auto glDeleteFramebuffersFn = (FUNC_glDeleteFramebuffers)get_gl_symbol(context, "glDeleteFramebuffers");
        const auto glBindFramebufferFn = (FUNC_glBindFramebuffer)get_gl_symbol(context, "glBindFramebuffer");
        const auto glFramebufferTexture2DFn = (FUNC_glFramebufferTexture2D)get_gl_symbol(context, "glFramebufferTexture2D");
        const auto glCheckFramebufferStatusFn = (FUNC_glCheckFramebufferStatus)get_gl_symbol(context, "glCheckFramebufferStatus");
        if(!glGenFramebuffersFn || !glDeleteFramebuffersFn || !glBindFramebufferFn || !glFramebufferTexture2DFn || !glCheckFramebufferStatusFn)
            return false;

        context->gl.glGenTextures(1, &video_texture_id);
        if(video_texture_id == 0)
            return false;

        context->gl.glBindTexture(GL_TEXTURE_2D, video_texture_id);
        context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        context->gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        context->gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, desired_size.x, desired_size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        context->gl.glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffersFn(1, &video_framebuffer_id);
        if(video_framebuffer_id == 0) {
            context->gl.glDeleteTextures(1, &video_texture_id);
            video_texture_id = 0;
            return false;
        }

        glBindFramebufferFn(GL_FRAMEBUFFER, video_framebuffer_id);
        glFramebufferTexture2DFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, video_texture_id, 0);
        const unsigned int framebuffer_status = glCheckFramebufferStatusFn(GL_FRAMEBUFFER);
        glBindFramebufferFn(GL_FRAMEBUFFER, 0);
        if(framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffersFn(1, &video_framebuffer_id);
            video_framebuffer_id = 0;
            context->gl.glDeleteTextures(1, &video_texture_id);
            video_texture_id = 0;
            return false;
        }

        video_texture = mgl::Texture(video_texture_id, MGL_TEXTURE_FORMAT_RGBA);
        video_sprite.set_texture(&video_texture);
        render_target_size = desired_size;
        render_update_pending.store(true);
        video_texture_has_content = false;
        return true;
    }

    void VideoPlayer::destroy_render_target() {
        mgl_context *context = mgl_get_context();
        if(!context)
            return;

        const auto glDeleteFramebuffersFn = (FUNC_glDeleteFramebuffers)get_gl_symbol(context, "glDeleteFramebuffers");
        if(video_framebuffer_id != 0 && glDeleteFramebuffersFn)
            glDeleteFramebuffersFn(1, &video_framebuffer_id);

        video_framebuffer_id = 0;

        if(video_texture_id != 0)
            context->gl.glDeleteTextures(1, &video_texture_id);

        video_texture_id = 0;
        render_target_size = {0, 0};
        video_texture_has_content = false;
        video_texture = mgl::Texture();
        video_sprite.set_texture(&video_texture);
    }

    bool VideoPlayer::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(!visible)
            return true;

        const mgl::vec2f draw_pos = position + offset;
        const mgl::vec2f item_size = get_size().floor();
        const mgl::FloatRect bounds(draw_pos, item_size);

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const mgl::vec2f mouse_pos((float)event.mouse_button.x, (float)event.mouse_button.y);
            if(!bounds.contains(mouse_pos))
                return true;

            if(seekbar_enabled && get_seekbar_hitbox(draw_pos, item_size).contains(mouse_pos)) {
                begin_scrub(ScrubOwner::Internal);
                set_widget_as_selected_in_parent();
                update_drag_seek(draw_pos, item_size, mouse_pos);
                return false;
            }

            if(get_play_pause_hitbox(draw_pos, item_size).contains(mouse_pos)) {
                toggle_pause();
                return false;
            }
        }

        if(event.type == mgl::Event::MouseButtonReleased && event.mouse_button.button == mgl::Mouse::Left && is_scrubbing() && !is_external_scrubbing()) {
            end_scrub(scrub_session->resume_playback_on_release, false);
            remove_widget_as_selected_in_parent();
            return false;
        }

        if(event.type == mgl::Event::MouseMoved && is_scrubbing() && !is_external_scrubbing()) {
            update_drag_seek(draw_pos, item_size, { (float)event.mouse_move.x, (float)event.mouse_move.y });
            return false;
        }

        return true;
    }

    void VideoPlayer::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        ensure_video_loaded();
        libmpv.process_events();
        refresh_playback_state();
        update_status_text();

        const mgl::vec2f draw_pos = (position + offset).floor();
        const mgl::vec2f item_size = get_size().floor();
        const bool show_controls = controls_visible(window, draw_pos, item_size);

        const mgl::Scissor prev_scissor = window.get_scissor();
        const mgl::Scissor scissor = scissor_get_sub_area(prev_scissor, { draw_pos.to_vec2i(), item_size.to_vec2i() });
        window.set_scissor(scissor);

        draw_video_surface(window, draw_pos, item_size);

        if(show_controls && !is_smart_controls_hide_enabled()) {
            mgl::Rectangle overlay(item_size);
            overlay.set_position(draw_pos);
            overlay.set_color(mgl::Color(0, 0, 0, (int)overlay_darkness_alpha));
            window.draw(overlay);
        }

        if(!playback_state.file_loaded) {
            status_text.set_position((draw_pos + item_size * 0.5f - status_text.get_bounds().size * 0.5f).floor());
            window.draw(status_text);
        }

        draw_overlay_controls(window, draw_pos, item_size, show_controls);
        window.set_scissor(prev_scissor);
    }

    mgl::vec2f VideoPlayer::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size;
    }

    void VideoPlayer::set_size(mgl::vec2f size) {
        this->size = size;
    }

    void VideoPlayer::set_video_path(std::string video_path) {
        this->video_path = std::move(video_path);
        ++video_generation;
        loaded_video_path.clear();
        pending_video_path.clear();
        proxy_video_path.clear();
        playback_state = {};
        scrub_session.reset();
        {
            std::lock_guard<std::mutex> lock(proxy_mutex);
            pending_proxy_request = false;
            proxy_ready = false;
            proxy_failed = false;
            pending_proxy_source_path.clear();
            ready_proxy_path.clear();
            pending_proxy_generation = video_generation;
            ready_proxy_generation = 0;
        }

        if(this->video_path.empty())
            libmpv.clear_file();
        else if(preview_source == PreviewSource::PROXY_FAST)
            queue_proxy_generation();

        video_texture_has_content = false;

        update_status_text();
    }

    const std::string& VideoPlayer::get_video_path() const {
        return video_path;
    }

    void VideoPlayer::set_preview_source(PreviewSource preview_source) {
        if(this->preview_source == preview_source)
            return;

        this->preview_source = preview_source;
        set_video_path(video_path);
    }

    VideoPlayer::PreviewSource VideoPlayer::get_preview_source() const {
        return preview_source;
    }

    void VideoPlayer::set_seekbar_enabled(bool enabled) {
        seekbar_enabled = enabled;
        if(!seekbar_enabled) {
            scrub_session.reset();
        }
    }

    bool VideoPlayer::is_seekbar_enabled() const {
        return seekbar_enabled;
    }

    void VideoPlayer::set_smart_controls_hide_enabled(bool enabled) {
        smart_controls_hide_enabled = enabled;
    }

    bool VideoPlayer::is_smart_controls_hide_enabled() const {
        return smart_controls_hide_enabled;
    }

    const std::string& VideoPlayer::get_proxy_video_path() const {
        return proxy_video_path;
    }

    void VideoPlayer::set_playback_state_callback(std::function<void(const PlaybackState&)> callback) {
        playback_state_callback = std::move(callback);
        has_notified_playback_state = false;
        notify_playback_state_changed();
    }

    void VideoPlayer::set_before_play_callback(std::function<bool()> callback) {
        before_play_callback = std::move(callback);
    }

    void VideoPlayer::begin_external_scrub() {
        begin_scrub(ScrubOwner::External);
    }

    void VideoPlayer::update_external_scrub(int64_t position_ms) {
        if(!is_external_scrubbing())
            return;

        update_scrub_position(position_ms, true);
    }

    void VideoPlayer::end_external_scrub(bool resume_playback, bool exact_seek) {
        if(!is_external_scrubbing())
            return;

        end_scrub(resume_playback, exact_seek);
    }

    void VideoPlayer::cancel_scrub(bool resume_playback, bool exact_seek) {
        if(!scrub_session)
            return;

        const bool was_internal_scrub = scrub_session->owner == ScrubOwner::Internal;
        end_scrub(resume_playback, exact_seek);
        if(was_internal_scrub)
            remove_widget_as_selected_in_parent();
    }

    void VideoPlayer::request_redraw() {
        render_update_pending.store(true);
    }

    bool VideoPlayer::is_backend_available() const {
        return libmpv.is_available();
    }

    bool VideoPlayer::is_file_loaded() const {
        return playback_state.file_loaded;
    }

    bool VideoPlayer::is_paused() const {
        return playback_state.paused;
    }

    bool VideoPlayer::is_external_scrubbing_active() const {
        return is_external_scrubbing();
    }

    bool VideoPlayer::play() {
        if(before_play_callback && before_play_callback())
            return true;

        const bool should_restart_from_beginning = playback_state.eof_reached && !is_scrubbing();
        if(should_restart_from_beginning)
            libmpv.seek_to_ms(0, false);

        return libmpv.play();
    }

    bool VideoPlayer::resume_from_current_position() {
        playback_state.eof_reached = false;
        playback_state.paused = false;
        notify_playback_state_changed();
        return libmpv.play();
    }

    bool VideoPlayer::pause() {
        return libmpv.pause();
    }

    bool VideoPlayer::toggle_pause() {
        return playback_state.paused ? play() : pause();
    }

    bool VideoPlayer::seek_to_ms(int64_t position_ms, bool exact) {
        if(playback_state.duration_ms > 0)
            playback_state.position_ms = clamp_to_duration(position_ms);
        else
            playback_state.position_ms = std::max<int64_t>(0, position_ms);

        notify_playback_state_changed();
        return libmpv.seek_to_ms(playback_state.position_ms, exact);
    }

    VideoPlayer::PlaybackState VideoPlayer::get_playback_state() const {
        return get_reported_playback_state();
    }

    int64_t VideoPlayer::get_position_ms() const {
        return get_reported_playback_state().position_ms;
    }

    int64_t VideoPlayer::get_duration_ms() const {
        return playback_state.duration_ms;
    }

    void VideoPlayer::queue_proxy_generation() {
        std::lock_guard<std::mutex> lock(proxy_mutex);
        pending_proxy_request = true;
        proxy_ready = false;
        proxy_failed = false;
        pending_proxy_source_path = video_path;
        pending_proxy_generation = video_generation;
        proxy_cv.notify_one();
    }

    void VideoPlayer::process_proxy_generation_result() {
        std::lock_guard<std::mutex> lock(proxy_mutex);
        if(proxy_ready && ready_proxy_generation == video_generation)
            proxy_video_path = ready_proxy_path;
        else if(!proxy_ready)
            proxy_video_path.clear();
    }

    void VideoPlayer::ensure_video_loaded() {
        process_proxy_generation_result();

        if(!pending_video_path.empty() && playback_state.file_loaded) {
            loaded_video_path = pending_video_path;
            pending_video_path.clear();
        } else if(!pending_video_path.empty() && !libmpv.get_error().empty()) {
            pending_video_path.clear();
        }

        const std::string effective_video_path = preview_source == PreviewSource::PROXY_FAST ? proxy_video_path : video_path;

        if(effective_video_path.empty() || effective_video_path == loaded_video_path || pending_video_path == effective_video_path || !libmpv.is_available())
            return;

        if(libmpv.load_file(effective_video_path))
            pending_video_path = effective_video_path;
        else
            pending_video_path.clear();
    }

    void VideoPlayer::update_status_text() {
        if(video_path.empty()) {
            status_text.set_string("No video selected");
        } else if(!libmpv.is_available()) {
            status_text.set_string("mpv is not installed");
        } else if(preview_source == PreviewSource::PROXY_FAST && proxy_video_path.empty()) {
            status_text.set_string("Preparing fast preview...");
        } else if(!libmpv.get_error().empty() && !playback_state.file_loaded) {
            status_text.set_string(libmpv.get_error());
        } else if(!pending_video_path.empty()) {
            status_text.set_string("Loading video...");
        } else if(!playback_state.file_loaded) {
            status_text.set_string("Loading video...");
        } else {
            status_text.set_string("");
        }
    }

    void VideoPlayer::update_drag_seek(mgl::vec2f draw_pos, mgl::vec2f item_size, mgl::vec2f mouse_pos) {
        if(!is_scrubbing() || playback_state.duration_ms <= 0)
            return;

        const mgl::FloatRect seekbar_hitbox = get_seekbar_hitbox(draw_pos, item_size);
        const float relative_x = clamp_float((mouse_pos.x - seekbar_hitbox.position.x) / seekbar_hitbox.size.x, 0.0f, 1.0f);
        update_scrub_position((int64_t)(relative_x * (double)playback_state.duration_ms), false);
    }

    void VideoPlayer::refresh_playback_state() {
        if(is_scrubbing()) {
            playback_state.paused = true;
            playback_state.position_ms = clamp_to_duration(scrub_session->position_ms);
            notify_playback_state_changed();
            return;
        }

        const PlaybackState backend_state = get_backend_playback_state();
        playback_state = backend_state;
        notify_playback_state_changed();
    }

    void VideoPlayer::notify_playback_state_changed() {
        if(!playback_state_callback)
            return;

        const PlaybackState state = get_reported_playback_state();
        if(has_notified_playback_state
            && state.position_ms == last_notified_playback_state.position_ms
            && state.duration_ms == last_notified_playback_state.duration_ms
            && state.paused == last_notified_playback_state.paused
            && state.file_loaded == last_notified_playback_state.file_loaded
            && state.eof_reached == last_notified_playback_state.eof_reached)
            return;

        last_notified_playback_state = state;
        has_notified_playback_state = true;
        playback_state_callback(state);
    }

    VideoPlayer::PlaybackState VideoPlayer::get_reported_playback_state() const {
        PlaybackState state = playback_state;
        if(is_scrubbing()) {
            state.paused = true;
            state.position_ms = clamp_to_duration(scrub_session->position_ms);
        }
        return state;
    }

    VideoPlayer::PlaybackState VideoPlayer::get_backend_playback_state() const {
        PlaybackState state;
        state.position_ms = libmpv.get_position_ms();
        state.duration_ms = libmpv.get_duration_ms();
        state.paused = libmpv.get_pause();
        state.file_loaded = libmpv.is_file_loaded();
        state.eof_reached = libmpv.is_eof_reached();
        if(state.duration_ms > 0)
            state.position_ms = std::clamp<int64_t>(state.position_ms, 0, std::max<int64_t>(0, state.duration_ms - 1));
        else
            state.position_ms = std::max<int64_t>(0, state.position_ms);
        return state;
    }

    int64_t VideoPlayer::clamp_to_duration(int64_t position_ms) const {
        if(playback_state.duration_ms <= 0)
            return std::max<int64_t>(0, position_ms);

        return std::clamp<int64_t>(position_ms, 0, std::max<int64_t>(0, playback_state.duration_ms - 1));
    }

    bool VideoPlayer::is_scrubbing() const {
        return scrub_session.has_value();
    }

    bool VideoPlayer::is_external_scrubbing() const {
        return scrub_session && scrub_session->owner == ScrubOwner::External;
    }

    void VideoPlayer::begin_scrub(ScrubOwner owner) {
        ScrubSession session;
        session.owner = owner;
        session.resume_playback_on_release = !playback_state.paused;
        session.position_ms = playback_state.position_ms;
        scrub_session = session;
        libmpv.pause();
        playback_state.paused = true;
        playback_state.position_ms = session.position_ms;
        notify_playback_state_changed();
    }

    void VideoPlayer::update_scrub_position(int64_t position_ms, bool exact_seek) {
        if(!scrub_session)
            return;

        scrub_session->position_ms = clamp_to_duration(position_ms);
        playback_state.position_ms = scrub_session->position_ms;
        notify_playback_state_changed();
        libmpv.seek_to_ms(scrub_session->position_ms, exact_seek);
    }

    void VideoPlayer::end_scrub(bool resume_playback, bool exact_seek) {
        if(!scrub_session)
            return;

        const int64_t final_position_ms = clamp_to_duration(scrub_session->position_ms);
        scrub_session.reset();
        seek_to_ms(final_position_ms, exact_seek);
        if(resume_playback)
            play();
    }

    // TODO: Please tweak this for performance/quality/size and compatibility
    std::vector<std::string> get_best_encoder_args(const GsrInfo& gsr_info) {
        const auto& codecs = gsr_info.supported_video_codecs;

        if (gsr_info.gpu_info.vendor == GpuVendor::NVIDIA && codecs.h264) {
            return {
                // Optimized for small file size, probably requires good GPU.
                // 10-20x smaller size than source video
                "-vf", "scale=1280:-2,fps=30",
                "-c:v", "h264_nvenc",
                "-preset", "p6",
                "-rc", "vbr",
                "-cq", "26",
                "-b:v", "0",
                "-g", "60",
                "-bf", "2",
                "-b_ref_mode", "middle",
                "-rc-lookahead", "20",
            };
        }

        if (codecs.h264_vulkan) {
            return {
                "-init_hw_device", "vulkan=vkdev:0",
                "-filter_hw_device", "vkdev",
                "-vf", "scale=1280:-2,fps=30,format=nv12,hwupload",
                "-c:v", "h264_vulkan",
                "-g", "15"
            };
        }

        if (codecs.h264 && !gsr_info.gpu_info.card_path.empty()) {
            // VA-API for AMD / Intel
            return {
                "-vaapi_device", gsr_info.gpu_info.card_path,
                "-vf", "scale=1280:-2,fps=30,format=nv12,hwupload",
                "-c:v", "h264_vaapi",
                "-g", "15"
            };
        }

        return {
            "-vf", "scale=1280:-2,fps=30",
            "-c:v", "libx264",
            "-preset", "ultrafast",
            "-tune", "zerolatency",
            "-g", "15",
            "-crf", "26"
        };
    }

    void VideoPlayer::proxy_worker_loop() {
        for(;;) {
            std::string source_path;
            uint64_t generation = 0;
            {
                std::unique_lock<std::mutex> lock(proxy_mutex);
                proxy_cv.wait(lock, [this]() { return stop_proxy_worker || pending_proxy_request; });
                if(stop_proxy_worker)
                    break;

                source_path = pending_proxy_source_path;
                generation = pending_proxy_generation;
                pending_proxy_request = false;
            }

            if(source_path.empty())
                continue;

            const std::string output_path = build_proxy_video_path(source_path);
            struct stat st;
            bool success = stat(output_path.c_str(), &st) == 0 && st.st_size > 0;

            if(!success) {
                std::vector<std::string> args_str = {
                    "ffmpeg", "-loglevel", "error", "-y",
                    "-i", source_path,
                    "-an"
                };

                std::vector<std::string> encoder_args = get_best_encoder_args(*gsr_info);
                args_str.insert(args_str.end(), encoder_args.begin(), encoder_args.end());

                args_str.push_back(output_path);

                std::vector<const char*> args;
                args.reserve(args_str.size() + 1);
                for (const auto& arg : args_str) {
                    args.push_back(arg.c_str());
                }
                args.push_back(nullptr);

                std::string ffmpeg_output;
                success = exec_program_on_host_get_stdout(args.data(), ffmpeg_output, false) == 0;
            }

            std::lock_guard<std::mutex> lock(proxy_mutex);
            if(generation != video_generation)
                continue;

            proxy_failed = !success;
            proxy_ready = success;
            ready_proxy_generation = generation;
            ready_proxy_path = success ? output_path : std::string();
        }
    }

    void VideoPlayer::draw_video_surface(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size) {
        mgl::Rectangle background(item_size);
        background.set_position(draw_pos);
        background.set_color(mgl::Color(8, 10, 12));
        window.draw(background);

        if(!libmpv.is_available() || !playback_state.file_loaded)
            return;

        if(!ensure_render_target(window, item_size))
            return;

        if(render_update_pending.exchange(false)) {
            mgl_context *context = mgl_get_context();
            if(!context)
                return;

            const mgl::View prev_view = window.get_view();
            const mgl::Scissor prev_scissor = window.get_scissor();
            const auto glPushAttribFn = (FUNC_glPushAttrib)get_gl_symbol(context, "glPushAttrib");
            const auto glPopAttribFn = (FUNC_glPopAttrib)get_gl_symbol(context, "glPopAttrib");
            const auto glPushClientAttribFn = (FUNC_glPushClientAttrib)get_gl_symbol(context, "glPushClientAttrib");
            const auto glPopClientAttribFn = (FUNC_glPopClientAttrib)get_gl_symbol(context, "glPopClientAttrib");
            const auto glBindFramebufferFn = (FUNC_glBindFramebuffer)get_gl_symbol(context, "glBindFramebuffer");
            const bool have_full_gl_state_stack = glPushAttribFn && glPopAttribFn && glPushClientAttribFn && glPopClientAttribFn;
            if(!glBindFramebufferFn)
                return;

            int previous_framebuffer = 0;
            context->gl.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);

            if(have_full_gl_state_stack) {
                glPushAttribFn(GL_ALL_ATTRIB_BITS);
                glPushClientAttribFn(GL_CLIENT_ALL_ATTRIB_BITS);
            }

            context->gl.glMatrixMode(GL_TEXTURE);
            context->gl.glPushMatrix();
            context->gl.glMatrixMode(GL_PROJECTION);
            context->gl.glPushMatrix();
            context->gl.glMatrixMode(GL_MODELVIEW);
            context->gl.glPushMatrix();

            glBindFramebufferFn(GL_FRAMEBUFFER, video_framebuffer_id);
            context->gl.glViewport(0, 0, render_target_size.x, render_target_size.y);
            context->gl.glScissor(0, 0, render_target_size.x, render_target_size.y);
            libmpv.render((int)video_framebuffer_id, render_target_size.x, render_target_size.y, false);
            glBindFramebufferFn(GL_FRAMEBUFFER, (unsigned int)previous_framebuffer);

            context->gl.glMatrixMode(GL_MODELVIEW);
            context->gl.glPopMatrix();
            context->gl.glMatrixMode(GL_PROJECTION);
            context->gl.glPopMatrix();
            context->gl.glMatrixMode(GL_TEXTURE);
            context->gl.glPopMatrix();
            context->gl.glMatrixMode(GL_MODELVIEW);

            if(have_full_gl_state_stack) {
                glPopClientAttribFn();
                glPopAttribFn();
            } else {
                context->gl.glEnable(GL_TEXTURE_2D);
                context->gl.glEnable(GL_BLEND);
                context->gl.glEnable(GL_SCISSOR_TEST);
                context->gl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                context->gl.glEnableClientState(GL_VERTEX_ARRAY);
                context->gl.glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                context->gl.glEnableClientState(GL_COLOR_ARRAY);
                context->gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
                context->gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            }

            window.set_view(prev_view);
            window.set_scissor(prev_scissor);
            video_texture_has_content = true;
        }

        if(!video_texture_has_content)
            return;

        video_sprite.set_position(draw_pos);
        video_sprite.set_size(item_size);
        window.draw(video_sprite);
    }

    void VideoPlayer::draw_overlay_controls(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, bool controls_visible) {
        if(!controls_visible && !is_smart_controls_hide_enabled())
            return;

        const float proximity_padding = std::max(control_proximity_min_padding, std::min(item_size.x, item_size.y) * control_proximity_padding_scale);
        const bool smart_hide_enabled = is_smart_controls_hide_enabled();
        const mgl::FloatRect play_pause_rect = get_play_pause_hitbox(draw_pos, item_size);
        const bool show_play_pause = !smart_hide_enabled || is_mouse_near_hitbox(window, play_pause_rect, proximity_padding);
        const bool show_seekbar = seekbar_enabled && (!smart_hide_enabled || is_scrubbing() || is_mouse_near_hitbox(window, get_seekbar_hitbox(draw_pos, item_size), proximity_padding));

        if(!show_play_pause && !show_seekbar)
            return;

        const bool play_pause_hovered = play_pause_rect.contains(window.get_mouse_position().to_vec2f());

        if(show_play_pause) {
            mgl::Rectangle play_pause_bg(play_pause_rect.size);
            play_pause_bg.set_position(play_pause_rect.position);
            play_pause_bg.set_color(play_pause_hovered ? mgl::Color(0, 0, 0, 80) : mgl::Color(0, 0, 0, 50));
            window.draw(play_pause_bg);
            // draw_rectangle_outline(window, play_pause_rect.position, play_pause_rect.size, mgl::Color(0, 0, 0, 80), std::max(1.0f, get_theme().window_height * 0.0014f));

            mgl::Sprite play_pause_icon(playback_state.paused ? &get_theme().play_texture : &get_theme().pause_texture);
            play_pause_icon.set_height(play_pause_rect.size.y * 0.42f);
            play_pause_icon.set_position((play_pause_rect.position + play_pause_rect.size * 0.5f - play_pause_icon.get_size() * 0.5f).floor());
            play_pause_icon.set_color(mgl::Color(255, 255, 255, 255));
            window.draw(play_pause_icon);
        }

        if(show_seekbar) {
            const mgl::FloatRect seekbar_hitbox = get_seekbar_hitbox(draw_pos, item_size);
            const float seeker_height = std::max(2.0f, item_size.y * seeker_height_scale);
            const mgl::vec2f seeker_pos = mgl::vec2f(seekbar_hitbox.position.x, seekbar_hitbox.position.y + seekbar_hitbox.size.y * 0.5f - seeker_height * 0.5f).floor();
            const mgl::vec2f seeker_size(seekbar_hitbox.size.x, seeker_height);
            const float progress = playback_state.duration_ms > 0
                ? ((playback_state.eof_reached && !is_scrubbing())
                    ? 1.0f
                    : clamp_float((float)((double)playback_state.position_ms / (double)playback_state.duration_ms), 0.0f, 1.0f))
                : 0.0f;

            mgl::Rectangle seeker_track(seeker_size);
            seeker_track.set_position(seeker_pos);
            seeker_track.set_color(mgl::Color(255, 255, 255, 70));
            window.draw(seeker_track);

            mgl::Rectangle seeker_fill({ seeker_size.x * progress, seeker_size.y });
            seeker_fill.set_position(seeker_pos);
            seeker_fill.set_color(get_color_theme().tint_color);
            window.draw(seeker_fill);
        }

    }

    mgl::FloatRect VideoPlayer::get_seekbar_hitbox(mgl::vec2f draw_pos, mgl::vec2f item_size) const {
        const float padding_x = std::max(8.0f, item_size.x * seeker_horizontal_padding_scale);
        const float padding_y = std::max(10.0f, item_size.y * seeker_vertical_padding_scale);
        const float hitbox_height = std::max(18.0f, item_size.y * seeker_hitbox_padding_scale);
        return {
            draw_pos + mgl::vec2f(padding_x, item_size.y - padding_y - hitbox_height),
            mgl::vec2f(std::max(1.0f, item_size.x - padding_x * 2.0f), hitbox_height)
        };
    }

    mgl::FloatRect VideoPlayer::get_play_pause_hitbox(mgl::vec2f draw_pos, mgl::vec2f item_size) const {
        const float box_size = clamp_float(item_size.y * center_button_size_scale, center_button_min_size, center_button_max_size);
        return {
            (draw_pos + item_size * 0.5f - mgl::vec2f(box_size, box_size) * 0.5f).floor(),
            mgl::vec2f(box_size, box_size).floor()
        };
    }

    bool VideoPlayer::controls_visible(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size) const {
        return mgl::FloatRect(draw_pos, item_size).contains(window.get_mouse_position().to_vec2f());
    }

    bool VideoPlayer::is_mouse_near_hitbox(mgl::Window &window, const mgl::FloatRect &hitbox, float proximity_padding) const {
        const mgl::vec2f mouse_pos = window.get_mouse_position().to_vec2f();
        const mgl::FloatRect expanded_hitbox(
            hitbox.position - mgl::vec2f(proximity_padding, proximity_padding),
            hitbox.size + mgl::vec2f(proximity_padding * 2.0f, proximity_padding * 2.0f)
        );
        return expanded_hitbox.contains(mouse_pos);
    }
}
