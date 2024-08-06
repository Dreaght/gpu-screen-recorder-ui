#include "../../include/gui/Entry.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>
#include <mglpp/system/Utf8.hpp>
#include <optional>

namespace gsr {
    static const float padding_top_scale = 0.004629f;
    static const float padding_bottom_scale = 0.004629f;
    static const float padding_left_scale = 0.007f;
    static const float padding_right_scale = 0.007f;
    static const float border_scale = 0.0015f;
    static const float caret_width_scale = 0.001f;

    Entry::Entry(mgl::Font *font, const char *text, float max_width) : text("", *font), max_width(max_width) {
        this->text.set_color(get_theme().text_color);
        set_string(text);
    }

    bool Entry::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            selected = mgl::FloatRect(position + offset, get_size()).contains({ (float)event.mouse_button.x, (float)event.mouse_button.y });
        } else if(event.type == mgl::Event::KeyPressed && selected) {
            if(event.key.code == mgl::Keyboard::Backspace && !text.get_string().empty()) {
                std::string str = text.get_string();
                const size_t prev_index = mgl::utf8_get_start_of_codepoint((const unsigned char*)str.c_str(), str.size(), str.size());
                str.erase(prev_index, std::string::npos);
                set_string(std::move(str));
            }
        } else if(event.type == mgl::Event::TextEntered && selected && event.text.codepoint >= 32) {
            std::string str = text.get_string();
            str.append(event.text.str, event.text.size);
            set_string(std::move(str));
        }
        return true;
    }

    void Entry::draw(mgl::Window &window, mgl::vec2f offset) {
        const mgl::vec2f draw_pos = position + offset;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;

        mgl::Rectangle background(get_size());
        background.set_position(draw_pos.floor());
        background.set_color(selected ? mgl::Color(0, 0, 0, 255) : mgl::Color(0, 0, 0, 120));
        window.draw(background);

        if(selected) {
            const int border_size = border_scale * get_theme().window_height;
            draw_rectangle_outline(window, draw_pos.floor(), get_size().floor(), get_theme().tint_color, border_size);

            const int caret_width = std::max(1.0f, caret_width_scale * get_theme().window_height);
            mgl::Rectangle caret({(float)caret_width, text.get_bounds().size.y});
            caret.set_position((draw_pos + mgl::vec2f(padding_left + caret_offset_x, padding_top)).floor());
            caret.set_color(mgl::Color(255, 255, 255));
            window.draw(caret);
        }

        text.set_position((draw_pos + mgl::vec2f(padding_left, get_size().y * 0.5f - text.get_bounds().size.y * 0.5f)).floor());
        window.draw(text);
    }

    mgl::vec2f Entry::get_size() {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        return { max_width, text.get_bounds().size.y + padding_top + padding_bottom };
    }

    void Entry::set_string(std::string str) {
        if(!validate_handler || validate_handler(str)) {
            text.set_string(std::move(str));
            caret_offset_x = text.find_character_pos(99999).x - this->text.get_position().x;
            fprintf(stderr, "caret offset: %f\n", caret_offset_x);
        }
    }

    static bool is_number(uint8_t c) {
        return c >= '0' && c <= '9';
    }

    static std::optional<int> to_integer(const std::string &str) {
        if(str.empty())
            return std::nullopt;

        size_t i = 0;
        const bool negative = str[0] == '-';
        if(negative)
            i = 1;

        int number = 0;
        for(; i < str.size(); ++i) {
            if(!is_number(str[i]))
                return false;

            const int new_number = number * 10 + (str[i] - '0');
            if(new_number < number)
                return std::nullopt; // Overflow
            number = new_number;
        }
        
        if(negative)
            number = -number;

        return number;
    }

    EntryValidateHandler create_entry_validator_integer_in_range(int min, int max) {
        return [min, max](std::string &str) {
            if(str.empty())
                return true;

            std::optional<int> number = to_integer(str);
            if(!number)
                return false;

            if(number.value() < min)
                str = std::to_string(min);
            else if(number.value() > max)
                str = std::to_string(max);
            return true;
        };
    }
}