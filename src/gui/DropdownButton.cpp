#include "../../include/gui/DropdownButton.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

namespace gsr {
    static const float padding_top = 10.0f;
    static const float padding_bottom = 10.0f;
    static const float padding_left = 10.0f;
    static const float padding_right = 10.0f;
    static const int border_size = 5;

    DropdownButton::DropdownButton(mgl::Font *title_font, mgl::Font *description_font, const char *title, const char *description_activated, const char *description_deactivated, mgl::Texture *icon_texture, mgl::vec2f size) :
        title_font(title_font), description_font(description_font), size(size), title(title, *title_font), description(description_deactivated, *description_font),
        description_activated(description_activated), description_deactivated(description_deactivated)
    {
        if(icon_texture && icon_texture->is_valid()) {
            icon_sprite.set_texture(icon_texture);
            icon_sprite.set_height((int)(size.y * 0.5f));
        }
        this->description.set_color(mgl::Color(150, 150, 150));
    }

    bool DropdownButton::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(event.type == mgl::Event::MouseMoved) {
            const mgl::vec2f draw_pos = position + offset;
            const mgl::vec2f collision_margin(1.0f, 1.0f); // Makes sure that multiple buttons that are next to each other wont activate at the same time when the cursor is right between them
            const bool inside = mgl::FloatRect(draw_pos + collision_margin, size - collision_margin).contains({ (float)event.mouse_move.x, (float)event.mouse_move.y });
            if(mouse_inside && !inside) {
                mouse_inside = false;
            } else if(!mouse_inside && inside) {
                mouse_inside = true;
            }
        } else if(event.type == mgl::Event::MouseButtonPressed) {
            const bool clicked_inside = mouse_inside;
            show_dropdown = clicked_inside;
            if(show_dropdown)
                move_to_top = true;
            if(mouse_inside_item >= 0 && mouse_inside_item < (int)items.size()) {
                if(on_click)
                    on_click(items[mouse_inside_item].id);
                return false;
            }
        }
        return true;
    }

    void DropdownButton::draw(mgl::Window &window, mgl::vec2f offset) {
        update_if_dirty();

        const mgl::vec2f draw_pos = position + offset;

        if(show_dropdown) {
            // Background
            {
                mgl::Rectangle rect(size);
                rect.set_position(draw_pos);
                rect.set_color(mgl::Color(0, 0, 0, 255));
                window.draw(rect);
            }

            const mgl::Color border_color = gsr::get_theme().tint_color;

            // Green line at top
            {
                mgl::Rectangle rect({ size.x, border_size });
                rect.set_position(draw_pos);
                rect.set_color(border_color);
                window.draw(rect);
            }
        } else if(mouse_inside) {
            // Background
            {
                mgl::Rectangle rect(size);
                rect.set_position(draw_pos);
                rect.set_color(mgl::Color(0, 0, 0, 255));
                window.draw(rect);
            }

            const mgl::Color border_color = gsr::get_theme().tint_color;
            draw_rectangle_outline(window, draw_pos, size, border_color, border_size);
        } else {
            // Background
            mgl::Rectangle rect(size);
            rect.set_position(draw_pos);
            rect.set_color(mgl::Color(0, 0, 0, 180));
            window.draw(rect);
        }

        const int text_margin = size.y * 0.085;

        const auto title_bounds = title.get_bounds();
        title.set_position((draw_pos + mgl::vec2f(size.x * 0.5f - title_bounds.size.x * 0.5f, text_margin)).floor());
        window.draw(title);

        const auto description_bounds = description.get_bounds();
        description.set_position((draw_pos + mgl::vec2f(size.x * 0.5f - description_bounds.size.x * 0.5f, size.y - description_bounds.size.y - text_margin)).floor());
        window.draw(description);

        if(icon_sprite.get_texture()->is_valid()) {
            icon_sprite.set_position((draw_pos + size * 0.5f - icon_sprite.get_size() * 0.5f).floor());
            window.draw(icon_sprite);
        }

        mouse_inside_item = -1;
        if(show_dropdown) {
            const mgl::vec2i mouse_pos = window.get_mouse_position();

            mgl::Rectangle dropdown_bg(max_size);
            dropdown_bg.set_position(draw_pos + mgl::vec2f(0.0f, size.y));
            dropdown_bg.set_color(mgl::Color(0, 0, 0));
            window.draw(dropdown_bg);

            mgl::vec2f item_position = dropdown_bg.get_position();
            for(size_t i = 0; i < items.size(); ++i) {
                auto &item = items[i];
                const auto text_bounds = item.text.get_bounds();
                const float item_height = padding_top + text_bounds.size.y + padding_bottom;

                if(mouse_inside_item == -1) {
                    const mgl::vec2f item_size(max_size.x, item_height);
                    const bool inside = mgl::FloatRect(item_position, item_size).contains({ (float)mouse_pos.x, (float)mouse_pos.y });
                    if(inside) {
                        draw_rectangle_outline(window, item_position, item_size, gsr::get_theme().tint_color, 5);
                        mouse_inside_item = i;
                    }
                }

                item.text.set_position((item_position + mgl::vec2f(padding_left, item_height * 0.5f - text_bounds.size.y * 0.5f)).floor());
                window.draw(item.text);
                item_position.y += item_height;
            }
        }
    }

    void DropdownButton::add_item(const std::string &text, const std::string &id) {
        items.push_back({mgl::Text(text, *title_font), id});
        dirty = true;
    }

    void DropdownButton::set_item_label(const std::string &id, const std::string &new_label) {
        for(auto &item : items) {
            if(item.id == id) {
                item.text.set_string(new_label);
                dirty = true;
                return;
            }
        }
    }

    void DropdownButton::set_activated(bool activated) {
        if(this->activated == activated)
            return;

        this->activated = activated;

        if(activated) {
            description = mgl::Text(description_activated, *description_font);
            description.set_color(get_theme().tint_color);
            icon_sprite.set_color(get_theme().tint_color);
        } else {
            description = mgl::Text(description_deactivated, *description_font);
            description.set_color(mgl::Color(150, 150, 150));
            icon_sprite.set_color(mgl::Color(255, 255, 255));
        }
    }

    void DropdownButton::update_if_dirty() {
        if(!dirty)
            return;

        max_size = { size.x, 0.0f };
        for(Item &item : items) {
            const mgl::vec2f bounds = item.text.get_bounds().size;
            max_size.x = std::max(max_size.x, bounds.x + padding_left + padding_right);
            max_size.y += bounds.y + padding_top + padding_bottom;
        }
        dirty = false;
    }

    mgl::vec2f DropdownButton::get_size() {
        update_if_dirty();
        return size;
    }
}