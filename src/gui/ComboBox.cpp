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

    ComboBox::ComboBox(mgl::Font *font) : font(font), dropdown_arrow(&get_theme().combobox_arrow_texture) {
        assert(font);
    }

    bool ComboBox::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(!visible)
            return true;

        if(items.empty())
            return true;

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const int padding_top = padding_top_scale * get_theme().window_height;
            const int padding_bottom = padding_bottom_scale * get_theme().window_height;

            const mgl::vec2f mouse_pos = { (float)event.mouse_button.x, (float)event.mouse_button.y };
            mgl::vec2f item_size = get_size();

            if(show_dropdown) {
                for(size_t i = 0; i < items.size(); ++i) {
                    Item &item = items[i];
                    item_size.y = padding_top + item.text.get_bounds().size.y + padding_bottom;
                    if(mgl::FloatRect(item.position, item_size).contains(mouse_pos)) {
                        const size_t prev_selected_item = selected_item;
                        selected_item = i;
                        show_dropdown = false;
                        dirty = true;
                        remove_widget_as_selected_in_parent();

                        if(selected_item != prev_selected_item && on_selection_changed)
                            on_selection_changed(item.text.get_string(), item.id);

                        return false;
                    }
                }
            }

            const mgl::vec2f draw_pos = position + offset;
            item_size = get_size();
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
        if(!visible)
            return;

        //const mgl::Scissor scissor = window.get_scissor();
        update_if_dirty();
        const mgl::vec2f draw_pos = (position + offset).floor();
        //max_size.x = std::min((scissor.position.x + scissor.size.x) - draw_pos.x, max_size.x);

        if(show_dropdown)
            draw_selected(window, draw_pos);
        else
            draw_unselected(window, draw_pos);
    }

    void ComboBox::add_item(const std::string &text, const std::string &id) {
        items.push_back({mgl::Text(text, *font), id, {0.0f, 0.0f}});
        items.back().text.set_max_width(font->get_character_size() * 20); // TODO: Make a proper solution
        //items.back().text.set_max_rows(1);
        dirty = true;
    }

    void ComboBox::set_selected_item(const std::string &id, bool trigger_event, bool trigger_event_even_if_selection_not_changed) {
        for(size_t i = 0; i < items.size(); ++i) {
            auto &item = items[i];
            if(item.id == id) {
                const size_t prev_selected_item = selected_item;
                selected_item = i;
                dirty = true;

                if(trigger_event && (trigger_event_even_if_selection_not_changed || selected_item != prev_selected_item) && on_selection_changed)
                    on_selection_changed(item.text.get_string(), item.id);

                break;
            }
        }
    }

    const std::string& ComboBox::get_selected_id() const {
        if(items.empty()) {
            static std::string dummy;
            return dummy;
        } else {
            return items[selected_item].id;
        }
    }

    void ComboBox::draw_selected(mgl::Window &window, mgl::vec2f draw_pos) {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;

        const mgl::Scissor scissor = window.get_scissor();
        const bool bottom_is_outside_scissor = draw_pos.y + max_size.y > scissor.position.y + scissor.size.y;

        mgl::vec2f item_size = get_size();
        mgl::vec2f items_draw_pos = draw_pos + mgl::vec2f(0.0f, item_size.y);

        mgl::Rectangle background(draw_pos, item_size.floor());
        background.set_size(max_size.floor());
        background.set_color(mgl::Color(0, 0, 0));
        if(bottom_is_outside_scissor) {
            background.set_position((draw_pos - mgl::vec2f(0.0f, background.get_size().y - item_size.y)).floor());
            items_draw_pos = background.get_position();
        }
        window.draw(background);

        if(selected_item < items.size()) {
            draw_item_outline(window, draw_pos, item_size);

            Item &selected_item_widget = items[selected_item];
            selected_item_widget.text.set_position(draw_pos + mgl::vec2f(padding_left, padding_top).floor());
            window.draw(selected_item_widget.text);
        }

        bool cursor_inside = false;
        const mgl::vec2f mouse_pos = window.get_mouse_position().to_vec2f();

        for(size_t i = 0; i < items.size(); ++i) {
            Item &item = items[i];
            item_size.y = padding_top + item.text.get_bounds().size.y + padding_bottom;

            if(!cursor_inside) {
                cursor_inside = mgl::FloatRect(items_draw_pos, item_size).contains(mouse_pos);
                if(cursor_inside) {
                    mgl::Rectangle item_background(items_draw_pos.floor(), item_size.floor());
                    item_background.set_color(get_color_theme().tint_color);
                    window.draw(item_background);
                }
            }

            item.text.set_position((items_draw_pos + mgl::vec2f(padding_left, padding_top)).floor());
            window.draw(item.text);

            item.position = items_draw_pos;
            items_draw_pos.y += item_size.y;
        }
    }

    void ComboBox::draw_unselected(mgl::Window &window, mgl::vec2f draw_pos) {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        mgl::vec2f item_size = get_size();
        mgl::Rectangle background(draw_pos.floor(), item_size.floor());
        background.set_color(mgl::Color(0, 0, 0, 120));
        window.draw(background);

        dropdown_arrow.set_height(get_dropdown_arrow_height());
        dropdown_arrow.set_position(draw_pos + mgl::vec2f(item_size.x - dropdown_arrow.get_size().x - padding_right, item_size.y * 0.5f - dropdown_arrow.get_size().y * 0.5f).floor());
        dropdown_arrow.set_color(mgl::Color(255, 255, 255, 30));
        window.draw(dropdown_arrow);

        if(selected_item < items.size()) {
            const mgl::vec2f mouse_pos = window.get_mouse_position().to_vec2f();
            const bool mouse_inside = mgl::FloatRect(draw_pos, item_size).contains(mouse_pos) && !has_parent_with_selected_child_widget();
            if(mouse_inside)
                draw_item_outline(window, draw_pos, item_size);

            Item &selected_item_widget = items[selected_item];
            selected_item_widget.text.set_position(draw_pos + mgl::vec2f(padding_left, padding_top).floor());
            window.draw(selected_item_widget.text);
        }
    }

    void ComboBox::draw_item_outline(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size) {
        const int border_size = std::max(1.0f, border_scale * get_theme().window_height);
        const mgl::Color border_color = get_color_theme().tint_color;
        draw_rectangle_outline(window, pos.floor(), size.floor(), border_color, border_size);
    }

    void ComboBox::update_if_dirty() {
        if(!dirty)
            return;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        Item *selected_item_ptr = (selected_item < items.size()) ? &items[selected_item] : nullptr;
        max_size = { 0.0f, padding_top + padding_bottom + (selected_item_ptr ? selected_item_ptr->text.get_bounds().size.y : font->get_character_size()) };
        for(Item &item : items) {
            const mgl::vec2f bounds = item.text.get_bounds().size;
            max_size.x = std::max(max_size.x, bounds.x + padding_left + padding_right);
            max_size.y += padding_top + bounds.y + padding_bottom;
        }

        if(max_size.x <= 0.001f)
            max_size.x = 50.0f;

        max_size.x += padding_left + get_dropdown_arrow_height();
        dirty = false;
    }

    mgl::vec2f ComboBox::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        update_if_dirty();

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        Item *selected_item_ptr = (selected_item < items.size()) ? &items[selected_item] : nullptr;
        return { max_size.x, padding_top + padding_bottom + (selected_item_ptr ? selected_item_ptr->text.get_bounds().size.y : font->get_character_size()) };
    }

    float ComboBox::get_dropdown_arrow_height() const {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        return (font->get_character_size() + padding_top + padding_bottom) * 0.4f;
    }
}