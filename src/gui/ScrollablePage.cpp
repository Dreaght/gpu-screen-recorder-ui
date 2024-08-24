#include "../../include/gui/ScrollablePage.hpp"

#include <mglpp/window/Window.hpp>

namespace gsr {
    ScrollablePage::ScrollablePage(mgl::vec2f size) : size(size) {}

    bool ScrollablePage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
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
        return widgets.for_each_reverse([selected_widget, &window, &event, offset](std::unique_ptr<Widget> &widget) {
            if(widget.get() != selected_widget) {
                if(!widget->on_event(event, window, offset))
                    return false;
            }
            return true;
        });
    }

    void ScrollablePage::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;
        offset = draw_pos;

        mgl_scissor prev_scissor;
        mgl_window_get_scissor(window.internal_window(), &prev_scissor);

        const mgl::vec2f content_size = get_inner_size();
        mgl_scissor new_scissor = {
            mgl_vec2i{(int)offset.x, (int)offset.y},
            mgl_vec2i{(int)content_size.x, (int)content_size.y}
        };
        mgl_window_set_scissor(window.internal_window(), &new_scissor);

        Widget *selected_widget = selected_child_widget;

        for(size_t i = 0; i < widgets.size(); ++i) {
            auto &widget = widgets[i];
            if(widget.get() != selected_widget)
                widget->draw(window, offset);
        }

        if(selected_widget)
            selected_widget->draw(window, offset);

        mgl_window_set_scissor(window.internal_window(), &prev_scissor);
    }

    mgl::vec2f ScrollablePage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size;
    }

    void ScrollablePage::set_size(mgl::vec2f size) {
        this->size = size;
    }

    void ScrollablePage::add_widget(std::unique_ptr<Widget> widget) {
        widgets.push_back(std::move(widget));
    }
}