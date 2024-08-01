#pragma once

#include <vector>
#include <memory>

namespace mgl {
    class Event;
    class Window;
}

namespace gsr {
    class Widget;

    class Page {
    public:
        Page() = default;
        Page(const Page&) = delete;
        Page& operator=(const Page&) = delete;

        void add_widget(std::unique_ptr<Widget> widget);

        void on_event(mgl::Event &event, mgl::Window &window);
        void draw(mgl::Window &window);
    private:
        std::vector<std::unique_ptr<Widget>> widgets;
    };
}