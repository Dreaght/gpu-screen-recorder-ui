#pragma once

#include "Widget.hpp"
#include <functional>

#include <mglpp/graphics/Color.hpp>
#include <mglpp/graphics/Text.hpp>

namespace gsr {
    using EntryValidateHandler = std::function<bool(std::string &str)>;

    class Entry : public Widget {
    public:
        Entry(mgl::Font *font, const char *text, float max_width);
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

        void set_string(std::string str);

        // Return false to specify that the string should not be accepted. This reverts the string back to its previous value.
        // The input can be changed by changing the input parameter and returning true.
        EntryValidateHandler validate_handler;
    private:
        mgl::Text text;
        float max_width;
        bool selected = false;
        float caret_offset_x = 0.0f;
    };

    EntryValidateHandler create_entry_validator_integer_in_range(int min, int max);
}