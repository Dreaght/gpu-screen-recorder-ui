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

        mgl::Rectangle background(size);
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
            widget->draw(window, offset);
        }

        for(auto &widget : widgets) {
            widget->draw(window, offset);
        }
    }

    float ScrollablePage::get_border_size(mgl::Window &window) const {
        return window.get_size().y * 0.004f;
    }
}