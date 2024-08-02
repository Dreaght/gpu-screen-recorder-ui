#pragma once

#include <mglpp/system/vec.hpp>

namespace mgl {
    class Event;
    class Window;
}

namespace gsr {
    class Widget {
        friend class StaticPage;
        friend class ScrollablePage;
        friend class List;
        friend class Page;
    public:
        Widget();
        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        virtual ~Widget();

        // Return true to allow other widgets to also process the event
        virtual bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) = 0;
        virtual void draw(mgl::Window &window, mgl::vec2f offset) = 0;
        virtual void set_position(mgl::vec2f position);

        virtual mgl::vec2f get_position() const;
        virtual mgl::vec2f get_size() = 0;
    protected:
        void set_widget_as_selected_in_parent();
        void remove_widget_as_selected_in_parent();
    protected:
        mgl::vec2f position;
        Widget *parent_widget = nullptr;
        Widget *selected_child_widget = nullptr;
    };
}