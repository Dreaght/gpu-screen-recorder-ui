#pragma once

#include <mglpp/system/vec.hpp>
#include <mglpp/graphics/Color.hpp>

namespace mgl {
    class Window;
}

namespace gsr {
    // Inner border
    void draw_rectangle_outline(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size, mgl::Color color, float border_size);
}