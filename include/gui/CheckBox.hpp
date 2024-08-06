#pragma once

#include "Widget.hpp"

#include <mglpp/graphics/Color.hpp>
#include <mglpp/graphics/Text.hpp>

namespace gsr {
    class CheckBox : public Widget {
    public:
        CheckBox(mgl::Font *font, const char *text);
        CheckBox(const CheckBox&) = delete;
        CheckBox& operator=(const CheckBox&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        bool is_checked() const;
    private:
        mgl::vec2f get_checkbox_size();
    private:
        mgl::Text text;
        bool checked = false;
    };
}