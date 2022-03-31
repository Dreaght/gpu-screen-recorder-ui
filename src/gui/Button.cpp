#include "../../include/gui/Button.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    Button::Button(mgl::vec2f size) : size(size) {

    }

    void Button::on_event(mgl::Event &event, mgl::Window&) {
        /*
        if(event.type == mgl::Event::MouseMoved) {
            const bool inside = mgl::FloatRect(position, size).contains({ (float)event.mouse_move.x, (float)event.mouse_move.y });
            if(mouse_inside && !inside) {
                mouse_inside = false;
            } else if(!mouse_inside && inside) {
                mouse_inside = true;
            }
        } else if(event.type == mgl::Event::MouseButtonPressed && mouse_inside) {

        }
        */
        if(event.type == mgl::Event::MouseButtonPressed && mouse_inside) {
            if(on_click)
                on_click();
        }
    }

    void Button::draw(mgl::Window &window) {
        const bool inside = mgl::FloatRect(position, size).contains(window.get_mouse_position().to_vec2f());
        if(mouse_inside && !inside) {
            mouse_inside = false;
        } else if(!mouse_inside && inside) {
            mouse_inside = true;
        }

        if(mouse_inside) {
            // Background
            /*
            {
                mgl::Rectangle rect(size);
                rect.set_position(position);
                rect.set_color(mgl::Color(20, 20, 20, 255));
                window.draw(rect);
            }
            */

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
                mgl::Rectangle rect({ border_size, size.y });
                rect.set_position(position);
                rect.set_color(border_color);
                window.draw(rect);
            }

            // Green line at right
            {
                mgl::Rectangle rect({ border_size, size.y });
                rect.set_position(position + mgl::vec2f(size.x - border_size, 0.0f));
                rect.set_color(border_color);
                window.draw(rect);
            }
        }
    }
}