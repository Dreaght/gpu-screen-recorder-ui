#pragma once

#include <mglpp/system/vec.hpp>
#include <mglpp/graphics/Color.hpp>

namespace mgl {
    class Window;
}

namespace gsr {
    // Inner border
    void draw_rectangle_outline(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size, mgl::Color color, float border_size);
    double get_frame_delta_seconds();
    void set_frame_delta_seconds(double frame_delta);
    mgl::vec2f scale_keep_aspect_ratio(mgl::vec2f from, mgl::vec2f to);
    mgl::vec2f clamp_keep_aspect_ratio(mgl::vec2f from, mgl::vec2f to);
}