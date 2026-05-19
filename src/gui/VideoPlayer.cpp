#include "../../include/gui/VideoPlayer.hpp"
#include "../../include/Process.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/graphics/Image.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/system/FloatRect.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/window/Window.hpp>

extern "C" {
#include <mgl/mgl.h>
}

#include <algorithm>
#include <dlfcn.h>
#include <GL/gl.h>

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

        static const int64_t seek_display_settle_threshold_ms = 120;
        static const double seek_display_settle_timeout_seconds = 0.35;
        static const int64_t thumbnail_interval_ms = 1000;
        static const int thumbnail_request_radius = 3;
        static const int thumbnail_width = 224;
        static const float thumbnail_popup_width_scale = 0.22f;
        static const float thumbnail_popup_max_width = 260.0f;
        static const float thumbnail_popup_min_width = 140.0f;
        static const float thumbnail_popup_bottom_spacing_scale = 0.02f;

        static const float ui_scale = 2.0f;
        static const float seeker_horizontal_padding_scale = 0.018f * ui_scale;
        static const float seeker_vertical_padding_scale = 0.018f * ui_scale;
        static const float seeker_height_scale = 0.005f * ui_scale;
        static const float seeker_hitbox_padding_scale = 0.012f * ui_scale;

        static const float overlay_darkness_alpha = 110.0f;

        static const float center_button_size_scale = 0.065f * ui_scale;
        static const float center_button_min_size = 46.0f * ui_scale;
        static const float center_button_max_size = 88.0f * ui_scale;

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

    VideoPlayer::VideoPlayer(mgl::vec2f size, std::string video_path) :
        size(size),
        video_path(std::move(video_path)),
        video_sprite(&video_texture),
        status_text("", get_theme().body_font_desc.c_str())
    {
        libmpv.set_render_update_callback([this]() {
            render_update_pending.store(true);
        });
        thumbnail_worker_thread = std::thread([this]() { thumbnail_worker_loop(); });
        refresh_cached_player_state();
        update_status_text();
    }

    VideoPlayer::~VideoPlayer() {
        libmpv.set_render_update_callback(nullptr);

        {
            std::lock_guard<std::mutex> lock(thumbnail_mutex);
            stop_thumbnail_worker = true;
        }
        thumbnail_cv.notify_one();
        if(thumbnail_worker_thread.joinable())
            thumbnail_worker_thread.join();

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

            if(get_seekbar_hitbox(draw_pos, item_size).contains(mouse_pos)) {
                dragging_seekbar = true;
                dragging_seek_position_valid = false;
                set_widget_as_selected_in_parent();
                update_drag_seek(draw_pos, item_size, mouse_pos);
                return false;
            }

            if(get_play_pause_hitbox(draw_pos, item_size).contains(mouse_pos)) {
                toggle_pause();
                return false;
            }
        }

        if(event.type == mgl::Event::MouseButtonReleased && event.mouse_button.button == mgl::Mouse::Left && dragging_seekbar) {
            if(dragging_seek_position_valid) {
                displayed_seek_position_valid = true;
                displayed_seek_position_ms = dragging_seek_position_ms;
                displayed_seek_position_timer = 0.0;
                libmpv.seek_to_ms(dragging_seek_position_ms, true);
            }
            dragging_seekbar = false;
            dragging_seek_position_valid = false;
            remove_widget_as_selected_in_parent();
            return false;
        }

        if(event.type == mgl::Event::MouseMoved && dragging_seekbar) {
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
        refresh_cached_player_state();
        process_pending_seek_display_state();
        process_ready_thumbnails();
        update_status_text();

        const mgl::vec2f draw_pos = (position + offset).floor();
        const mgl::vec2f item_size = get_size().floor();
        const bool show_controls = controls_visible(window, draw_pos, item_size);

        const mgl::Scissor prev_scissor = window.get_scissor();
        const mgl::Scissor scissor = scissor_get_sub_area(prev_scissor, { draw_pos.to_vec2i(), item_size.to_vec2i() });
        window.set_scissor(scissor);

        draw_video_surface(window, draw_pos, item_size);

        if(show_controls) {
            mgl::Rectangle overlay(item_size);
            overlay.set_position(draw_pos);
            overlay.set_color(mgl::Color(0, 0, 0, (int)overlay_darkness_alpha));
            window.draw(overlay);
        }

        if(!cached_file_loaded.load()) {
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
        ++thumbnail_generation;
        loaded_video_path.clear();
        pending_video_path.clear();
        displayed_seek_position_valid = false;
        dragging_seek_position_valid = false;
        {
            std::lock_guard<std::mutex> lock(thumbnail_mutex);
            thumbnail_request_queue.clear();
            thumbnails.clear();
        }

        if(this->video_path.empty())
            libmpv.clear_file();

        video_texture_has_content = false;

        update_status_text();
    }

    const std::string& VideoPlayer::get_video_path() const {
        return video_path;
    }

    bool VideoPlayer::is_backend_available() const {
        return libmpv.is_available();
    }

    bool VideoPlayer::is_file_loaded() const {
        return cached_file_loaded;
    }

    bool VideoPlayer::is_paused() const {
        return cached_pause;
    }

    bool VideoPlayer::play() {
        const bool should_restart_from_beginning = cached_eof_reached.load() && !displayed_seek_position_valid;
        if(should_restart_from_beginning)
            libmpv.seek_to_ms(0, true);

        return libmpv.play();
    }

    bool VideoPlayer::pause() {
        return libmpv.pause();
    }

    bool VideoPlayer::toggle_pause() {
        return cached_pause.load() ? play() : libmpv.pause();
    }

    bool VideoPlayer::seek_to_ms(int64_t position_ms) {
        return libmpv.seek_to_ms(position_ms, true);
    }

    int64_t VideoPlayer::get_position_ms() const {
        return cached_position_ms.load();
    }

    int64_t VideoPlayer::get_duration_ms() const {
        return cached_duration_ms.load();
    }

    void VideoPlayer::ensure_video_loaded() {
        if(!pending_video_path.empty() && cached_file_loaded.load()) {
            loaded_video_path = pending_video_path;
            pending_video_path.clear();
        } else if(!pending_video_path.empty() && !libmpv.get_error().empty()) {
            pending_video_path.clear();
        }

        if(video_path.empty() || video_path == loaded_video_path || pending_video_path == video_path || !libmpv.is_available())
            return;

        if(libmpv.load_file(video_path))
            pending_video_path = video_path;
        else
            pending_video_path.clear();
    }

    void VideoPlayer::update_status_text() {
        if(video_path.empty()) {
            status_text.set_string("No video selected");
        } else if(!libmpv.is_available()) {
            status_text.set_string("mpv is not installed");
        } else if(!libmpv.get_error().empty() && !cached_file_loaded.load()) {
            status_text.set_string(libmpv.get_error());
        } else if(!pending_video_path.empty()) {
            status_text.set_string("Loading video...");
        } else if(!cached_file_loaded.load()) {
            status_text.set_string("Loading video...");
        } else {
            status_text.set_string("");
        }
    }

    void VideoPlayer::update_drag_seek(mgl::vec2f draw_pos, mgl::vec2f item_size, mgl::vec2f mouse_pos) {
        const int64_t duration_ms = cached_duration_ms.load();
        if(duration_ms <= 0) {
            dragging_seek_position_valid = false;
            return;
        }

        const mgl::FloatRect seekbar_hitbox = get_seekbar_hitbox(draw_pos, item_size);
        const float relative_x = clamp_float((mouse_pos.x - seekbar_hitbox.position.x) / seekbar_hitbox.size.x, 0.0f, 1.0f);
        dragging_seek_position_ms = std::min<int64_t>((int64_t)(relative_x * (double)duration_ms), std::max<int64_t>(0, duration_ms - 1));
        dragging_seek_position_valid = true;
        displayed_seek_position_valid = true;
        displayed_seek_position_ms = dragging_seek_position_ms;
        displayed_seek_position_timer = 0.0;
        queue_thumbnail_requests(dragging_seek_position_ms);
    }

    void VideoPlayer::process_pending_seek_display_state() {
        if(displayed_seek_position_valid && !dragging_seekbar) {
            displayed_seek_position_timer += get_frame_delta_seconds();
            const int64_t diff_ms = std::llabs(cached_position_ms.load() - displayed_seek_position_ms);
            if(diff_ms <= seek_display_settle_threshold_ms || displayed_seek_position_timer >= seek_display_settle_timeout_seconds)
                displayed_seek_position_valid = false;
        }
    }

    void VideoPlayer::refresh_cached_player_state() {
        if(dragging_seekbar)
            return;

        cached_file_loaded.store(libmpv.is_file_loaded());
        cached_pause.store(libmpv.get_pause());
        cached_eof_reached.store(libmpv.is_eof_reached());

        cached_duration_ms.store(libmpv.get_duration_ms());
        cached_position_ms.store(libmpv.get_position_ms());
    }

    void VideoPlayer::queue_thumbnail_requests(int64_t position_ms) {
        const int64_t duration_ms = cached_duration_ms.load();
        const int64_t max_second = std::max<int64_t>(0, (duration_ms > 0 ? duration_ms - 1 : 0) / thumbnail_interval_ms);
        const int64_t center_second = std::min<int64_t>(std::max<int64_t>(0, position_ms / thumbnail_interval_ms), max_second);
        static const int progressive_steps[] = {10, 5, 3, 1};

        std::lock_guard<std::mutex> lock(thumbnail_mutex);
        auto enqueue_second = [this](int64_t second) {
            if(second < 0)
                return;

            auto it = thumbnails.find(second);
            if(it != thumbnails.end()) {
                if(it->second.state == ThumbnailEntry::State::QUEUED ||
                   it->second.state == ThumbnailEntry::State::READY_CPU ||
                   it->second.state == ThumbnailEntry::State::READY_GPU)
                    return;
            }

            ThumbnailEntry &entry = thumbnails[second];
            entry.state = ThumbnailEntry::State::QUEUED;
            if(std::find(thumbnail_request_queue.begin(), thumbnail_request_queue.end(), second) == thumbnail_request_queue.end())
                thumbnail_request_queue.push_back(second);
        };

        enqueue_second(center_second);

        for(int step : progressive_steps) {
            for(int offset = 1; offset <= thumbnail_request_radius; ++offset) {
                enqueue_second(center_second - (int64_t)offset * step);
                enqueue_second(center_second + (int64_t)offset * step);
            }
        }

        thumbnail_cv.notify_one();
    }

    void VideoPlayer::process_ready_thumbnails() {
        std::lock_guard<std::mutex> lock(thumbnail_mutex);
        for(auto &it : thumbnails) {
            ThumbnailEntry &entry = it.second;
            if(entry.state != ThumbnailEntry::State::READY_CPU)
                continue;

            mgl::Image image;
            if(!image.load_from_memory((const unsigned char*)entry.encoded_image.data(), entry.encoded_image.size())) {
                entry.state = ThumbnailEntry::State::FAILED;
                entry.encoded_image.clear();
                continue;
            }

            auto texture = std::make_unique<mgl::Texture>();
            if(!texture->load_from_image(image)) {
                entry.state = ThumbnailEntry::State::FAILED;
                entry.encoded_image.clear();
                continue;
            }

            entry.texture = std::move(texture);
            entry.encoded_image.clear();
            entry.state = ThumbnailEntry::State::READY_GPU;
        }
    }

    void VideoPlayer::thumbnail_worker_loop() {
        for(;;) {
            int64_t second = -1;
            uint64_t generation = 0;
            std::string path;
            {
                std::unique_lock<std::mutex> lock(thumbnail_mutex);
                thumbnail_cv.wait(lock, [this]() { return stop_thumbnail_worker || !thumbnail_request_queue.empty(); });
                if(stop_thumbnail_worker)
                    break;

                second = thumbnail_request_queue.front();
                thumbnail_request_queue.erase(thumbnail_request_queue.begin());
                generation = thumbnail_generation;
                path = video_path;
            }

            if(path.empty())
                continue;

            const std::string second_str = std::to_string((double)second);
            const std::string scale_str = "scale=" + std::to_string(thumbnail_width) + ":-2";
            const char *args[] = {
                "ffmpeg",
                "-loglevel", "error",
                "-ss", second_str.c_str(),
                "-i", path.c_str(),
                "-frames:v", "1",
                "-vf", scale_str.c_str(),
                "-pix_fmt", "yuvj420p",
                "-f", "image2pipe",
                "-vcodec", "mjpeg",
                "-q:v", "5",
                "-",
                nullptr
            };

            std::string output;
            const int exit_status = exec_program_on_host_get_stdout(args, output, false);

            std::lock_guard<std::mutex> lock(thumbnail_mutex);
            if(generation != thumbnail_generation)
                continue;

            auto it = thumbnails.find(second);
            if(it == thumbnails.end())
                continue;

            if(exit_status == 0 && !output.empty()) {
                it->second.encoded_image = std::move(output);
                it->second.state = ThumbnailEntry::State::READY_CPU;
            } else {
                it->second.state = ThumbnailEntry::State::FAILED;
            }
        }
    }

    void VideoPlayer::draw_video_surface(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size) {
        mgl::Rectangle background(item_size);
        background.set_position(draw_pos);
        background.set_color(mgl::Color(8, 10, 12));
        window.draw(background);

        if(!libmpv.is_available() || !cached_file_loaded.load())
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
        if(!controls_visible)
            return;

        const mgl::FloatRect play_pause_rect = get_play_pause_hitbox(draw_pos, item_size);
        const bool play_pause_hovered = play_pause_rect.contains(window.get_mouse_position().to_vec2f());

        mgl::Rectangle play_pause_bg(play_pause_rect.size);
        play_pause_bg.set_position(play_pause_rect.position);
        play_pause_bg.set_color(play_pause_hovered ? mgl::Color(0, 0, 0, 80) : mgl::Color(0, 0, 0, 50));
        window.draw(play_pause_bg);
        // draw_rectangle_outline(window, play_pause_rect.position, play_pause_rect.size, mgl::Color(0, 0, 0, 80), std::max(1.0f, get_theme().window_height * 0.0014f));

        mgl::Sprite play_pause_icon(cached_pause.load() ? &get_theme().play_texture : &get_theme().pause_texture);
        play_pause_icon.set_height(play_pause_rect.size.y * 0.42f);
        play_pause_icon.set_position((play_pause_rect.position + play_pause_rect.size * 0.5f - play_pause_icon.get_size() * 0.5f).floor());
        play_pause_icon.set_color(mgl::Color(255, 255, 255, 255));
        window.draw(play_pause_icon);

        const mgl::FloatRect seekbar_hitbox = get_seekbar_hitbox(draw_pos, item_size);
        const float seeker_height = std::max(2.0f, item_size.y * seeker_height_scale);
        const mgl::vec2f seeker_pos = mgl::vec2f(seekbar_hitbox.position.x, seekbar_hitbox.position.y + seekbar_hitbox.size.y * 0.5f - seeker_height * 0.5f).floor();
        const mgl::vec2f seeker_size(seekbar_hitbox.size.x, seeker_height);
        const int64_t duration_ms = cached_duration_ms.load();
        const int64_t position_ms = (dragging_seekbar && dragging_seek_position_valid)
            ? dragging_seek_position_ms
            : (displayed_seek_position_valid ? displayed_seek_position_ms : cached_position_ms.load());
        const bool using_displayed_scrub_position = !dragging_seekbar && displayed_seek_position_valid;
        const float progress = duration_ms > 0
            ? ((cached_eof_reached.load() && !dragging_seekbar && !using_displayed_scrub_position)
                ? 1.0f
                : clamp_float((float)((double)position_ms / (double)duration_ms), 0.0f, 1.0f))
            : 0.0f;

        mgl::Rectangle seeker_track(seeker_size);
        seeker_track.set_position(seeker_pos);
        seeker_track.set_color(mgl::Color(255, 255, 255, 70));
        window.draw(seeker_track);

        mgl::Rectangle seeker_fill({ seeker_size.x * progress, seeker_size.y });
        seeker_fill.set_position(seeker_pos);
        seeker_fill.set_color(get_color_theme().tint_color);
        window.draw(seeker_fill);

        if(dragging_seekbar && dragging_seek_position_valid) {
            const int64_t duration_ms_limit = cached_duration_ms.load();
            const int64_t max_second = std::max<int64_t>(0, (duration_ms_limit > 0 ? duration_ms_limit - 1 : 0) / thumbnail_interval_ms);
            const int64_t preview_second = std::min<int64_t>(std::max<int64_t>(0, dragging_seek_position_ms / thumbnail_interval_ms), max_second);
            std::lock_guard<std::mutex> lock(thumbnail_mutex);
            auto it = thumbnails.find(preview_second);
            if(it != thumbnails.end() && it->second.state == ThumbnailEntry::State::READY_GPU && it->second.texture) {
                mgl::Sprite thumbnail_sprite(it->second.texture.get());
                const float popup_width = clamp_float(item_size.x * thumbnail_popup_width_scale, thumbnail_popup_min_width, thumbnail_popup_max_width);
                thumbnail_sprite.set_width(popup_width);
                const mgl::vec2f popup_size = thumbnail_sprite.get_size();
                const float popup_x = clamp_float(window.get_mouse_position().x - popup_size.x * 0.5f, draw_pos.x, draw_pos.x + item_size.x - popup_size.x);
                const float popup_y = seekbar_hitbox.position.y - popup_size.y - item_size.y * thumbnail_popup_bottom_spacing_scale;

                mgl::Rectangle popup_bg(popup_size);
                popup_bg.set_position({ popup_x, popup_y });
                popup_bg.set_color(mgl::Color(0, 0, 0, 220));
                window.draw(popup_bg);

                thumbnail_sprite.set_position({ popup_x, popup_y });
                window.draw(thumbnail_sprite);
                draw_rectangle_outline(window, { popup_x, popup_y }, popup_size, mgl::Color(255, 255, 255, 70), std::max(1.0f, get_theme().window_height * 0.0012f));
            }
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
        return dragging_seekbar || mgl::FloatRect(draw_pos, item_size).contains(window.get_mouse_position().to_vec2f());
    }
}
