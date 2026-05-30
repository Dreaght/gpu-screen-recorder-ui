#pragma once

#include "GsrInfo.hpp"
#include "RecentVideos.hpp"
#include "gui/TimelineWidget.hpp"

#include <mglpp/system/vec.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gsr {
    struct TrimmerExportSourceInfo {
        VideoMetadata metadata;
        std::vector<TimelineWidget::TimelineChunk> chunks;
        std::string container = "mp4";
        std::string video_codec;
        std::string audio_codec;
        int64_t total_bitrate_kbps = 0;
        int64_t video_bitrate_kbps = 0;
        int64_t audio_bitrate_kbps = 0;
        double fps = 0.0;
        bool has_video_bitrate = false;
        bool has_audio_bitrate = false;
    };

    struct TrimmerExportRequest {
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
        GpuVendor gpu_vendor = GpuVendor::UNKNOWN;
        std::string gpu_card_path;
        SupportedVideoCodecs supported_video_codecs;
    };

    struct ResolutionPreset {
        const char *label;
        const char *id;
        int width;
        int height;
    };

    struct TrimmerExportNotificationConfig {
        std::string success_background_color;
        std::string failure_icon_path;
    };

    TrimmerExportSourceInfo load_trimmer_export_source_info(VideoMetadata metadata, std::vector<TimelineWidget::TimelineChunk> chunks);

    std::string get_container_from_filepath(const std::string &filepath);
    std::string container_to_file_extension(const std::string &container);
    std::string map_video_codec_to_option_id(const std::string &codec);
    std::string map_audio_codec_to_option_id(const std::string &codec);
    std::string choose_default_video_codec(const TrimmerExportRequest &request, const TrimmerExportSourceInfo &source_info);
    std::string get_default_audio_codec_for_container(const std::string &container);

    bool host_supports_video_codec(const std::string &codec);
    bool host_supports_video_codec(const std::string &codec, const TrimmerExportRequest &request);
    bool host_supports_hardware_video_codec(const std::string &codec, const TrimmerExportRequest &request);
    bool host_supports_audio_codec(const std::string &codec);
    bool container_supports_video_codec(const std::string &container, const std::string &codec);
    bool container_supports_audio_codec(const std::string &container, const std::string &codec);
    bool container_supports_audio_reencode_codec(const std::string &container, const std::string &codec);

    const ResolutionPreset* get_resolution_presets(size_t &count);
    const ResolutionPreset* find_resolution_preset(std::string_view id);
    mgl::vec2i get_scaled_video_size(mgl::vec2i source_size, const ResolutionPreset *preset);

    int64_t get_estimated_audio_bitrate_guess_kbps(const TrimmerExportSourceInfo &source_info);
    int64_t get_estimated_video_bitrate_guess_kbps(const TrimmerExportSourceInfo &source_info);
    bool should_override_fps(const TrimmerExportSourceInfo &source_info, double target_fps, bool framerate_modified);
    int64_t scale_bitrate_for_resolution(int64_t bitrate_kbps, int source_width, int source_height, int target_width, int target_height);
    int64_t scale_bitrate_for_fps(int64_t bitrate_kbps, double source_fps, double target_fps);
    int get_quality_preset_crf(std::string_view codec, std::string_view quality);
    bool quality_preset_uses_zero_bitrate(std::string_view codec);
    int64_t estimate_quality_preset_video_bitrate_kbps(const TrimmerExportSourceInfo &source_info, const ResolutionPreset *preset,
        const std::string &target_codec, std::string_view quality, double target_fps);

    bool start_trimmer_export(const TrimmerExportRequest &request, const TrimmerExportSourceInfo &source_info,
        const TrimmerExportNotificationConfig &notification_config, bool use_constant_video_bitrate,
        std::string_view selected_quality, std::string_view framerate_text, bool framerate_modified, std::string &error_text);
}
