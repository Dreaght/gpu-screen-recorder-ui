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

        //void remove_child_widget(Widget *widget) override;

        // Might not take effect immediately but at the next draw iteration if inside an event loop
        void add_widget(std::unique_ptr<Widget> widget);
        // Might not take effect immediately but at the next draw iteration if inside an event loop
        void remove_widget(Widget *widget);
        // Excludes widgets from queue
        const std::vector<std::unique_ptr<Widget>>& get_child_widgets() const;
        // Returns nullptr if index is invalid
        Widget* get_child_widget_by_index(size_t index) const;

        void set_spacing(float spacing);

        mgl::vec2f get_size() override;
    private:
        void update();
        void remove_widget_immediate(Widget *widget);
    protected:
        std::vector<std::unique_ptr<Widget>> widgets;
        std::vector<std::unique_ptr<Widget>> add_queue;
        std::vector<Widget*> remove_queue;
        Orientation orientation;
        Alignment content_alignment;
        bool inside_event_handler = false;
        float spacing_scale = 0.009f;
    };
}