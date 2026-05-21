#include "../../include/gui/ScrollablePage.hpp"

#include <algorithm>

#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    static const int scroll_speed = 80;
    static const double scroll_update_speed = 10.0;
    static const float scrollbar_width_scale = 0.004f;
    static const float scrollbar_spacing_scale = 0.004f;

    ScrollablePage::ScrollablePage(mgl::vec2f size, ScrollbarSide scrollbar_side) : size(size), scrollbar_side(scrollbar_side) {}

    ScrollablePage::~ScrollablePage() {
        widgets.for_each([this](std::unique_ptr<Widget> &widget) {
            if(widget->parent_widget == this)
                widget->parent_widget = nullptr;
            return true;
        }, true);
    }

    bool ScrollablePage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return true;

        const mgl::vec2f draw_pos = position + offset;

        const mgl::vec2f content_size = get_inner_size();
        const mgl::vec2f aligned_pos = draw_pos + (get_content_offset() + scroll);
        const mgl::vec2i scissor_pos(aligned_pos.x, aligned_pos.y);
        const mgl::vec2i scissor_size(content_size.x, content_size.y);

        offset = draw_pos + get_content_offset();
        Widget *selected_widget = selected_child_widget;

        if(event.type == mgl::Event::MouseButtonPressed && scrollbar_rect.contains(mgl::vec2f(event.mouse_button.x, event.mouse_button.y))) {
            set_widget_as_selected_in_parent();
            moving_scrollbar_with_cursor = true;
            scrollbar_move_cursor_start_pos = mgl::vec2f(event.mouse_button.x, event.mouse_button.y);

            if (is_horizontal()) {
                scrollbar_move_cursor_scroll_y_start = scroll.x;
            } else {
                scrollbar_move_cursor_scroll_y_start = scroll.y;
            }
            return false;
        }

        if(event.type == mgl::Event::MouseButtonReleased && moving_scrollbar_with_cursor) {
            moving_scrollbar_with_cursor = false;
            remove_widget_as_selected_in_parent();
            return false;
        }

        // Pass release to children even if outside area, because we want to be able to release mouse when moved outside,
        // for example in Entry when selecting text
        if(event.type == mgl::Event::MouseButtonPressed/* || event.type == mgl::Event::MouseButtonReleased*/) {
            if(!mgl::IntRect(scissor_pos, scissor_size).contains({event.mouse_button.x, event.mouse_button.y}))
                return true;
        } else if(event.type == mgl::Event::MouseMoved) {
            if(!mgl::IntRect(scissor_pos, scissor_size).contains({event.mouse_move.x, event.mouse_move.y}))
                return true;
        }

        if(selected_widget) {
            if(!selected_widget->on_event(event, window, offset))
                return false;
        }

        // Process widgets by visibility (backwards)
        const bool continue_events = widgets.for_each_reverse([selected_widget, &window, &event, offset](std::unique_ptr<Widget> &widget) {
            Widget *p = widget.get();
            if(p != selected_widget) {
                if(!p->on_event(event, window, offset))
                    return false;
            }
            return true;
        });

        if(!continue_events)
            return false;

        if(event.type == mgl::Event::MouseWheelScrolled) {
            const double delta = event.mouse_wheel_scroll.delta * scroll_speed;

            if (is_horizontal()) {
                scroll_target.x -= delta;
            } else {
                scroll_target.y -= delta;
            }
            return false;
        }

        return true;
    }

    void ScrollablePage::draw(mgl::Window &window, mgl::vec2f offset) {
        scrollbar_rect = mgl::FloatRect();

        if(!visible || widgets.empty()) {
            reset_scroll();
            return;
        }

        const mgl::vec2f draw_pos = position + offset;

        const mgl::Scissor prev_scissor = window.get_scissor();

        const mgl::vec2f content_size = get_inner_size();
        const mgl::vec2f aligned_pos = draw_pos + (get_content_offset() + scroll);
        const mgl_scissor new_scissor = {
            mgl_vec2i{(int)aligned_pos.x, (int)aligned_pos.y},
            mgl_vec2i{(int)content_size.x, (int)content_size.y}
        };
        mgl_window_set_scissor(window.internal_window(), &new_scissor);

        offset = draw_pos + get_content_offset();

        Widget *selected_widget = selected_child_widget;

        mgl::vec2f content_start = { 999999.0f, 999999.0f };
        mgl::vec2f content_end = { 0.0f, 0.0f };

        for(size_t i = 0; i < widgets.size(); ++i) {
            auto &widget = widgets[i];
            if(widget.get() != selected_widget) {
                widget->draw(window, offset);

                // TODO: Create a widget function to get the render area instead, which each widget should set (position + offset as start, and position + offset + size as end), this has to be done in the widgets to ensure that recursive rendering has correct position.
                // TODO: Do these calls before drawing, so that scroll limits can be set before drawing to prevent scrolling from overflowing for 1 frame.
                // To make that possible move inner_size calculation in FileChooserBody to the get_inner_size function.
                // That calculation can be done without looping folders.
                const mgl::vec2f widget_pos = widget->get_position();
                const mgl::vec2f widget_inner_size = widget->get_inner_size();

                content_start.x = std::min(content_start.x, widget_pos.x);
                content_start.y = std::min(content_start.y, widget_pos.y);
                content_end.x = std::max(content_end.x, widget_pos.x + widget_inner_size.x);
                content_end.y = std::max(content_end.y, widget_pos.y + widget_inner_size.y);
            }
        }

        if(selected_widget) {
            selected_widget->draw(window, offset);

            const mgl::vec2f widget_pos = selected_widget->get_position();
            const mgl::vec2f widget_inner_size = selected_widget->get_inner_size();

            content_start.x = std::min(content_start.x, widget_pos.x);
            content_start.y = std::min(content_start.y, widget_pos.y);
            content_end.x = std::max(content_end.x, widget_pos.x + widget_inner_size.x);
            content_end.y = std::max(content_end.y, widget_pos.y + widget_inner_size.y);
        }

        const mgl::vec2f child_size = content_end - content_start;

        //fprintf(stderr, "scrollbar height: %f\n", scrollbar_height);

        // Debug output
        // mgl::Rectangle bottom(mgl::vec2f(size.x, 5.0f));
        // bottom.set_color(mgl::Color(255, 0, 0, 255));
        // bottom.set_position(mgl::vec2f(offset.x, child_bottom));
        // window.draw(bottom);

        // mgl::Rectangle bottom_d(mgl::vec2f(size.x, 5.0f));
        // bottom_d.set_color(mgl::Color(0, 255, 0, 255));
        // bottom_d.set_position(mgl::vec2f(offset.x, page_scroll_start.y + size.y - 5));
        // window.draw(bottom_d);

        apply_animation();
        limit_scroll(child_size);

        window.set_scissor(prev_scissor);

        draw_scrollbar(window, draw_pos, child_size);
    }

    void ScrollablePage::draw_scrollbar(mgl::Window &window, mgl::vec2f draw_pos, const mgl::vec2f child_size) {
        const double scrollbar_width = get_scrollbar_width();

        const mgl::vec2f scrollbar_pos = [=]() {
            switch (scrollbar_side) {
                case ScrollbarSide::RIGHT:
                    return draw_pos + mgl::vec2f(size.x - scrollbar_width, 0.0f);
                case ScrollbarSide::LEFT:
                    return draw_pos;
                case ScrollbarSide::BOTTOM:
                    return draw_pos + mgl::vec2f(0.0f, size.y - scrollbar_width);
                default:
                    return draw_pos;
            }
        }();

        const double container_len = is_horizontal() ? size.x : size.y;
        const double total_content_len = is_horizontal() ? child_size.x : child_size.y;
        const double current_scroll = is_horizontal() ? scroll.x : scroll.y;

        double scrollbar_ratio = 1.0;
        if(total_content_len > 0.001)
            scrollbar_ratio = container_len / total_content_len;
        if(scrollbar_ratio > 1.0)
            scrollbar_ratio = 1.0;

        const double thumb_len = std::max(10.0, container_len * scrollbar_ratio);
        const double scrollbar_empty_space = container_len - thumb_len;

        if(scrollbar_ratio < 0.999) {
            double scroll_amount = current_scroll / (total_content_len - container_len);
            scroll_amount = std::clamp(scroll_amount, 0.0, 1.0);

            mgl::vec2f thumb_offset;
            mgl::vec2f thumb_size;

            if (is_horizontal()) {
                thumb_offset = mgl::vec2f(scroll_amount * scrollbar_empty_space, 0.0f);
                thumb_size = mgl::vec2f(thumb_len, scrollbar_width);
            } else {
                thumb_offset = mgl::vec2f(0.0f, scroll_amount * scrollbar_empty_space);
                thumb_size = mgl::vec2f(scrollbar_width, thumb_len);
            }

            mgl::Rectangle scrollbar((scrollbar_pos + thumb_offset).floor(), thumb_size.floor());
            scrollbar.set_color(mgl::Color(200, 200, 200));
            window.draw(scrollbar);

            scrollbar_rect.position = scrollbar.get_position();
            scrollbar_rect.size = scrollbar.get_size();
        }

        limit_scroll_cursor(window, child_size, scrollbar_empty_space);
    }

    mgl::vec2f ScrollablePage::get_content_offset() {
        mgl::vec2f offset = {0.0f, 0.0f};
        if (scrollbar_side == ScrollbarSide::LEFT) {
            offset.x = size.x - get_inner_size().x;
        }
        return offset - scroll;
    }

    void ScrollablePage::apply_animation() {
        mgl::vec2f scroll_diff = scroll_target - scroll;
        const double frame_scroll_speed = std::min(1.0, get_frame_delta_seconds() * scroll_update_speed);

        if (std::abs(scroll_diff.x) < 0.1f) scroll.x = scroll_target.x;
        else scroll.x += (scroll_diff.x * frame_scroll_speed);

        if (std::abs(scroll_diff.y) < 0.1f) scroll.y = scroll_target.y;
        else scroll.y += (scroll_diff.y * frame_scroll_speed);
    }

    void ScrollablePage::limit_scroll(mgl::vec2f child_size) {
        const float scroll_right_limit = std::max(0.0f, child_size.x - size.x);
        const float scroll_bottom_limit = std::max(0.0f, child_size.y - size.y);

        if (scroll.x < 0.0f || child_size.x < size.x) {
            scroll.x = 0.0f;
            scroll_target.x = 0.0f;
        } else if (scroll.x > scroll_right_limit) {
            scroll.x = scroll_right_limit;
            scroll_target.x = scroll_right_limit;
        }

        if (scroll.y < 0.0f || child_size.y < size.y) {
            scroll.y = 0.0f;
            scroll_target.y = 0.0f;
        } else if (scroll.y > scroll_bottom_limit) {
            scroll.y = scroll_bottom_limit;
            scroll_target.y = scroll_bottom_limit;
        }
    }

    void ScrollablePage::limit_scroll_cursor(mgl::Window &window, mgl::vec2f child_size, double scrollbar_empty_space) {
        if (!moving_scrollbar_with_cursor)
            return;

        const mgl::vec2f scrollbar_move_diff = window.get_mouse_position().to_vec2f() - scrollbar_move_cursor_start_pos;

        if (scrollbar_empty_space < 0.001)
            return;

        if (is_horizontal()) {
            const float scroll_right_limit = std::max(0.0f, child_size.x - size.x);
            const double scroll_amount = scrollbar_move_diff.x / scrollbar_empty_space;

            scroll.x = scrollbar_move_cursor_scroll_y_start + scroll_amount * scroll_right_limit;
            scroll.x = std::clamp(scroll.x, 0.0f, scroll_right_limit);

            scroll_target.x = scroll.x;
        } else {
            const float scroll_bottom_limit = std::max(0.0f, child_size.y - size.y);
            const double scroll_amount = scrollbar_move_diff.y / scrollbar_empty_space;

            scroll.y = scrollbar_move_cursor_scroll_y_start + scroll_amount * scroll_bottom_limit;
            scroll.y = std::clamp(scroll.y, 0.0f, scroll_bottom_limit);

            scroll_target.y = scroll.y;
        }
    }

    mgl::vec2f ScrollablePage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size;
    }

    mgl::vec2f ScrollablePage::get_inner_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const float scrollbar_spacing = std::max(2.0f, scrollbar_spacing_scale * get_theme().window_height);
        const float total_scrollbar_allocated_space = get_scrollbar_width() + scrollbar_spacing;

        if (is_horizontal()) {
            return size - mgl::vec2f(0.0f, total_scrollbar_allocated_space);
        } else {
            return size - mgl::vec2f(total_scrollbar_allocated_space, 0.0f);
        }
    }

    void ScrollablePage::set_size(mgl::vec2f size) {
        this->size = size;
    }

    void ScrollablePage::add_widget(std::unique_ptr<Widget> widget) {
        widget->parent_widget = this;
        widgets.push_back(std::move(widget));
    }

    void ScrollablePage::reset_scroll() {
        scroll = {0.0f, 0.0f};
        scroll_target = {0.0f, 0.0f};
    }

    void ScrollablePage::reset_scrollbar_drag_anchor(mgl::Window &window) {
        if(!moving_scrollbar_with_cursor)
            return;

        scrollbar_move_cursor_start_pos = window.get_mouse_position().to_vec2f();
        if(is_horizontal())
            scrollbar_move_cursor_scroll_y_start = scroll.x;
        else
            scrollbar_move_cursor_scroll_y_start = scroll.y;
    }

    float ScrollablePage::get_scrollbar_width() const {
        return std::max(5.0f, scrollbar_width_scale * get_theme().window_height);
    }
}
