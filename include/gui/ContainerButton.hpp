#pragma once

#include "Widget.hpp"
#include <functional>

#include <mglpp/graphics/Color.hpp>

namespace gsr {
    class ContainerButton : public Widget {
    public:
        // If width is 0 then the width of the widget is used instead.
        // If height is 0 then the height of the widget is used instead.
        ContainerButton(mgl::vec2f size, mgl::Color bg_color);
        ContainerButton(const ContainerButton&) = delete;
        ContainerButton& operator=(const ContainerButton&) = delete;
        ~ContainerButton() override;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        void set_border_scale(float scale);
        void set_bg_hover_color(mgl::Color color);

        Widget* get_widget() const;
        void set_widget(std::unique_ptr<Widget> widget);

        std::function<void()> on_click;
    private:
        mgl::vec2f size;
        mgl::Color bg_color;
        mgl::Color bg_hover_color;

        std::unique_ptr<Widget> widget;

        float border_scale = 0.0015f;
    };
}