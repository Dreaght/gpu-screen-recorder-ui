#include "../../include/gui/StaticPage.hpp"

#include <mglpp/window/Window.hpp>

namespace gsr {
    StaticPage::StaticPage(mgl::vec2f size) : size(size) {}
    
    bool StaticPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return true;

        const mgl::vec2f draw_pos = position + offset;
        offset = draw_pos;
        Widget *selected_widget = selected_child_widget;

        if(selected_widget) {
            if(!selected_widget->on_event(event, window, offset))
                return false;
        }

        // Process widgets by visibility (backwards)
        for(auto it = widgets.rbegin(), end = widgets.rend(); it != end; ++it) {
            if(it->get() != selected_widget) {
                if(!(*it)->on_event(event, window, offset))
                    return false;
            }
        }

        return true;
    }

    void StaticPage::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;
        offset = draw_pos;
        Widget *selected_widget = selected_child_widget;

        mgl_scissor prev_scissor;
        mgl_window_get_scissor(window.internal_window(), &prev_scissor);

        mgl_scissor new_scissor = {
            mgl_vec2i{(int)draw_pos.x, (int)draw_pos.y},
            mgl_vec2i{(int)size.x, (int)size.y}
        };
        mgl_window_set_scissor(window.internal_window(), &new_scissor);

        for(auto &widget : widgets) {
            if(widget.get() != selected_widget)
                widget->draw(window, offset);
        }

        if(selected_widget)
            selected_widget->draw(window, offset);

        mgl_window_set_scissor(window.internal_window(), &prev_scissor);
    }

    mgl::vec2f StaticPage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size;
    }
}