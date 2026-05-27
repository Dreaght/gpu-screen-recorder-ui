#pragma once

#include "Widget.hpp"

#include <mglpp/graphics/Sprite.hpp>
#include <functional>
#include <memory>

namespace gsr {
    class Image : public Widget {
    public:
        enum class ScaleBehavior {
            SCALE,
            CLAMP,
            COVER
        };

        // Set size to {0.0f, 0.0f} for no limit. The image is scaled to the size while keeping its aspect ratio
        Image(mgl::Texture *texture, mgl::vec2f size, ScaleBehavior scale_behavior);
        Image(std::shared_ptr<mgl::Texture> texture, mgl::vec2f size, ScaleBehavior scale_behavior);
        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

        std::function<void(bool inside)> on_mouse_move;
    private:
        mgl::Sprite sprite;
        std::shared_ptr<mgl::Texture> owned_texture;
        mgl::vec2f size;
        ScaleBehavior scale_behavior;
    };
}