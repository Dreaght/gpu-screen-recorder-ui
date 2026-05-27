#include "../../include/gui/Image.hpp"
#include "../../include/gui/Utils.hpp"

#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>
#include <mglpp/graphics/Texture.hpp>

namespace gsr {
    Image::Image(mgl::Texture *texture, mgl::vec2f size, ScaleBehavior scale_behavior) :
        sprite(texture), size(size), scale_behavior(scale_behavior)
    {

    }

    Image::Image(std::shared_ptr<mgl::Texture> texture, mgl::vec2f size, ScaleBehavior scale_behavior) :
        Image(texture.get(), size, scale_behavior)
    {
        owned_texture = std::move(texture);
    }

    bool Image::on_event(mgl::Event&, mgl::Window&, mgl::vec2f) {
        return true;
    }

    void Image::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos = (position + offset).floor();
        const mgl::vec2f draw_size = get_size();

        if(on_mouse_move) {
            const bool mouse_inside = mgl::FloatRect(draw_pos, draw_size).contains(window.get_mouse_position().to_vec2f());
            on_mouse_move(mouse_inside);
        }

        if(scale_behavior == ScaleBehavior::COVER && draw_size.x > 0.001f && draw_size.y > 0.001f && sprite.get_texture()) {
            const mgl::vec2f sprite_size = sprite.get_texture()->get_size().to_vec2f();
            const mgl::vec2f cover_size = cover_keep_aspect_ratio(sprite_size, size);
            const mgl::vec2f cover_pos = (draw_pos + (size - cover_size) * 0.5f).floor();
            const mgl::Scissor prev_scissor = window.get_scissor();
            window.set_scissor(scissor_get_sub_area(prev_scissor, { draw_pos.to_vec2i(), size.to_vec2i() }));
            sprite.set_size(cover_size);
            sprite.set_position(cover_pos);
            window.draw(sprite);
            window.set_scissor(prev_scissor);
            return;
        }

        sprite.set_size(draw_size);
        sprite.set_position(draw_pos);
        window.draw(sprite);
    }

    mgl::vec2f Image::get_size() {
        if(!visible || !sprite.get_texture())
            return {0.0f, 0.0f};

        const mgl::vec2f sprite_size = sprite.get_texture()->get_size().to_vec2f();
        if(size.x < 0.001f && size.y < 0.001f)
            return sprite_size;
        else if(scale_behavior == ScaleBehavior::SCALE)
            return scale_keep_aspect_ratio(sprite_size, size);
        else if(scale_behavior == ScaleBehavior::COVER)
            return size;
        else
            return clamp_keep_aspect_ratio(sprite_size, size);
    }
}