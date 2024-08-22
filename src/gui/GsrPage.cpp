#include "../../include/gui/GsrPage.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/window/Window.hpp>

namespace gsr {
    GsrPage::GsrPage() :
        scrollable_body(mgl::vec2f(0.0f, 0.0f)),
        back_button(&get_theme().title_font, "Back",
            mgl::vec2f(get_theme().window_width / 10, get_theme().window_height / 15).floor(), get_theme().scrollable_page_bg_color),
        label_text("Settings", get_theme().title_font)
    {
        //set_position(content_page_position);
        const float margin = 0.02f;
        set_margins(margin, margin, margin, margin);
        back_button.set_border_scale(0.003f);
    }

    bool GsrPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        if(!scrollable_body.on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
            return false;

        if(!back_button.on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
            return false;

        return true;
    }

    void GsrPage::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;

        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = get_size();
        const mgl::vec2f content_page_position = mgl::vec2f(window_size * 0.5f - content_page_size * 0.5f).floor();
        offset = content_page_position + mgl::vec2f(margin_left, get_border_size() + margin_top).floor();

        mgl::Rectangle background(content_page_size);
        background.set_position(content_page_position);
        background.set_color(get_theme().scrollable_page_bg_color);
        window.draw(background);

        mgl::Rectangle border(mgl::vec2f(content_page_size.x, get_border_size()).floor());
        border.set_position(content_page_position);
        border.set_color(get_theme().tint_color);
        window.draw(border);

        scrollable_body.set_position(offset);
        scrollable_body.draw(window, mgl::vec2f(0.0f, 0.0f));

        draw_page_label(window, content_page_position);

        back_button.set_position(content_page_position + mgl::vec2f(content_page_size.x + get_horizontal_spacing(), 0.0f).floor());
        back_button.draw(window, mgl::vec2f(0.0f, 0.0f));
    }

    void GsrPage::draw_page_label(mgl::Window &window, mgl::vec2f body_pos) {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();

        mgl::Rectangle background(mgl::vec2f(window_size.x / 10, window_size.x / 10).floor());
        background.set_position(body_pos.floor() - mgl::vec2f(background.get_size().x + get_horizontal_spacing(), 0.0f).floor());
        background.set_color(mgl::Color(0, 0, 0, 255));
        window.draw(background);

        const int text_margin = background.get_size().y * 0.085;
        label_text.set_position((background.get_position() + mgl::vec2f(background.get_size().x * 0.5f - label_text.get_bounds().size.x * 0.5f, text_margin)).floor());
        window.draw(label_text);

        mgl::Sprite icon(&get_theme().settings_texture);
        icon.set_height((int)(background.get_size().y * 0.5f));
        icon.set_position((background.get_position() + background.get_size() * 0.5f - icon.get_size() * 0.5f).floor());
        window.draw(icon);
    }

    mgl::vec2f GsrPage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = (window_size * mgl::vec2f(0.3333f, 0.7f)).floor();
        return content_page_size;
    }

    mgl::vec2f GsrPage::get_inner_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_bottom = margin_bottom_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        const int margin_right = margin_right_scale * get_theme().window_height;
        return get_size() - mgl::vec2f(margin_left + margin_right, margin_top + margin_bottom + get_border_size());
    }

    void GsrPage::add_widget(std::unique_ptr<Widget> widget) {
        scrollable_body.add_widget(std::move(widget));
    }

    void GsrPage::set_margins(float top, float bottom, float left, float right) {
        margin_top_scale = top;
        margin_bottom_scale = bottom;
        margin_left_scale = left;
        margin_right_scale = right;
        scrollable_body.set_size(get_inner_size());
    }

    void GsrPage::set_on_back_button_click(std::function<void()> on_click_handler) {
        back_button.on_click = std::move(on_click_handler);
    }

    float GsrPage::get_border_size() const {
        return 0.004f * get_theme().window_height;
    }

    float GsrPage::get_horizontal_spacing() const {
        return get_theme().window_width / 50;
    }
}