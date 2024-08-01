#include "../../include/gui/Page.hpp"
#include "../../include/gui/Widget.hpp"

namespace gsr {
    void Page::add_widget(std::unique_ptr<Widget> widget) {
        widgets.push_back(std::move(widget));
    }

    void Page::on_event(mgl::Event &event, mgl::Window &window) {
        // Process widgets by visibility (backwards)
        for(auto it = widgets.rbegin(), end = widgets.rend(); it != end; ++it) {
            if(!(*it)->on_event(event, window))
                return;
        }
    }

    void Page::draw(mgl::Window &window) {
        for(auto &widget : widgets) {
            if(widget->move_to_top) {
                widget->move_to_top = false;
                std::swap(widget, widgets.back());
            }
            widget->draw(window);
        }

        for(auto &widget : widgets) {
            widget->draw(window);
        }
    }
}