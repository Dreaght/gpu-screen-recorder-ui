#pragma once

#include <mglpp/system/MemoryMappedFile.hpp>
#include <mglpp/graphics/Color.hpp>
#include <mglpp/graphics/Font.hpp>
#include <mglpp/graphics/Texture.hpp>

#include <string>

namespace gsr {
    struct GsrInfo;

    struct Theme {
        Theme() = default;
        Theme(const Theme&) = delete;
        Theme& operator=(const Theme&) = delete;

        float window_width = 0.0f;
        float window_height = 0.0f;

        mgl::Color tint_color = mgl::Color(118, 185, 0);
        mgl::Color page_bg_color = mgl::Color(38, 43, 47);
        mgl::Color text_color = mgl::Color(255, 255, 255);

        mgl::MemoryMappedFile body_font_file;
        mgl::MemoryMappedFile title_font_file;
        mgl::Font body_font;
        mgl::Font title_font;
        mgl::Font top_bar_font;

        mgl::Texture combobox_arrow_texture;
        mgl::Texture settings_texture;
        mgl::Texture folder_texture;
        mgl::Texture up_arrow_texture;
        mgl::Texture replay_button_texture;
        mgl::Texture record_button_texture;
        mgl::Texture stream_button_texture;
        mgl::Texture close_texture;
        mgl::Texture logo_texture;
        mgl::Texture checkbox_circle_texture;
        mgl::Texture checkbox_background_texture;
        mgl::Texture play_texture;
        mgl::Texture stop_texture;
        mgl::Texture pause_texture;

        double double_click_timeout_seconds = 0.4;

        // Reloads fonts
        bool set_window_size(mgl::vec2i window_size);
    };

    bool init_theme(const GsrInfo &gsr_info, const std::string &resources_path);
    void deinit_theme();

    Theme& get_theme();
}