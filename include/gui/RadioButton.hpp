#pragma once

#include "Widget.hpp"

#include <mglpp/graphics/Text.hpp>
#include <vector>
#include <functional>

namespace gsr {
    class RadioButton : public Widget {
    public:
        RadioButton(mgl::Font *font);
        RadioButton(const RadioButton&) = delete;
        RadioButton& operator=(const RadioButton&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        void add_item(const std::string &text, const std::string &id);
        void set_selected_item(const std::string &id, bool trigger_event = true);
        const std::string get_selected_id() const;

        mgl::vec2f get_size() override;

        std::function<void(const std::string &text, const std::string &id)> on_selection_changed;
    private:
        void update_if_dirty();
    private:
        struct Item {
            mgl::Text text;
            std::string id;
        };

        mgl::Font *font;
        std::vector<Item> items;
        size_t selected_item = 0;
        bool dirty = true;
        mgl::vec2f size;
    };
}