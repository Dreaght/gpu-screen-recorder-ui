#include "../include/GsrInfo.hpp"
#include "../include/Utils.hpp"
#include "../include/Process.hpp"

#include <optional>
#include <string.h>

namespace gsr {
    bool GsrVersion::operator>(const GsrVersion &other) const {
        return major > other.major || (major == other.major && minor > other.minor) || (major == other.major && minor == other.minor && patch > other.patch);
    }

    bool GsrVersion::operator>=(const GsrVersion &other) const {
        return major >= other.major || (major == other.major && minor >= other.minor) || (major == other.major && minor == other.minor && patch >= other.patch);
    }

    bool GsrVersion::operator<(const GsrVersion &other) const {
        return !operator>=(other);
    }

    bool GsrVersion::operator<=(const GsrVersion &other) const {
        return !operator>(other);
    }

    bool GsrVersion::operator==(const GsrVersion &other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }

    bool GsrVersion::operator!=(const GsrVersion &other) const {
        return !operator==(other);
    }

    std::string GsrVersion::to_string() const {
        std::string result;
        if(major == 0 && minor == 0 && patch == 0)
            result = "Unknown";
        else
            result = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
        return result;
    }

    /* Returns -1 on error */
    static int parse_u8(const char *str, int size) {
        if(size <= 0)
            return -1;

        int result = 0;
        for(int i = 0; i < size; ++i) {
            char c = str[i];
            if(c >= '0' && c <= '9') {
                result = result * 10 + (c - '0');
                if(result > 255)
                    return -1;
            } else {
                return -1;
            }
        }
        return result;
    }

    static GsrVersion parse_gsr_version(const std::string_view str) {
        GsrVersion result;
        uint8_t numbers[3];
        int number_index = 0;

        size_t index = 0;
        while(true) {
            size_t next_index = str.find('.', index);
            if(next_index == std::string::npos)
                next_index = str.size();

            const int number = parse_u8(str.data() + index, next_index - index);
            if(number == -1) {
                fprintf(stderr, "Error: gpu-screen-recorder --info contains invalid gsr version: %.*s\n", (int)str.size(), str.data());
                return {0, 0, 0};
            }

            if(number_index >= 3) {
                fprintf(stderr, "Error: gpu-screen-recorder --info contains invalid gsr version: %.*s\n", (int)str.size(), str.data());
                return {0, 0, 0};
            }

            numbers[number_index] = number;
            ++number_index;
            index = next_index + 1;
            if(next_index == str.size())
                break;
        }

        result.major = numbers[0];
        result.minor = numbers[1];
        result.patch = numbers[2];
        return result;
    }

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
        } else if(key_value->key == "gsr_version") {
            gsr_info->system_info.gsr_version = parse_gsr_version(key_value->value);
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

    static void parse_image_formats_line(GsrInfo *gsr_info, std::string_view line) {
        if(line == "jpeg")
            gsr_info->supported_image_formats.jpeg = true;
        else if(line == "png")
            gsr_info->supported_image_formats.png = true;
    }

    enum class GsrInfoSection {
        UNKNOWN,
        SYSTEM_INFO,
        GPU_INFO,
        VIDEO_CODECS,
        IMAGE_FORMATS,
        CAPTURE_OPTIONS
    };

    static bool starts_with(std::string_view str, const char *substr) {
        size_t len = strlen(substr);
        return str.size() >= len && memcmp(str.data(), substr, len) == 0;
    }

    GsrInfoExitStatus get_gpu_screen_recorder_info(GsrInfo *gsr_info) {
        *gsr_info = GsrInfo{};

        std::string stdout_str;
        const char *args[] = { "gpu-screen-recorder", "--info", nullptr };
        const int exit_status = exec_program_get_stdout(args, stdout_str);
        switch(exit_status) {
            case 0:  break;
            case 14: return GsrInfoExitStatus::BROKEN_DRIVERS;
            case 22: return GsrInfoExitStatus::OPENGL_FAILED;
            case 23: return GsrInfoExitStatus::NO_DRM_CARD;
            default: return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND;
        }

        GsrInfoSection section = GsrInfoSection::UNKNOWN;
        string_split_char(stdout_str, '\n', [&](std::string_view line) {
            if(starts_with(line, "section=")) {
                const std::string_view section_name = line.substr(8);
                if(section_name == "system_info")
                    section = GsrInfoSection::SYSTEM_INFO;
                else if(section_name == "gpu_info")
                    section = GsrInfoSection::GPU_INFO;
                else if(section_name == "video_codecs")
                    section = GsrInfoSection::VIDEO_CODECS;
                else if(section_name == "image_formats")
                    section = GsrInfoSection::IMAGE_FORMATS;
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
                case GsrInfoSection::IMAGE_FORMATS: {
                    parse_image_formats_line(gsr_info, line);
                    break;
                }
                case GsrInfoSection::CAPTURE_OPTIONS: {
                    // Intentionally ignore, get capture options with get_supported_capture_options instead
                    break;
                }
            }

            return true;
        });

        return GsrInfoExitStatus::OK;
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

        std::string stdout_str;
        const char *args[] = { "gpu-screen-recorder", "--list-audio-devices", nullptr };
        if(exec_program_get_stdout(args, stdout_str, false) != 0) {
            fprintf(stderr, "error: 'gpu-screen-recorder --list-audio-devices' failed\n");
            return audio_devices;
        }

        string_split_char(stdout_str, '\n', [&](std::string_view line) {
            std::optional<AudioDevice> audio_device = parse_audio_device_line(line);
            if(audio_device)
                audio_devices.push_back(std::move(audio_device.value()));
            return true;
        });

        return audio_devices;
    }

    std::vector<std::string> get_application_audio() {
        std::vector<std::string> application_audio;

        std::string stdout_str;
        const char *args[] = { "gpu-screen-recorder", "--list-application-audio", nullptr };
        if(exec_program_get_stdout(args, stdout_str) != 0) {
            fprintf(stderr, "error: 'gpu-screen-recorder --list-application-audio' failed\n");
            return application_audio;
        }

        string_split_char(stdout_str, '\n', [&](std::string_view line) {
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
        else if(line == "region")
            capture_options.region = true;
        else if(line == "focused")
            capture_options.focused = true;
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

        std::string stdout_str;
        const char *args[] = { "gpu-screen-recorder", "--list-capture-options", gsr_info.gpu_info.card_path.c_str(), gpu_vendor_to_string(gsr_info.gpu_info.vendor), nullptr };
        if(exec_program_get_stdout(args, stdout_str) != 0) {
            fprintf(stderr, "error: 'gpu-screen-recorder --list-capture-options' failed\n");
            return capture_options;
        }

        string_split_char(stdout_str, '\n', [&](std::string_view line) {
            parse_capture_options_line(capture_options, line);
            return true;
        });

        return capture_options;
    }
}
