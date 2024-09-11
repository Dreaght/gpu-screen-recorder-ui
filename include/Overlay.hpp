#pragma once

#include "gui/PageStack.hpp"
#include "gui/CustomRendererWidget.hpp"
#include "GsrInfo.hpp"
#include "Config.hpp"
#include "window_texture.h"

#include <mglpp/window/Window.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Text.hpp>

namespace gsr {
    class DropdownButton;

    class Overlay {
    public:
        Overlay(mgl::Window &window, std::string resources_path, GsrInfo gsr_info, egl_functions egl_funcs, mgl::Color bg_color);
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;
        ~Overlay();

        void on_event(mgl::Event &event, mgl::Window &window);
        void draw(mgl::Window &window);

        void show();
        void hide();
        void toggle_show();
        bool is_open() const;
    private:
        void on_press_start_replay(const std::string &id);
        void on_press_start_record(const std::string &id);
        void on_press_start_stream(const std::string &id);
        bool update_compositor_texture(const mgl_monitor *monitor);
    private:
        mgl::Window &window;
        std::string resources_path;
        GsrInfo gsr_info;
        egl_functions egl_funcs;
        mgl::Color bg_color;
        std::vector<gsr::AudioDevice> audio_devices;
        mgl::Texture window_texture_texture;
        mgl::Sprite window_texture_sprite;
        mgl::Texture screenshot_texture;
        mgl::Sprite screenshot_sprite;
        mgl::Rectangle bg_screenshot_overlay;
        WindowTexture window_texture;
        gsr::PageStack page_stack;
        mgl::Rectangle top_bar_background;
        mgl::Text top_bar_text;
        mgl::Sprite logo_sprite;
        CustomRendererWidget close_button_widget;
        bool close_button_pressed_inside = false;
        bool visible = false;
        uint64_t default_cursor = 0;
        pid_t gpu_screen_recorder_process = -1;
        std::optional<Config> config;
        DropdownButton *replay_dropdown_button_ptr = nullptr;
        DropdownButton *record_dropdown_button_ptr = nullptr;
        DropdownButton *stream_dropdown_button_ptr = nullptr;
    };
}