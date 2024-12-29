#pragma once

#include "StaticPage.hpp"
#include "../GsrInfo.hpp"
#include "../Config.hpp"

#include <functional>

namespace gsr {
    class GsrPage;
    class PageStack;
    class ScrollablePage;
    class Subsection;
    class RadioButton;
    class Button;

    class GlobalSettingsPage : public StaticPage {
    public:
        GlobalSettingsPage(const GsrInfo *gsr_info, Config &config, PageStack *page_stack);
        GlobalSettingsPage(const GlobalSettingsPage&) = delete;
        GlobalSettingsPage& operator=(const GlobalSettingsPage&) = delete;

        void load();
        void save();
        void on_navigate_away_from_page() override;

        // Called with (enable, exit_status)
        std::function<void(bool, int)> on_startup_changed;
        // Called with (reason)
        std::function<void(const char*)> on_click_exit_program_button;
    private:
        std::unique_ptr<Subsection> create_appearance_subsection(ScrollablePage *parent_page);
        std::unique_ptr<Subsection> create_startup_subsection(ScrollablePage *parent_page);
        std::unique_ptr<Button> create_exit_program_button();
        std::unique_ptr<Button> create_go_back_to_old_ui_button();
        std::unique_ptr<Subsection> create_application_options_subsection(ScrollablePage *parent_page);
        void add_widgets();
    private:
        Config &config;
        const GsrInfo *gsr_info = nullptr;

        GsrPage *content_page_ptr = nullptr;
        PageStack *page_stack = nullptr;
        RadioButton *tint_color_radio_button_ptr = nullptr;
        RadioButton *startup_radio_button_ptr = nullptr;
    };
}