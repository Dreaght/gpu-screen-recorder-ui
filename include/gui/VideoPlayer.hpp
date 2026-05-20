#pragma once

#include "Widget.hpp"
#include "../Libmpv.hpp"

#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/system/Clock.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace gsr {
    class VideoPlayer : public Widget {
    public:
        enum class PreviewSource {
            ORIGINAL_SLOW,
            PROXY_FAST,
        };

        VideoPlayer(mgl::vec2f size, std::string video_path = "", PreviewSource preview_source = PreviewSource::ORIGINAL_SLOW);
        VideoPlayer(const VideoPlayer&) = delete;
        VideoPlayer& operator=(const VideoPlayer&) = delete;
        ~VideoPlayer() override;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        void set_size(mgl::vec2f size);

        void set_video_path(std::string video_path);
        const std::string& get_video_path() const;
        void set_preview_source(PreviewSource preview_source);
        PreviewSource get_preview_source() const;
        void set_seekbar_enabled(bool enabled);
        bool is_seekbar_enabled() const;
        const std::string& get_proxy_video_path() const;
        void set_seek_state_callback(std::function<void(int64_t position_ms, int64_t duration_ms, bool paused)> callback);
        void begin_external_scrub();
        void update_external_scrub(int64_t position_ms);
        void end_external_scrub(bool resume_playback);

        bool is_backend_available() const;
        bool is_file_loaded() const;
        bool is_paused() const;

        bool play();
        bool pause();
        bool toggle_pause();
        bool seek_to_ms(int64_t position_ms, bool exact = true);

        int64_t get_position_ms() const;
        int64_t get_duration_ms() const;
    private:
        void ensure_video_loaded();
        void queue_proxy_generation();
        void process_proxy_generation_result();
        bool ensure_render_target(mgl::Window &window, mgl::vec2f item_size);
        void destroy_render_target();
        void process_pending_seek_display_state();
        void proxy_worker_loop();
        void refresh_cached_player_state();
        void notify_seek_state_changed();
        void update_status_text();
        void update_drag_seek(mgl::vec2f draw_pos, mgl::vec2f item_size, mgl::vec2f mouse_pos);
        void draw_video_surface(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size);
        void draw_overlay_controls(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, bool controls_visible);
        mgl::FloatRect get_seekbar_hitbox(mgl::vec2f draw_pos, mgl::vec2f item_size) const;
        mgl::FloatRect get_play_pause_hitbox(mgl::vec2f draw_pos, mgl::vec2f item_size) const;
        bool controls_visible(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size) const;
    private:
        Libmpv libmpv;
        mgl::vec2f size;
        PreviewSource preview_source;
        std::string video_path;
        std::string loaded_video_path;
        std::string pending_video_path;
        std::string proxy_video_path;
        mgl::Texture video_texture;
        mgl::Sprite video_sprite;
        mgl::Text status_text;
        bool seekbar_enabled = true;
        bool dragging_seekbar = false;
        bool dragging_seekbar_resume_on_release = false;
        bool dragging_seek_position_valid = false;
        int64_t dragging_seek_position_ms = 0;
        bool displayed_seek_position_valid = false;
        int64_t displayed_seek_position_ms = 0;
        double displayed_seek_position_timer = 0.0;
        mgl::Clock drag_seek_dispatch_clock;
        bool video_texture_has_content = false;
        std::atomic_bool cached_pause { true };
        std::atomic_bool cached_eof_reached { false };
        std::atomic_bool cached_file_loaded { false };
        std::atomic<int64_t> cached_duration_ms { 0 };
        std::atomic<int64_t> cached_position_ms { 0 };
        std::atomic_bool render_update_pending { false };
        std::thread proxy_worker_thread;
        std::mutex proxy_mutex;
        std::condition_variable proxy_cv;
        bool stop_proxy_worker = false;
        bool pending_proxy_request = false;
        bool proxy_ready = false;
        bool proxy_failed = false;
        std::string pending_proxy_source_path;
        std::string ready_proxy_path;
        uint64_t pending_proxy_generation = 0;
        uint64_t ready_proxy_generation = 0;
        uint64_t video_generation = 0;
        std::function<void(int64_t position_ms, int64_t duration_ms, bool paused)> seek_state_callback;
        int64_t last_notified_position_ms = -1;
        int64_t last_notified_duration_ms = -1;
        bool last_notified_pause_state = true;
        unsigned int video_texture_id = 0;
        unsigned int video_framebuffer_id = 0;
        mgl::vec2i render_target_size = {0, 0};
    };
}
