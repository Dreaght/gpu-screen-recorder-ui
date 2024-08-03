#include "../../include/gui/Button.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    Button::Button(mgl::Font *font, const char *text, mgl::vec2f size, mgl::Color bg_color) : size(size), bg_color(bg_color), text(text, *font) {

    }

    bool Button::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(event.type == mgl::Event::MouseMoved) {
            mouse_inside = mgl::FloatRect(position + offset, size).contains({ (float)event.mouse_move.x, (float)event.mouse_move.y });
        } else if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const bool clicked_inside = mouse_inside;
            if(clicked_inside && on_click)
                on_click();
        }
        return true;
    }

    void Button::draw(mgl::Window &window, mgl::vec2f offset) {
        mgl::Rectangle background(size.floor());
        background.set_position((position + offset).floor());
        background.set_color(bg_color);
        window.draw(background);

        text.set_position((position + offset + size * 0.5f - text.get_bounds().size * 0.5f).floor());
        window.draw(text);

        if(mouse_inside) {
            const int border_size = 5;
            const mgl::Color border_color = gsr::get_theme().tint_color;
            draw_rectangle_outline(window, position, size, border_color, border_size);
        }
    }
}