#include "../../include/gui/ExportPage.hpp"
#include "../../include/Theme.hpp"
#include "../../include/Utils.hpp"
#include "../../include/Process.hpp"
#include "../../include/gui/PageStack.hpp"
#include "../../include/gui/Subsection.hpp"
#include "../../include/gui/List.hpp"
#include "../../include/gui/Label.hpp"
#include "../../include/gui/ComboBox.hpp"
#include "../../include/gui/Entry.hpp"
#include "../../include/gui/CheckBox.hpp"
#include "../../include/gui/FileChooser.hpp"
#include "../../include/gui/GsrPage.hpp"
#include "../../include/gui/ScrollablePage.hpp"
#include "../../include/GsrInfo.hpp"
#include "../../include/Translation.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/window/Window.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <time.h>
#include <unordered_map>
#include <unordered_set>

namespace gsr {
    namespace {
        static const float button_spacing_scale = 0.015f;

        template<typename T>
        static T sv_to_int(std::string_view str) {
            long long result = 0;
            sscanf(str.data(), "%lld", &result);
            return (T)result;
        }

        static std::string get_container_from_filepath(const std::string &filepath) {
            const std::string extension = std::filesystem::path(filepath).extension().string();
            if(extension == ".mkv")
                return "matroska";
            if(extension == ".webm")
                return "webm";
            if(extension == ".mov")
                return "mov";
            return "mp4";
        }

        static std::string get_parent_directory(const std::string &filepath) {
            const std::filesystem::path path(filepath);
            if(path.has_parent_path())
                return path.parent_path().string();
            return get_videos_dir();
        }

        static std::string format_duration(int64_t duration_ms) {
            const int64_t total_seconds = std::max<int64_t>(0, duration_ms / 1000);
            const int64_t hours = total_seconds / 3600;
            const int64_t minutes = (total_seconds % 3600) / 60;
            const int64_t seconds = total_seconds % 60;

            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld",
                (long long)hours,
                (long long)minutes,
                (long long)seconds);
            return buffer;
        }

        static std::string format_file_size(int64_t bytes) {
            const char *units[] = {"B", "KB", "MB", "GB", "TB"};
            double size = (double)std::max<int64_t>(0, bytes);
            size_t unit_index = 0;
            while(size >= 1024.0 && unit_index + 1 < std::size(units)) {
                size /= 1024.0;
                ++unit_index;
            }

            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%.2f%s", size, units[unit_index]);
            return buffer;
        }

        static std::string humanize_video_codec(const std::string &codec) {
            if(codec == "h264")
                return "H264";
            if(codec == "hevc")
                return "HEVC";
            if(codec == "av1")
                return "AV1";
            if(codec == "vp8")
                return "VP8";
            if(codec == "vp9")
                return "VP9";
            if(codec == "h264_software")
                return "H264 Software";
            if(codec.empty())
                return TR("Unknown");
            return codec;
        }

        static std::string humanize_audio_codec(const std::string &codec) {
            if(codec == "aac")
                return "AAC";
            if(codec == "opus")
                return "Opus";
            if(codec.empty())
                return TR("Unknown");
            return codec;
        }

        static std::string map_video_codec_to_option_id(const std::string &codec) {
            if(codec == "h264" || codec == "hevc" || codec == "av1" || codec == "vp8" || codec == "vp9" || codec == "h264_software")
                return codec;
            return "auto";
        }

        static std::string map_audio_codec_to_option_id(const std::string &codec) {
            if(codec == "aac" || codec == "opus")
                return codec;
            return "aac";
        }

        static int64_t bitrate_kbps_from_bps(double bitrate_bps) {
            if(bitrate_bps <= 0.0)
                return 0;
            return std::max<int64_t>(1, (int64_t)std::llround(bitrate_bps / 1000.0));
        }

        static double parse_ffprobe_fps(const std::string &fps_str) {
            int numerator = 0;
            int denominator = 0;
            if(sscanf(fps_str.c_str(), "%d/%d", &numerator, &denominator) == 2 && denominator != 0)
                return (double)numerator / (double)denominator;
            return strtod(fps_str.c_str(), nullptr);
        }

        static bool parse_positive_double(std::string_view text, double &result) {
            const std::string str(text);
            char *end = nullptr;
            result = strtod(str.c_str(), &end);
            if(end == str.c_str() || (end && *end != '\0') || !std::isfinite(result) || result <= 0.0)
                return false;
            return true;
        }

        static std::string format_fps_value(double fps) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%.3f", fps);
            std::string result = buffer;
            while(result.size() > 1 && result.back() == '0')
                result.pop_back();
            if(!result.empty() && result.back() == '.')
                result.pop_back();
            return result;
        }

        static bool parse_ffprobe_key_value(const std::string &output, const char *key, std::string &value) {
            std::istringstream iss(output);
            std::string line;
            const std::string prefix = std::string(key) + "=";
            while(std::getline(iss, line)) {
                if(starts_with(line, prefix.c_str())) {
                    value = line.substr(prefix.size());
                    return true;
                }
            }
            return false;
        }

        static std::string get_date_str() {
            char str[128];
            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            strftime(str, sizeof(str) - 1, "%Y-%m-%d_%H-%M-%S", t);
            return str;
        }

        static std::string container_to_file_extension(const std::string &container) {
            if(container == "matroska")
                return "mkv";
            return container;
        }

        static std::string color_to_hex_str(mgl::Color color) {
            char color_str[8];
            snprintf(color_str, sizeof(color_str), "%02x%02x%02x", color.r, color.g, color.b);
            return color_str;
        }

        static std::string escape_ffconcat_path(const std::string &path) {
            std::string escaped;
            escaped.reserve(path.size() * 2);
            for(char c : path) {
                if(c == '\\' || c == ' ' || c == '\'' || c == '#')
                    escaped += '\\';
                escaped += c;
            }
            return escaped;
        }

        static std::string format_seconds(double seconds) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%.3f", std::max(0.0, seconds));
            return buffer;
        }

        static std::string shell_quote(const std::string &str) {
            std::string result = "'";
            for(char c : str) {
                if(c == '\'')
                    result += "'\\''";
                else
                    result += c;
            }
            result += "'";
            return result;
        }

        static void show_export_notification(const std::string &text, bool success) {
            const std::string timeout_seconds = success ? "3.000000" : "5.000000";
            const std::string icon_color_str = success ? "ffffff" : "ff0000";
            const std::string bg_color_str = success ? color_to_hex_str(get_color_theme().tint_color) : "ff0000";
            const char *notification_args[] = {
                "gsr-notify",
                "--text", text.c_str(),
                "--timeout", timeout_seconds.c_str(),
                "--icon-color", icon_color_str.c_str(),
                "--bg-color", bg_color_str.c_str(),
                "--icon", success ? "record" : GSR_UI_RESOURCES_PATH "/images/gsr-ui.png",
                nullptr
            };
            exec_program_on_host_daemonized(notification_args, false);
        }

        struct ExportRequest {
            std::string input_path;
            std::string output_path;
            std::string container;
            std::string video_codec;
            std::string audio_codec;
            std::string video_bitrate;
            std::string audio_bitrate;
            std::vector<TimelineWidget::TimelineChunk> chunks;
            bool reencode_video = false;
            bool reencode_audio = false;
            bool has_audio_stream = false;
            int video_width = 0;
            int video_height = 0;
        };

        struct ResolutionPreset {
            const char *label;
            const char *id;
            int width;
            int height;
        };

        static const ResolutionPreset resolution_presets[] = {
            { "Source (Recommended)", "source", 0, 0 },
            { "3840x2160 (4K)", "3840x2160", 3840, 2160 },
            { "2560x1440", "2560x1440", 2560, 1440 },
            { "1920x1080", "1920x1080", 1920, 1080 },
            { "1280x720", "1280x720", 1280, 720 },
            { "854x480", "854x480", 854, 480 },
            { "640x360", "640x360", 640, 360 }
        };

        static bool host_supports_video_codec(const std::string &codec);
        static bool host_supports_audio_codec(const std::string &codec);

        static std::string choose_default_video_codec(const ExportRequest &request, const ExportPage::SourceVideoInfo &source_info) {
            if(request.video_codec == "auto") {
                if(!request.reencode_video && !source_info.video_codec.empty() && source_info.video_codec != "h264_software")
                    return source_info.video_codec;

                if(request.container == "webm") {
                    if(host_supports_video_codec("vp9"))
                        return "vp9";
                    if(host_supports_video_codec("av1"))
                        return "av1";
                    if(host_supports_video_codec("vp8"))
                        return "vp8";
                    return "";
                }

                if(host_supports_video_codec("h264"))
                    return "h264";
                if(request.container != "mov" && host_supports_video_codec("av1"))
                    return "av1";
                if(host_supports_video_codec("hevc"))
                    return "hevc";
                if(host_supports_video_codec("vp9"))
                    return "vp9";
                if(host_supports_video_codec("vp8"))
                    return "vp8";
                return "";
            }
            if(request.video_codec == "h264_software")
                return "h264";
            return request.video_codec;
        }

        static std::string map_ffmpeg_video_encoder(const std::string &codec) {
            if(codec == "h264")
                return "libx264";
            if(codec == "hevc")
                return "libx265";
            if(codec == "av1")
                return "libaom-av1";
            if(codec == "vp8")
                return "libvpx";
            if(codec == "vp9")
                return "libvpx-vp9";
            return "libx264";
        }

        static std::string map_ffmpeg_audio_encoder(const std::string &codec) {
            if(codec == "opus")
                return "libopus";
            return "aac";
        }

        static const std::unordered_set<std::string>& get_host_ffmpeg_encoders() {
            static const std::unordered_set<std::string> encoders = [] {
                std::unordered_set<std::string> result;
                const char *args[] = { "ffmpeg", "-hide_banner", "-encoders", nullptr };
                std::string output;
                if(exec_program_on_host_get_stdout(args, output, false) != 0)
                    return result;

                std::istringstream iss(output);
                std::string line;
                while(std::getline(iss, line)) {
                    if(line.size() < 8)
                        continue;
                    if(line[0] != ' ')
                        continue;

                    std::istringstream line_stream(line.substr(8));
                    std::string encoder_name;
                    if(line_stream >> encoder_name)
                        result.insert(encoder_name);
                }
                return result;
            }();
            return encoders;
        }

        static bool host_supports_ffmpeg_encoder(const std::string &encoder_name) {
            const auto &encoders = get_host_ffmpeg_encoders();
            return encoders.find(encoder_name) != encoders.end();
        }

        static bool host_supports_video_codec(const std::string &codec) {
            return host_supports_ffmpeg_encoder(map_ffmpeg_video_encoder(codec));
        }

        static bool host_supports_audio_codec(const std::string &codec) {
            return host_supports_ffmpeg_encoder(map_ffmpeg_audio_encoder(codec));
        }

        static bool host_supports_audio_codec_in_container(const std::string &container, const std::string &codec) {
            if(!host_supports_audio_codec(codec))
                return false;

            static std::unordered_map<std::string, bool> compatibility_cache;
            const std::string cache_key = container + ":" + codec;
            auto it = compatibility_cache.find(cache_key);
            if(it != compatibility_cache.end())
                return it->second;

            const std::string output_path = (std::filesystem::temp_directory_path() /
                ("gpu-screen-recorder-audio-codec-probe-" + container + "-" + codec + "." + container_to_file_extension(container))).string();
            const std::string ffmpeg_audio_encoder = map_ffmpeg_audio_encoder(codec);
            const char *args[] = {
                "ffmpeg",
                "-hide_banner",
                "-loglevel",
                "error",
                "-f",
                "lavfi",
                "-i",
                "anullsrc=r=48000:cl=stereo",
                "-t",
                "0.1",
                "-c:a",
                ffmpeg_audio_encoder.c_str(),
                "-vn",
                "-y",
                output_path.c_str(),
                nullptr
            };

            std::string output;
            const bool supported = exec_program_on_host_get_stdout(args, output, false) == 0;
            compatibility_cache[cache_key] = supported;
            std::error_code remove_error;
            std::filesystem::remove(output_path, remove_error);
            return supported;
        }

        static bool container_supports_video_codec(const std::string &container, const std::string &codec) {
            if(container == "webm")
                return codec == "vp8" || codec == "vp9" || codec == "av1";
            if(container == "mp4")
                return codec == "h264" || codec == "hevc" || codec == "av1";
            if(container == "mov")
                return codec == "h264" || codec == "hevc";
            return true;
        }

        static bool container_supports_audio_codec(const std::string &container, const std::string &codec) {
            if(container == "webm")
                return codec == "opus";
            if(container == "mp4" || container == "mov")
                return codec == "aac";
            return true;
        }

        static bool container_supports_audio_reencode_codec(const std::string &container, const std::string &codec) {
            if(container == "webm")
                return codec == "opus";
            return host_supports_audio_codec_in_container(container, codec);
        }

        static std::string get_default_audio_codec_for_container(const std::string &container) {
            if(container == "webm")
                return host_supports_audio_codec("opus") ? "opus" : "";

            if(container_supports_audio_reencode_codec(container, "opus"))
                return "opus";
            if(container_supports_audio_reencode_codec(container, "aac"))
                return "aac";
            return "";
        }

        static int64_t get_estimated_audio_bitrate_guess_kbps(const ExportPage::SourceVideoInfo &source_info) {
            if(source_info.has_audio_bitrate)
                return source_info.audio_bitrate_kbps;
            if(source_info.audio_codec.empty())
                return 0;
            if(source_info.total_bitrate_kbps > 0)
                return std::min<int64_t>(128, std::max<int64_t>(32, source_info.total_bitrate_kbps / 8));
            return 128;
        }

        static int64_t get_estimated_video_bitrate_guess_kbps(const ExportPage::SourceVideoInfo &source_info) {
            if(source_info.has_video_bitrate)
                return source_info.video_bitrate_kbps;
            if(source_info.total_bitrate_kbps > 0)
                return std::max<int64_t>(1, source_info.total_bitrate_kbps - get_estimated_audio_bitrate_guess_kbps(source_info));
            return source_info.video_bitrate_kbps;
        }

        static bool should_override_fps(const ExportPage::SourceVideoInfo &source_info, double target_fps, bool framerate_modified) {
            if(!framerate_modified || target_fps <= 0.0)
                return false;
            if(source_info.fps <= 0.0)
                return true;
            return std::abs(target_fps - source_info.fps) > 0.01;
        }

        static int64_t scale_bitrate_for_resolution(int64_t bitrate_kbps, int source_width, int source_height, int target_width, int target_height) {
            if(bitrate_kbps <= 0 || source_width <= 0 || source_height <= 0 || target_width <= 0 || target_height <= 0)
                return bitrate_kbps;

            const double source_pixels = (double)source_width * (double)source_height;
            const double target_pixels = (double)target_width * (double)target_height;
            if(source_pixels <= 0.0 || target_pixels <= 0.0)
                return bitrate_kbps;

            return std::max<int64_t>(1, (int64_t)std::llround((double)bitrate_kbps * (target_pixels / source_pixels)));
        }

        static int64_t scale_bitrate_for_fps(int64_t bitrate_kbps, double source_fps, double target_fps) {
            if(bitrate_kbps <= 0 || source_fps <= 0.0 || target_fps <= 0.0)
                return bitrate_kbps;

            const double fps_scale = std::min(1.0, target_fps / source_fps);
            return std::max<int64_t>(1, (int64_t)std::llround((double)bitrate_kbps * fps_scale));
        }

        static double get_codec_efficiency_factor(std::string_view codec) {
            if(codec == "av1")
                return 0.62;
            if(codec == "hevc")
                return 0.74;
            if(codec == "vp9")
                return 0.82;
            if(codec == "h264")
                return 1.0;
            if(codec == "vp8")
                return 1.08;
            return 1.0;
        }

        static const ResolutionPreset* find_resolution_preset(std::string_view id) {
            for(const ResolutionPreset &preset : resolution_presets) {
                if(id == preset.id)
                    return &preset;
            }
            return nullptr;
        }

        static mgl::vec2i get_scaled_video_size(mgl::vec2i source_size, const ResolutionPreset *preset) {
            if(!preset || preset->width <= 0 || preset->height <= 0 || source_size.x <= 0 || source_size.y <= 0)
                return source_size;

            const double scale = std::min((double)preset->width / (double)source_size.x, (double)preset->height / (double)source_size.y);
            if(scale >= 1.0)
                return source_size;

            int scaled_width = std::max(2, (int)std::llround((double)source_size.x * scale));
            int scaled_height = std::max(2, (int)std::llround((double)source_size.y * scale));
            if((scaled_width & 1) != 0)
                --scaled_width;
            if((scaled_height & 1) != 0)
                --scaled_height;
            scaled_width = std::max(2, scaled_width);
            scaled_height = std::max(2, scaled_height);
            return { scaled_width, scaled_height };
        }

        static int get_quality_preset_multiplier_percent(std::string_view quality) {
            if(quality == "medium")
                return 100;
            if(quality == "high")
                return 140;
            if(quality == "very_high")
                return 200;
            if(quality == "ultra")
                return 280;
            return 100;
        }

        static bool parse_target_fps_for_estimation(std::string_view fps_text, double &target_fps) {
            return parse_positive_double(fps_text, target_fps);
        }

        static int get_quality_preset_crf(std::string_view codec, std::string_view quality) {
            if(codec == "hevc") {
                if(quality == "medium") return 31;
                if(quality == "high") return 28;
                if(quality == "very_high") return 25;
                if(quality == "ultra") return 21;
                return 25;
            }
            if(codec == "av1") {
                if(quality == "medium") return 40;
                if(quality == "high") return 35;
                if(quality == "very_high") return 31;
                if(quality == "ultra") return 27;
                return 31;
            }
            if(codec == "vp8" || codec == "vp9") {
                if(quality == "medium") return 37;
                if(quality == "high") return 33;
                if(quality == "very_high") return 29;
                if(quality == "ultra") return 26;
                return 29;
            }

            if(quality == "medium") return 29;
            if(quality == "high") return 25;
            if(quality == "very_high") return 22;
            if(quality == "ultra") return 18;
            return 22;
        }

        static bool quality_preset_uses_zero_bitrate(std::string_view codec) {
            return codec == "av1" || codec == "vp8" || codec == "vp9";
        }

        static int64_t estimate_quality_preset_video_bitrate_kbps(const ExportPage::SourceVideoInfo &source_info, const ResolutionPreset *preset,
            const std::string &target_codec, std::string_view quality, double target_fps)
        {
            int64_t bitrate_kbps = std::max<int64_t>(1, get_estimated_video_bitrate_guess_kbps(source_info));
            const mgl::vec2i scaled_video_size = get_scaled_video_size({ source_info.metadata.width, source_info.metadata.height }, preset);
            bitrate_kbps = scale_bitrate_for_resolution(
                bitrate_kbps,
                source_info.metadata.width,
                source_info.metadata.height,
                scaled_video_size.x,
                scaled_video_size.y);
            bitrate_kbps = scale_bitrate_for_fps(bitrate_kbps, source_info.fps, target_fps);

            const double source_efficiency = get_codec_efficiency_factor(source_info.video_codec);
            const double target_efficiency = get_codec_efficiency_factor(target_codec);
            if(source_efficiency > 0.0 && target_efficiency > 0.0)
                bitrate_kbps = std::max<int64_t>(1, (int64_t)std::llround((double)bitrate_kbps * (target_efficiency / source_efficiency)));

            bitrate_kbps = std::max<int64_t>(1, (bitrate_kbps * get_quality_preset_multiplier_percent(quality)) / 100);
            return bitrate_kbps;
        }
    }

    ExportPage::ExportPage(const GsrInfo *gsr_info, PageStack *page_stack, VideoMetadata video_metadata, std::vector<TimelineWidget::TimelineChunk> chunks) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        gsr_info(gsr_info),
        page_stack(page_stack),
        top_text(TR("Export"), get_theme().title_font_desc.c_str()),
        bottom_text(TR("Video Trimmer"), get_theme().title_font_desc.c_str())
    {
        source_info.metadata = std::move(video_metadata);
        source_info.chunks = std::move(chunks);
        load_source_video_info();

        const float margin = 0.02f;
        set_margins(margin, margin, margin, margin);

        add_button(TR("Export"), "export", get_color_theme().tint_color);
        add_button(TR("Cancel"), "cancel", get_color_theme().page_bg_color);
        on_click = [this, page_stack](const std::string &id) {
            if(id == "cancel")
                page_stack->pop();
            if(id == "export")
                start_export();
        };

        add_widgets();
        apply_source_defaults();
        update_reencode_options_visibility();
        update_source_summary();
        update_estimated_file_size();
        update_settings_scrollable_size();
    }

    bool ExportPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        for(size_t i = 0; i < buttons.size(); ++i) {
            ButtonItem &button_item = buttons[i];
            if(!button_item.button->on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
                return false;
        }

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        const mgl::vec2f content_page_position = get_content_position() + mgl::vec2f(margin_left, get_border_size() + margin_top).floor();

        Widget *selected_widget = selected_child_widget;

        if(selected_widget) {
            if(!selected_widget->on_event(event, window, content_page_position))
                return false;
        }

        return widgets.for_each_reverse([selected_widget, &window, &event, content_page_position](std::unique_ptr<Widget> &widget) {
            Widget *p = widget.get();
            if(p != selected_widget) {
                if(!p->on_event(event, window, content_page_position))
                    return false;
            }
            return true;
        });
    }

    void ExportPage::draw(mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return;

        const mgl::vec2f content_page_position = get_content_position();
        const mgl::vec2f content_page_size = get_size();

        mgl::Rectangle background(content_page_size);
        background.set_position(content_page_position);
        background.set_color(get_color_theme().page_bg_color);
        window.draw(background);

        mgl::Rectangle border(mgl::vec2f(content_page_size.x, get_border_size()).floor());
        border.set_position(content_page_position);
        border.set_color(get_color_theme().tint_color);
        window.draw(border);

        draw_page_label(window, content_page_position);
        draw_buttons(window, content_page_position, content_page_size);

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        draw_children(window, content_page_position + mgl::vec2f(margin_left, get_border_size() + margin_top).floor());
    }

    void ExportPage::draw_page_label(mgl::Window &window, mgl::vec2f body_pos) {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();

        mgl::Rectangle background(mgl::vec2f(window_size.x / 10, window_size.x / 10).floor());
        background.set_position(body_pos.floor() - mgl::vec2f(background.get_size().x + get_horizontal_spacing(), 0.0f).floor());
        background.set_color(mgl::Color(0, 0, 0, 255));
        window.draw(background);

        const int text_margin = background.get_size().y * 0.085;

        top_text.set_position((background.get_position() + mgl::vec2f(background.get_size().x * 0.5f - top_text.get_bounds().size.x * 0.5f, text_margin)).floor());
        window.draw(top_text);

        mgl::Sprite icon(&get_theme().trimmer_texture);
        icon.set_height((int)(background.get_size().y * 0.8f));
        icon.set_position((background.get_position() + background.get_size() * 0.5f - icon.get_size() * 0.5f).floor());
        window.draw(icon);

        bottom_text.set_position((background.get_position() + mgl::vec2f(background.get_size().x * 0.5f - bottom_text.get_bounds().size.x * 0.5f, background.get_size().y - bottom_text.get_bounds().size.y - text_margin)).floor());
        window.draw(bottom_text);
    }

    void ExportPage::draw_buttons(mgl::Window &window, mgl::vec2f body_pos, mgl::vec2f body_size) {
        float offset_y = 0.0f;
        for(size_t i = 0; i < buttons.size(); ++i) {
            ButtonItem &button_item = buttons[i];
            button_item.button->set_position(body_pos + mgl::vec2f(body_size.x + get_horizontal_spacing(), offset_y).floor());
            button_item.button->draw(window, mgl::vec2f(0.0f, 0.0f));
            offset_y += button_item.button->get_size().y + (button_spacing_scale * get_theme().window_height);
        }
    }

    void ExportPage::draw_children(mgl::Window &window, mgl::vec2f position) {
        Widget *selected_widget = selected_child_widget;

        const mgl::Scissor prev_scissor = window.get_scissor();
        window.set_scissor({position.to_vec2i(), get_inner_size().to_vec2i()});

        for(size_t i = 0; i < widgets.size(); ++i) {
            auto &widget = widgets[i];
            if(widget.get() != selected_widget)
                widget->draw(window, position);
        }

        if(selected_widget)
            selected_widget->draw(window, position);

        window.set_scissor(prev_scissor);
    }

    std::unique_ptr<Widget> ExportPage::create_save_directory(const char *label) {
        auto save_directory_list = std::make_unique<List>(List::Orientation::VERTICAL);
        save_directory_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), label, get_color_theme().text_color));
        auto save_directory_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), get_parent_directory(source_info.metadata.filepath).c_str(), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        save_directory_button_ptr = save_directory_button.get();
        save_directory_button->on_click = [this]() {
            auto select_directory_page = std::make_unique<GsrPage>(TR("File"), TR("Export"));
            select_directory_page->add_button(TR("Save"), "save", get_color_theme().tint_color);
            select_directory_page->add_button(TR("Cancel"), "cancel", get_color_theme().page_bg_color);

            auto file_chooser = std::make_unique<FileChooser>(save_directory_button_ptr->get_text(), select_directory_page->get_inner_size());
            FileChooser *file_chooser_ptr = file_chooser.get();
            select_directory_page->add_widget(std::move(file_chooser));

            select_directory_page->on_click = [this, file_chooser_ptr](const std::string &id) {
                if(id == "save") {
                    save_directory_button_ptr->set_text(file_chooser_ptr->get_current_directory());
                    page_stack->pop();
                } else if(id == "cancel") {
                    page_stack->pop();
                }
            };

            page_stack->push(std::move(select_directory_page));
        };
        save_directory_list->add_widget(std::move(save_directory_button));
        return save_directory_list;
    }

    std::unique_ptr<ComboBox> ExportPage::create_container_box() {
        auto container_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        container_box->add_item("mp4", "mp4");
        container_box->add_item("mkv", "matroska");
        container_box->add_item("webm", "webm");
        container_box->add_item("mov", "mov");
        container_box_ptr = container_box.get();
        return container_box;
    }

    std::unique_ptr<Widget> ExportPage::create_container_section() {
        auto container_list = std::make_unique<List>(List::Orientation::VERTICAL);
        container_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Container:"), get_color_theme().text_color));
        container_list->add_widget(create_container_box());
        container_section_ptr = container_list.get();
        return container_list;
    }

    std::unique_ptr<Widget> ExportPage::create_file_info_section() {
        auto file_info_data_list = std::make_unique<List>(List::Orientation::VERTICAL);
        file_info_data_list->add_widget(create_save_directory(TR("Directory to save trimmed video:")));
        file_info_data_list->add_widget(create_container_section());
        return std::make_unique<Subsection>(TR("File info"), std::move(file_info_data_list), mgl::vec2f(get_settings_content_width(), 0.0f));
    }

    std::unique_ptr<Label> ExportPage::create_source_summary_label() {
        auto label = std::make_unique<Label>(get_theme().body_font_desc.c_str(), "", get_color_theme().text_color);
        label->set_wrap_width(get_settings_content_width());
        source_summary_label_ptr = label.get();
        return label;
    }

    std::unique_ptr<Label> ExportPage::create_estimated_file_size() {
        auto label = std::make_unique<Label>(get_theme().body_font_desc.c_str(), "", get_color_theme().text_color);
        label->set_wrap_width(get_settings_content_width());
        estimated_file_size_ptr = label.get();
        return label;
    }

    std::unique_ptr<RadioButton> ExportPage::create_view_radio_button() {
        auto view_radio_button = std::make_unique<RadioButton>(get_theme().body_font_desc.c_str(), RadioButton::Orientation::HORIZONTAL);
        view_radio_button->add_item(TR("Simple view"), "simple");
        view_radio_button->add_item(TR("Advanced view"), "advanced");
        view_radio_button->set_horizontal_alignment(Widget::Alignment::CENTER);
        view_radio_button_ptr = view_radio_button.get();
        return view_radio_button;
    }

    std::unique_ptr<Widget> ExportPage::create_source_info_section() {
        auto source_info_list = std::make_unique<List>(List::Orientation::VERTICAL);
        source_info_list->add_widget(create_source_summary_label());
        source_info_list->add_widget(create_estimated_file_size());
        return std::make_unique<Subsection>(TR("Source"), std::move(source_info_list), mgl::vec2f(get_settings_content_width(), 0.0f));
    }

    std::unique_ptr<Widget> ExportPage::create_reencode_video_checkbox() {
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Re-encode video to reduce file size"));
        reencode_video_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<Widget> ExportPage::create_reencode_audio_checkbox() {
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Re-encode audio to reduce file size"));
        reencode_audio_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<ComboBox> ExportPage::create_video_codec_box() {
        auto video_codec_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        video_codec_box->add_item(TR("Auto (Recommended)"), "auto");
        if(host_supports_video_codec("h264"))
            video_codec_box->add_item(TR("H264"), "h264");
        if(host_supports_video_codec("hevc"))
            video_codec_box->add_item(TR("HEVC"), "hevc");
        if(host_supports_video_codec("av1"))
            video_codec_box->add_item(TR("AV1"), "av1");
        if(host_supports_video_codec("vp9"))
            video_codec_box->add_item(TR("VP9"), "vp9");
        if(host_supports_video_codec("vp8"))
            video_codec_box->add_item(TR("VP8"), "vp8");
        if(host_supports_video_codec("h264"))
            video_codec_box->add_item(TR("H264 Software Encoder (Slow, not recommended)"), "h264_software");
        video_codec_box_ptr = video_codec_box.get();
        return video_codec_box;
    }

    std::unique_ptr<Widget> ExportPage::create_video_codec() {
        auto video_codec_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_codec_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Video codec:"), get_color_theme().text_color));
        video_codec_list->add_widget(create_video_codec_box());
        video_codec_ptr = video_codec_list.get();
        return video_codec_list;
    }

    std::unique_ptr<ComboBox> ExportPage::create_audio_codec_box() {
        auto audio_codec_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        if(host_supports_audio_codec("aac"))
            audio_codec_box->add_item(TR("AAC"), "aac");
        if(host_supports_audio_codec("opus"))
            audio_codec_box->add_item(TR("Opus"), "opus");
        audio_codec_box_ptr = audio_codec_box.get();
        return audio_codec_box;
    }

    std::unique_ptr<Widget> ExportPage::create_audio_codec() {
        auto audio_codec_list = std::make_unique<List>(List::Orientation::VERTICAL);
        audio_codec_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Audio codec:"), get_color_theme().text_color));
        audio_codec_list->add_widget(create_audio_codec_box());
        audio_codec_ptr = audio_codec_list.get();
        return audio_codec_list;
    }

    std::unique_ptr<Entry> ExportPage::create_framerate_entry() {
        auto entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "60", (int)(2.0f * mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 3));
        framerate_entry_ptr = entry.get();
        return entry;
    }

    std::unique_ptr<Widget> ExportPage::create_framerate() {
        auto framerate_list = std::make_unique<List>(List::Orientation::VERTICAL);
        framerate_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Frame rate:"), get_color_theme().text_color));
        framerate_list->add_widget(create_framerate_entry());
        framerate_ptr = framerate_list.get();
        return framerate_list;
    }

    std::unique_ptr<ComboBox> ExportPage::create_video_quality_box() {
        auto box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        box->add_item(TR("Constant bitrate (Recommended)"), "custom");
        box->add_item(TR("Medium"), "medium");
        box->add_item(TR("High"), "high");
        box->add_item(TR("Very high"), "very_high");
        box->add_item(TR("Ultra"), "ultra");
        video_quality_box_ptr = box.get();
        return box;
    }

    std::unique_ptr<Widget> ExportPage::create_video_quality() {
        auto video_quality_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_quality_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Video quality:"), get_color_theme().text_color));
        video_quality_list->add_widget(create_video_quality_box());
        video_quality_ptr = video_quality_list.get();
        return video_quality_list;
    }

    std::unique_ptr<ComboBox> ExportPage::create_video_resolution_box() {
        auto box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        for(const ResolutionPreset &preset : resolution_presets)
            box->add_item(TR(preset.label), preset.id);
        video_resolution_box_ptr = box.get();
        return box;
    }

    std::unique_ptr<Widget> ExportPage::create_video_resolution() {
        auto video_resolution_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_resolution_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Video resolution limit:"), get_color_theme().text_color));
        video_resolution_list->add_widget(create_video_resolution_box());

        video_resolution_ptr = video_resolution_list.get();
        return video_resolution_list;
    }

    std::unique_ptr<Entry> ExportPage::create_video_bitrate_entry() {
        auto entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "8000", (int)(2.0f * mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 6));
        entry->set_number_mode(true, 1, 500000);
        video_bitrate_entry_ptr = entry.get();
        return entry;
    }

    std::unique_ptr<Entry> ExportPage::create_audio_bitrate_entry() {
        auto entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "128", (int)(2.0f * mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 5));
        entry->set_number_mode(true, 1, 1000);
        audio_bitrate_entry_ptr = entry.get();
        return entry;
    }

    std::unique_ptr<Widget> ExportPage::create_video_bitrate() {
        auto video_bitrate_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_bitrate_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Target video bitrate (Kbps):"), get_color_theme().text_color));
        video_bitrate_list->add_widget(create_video_bitrate_entry());
        video_bitrate_ptr = video_bitrate_list.get();
        return video_bitrate_list;
    }

    std::unique_ptr<Widget> ExportPage::create_audio_bitrate() {
        auto audio_bitrate_list = std::make_unique<List>(List::Orientation::VERTICAL);
        audio_bitrate_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Target audio bitrate (Kbps):"), get_color_theme().text_color));
        audio_bitrate_list->add_widget(create_audio_bitrate_entry());
        audio_bitrate_ptr = audio_bitrate_list.get();
        return audio_bitrate_list;
    }

    std::unique_ptr<Widget> ExportPage::create_video_section() {
        auto video_section_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_section_list->add_widget(create_reencode_video_checkbox());

        auto video_reencode_options = std::make_unique<List>(List::Orientation::VERTICAL);
        video_reencode_options_ptr = video_reencode_options.get();
        video_reencode_options->add_widget(create_video_quality());
        video_reencode_options->add_widget(create_video_resolution());
        video_reencode_options->add_widget(create_video_codec());
        video_reencode_options->add_widget(create_framerate());
        video_reencode_options->add_widget(create_video_bitrate());
        video_section_list->add_widget(std::move(video_reencode_options));

        video_section_list->add_widget(create_reencode_audio_checkbox());

        auto audio_reencode_options = std::make_unique<List>(List::Orientation::VERTICAL);
        audio_reencode_options_ptr = audio_reencode_options.get();
        audio_reencode_options->add_widget(create_audio_codec());
        audio_reencode_options->add_widget(create_audio_bitrate());
        video_section_list->add_widget(std::move(audio_reencode_options));

        return std::make_unique<Subsection>(TR("Compression"), std::move(video_section_list), mgl::vec2f(get_settings_content_width(), 0.0f));
    }

    std::unique_ptr<Widget> ExportPage::create_settings() {
        auto page_list = std::make_unique<List>(List::Orientation::VERTICAL);
        page_list->set_spacing(0.018f);
        page_list->add_widget(create_view_radio_button());
        page_list_ptr = page_list.get();

        const float page_spacing = std::floor(0.018f * get_theme().window_height);
        const float margin_top = std::floor(margin_top_scale * get_theme().window_height);
        const float margin_bottom = std::floor(margin_bottom_scale * get_theme().window_height);
        const float max_inner_page_height = std::max(0.0f, std::floor(get_theme().window_height * 0.7f) - margin_top - margin_bottom - get_border_size());
        const float max_scrollable_height = std::max(0.0f, max_inner_page_height - page_list->get_size().y - page_spacing);
        auto scrollable_page = std::make_unique<ScrollablePage>(mgl::vec2f(get_settings_page_width(), max_scrollable_height));
        settings_scrollable_page_ptr = scrollable_page.get();
        page_list->add_widget(std::move(scrollable_page));

        auto settings_list = std::make_unique<List>(List::Orientation::VERTICAL);
        settings_list->set_spacing(0.018f);
        settings_list->add_widget(create_file_info_section());
        settings_list->add_widget(create_source_info_section());
        settings_list->add_widget(create_video_section());
        settings_list_ptr = settings_list.get();
        settings_scrollable_page_ptr->add_widget(std::move(settings_list));
        update_settings_scrollable_size();

        return page_list;
    }

    void ExportPage::add_widgets() {
        add_widget(create_settings());

        reencode_video_checkbox_ptr->on_changed = [this](bool) {
            update_reencode_options_visibility();
            update_estimated_file_size();
        };
        reencode_audio_checkbox_ptr->on_changed = [this](bool) {
            update_reencode_options_visibility();
            update_estimated_file_size();
        };
        video_quality_box_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            const bool custom_selected = id == "custom";
            if(custom_selected)
                apply_video_quality_preset(false);
            update_reencode_options_visibility();
            update_estimated_file_size();
        };
        video_resolution_box_ptr->on_selection_changed = [this](std::string_view, std::string_view) {
            apply_selected_resolution_preset(false);
            update_estimated_file_size();
        };
        video_bitrate_entry_ptr->on_changed = [this](std::string_view) {
            if(!changing_video_bitrate_programmatically) {
                video_bitrate_manually_modified = true;
                last_manual_video_bitrate_text = std::string(video_bitrate_entry_ptr->get_text());
            }
            update_estimated_file_size();
        };
        framerate_entry_ptr->on_changed = [this](std::string_view) {
            framerate_modified = framerate_entry_ptr->get_text() != source_framerate_text;
            apply_video_quality_preset(false);
            update_estimated_file_size();
        };
        audio_bitrate_entry_ptr->on_changed = [this](std::string_view) {
            update_estimated_file_size();
        };
        video_codec_box_ptr->on_selection_changed = [this](std::string_view, std::string_view) {
            update_estimated_file_size();
            return true;
        };
        audio_codec_box_ptr->on_selection_changed = [this](std::string_view, std::string_view) {
            update_estimated_file_size();
            return true;
        };
        container_box_ptr->on_selection_changed = [this](std::string_view, std::string_view) {
            update_reencode_options_visibility();
            update_estimated_file_size();
            return true;
        };
        view_radio_button_ptr->set_selected_item("simple");
        view_radio_button_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            view_changed(id == "advanced");
            return true;
        };
        view_changed(false);
    }

    void ExportPage::load_source_video_info() {
        if(source_info.metadata.file_size <= 0) {
            struct stat st;
            if(stat(source_info.metadata.filepath.c_str(), &st) == 0)
                source_info.metadata.file_size = st.st_size;
        }

        {
            const char *args[] = {
                "ffprobe",
                "-v", "error",
                "-select_streams", "v:0",
                "-show_entries", "stream=codec_name,bit_rate,width,height,avg_frame_rate,r_frame_rate",
                "-of", "default=noprint_wrappers=1:nokey=0",
                source_info.metadata.filepath.c_str(),
                nullptr
            };

            std::string output;
            if(exec_program_on_host_get_stdout(args, output, false) == 0) {
                std::string value;
                if(parse_ffprobe_key_value(output, "codec_name", value))
                    source_info.video_codec = value;
                if(parse_ffprobe_key_value(output, "bit_rate", value)) {
                    source_info.video_bitrate_kbps = bitrate_kbps_from_bps(strtod(value.c_str(), nullptr));
                    source_info.has_video_bitrate = source_info.video_bitrate_kbps > 0;
                }
                if(parse_ffprobe_key_value(output, "avg_frame_rate", value))
                    source_info.fps = parse_ffprobe_fps(value);
                if(source_info.fps <= 0.0 && parse_ffprobe_key_value(output, "r_frame_rate", value))
                    source_info.fps = parse_ffprobe_fps(value);
                if(source_info.metadata.width <= 0 && parse_ffprobe_key_value(output, "width", value))
                    source_info.metadata.width = atoi(value.c_str());
                if(source_info.metadata.height <= 0 && parse_ffprobe_key_value(output, "height", value))
                    source_info.metadata.height = atoi(value.c_str());
            }
        }

        {
            const char *args[] = {
                "ffprobe",
                "-v", "error",
                "-select_streams", "a:0",
                "-show_entries", "stream=codec_name,bit_rate",
                "-of", "default=noprint_wrappers=1:nokey=0",
                source_info.metadata.filepath.c_str(),
                nullptr
            };

            std::string output;
            if(exec_program_on_host_get_stdout(args, output, false) == 0) {
                std::string value;
                if(parse_ffprobe_key_value(output, "codec_name", value))
                    source_info.audio_codec = value;
                if(parse_ffprobe_key_value(output, "bit_rate", value)) {
                    source_info.audio_bitrate_kbps = bitrate_kbps_from_bps(strtod(value.c_str(), nullptr));
                    source_info.has_audio_bitrate = source_info.audio_bitrate_kbps > 0;
                }
            }
        }

        {
            const char *args[] = {
                "ffprobe",
                "-v", "error",
                "-show_entries", "format=duration,bit_rate",
                "-of", "default=noprint_wrappers=1:nokey=0",
                source_info.metadata.filepath.c_str(),
                nullptr
            };

            std::string output;
            if(exec_program_on_host_get_stdout(args, output, false) == 0) {
                std::string value;
                if(source_info.metadata.duration_seconds <= 0.0 && parse_ffprobe_key_value(output, "duration", value))
                    source_info.metadata.duration_seconds = strtod(value.c_str(), nullptr);
                if(parse_ffprobe_key_value(output, "bit_rate", value))
                    source_info.total_bitrate_kbps = bitrate_kbps_from_bps(strtod(value.c_str(), nullptr));
            }
        }

        if(source_info.total_bitrate_kbps <= 0 && source_info.metadata.file_size > 0 && source_info.metadata.duration_seconds > 0.0) {
            source_info.total_bitrate_kbps = std::max<int64_t>(1,
                (int64_t)std::llround(((double)source_info.metadata.file_size * 8.0) / source_info.metadata.duration_seconds / 1000.0));
        }

        if(source_info.video_bitrate_kbps <= 0 && source_info.has_audio_bitrate && source_info.total_bitrate_kbps > source_info.audio_bitrate_kbps) {
            source_info.video_bitrate_kbps = source_info.total_bitrate_kbps - source_info.audio_bitrate_kbps;
            source_info.has_video_bitrate = source_info.video_bitrate_kbps > 0;
        }
        if(source_info.audio_bitrate_kbps <= 0 && source_info.has_video_bitrate && source_info.total_bitrate_kbps > source_info.video_bitrate_kbps) {
            source_info.audio_bitrate_kbps = source_info.total_bitrate_kbps - source_info.video_bitrate_kbps;
            source_info.has_audio_bitrate = source_info.audio_bitrate_kbps > 0;
        }
        if(source_info.audio_bitrate_kbps <= 0 && !source_info.audio_codec.empty())
            source_info.audio_bitrate_kbps = 128;

        source_info.container = get_container_from_filepath(source_info.metadata.filepath);
    }

    void ExportPage::apply_source_defaults() {
        save_directory_button_ptr->set_text(get_parent_directory(source_info.metadata.filepath));
        container_box_ptr->set_selected_item(source_info.container);
        video_codec_box_ptr->set_selected_item(map_video_codec_to_option_id(source_info.video_codec));
        audio_codec_box_ptr->set_selected_item(map_audio_codec_to_option_id(source_info.audio_codec));
        source_framerate_known = source_info.fps > 0.0;
        source_framerate_text = format_fps_value(source_framerate_known ? source_info.fps : 60.0);
        framerate_entry_ptr->set_text(source_framerate_text);
        framerate_modified = !source_framerate_known;
        video_quality_box_ptr->set_selected_item("custom", false, false);
        video_resolution_box_ptr->set_selected_item("source", false, false);
        video_bitrate_manually_modified = false;
        changing_video_bitrate_programmatically = true;
        video_bitrate_entry_ptr->set_text(std::to_string(std::max<int64_t>(1, get_estimated_video_bitrate_guess_kbps(source_info))));
        changing_video_bitrate_programmatically = false;
        last_manual_video_bitrate_text.clear();
        audio_bitrate_entry_ptr->set_text(std::to_string(std::max<int64_t>(1, get_estimated_audio_bitrate_guess_kbps(source_info))));
        reencode_video_checkbox_ptr->set_checked(false);
        reencode_audio_checkbox_ptr->set_checked(false);
    }

    bool ExportPage::is_video_reencode_active() const {
        if(reencode_video_checkbox_ptr->is_checked())
            return true;
        return !container_supports_video_codec(std::string(container_box_ptr->get_selected_id()), source_info.video_codec);
    }

    bool ExportPage::is_audio_reencode_active() const {
        if(source_info.audio_codec.empty())
            return false;
        if(reencode_audio_checkbox_ptr->is_checked())
            return true;
        return !container_supports_audio_codec(std::string(container_box_ptr->get_selected_id()), source_info.audio_codec);
    }

    void ExportPage::view_changed(bool advanced_view) {
        this->advanced_view = advanced_view;

        if(container_section_ptr)
            container_section_ptr->set_visible(advanced_view);
        if(video_codec_ptr)
            video_codec_ptr->set_visible(advanced_view);
        if(audio_codec_ptr)
            audio_codec_ptr->set_visible(advanced_view);
        if(framerate_ptr)
            framerate_ptr->set_visible(advanced_view);
        if(video_quality_ptr)
            video_quality_ptr->set_visible(true);
        if(video_bitrate_ptr)
            video_bitrate_ptr->set_visible(use_constant_video_bitrate());
        if(audio_bitrate_ptr)
            audio_bitrate_ptr->set_visible(advanced_view);

        update_reencode_options_visibility();
    }

    void ExportPage::update_settings_scrollable_size() {
        if(!page_list_ptr || !settings_scrollable_page_ptr || !settings_list_ptr)
            return;

        const float margin_top = std::floor(margin_top_scale * get_theme().window_height);
        const float margin_bottom = std::floor(margin_bottom_scale * get_theme().window_height);
        const float page_spacing = std::floor(0.018f * get_theme().window_height);
        const float max_inner_page_height = std::max(0.0f, std::floor(get_theme().window_height * 0.7f) - margin_top - margin_bottom - get_border_size());
        const float fixed_page_height = view_radio_button_ptr ? (view_radio_button_ptr->get_size().y + page_spacing) : 0.0f;
        const float max_scrollable_height = std::max(0.0f, max_inner_page_height - fixed_page_height);
        const float visible_settings_height = settings_list_ptr->get_size().y;
        const float scrollable_height = std::min(visible_settings_height, max_scrollable_height);
        settings_scrollable_page_ptr->set_size(mgl::vec2f(get_settings_page_width(), scrollable_height).floor());
    }

    bool ExportPage::start_export() {
        std::vector<TimelineWidget::TimelineChunk> selected_chunks;
        if(source_info.chunks.empty()) {
            const int64_t full_duration_ms = std::max<int64_t>(0, (int64_t)std::llround(source_info.metadata.duration_seconds * 1000.0));
            if(full_duration_ms > 0)
                selected_chunks.push_back({0, full_duration_ms, true});
        } else {
            selected_chunks.reserve(source_info.chunks.size());
            for(const auto &chunk : source_info.chunks) {
                if(chunk.enabled && chunk.end_ms > chunk.start_ms)
                    selected_chunks.push_back(chunk);
            }
        }

        if(selected_chunks.empty()) {
            show_export_notification(TR("No video segment selected to export"), false);
            return false;
        }

        std::string output_directory(save_directory_button_ptr->get_text());
        if(output_directory.empty())
            output_directory = get_parent_directory(source_info.metadata.filepath);

        std::string output_directory_copy = output_directory;
        if(create_directory_recursive(output_directory_copy.data()) != 0) {
            show_export_notification(TR("Failed to create export directory"), false);
            return false;
        }

        const std::string output_extension = container_to_file_extension(std::string(container_box_ptr->get_selected_id()));
        std::string output_path = output_directory + "/Trimmed_" + get_date_str() + "." + output_extension;
        for(int suffix = 1; std::filesystem::exists(output_path); ++suffix)
            output_path = output_directory + "/Trimmed_" + get_date_str() + "_" + std::to_string(suffix) + "." + output_extension;

        ExportRequest request;
        request.input_path = source_info.metadata.filepath;
        request.output_path = output_path;
        request.container = std::string(container_box_ptr->get_selected_id());
        request.video_codec = std::string(video_codec_box_ptr->get_selected_id());
        request.audio_codec = std::string(audio_codec_box_ptr->get_selected_id());
        request.video_bitrate = std::string(video_bitrate_entry_ptr->get_text());
        request.audio_bitrate = std::string(audio_bitrate_entry_ptr->get_text());
        request.chunks = std::move(selected_chunks);
        request.reencode_video = is_video_reencode_active();
        request.reencode_audio = is_audio_reencode_active();
        request.has_audio_stream = !source_info.audio_codec.empty();

        const ResolutionPreset *resolution_preset = find_resolution_preset(video_resolution_box_ptr->get_selected_id());
        const mgl::vec2i scaled_video_size = get_scaled_video_size({ source_info.metadata.width, source_info.metadata.height }, resolution_preset);
        request.video_width = scaled_video_size.x;
        request.video_height = scaled_video_size.y;

        std::string video_codec = choose_default_video_codec(request, source_info);
        std::string audio_codec = request.audio_codec.empty() ? get_default_audio_codec_for_container(request.container) : request.audio_codec;

        if(request.reencode_video) {
            double target_fps = 0.0;
            if(!parse_positive_double(framerate_entry_ptr->get_text(), target_fps)) {
                show_export_notification(TR("Frame rate must be a positive number"), false);
                return false;
            }

            if(video_codec.empty()) {
                show_export_notification(TR("No compatible video codec is available for the selected export settings"), false);
                return false;
            }
            if(!container_supports_video_codec(request.container, video_codec)) {
                show_export_notification(TR("The selected video codec is not compatible with the selected container"), false);
                return false;
            }
            if(!host_supports_video_codec(video_codec)) {
                show_export_notification(TR("The selected video codec is not available in host ffmpeg"), false);
                return false;
            }
        } else if(!container_supports_video_codec(request.container, source_info.video_codec)) {
            show_export_notification(TR("Enable video re-encoding or select a compatible container for the source video codec"), false);
            return false;
        }

        if(request.has_audio_stream) {
            if(request.reencode_audio) {
                if(audio_codec.empty()) {
                    show_export_notification(TR("No compatible audio codec is available for the selected export settings"), false);
                    return false;
                }
                if(!container_supports_audio_reencode_codec(request.container, audio_codec)) {
                    show_export_notification(TR("The selected audio codec is not compatible with the selected container"), false);
                    return false;
                }
                if(!host_supports_audio_codec(audio_codec)) {
                    show_export_notification(TR("The selected audio codec is not available in host ffmpeg"), false);
                    return false;
                }
            } else if(!container_supports_audio_codec(request.container, source_info.audio_codec)) {
                show_export_notification(TR("Enable audio re-encoding or select a compatible container for the source audio codec"), false);
                return false;
            }
        }

        const SourceVideoInfo source_info_copy = source_info;
        const std::string export_dir = get_state_dir() + "/trimmer-export";
        std::string export_dir_buffer = export_dir;
        create_directory_recursive(export_dir_buffer.data());

        const std::string export_id = std::to_string(std::hash<std::string>{}(request.output_path + request.input_path + std::to_string(time(NULL))));
        const std::string concat_path = export_dir + "/" + export_id + ".ffconcat";
        const std::string script_path = export_dir + "/" + export_id + ".sh";

        std::string concat_data = "ffconcat version 1.0\n";
        for(const auto &chunk : request.chunks) {
            concat_data += "file ";
            concat_data += escape_ffconcat_path(request.input_path);
            concat_data += "\n";
            concat_data += "inpoint ";
            concat_data += format_seconds((double)chunk.start_ms / 1000.0);
            concat_data += "\n";
            concat_data += "outpoint ";
            concat_data += format_seconds((double)chunk.end_ms / 1000.0);
            concat_data += "\n";
        }

        if(!file_overwrite(concat_path.c_str(), concat_data)) {
            show_export_notification(TR("Failed to prepare trimmed video export"), false);
            return false;
        }

        std::vector<std::string> args_str = {
            "ffmpeg", "-loglevel", "error", "-y",
            "-safe", "0",
            "-f", "concat",
            "-i", concat_path,
            "-map", "0:v:0",
            "-map", "0:a:0?"
        };

        if(request.reencode_video) {
            double target_fps = 0.0;
            parse_positive_double(framerate_entry_ptr->get_text(), target_fps);
            args_str.push_back("-c:v");
            args_str.push_back(map_ffmpeg_video_encoder(video_codec));
            if(should_override_fps(source_info, target_fps, framerate_modified)) {
                args_str.push_back("-r");
                args_str.push_back(format_fps_value(target_fps));
            }
            if(request.video_width > 0 && request.video_height > 0 &&
                (request.video_width != source_info.metadata.width || request.video_height != source_info.metadata.height)) {
                args_str.push_back("-vf");
                args_str.push_back("scale=w=" + std::to_string(request.video_width) + ":h=" + std::to_string(request.video_height) + ":force_original_aspect_ratio=decrease:force_divisible_by=2:flags=lanczos");
            }
            if(use_constant_video_bitrate()) {
                args_str.push_back("-b:v");
                args_str.push_back(std::to_string(get_target_video_bitrate_kbps()) + "k");
            } else {
                const std::string selected_quality = std::string(video_quality_box_ptr->get_selected_id());
                const int crf = get_quality_preset_crf(video_codec, selected_quality);
                args_str.push_back("-crf");
                args_str.push_back(std::to_string(crf));
                if(quality_preset_uses_zero_bitrate(video_codec)) {
                    args_str.push_back("-b:v");
                    args_str.push_back("0");
                }
            }
        } else {
            args_str.push_back("-c:v");
            args_str.push_back("copy");
        }

        if(request.has_audio_stream) {
            if(request.reencode_audio) {
                args_str.push_back("-c:a");
                args_str.push_back(map_ffmpeg_audio_encoder(audio_codec));
                args_str.push_back("-b:a");
                args_str.push_back(request.audio_bitrate + "k");
            } else {
                args_str.push_back("-c:a");
                args_str.push_back("copy");
            }
        }

        if(request.container == "mp4" || request.container == "mov") {
            args_str.push_back("-movflags");
            args_str.push_back("+faststart");
        }

        args_str.push_back(request.output_path);

        std::string ffmpeg_command;
        for(size_t i = 0; i < args_str.size(); ++i) {
            if(i > 0)
                ffmpeg_command += ' ';
            ffmpeg_command += shell_quote(args_str[i]);
        }

        const std::string success_text = std::string(TR("Trimmed video exported:\n")) + request.output_path;
        const std::string failure_text = TR("Failed to export trimmed video");
        const std::string success_bg = color_to_hex_str(get_color_theme().tint_color);
        const std::string script =
            std::string("#!/bin/sh\n") +
            ffmpeg_command + "\n" +
            "status=$?\n" +
            "rm -f -- " + shell_quote(concat_path) + "\n" +
            "if [ \"$status\" -eq 0 ]; then\n" +
            "  gsr-notify --text " + shell_quote(success_text) + " --timeout 3.000000 --icon-color 'ffffff' --bg-color " + shell_quote(success_bg) + " --icon record\n" +
            "else\n" +
            "  rm -f -- " + shell_quote(request.output_path) + "\n" +
            "  gsr-notify --text " + shell_quote(failure_text) + " --timeout 5.000000 --icon-color 'ff0000' --bg-color 'ff0000' --icon " + shell_quote(std::string(GSR_UI_RESOURCES_PATH) + "/images/gsr-ui.png") + "\n" +
            "fi\n" +
            "rm -f -- \"$0\"\n";

        if(!file_overwrite(script_path.c_str(), script)) {
            std::filesystem::remove(concat_path);
            show_export_notification(TR("Failed to prepare trimmed video export"), false);
            return false;
        }

        chmod(script_path.c_str(), 0700);
        const char *args[] = { "sh", script_path.c_str(), nullptr };
        if(!exec_program_on_host_daemonized(args, false)) {
            std::filesystem::remove(script_path);
            std::filesystem::remove(concat_path);
            show_export_notification(TR("Failed to start trimmed video export"), false);
            return false;
        }

        page_stack->pop();
        return true;
    }

    void ExportPage::update_reencode_options_visibility() {
        const std::string container = std::string(container_box_ptr->get_selected_id());

        if(is_video_reencode_active()) {
            const std::string selected_video_codec = std::string(video_codec_box_ptr->get_selected_id());
            if(selected_video_codec.empty() || !container_supports_video_codec(container, selected_video_codec)) {
                ExportRequest request;
                request.container = container;
                request.video_codec = "auto";
                request.reencode_video = true;
                const std::string default_video_codec = choose_default_video_codec(request, source_info);
                if(!default_video_codec.empty() && container_supports_video_codec(container, default_video_codec))
                    video_codec_box_ptr->set_selected_item(default_video_codec);
            }
        }

        if(is_audio_reencode_active()) {
            const std::string selected_audio_codec = std::string(audio_codec_box_ptr->get_selected_id());
            if(selected_audio_codec.empty() || !container_supports_audio_reencode_codec(container, selected_audio_codec)) {
                const std::string default_audio_codec = get_default_audio_codec_for_container(container);
                if(!default_audio_codec.empty())
                    audio_codec_box_ptr->set_selected_item(default_audio_codec);
            }
        }

        if(video_resolution_ptr)
            video_resolution_ptr->set_visible(is_video_reencode_active());
        if(video_bitrate_ptr)
            video_bitrate_ptr->set_visible(is_video_reencode_active() && use_constant_video_bitrate());
        video_reencode_options_ptr->set_visible(is_video_reencode_active());
        audio_reencode_options_ptr->set_visible(advanced_view && reencode_audio_checkbox_ptr->is_checked());
        update_settings_scrollable_size();
    }

    void ExportPage::apply_video_quality_preset(bool force_bitrate_update) {
        if(!use_constant_video_bitrate())
            return;

        if(video_bitrate_manually_modified && !last_manual_video_bitrate_text.empty()) {
            changing_video_bitrate_programmatically = true;
            video_bitrate_entry_ptr->set_text(last_manual_video_bitrate_text);
            changing_video_bitrate_programmatically = false;
            return;
        }

        if(use_constant_video_bitrate())
            apply_selected_resolution_preset(force_bitrate_update);
    }

    void ExportPage::apply_selected_resolution_preset(bool force_bitrate_update) {
        const ResolutionPreset *preset = find_resolution_preset(video_resolution_box_ptr->get_selected_id());
        if(!preset)
            return;

        if(!use_constant_video_bitrate())
            return;

        if(!force_bitrate_update && video_bitrate_manually_modified)
            return;

        const int64_t source_bitrate_kbps = std::max<int64_t>(1, get_estimated_video_bitrate_guess_kbps(source_info));
        const mgl::vec2i scaled_video_size = get_scaled_video_size({ source_info.metadata.width, source_info.metadata.height }, preset);
        int64_t target_bitrate_kbps = scale_bitrate_for_resolution(
            source_bitrate_kbps,
            source_info.metadata.width,
            source_info.metadata.height,
            scaled_video_size.x,
            scaled_video_size.y);

        double target_fps = 0.0;
        if(!parse_target_fps_for_estimation(framerate_entry_ptr->get_text(), target_fps))
            return;
        target_bitrate_kbps = scale_bitrate_for_fps(target_bitrate_kbps, source_info.fps, target_fps);

        changing_video_bitrate_programmatically = true;
        video_bitrate_entry_ptr->set_text(std::to_string(std::max<int64_t>(1, target_bitrate_kbps)));
        changing_video_bitrate_programmatically = false;
        video_bitrate_manually_modified = false;
    }

    bool ExportPage::use_constant_video_bitrate() const {
        return std::string(video_quality_box_ptr->get_selected_id()) == "custom";
    }

    int64_t ExportPage::get_target_video_bitrate_kbps() const {
        return std::max<int64_t>(1, sv_to_int<int64_t>(video_bitrate_entry_ptr->get_text()));
    }

    int64_t ExportPage::get_selected_duration_ms() const {
        if(source_info.chunks.empty())
            return std::max<int64_t>(0, (int64_t)std::llround(source_info.metadata.duration_seconds * 1000.0));

        int64_t duration_ms = 0;
        for(const auto &chunk : source_info.chunks) {
            if(chunk.enabled && chunk.end_ms > chunk.start_ms)
                duration_ms += chunk.end_ms - chunk.start_ms;
        }

        return duration_ms;
    }

    int64_t ExportPage::get_source_trimmed_size_bytes(int64_t selected_duration_ms) const {
        if(selected_duration_ms <= 0 || source_info.metadata.file_size <= 0 || source_info.metadata.duration_seconds <= 0.0)
            return 0;

        return (int64_t)std::llround((double)source_info.metadata.file_size * ((double)selected_duration_ms / (source_info.metadata.duration_seconds * 1000.0)));
    }

    void ExportPage::update_source_summary() {
        const std::string total_bitrate_suffix = source_info.total_bitrate_kbps > 0
            ? std::string(", ") + std::to_string(source_info.total_bitrate_kbps) + "Kbps total"
            : std::string();
        const std::string video_bitrate_suffix = source_info.has_video_bitrate
            ? std::string(" (") + std::to_string(source_info.video_bitrate_kbps) + "Kbps)"
            : std::string();
        const std::string audio_bitrate_suffix = source_info.has_audio_bitrate
            ? std::string(" (") + std::to_string(source_info.audio_bitrate_kbps) + "Kbps)"
            : std::string();

        char buffer[1024];
        snprintf(buffer, sizeof(buffer),
            TR("Source video: %dx%d, %s, %s%s.\nSelected trim duration: %s.\nSource codecs: video %s%s, audio %s%s."),
            source_info.metadata.width,
            source_info.metadata.height,
            format_duration((int64_t)std::llround(source_info.metadata.duration_seconds * 1000.0)).c_str(),
            format_file_size(source_info.metadata.file_size).c_str(),
            total_bitrate_suffix.c_str(),
            format_duration(get_selected_duration_ms()).c_str(),
            humanize_video_codec(source_info.video_codec).c_str(),
            video_bitrate_suffix.c_str(),
            humanize_audio_codec(source_info.audio_codec).c_str(),
            audio_bitrate_suffix.c_str());
        source_summary_label_ptr->set_text(buffer);
        update_settings_scrollable_size();
    }

    void ExportPage::update_estimated_file_size() {
        const int64_t selected_duration_ms = get_selected_duration_ms();
        if(selected_duration_ms <= 0) {
            estimated_file_size_ptr->set_text(TR("Estimated output file size unavailable."));
            return;
        }

        const bool reencode_video = is_video_reencode_active();
        const bool reencode_audio = is_audio_reencode_active();
        double target_fps = 0.0;
        if(reencode_video && !parse_target_fps_for_estimation(framerate_entry_ptr->get_text(), target_fps)) {
            estimated_file_size_ptr->set_text(TR("Estimated output file size unavailable.\nFrame rate must be a positive number."));
            update_settings_scrollable_size();
            return;
        }

        int64_t estimated_size_bytes = 0;
        if(!reencode_video && !reencode_audio && source_info.metadata.file_size > 0 && source_info.metadata.duration_seconds > 0.0) {
            estimated_size_bytes = get_source_trimmed_size_bytes(selected_duration_ms);
        } else {
            int64_t video_bitrate_kbps = 0;
            int64_t audio_bitrate_kbps = 0;

            if(reencode_video) {
                if(use_constant_video_bitrate()) {
                    video_bitrate_kbps = get_target_video_bitrate_kbps();
                } else {
                    const ResolutionPreset *preset = find_resolution_preset(video_resolution_box_ptr->get_selected_id());
                    ExportRequest request;
                    request.container = std::string(container_box_ptr->get_selected_id());
                    request.video_codec = std::string(video_codec_box_ptr->get_selected_id());
                    request.reencode_video = true;

                    video_bitrate_kbps = estimate_quality_preset_video_bitrate_kbps(
                        source_info,
                        preset,
                        choose_default_video_codec(request, source_info),
                        video_quality_box_ptr->get_selected_id(),
                        target_fps);
                }
            } else {
                video_bitrate_kbps = std::max<int64_t>(0, get_estimated_video_bitrate_guess_kbps(source_info));
            }

            if(reencode_audio) {
                audio_bitrate_kbps = std::max<int64_t>(0, sv_to_int<int64_t>(audio_bitrate_entry_ptr->get_text()));
            } else {
                audio_bitrate_kbps = std::max<int64_t>(0, get_estimated_audio_bitrate_guess_kbps(source_info));
            }

            int64_t total_bitrate_kbps = video_bitrate_kbps + audio_bitrate_kbps;
            if(total_bitrate_kbps <= 0)
                total_bitrate_kbps = source_info.total_bitrate_kbps;
            if(total_bitrate_kbps > 0) {
                estimated_size_bytes = (int64_t)std::llround(((double)selected_duration_ms / 1000.0) * ((double)total_bitrate_kbps * 1000.0 / 8.0));
                estimated_size_bytes = (int64_t)std::llround((double)estimated_size_bytes * 1.024);
            } else {
                estimated_size_bytes = get_source_trimmed_size_bytes(selected_duration_ms);
            }
        }

        char buffer[512];
        const bool user_enabled_video_reencode = reencode_video_checkbox_ptr->is_checked();
        const bool user_enabled_audio_reencode = reencode_audio_checkbox_ptr->is_checked();
        const bool forced_video_reencode = !reencode_video_checkbox_ptr->is_checked() && reencode_video;
        const bool forced_audio_reencode = !reencode_audio_checkbox_ptr->is_checked() && reencode_audio;
        const bool only_forced_reencode = (forced_video_reencode || forced_audio_reencode)
            && !user_enabled_video_reencode && !user_enabled_audio_reencode;
        const bool quality_preset_video_estimate = reencode_video && !use_constant_video_bitrate();
        const bool bitrate_based_estimate = (reencode_video && use_constant_video_bitrate()) || reencode_audio;
        if(!reencode_video && !reencode_audio) {
            snprintf(buffer, sizeof(buffer),
                TR("Estimated trimmed file size without re-encoding: %s.\nThis is based on the selected chunks relative to the original file size."),
                format_file_size(estimated_size_bytes).c_str());
        } else {
            if(quality_preset_video_estimate) {
                if(only_forced_reencode) {
                    snprintf(buffer, sizeof(buffer),
                        TR("Estimated output file size: %s.\nThe selected container requires compatible codec re-encoding for this export. This is approximate and based on the selected trim duration, resolution and quality preset. The estimate may be off by about 25%%."),
                        format_file_size(estimated_size_bytes).c_str());
                } else {
                    snprintf(buffer, sizeof(buffer),
                        TR("Estimated output file size: %s.\nThis is approximate and based on the selected trim duration, resolution and quality preset. The estimate may be off by about 25%%."),
                        format_file_size(estimated_size_bytes).c_str());
                }
            } else if(bitrate_based_estimate) {
                snprintf(buffer, sizeof(buffer),
                    TR("Estimated output file size: %s.\nThis is approximate and based on the selected trim duration and target bitrates."),
                    format_file_size(estimated_size_bytes).c_str());
            } else {
                snprintf(buffer, sizeof(buffer),
                    TR("Estimated output file size: %s."),
                    format_file_size(estimated_size_bytes).c_str());
            }
        }
        estimated_file_size_ptr->set_text(buffer);
        update_settings_scrollable_size();
    }

    mgl::vec2f ExportPage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const float margin_top = std::floor(margin_top_scale * get_theme().window_height);
        const float margin_bottom = std::floor(margin_bottom_scale * get_theme().window_height);
        const float margin_left = std::floor(margin_left_scale * get_theme().window_height);
        const float margin_right = std::floor(margin_right_scale * get_theme().window_height);
        const mgl::vec2f settings_size = page_list_ptr ? page_list_ptr->get_size() : mgl::vec2f(get_settings_content_width(), 0.0f);
        const mgl::vec2f page_size = settings_size + mgl::vec2f(margin_left + margin_right, margin_top + margin_bottom + get_border_size());
        return mgl::vec2f(page_size.x, std::min(page_size.y, std::floor(get_theme().window_height * 0.7f))).floor();
    }

    mgl::vec2f ExportPage::get_inner_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_bottom = margin_bottom_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        const int margin_right = margin_right_scale * get_theme().window_height;
        return get_size() - mgl::vec2f(margin_left + margin_right, margin_top + margin_bottom + get_border_size());
    }

    void ExportPage::set_margins(float top, float bottom, float left, float right) {
        margin_top_scale = top;
        margin_bottom_scale = bottom;
        margin_left_scale = left;
        margin_right_scale = right;
    }

    void ExportPage::add_button(const std::string &text, const std::string &id, mgl::Color color) {
        auto button = std::make_unique<Button>(get_theme().title_font_desc.c_str(), text.c_str(),
            mgl::vec2f(get_theme().window_width / 10, get_theme().window_height / 15).floor(), color);
        button->set_border_scale(0.003f);
        button->on_click = [this, id]() {
            if(on_click)
                on_click(id);
        };
        buttons.push_back({ std::move(button), id });
    }

    float ExportPage::get_border_size() const {
        return 0.004f * get_theme().window_height;
    }

    float ExportPage::get_horizontal_spacing() const {
        return get_theme().window_width / 50;
    }

    float ExportPage::get_settings_page_width() const {
        const float margin_left = std::floor(margin_left_scale * get_theme().window_height);
        const float margin_right = std::floor(margin_right_scale * get_theme().window_height);
        const float preferred_page_width = std::floor(get_theme().window_width * 0.4f);
        return std::max(0.0f, preferred_page_width - margin_left - margin_right);
    }

    float ExportPage::get_settings_content_width() const {
        if(settings_scrollable_page_ptr)
            return settings_scrollable_page_ptr->get_inner_size().x;

        return get_settings_page_width();
    }

    mgl::vec2f ExportPage::get_content_position() {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = get_size();
        return mgl::vec2f(
            window_size.x * 0.5f - content_page_size.x * 0.5f,
            window_size.y * 0.5f - window_size.y * 0.7f * 0.5f
        ).floor();
    }
}
