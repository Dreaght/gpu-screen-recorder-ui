#include "../../include/gui/Page.hpp"

namespace gsr {
    void Page::add_widget(std::unique_ptr<Widget> widget) {
        widget->parent_widget = this;
        widgets.push_back(std::move(widget));
    }
}