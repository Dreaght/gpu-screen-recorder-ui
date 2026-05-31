#pragma once

#include <optional>
#include <string>
#include <vector>

namespace gsr {
    struct VideoMetadata {
        std::string filepath;
        std::string thumbnail_path;
        int64_t file_size = 0;
        int32_t width = 0;
        int32_t height = 0;
        double duration_seconds = 0.0;
    };

    bool add_recent_video(const std::string &filepath);
    bool purge_recent_video_cache(const std::vector<std::string> &extra_thumbnail_paths_to_keep = {});
    std::optional<std::vector<VideoMetadata>> get_recent_videos();
}
