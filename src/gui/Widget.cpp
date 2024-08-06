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

    bool Widget::has_parent_with_selected_child_widget() const {
        // TODO: Optimize since this is called in draw function in widgets
        if(parent_widget) {
            if(parent_widget->selected_child_widget)
                return true;
            return parent_widget->has_parent_with_selected_child_widget();
        }
        return false;
    }

    void Widget::set_horizontal_alignment(Alignment alignment) {
        horizontal_aligment = alignment;
    }

    void Widget::set_vertical_alignment(Alignment alignment) {
        vertical_aligment = alignment;
    }

    Widget::Alignment Widget::get_horizontal_alignment() const {
        return horizontal_aligment;
    }

    Widget::Alignment Widget::get_vertical_alignment() const {
        return vertical_aligment;
    }

    void Widget::set_visible(bool visible) {
        this->visible = visible;
    }
}