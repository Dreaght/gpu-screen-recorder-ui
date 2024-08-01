#include "../../include/gui/WidgetContainer.hpp"
#include "../../include/gui/Widget.hpp"

namespace gsr {
    // static
    WidgetContainer& WidgetContainer::get_instance() {
        static WidgetContainer instance;
        return instance;
    }

    void WidgetContainer::add_widget(Widget *widget) {
        // TODO: to_be_added, and remove in the draw loop
        #ifdef DEBUG
        for(Widget *existing_widget : widgets) {
            if(existing_widget == widget)
                return;
        }
        #endif
        widgets.push_back(widget);
    }

    void WidgetContainer::remove_widget(Widget *widget) {
        // TODO: to_be_removed, and remove in draw loop
        for(auto it = widgets.begin(), end = widgets.end(); it != end; ++it) {
            if(*it == widget) {
                widgets.erase(it);
                return;
            }
        }
    }

    void WidgetContainer::on_event(mgl::Event &event, mgl::Window &window) {
        // Process widgets by visibility (backwards)
        for(auto it = widgets.rbegin(), end = widgets.rend(); it != end; ++it) {
            if(!(*it)->on_event(event, window))
                return;
        }
    }

    void WidgetContainer::draw(mgl::Window &window) {
        for(auto it = widgets.begin(); it != widgets.end(); ++it) {
            Widget *widget = *it;
            if(widget->move_to_top) {
                widget->move_to_top = false;
                std::swap(*it, widgets.back());
                /*if(widgets.back() != widget) {
                    widgets.erase(it);
                    widgets.push_back(widget);
                }*/
            }
        }

        for(Widget *widget : widgets) {
            widget->draw(window);
        }
    }
}