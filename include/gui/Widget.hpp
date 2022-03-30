#pragma once

#include <mglpp/system/vec.hpp>

namespace mgl {
    class Event;
    class Window;
}

namespace gsr {
    class Widget {
    public:
        virtual ~Widget() = default;

        virtual void on_event(mgl::Event &event, mgl::Window &window) = 0;
        virtual void draw(mgl::Window &window) = 0;
        virtual void set_position(mgl::vec2f position);
    protected:
        mgl::vec2f position;
    };
}