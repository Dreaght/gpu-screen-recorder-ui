#pragma once

#include "Page.hpp"

namespace gsr {
    class ScrollablePage : public Page {
    public:
        ScrollablePage(mgl::vec2f size);
        ScrollablePage(const ScrollablePage&) = delete;
        ScrollablePage& operator=(const ScrollablePage&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override { return size; }
    private:
        float get_border_size(mgl::Window &window) const;
    private:
        mgl::vec2f size;
    };
}