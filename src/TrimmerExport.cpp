#include "../include/TrimmerExport.hpp"
#include "../include/Process.hpp"
#include "../include/Theme.hpp"
#include "../include/Translation.hpp"
#include "../include/Utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <sys/stat.h>
#include <time.h>
#include <unordered_map>
#include <unordered_set>

namespace gsr {
    namespace {
        static const ResolutionPreset resolution_presets[] = {
            { "Source (Recommended)", "source", 0, 0 },
            { "3840x2160 (4K)", "3840x2160", 3840, 2160 },
            { "2560x1440", "2560x1440", 2560, 1440 },
            { "1920x1080", "1920x1080", 1920, 1080 },
            { "1280x720", "1280x720", 1280, 720 },
            { "854x480", "854x480", 854, 480 },
            { "640x360", "640x360", 640, 360 }
        };

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

        static int64_t scale_bitrate_for_resolution_impl(int64_t bitrate_kbps, int source_width, int source_height, int target_width, int target_height) {
            if(bitrate_kbps <= 0 || source_width <= 0 || source_height <= 0 || target_width <= 0 || target_height <= 0)
                return bitrate_kbps;

            const double source_pixels = (double)source_width * (double)source_height;
            const double target_pixels = (double)target_width * (double)target_height;
            if(source_pixels <= 0.0 || target_pixels <= 0.0)
                return bitrate_kbps;

            return std::max<int64_t>(1, (int64_t)std::llround((double)bitrate_kbps * (target_pixels / source_pixels)));
        }

        static int64_t scale_bitrate_for_fps_impl(int64_t bitrate_kbps, double source_fps, double target_fps) {
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
    }

    int64_t scale_bitrate_for_resolution(int64_t bitrate_kbps, int source_width, int source_height, int target_width, int target_height) {
        return scale_bitrate_for_resolution_impl(bitrate_kbps, source_width, source_height, target_width, target_height);
    }

    int64_t scale_bitrate_for_fps(int64_t bitrate_kbps, double source_fps, double target_fps) {
        return scale_bitrate_for_fps_impl(bitrate_kbps, source_fps, target_fps);
    }

    TrimmerExportSourceInfo load_trimmer_export_source_info(VideoMetadata metadata, std::vector<TimelineWidget::TimelineChunk> chunks) {
        TrimmerExportSourceInfo source_info;
        source_info.metadata = std::move(metadata);
        source_info.chunks = std::move(chunks);

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
        return source_info;
    }

    std::string get_container_from_filepath(const std::string &filepath) {
        const std::string extension = std::filesystem::path(filepath).extension().string();
        if(extension == ".mkv")
            return "matroska";
        if(extension == ".webm")
            return "webm";
        if(extension == ".mov")
            return "mov";
        return "mp4";
    }

    std::string container_to_file_extension(const std::string &container) {
        if(container == "matroska")
            return "mkv";
        return container;
    }

    std::string map_video_codec_to_option_id(const std::string &codec) {
        if(codec == "h264" || codec == "hevc" || codec == "av1" || codec == "vp8" || codec == "vp9" || codec == "h264_software")
            return codec;
        return "auto";
    }

    std::string map_audio_codec_to_option_id(const std::string &codec) {
        if(codec == "aac" || codec == "opus")
            return codec;
        return "aac";
    }

    std::string choose_default_video_codec(const TrimmerExportRequest &request, const TrimmerExportSourceInfo &source_info) {
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

    bool host_supports_video_codec(const std::string &codec) {
        return host_supports_ffmpeg_encoder(map_ffmpeg_video_encoder(codec));
    }

    bool host_supports_audio_codec(const std::string &codec) {
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

    bool container_supports_video_codec(const std::string &container, const std::string &codec) {
        if(container == "webm")
            return codec == "vp8" || codec == "vp9" || codec == "av1";
        if(container == "mp4")
            return codec == "h264" || codec == "hevc" || codec == "av1";
        if(container == "mov")
            return codec == "h264" || codec == "hevc";
        return true;
    }

    bool container_supports_audio_codec(const std::string &container, const std::string &codec) {
        if(container == "webm")
            return codec == "opus";

        if(container == "mov")
            return codec == "aac";

        return host_supports_audio_codec_in_container(container, codec);
    }

    bool container_supports_audio_reencode_codec(const std::string &container, const std::string &codec) {
        if(container == "webm")
            return codec == "opus";

        return host_supports_audio_codec_in_container(container, codec);
    }

    std::string get_default_audio_codec_for_container(const std::string &container) {
        if(container == "webm")
            return host_supports_audio_codec("opus") ? "opus" : "";

        if(container == "mov") {
            if(container_supports_audio_reencode_codec(container, "aac"))
                return "aac";
            if(container_supports_audio_reencode_codec(container, "opus"))
                return "opus";
            return "";
        }

        if(container_supports_audio_reencode_codec(container, "opus"))
            return "opus";
        if(container_supports_audio_reencode_codec(container, "aac"))
            return "aac";
        return "";
    }

    const ResolutionPreset* get_resolution_presets(size_t &count) {
        count = std::size(resolution_presets);
        return resolution_presets;
    }

    const ResolutionPreset* find_resolution_preset(std::string_view id) {
        for(const ResolutionPreset &preset : resolution_presets) {
            if(id == preset.id)
                return &preset;
        }
        return nullptr;
    }

    mgl::vec2i get_scaled_video_size(mgl::vec2i source_size, const ResolutionPreset *preset) {
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

    int64_t get_estimated_audio_bitrate_guess_kbps(const TrimmerExportSourceInfo &source_info) {
        if(source_info.has_audio_bitrate)
            return source_info.audio_bitrate_kbps;
        if(source_info.audio_codec.empty())
            return 0;
        if(source_info.total_bitrate_kbps > 0)
            return std::min<int64_t>(128, std::max<int64_t>(32, source_info.total_bitrate_kbps / 8));
        return 128;
    }

    int64_t get_estimated_video_bitrate_guess_kbps(const TrimmerExportSourceInfo &source_info) {
        if(source_info.has_video_bitrate)
            return source_info.video_bitrate_kbps;
        if(source_info.total_bitrate_kbps > 0)
            return std::max<int64_t>(1, source_info.total_bitrate_kbps - get_estimated_audio_bitrate_guess_kbps(source_info));
        return source_info.video_bitrate_kbps;
    }

    bool should_override_fps(const TrimmerExportSourceInfo &source_info, double target_fps, bool framerate_modified) {
        if(!framerate_modified || target_fps <= 0.0)
            return false;
        if(source_info.fps <= 0.0)
            return true;
        return std::abs(target_fps - source_info.fps) > 0.01;
    }

    int get_quality_preset_crf(std::string_view codec, std::string_view quality) {
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

    bool quality_preset_uses_zero_bitrate(std::string_view codec) {
        return codec == "av1" || codec == "vp8" || codec == "vp9";
    }

    int64_t estimate_quality_preset_video_bitrate_kbps(const TrimmerExportSourceInfo &source_info, const ResolutionPreset *preset,
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

    bool start_trimmer_export(const TrimmerExportRequest &request, const TrimmerExportSourceInfo &source_info,
        const TrimmerExportNotificationConfig &notification_config, bool use_constant_video_bitrate,
        std::string_view selected_quality, std::string_view framerate_text, bool framerate_modified, std::string &error_text)
    {
        std::string video_codec = choose_default_video_codec(request, source_info);
        std::string audio_codec = request.audio_codec.empty() ? get_default_audio_codec_for_container(request.container) : request.audio_codec;

        if(request.reencode_video) {
            double target_fps = 0.0;
            if(!parse_positive_double(framerate_text, target_fps)) {
                error_text = TR("Frame rate must be a positive number");
                return false;
            }

            if(video_codec.empty()) {
                error_text = TR("No compatible video codec is available for the selected export settings");
                return false;
            }
            if(!container_supports_video_codec(request.container, video_codec)) {
                error_text = TR("The selected video codec is not compatible with the selected container");
                return false;
            }
            if(!host_supports_video_codec(video_codec)) {
                error_text = TR("The selected video codec is not available in host ffmpeg");
                return false;
            }
        } else if(!container_supports_video_codec(request.container, source_info.video_codec)) {
            error_text = TR("Enable video re-encoding or select a compatible container for the source video codec");
            return false;
        }

        if(request.has_audio_stream) {
            if(request.reencode_audio) {
                if(audio_codec.empty()) {
                    error_text = TR("No compatible audio codec is available for the selected export settings");
                    return false;
                }
                if(!container_supports_audio_reencode_codec(request.container, audio_codec)) {
                    error_text = TR("The selected audio codec is not compatible with the selected container");
                    return false;
                }
                if(!host_supports_audio_codec(audio_codec)) {
                    error_text = TR("The selected audio codec is not available in host ffmpeg");
                    return false;
                }
            } else if(!container_supports_audio_codec(request.container, source_info.audio_codec)) {
                error_text = TR("Enable audio re-encoding or select a compatible container for the source audio codec");
                return false;
            }
        }

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
            error_text = TR("Failed to prepare trimmed video export");
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
            parse_positive_double(framerate_text, target_fps);
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
            if(use_constant_video_bitrate) {
                args_str.push_back("-b:v");
                args_str.push_back(request.video_bitrate + "k");
            } else {
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
        const std::string success_bg = notification_config.success_background_color.empty()
            ? color_to_hex_str(get_color_theme().tint_color)
            : notification_config.success_background_color;
        const std::string failure_icon = notification_config.failure_icon_path.empty()
            ? std::string(GSR_UI_RESOURCES_PATH) + "/images/gsr-ui.png"
            : notification_config.failure_icon_path;
        const std::string script =
            std::string("#!/bin/sh\n") +
            ffmpeg_command + "\n" +
            "status=$?\n" +
            "rm -f -- " + shell_quote(concat_path) + "\n" +
            "if [ \"$status\" -eq 0 ]; then\n" +
            "  gsr-notify --text " + shell_quote(success_text) + " --timeout 3.000000 --icon-color 'ffffff' --bg-color " + shell_quote(success_bg) + " --icon record\n" +
            "else\n" +
            "  rm -f -- " + shell_quote(request.output_path) + "\n" +
            "  gsr-notify --text " + shell_quote(failure_text) + " --timeout 5.000000 --icon-color 'ff0000' --bg-color 'ff0000' --icon " + shell_quote(failure_icon) + "\n" +
            "fi\n" +
            "rm -f -- \"$0\"\n";

        if(!file_overwrite(script_path.c_str(), script)) {
            std::filesystem::remove(concat_path);
            error_text = TR("Failed to prepare trimmed video export");
            return false;
        }

        chmod(script_path.c_str(), 0700);
        const char *args[] = { "sh", script_path.c_str(), nullptr };
        if(!exec_program_on_host_daemonized(args, false)) {
            std::filesystem::remove(script_path);
            std::filesystem::remove(concat_path);
            error_text = TR("Failed to start trimmed video export");
            return false;
        }

        return true;
    }
}
