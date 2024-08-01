#include "../../include/gui/Utils.hpp"
#include <mglpp/window/Window.hpp>
#include <mglpp/graphics/Rectangle.hpp>

namespace gsr {
    // TODO: Use vertices to make it one draw call
    void draw_rectangle_outline(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size, mgl::Color color, float border_size) {
        pos = pos.floor();
        size = size.floor();
        border_size = (int)border_size;
        // Green line at top
        {
            mgl::Rectangle rect({ size.x, border_size });
            rect.set_position(pos);
            rect.set_color(color);
            window.draw(rect);
        }

        // Green line at bottom
        {
            mgl::Rectangle rect({ size.x, border_size });
            rect.set_position(pos + mgl::vec2f(0.0f, size.y - border_size));
            rect.set_color(color);
            window.draw(rect);
        }

        // Green line at left
        {
            mgl::Rectangle rect({ border_size, size.y - border_size * 2 });
            rect.set_position(pos + mgl::vec2f(0, border_size));
            rect.set_color(color);
            window.draw(rect);
        }

        // Green line at right
        {
            mgl::Rectangle rect({ border_size, size.y - border_size * 2 });
            rect.set_position(pos + mgl::vec2f(size.x - border_size, border_size));
            rect.set_color(color);
            window.draw(rect);
        }
    }
}