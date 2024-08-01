#include "../../include/gui/ScrollablePage.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>

namespace gsr {
    ScrollablePage::ScrollablePage(mgl::vec2f size) : size(size) {}

    bool ScrollablePage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        const mgl::vec2f draw_pos = position + offset;
        offset = draw_pos + mgl::vec2f(0.0f, get_border_size(window)).floor();

        // Process widgets by visibility (backwards)
        for(auto it = widgets.rbegin(), end = widgets.rend(); it != end; ++it) {
            if(!(*it)->on_event(event, window, offset))
                return false;
        }

        return true;
    }

    void ScrollablePage::draw(mgl::Window &window, mgl::vec2f offset) {
        const mgl::vec2f draw_pos = position + offset;
        offset = draw_pos + mgl::vec2f(0.0f, get_border_size(window)).floor();

        mgl_scissor prev_scissor;
        mgl_window_get_scissor(window.internal_window(), &prev_scissor);

        mgl_scissor new_scissor = {
            mgl_vec2i{(int)draw_pos.x, (int)draw_pos.y},
            mgl_vec2i{(int)size.x, (int)size.y}
        };
        mgl_window_set_scissor(window.internal_window(), &new_scissor);

        mgl::Rectangle background(size.floor());
        background.set_position(draw_pos);
        background.set_color(get_theme().scrollable_page_bg_color);
        window.draw(background);

        mgl::Rectangle border(mgl::vec2f(size.x, get_border_size(window)).floor());
        border.set_position(draw_pos);
        border.set_color(get_theme().tint_color);
        window.draw(border);

        for(auto &widget : widgets) {
            if(widget->move_to_top) {
                widget->move_to_top = false;
                std::swap(widget, widgets.back());
            }
        }

        for(auto &widget : widgets) {
            widget->draw(window, offset);
        }

        mgl_window_set_scissor(window.internal_window(), &prev_scissor);
    }

    float ScrollablePage::get_border_size(mgl::Window &window) const {
        return window.get_size().y * 0.004f;
    }
}