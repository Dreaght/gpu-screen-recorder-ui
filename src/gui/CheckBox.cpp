#include "../../include/gui/CheckBox.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    static const float spacing_scale = 0.005f;
    static const float checked_margin_scale = 0.003f;
    static const float border_scale = 0.001f;

    CheckBox::CheckBox(mgl::Font *font, const char *text) : text(text, *font) {

    }

    bool CheckBox::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const bool clicked_inside = mgl::FloatRect(position + offset, get_size()).contains({ (float)event.mouse_button.x, (float)event.mouse_button.y });
            if(clicked_inside) {
                checked = !checked;
                if(on_click)
                    on_click();
            }
        }
        return true;
    }

    void CheckBox::draw(mgl::Window &window, mgl::vec2f offset) {
        const mgl::vec2f draw_pos = position + offset;

        const mgl::vec2f checkbox_size = get_checkbox_size();
        mgl::Rectangle background(get_checkbox_size());
        background.set_position(draw_pos.floor());
        background.set_color(mgl::Color(0, 0, 0, 120));
        window.draw(background);

        if(checked) {
            const float side_margin = checked_margin_scale * get_theme().window_height;
            mgl::Rectangle background(get_checkbox_size() - mgl::vec2f(side_margin, side_margin).floor() * 2.0f);
            background.set_position(draw_pos.floor() + mgl::vec2f(side_margin, side_margin).floor());
            background.set_color(gsr::get_theme().tint_color);
            window.draw(background);
        }

        const mgl::vec2f text_bounds = text.get_bounds().size;
        text.set_position((draw_pos + mgl::vec2f(checkbox_size.x + spacing_scale * get_theme().window_height, checkbox_size.y * 0.5f - text_bounds.y * 0.5f)).floor());
        window.draw(text);

        const bool mouse_inside = mgl::FloatRect(draw_pos, get_size()).contains(window.get_mouse_position().to_vec2f());
        if(mouse_inside) {
            const int border_size = border_scale * gsr::get_theme().window_height;
            const mgl::Color border_color = gsr::get_theme().tint_color;
            draw_rectangle_outline(window, draw_pos, checkbox_size, border_color, border_size);
        }
    }

    mgl::vec2f CheckBox::get_size() {
        mgl::vec2f size = text.get_bounds().size;
        const mgl::vec2f checkbox_size = get_checkbox_size();
        size.x += checkbox_size.x + spacing_scale * get_theme().window_height;
        size.y = std::max(size.y, checkbox_size.y);
        return size;
    }

    mgl::vec2f CheckBox::get_checkbox_size() {
        const mgl::vec2f text_bounds = text.get_bounds().size;
        return mgl::vec2f(text_bounds.y, text_bounds.y).floor();
    }
}