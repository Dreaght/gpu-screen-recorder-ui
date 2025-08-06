#include "../../include/gui/Entry.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>
#include <mglpp/system/Utf8.hpp>
#include <optional>
#include <string.h>

namespace gsr {
    static const float padding_top_scale = 0.004629f;
    static const float padding_bottom_scale = 0.004629f;
    static const float padding_left_scale = 0.007f;
    static const float padding_right_scale = 0.007f;
    static const float border_scale = 0.0015f;
    static const float caret_width_scale = 0.001f;

    static void string_replace_all(std::string &str, char old_char, char new_char) {
        for(char &c : str) {
            if(c == old_char)
                c = new_char;
        }
    }

    Entry::Entry(mgl::Font *font, const char *text, float max_width) : text("", *font), max_width(max_width) {
        this->text.set_color(get_color_theme().text_color);
        set_text(text);
    }

    bool Entry::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return true;

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            const mgl::vec2f mouse_pos = { (float)event.mouse_button.x, (float)event.mouse_button.y };
            selected = mgl::FloatRect(position + offset, get_size()).contains(mouse_pos);
            if(selected) {
                selecting_text = true;

                const auto caret_index_mouse = find_closest_caret_index_by_position(mouse_pos);
                caret.byte_index = caret_index_mouse.byte_index;
                caret.utf8_index = caret_index_mouse.utf8_index;
                caret.offset_x = caret_index_mouse.pos.x - this->text.get_position().x;
                selection_start_caret = caret;
                show_selection = true;
            } else {
                selecting_text = false;
                selecting_with_keyboard = false;
                show_selection = false;
            }
        } else if(event.type == mgl::Event::MouseButtonReleased && event.mouse_button.button == mgl::Mouse::Left) {
            selecting_text = false;
            if(caret.byte_index == selection_start_caret.byte_index)
                show_selection = false;
        } else if(event.type == mgl::Event::MouseMoved && selected) {
            if(selecting_text) {
                const auto caret_index_mouse = find_closest_caret_index_by_position(mgl::vec2f(event.mouse_move.x, event.mouse_move.y));
                caret.byte_index = caret_index_mouse.byte_index;
                caret.utf8_index = caret_index_mouse.utf8_index;
                caret.offset_x = caret_index_mouse.pos.x - this->text.get_position().x;
                return false;
            }
        } else if(event.type == mgl::Event::KeyPressed && selected) {
            int selection_start_byte = caret.byte_index;
            int selection_end_byte = caret.byte_index;
            if(show_selection) {
                selection_start_byte = std::min(caret.byte_index, selection_start_caret.byte_index);
                selection_end_byte = std::max(caret.byte_index, selection_start_caret.byte_index);
            }

            if(event.key.code == mgl::Keyboard::Backspace) {
                if(selection_start_byte == selection_end_byte && caret.byte_index > 0)
                    selection_start_byte = mgl::utf8_get_start_of_codepoint((const unsigned char*)text.get_string().c_str(), text.get_string().size(), caret.byte_index - 1);

                replace_text(selection_start_byte, selection_end_byte - selection_start_byte, "");
            } else if(event.key.code == mgl::Keyboard::Delete) {
                if(selection_start_byte == selection_end_byte && caret.byte_index < (int)text.get_string().size()) {
                    size_t codepoint_length = 1;
                    mgl::utf8_get_codepoint_length(((const unsigned char*)text.get_string().c_str())[caret.byte_index], &codepoint_length);
                    selection_end_byte = selection_start_byte + codepoint_length;
                }

                replace_text(selection_start_byte, selection_end_byte - selection_start_byte, "");
            } else if(event.key.code == mgl::Keyboard::C && event.key.control) {
                const size_t selection_num_bytes = selection_end_byte - selection_start_byte;
                if(selection_num_bytes > 0)
                    window.set_clipboard(text.get_string().substr(selection_start_byte, selection_num_bytes));
            } else if(event.key.code == mgl::Keyboard::V && event.key.control) {
                std::string clipboard_string = window.get_clipboard_string();
                string_replace_all(clipboard_string, '\n', ' ');
                replace_text(selection_start_byte, selection_end_byte - selection_start_byte, std::move(clipboard_string));
            } else if(event.key.code == mgl::Keyboard::A && event.key.control) {
                selection_start_caret.byte_index = 0;
                selection_start_caret.utf8_index = 0;
                selection_start_caret.offset_x = 0.0f;

                caret.byte_index = text.get_string().size();
                caret.utf8_index = mgl::utf8_get_character_count((const unsigned char*)text.get_string().data(), text.get_string().size());
                // TODO: Optimize
                caret.offset_x = text.find_character_pos(caret.utf8_index).x - this->text.get_position().x;

                show_selection = true;
            } else if(event.key.code == mgl::Keyboard::Left) {
                if(!selecting_with_keyboard && show_selection)
                    show_selection = false;
                else
                    move_caret_word(Direction::LEFT, event.key.control ? 999999 : 1);

                if(!selecting_with_keyboard) {
                    selection_start_caret = caret;
                    show_selection = false;
                }
            } else if(event.key.code == mgl::Keyboard::Right) {
                if(!selecting_with_keyboard && show_selection)
                    show_selection = false;
                else
                    move_caret_word(Direction::RIGHT, event.key.control ? 999999 : 1);

                if(!selecting_with_keyboard) {
                    selection_start_caret = caret;
                    show_selection = false;
                }
            } else if(event.key.code == mgl::Keyboard::Home) {
                caret.byte_index = 0;
                caret.utf8_index = 0;
                caret.offset_x = 0.0f;

                if(!selecting_with_keyboard) {
                    selection_start_caret = caret;
                    show_selection = false;
                }
            } else if(event.key.code == mgl::Keyboard::End) {
                caret.byte_index = text.get_string().size();
                caret.utf8_index = mgl::utf8_get_character_count((const unsigned char*)text.get_string().data(), text.get_string().size());
                // TODO: Optimize
                caret.offset_x = text.find_character_pos(caret.utf8_index).x - this->text.get_position().x;

                if(!selecting_with_keyboard) {
                    selection_start_caret = caret;
                    show_selection = false;
                }
            } else if(event.key.code == mgl::Keyboard::LShift || event.key.code == mgl::Keyboard::RShift) {
                if(!show_selection)
                    selection_start_caret = caret;
                selecting_with_keyboard = true;
                show_selection = true;
            }

            return false;
        } else if(event.type == mgl::Event::KeyReleased && selected) {
            if(event.key.code == mgl::Keyboard::LShift || event.key.code == mgl::Keyboard::RShift) {
                selecting_with_keyboard = false;
            }

            return false;
        } else if(event.type == mgl::Event::TextEntered && selected && event.text.codepoint >= 32 && event.text.codepoint != 127) {
            int selection_start_byte = caret.byte_index;
            int selection_end_byte = caret.byte_index;
            if(show_selection) {
                selection_start_byte = std::min(caret.byte_index, selection_start_caret.byte_index);
                selection_end_byte = std::max(caret.byte_index, selection_start_caret.byte_index);
            }

            replace_text(selection_start_byte, selection_end_byte - selection_start_byte, std::string(event.text.str, event.text.size));
            return false;
        }

        return true;
    }

    void Entry::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int padding_right = padding_right_scale * get_theme().window_height;

        background.set_size(get_size());
        background.set_position(draw_pos.floor());
        background.set_color(selected ? mgl::Color(0, 0, 0, 255) : mgl::Color(0, 0, 0, 120));
        window.draw(background);

        const int caret_width = std::max(1.0f, caret_width_scale * get_theme().window_height);
        const mgl::vec2f caret_size = mgl::vec2f(caret_width, text.get_bounds().size.y).floor();

        const float overflow_left = (caret.offset_x + padding_left) - (padding_left + text_overflow);
        if(overflow_left < 0.0f)
            text_overflow += overflow_left;

        const float overflow_right = (caret.offset_x + padding_left) - (background.get_size().x - padding_right);
        if(overflow_right - text_overflow > 0.0f)
            text_overflow = overflow_right;

        text.set_position((draw_pos + mgl::vec2f(padding_left, get_size().y * 0.5f - text.get_bounds().size.y * 0.5f) - mgl::vec2f(text_overflow, 0.0f)).floor());

        const auto text_bounds = text.get_bounds();
        const bool text_larger_than_background = text_bounds.size.x > (background.get_size().x - padding_left - padding_right);
        const float text_overflow_right = (text_bounds.position.x + text_bounds.size.x) - (background.get_position().x + background.get_size().x - padding_right);
        if(text_larger_than_background) {
            if(text_overflow_right < 0.0f) {
                text_overflow += text_overflow_right;
                text.set_position(text.get_position() + mgl::vec2f(-text_overflow_right, 0.0f));
            }
        } else {
            text.set_position(text.get_position() + mgl::vec2f(-text_overflow, 0.0f));
            text_overflow = 0.0f;
        }

        if(selected) {
            const int border_size = std::max(1.0f, border_scale * get_theme().window_height);
            draw_rectangle_outline(window, draw_pos.floor(), get_size().floor(), get_color_theme().tint_color, border_size);
            draw_caret(window, draw_pos, caret_size);
        }

        const mgl::Scissor parent_scissor = window.get_scissor();
        const mgl::Scissor scissor = scissor_get_sub_area(parent_scissor,
            mgl::Scissor{
                (background.get_position() + mgl::vec2f(padding_left, padding_top)).to_vec2i(),
                (background.get_size() - mgl::vec2f(padding_left + padding_right, padding_top + padding_bottom)).to_vec2i()
            });
        window.set_scissor(scissor);

        window.draw(text);

        if(show_selection)
            draw_caret_selection(window, draw_pos, caret_size);

        window.set_scissor(parent_scissor);
    }

    void Entry::draw_caret(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f caret_size) {
        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;

        mgl::Rectangle caret_rect(caret_size);
        mgl::vec2f caret_draw_pos = draw_pos + mgl::vec2f(padding_left + caret.offset_x - text_overflow, padding_top);
        caret_rect.set_position(caret_draw_pos.floor());
        caret_rect.set_color(mgl::Color(255, 255, 255));
        window.draw(caret_rect);
    }

    void Entry::draw_caret_selection(mgl::Window &window, mgl::vec2f draw_pos, mgl::vec2f caret_size) {
        if(selection_start_caret.byte_index == caret.byte_index)
            return;

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_left = padding_left_scale * get_theme().window_height;
        const int caret_width = std::max(1.0f, caret_width_scale * get_theme().window_height);
        const int offset = caret.byte_index < selection_start_caret.byte_index ? caret_width : 0;

        mgl::Rectangle caret_selection_rect(mgl::vec2f(std::abs(selection_start_caret.offset_x - caret.offset_x) - offset, caret_size.y).floor());
        caret_selection_rect.set_position((draw_pos + mgl::vec2f(padding_left + std::min(caret.offset_x, selection_start_caret.offset_x) - text_overflow + offset, padding_top)).floor());
        mgl::Color caret_select_color = get_color_theme().tint_color;
        caret_select_color.a = 100;
        caret_selection_rect.set_color(caret_select_color);
        window.draw(caret_selection_rect);
    }

    mgl::vec2f Entry::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const int padding_top = padding_top_scale * get_theme().window_height;
        const int padding_bottom = padding_bottom_scale * get_theme().window_height;
        return { max_width, text.get_bounds().size.y + padding_top + padding_bottom };
    }

    void Entry::move_caret_word(Direction direction, size_t max_codepoints) {
        const int dir_step = direction == Direction::LEFT ? -1 : 1;
        const int num_delimiter_chars = 7;
        const char delimiter_chars[num_delimiter_chars + 1] = " \t\n/.,;";
        const unsigned char *text_str = (const unsigned char*)text.get_string().data();

        int num_non_delimiter_chars_found = 0;

        for(size_t i = 0; i < max_codepoints; ++i) {
            const int caret_byte_index_before = caret.byte_index;
            caret.byte_index = mgl::utf8_index_to_byte_index(text_str, text.get_string().size(), std::max(0, caret.utf8_index + dir_step));
            if(caret.byte_index == caret_byte_index_before)
                break;

            const size_t codepoint_start = std::min(caret_byte_index_before, caret.byte_index);
            const size_t codepoint_end = std::max(caret_byte_index_before, caret.byte_index);
            uint32_t decoded_codepoint = ' ';
            size_t codepoint_length = 1;
            mgl::utf8_decode(text_str + codepoint_start, codepoint_end - codepoint_start, &decoded_codepoint, &codepoint_length);

            const bool is_delimiter_char = !!memchr(delimiter_chars, decoded_codepoint, num_delimiter_chars);
            if(is_delimiter_char) {
                if(num_non_delimiter_chars_found > 0) {
                    caret.byte_index = caret_byte_index_before;
                    break;
                }
            } else {
                ++num_non_delimiter_chars_found;
            }

            caret.utf8_index += dir_step;
        }
        // TODO: Move right by one character instead of calculating every character to caret index
        caret.offset_x = text.find_character_pos(caret.utf8_index).x - this->text.get_position().x;
    }

    EntryValidateHandlerResult Entry::set_text(std::string str) {
        EntryValidateHandlerResult validate_result = set_text_internal(std::move(str));
        if(validate_result == EntryValidateHandlerResult::ALLOW) {
            caret.byte_index = text.get_string().size();
            caret.utf8_index = mgl::utf8_get_character_count((const unsigned char*)text.get_string().data(), text.get_string().size());
            // TODO: Optimize
            caret.offset_x = text.find_character_pos(caret.utf8_index).x - this->text.get_position().x;
            selection_start_caret = caret;

            selecting_text = false;
            selecting_with_keyboard = false;
            show_selection = false;
        }
        return validate_result;
    }

    EntryValidateHandlerResult Entry::set_text_internal(std::string str) {
        EntryValidateHandlerResult validate_result = EntryValidateHandlerResult::ALLOW;
        if(validate_handler)
            validate_result = validate_handler(*this, str);

        if(validate_result == EntryValidateHandlerResult::ALLOW) {
            text.set_string(std::move(str));
            if(on_changed)
                on_changed(text.get_string());
        }

        return validate_result;
    }

    const std::string& Entry::get_text() const {
        return text.get_string();
    }

    void Entry::replace_text(size_t index, size_t size, const std::string &replacement) {
        if(index + size > text.get_string().size())
            return;

        const auto prev_caret = caret;

        if((int)index >= caret.byte_index) {
            caret.utf8_index += mgl::utf8_get_character_count((const unsigned char*)replacement.c_str(), replacement.size());
            caret.byte_index += replacement.size();
        } else {
            caret.utf8_index -= mgl::utf8_get_character_count((const unsigned char*)(text.get_string().c_str() + caret.byte_index - size), size);
            caret.utf8_index += mgl::utf8_get_character_count((const unsigned char*)replacement.c_str(), replacement.size());
            caret.byte_index = caret.byte_index - size + replacement.size();
        }

        std::string str = text.get_string();
        str.replace(index, size, replacement);
        const EntryValidateHandlerResult validate_result = set_text_internal(std::move(str));
        if(validate_result == EntryValidateHandlerResult::DENY) {
            caret = prev_caret;
            return;
        } else if(validate_result == EntryValidateHandlerResult::REPLACED) {
            return;
        }

        // TODO: Optimize
        caret.offset_x = text.find_character_pos(caret.utf8_index).x - this->text.get_position().x;
        selection_start_caret = caret;

        selecting_text = false;
        selecting_with_keyboard = false;
        show_selection = false;
    }

    mgl_index_codepoint_pair Entry::find_closest_caret_index_by_position(mgl::vec2f position) {
        const std::string &str = text.get_string();
        mgl::Font *font = text.get_font();

        mgl_index_codepoint_pair result = {0, 0, {text.get_position().x, text.get_position().y}};

        for(; result.byte_index < str.size();) {
            uint32_t codepoint = ' ';
            size_t clen = 1;
            if(!mgl::utf8_decode((const unsigned char*)&str[result.byte_index], str.size() - result.byte_index, &codepoint, &clen))
                clen = 1;

            float glyph_width = 0.0f;
            if(codepoint == '\t') {
                const auto glyph = font->get_glyph(' ');
                const int tab_width = 4;
                glyph_width = glyph.advance * tab_width;
            } else {
                const auto glyph = font->get_glyph(codepoint);
                glyph_width = glyph.advance;
            }

            if(result.pos.x + glyph_width * 0.5f >= position.x)
                break;
            
            result.pos.x += glyph_width;
            result.byte_index += clen;
            result.utf8_index += 1;
        }

        return result;
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
                return std::nullopt;

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
        return [min, max](Entry &entry, const std::string &str) {
            if(str.empty())
                return EntryValidateHandlerResult::ALLOW;

            const std::optional<int> number = to_integer(str);
            if(!number)
                return EntryValidateHandlerResult::DENY;

            if(number.value() < min) {
                entry.set_text(std::to_string(min));
                return EntryValidateHandlerResult::REPLACED;
            } else if(number.value() > max) {
                entry.set_text(std::to_string(max));
                return EntryValidateHandlerResult::REPLACED;
            }

            return EntryValidateHandlerResult::ALLOW;
        };
    }
}