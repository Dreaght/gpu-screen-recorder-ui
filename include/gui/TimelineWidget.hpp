#pragma once

#include "Widget.hpp"

#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace gsr {
    class TimelineWidget : public Widget {
    public:
        struct TimelineChunk {
            int64_t start_ms = 0;
            int64_t end_ms = 0;
            bool enabled = true;
        };

        struct ThumbnailCacheLockState {
            int fd = -1;
            std::string cache_dir;
        };

        explicit TimelineWidget(mgl::vec2f size);
        TimelineWidget(const TimelineWidget&) = delete;
        TimelineWidget& operator=(const TimelineWidget&) = delete;
        ~TimelineWidget() override;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        mgl::vec2f get_inner_size() override;
        void set_size(mgl::vec2f size);

        void set_source_video_path(std::string path);
        void set_duration_ms(int64_t duration_ms);
        void set_position_ms(int64_t position_ms);
        int64_t get_position_ms() const;
        void set_paused(bool paused);
        mgl::vec2f get_content_offset() const;
        float get_timeline_width() const;
        float get_pixels_per_ms() const;
        float position_ms_to_scroll(int64_t position_ms) const;
        int64_t scroll_to_position_ms(float scroll_x) const;
        bool take_zoom_changed();
        void set_zoom_interaction_region(mgl::vec2f offset, mgl::vec2f size);

        const std::vector<TimelineChunk>& get_chunks() const;
        void set_chunks(std::vector<TimelineChunk> new_chunks);
        void set_cut_point_proximity_ms(int64_t ms);
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
            std::string cache_dir;
            std::vector<Thumbnail> thumbnails;
        };

        void queue_thumbnail_generation();
        void process_thumbnail_generation_result();
        void thumbnail_worker_loop();
        void refresh_thumbnail_cache_locks();
        void refresh_displayed_thumbnail_cache_dirs();
        bool has_thumbnail_cache_lock(const std::string &cache_dir) const;
        static void purge_timeline_thumbnail_cache();
        struct LayoutRects {
            mgl::vec2f outer_pos;
            mgl::vec2f outer_size;
            mgl::vec2f content_pos;
            mgl::vec2f content_size;
            mgl::vec2f footer_pos;
            mgl::vec2f footer_size;
        };
        LayoutRects get_layout(mgl::vec2f draw_pos, mgl::vec2f item_size) const;
        void draw_background(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f visible_pos, mgl::vec2f visible_size, mgl::vec2f item_size) const;
        void draw_ticks(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, float visible_left, float visible_right);
        void draw_thumbnails(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, float visible_left, float visible_right);
        void draw_status(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size);
        void draw_cut_points(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, float visible_left, float visible_right) const;
        void draw_chunk_overlay(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, float visible_left, float visible_right) const;
        int64_t clamp_position_ms(int64_t position) const;
        int64_t get_cut_point_proximity_ms() const;
        int64_t get_cut_point_hit_proximity_ms() const;
        float get_pixels_per_second() const;
        float get_visible_duration_ms(float visible_width) const;
        double get_tick_step_ms(float visible_width) const;
        void ensure_chunks();
        int find_chunk_at_ms(int64_t ms) const;
        void split_chunk_at(int64_t ms);
        bool try_remove_cut_point_at(int64_t ms);
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
        std::vector<TimelineChunk> chunks;
        int64_t cut_point_proximity_ms = 500;
        std::vector<Thumbnail> thumbnails;
        std::thread thumbnail_worker_thread;
        std::mutex thumbnail_mutex;
        std::condition_variable thumbnail_cv;
        bool stop_thumbnail_worker = false;
        bool pending_thumbnail_request = false;
        uint64_t pending_thumbnail_generation = 0;
        uint64_t ready_thumbnail_generation = 0;
        std::string pending_thumbnail_source_path;
        std::string pending_thumbnail_cache_dir;
        std::string generating_thumbnail_cache_dir;
        int64_t pending_thumbnail_duration_ms = 0;
        bool thumbnail_result_ready = false;
        ThumbnailJobResult ready_thumbnail_result;
        std::map<std::string, ThumbnailCacheLockState> thumbnail_cache_locks;
        std::unordered_set<std::string> displayed_thumbnail_cache_dirs;
    };
}
