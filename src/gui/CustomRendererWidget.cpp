#include "../../include/gui/CustomRendererWidget.hpp"

#include <mglpp/window/Window.hpp>

namespace gsr {
    CustomRendererWidget::CustomRendererWidget(mgl::vec2f size) : size(size) {}
    
    bool CustomRendererWidget::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible || !event_handler)
            return true;
        return event_handler(event, window, position + offset, size);
    }

    void CustomRendererWidget::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;

        mgl_scissor prev_scissor;
        mgl_window_get_scissor(window.internal_window(), &prev_scissor);

        mgl_scissor new_scissor = {
            mgl_vec2i{(int)draw_pos.x, (int)draw_pos.y},
            mgl_vec2i{(int)size.x, (int)size.y}
        };
        mgl_window_set_scissor(window.internal_window(), &new_scissor);

        if(draw_handler)
            draw_handler(window, draw_pos, size);

        mgl_window_set_scissor(window.internal_window(), &prev_scissor);
    }

    mgl::vec2f CustomRendererWidget::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size;
    }

    void CustomRendererWidget::set_size(mgl::vec2f size) {
        this->size = size;
    }
}