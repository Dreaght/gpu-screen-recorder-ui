#include "../../include/gui/Label.hpp"
#include <mglpp/window/Window.hpp>

namespace gsr {
    Label::Label(mgl::Font *font, const char *text, mgl::Color color) : text(text, *font) {
        this->text.set_color(color);
    }

    bool Label::on_event(mgl::Event&, mgl::Window&, mgl::vec2f) {
        return true;
    }

    void Label::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        text.set_position((position + offset).floor());
        window.draw(text);
    }

    void Label::set_text(std::string str) {
        text.set_string(std::move(str));
    }

    const std::string& Label::get_text() const {
        return text.get_string();
    }

    mgl::vec2f Label::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return text.get_bounds().size;
    }
}