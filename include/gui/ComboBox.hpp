#pragma once

#include "Widget.hpp"
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Sprite.hpp>
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
        void set_selected_item(const std::string &id);

        mgl::vec2f get_size() override;
    private:
        void update_if_dirty();
        float get_dropdown_arrow_height() const;
    private:
        struct Item {
            mgl::Text text;
            std::string id;
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