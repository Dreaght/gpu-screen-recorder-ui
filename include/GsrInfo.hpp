#pragma once

#include <string>
#include <vector>

#include <mglpp/system/vec.hpp>

namespace gsr {
    struct SupportedVideoCodecs {
        bool h264 = false;
        bool h264_software = false;
        bool hevc = false;
        bool hevc_hdr = false;
        bool hevc_10bit = false;
        bool av1 = false;
        bool av1_hdr = false;
        bool av1_10bit = false;
        bool vp8 = false;
        bool vp9 = false;
    };

    struct GsrMonitor {
        std::string name;
        mgl::vec2i size;
    };

    struct SupportedCaptureOptions {
        bool window = false;
        bool focused = false;
        bool screen = false;
        bool portal = false;
        std::vector<GsrMonitor> monitors;
    };

    enum class DisplayServer {
        UNKNOWN,
        X11,
        WAYLAND
    };

    struct SystemInfo {
        DisplayServer display_server = DisplayServer::UNKNOWN;
    };

    enum class GpuVendor {
        UNKNOWN,
        AMD,
        INTEL,
        NVIDIA
    };

    struct GpuInfo {
        GpuVendor vendor = GpuVendor::UNKNOWN;
    };

    struct GsrInfo {
        SystemInfo system_info;
        GpuInfo gpu_info;
        SupportedVideoCodecs supported_video_codecs;
        SupportedCaptureOptions supported_capture_options;
    };

    enum class GsrInfoExitStatus {
        OK,
        BROKEN_DRIVERS,
        FAILED_TO_RUN_COMMAND,
        OPENGL_FAILED,
        NO_DRM_CARD
    };

    struct AudioDevice {
        std::string name;
        std::string description;
    };

    GsrInfoExitStatus get_gpu_screen_recorder_info(GsrInfo *gsr_info);

    std::vector<AudioDevice> get_audio_devices();
}