#pragma once

#include "gui/StaticPage.hpp"
#include "GsrInfo.hpp"

#include <functional>

namespace gsr {
    class ScrollablePage;
    class List;

    class SettingsPage {
    public:
        enum class Type {
            REPLAY,
            RECORD,
            STREAM
        };

        SettingsPage(Type type, const GsrInfo &gsr_info, const std::vector<AudioDevice> &audio_devices, std::function<void()> back_button_callback);
        SettingsPage(const SettingsPage&) = delete;
        SettingsPage& operator=(const SettingsPage&) = delete;

        Page& get_page();
    private:
        void add_widgets(const gsr::GsrInfo &gsr_info, const std::vector<gsr::AudioDevice> &audio_devices, std::function<void()> back_button_callback);
        void add_page_specific_widgets();
        void add_replay_widgets();
        void add_record_widgets();
        void add_stream_widgets();
    private:
        StaticPage page;
        ScrollablePage *content_page_ptr = nullptr;
        List *settings_list_ptr = nullptr;
        Type type;
    };
}