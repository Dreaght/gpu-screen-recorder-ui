#pragma once

#include "gui/PageStack.hpp"
#include "gui/CustomRendererWidget.hpp"
#include "GsrInfo.hpp"
#include "Config.hpp"
#include "window_texture.h"
#include "WindowUtils.hpp"

#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/system/Clock.hpp>

#include <array>

namespace gsr {
    class DropdownButton;
    class GlobalHotkeys;

    enum class RecordingStatus {
        NONE,
        REPLAY,
        RECORD,
        STREAM
    };

    enum class NotificationType {
        NONE,
        RECORD,
        REPLAY,
        STREAM
    };

    class Overlay {
    public:
        Overlay(std::string resources_path, GsrInfo gsr_info, SupportedCaptureOptions capture_options, egl_functions egl_funcs);
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;
        ~Overlay();

        void handle_events(gsr::GlobalHotkeys *global_hotkeys);
        void on_event(mgl::Event &event);
        // Returns false if not visible
        bool draw();

        void show();
        void hide();
        void toggle_show();
        void toggle_record();
        void toggle_pause();
        void toggle_stream();
        void toggle_replay();
        void save_replay();
        void show_notification(const char *str, double timeout_seconds, mgl::Color icon_color, mgl::Color bg_color, NotificationType notification_type);
        bool is_open() const;
        bool should_exit(std::string &reason) const;
        void exit();

        const Config& get_config() const;
    private:
        void xi_setup();
        void handle_xi_events();
        void process_key_bindings(mgl::Event &event);
        void grab_mouse_and_keyboard();
        void xi_setup_fake_cursor();
        void xi_grab_all_mouse_devices();

        void close_gpu_screen_recorder_output();

        void update_notification_process_status();
        void save_video_in_current_game_directory(const char *video_filepath, NotificationType notification_type);
        void update_gsr_replay_save();
        void update_gsr_process_status();

        void replay_status_update_status();
        void update_focused_fullscreen_status();
        void update_power_supply_status();

        void on_stop_recording(int exit_code);

        void update_ui_recording_paused();
        void update_ui_recording_unpaused();

        void update_ui_recording_started();
        void update_ui_recording_stopped();

        void update_ui_streaming_started();
        void update_ui_streaming_stopped();

        void update_ui_replay_started();
        void update_ui_replay_stopped();

        void on_press_save_replay();
        void on_press_start_replay(bool disable_notification);
        void on_press_start_record();
        void on_press_start_stream();
        bool update_compositor_texture(const Monitor &monitor);

        void force_window_on_top();
    private:
        using KeyBindingCallback = std::function<void()>;
        struct KeyBinding {
            mgl::Event::KeyEvent key_event;
            KeyBindingCallback callback;
        };

        std::unique_ptr<mgl::Window> window;
        mgl::Event event;
        std::string resources_path;
        GsrInfo gsr_info;
        egl_functions egl_funcs;
        Config config;

        bool visible = false;

        mgl::Texture window_texture_texture;
        mgl::Sprite window_texture_sprite;
        mgl::Texture screenshot_texture;
        mgl::Sprite screenshot_sprite;
        mgl::Rectangle bg_screenshot_overlay;

        mgl::Texture cursor_texture;
        mgl::Sprite cursor_sprite;
        mgl::vec2i cursor_hotspot;

        WindowTexture window_texture;
        PageStack page_stack;
        mgl::Rectangle top_bar_background;
        mgl::Text top_bar_text;
        mgl::Sprite logo_sprite;
        CustomRendererWidget close_button_widget;
        bool close_button_pressed_inside = false;
        uint64_t default_cursor = 0;

        pid_t gpu_screen_recorder_process = -1;
        pid_t notification_process = -1;
        int gpu_screen_recorder_process_output_fd = -1;
        FILE *gpu_screen_recorder_process_output_file = nullptr;

        DropdownButton *replay_dropdown_button_ptr = nullptr;
        DropdownButton *record_dropdown_button_ptr = nullptr;
        DropdownButton *stream_dropdown_button_ptr = nullptr;

        mgl::Clock force_window_on_top_clock;

        RecordingStatus recording_status = RecordingStatus::NONE;
        bool paused = false;

        mgl::Clock replay_status_update_clock;
        std::string power_supply_online_filepath;
        bool power_supply_connected = false;
        bool focused_window_is_fullscreen = false;

        std::string record_filepath;

        Display *xi_display = nullptr;
        int xi_opcode = 0;
        XEvent *xi_input_xev = nullptr;
        XEvent *xi_output_xev = nullptr;

        std::array<KeyBinding, 1> key_bindings;
        bool drawn_first_frame = false;

        bool do_exit = false;
        std::string exit_reason;

        mgl::vec2i window_size = { 1280, 720 };
        mgl::vec2i window_pos = { 0, 0 };
    };
}