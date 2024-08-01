#include "../../include/gui/StaticPage.hpp"

namespace gsr {
    bool StaticPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        const mgl::vec2f draw_pos = position + offset;
        offset = draw_pos;

        // Process widgets by visibility (backwards)
        for(auto it = widgets.rbegin(), end = widgets.rend(); it != end; ++it) {
            if(!(*it)->on_event(event, window, offset))
                return false;
        }

        return true;
    }

    void StaticPage::draw(mgl::Window &window, mgl::vec2f offset) {
        const mgl::vec2f draw_pos = position + offset;
        offset = draw_pos;

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
}