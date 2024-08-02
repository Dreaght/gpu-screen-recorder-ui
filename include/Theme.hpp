#pragma once

#include <mglpp/graphics/Color.hpp>

namespace gsr {
    struct GsrInfo;

    struct Theme {
        Theme() = default;
        Theme(const Theme&) = delete;
        Theme& operator=(const Theme&) = delete;

        mgl::Color tint_color = mgl::Color(118, 185, 0);
        mgl::Color scrollable_page_bg_color = mgl::Color(38, 43, 47);
        mgl::Color text_color = mgl::Color(255, 255, 255);
    };

    void init_theme(const gsr::GsrInfo &gsr_info);
    void deinit_theme();
    const Theme& get_theme();
}