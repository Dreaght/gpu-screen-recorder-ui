#pragma once

#include "Widget.hpp"
#include "../SafeVector.hpp"
#include <memory>

#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    class ScrollablePage : public Widget {
    public:
        ScrollablePage(mgl::vec2f size);
        ScrollablePage(const ScrollablePage&) = delete;
        ScrollablePage& operator=(const ScrollablePage&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        mgl::vec2f get_inner_size() override;
        void set_size(mgl::vec2f size);

        void add_widget(std::unique_ptr<Widget> widget);

        void reset_scroll();
    private:
        float get_scrollbar_width() const;
    private:
        mgl::vec2f size;
        SafeVector<std::unique_ptr<Widget>> widgets;
        int scroll_target_y = 0;
        double scroll_y = 0.0;
        mgl::FloatRect scrollbar_rect;
        bool moving_scrollbar_with_cursor = false;
        mgl::vec2f scrollbar_move_cursor_start_pos;
        double scrollbar_move_cursor_scroll_y_start = 0.0;
    };
}