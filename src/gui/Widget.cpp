#include "../../include/gui/Widget.hpp"
#include "../../include/gui/WidgetContainer.hpp"

namespace gsr {
    Widget::Widget() {
        WidgetContainer::get_instance().add_widget(this);
    }

    Widget::~Widget() {
        WidgetContainer::get_instance().remove_widget(this);
    }

    void Widget::set_position(mgl::vec2f position) {
        this->position = position;
    }

    mgl::vec2f Widget::get_position() const {
        return position;
    }
}