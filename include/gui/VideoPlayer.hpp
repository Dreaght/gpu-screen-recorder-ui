#pragma once

#include "Widget.hpp"
#include "../Libmpv.hpp"

#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/graphics/Text.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <cstdint>

namespace gsr {
    class VideoPlayer : public Widget {
    public:
        VideoPlayer(mgl::vec2f size, std::string video_path = "");
        VideoPlayer(const VideoPlayer&) = delete;
        VideoPlayer& operator=(const VideoPlayer&) = delete;
        ~VideoPlayer() override;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        void set_size(mgl::vec2f size);

        void set_video_path(std::string video_path);
        const std::string& get_video_path() const;

        bool is_backend_available() const;
        bool is_file_loaded() const;
        bool is_paused() const;

        bool play();
        bool pause();
        bool toggle_pause();
        bool seek_to_ms(int64_t position_ms);

        int64_t get_position_ms() const;
        int64_t get_duration_ms() const;
    private:
        void ensure_video_loaded();
        bool ensure_render_target(mgl::Window &window, mgl::vec2f item_size);
        void destroy_render_target();
        void process_pending_seek_display_state();
        void queue_seek_request(int64_t position_ms, bool exact);
        void queue_play_request(bool restart_from_beginning);
        void queue_pause_request();
        void seek_worker_loop();
        void refresh_cached_player_state();
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
        std::string video_path;
        std::string loaded_video_path;
        std::string pending_video_path;
        mgl::Texture video_texture;
        mgl::Sprite video_sprite;
        mgl::Text status_text;
        bool dragging_seekbar = false;
        bool dragging_seek_position_valid = false;
        int64_t dragging_seek_position_ms = 0;
        bool displayed_seek_position_valid = false;
        int64_t displayed_seek_position_ms = 0;
        double displayed_seek_position_timer = 0.0;
        bool video_texture_has_content = false;
        std::atomic_bool cached_pause { true };
        std::atomic_bool cached_eof_reached { false };
        std::atomic_bool cached_file_loaded { false };
        std::atomic<int64_t> cached_duration_ms { 0 };
        std::atomic<int64_t> cached_position_ms { 0 };
        std::atomic_bool render_update_pending { false };
        std::thread seek_worker_thread;
        std::mutex seek_mutex;
        std::condition_variable seek_cv;
        bool stop_seek_worker = false;
        bool wakeup_pending = false;
        bool pending_seek_request = false;
        bool pending_seek_exact = false;
        int64_t pending_seek_position_ms = 0;
        uint64_t pending_seek_generation = 0;
        uint64_t video_generation = 0;
        bool pending_play_request = false;
        bool pending_play_restart_from_beginning = false;
        bool pending_pause_request = false;
        unsigned int video_texture_id = 0;
        unsigned int video_framebuffer_id = 0;
        mgl::vec2i render_target_size = {0, 0};
    };
}
