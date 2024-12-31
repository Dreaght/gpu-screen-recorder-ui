#include "../../include/gui/GlobalSettingsPage.hpp"

#include "../../include/Theme.hpp"
#include "../../include/Process.hpp"
#include "../../include/gui/GsrPage.hpp"
#include "../../include/gui/PageStack.hpp"
#include "../../include/gui/ScrollablePage.hpp"
#include "../../include/gui/Subsection.hpp"
#include "../../include/gui/List.hpp"
#include "../../include/gui/Label.hpp"
#include "../../include/gui/RadioButton.hpp"

namespace gsr {
    static const char* gpu_vendor_to_color_name(GpuVendor vendor) {
        switch(vendor) {
            case GpuVendor::UNKNOWN: return "amd";
            case GpuVendor::AMD:     return "amd";
            case GpuVendor::INTEL:   return "intel";
            case GpuVendor::NVIDIA:  return "nvidia";
        }
        return "amd";
    }

    GlobalSettingsPage::GlobalSettingsPage(const GsrInfo *gsr_info, Config &config, PageStack *page_stack) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        config(config),
        gsr_info(gsr_info),
        page_stack(page_stack)
    {
        auto content_page = std::make_unique<GsrPage>();
        content_page->add_button("Back", "back", get_color_theme().page_bg_color);
        content_page->on_click = [page_stack](const std::string &id) {
            if(id == "back")
                page_stack->pop();
        };
        content_page_ptr = content_page.get();
        add_widget(std::move(content_page));

        add_widgets();
        load();
    }

    std::unique_ptr<Subsection> GlobalSettingsPage::create_appearance_subsection(ScrollablePage *parent_page) {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Tint color", get_color_theme().text_color));
        auto tint_color_radio_button = std::make_unique<RadioButton>(&get_theme().body_font, RadioButton::Orientation::HORIZONTAL);
        tint_color_radio_button_ptr = tint_color_radio_button.get();
        tint_color_radio_button->add_item("Red", "amd");
        tint_color_radio_button->add_item("Green", "nvidia");
        tint_color_radio_button->add_item("blue", "intel");
        tint_color_radio_button->on_selection_changed = [](const std::string&, const std::string &id) {
            if(id == "amd")
                get_color_theme().tint_color = mgl::Color(221, 0, 49);
            else if(id == "nvidia")
                get_color_theme().tint_color = mgl::Color(118, 185, 0);
            else if(id == "intel")
                get_color_theme().tint_color = mgl::Color(8, 109, 183);
            return true;
        };
        list->add_widget(std::move(tint_color_radio_button));
        return std::make_unique<Subsection>("Appearance", std::move(list), mgl::vec2f(parent_page->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<Subsection> GlobalSettingsPage::create_startup_subsection(ScrollablePage *parent_page) {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(std::make_unique<Label>(&get_theme().body_font, "Start program on system startup?", get_color_theme().text_color));
        auto startup_radio_button = std::make_unique<RadioButton>(&get_theme().body_font, RadioButton::Orientation::HORIZONTAL);
        startup_radio_button_ptr = startup_radio_button.get();
        startup_radio_button->add_item("Yes", "start_on_system_startup");
        startup_radio_button->add_item("No", "dont_start_on_system_startup");
        startup_radio_button->on_selection_changed = [&](const std::string&, const std::string &id) {
            bool enable = false;
            if(id == "dont_start_on_system_startup")
                enable = false;
            else if(id == "start_on_system_startup")
                enable = true;
            else
                return false;

            const char *args[] = { "systemctl", enable ? "enable" : "disable", "--user", "gpu-screen-recorder-ui", nullptr };
            std::string stdout_str;
            const int exit_status = exec_program_on_host_get_stdout(args, stdout_str);
            if(on_startup_changed)
                on_startup_changed(enable, exit_status);
            return exit_status == 0;
        };
        list->add_widget(std::move(startup_radio_button));
        return std::make_unique<Subsection>("Startup", std::move(list), mgl::vec2f(parent_page->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<Button> GlobalSettingsPage::create_exit_program_button() {
        auto exit_program_button = std::make_unique<Button>(&get_theme().body_font, "Exit program", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        exit_program_button->on_click = [&]() {
            if(on_click_exit_program_button)
                on_click_exit_program_button("exit");
        };
        return exit_program_button;
    }

    std::unique_ptr<Button> GlobalSettingsPage::create_go_back_to_old_ui_button() {
        auto exit_program_button = std::make_unique<Button>(&get_theme().body_font, "Go back to the old UI", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        exit_program_button->on_click = [&]() {
            if(on_click_exit_program_button)
                on_click_exit_program_button("back-to-old-ui");
        };
        return exit_program_button;
    }

    std::unique_ptr<Subsection> GlobalSettingsPage::create_application_options_subsection(ScrollablePage *parent_page) {
        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;

        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        list->add_widget(create_exit_program_button());
        if(inside_flatpak)
            list->add_widget(create_go_back_to_old_ui_button());
        return std::make_unique<Subsection>("Application options", std::move(list), mgl::vec2f(parent_page->get_inner_size().x, 0.0f));
    }

    void GlobalSettingsPage::add_widgets() {
        auto scrollable_page = std::make_unique<ScrollablePage>(content_page_ptr->get_inner_size());

        auto settings_list = std::make_unique<List>(List::Orientation::VERTICAL);
        settings_list->set_spacing(0.018f);
        settings_list->add_widget(create_appearance_subsection(scrollable_page.get()));
        settings_list->add_widget(create_startup_subsection(scrollable_page.get()));
        settings_list->add_widget(create_application_options_subsection(scrollable_page.get()));
        scrollable_page->add_widget(std::move(settings_list));

        content_page_ptr->add_widget(std::move(scrollable_page));
    }

    void GlobalSettingsPage::on_navigate_away_from_page() {
        save();
    }

    void GlobalSettingsPage::load() {
        if(config.main_config.tint_color.empty())
            tint_color_radio_button_ptr->set_selected_item(gpu_vendor_to_color_name(gsr_info->gpu_info.vendor));
        else
            tint_color_radio_button_ptr->set_selected_item(config.main_config.tint_color);

        const char *args[] = { "systemctl", "is-enabled", "--quiet", "--user", "gpu-screen-recorder-ui", nullptr };
        std::string stdout_str;
        const int exit_status = exec_program_on_host_get_stdout(args, stdout_str);
        startup_radio_button_ptr->set_selected_item(exit_status == 0 ? "start_on_system_startup" : "dont_start_on_system_startup", false, false);
    }

    void GlobalSettingsPage::save() {
        config.main_config.tint_color = tint_color_radio_button_ptr->get_selected_id();
        save_config(config);
    }
}