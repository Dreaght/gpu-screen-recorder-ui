#include "../../include/gui/ComboBox.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Font.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <assert.h>

namespace gsr {
    static const float padding_top_scale = 0.004629f;
    static const float padding_bottom_scale = 0.004629f;
    static const float padding_left_scale = 0.007f;
    static const float padding_right_scale = 0.007f;
    static const float border_scale = 0.0015f;

    ComboBox::ComboBox(mgl::Font *font) : font(font), dropdown_arrow(&get_theme().combobox_arrow) {
        assert(font);
    }

    bool ComboBox::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const mgl::vec2f draw_pos = position + offset;
            const mgl::vec2f mouse_pos = { (float)event.mouse_button.x, (float)event.mouse_button.y };
            const mgl::vec2f item_size(max_size.x, font->get_character_size() + padding_top + padding_bottom);

            if(show_dropdown && !items.empty()) {
                mgl::vec2f pos = draw_pos + mgl::vec2f(padding_left, padding_top);
                pos.y += items[selected_item].text.get_bounds().size.y + padding_top + padding_bottom;

                for(size_t i = 0; i < items.size(); ++i) {
                    Item &item = items[i];
                    const mgl::FloatRect text_bounds = item.text.get_bounds();
                    if(mgl::FloatRect(pos - mgl::vec2f(padding_left, padding_top), item_size).contains(mouse_pos)) {
                        selected_item = i;
                        show_dropdown = false;
                        remove_widget_as_selected_in_parent();
                        return false;
                    }
                    pos.y += text_bounds.size.y + padding_top + padding_bottom;
                }
            }

            if(mgl::FloatRect(draw_pos, item_size).contains(mouse_pos)) {
                show_dropdown = !show_dropdown;
                if(show_dropdown)
                    set_widget_as_selected_in_parent();
                else
                    remove_widget_as_selected_in_parent();
            } else {
                show_dropdown = false;
                remove_widget_as_selected_in_parent();
            }
        }
        return true;
    }

    void ComboBox::draw(mgl::Window &window, mgl::vec2f offset) {
        update_if_dirty();

        if(items.empty())
            return;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        const mgl::vec2f draw_pos = (position + offset).floor();

        const mgl::vec2f item_size(max_size.x, font->get_character_size() + padding_top + padding_bottom);
        const mgl::vec2i mouse_pos = window.get_mouse_position();
        bool inside = false;

        mgl::Rectangle background(draw_pos, item_size.floor());
        if(show_dropdown) {
            background.set_size(max_size.floor());
            background.set_color(mgl::Color(0, 0, 0));
        } else {
            background.set_color(mgl::Color(0, 0, 0, 120));
        }
        window.draw(background);

        if(!show_dropdown) {
            dropdown_arrow.set_height(get_dropdown_arrow_height());
            dropdown_arrow.set_position(draw_pos + mgl::vec2f(item_size.x - dropdown_arrow.get_size().x - padding_right, item_size.y * 0.5f - dropdown_arrow.get_size().y * 0.5f).floor());
            dropdown_arrow.set_color(mgl::Color(255, 255, 255, 30));
            window.draw(dropdown_arrow);
        }

        const bool mouse_inside = mgl::FloatRect(draw_pos, item_size).contains(window.get_mouse_position().to_vec2f());
        mgl::vec2f pos = draw_pos + mgl::vec2f(padding_left, padding_top);

        Item &item = items[selected_item];
        item.text.set_position(pos.floor());
        if(show_dropdown || mouse_inside) {
            const int border_size = border_scale * get_theme().window_height;
            const mgl::Color border_color = get_theme().tint_color;
            draw_rectangle_outline(window, pos - mgl::vec2f(padding_left, padding_top), item_size.floor(), border_color, border_size);
        }
        window.draw(item.text);
        pos.y += item.text.get_bounds().size.y + padding_top + padding_bottom;

        for(size_t i = 0; i < items.size(); ++i) {
            Item &item = items[i];
            item.text.set_position(pos.floor());
            const mgl::FloatRect text_bounds = item.text.get_bounds();

            if(show_dropdown) {
                if(!inside) {
                    inside = mgl::FloatRect(text_bounds.position - mgl::vec2f(padding_left, padding_top), item_size).contains({ (float)mouse_pos.x, (float)mouse_pos.y });
                    if(inside) {
                        mgl::Rectangle item_background((text_bounds.position - mgl::vec2f(padding_left, padding_top)).floor(), item_size.floor());
                        item_background.set_color(get_theme().tint_color);
                        window.draw(item_background);
                    } else {
                        /*const int border_size = 3;
                        const mgl::Color border_color(150, 150, 150);
                        draw_rectangle_outline(window, text_bounds.position, item_size, border_color, border_size);*/
                    }
                }
                window.draw(item.text);
            }

            pos.y += text_bounds.size.y + padding_top + padding_bottom;
        }
    }

    void ComboBox::add_item(const std::string &text, const std::string &id) {
        items.push_back({mgl::Text(text, *font), id});
        dirty = true;
    }

    void ComboBox::set_selected_item(const std::string &id) {
        for(size_t i = 0; i < items.size(); ++i) {
            if(items[i].id == id) {
                selected_item = i;
                break;
            }
        }
    }

    void ComboBox::update_if_dirty() {
        if(!dirty)
            return;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        max_size = { 0.0f, font->get_character_size() + (float)padding_top + (float)padding_bottom };
        for(Item &item : items) {
            const mgl::vec2f bounds = item.text.get_bounds().size;
            max_size.x = std::max(max_size.x, bounds.x + padding_left + padding_right);
            max_size.y += bounds.y + padding_top + padding_bottom;
        }
        max_size.x += padding_left + get_dropdown_arrow_height();
        dirty = false;
    }

    mgl::vec2f ComboBox::get_size() {
        update_if_dirty();

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        return { max_size.x, font->get_character_size() + (float)padding_top + (float)padding_bottom };
    }

    float ComboBox::get_dropdown_arrow_height() const {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        return (font->get_character_size() + padding_top + padding_bottom) * 0.4f;
    }
}