#pragma once

#include "Widget.hpp"
#include <vector>
#include <memory>

namespace gsr {
    class List : public Widget {
    public:
        enum class Orientation {
            VERTICAL,
            HORIZONTAL
        };

        enum class Alignment {
            START,
            CENTER,
            END
        };

        List(Orientation orientation, Alignment content_alignment = Alignment::START);
        List(const List&) = delete;
        List& operator=(const List&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        void add_widget(std::unique_ptr<Widget> widget);
        mgl::vec2f get_size() override;
    protected:
        std::vector<std::unique_ptr<Widget>> widgets;
        Orientation orientation;
        Alignment content_alignment;
    };
}