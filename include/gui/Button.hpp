#pragma once

#include "Widget.hpp"
#include <functional>

#include <mglpp/graphics/Color.hpp>
#include <mglpp/graphics/Text.hpp>

namespace gsr {
    class Button : public Widget {
    public:
        Button(mgl::Font *font, const char *text, mgl::vec2f size, mgl::Color bg_color);
        Button(const Button&) = delete;
        Button& operator=(const Button&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() const { return size; }

        std::function<void()> on_click;
    private:
        mgl::vec2f size;
        mgl::Color bg_color;
        bool mouse_inside = false;
        mgl::Text text;
    };
}