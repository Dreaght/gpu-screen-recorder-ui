#include "../../include/gui/Subsection.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/window/Window.hpp>
#include <mglpp/graphics/Rectangle.hpp>

namespace gsr {
    static const float margin_top_scale = 0.012f;
    static const float margin_bottom_scale = 0.012f;
    static const float margin_left_scale = 0.015f;
    static const float margin_right_scale = 0.015f;
    static const float title_spacing_scale = 0.015f;

    Subsection::Subsection(const char *title, std::unique_ptr<Widget> inner_widget, mgl::vec2f size) :
        label(&get_theme().title_font, title, get_theme().text_color),
        inner_widget(std::move(inner_widget)),
        size(size)
    {
        this->inner_widget->parent_widget = this;
    }
    
    bool Subsection::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        return inner_widget->on_event(event, window, mgl::vec2f(0.0f, 0.0f));
    }

    void Subsection::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        mgl::vec2f draw_pos = position + offset;
        mgl::Rectangle background(draw_pos.floor(), get_size().floor());
        background.set_color(mgl::Color(25, 30, 34));
        window.draw(background);

        draw_pos += mgl::vec2f(margin_left_scale, margin_top_scale) * mgl::vec2f(get_theme().window_height, get_theme().window_height);
        label.draw(window, draw_pos);
        draw_pos.y += label.get_size().y + title_spacing_scale * get_theme().window_height;

        inner_widget->set_position(draw_pos);
        inner_widget->draw(window, mgl::vec2f(0.0f, 0.0f));
    }

    mgl::vec2f Subsection::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const mgl::vec2f margin_size = mgl::vec2f(margin_left_scale + margin_right_scale, margin_top_scale + margin_bottom_scale) * mgl::vec2f(get_theme().window_height, get_theme().window_height);
        mgl::vec2f widgets_size = mgl::vec2f(0.0f, label.get_size().y + title_spacing_scale * get_theme().window_height) + inner_widget->get_size() + margin_size;

        if(std::abs(size.x) > 0.001f)
            widgets_size.x = size.x;
        if(std::abs(size.y) > 0.001f)
            widgets_size.y = size.y;

        return widgets_size;
    }
}