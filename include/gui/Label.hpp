#pragma once

#include "Widget.hpp"

#include <mglpp/graphics/Color.hpp>
#include <mglpp/graphics/Text.hpp>

namespace gsr {
    class Label : public Widget {
    public:
        // TODO: Allow specifying max width, at which either a line-break should occur or elipses should show
        Label(mgl::Font *font, const char *text, mgl::Color color);
        Label(const Label&) = delete;
        Label& operator=(const Label&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

        void set_text(std::string str);
        const std::string& get_text() const;
    private:
        mgl::Text text;
    };
}