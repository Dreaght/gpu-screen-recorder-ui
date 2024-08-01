#pragma once

#include <mglpp/graphics/Color.hpp>

namespace gsr {
    struct GsrInfo;

    struct Theme {
        Theme() = default;
        Theme(const Theme&) = delete;
        Theme& operator=(const Theme&) = delete;

        mgl::Color tint_color = mgl::Color(118, 185, 0);
    };

    void init_theme(const gsr::GsrInfo &gsr_info);
    const Theme& get_theme();
}