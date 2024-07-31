#include "../../include/gui/Button.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    Button::Button(mgl::vec2f size) : size(size) {

    }

    bool Button::on_event(mgl::Event &event, mgl::Window&) {
        if(event.type == mgl::Event::MouseMoved) {
            const mgl::vec2f collision_margin(1.0f, 1.0f); // Makes sure that multiple buttons that are next to each other wont activate at the same time when the cursor is right between them
            const bool inside = mgl::FloatRect(position + collision_margin, size - collision_margin).contains({ (float)event.mouse_move.x, (float)event.mouse_move.y });
            if(mouse_inside && !inside) {
                mouse_inside = false;
            } else if(!mouse_inside && inside) {
                mouse_inside = true;
            }
        } else if(event.type == mgl::Event::MouseButtonPressed) {
            pressed_inside = mouse_inside;
        } else if(event.type == mgl::Event::MouseButtonReleased) {
            const bool clicked_inside = pressed_inside && mouse_inside;
            pressed_inside = false;
            if(clicked_inside && on_click)
                on_click();
        }
        return true;
    }

    void Button::draw(mgl::Window &window) {
        if(mouse_inside) {
            // Background
            {
                mgl::Rectangle rect(size);
                rect.set_position(position);
                rect.set_color(mgl::Color(0, 0, 0, 255));
                window.draw(rect);
            }

            const int border_size = 5;
            const mgl::Color border_color(118, 185, 0);

            // Green line at top
            {
                mgl::Rectangle rect({ size.x, border_size });
                rect.set_position(position);
                rect.set_color(border_color);
                window.draw(rect);
            }

            // Green line at bottom
            {
                mgl::Rectangle rect({ size.x, border_size });
                rect.set_position(position + mgl::vec2f(0.0f, size.y - border_size));
                rect.set_color(border_color);
                window.draw(rect);
            }

            // Green line at left
            {
                mgl::Rectangle rect({ border_size, size.y - border_size * 2 });
                rect.set_position(position + mgl::vec2f(0, border_size));
                rect.set_color(border_color);
                window.draw(rect);
            }

            // Green line at right
            {
                mgl::Rectangle rect({ border_size, size.y - border_size * 2 });
                rect.set_position(position + mgl::vec2f(size.x - border_size, border_size));
                rect.set_color(border_color);
                window.draw(rect);
            }
        } else {
            // Background
            mgl::Rectangle rect(size);
            rect.set_position(position);
            rect.set_color(mgl::Color(0, 0, 0, 220));
            window.draw(rect);
        }
    }
}