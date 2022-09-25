#pragma once

#include <vector>
#include <memory>

namespace mgl {
    class Event;
    class Window;
}

namespace gsr {
    class Widget;

    class WidgetContainer {
    public:
        static WidgetContainer& get_instance();

        void add_widget(Widget *widget);
        void remove_widget(Widget *widget);

        void on_event(mgl::Event &event, mgl::Window &window);
        void draw(mgl::Window &window);
    private:
        WidgetContainer() = default;
        WidgetContainer& operator=(const WidgetContainer&) = delete;
        WidgetContainer(const WidgetContainer&) = delete;
    private:
        std::vector<Widget*> widgets;
    };
}