#pragma once

#include "Widget.hpp"
#include "../Libmpv.hpp"

#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/graphics/Text.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace gsr {
    struct GsrInfo;

    class VideoPlayer : public Widget {
    public:
        struct PlaybackState {
            int64_t position_ms = 0;
            int64_t duration_ms = 0;
            bool paused = true;
            bool file_loaded = false;
            bool eof_reached = false;
        };

        enum class PreviewSource {
            ORIGINAL_SLOW,
            PROXY_FAST,
        };

        VideoPlayer(const GsrInfo *gsr_info, mgl::vec2f size, std::string video_path = "", PreviewSource preview_source = PreviewSource::ORIGINAL_SLOW);
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
        void set_smart_controls_hide_enabled(bool enabled);
        bool is_smart_controls_hide_enabled() const;
        const std::string& get_proxy_video_path() const;
        void set_playback_state_callback(std::function<void(const PlaybackState&)> callback);
        void set_before_play_callback(std::function<bool()> callback);
        void begin_external_scrub();
        void update_external_scrub(int64_t position_ms);
        void end_external_scrub(bool resume_playback, bool exact_seek = false);
        void cancel_scrub(bool resume_playback = false, bool exact_seek = true);
        void request_redraw();

        bool is_backend_available() const;
        bool is_file_loaded() const;
        bool is_paused() const;
        bool is_external_scrubbing_active() const;

        bool play();
        bool resume_from_current_position();
        bool pause();
        bool toggle_pause();
        bool seek_to_ms(int64_t position_ms, bool exact = false);

        PlaybackState get_playback_state() const;
        int64_t get_position_ms() const;
        int64_t get_duration_ms() const;
    private:
        enum class ScrubOwner {
            Internal,
            External,
        };

        struct ScrubSession {
            ScrubOwner owner = ScrubOwner::Internal;
            bool resume_playback_on_release = false;
            int64_t position_ms = 0;
        };

        void ensure_video_loaded();
        void queue_proxy_generation();
        void process_proxy_generation_result();
        void refresh_proxy_lock();
        bool ensure_render_target(mgl::Window &window, mgl::vec2f item_size);
        void destroy_render_target();
        void proxy_worker_loop();
        void notify_playback_state_changed();
        void refresh_playback_state();
        void update_status_text();
        PlaybackState get_reported_playback_state() const;
        PlaybackState get_backend_playback_state() const;
        int64_t clamp_to_duration(int64_t position_ms) const;
        bool is_scrubbing() const;
        bool is_external_scrubbing() const;
        void begin_scrub(ScrubOwner owner);
        void update_scrub_position(int64_t position_ms, bool exact_seek);
        void end_scrub(bool resume_playback, bool exact_seek);
        void update_drag_seek(mgl::vec2f draw_pos, mgl::vec2f item_size, mgl::vec2f mouse_pos);
        void draw_video_surface(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size);
        void draw_overlay_controls(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, bool controls_visible);
        mgl::FloatRect get_seekbar_hitbox(mgl::vec2f draw_pos, mgl::vec2f item_size) const;
        mgl::FloatRect get_play_pause_hitbox(mgl::vec2f draw_pos, mgl::vec2f item_size) const;
        bool controls_visible(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size) const;
        bool is_mouse_near_hitbox(mgl::Window &window, const mgl::FloatRect &hitbox, float proximity_padding) const;
    private:
        const GsrInfo *gsr_info = nullptr;
        Libmpv libmpv;
        mgl::vec2f size;
        PreviewSource preview_source;
        std::string video_path;
        std::string loaded_video_path;
        std::string handoff_proxy_path;
        std::string pending_video_path;
        std::string proxy_video_path;
        mgl::Texture video_texture;
        mgl::Sprite video_sprite;
        mgl::Text status_text;
        bool seekbar_enabled = true;
        bool smart_controls_hide_enabled = false;
        PlaybackState playback_state;
        std::optional<ScrubSession> scrub_session;
        bool video_texture_has_content = false;
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
        std::function<void(const PlaybackState&)> playback_state_callback;
        std::function<bool()> before_play_callback;
        PlaybackState last_notified_playback_state;
        bool has_notified_playback_state = false;
        unsigned int video_texture_id = 0;
        unsigned int video_framebuffer_id = 0;
        mgl::vec2i render_target_size = {0, 0};
    };
}
