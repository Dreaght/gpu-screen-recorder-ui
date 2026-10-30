#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/window/Window.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Vertex.hpp>

#include <algorithm>
#include <cstdlib> // std::abs

namespace gsr {
    static double frame_delta_seconds = 1.0;

    mgl::vec2i min_vec2i(mgl::vec2i a, mgl::vec2i b) {
        return { std::min(a.x, b.x), std::min(a.y, b.y) };
    }

    mgl::vec2i max_vec2i(mgl::vec2i a, mgl::vec2i b) {
        return { std::max(a.x, b.x), std::max(a.y, b.y) };
    }

    mgl::vec2i clamp_vec2i(mgl::vec2i value, mgl::vec2i min, mgl::vec2i max) {
        return min_vec2i(max, max_vec2i(value, min));
    }

    static mgl::Shader* get_squircle_shader() {
        const RoundedCorners &rounded_corners = get_theme().rounded_corners;
        return rounded_corners.enabled ? rounded_corners.squircle_shader.get() : nullptr;
    }

    static void draw_squircle(mgl::Window &window, mgl::Shader &shader, mgl::vec2f pos, mgl::vec2f size, mgl::Color color, float border_size) {
        const float radius = std::min(get_theme().rounded_corners.radius_scale * get_theme().window_height, std::min(size.x, size.y) * 0.5f);
        shader.set_uniform("size", size);
        shader.set_uniform("radius", radius);
        shader.set_uniform("border_size", border_size);

        const mgl::Vertex vertices[4] = {
            mgl::Vertex(mgl::vec2f(0.0f, 0.0f), mgl::vec2f(0.0f, 0.0f), color),
            mgl::Vertex(mgl::vec2f(size.x, 0.0f), mgl::vec2f(size.x, 0.0f), color),
            mgl::Vertex(size, size, color),
            mgl::Vertex(mgl::vec2f(0.0f, size.y), mgl::vec2f(0.0f, size.y), color),
        };
        window.draw(vertices, 4, mgl::PrimitiveType::Quads, pos, &shader);
    }

    static void draw_square_rectangle(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size, mgl::Color color) {
        mgl::Rectangle rect(pos, size);
        rect.set_color(color);
        window.draw(rect);
    }

    void draw_rectangle(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size, mgl::Color color) {
        pos = pos.floor();
        size = size.floor();
        mgl::Shader *squircle_shader = get_squircle_shader();
        if(squircle_shader)
            draw_squircle(window, *squircle_shader, pos, size, color, 0.0f);
        else
            draw_square_rectangle(window, pos, size, color);
    }

    // TODO: Use vertices to make it one draw call
    void draw_rectangle_outline(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size, mgl::Color color, float border_size) {
        pos = pos.floor();
        size = size.floor();
        border_size = (int)border_size;

        mgl::Shader *squircle_shader = get_squircle_shader();
        if(squircle_shader) {
            draw_squircle(window, *squircle_shader, pos, size, color, border_size);
            return;
        }

        draw_square_rectangle(window, pos, { size.x, border_size }, color);
        draw_square_rectangle(window, pos + mgl::vec2f(0.0f, size.y - border_size), { size.x, border_size }, color);
        draw_square_rectangle(window, pos + mgl::vec2f(0.0f, border_size), { border_size, size.y - border_size * 2 }, color);
        draw_square_rectangle(window, pos + mgl::vec2f(size.x - border_size, border_size), { border_size, size.y - border_size * 2 }, color);
    }

    void draw_rectangle_with_top_accent(mgl::Window &window, mgl::vec2f pos, mgl::vec2f size, mgl::Color color, mgl::Color accent_color, float accent_height) {
        draw_rectangle(window, pos, size, color);

        const mgl::Scissor prev_scissor = window.get_scissor();
        window.set_scissor(scissor_get_sub_area(prev_scissor, mgl::Scissor{ pos.floor().to_vec2i(), mgl::vec2i((int)size.x, (int)accent_height) }));
        draw_rectangle(window, pos, size, accent_color);
        window.set_scissor(prev_scissor);
    }

    double get_frame_delta_seconds() {
        return frame_delta_seconds;
    }

    void set_frame_delta_seconds(double frame_delta) {
        frame_delta_seconds = frame_delta;
    }

    mgl::vec2f scale_keep_aspect_ratio(mgl::vec2f from, mgl::vec2f to) {
        if(std::abs(from.x) <= 0.0001f || std::abs(from.y) <= 0.0001f)
            return {0.0f, 0.0f};

        const double height_to_width_ratio = (double)from.y / (double)from.x;
        from.x = to.x;
        from.y = from.x * height_to_width_ratio;
        
        if(from.y > to.y) {
            const double width_height_ratio = (double)from.x / (double)from.y;
            from.y = to.y;
            from.x = from.y * width_height_ratio;
        }

        return from;
    }

    mgl::vec2f clamp_keep_aspect_ratio(mgl::vec2f from, mgl::vec2f to) {
        if(from.x > to.x || from.y > to.y)
            return scale_keep_aspect_ratio(from, to);
        else
            return from;
    }

    mgl::Scissor scissor_get_sub_area(mgl::Scissor parent, mgl::Scissor child) {
        const mgl::vec2i pos = clamp_vec2i(child.position, parent.position, parent.position + parent.size);
        return mgl::Scissor{
            pos,
            max_vec2i(mgl::vec2i(0, 0), min_vec2i(child.position + child.size - pos, parent.position + parent.size - pos))
        };
    }
}
