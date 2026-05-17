#include "../../include/gui/ContainerButton.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    ContainerButton::ContainerButton(mgl::vec2f size, mgl::Color bg_color) :
        size(size), bg_color(bg_color), bg_hover_color(bg_color)
    {

    }

    ContainerButton::~ContainerButton() {
        if(widget && widget->parent_widget == this)
            widget->parent_widget = nullptr;
    }

    bool ContainerButton::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return true;

        const mgl::vec2f draw_pos = position + offset;
        const mgl::vec2f item_size = get_size().floor();

        if(widget) {
            if(!widget->on_event(event, window, draw_pos))
                return false;
        }

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const bool clicked_inside = mgl::FloatRect(draw_pos, item_size).contains({ (float)event.mouse_button.x, (float)event.mouse_button.y });
            if(clicked_inside) {
                if(on_click)
                    on_click();
                return false;
            }
        }
        return true;
    }

    void ContainerButton::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;
        const mgl::vec2f item_size = get_size().floor();
        const bool mouse_inside = mgl::FloatRect(draw_pos, item_size).contains(window.get_mouse_position().to_vec2f()) && !has_parent_with_selected_child_widget();

        mgl::Rectangle background(item_size);
        background.set_position(draw_pos.floor());
        background.set_color(mouse_inside ? bg_hover_color : bg_color);
        window.draw(background);

        if (widget) {
            widget->draw(window, draw_pos);
        }

        if(mouse_inside) {
            const mgl::Color outline_color = (bg_color == get_color_theme().tint_color) ? mgl::Color(255, 255, 255) : get_color_theme().tint_color;
            draw_rectangle_outline(window, draw_pos, item_size, outline_color, std::max(1.0f, border_scale * get_theme().window_height));
        }
    }

    mgl::vec2f ContainerButton::get_size() {
        mgl::vec2f widget_size = widget ? widget->get_size() : mgl::vec2f(0.0f, 0.0f);

        return {
            size.x > 0.0f ? size.x : widget_size.x,
            size.y > 0.0f ? size.y : widget_size.y
        };
    }

    void ContainerButton::set_border_scale(float scale) {
        border_scale = scale;
    }

    void ContainerButton::set_bg_hover_color(mgl::Color color) {
        bg_hover_color = color;
    }

    Widget* ContainerButton::get_widget() const {
        return widget.get();
    }

    void ContainerButton::set_widget(std::unique_ptr<Widget> widget) {
        if(this->widget && this->widget->parent_widget == this)
            this->widget->parent_widget = nullptr;

        this->widget = std::move(widget);

        if(this->widget) {
            this->widget->parent_widget = this;
            this->widget->set_position({0.0f, 0.0f});
        }
    }
}