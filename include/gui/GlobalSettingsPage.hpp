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

        std::function<void(bool enable, int exit_status)> on_startup_changed;
        std::function<void(const char *reason)> on_click_exit_program_button;
        std::function<void(const char *hotkey_option)> on_keyboard_hotkey_changed;
        std::function<void(const char *hotkey_option)> on_joystick_hotkey_changed;
    private:
        std::unique_ptr<Subsection> create_appearance_subsection(ScrollablePage *parent_page);
        std::unique_ptr<Subsection> create_startup_subsection(ScrollablePage *parent_page);
        std::unique_ptr<RadioButton> create_enable_keyboard_hotkeys_button();
        std::unique_ptr<RadioButton> create_enable_joystick_hotkeys_button();
        std::unique_ptr<Subsection> create_hotkey_subsection(ScrollablePage *parent_page);
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
        RadioButton *enable_keyboard_hotkeys_radio_button_ptr = nullptr;
        RadioButton *enable_joystick_hotkeys_radio_button_ptr = nullptr;
    };
}