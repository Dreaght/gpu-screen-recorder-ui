#pragma once

#include "Widget.hpp"
#include "../SafeVector.hpp"
#include <memory>

#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    class ScrollablePage : public Widget {
    public:
        enum class ScrollbarSide {
            RIGHT,
            LEFT,
            BOTTOM,
        };

        ScrollablePage(mgl::vec2f size, ScrollbarSide scrollbar_side = ScrollbarSide::RIGHT);
        ScrollablePage(const ScrollablePage&) = delete;
        ScrollablePage& operator=(const ScrollablePage&) = delete;
        virtual ~ScrollablePage() override;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        mgl::vec2f get_inner_size() override;
        void set_size(mgl::vec2f size);

        void add_widget(std::unique_ptr<Widget> widget);

        void reset_scroll();
        mgl::vec2f get_scroll_target() const { return scroll_target; }
        void set_scroll(mgl::vec2f new_scroll) { this->scroll = new_scroll; scroll_target = new_scroll; }
    private:
        bool is_horizontal() const {
            return scrollbar_side == ScrollbarSide::BOTTOM;
        }
        int get_scroll_axis() const {
            return is_horizontal() ? 0 : 1;
        }
        void apply_animation();
        void limit_scroll(mgl::vec2f child_size);
        void limit_scroll_cursor(mgl::Window &window, mgl::vec2f child_size, double scrollbar_empty_space);
        void draw_scrollbar(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f child_size);
        mgl::vec2f get_content_offset();
        float get_scrollbar_width() const;
    private:
        mgl::vec2f size;
        ScrollbarSide scrollbar_side;
        SafeVector<std::unique_ptr<Widget>> widgets;
        mgl::vec2f scroll_target = {0.0f, 0.0f};
        mgl::vec2f scroll = {0.0f, 0.0f};
        mgl::FloatRect scrollbar_rect;
        bool moving_scrollbar_with_cursor = false;
        mgl::vec2f scrollbar_move_cursor_start_pos;
        double scrollbar_move_cursor_scroll_y_start = 0.0;
    };
}