#include "../include/RecentVideos.hpp"
#include "../include/Utils.hpp"
#include "../include/Process.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <stdio.h>
#include <sys/stat.h>

namespace gsr {
    static constexpr size_t max_recent_videos = 16;

    static std::mutex recent_videos_mutex;

    static std::string get_recent_video_thumbnail_path(const std::string &video_path) {
        struct stat st;
        std::string key = video_path;
        if(stat(video_path.c_str(), &st) == 0)
            key += ":" + std::to_string((int64_t)st.st_mtim.tv_sec) + ":" + std::to_string((int64_t)st.st_size);

        const std::string cache_dir = get_cache_dir() + "/recent-video-thumbnails";
        char cache_dir_buffer[4096];
        snprintf(cache_dir_buffer, sizeof(cache_dir_buffer), "%s", cache_dir.c_str());
        create_directory_recursive(cache_dir_buffer);
        return cache_dir + "/" + std::to_string(std::hash<std::string>{}(key)) + ".jpg";
    }

    static std::string get_recent_videos_filepath() {
        return get_state_dir() + "/recent_videos";
    }

    static std::optional<std::vector<std::string>> get_recent_video_paths_no_lock() {
        const std::string recent_videos_filepath = get_recent_videos_filepath();
        FILE *file = fopen(recent_videos_filepath.c_str(), "rb");
        if(!file)
            return std::vector<std::string>();

        std::string file_content;
        if(!file_get_content(recent_videos_filepath.c_str(), file_content)) {
            fclose(file);
            return std::nullopt;
        }

        fclose(file);

        std::vector<std::string> result;

        string_split_char(file_content, '\n', [&result](std::string_view line) {
            if(line.empty())
                return true;

            std::string filepath(line);
            if(!filepath.empty() && filepath.back() == '\r')
                filepath.pop_back();

            if(filepath.empty())
                return true;

            result.push_back(std::move(filepath));
            return result.size() < max_recent_videos;
        });

        return result;
    }

    static bool save_recent_video_paths_no_lock(const std::vector<std::string> &filepaths) {
        const std::string recent_videos_filepath = get_recent_videos_filepath();
        std::string state_dir = get_state_dir();
        if(create_directory_recursive(state_dir.data()) != 0) {
            fprintf(stderr, "Warning: Failed to create state directory: %s\n", state_dir.c_str());
            return false;
        }

        std::string data;
        for(const std::string &filepath : filepaths) {
            data += filepath;
            data += '\n';
        }

        if(!file_overwrite(recent_videos_filepath.c_str(), data)) {
            fprintf(stderr, "Warning: Failed to write recent videos file: %s\n", recent_videos_filepath.c_str());
            return false;
        }

        return true;
    }

    static void parse_recent_video_metadata(VideoMetadata &recent_video) {
        struct stat st;
        if(stat(recent_video.filepath.c_str(), &st) == 0)
            recent_video.file_size = st.st_size;

        const char *args[] = {
            "ffprobe",
            "-v", "error",
            "-select_streams", "v:0",
            "-show_entries", "stream=width,height:format=duration",
            "-of", "default=noprint_wrappers=1:nokey=0",
            recent_video.filepath.c_str(),
            nullptr
        };

        std::string output;
        if(exec_program_on_host_get_stdout(args, output, false) != 0)
            return;

        std::istringstream iss(output);
        std::string line;
        while(std::getline(iss, line)) {
            if(starts_with(line, "width=")) {
                int width = 0;
                if(sscanf(line.c_str() + 6, "%d", &width) == 1)
                    recent_video.width = width;
            } else if(starts_with(line, "height=")) {
                int height = 0;
                if(sscanf(line.c_str() + 7, "%d", &height) == 1)
                    recent_video.height = height;
            } else if(starts_with(line, "duration=")) {
                double duration_seconds = 0.0;
                if(sscanf(line.c_str() + 9, "%lf", &duration_seconds) == 1)
                    recent_video.duration_seconds = duration_seconds;
            }
        }

        if(recent_video.width <= 0 || recent_video.height <= 0)
            return;

        recent_video.thumbnail_path = get_recent_video_thumbnail_path(recent_video.filepath);
        if(std::filesystem::exists(recent_video.thumbnail_path))
            return;

        const double seek_seconds = std::clamp(recent_video.duration_seconds * 0.35, 0.0, std::max(0.0, recent_video.duration_seconds - 0.25));
        char seek_seconds_str[64];
        snprintf(seek_seconds_str, sizeof(seek_seconds_str), "%.3f", seek_seconds);

        const char *thumbnail_args[] = {
            "ffmpeg",
            "-loglevel", "error",
            "-y",
            "-ss", seek_seconds_str,
            "-i", recent_video.filepath.c_str(),
            "-frames:v", "1",
            "-vf", "scale=320:-2",
            recent_video.thumbnail_path.c_str(),
            nullptr
        };

        std::string ffmpeg_output;
        if(exec_program_on_host_get_stdout(thumbnail_args, ffmpeg_output, false) != 0)
            recent_video.thumbnail_path.clear();
    }

    bool add_recent_video(const std::string &filepath) {
        if(filepath.empty())
            return false;

        std::lock_guard<std::mutex> lock(recent_videos_mutex);

        const std::optional<std::vector<std::string>> filepaths_opt = get_recent_video_paths_no_lock();
        if(!filepaths_opt)
            return false;

        std::vector<std::string> filepaths = filepaths_opt.value();
        filepaths.erase(std::remove(filepaths.begin(), filepaths.end(), filepath), filepaths.end());
        filepaths.insert(filepaths.begin(), filepath);

        if(filepaths.size() > max_recent_videos)
            filepaths.resize(max_recent_videos);

        return save_recent_video_paths_no_lock(filepaths);
    }

    std::optional<std::vector<VideoMetadata>> get_recent_videos() {
        std::lock_guard<std::mutex> lock(recent_videos_mutex);

        const std::optional<std::vector<std::string>> filepaths_opt = get_recent_video_paths_no_lock();
        if(!filepaths_opt)
            return std::nullopt;

        const std::vector<std::string> &filepaths = filepaths_opt.value();
        std::vector<VideoMetadata> result;
        result.reserve(filepaths.size());

        for(const std::string &filepath : filepaths) {
            VideoMetadata recent_video;
            recent_video.filepath = filepath;
            parse_recent_video_metadata(recent_video);
            result.push_back(std::move(recent_video));
        }

        return result;
    }
}
