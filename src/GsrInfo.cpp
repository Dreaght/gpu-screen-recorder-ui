#include "../include/GsrInfo.hpp"
#include <string.h>
#include <functional>

namespace gsr {
    using StringSplitCallback = std::function<bool(std::string_view line)>;

    static void string_split_char(const std::string &str, char delimiter, StringSplitCallback callback_func) {
        size_t index = 0;
        while(index < str.size()) {
            size_t new_index = str.find(delimiter, index);
            if(new_index == std::string::npos)
                new_index = str.size();

            if(!callback_func({str.data() + index, new_index - index}))
                break;

            index = new_index + 1;
        }
    }

    static void parse_system_info_line(GsrInfo *gsr_info, const std::string &line) {
        const size_t space_index = line.find(' ');
        if(space_index == std::string::npos)
            return;

        const std::string_view attribute_name = {line.c_str(), space_index};
        const std::string_view attribute_value = {line.c_str() + space_index + 1, line.size() - (space_index + 1)};
        if(attribute_name == "display_server") {
            if(attribute_value == "x11")
                gsr_info->system_info.display_server = DisplayServer::X11;
            else if(attribute_value == "wayland")
                gsr_info->system_info.display_server = DisplayServer::WAYLAND;
        }
    }

    static void parse_gpu_info_line(GsrInfo *gsr_info, const std::string &line) {
        const size_t space_index = line.find(' ');
        if(space_index == std::string::npos)
            return;

        const std::string_view attribute_name = {line.c_str(), space_index};
        const std::string_view attribute_value = {line.c_str() + space_index + 1, line.size() - (space_index + 1)};
        if(attribute_name == "vendor") {
            if(attribute_value == "amd")
                gsr_info->gpu_info.vendor = GpuVendor::AMD;
            else if(attribute_value == "intel")
                gsr_info->gpu_info.vendor = GpuVendor::INTEL;
            else if(attribute_value == "nvidia")
                gsr_info->gpu_info.vendor = GpuVendor::NVIDIA;
        }
    }

    static void parse_video_codecs_line(GsrInfo *gsr_info, const std::string &line) {
        if(line == "h264")
            gsr_info->supported_video_codecs.h264 = true;
        else if(line == "hevc")
            gsr_info->supported_video_codecs.hevc = true;
        else if(line == "av1")
            gsr_info->supported_video_codecs.av1 = true;
        else if(line == "vp8")
            gsr_info->supported_video_codecs.vp8 = true;
        else if(line == "vp9")
            gsr_info->supported_video_codecs.vp9 = true;
    }

    static GsrMonitor capture_option_line_to_monitor(const std::string &line) {
        size_t space_index = line.find(' ');
        if(space_index == std::string::npos)
            return { line, {0, 0} };

        mgl::vec2i size = {0, 0};
        if(sscanf(line.c_str() + space_index + 1, "%dx%d", &size.x, &size.y) != 2)
            size = {0, 0};

        return { line.substr(0, space_index), size };
    }

    static void parse_capture_options_line(GsrInfo *gsr_info, const std::string &line) {
        if(line == "window")
            gsr_info->supported_capture_options.window = true;
        else if(line == "focused")
            gsr_info->supported_capture_options.focused = true;
        else if(line == "screen")
            gsr_info->supported_capture_options.screen = true;
        else if(line == "portal")
            gsr_info->supported_capture_options.portal = true;
        else
            gsr_info->supported_capture_options.monitors.push_back(capture_option_line_to_monitor(line));
    }

    enum class GsrInfoSection {
        UNKNOWN,
        SYSTEM_INFO,
        GPU_INFO,
        VIDEO_CODECS,
        CAPTURE_OPTIONS
    };

    static bool starts_with(const std::string &str, const char *substr) {
        size_t len = strlen(substr);
        return str.size() >= len && memcmp(str.data(), substr, len) == 0;
    }

    GsrInfoExitStatus get_gpu_screen_recorder_info(GsrInfo *gsr_info) {
        *gsr_info = GsrInfo{};

        FILE *f = popen("gpu-screen-recorder --info", "r");
        if(!f) {
            fprintf(stderr, "error: 'gpu-screen-recorder --info' failed\n");
            return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND;
        }

        char output[8192];
        ssize_t bytes_read = fread(output, 1, sizeof(output) - 1, f);
        if(bytes_read < 0 || ferror(f)) {
            fprintf(stderr, "error: failed to read 'gpu-screen-recorder --info' output\n");
            pclose(f);
            return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND;
        }
        output[bytes_read] = '\0';

        GsrInfoSection section = GsrInfoSection::UNKNOWN;
        string_split_char(output, '\n', [&](std::string_view line) {
            const std::string line_str(line.data(), line.size());

            if(starts_with(line_str, "section=")) {
                const char *section_name = line_str.c_str() + 8;
                if(strcmp(section_name, "system_info") == 0)
                    section = GsrInfoSection::SYSTEM_INFO;
                else if(strcmp(section_name, "gpu_info") == 0)
                    section = GsrInfoSection::GPU_INFO;
                else if(strcmp(section_name, "video_codecs") == 0)
                    section = GsrInfoSection::VIDEO_CODECS;
                else if(strcmp(section_name, "capture_options") == 0)
                    section = GsrInfoSection::CAPTURE_OPTIONS;
                else
                    section = GsrInfoSection::UNKNOWN;
                return true;
            }

            switch(section) {
                case GsrInfoSection::UNKNOWN: {
                    break;
                }
                case GsrInfoSection::SYSTEM_INFO: {
                    parse_system_info_line(gsr_info, line_str);
                    break;
                }
                case GsrInfoSection::GPU_INFO: {
                    parse_gpu_info_line(gsr_info, line_str);
                    break;
                }
                case GsrInfoSection::VIDEO_CODECS: {
                    parse_video_codecs_line(gsr_info, line_str);
                    break;
                }
                case GsrInfoSection::CAPTURE_OPTIONS: {
                    parse_capture_options_line(gsr_info, line_str);
                    break;
                }
            }

            return true;
        });

        int status = pclose(f);
        if(WIFEXITED(status)) {
            switch(WEXITSTATUS(status)) {
                case 0:  return GsrInfoExitStatus::OK;
                case 22: return GsrInfoExitStatus::OPENGL_FAILED;
                case 23: return GsrInfoExitStatus::NO_DRM_CARD;
                default: return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND;
            }
        }

        return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND;
    }
}