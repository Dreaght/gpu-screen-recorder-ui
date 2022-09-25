#pragma once

#include <mglpp/system/vec.hpp>

namespace mgl {
    class Event;
    class Window;
}

namespace gsr {
    class Widget {
        friend class WidgetContainer;
    public:
        Widget();
        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        virtual ~Widget();

        // Return true to allow other widgets to also process the event
        virtual bool on_event(mgl::Event &event, mgl::Window &window) = 0;
        virtual void draw(mgl::Window &window) = 0;
        virtual void set_position(mgl::vec2f position);

        virtual mgl::vec2f get_position() const;
    protected:
        mgl::vec2f position;
        bool move_to_top = false;
    };
}