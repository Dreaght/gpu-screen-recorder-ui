#pragma once

#include <optional>
#include <string>
#include <vector>

namespace gsr {
    struct VideoMetadata {
        std::string filepath;
        int64_t file_size = 0;
        int32_t width = 0;
        int32_t height = 0;
        double duration_seconds = 0.0;
    };

    bool add_recent_video(const std::string &filepath);
    std::optional<std::vector<VideoMetadata>> get_recent_videos();
}
