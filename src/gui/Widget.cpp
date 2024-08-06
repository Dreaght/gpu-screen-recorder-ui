#include "../../include/gui/Widget.hpp"

namespace gsr {
    Widget::Widget() {
        
    }

    Widget::~Widget() {
        remove_widget_as_selected_in_parent();
        // if(parent_widget)
        //     parent_widget->remove_child_widget(this);
    }

    void Widget::set_position(mgl::vec2f position) {
        this->position = position;
    }

    mgl::vec2f Widget::get_position() const {
        return position;
    }

    void Widget::set_widget_as_selected_in_parent() {
        if(parent_widget) {
            parent_widget->selected_child_widget = this;
            parent_widget->set_widget_as_selected_in_parent();
        }
    }

    void Widget::remove_widget_as_selected_in_parent() {
        if(parent_widget && parent_widget->selected_child_widget == this) {
            parent_widget->selected_child_widget = nullptr;
            parent_widget->remove_widget_as_selected_in_parent();
        }
    }
}