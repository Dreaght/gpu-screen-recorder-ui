#pragma once

#include "Widget.hpp"
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Sprite.hpp>

#include <functional>
#include <string>
#include <vector>

namespace gsr {
    class ComboBox : public Widget {
    public:
        ComboBox(const char *font_desc);
        ComboBox(const ComboBox&) = delete;
        ComboBox& operator=(const ComboBox&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        void add_item(std::string_view text, const std::string &id, bool allow_duplicate = true);
        void clear_items();

        // The item can only be selected if it's enabled
        void set_selected_item(std::string_view id, bool trigger_event = true, bool trigger_event_even_if_selection_not_changed = true);
        void set_item_enabled(std::string_view id, bool enabled);
        std::string_view get_selected_id() const;

        mgl::vec2f get_size() override;

        std::function<void(std::string_view text, std::string_view id)> on_selection_changed;
    private:
        void draw_selected(mgl::Window &window, mgl::vec2f draw_pos);
        void draw_unselected(mgl::Window &window, mgl::vec2f draw_pos);
        void draw_item_outline(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size);
        void update_if_dirty();
        float get_dropdown_arrow_height() const;
    private:
        struct Item {
            mgl::Text text;
            std::string id;
            mgl::vec2f position;
            bool enabled = true;
        };

        mgl::vec2f max_size;
        std::string font_desc;
        int font_size = 0;
        std::vector<Item> items;
        mgl::Sprite dropdown_arrow;
        bool dirty = true;
        bool show_dropdown = false;
        size_t selected_item = 0;
    };
}