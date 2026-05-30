#include "../../include/gui/TimelineWidget.hpp"
#include "../../include/Process.hpp"
#include "../../include/Theme.hpp"
#include "../../include/Utils.hpp"
#include "../../include/gui/Utils.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/window/Window.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <sys/stat.h>

namespace gsr {
    namespace {
        static const float min_zoom = 0.35f;
        static const float default_zoom = 1.0f;
        static const float max_zoom = 24.0f;
        static const float base_pixels_per_second = 80.0f;
        static const float zoom_speed = 1.18f;
        static const int64_t min_cut_point_proximity_ms = 50;
        static const int64_t max_cut_point_proximity_ms = 1000;
        static const float footer_height_ratio = 0.24f;
        static const float frame_thickness_ratio = 0.045f;
        static const float well_inset_ratio = 0.018f;
        static const int min_thumbnail_count = 18;
        static const int max_thumbnail_count = 180;
        static const int max_thumbnail_loads_per_frame = 2;
        static const double tick_steps_ms[] = {
            100.0, 250.0, 500.0,
            1000.0, 2000.0, 5000.0,
            10000.0, 15000.0, 30000.0,
            60000.0, 120000.0, 300000.0,
            600000.0, 900000.0, 1800000.0,
        };

        static std::string format_timecode(int64_t position_ms) {
            const int64_t total_seconds = std::max<int64_t>(0, position_ms / 1000);
            const int64_t hours = total_seconds / 3600;
            const int64_t minutes = (total_seconds / 60) % 60;
            const int64_t seconds = total_seconds % 60;

            char buffer[64];
            if(hours > 0)
                snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld", (long long)hours, (long long)minutes, (long long)seconds);
            else
                snprintf(buffer, sizeof(buffer), "%02lld:%02lld", (long long)minutes, (long long)seconds);
            return buffer;
        }

        static std::string get_timeline_cache_dir(const std::string &video_path, int64_t duration_ms) {
            struct stat st;
            std::string key = video_path + ":" + std::to_string(duration_ms);
            if(stat(video_path.c_str(), &st) == 0)
                key += ":" + std::to_string((int64_t)st.st_mtim.tv_sec) + ":" + std::to_string((int64_t)st.st_size);

            const std::string cache_dir = get_cache_dir() + "/timeline-thumbnails/" + std::to_string(std::hash<std::string>{}(key));
            char cache_dir_buffer[4096];
            snprintf(cache_dir_buffer, sizeof(cache_dir_buffer), "%s", cache_dir.c_str());
            create_directory_recursive(cache_dir_buffer);
            return cache_dir;
        }

        static int get_target_thumbnail_count(int64_t duration_ms) {
            const double duration_seconds = std::max(1.0, duration_ms / 1000.0);
            return std::clamp((int)std::round(duration_seconds * 1.5), min_thumbnail_count, max_thumbnail_count);
        }

        static void draw_filled_rect(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size, mgl::Color color) {
            if(size.x <= 0.0f || size.y <= 0.0f)
                return;

            mgl::Rectangle rect(size.floor());
            rect.set_position(pos.floor());
            rect.set_color(color);
            window.draw(rect);
        }
    }

    TimelineWidget::TimelineWidget(mgl::vec2f size) :
        size(size),
        status_text("Preparing timeline...", get_theme().body_font_desc.c_str())
    {
        thumbnail_worker_thread = std::thread([this]() { thumbnail_worker_loop(); });
    }

    TimelineWidget::~TimelineWidget() {
        {
            std::lock_guard<std::mutex> lock(thumbnail_mutex);
            stop_thumbnail_worker = true;
        }
        thumbnail_cv.notify_one();
        if(thumbnail_worker_thread.joinable())
            thumbnail_worker_thread.join();
    }

    bool TimelineWidget::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(!visible)
            return true;

        const mgl::vec2f draw_pos = position + offset;
        const mgl::vec2f item_size = get_size().floor();
        const mgl::vec2f interaction_size = zoom_interaction_size.x > 0.0f && zoom_interaction_size.y > 0.0f
            ? zoom_interaction_size.floor()
            : item_size;
        const mgl::vec2f interaction_pos = zoom_interaction_size.x > 0.0f && zoom_interaction_size.y > 0.0f
            ? (draw_pos + zoom_interaction_offset).floor()
            : draw_pos;
        const mgl::FloatRect bounds(interaction_pos, interaction_size);

        if(event.type == mgl::Event::MouseWheelScrolled) {
            const mgl::vec2f mouse_pos((float)event.mouse_wheel_scroll.x, (float)event.mouse_wheel_scroll.y);
            if(!bounds.contains(mouse_pos))
                return true;

            if(event.mouse_wheel_scroll.key_states.control) {
                if(event.mouse_wheel_scroll.delta > 0)
                    zoom = std::min(max_zoom, zoom * std::pow(zoom_speed, (float)event.mouse_wheel_scroll.delta));
                else if(event.mouse_wheel_scroll.delta < 0)
                    zoom = std::max(min_zoom, zoom / std::pow(zoom_speed, (float)-event.mouse_wheel_scroll.delta));
                zoom_changed = true;
                return false;
            }
        }

        if(event.type == mgl::Event::MouseButtonPressed) {
            const mgl::vec2f mouse_pos((float)event.mouse_button.x, (float)event.mouse_button.y);
            if(!bounds.contains(mouse_pos))
                return true;

            if(event.mouse_button.button == mgl::Mouse::Left) {
                ensure_chunks();
                if(chunks.empty())
                    return true;

                const float relative_x = mouse_pos.x - draw_pos.x;
                const int64_t click_ms = clamp_position_ms((int64_t)std::llround(relative_x / get_pixels_per_ms()));

                const int idx = find_chunk_at_ms(click_ms);
                if(idx >= 0) {
                    chunks[idx].enabled = !chunks[idx].enabled;
                    return false;
                }
            }

            if(event.mouse_button.button == mgl::Mouse::Right) {
                ensure_chunks();
                if(chunks.empty())
                    return true;

                if(try_remove_cut_point_at(position_ms))
                    return false;

                split_chunk_at(position_ms);
                return false;
            }
        }

        return true;
    }

    void TimelineWidget::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        process_thumbnail_generation_result();

        const mgl::vec2f draw_pos = (position + offset).floor();
        const mgl::vec2f item_size = get_size().floor();
        const mgl::Scissor scissor = window.get_scissor();
        const float visible_left = std::max(draw_pos.x, (float)scissor.position.x);
        const float visible_top = std::max(draw_pos.y, (float)scissor.position.y);
        const float visible_right = std::min(draw_pos.x + item_size.x, (float)(scissor.position.x + scissor.size.x));
        const float visible_bottom = std::min(draw_pos.y + item_size.y, (float)(scissor.position.y + scissor.size.y));
        if(visible_right <= visible_left || visible_bottom <= visible_top)
            return;

        draw_background(window, draw_pos, {visible_left, visible_top}, {visible_right - visible_left, visible_bottom - visible_top}, item_size);
        draw_thumbnails(window, draw_pos, item_size, visible_left, visible_right);
        draw_cut_points(window, draw_pos, item_size, visible_left, visible_right);
        draw_chunk_overlay(window, draw_pos, item_size, visible_left, visible_right);
        draw_ticks(window, draw_pos, item_size, visible_left, visible_right);
        draw_status(window, draw_pos, item_size);
    }

    mgl::vec2f TimelineWidget::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size;
    }

    mgl::vec2f TimelineWidget::get_inner_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const LayoutRects layout = get_layout({0.0f, 0.0f}, size.floor());
        return layout.content_size;
    }

    void TimelineWidget::set_size(mgl::vec2f size) {
        this->size = size;
    }

    void TimelineWidget::set_source_video_path(std::string path) {
        if(source_video_path == path)
            return;

        source_video_path = std::move(path);
        thumbnails.clear();
        queue_thumbnail_generation();
    }

    void TimelineWidget::set_duration_ms(int64_t duration_ms) {
        duration_ms = std::max<int64_t>(0, duration_ms);
        if(this->duration_ms == duration_ms)
            return;

        this->duration_ms = duration_ms;
        position_ms = clamp_position_ms(position_ms);
        thumbnails.clear();
        queue_thumbnail_generation();
    }

    void TimelineWidget::set_position_ms(int64_t position_ms) {
        this->position_ms = clamp_position_ms(position_ms);
    }

    int64_t TimelineWidget::get_position_ms() const {
        return position_ms;
    }

    bool TimelineWidget::take_zoom_changed() {
        const bool changed = zoom_changed;
        zoom_changed = false;
        return changed;
    }

    void TimelineWidget::set_zoom_interaction_region(mgl::vec2f offset, mgl::vec2f size) {
        zoom_interaction_offset = offset;
        zoom_interaction_size = size;
    }

    void TimelineWidget::set_paused(bool paused) {
        this->paused = paused;
    }

    mgl::vec2f TimelineWidget::get_content_offset() const {
        const LayoutRects layout = get_layout({0.0f, 0.0f}, size.floor());
        return layout.content_pos;
    }

    const std::vector<TimelineWidget::TimelineChunk>& TimelineWidget::get_chunks() const {
        return chunks;
    }

    void TimelineWidget::set_chunks(std::vector<TimelineChunk> new_chunks) {
        chunks = std::move(new_chunks);
    }

    void TimelineWidget::set_cut_point_proximity_ms(int64_t ms) {
        cut_point_proximity_ms = std::max<int64_t>(1, ms);
    }

    void TimelineWidget::ensure_chunks() {
        if(chunks.empty() && duration_ms > 0)
            chunks.push_back({0, duration_ms, true});
    }

    int TimelineWidget::find_chunk_at_ms(int64_t ms) const {
        if(chunks.empty())
            return (ms >= 0 && ms <= duration_ms) ? 0 : -1;

        for(size_t i = 0; i < chunks.size(); ++i) {
            if(ms >= chunks[i].start_ms && ms < chunks[i].end_ms)
                return (int)i;
        }
        return -1;
    }

    void TimelineWidget::split_chunk_at(int64_t ms) {
        ensure_chunks();
        const int idx = find_chunk_at_ms(ms);
        if(idx < 0)
            return;

        TimelineChunk &chunk = chunks[idx];
        if(ms <= chunk.start_ms || ms >= chunk.end_ms)
            return;

        TimelineChunk new_chunk;
        new_chunk.start_ms = ms;
        new_chunk.end_ms = chunk.end_ms;
        new_chunk.enabled = chunk.enabled;

        chunk.end_ms = ms;

        chunks.insert(chunks.begin() + idx + 1, new_chunk);
    }

    bool TimelineWidget::try_remove_cut_point_at(int64_t ms) {
        ensure_chunks();
        if(chunks.size() <= 1)
            return false;

        int best_idx = -1;
        int64_t best_dist = get_cut_point_hit_proximity_ms();

        for(size_t i = 0; i < chunks.size() - 1; ++i) {
            const int64_t dist = std::abs(chunks[i].end_ms - ms);
            if(dist <= best_dist) {
                best_dist = dist;
                best_idx = (int)i;
            }
        }

        if(best_idx < 0)
            return false;

        chunks[best_idx].enabled = chunks[best_idx].enabled || chunks[best_idx + 1].enabled;
        chunks[best_idx].end_ms = chunks[best_idx + 1].end_ms;
        chunks.erase(chunks.begin() + best_idx + 1);

        return true;
    }

    void TimelineWidget::queue_thumbnail_generation() {
        std::lock_guard<std::mutex> lock(thumbnail_mutex);
        ++pending_thumbnail_generation;
        pending_thumbnail_request = !source_video_path.empty() && duration_ms > 0;
        pending_thumbnail_source_path = source_video_path;
        pending_thumbnail_duration_ms = duration_ms;
        thumbnail_result_ready = false;
        thumbnail_cv.notify_one();
    }

    void TimelineWidget::process_thumbnail_generation_result() {
        std::lock_guard<std::mutex> lock(thumbnail_mutex);
        if(!thumbnail_result_ready || ready_thumbnail_generation != pending_thumbnail_generation)
            return;

        thumbnails = std::move(ready_thumbnail_result.thumbnails);
        thumbnail_result_ready = false;
    }

    void TimelineWidget::thumbnail_worker_loop() {
        for(;;) {
            std::string source_path;
            int64_t generation_duration_ms = 0;
            uint64_t generation = 0;
            {
                std::unique_lock<std::mutex> lock(thumbnail_mutex);
                thumbnail_cv.wait(lock, [this]() { return stop_thumbnail_worker || pending_thumbnail_request; });
                if(stop_thumbnail_worker)
                    break;

                source_path = pending_thumbnail_source_path;
                generation_duration_ms = pending_thumbnail_duration_ms;
                generation = pending_thumbnail_generation;
                pending_thumbnail_request = false;
            }

            if(source_path.empty() || generation_duration_ms <= 0)
                continue;

            ThumbnailJobResult result;
            result.generation = generation;

            const std::string cache_dir = get_timeline_cache_dir(source_path, generation_duration_ms);
            const int thumbnail_count = get_target_thumbnail_count(generation_duration_ms);
            std::vector<std::string> thumbnail_paths;
            thumbnail_paths.reserve(thumbnail_count);

            for(int i = 0; i < thumbnail_count; ++i) {
                char filename[64];
                snprintf(filename, sizeof(filename), "thumb-%05d.jpg", i + 1);
                thumbnail_paths.emplace_back(cache_dir + "/" + filename);
            }

            bool cache_ready = true;
            for(const std::string &thumbnail_path : thumbnail_paths) {
                if(!std::filesystem::exists(thumbnail_path)) {
                    cache_ready = false;
                    break;
                }
            }

            if(!cache_ready) {
                for(const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(cache_dir)) {
                    if(entry.is_regular_file())
                        std::filesystem::remove(entry.path());
                }

                const double duration_seconds = std::max(0.001, generation_duration_ms / 1000.0);
                const std::string fps_value = std::to_string((double)thumbnail_count / duration_seconds);
                const std::string vf_arg = "fps=" + fps_value + ",scale=240:-2";
                const std::string output_pattern = cache_dir + "/thumb-%05d.jpg";
                const char *args[] = {
                    "ffmpeg",
                    "-loglevel", "error",
                    "-y",
                    "-i", source_path.c_str(),
                    "-vf", vf_arg.c_str(),
                    "-q:v", "4",
                    output_pattern.c_str(),
                    nullptr
                };
                std::string ffmpeg_output;
                if(exec_program_on_host_get_stdout(args, ffmpeg_output, false) != 0)
                    thumbnail_paths.clear();
            }

            const size_t generated_thumbnail_count = std::count_if(thumbnail_paths.begin(), thumbnail_paths.end(), [](const std::string &thumbnail_path) {
                return std::filesystem::exists(thumbnail_path);
            });
            if(generated_thumbnail_count == 0)
                thumbnail_paths.clear();

            for(size_t i = 0; i < thumbnail_paths.size(); ++i) {
                if(!std::filesystem::exists(thumbnail_paths[i]))
                    continue;

                const double start_ratio = (double)i / (double)thumbnail_paths.size();
                const double end_ratio = (double)(i + 1) / (double)thumbnail_paths.size();
                Thumbnail thumbnail;
                thumbnail.start_ms = (int64_t)std::llround(start_ratio * generation_duration_ms);
                thumbnail.end_ms = (int64_t)std::llround(end_ratio * generation_duration_ms);
                thumbnail.path = thumbnail_paths[i];
                result.thumbnails.push_back(std::move(thumbnail));
            }

            std::lock_guard<std::mutex> lock(thumbnail_mutex);
            if(generation != pending_thumbnail_generation)
                continue;

            ready_thumbnail_generation = generation;
            ready_thumbnail_result = std::move(result);
            thumbnail_result_ready = true;
        }
    }

    TimelineWidget::LayoutRects TimelineWidget::get_layout(mgl::vec2f draw_pos, mgl::vec2f item_size) const {
        LayoutRects layout;
        layout.outer_pos = draw_pos.floor();
        layout.outer_size = item_size.floor();

        const float frame = std::max(3.0f, std::round(item_size.y * frame_thickness_ratio));
        const float footer_height = std::max(18.0f, std::round(item_size.y * footer_height_ratio));
        const float gutter = std::max(2.0f, std::round(item_size.y * well_inset_ratio));

        layout.footer_size = {
            std::max(1.0f, item_size.x - frame * 2.0f),
            std::max(1.0f, footer_height - frame)
        };
        layout.footer_pos = {
            draw_pos.x + frame,
            draw_pos.y + item_size.y - footer_height
        };

        layout.content_pos = {
            draw_pos.x + frame + gutter,
            draw_pos.y + frame + gutter
        };
        layout.content_size = {
            std::max(1.0f, item_size.x - (frame + gutter) * 2.0f),
            std::max(1.0f, item_size.y - footer_height - frame - gutter * 2.0f)
        };
        return layout;
    }

    void TimelineWidget::draw_background(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f visible_pos, mgl::vec2f visible_size, mgl::vec2f item_size) const {
        (void)visible_pos;
        (void)visible_size;

        const LayoutRects layout = get_layout(draw_pos, item_size);
        const float frame = layout.content_pos.x - layout.outer_pos.x;
        const float footer_top = layout.footer_pos.y;

        draw_filled_rect(window, {layout.content_pos.x, layout.outer_pos.y}, {layout.content_size.x, frame}, get_color_theme().tint_color);
        draw_filled_rect(window, {layout.content_pos.x, footer_top}, {layout.content_size.x, 1.0f}, get_color_theme().tint_color);

        const mgl::vec2f well_outer_pos = layout.content_pos - mgl::vec2f(1.0f, 1.0f);
        const mgl::vec2f well_outer_size = layout.content_size + mgl::vec2f(2.0f, 2.0f);
        draw_filled_rect(window, well_outer_pos, well_outer_size, mgl::Color(10, 12, 15));
        draw_filled_rect(window, layout.content_pos, layout.content_size, mgl::Color(28, 32, 38));
    }

    void TimelineWidget::draw_ticks(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, float visible_left, float visible_right) {
        if(duration_ms <= 0)
            return;

        const LayoutRects layout = get_layout(draw_pos, item_size);

        const double tick_step_ms = get_tick_step_ms(std::max(1.0f, visible_right - visible_left));
        const int64_t start_ms = clamp_position_ms((int64_t)std::floor((visible_left - draw_pos.x) / std::max(0.0001f, get_pixels_per_ms())));
        const int64_t end_ms = clamp_position_ms((int64_t)std::ceil((visible_right - draw_pos.x) / std::max(0.0001f, get_pixels_per_ms())));
        const int64_t first_tick_ms = (int64_t)(std::floor(start_ms / tick_step_ms) * tick_step_ms);

        for(int64_t tick_ms = first_tick_ms; tick_ms <= end_ms + (int64_t)tick_step_ms; tick_ms += (int64_t)tick_step_ms) {
            if(tick_ms < 0 || tick_ms > duration_ms)
                continue;

            const float tick_x = draw_pos.x + tick_ms * get_pixels_per_ms();
            if(tick_x < visible_left || tick_x > visible_right)
                continue;

            const bool major = ((tick_ms / (int64_t)tick_step_ms) % 2) == 0;
            mgl::Rectangle tick({std::max(1.0f, (visible_right - visible_left) * 0.0012f), major ? layout.footer_size.y * 0.55f : layout.footer_size.y * 0.32f});
            tick.set_position(mgl::vec2f(tick_x, layout.footer_pos.y + 2.0f).floor());
            tick.set_color(major ? mgl::Color(215, 215, 215, 200) : mgl::Color(145, 145, 145, 150));
            window.draw(tick);

            if(major) {
                mgl::Text tick_label(format_timecode(tick_ms), get_theme().title_font_desc.c_str());
                tick_label.set_color(mgl::Color(208, 214, 220));
                tick_label.set_position((mgl::vec2f(tick_x + (visible_right - visible_left) * 0.008f, layout.footer_pos.y + layout.footer_size.y * 0.34f)).floor());
                window.draw(tick_label);
            }
        }
    }

    void TimelineWidget::draw_thumbnails(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, float visible_left, float visible_right) {
        const LayoutRects layout = get_layout(draw_pos, item_size);
        const float thumbnail_height = layout.content_size.y;
        const mgl::Scissor prev_scissor = window.get_scissor();
        window.set_scissor(scissor_get_sub_area(prev_scissor, { layout.content_pos.to_vec2i(), layout.content_size.to_vec2i() }));
        int thumbnail_loads_this_frame = 0;

        if(thumbnails.empty()) {
            mgl::Rectangle placeholder({layout.content_size.x, thumbnail_height});
            placeholder.set_position(layout.content_pos.floor());
            placeholder.set_color(mgl::Color(35, 39, 46));
            window.draw(placeholder);
            window.set_scissor(prev_scissor);
            return;
        }

        for(Thumbnail &thumbnail : thumbnails) {
            const float start_x = draw_pos.x + thumbnail.start_ms * get_pixels_per_ms();
            const float end_x = draw_pos.x + thumbnail.end_ms * get_pixels_per_ms();
            if(end_x < visible_left || start_x > visible_right)
                continue;

            const float clamped_start_x = std::max(visible_left, start_x);
            const float clamped_end_x = std::min(visible_right, end_x);
            const float width = std::max(1.0f, clamped_end_x - clamped_start_x);

            if(!thumbnail.texture_loaded && !thumbnail.texture_load_failed && thumbnail_loads_this_frame < max_thumbnail_loads_per_frame) {
                thumbnail.texture_loaded = thumbnail.texture.load_from_file(thumbnail.path.c_str());
                thumbnail.texture_load_failed = !thumbnail.texture_loaded;
                ++thumbnail_loads_this_frame;
            }

            if(thumbnail.texture_loaded) {
                mgl::Sprite sprite(&thumbnail.texture);
                sprite.set_position(mgl::vec2f(clamped_start_x, layout.content_pos.y).floor());
                sprite.set_size({width, thumbnail_height});
                window.draw(sprite);
            } else {
                mgl::Rectangle placeholder({width, thumbnail_height});
                placeholder.set_position(mgl::vec2f(clamped_start_x, layout.content_pos.y).floor());
                placeholder.set_color(mgl::Color(52, 56, 64));
                window.draw(placeholder);
            }

        }

        window.set_scissor(prev_scissor);
    }

    void TimelineWidget::draw_status(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size) {
        (void)window;
        (void)draw_pos;
        (void)item_size;
    }

    void TimelineWidget::draw_cut_points(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, float visible_left, float visible_right) const {
        if(chunks.empty() || duration_ms <= 0)
            return;

        const LayoutRects layout = get_layout(draw_pos, item_size);
        const float thumbnail_height = layout.content_size.y;
        const float marker_width = std::max(4.0f, std::round(item_size.y * 0.05f));
        const float cap_width = std::max(marker_width * 2.4f, 10.0f);
        const float cap_height = std::max(4.0f, std::round(item_size.y * 0.06f));

        for(size_t i = 0; i < chunks.size() - 1; ++i) {
            const float cut_x = draw_pos.x + chunks[i].end_ms * get_pixels_per_ms();
            if(cut_x < visible_left || cut_x > visible_right)
                continue;

            draw_filled_rect(window, {cut_x - marker_width * 0.5f, layout.content_pos.y}, {marker_width, thumbnail_height}, mgl::Color(255, 255, 255, 235));
            draw_filled_rect(window, {cut_x - cap_width * 0.5f, layout.content_pos.y}, {cap_width, cap_height}, mgl::Color(255, 0, 0, 240));
            draw_filled_rect(window, {cut_x - cap_width * 0.5f, layout.content_pos.y + thumbnail_height - cap_height}, {cap_width, cap_height}, mgl::Color(255, 0, 0, 220));
        }
    }

    void TimelineWidget::draw_chunk_overlay(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f item_size, float visible_left, float visible_right) const {
        if(chunks.empty() || duration_ms <= 0)
            return;

        const LayoutRects layout = get_layout(draw_pos, item_size);
        const float thumbnail_height = layout.content_size.y;
        const float pixels_per_ms = get_pixels_per_ms();
        const mgl::Scissor prev_scissor = window.get_scissor();
        window.set_scissor(scissor_get_sub_area(prev_scissor, { layout.content_pos.to_vec2i(), layout.content_size.to_vec2i() }));

        for(const TimelineChunk &chunk : chunks) {
            if(chunk.enabled)
                continue;

            const float start_x = draw_pos.x + chunk.start_ms * pixels_per_ms;
            const float end_x = draw_pos.x + chunk.end_ms * pixels_per_ms;

            if(end_x <= visible_left || start_x >= visible_right)
                continue;

            const float clamped_start = std::max(visible_left, start_x);
            const float clamped_end = std::min(visible_right, end_x);
            const float width = std::max(1.0f, clamped_end - clamped_start);

            draw_filled_rect(window, {clamped_start, layout.content_pos.y}, {width, thumbnail_height}, mgl::Color(12, 14, 18, 120));
            draw_filled_rect(window, {clamped_start, layout.content_pos.y}, {width, 1.0f}, mgl::Color(220, 226, 234, 50));
        }

        window.set_scissor(prev_scissor);
    }

    int64_t TimelineWidget::clamp_position_ms(int64_t position) const {
        return std::clamp<int64_t>(position, 0, std::max<int64_t>(0, duration_ms));
    }

    int64_t TimelineWidget::get_cut_point_proximity_ms() const {
        const int64_t default_proximity_ms = std::max<int64_t>(1, cut_point_proximity_ms);
        const int64_t min_zoom_proximity_ms = std::max<int64_t>(default_proximity_ms, max_cut_point_proximity_ms);
        const int64_t max_zoom_proximity_ms = std::min<int64_t>(default_proximity_ms, min_cut_point_proximity_ms);
        const double clamped_zoom = std::clamp((double)zoom, (double)min_zoom, (double)max_zoom);

        auto interpolate_log_zoom = [](double zoom_value, double zoom_start, double zoom_end, double value_start, double value_end) {
            if(zoom_end <= zoom_start)
                return value_end;

            const double zoom_ratio = std::log(zoom_value / zoom_start) / std::log(zoom_end / zoom_start);
            return value_start + (value_end - value_start) * zoom_ratio;
        };

        double proximity_ms = default_proximity_ms;
        if(clamped_zoom <= default_zoom) {
            proximity_ms = interpolate_log_zoom(clamped_zoom, min_zoom, default_zoom, min_zoom_proximity_ms, default_proximity_ms);
        } else {
            proximity_ms = interpolate_log_zoom(clamped_zoom, default_zoom, max_zoom, default_proximity_ms, max_zoom_proximity_ms);
        }

        return std::max<int64_t>(1, (int64_t)std::llround(proximity_ms));
    }

    int64_t TimelineWidget::get_cut_point_hit_proximity_ms() const {
        const double zoom_clamped = std::max((double)min_zoom, (double)zoom);
        const double pixel_capped_proximity_ms = (double)std::max<int64_t>(1, cut_point_proximity_ms) / zoom_clamped;
        return std::max<int64_t>(1, std::min<int64_t>(get_cut_point_proximity_ms(), (int64_t)std::llround(pixel_capped_proximity_ms)));
    }

    float TimelineWidget::get_timeline_width() const {
        return std::max(1.0f, duration_ms * get_pixels_per_ms());
    }

    float TimelineWidget::get_pixels_per_second() const {
        return base_pixels_per_second * zoom;
    }

    float TimelineWidget::get_pixels_per_ms() const {
        return get_pixels_per_second() / 1000.0f;
    }

    float TimelineWidget::get_visible_duration_ms(float visible_width) const {
        const float pixels_per_ms = std::max(0.001f, get_pixels_per_ms());
        return visible_width / pixels_per_ms;
    }

    double TimelineWidget::get_tick_step_ms(float visible_width) const {
        const double target_ms = get_visible_duration_ms(visible_width) / 8.0;
        for(double tick_step : tick_steps_ms) {
            if(tick_step >= target_ms)
                return tick_step;
        }
        return tick_steps_ms[sizeof(tick_steps_ms) / sizeof(tick_steps_ms[0]) - 1];
    }

    float TimelineWidget::position_ms_to_scroll(int64_t position_ms) const {
        return clamp_position_ms(position_ms) * get_pixels_per_ms();
    }

    int64_t TimelineWidget::scroll_to_position_ms(float scroll_x) const {
        return clamp_position_ms((int64_t)std::llround(scroll_x / std::max(0.0001f, get_pixels_per_ms())));
    }
}
