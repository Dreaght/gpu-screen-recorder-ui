#pragma once

#include "Widget.hpp"

#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gsr {
    class TimelineWidget : public Widget {
    public:
        explicit TimelineWidget(mgl::vec2f size);
        TimelineWidget(const TimelineWidget&) = delete;
        TimelineWidget& operator=(const TimelineWidget&) = delete;
        ~TimelineWidget() override;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        void set_size(mgl::vec2f size);

        void set_source_video_path(std::string path);
        void set_duration_ms(int64_t duration_ms);
        void set_position_ms(int64_t position_ms);
        int64_t get_position_ms() const;
        void set_paused(bool paused);
        float get_timeline_width() const;
        float get_pixels_per_ms() const;
        float position_ms_to_scroll(int64_t position_ms) const;
        int64_t scroll_to_position_ms(float scroll_x) const;
        bool take_zoom_changed();
        void set_zoom_interaction_region(mgl::vec2f offset, mgl::vec2f size);
    private:
        struct Thumbnail {
            int64_t start_ms = 0;
            int64_t end_ms = 0;
            std::string path;
            mgl::Texture texture;
            bool texture_loaded = false;
            bool texture_load_failed = false;
        };

        struct ThumbnailJobResult {
            uint64_t generation = 0;
            std::vector<Thumbnail> thumbnails;
        };

        void queue_thumbnail_generation();
        void process_thumbnail_generation_result();
        void thumbnail_worker_loop();
        void draw_background(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f visible_pos, mgl::vec2f visible_size, mgl::vec2f item_size) const;
        void draw_ticks(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, float visible_left, float visible_right);
        void draw_thumbnails(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, float visible_left, float visible_right);
        void draw_status(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size);
        int64_t clamp_position_ms(int64_t position) const;
        float get_pixels_per_second() const;
        float get_visible_duration_ms(float visible_width) const;
        double get_tick_step_ms(float visible_width) const;
    private:
        mgl::vec2f size;
        std::string source_video_path;
        int64_t duration_ms = 0;
        int64_t position_ms = 0;
        bool paused = true;
        float zoom = 1.0f;
        bool zoom_changed = false;
        mgl::vec2f zoom_interaction_offset = {0.0f, 0.0f};
        mgl::vec2f zoom_interaction_size = {0.0f, 0.0f};
        mgl::Text status_text;
        std::vector<Thumbnail> thumbnails;
        std::thread thumbnail_worker_thread;
        std::mutex thumbnail_mutex;
        std::condition_variable thumbnail_cv;
        bool stop_thumbnail_worker = false;
        bool pending_thumbnail_request = false;
        uint64_t pending_thumbnail_generation = 0;
        uint64_t ready_thumbnail_generation = 0;
        std::string pending_thumbnail_source_path;
        int64_t pending_thumbnail_duration_ms = 0;
        bool thumbnail_result_ready = false;
        ThumbnailJobResult ready_thumbnail_result;
    };
}
