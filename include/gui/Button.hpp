#pragma once

#include "Widget.hpp"
#include <string>
#include <functional>

namespace gsr {
    class Button : public Widget {
    public:
        Button(mgl::vec2f size);
        void on_event(mgl::Event &event, mgl::Window &window) override;
        void draw(mgl::Window &window) override;

        std::function<void()> on_click;
    private:
        mgl::vec2f size;
        bool mouse_inside = false;
        bool pressed_inside = false;
    };
}