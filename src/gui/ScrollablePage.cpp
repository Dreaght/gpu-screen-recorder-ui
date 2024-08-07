#include "../../include/gui/ScrollablePage.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>

namespace gsr {
    ScrollablePage::ScrollablePage(mgl::vec2f size) : size(size) {}

    bool ScrollablePage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return true;

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;

        const mgl::vec2f draw_pos = position + offset;
        offset = draw_pos + mgl::vec2f(margin_left, get_border_size() + margin_top).floor();
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

    void ScrollablePage::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;

        const mgl::vec2f draw_pos = position + offset;
        offset = draw_pos + mgl::vec2f(margin_left, get_border_size() + margin_top).floor();

        mgl::Rectangle background(size.floor());
        background.set_position(draw_pos);
        background.set_color(get_theme().scrollable_page_bg_color);
        window.draw(background);

        mgl::Rectangle border(mgl::vec2f(size.x, get_border_size()).floor());
        border.set_position(draw_pos);
        border.set_color(get_theme().tint_color);
        window.draw(border);

        mgl_scissor prev_scissor;
        mgl_window_get_scissor(window.internal_window(), &prev_scissor);

        const mgl::vec2f content_size = get_inner_size();
        mgl_scissor new_scissor = {
            mgl_vec2i{(int)offset.x, (int)offset.y},
            mgl_vec2i{(int)content_size.x, (int)content_size.y}
        };
        mgl_window_set_scissor(window.internal_window(), &new_scissor);

        Widget *selected_widget = selected_child_widget;

        for(auto &widget : widgets) {
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

    mgl::vec2f ScrollablePage::get_inner_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const int margin_top = margin_top_scale * get_theme().window_height;
        const int margin_bottom = margin_bottom_scale * get_theme().window_height;
        const int margin_left = margin_left_scale * get_theme().window_height;
        const int margin_right = margin_right_scale * get_theme().window_height;
        return size - mgl::vec2f(margin_left + margin_right, margin_top + margin_bottom + get_border_size());
    }

    void ScrollablePage::set_margins(float top, float bottom, float left, float right) {
        margin_top_scale = top;
        margin_bottom_scale = bottom;
        margin_left_scale = left;
        margin_right_scale = right;
    }

    float ScrollablePage::get_border_size() const {
        return 0.004f * get_theme().window_height;
    }
}