#include "../../include/gui/Page.hpp"

namespace gsr {
    // void Page::remove_child_widget(Widget *widget) {
    //     for(auto it = widgets.begin(), end = widgets.end(); it != end; ++it) {
    //         if(it->get() == widget) {
    //             widgets.erase(it);
    //             return;
    //         }
    //     }
    // }

    void Page::add_widget(std::unique_ptr<Widget> widget) {
        widget->parent_widget = this;
        widgets.push_back(std::move(widget));
    }
}