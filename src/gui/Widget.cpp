#include "../../include/gui/Widget.hpp"

namespace gsr {
    Widget::Widget() {
        
    }

    Widget::~Widget() {
        
    }

    void Widget::set_position(mgl::vec2f position) {
        this->position = position;
    }

    mgl::vec2f Widget::get_position() const {
        return position;
    }
}