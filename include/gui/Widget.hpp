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
        friend class Subsection;
    public:
        enum class Alignment {
            START,
            CENTER,
            END
        };

        Widget();
        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        virtual ~Widget();

        // Return true to allow other widgets to also process the event
        virtual bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) = 0;
        virtual void draw(mgl::Window &window, mgl::vec2f offset) = 0;
        virtual void set_position(mgl::vec2f position);

        //virtual void remove_child_widget(Widget *widget) { (void)widget; }

        virtual mgl::vec2f get_position() const;
        virtual mgl::vec2f get_size() = 0;
        // This can be different from get_size, for example with ScrollablePage this excludes the margins
        virtual mgl::vec2f get_inner_size() { return get_size(); }

        void set_horizontal_alignment(Alignment alignment);
        void set_vertical_alignment(Alignment alignment);

        Alignment get_horizontal_alignment() const;
        Alignment get_vertical_alignment() const;

        void set_visible(bool visible);
    protected:
        void set_widget_as_selected_in_parent();
        void remove_widget_as_selected_in_parent();
        bool has_parent_with_selected_child_widget() const;
    protected:
        mgl::vec2f position;
        Widget *parent_widget = nullptr;
        Widget *selected_child_widget = nullptr;

        Alignment horizontal_aligment = Alignment::START;
        Alignment vertical_aligment = Alignment::START;

        bool visible = true;
    };
}