#include "../../include/gui/ExportPage.hpp"
#include "../../include/Theme.hpp"
#include "../../include/gui/PageStack.hpp"
#include "../../include/gui/Subsection.hpp"
#include "../../include/gui/List.hpp"
#include "../../include/Translation.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/window/Window.hpp>

namespace gsr {
    static const float button_spacing_scale = 0.015f;

    ExportPage::ExportPage(PageStack *page_stack) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        page_stack(page_stack),
        top_text(TR("Export"), get_theme().title_font_desc.c_str()),
        bottom_text(TR("Video Trimmer"), get_theme().title_font_desc.c_str())
    {
        const float margin = 0.02f;
        set_margins(margin, margin, margin, margin);

        add_button(TR("Export"), "export", get_color_theme().tint_color);
        add_button(TR("Cancel"), "cancel", get_color_theme().page_bg_color);
        on_click = [page_stack](const std::string &id) {
            if(id == "cancel")
                page_stack->pop();
            if(id == "export") {
                fprintf(stderr, "Export pressed!\n");
            }
        };
        add_widgets();
    }

    bool ExportPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        for(size_t i = 0; i < buttons.size(); ++i) {
            ButtonItem &button_item = buttons[i];
            if(!button_item.button->on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
                return false;
        }

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        const mgl::vec2f content_page_position = get_content_position() + mgl::vec2f(margin_left, get_border_size() + margin_top).floor();

        Widget *selected_widget = selected_child_widget;

        if(selected_widget) {
            if(!selected_widget->on_event(event, window, content_page_position))
                return false;
        }

        // Process widgets by visibility (backwards)
        return widgets.for_each_reverse([selected_widget, &window, &event, content_page_position](std::unique_ptr<Widget> &widget) {
            Widget *p = widget.get();
            if(p != selected_widget) {
                if(!p->on_event(event, window, content_page_position))
                    return false;
            }
            return true;
        });
    }

    void ExportPage::draw(mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return;

        const mgl::vec2f content_page_position = get_content_position();
        const mgl::vec2f content_page_size = get_size();

        mgl::Rectangle background(content_page_size);
        background.set_position(content_page_position);
        background.set_color(get_color_theme().page_bg_color);
        window.draw(background);

        mgl::Rectangle border(mgl::vec2f(content_page_size.x, get_border_size()).floor());
        border.set_position(content_page_position);
        border.set_color(get_color_theme().tint_color);
        window.draw(border);

        draw_page_label(window, content_page_position);
        draw_buttons(window, content_page_position, content_page_size);
    }

    void ExportPage::draw_page_label(mgl::Window &window, mgl::vec2f body_pos) {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();

        mgl::Rectangle background(mgl::vec2f(window_size.x / 10, window_size.x / 10).floor());
        background.set_position(body_pos.floor() - mgl::vec2f(background.get_size().x + get_horizontal_spacing(), 0.0f).floor());
        background.set_color(mgl::Color(0, 0, 0, 255));
        window.draw(background);

        const int text_margin = background.get_size().y * 0.085;

        top_text.set_position((background.get_position() + mgl::vec2f(background.get_size().x * 0.5f - top_text.get_bounds().size.x * 0.5f, text_margin)).floor());
        window.draw(top_text);

        mgl::Sprite icon(&get_theme().trimmer_texture);
        icon.set_height((int)(background.get_size().y * 0.8f));
        icon.set_position((background.get_position() + background.get_size() * 0.5f - icon.get_size() * 0.5f).floor());
        window.draw(icon);

        bottom_text.set_position((background.get_position() + mgl::vec2f(background.get_size().x * 0.5f - bottom_text.get_bounds().size.x * 0.5f, background.get_size().y - bottom_text.get_bounds().size.y - text_margin)).floor());
        window.draw(bottom_text);
    }

    void ExportPage::draw_buttons(mgl::Window &window, mgl::vec2f body_pos, mgl::vec2f body_size) {
        float offset_y = 0.0f;
        for(size_t i = 0; i < buttons.size(); ++i) {
            ButtonItem &button_item = buttons[i];
            button_item.button->set_position(body_pos + mgl::vec2f(body_size.x + get_horizontal_spacing(), offset_y).floor());
            button_item.button->draw(window, mgl::vec2f(0.0f, 0.0f));
            offset_y +=  button_item.button->get_size().y + (button_spacing_scale * get_theme().window_height);
        }
    }

    std::unique_ptr<Widget> ExportPage::create_video_section() {
        auto video_section_list = std::make_unique<List>(List::Orientation::VERTICAL);

        return std::make_unique<Subsection>(TR("Video"), std::move(video_section_list), mgl::vec2f(get_inner_size().x, 0.0f));
    }

    std::unique_ptr<Widget> ExportPage::create_settings() {
        auto settings_list = std::make_unique<List>(List::Orientation::VERTICAL);
        settings_list->set_spacing(0.018f);
        settings_list->add_widget(create_video_section());

        return settings_list;
    }

    void ExportPage::add_widgets() {
        add_widget(create_settings());
    }

    mgl::vec2f ExportPage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = (window_size * mgl::vec2f(0.3333f, 0.3333f)).floor();
        return content_page_size;
    }

    mgl::vec2f ExportPage::get_inner_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_bottom = margin_bottom_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        const int margin_right = margin_right_scale * get_theme().window_height;
        return get_size() - mgl::vec2f(margin_left + margin_right, margin_top + margin_bottom + get_border_size());
    }

    void ExportPage::set_margins(float top, float bottom, float left, float right) {
        margin_top_scale = top;
        margin_bottom_scale = bottom;
        margin_left_scale = left;
        margin_right_scale = right;
    }

    void ExportPage::add_button(const std::string &text, const std::string &id, mgl::Color color) {
        auto button = std::make_unique<Button>(get_theme().title_font_desc.c_str(), text.c_str(),
            mgl::vec2f(get_theme().window_width / 10, get_theme().window_height / 15).floor(), color);
        button->set_border_scale(0.003f);
        button->on_click = [this, id]() {
            if(on_click)
                on_click(id);
        };
        buttons.push_back({ std::move(button), id });
    }

    float ExportPage::get_border_size() const {
        return 0.004f * get_theme().window_height;
    }

    float ExportPage::get_horizontal_spacing() const {
        return get_theme().window_width / 50;
    }

    mgl::vec2f ExportPage::get_content_position() {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = get_size();
        return mgl::vec2f(mgl::vec2f(
            window_size.x * 0.5f - content_page_size.x * 0.5f,
            window_size.y * 0.5f - window_size.y * 0.7f * 0.5f
            )).floor();
    }
}
