#include "../../include/gui/Button.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    static const float padding_top_scale = 0.004629f;
    static const float padding_bottom_scale = 0.004629f;
    static const float padding_left_scale = 0.007f;
    static const float padding_right_scale = 0.007f;

    Button::Button(mgl::Font *font, const char *text, mgl::vec2f size, mgl::Color bg_color) : size(size), bg_color(bg_color), text(text, *font) {

    }

    bool Button::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(!visible)
            return true;

        const mgl::vec2f item_size = get_size().floor();
        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const bool clicked_inside = mgl::FloatRect(position + offset, item_size).contains({ (float)event.mouse_button.x, (float)event.mouse_button.y });
            if(clicked_inside) {
                if(on_click)
                    on_click();
                return false;
            }
        }
        return true;
    }

    void Button::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;

        const mgl::vec2f item_size = get_size().floor();
        mgl::Rectangle background(item_size);
        background.set_position(draw_pos.floor());
        background.set_color(bg_color);
        window.draw(background);

        text.set_position((draw_pos + item_size * 0.5f - text.get_bounds().size * 0.5f).floor());
        window.draw(text);

        const bool mouse_inside = mgl::FloatRect(draw_pos, item_size).contains(window.get_mouse_position().to_vec2f()) && !has_parent_with_selected_child_widget();
        if(mouse_inside) {
            const mgl::Color outline_color = (bg_color == get_theme().tint_color) ? mgl::Color(255, 255, 255) : get_theme().tint_color;
            draw_rectangle_outline(window, draw_pos, item_size, outline_color, std::max(1.0f, border_scale * get_theme().window_height));
        }
    }

    mgl::vec2f Button::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        const mgl::vec2f text_bounds = text.get_bounds().size;
        mgl::vec2f s = size;
        if(s.x < 0.0001f)
            s.x = padding_left + text_bounds.x + padding_right;
        if(s.y < 0.0001f)
            s.y = padding_top + text_bounds.y + padding_bottom;
        return s;
    }

    void Button::set_border_scale(float scale) {
        border_scale = scale;
    }

    const std::string& Button::get_text() const {
        return text.get_string();
    }

    void Button::set_text(std::string str) {
        text.set_string(std::move(str));
    }
}