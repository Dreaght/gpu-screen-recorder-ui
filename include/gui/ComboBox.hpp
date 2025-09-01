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
        ComboBox(mgl::Font *font);
        ComboBox(const ComboBox&) = delete;
        ComboBox& operator=(const ComboBox&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        void add_item(const std::string &text, const std::string &id);
        // The item can only be selected if it's enabled
        void set_selected_item(const std::string &id, bool trigger_event = true, bool trigger_event_even_if_selection_not_changed = true);
        void set_item_enabled(const std::string &id, bool enabled);
        const std::string& get_selected_id() const;

        mgl::vec2f get_size() override;

        std::function<void(const std::string &text, const std::string &id)> on_selection_changed;
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
        mgl::Font *font;
        std::vector<Item> items;
        mgl::Sprite dropdown_arrow;
        bool dirty = true;
        bool show_dropdown = false;
        size_t selected_item = 0;
    };
}