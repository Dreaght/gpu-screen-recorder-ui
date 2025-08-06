#pragma once

#include "Widget.hpp"
#include <functional>

#include <mglpp/graphics/Color.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Rectangle.hpp>

namespace gsr {
    class Entry;

    enum class EntryValidateHandlerResult {
        DENY,
        ALLOW,
        REPLACED
    };
    using EntryValidateHandler = std::function<EntryValidateHandlerResult(Entry &entry, const std::string &str)>;

    class Entry : public Widget {
    public:
        enum class Direction {
            LEFT,
            RIGHT
        };

        Entry(mgl::Font *font, const char *text, float max_width);
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

        EntryValidateHandlerResult set_text(std::string str);
        const std::string& get_text() const;

        // Also updates the cursor position
        void replace_text(size_t index, size_t size, const std::string &replacement);

        // Return false to specify that the string should not be accepted. This reverts the string back to its previous value.
        // The input can be changed by changing the input parameter and returning true.
        EntryValidateHandler validate_handler;

        std::function<void(const std::string &text)> on_changed;
    private:
        void move_caret_word(Direction direction, size_t max_codepoints);
        EntryValidateHandlerResult set_text_internal(std::string str);
        void draw_caret(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f caret_size);
        void draw_caret_selection(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f caret_size);
        mgl_index_codepoint_pair find_closest_caret_index_by_position(mgl::vec2f position);
    private:
        struct Caret {
            float offset_x = 0.0f;
            int utf8_index = 0;
            int byte_index = 0;
        };

        mgl::Rectangle background;
        mgl::Text text;
        float max_width;
        bool selected = false;
        bool selecting_text = false;
        bool selecting_with_keyboard = false;
        bool show_selection = false;
        Caret caret;
        Caret selection_start_caret;
        float text_overflow = 0.0f;
    };

    EntryValidateHandler create_entry_validator_integer_in_range(int min, int max);
}