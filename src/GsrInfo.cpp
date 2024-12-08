#include "../include/GsrInfo.hpp"
#include "../include/Utils.hpp"
#include <optional>
#include <string.h>

namespace gsr {
    static std::optional<KeyValue> parse_key_value(std::string_view line) {
        const size_t space_index = line.find('|');
        if(space_index == std::string_view::npos)
            return std::nullopt;
        return KeyValue{line.substr(0, space_index), line.substr(space_index + 1)};
    }

    static void parse_system_info_line(GsrInfo *gsr_info, std::string_view line) {
        const std::optional<KeyValue> key_value = parse_key_value(line);
        if(!key_value)
            return;

        if(key_value->key == "display_server") {
            if(key_value->value == "x11")
                gsr_info->system_info.display_server = DisplayServer::X11;
            else if(key_value->value == "wayland")
                gsr_info->system_info.display_server = DisplayServer::WAYLAND;
        } else if(key_value->key == "supports_app_audio") {
            gsr_info->system_info.supports_app_audio = key_value->value == "yes";
        }
    }

    static void parse_gpu_info_line(GsrInfo *gsr_info, std::string_view line) {
        const std::optional<KeyValue> key_value = parse_key_value(line);
        if(!key_value)
            return;

        if(key_value->key == "vendor") {
            if(key_value->value == "amd")
                gsr_info->gpu_info.vendor = GpuVendor::AMD;
            else if(key_value->value == "intel")
                gsr_info->gpu_info.vendor = GpuVendor::INTEL;
            else if(key_value->value == "nvidia")
                gsr_info->gpu_info.vendor = GpuVendor::NVIDIA;
        } else if(key_value->key == "card_path") {
            gsr_info->gpu_info.card_path = key_value->value;
        }
    }

    static void parse_video_codecs_line(GsrInfo *gsr_info, std::string_view line) {
        if(line == "h264")
            gsr_info->supported_video_codecs.h264 = true;
        else if(line == "h264_software")
            gsr_info->supported_video_codecs.h264_software = true;
        else if(line == "hevc")
            gsr_info->supported_video_codecs.hevc = true;
        else if(line == "hevc_hdr")
            gsr_info->supported_video_codecs.hevc_hdr = true;
        else if(line == "hevc_10bit")
            gsr_info->supported_video_codecs.hevc_10bit = true;
        else if(line == "av1")
            gsr_info->supported_video_codecs.av1 = true;
        else if(line == "av1_hdr")
            gsr_info->supported_video_codecs.av1_hdr = true;
        else if(line == "av1_10bit")
            gsr_info->supported_video_codecs.av1_10bit = true;
        else if(line == "vp8")
            gsr_info->supported_video_codecs.vp8 = true;
        else if(line == "vp9")
            gsr_info->supported_video_codecs.vp9 = true;
    }

    enum class GsrInfoSection {
        UNKNOWN,
        SYSTEM_INFO,
        GPU_INFO,
        VIDEO_CODECS,
        CAPTURE_OPTIONS
    };

    static bool starts_with(std::string_view str, const char *substr) {
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
        string_split_char({output, (size_t)bytes_read}, '\n', [&](std::string_view line) {
            if(starts_with(line, "section=")) {
                const std::string_view section_name = line.substr(8);
                if(section_name == "system_info")
                    section = GsrInfoSection::SYSTEM_INFO;
                else if(section_name == "gpu_info")
                    section = GsrInfoSection::GPU_INFO;
                else if(section_name == "video_codecs")
                    section = GsrInfoSection::VIDEO_CODECS;
                else if(section_name == "capture_options")
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
                    parse_system_info_line(gsr_info, line);
                    break;
                }
                case GsrInfoSection::GPU_INFO: {
                    parse_gpu_info_line(gsr_info, line);
                    break;
                }
                case GsrInfoSection::VIDEO_CODECS: {
                    parse_video_codecs_line(gsr_info, line);
                    break;
                }
                case GsrInfoSection::CAPTURE_OPTIONS: {
                    // Intentionally ignore, get capture options with get_supported_capture_options instead
                    break;
                }
            }

            return true;
        });

        int status = pclose(f);
        if(WIFEXITED(status)) {
            switch(WEXITSTATUS(status)) {
                case 0:  return GsrInfoExitStatus::OK;
                case 14: return GsrInfoExitStatus::BROKEN_DRIVERS;
                case 22: return GsrInfoExitStatus::OPENGL_FAILED;
                case 23: return GsrInfoExitStatus::NO_DRM_CARD;
                default: return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND;
            }
        }

        return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND;
    }

    static std::optional<AudioDevice> parse_audio_device_line(std::string_view line) {
        std::optional<AudioDevice> audio_device;
        const std::optional<KeyValue> key_value = parse_key_value(line);
        if(!key_value)
            return audio_device;

        audio_device = AudioDevice{std::string(key_value->key), std::string(key_value->value)};
        return audio_device;
    }

    std::vector<AudioDevice> get_audio_devices() {
        std::vector<AudioDevice> audio_devices;

        FILE *f = popen("gpu-screen-recorder --list-audio-devices", "r");
        if(!f) {
            fprintf(stderr, "error: 'gpu-screen-recorder --list-audio-devices' failed\n");
            return audio_devices;
        }

        char output[16384];
        ssize_t bytes_read = fread(output, 1, sizeof(output) - 1, f);
        if(bytes_read < 0 || ferror(f)) {
            fprintf(stderr, "error: failed to read 'gpu-screen-recorder --list-audio-devices' output\n");
            pclose(f);
            return audio_devices;
        }
        output[bytes_read] = '\0';

        string_split_char({output, (size_t)bytes_read}, '\n', [&](std::string_view line) {
            std::optional<AudioDevice> audio_device = parse_audio_device_line(line);
            if(audio_device)
                audio_devices.push_back(std::move(audio_device.value()));
            return true;
        });

        return audio_devices;
    }

    std::vector<std::string> get_application_audio() {
        std::vector<std::string> application_audio;

        FILE *f = popen("gpu-screen-recorder --list-application-audio", "r");
        if(!f) {
            fprintf(stderr, "error: 'gpu-screen-recorder --list-application-audio' failed\n");
            return application_audio;
        }

        char output[8192];
        ssize_t bytes_read = fread(output, 1, sizeof(output) - 1, f);
        if(bytes_read < 0 || ferror(f)) {
            fprintf(stderr, "error: failed to read 'gpu-screen-recorder --list-application-audio' output\n");
            pclose(f);
            return application_audio;
        }
        output[bytes_read] = '\0';

        string_split_char({output, (size_t)bytes_read}, '\n', [&](std::string_view line) {
            application_audio.emplace_back(line);
            return true;
        });

        return application_audio;
    }

    static std::optional<GsrMonitor> capture_option_line_to_monitor(std::string_view line) {
        std::optional<GsrMonitor> monitor;
        const std::optional<KeyValue> key_value = parse_key_value(line);
        if(!key_value)
            return monitor;

        char value_buffer[256];
        snprintf(value_buffer, sizeof(value_buffer), "%.*s", (int)key_value->value.size(), key_value->value.data());

        monitor = GsrMonitor{std::string(key_value->key), mgl::vec2i{0, 0}};
        if(sscanf(value_buffer, "%dx%d", &monitor->size.x, &monitor->size.y) != 2)
            monitor->size = {0, 0};

        return monitor;
    }

    static void parse_capture_options_line(SupportedCaptureOptions &capture_options, std::string_view line) {
        if(line == "window")
            capture_options.window = true;
        else if(line == "focused")
            capture_options.focused = true;
        else if(line == "screen")
            capture_options.screen = true;
        else if(line == "portal")
            capture_options.portal = true;
        else {
            std::optional<GsrMonitor> monitor = capture_option_line_to_monitor(line);
            if(monitor)
                capture_options.monitors.push_back(std::move(monitor.value()));
        }
    }

    static const char* gpu_vendor_to_string(GpuVendor vendor) {
        switch(vendor) {
            case GpuVendor::UNKNOWN: return "unknown";
            case GpuVendor::AMD:     return "amd";
            case GpuVendor::INTEL:   return "intel";
            case GpuVendor::NVIDIA:  return "nvidia";
        }
        return "unknown";
    }

    SupportedCaptureOptions get_supported_capture_options(const GsrInfo &gsr_info) {
        SupportedCaptureOptions capture_options;

        char command[512];
        snprintf(command, sizeof(command), "gpu-screen-recorder --list-capture-options %s %s", gsr_info.gpu_info.card_path.c_str(), gpu_vendor_to_string(gsr_info.gpu_info.vendor));

        FILE *f = popen(command, "r");
        if(!f) {
            fprintf(stderr, "error: 'gpu-screen-recorder --list-capture-options' failed\n");
            return capture_options;
        }

        char output[8192];
        ssize_t bytes_read = fread(output, 1, sizeof(output) - 1, f);
        if(bytes_read < 0 || ferror(f)) {
            fprintf(stderr, "error: failed to read 'gpu-screen-recorder --list-capture-options' output\n");
            pclose(f);
            return capture_options;
        }
        output[bytes_read] = '\0';

        string_split_char({output, (size_t)bytes_read}, '\n', [&](std::string_view line) {
            parse_capture_options_line(capture_options, line);
            return true;
        });

        return capture_options;
    }
}
