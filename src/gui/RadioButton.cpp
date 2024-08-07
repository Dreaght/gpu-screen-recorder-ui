#include "../../include/gui/RadioButton.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    static const float padding_top_scale = 0.004629f;
    static const float padding_bottom_scale = 0.004629f;
    static const float padding_left_scale = 0.007f;
    static const float padding_right_scale = 0.007f;
    static const float spacing_scale = 0.007f;
    static const float border_scale = 0.0015f;

    RadioButton::RadioButton(mgl::Font *font) : font(font) {

    }

    bool RadioButton::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(!visible)
            return true;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            mgl::vec2f draw_pos = position + offset;
            for(size_t i = 0; i < items.size(); ++i) {
                auto &item = items[i];
                const mgl::vec2f item_size = item.text.get_bounds().size + mgl::vec2f(padding_left + padding_right, padding_top + padding_bottom);

                const bool mouse_inside = mgl::FloatRect(draw_pos, item_size).contains(mgl::vec2f(event.mouse_button.x, event.mouse_button.y));
                if(mouse_inside) {
                    const size_t prev_selected_item = selected_item;
                    selected_item = i;

                    if(selected_item != prev_selected_item && on_selection_changed)
                        on_selection_changed(item.text.get_string(), item.id);

                    return false;
                }

                draw_pos.x += item_size.x + spacing_scale * get_theme().window_height;
            }
        }
        return true;
    }

    void RadioButton::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        update_if_dirty();

        if(items.empty())
            return;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        const bool can_select_item = !has_parent_with_selected_child_widget();

        mgl::vec2f draw_pos = position + offset;
        for(size_t i = 0; i < items.size(); ++i) {
            auto &item = items[i];
            const mgl::vec2f item_size = item.text.get_bounds().size + mgl::vec2f(padding_left + padding_right, padding_top + padding_bottom);
            mgl::Rectangle background(item_size.floor());
            background.set_position(draw_pos.floor());
            background.set_color(i == selected_item ? get_theme().tint_color : mgl::Color(0, 0, 0, 120));
            window.draw(background);

            const bool mouse_inside = mgl::FloatRect(draw_pos, item_size).contains(window.get_mouse_position().to_vec2f());
            if(can_select_item && mouse_inside) {
                const int border_size = border_scale * get_theme().window_height;
                const mgl::Color border_color = get_theme().tint_color;
                draw_rectangle_outline(window, draw_pos.floor(), item_size.floor(), border_color, border_size);
            }

            item.text.set_position((draw_pos + item_size * 0.5f - item.text.get_bounds().size * 0.5f).floor());
            window.draw(item.text);

            draw_pos.x += item_size.x + spacing_scale * get_theme().window_height;
        }
    }

    void RadioButton::update_if_dirty() {
        if(!dirty)
            return;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        size = { 0.0f, font->get_character_size() + (float)padding_top + (float)padding_bottom };
        for(Item &item : items) {
            const mgl::vec2f bounds = item.text.get_bounds().size;
            size.x += bounds.x + padding_left + padding_right;
        }
        if(items.size() > 1)
            size.x += (items.size() - 1) * spacing_scale * get_theme().window_height;
        dirty = false;
    }

    mgl::vec2f RadioButton::get_size() {
        if(!visible || items.empty())
            return {0.0f, 0.0f};

        update_if_dirty();
        return size;
    }

    void RadioButton::add_item(const std::string &text, const std::string &id) {
        items.push_back({mgl::Text(text, *font), id});
        dirty = true;
    }

    void RadioButton::set_selected_item(const std::string &id) {
        for(size_t i = 0; i < items.size(); ++i) {
            if(items[i].id == id) {
                selected_item = i;
                break;
            }
        }
    }
}