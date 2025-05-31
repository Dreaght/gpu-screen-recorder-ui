#pragma once

#include "gui/PageStack.hpp"
#include "gui/CustomRendererWidget.hpp"
#include "GsrInfo.hpp"
#include "Config.hpp"
#include "window_texture.h"
#include "WindowUtils.hpp"
#include "GlobalHotkeys/GlobalHotkeysJoystick.hpp"
#include "AudioPlayer.hpp"
#include "RegionSelector.hpp"
#include "WindowSelector.hpp"
#include "CursorTracker/CursorTracker.hpp"

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
        STREAM,
        SCREENSHOT
    };

    class Overlay {
    public:
        Overlay(std::string resources_path, GsrInfo gsr_info, SupportedCaptureOptions capture_options, egl_functions egl_funcs);
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;
        ~Overlay();

        void handle_events();
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
        void save_replay_1_min();
        void save_replay_10_min();
        void take_screenshot();
        void take_screenshot_region();
        void show_notification(const char *str, double timeout_seconds, mgl::Color icon_color, mgl::Color bg_color, NotificationType notification_type, const char *capture_target = nullptr);
        bool is_open() const;
        bool should_exit(std::string &reason) const;
        void exit();
        void go_back_to_old_ui();

        const Config& get_config() const;

        void unbind_all_keyboard_hotkeys();
        void rebind_all_keyboard_hotkeys();
    private:
        void handle_keyboard_mapping_event();
        void on_event(mgl::Event &event);

        void create_frontpage_ui_components();
        void xi_setup();
        void handle_xi_events();
        void process_key_bindings(mgl::Event &event);
        void grab_mouse_and_keyboard();
        void xi_setup_fake_cursor();

        void close_gpu_screen_recorder_output();

        void update_notification_process_status();
        void save_video_in_current_game_directory(const char *video_filepath, NotificationType notification_type);
        void on_replay_saved(const char *replay_saved_filepath);
        void process_gsr_output();
        void update_gsr_process_status();
        void update_gsr_screenshot_process_status();

        void replay_status_update_status();
        void update_focused_fullscreen_status();
        void update_power_supply_status();
        void update_system_startup_status();

        void on_stop_recording(int exit_code, const std::string &video_filepath);

        void update_ui_recording_paused();
        void update_ui_recording_unpaused();

        void update_ui_recording_started();
        void update_ui_recording_stopped();

        void update_ui_streaming_started();
        void update_ui_streaming_stopped();

        void update_ui_replay_started();
        void update_ui_replay_stopped();

        void prepare_gsr_output_for_reading();
        void on_press_save_replay();
        void on_press_save_replay_1_min_replay();
        void on_press_save_replay_10_min_replay();
        bool on_press_start_replay(bool disable_notification, bool finished_selection);
        void on_press_start_record(bool finished_selection);
        void on_press_start_stream(bool finished_selection);
        void on_press_take_screenshot(bool finished_selection, bool force_region_capture);
        bool update_compositor_texture(const Monitor &monitor);

        std::string get_capture_target(const std::string &capture_target, const SupportedCaptureOptions &capture_options);

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
        pid_t gpu_screen_recorder_screenshot_process = -1;

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
        std::string screenshot_filepath;

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

        mgl::Clock show_overlay_clock;
        double show_overlay_timeout_seconds = 0.0;

        std::unique_ptr<GlobalHotkeys> global_hotkeys = nullptr;
        std::unique_ptr<GlobalHotkeysJoystick> global_hotkeys_js = nullptr;
        Display *x11_mapping_display = nullptr;
        XEvent x11_mapping_xev;

        mgl::Clock replay_save_clock;
        bool replay_save_show_notification = false;
        ReplayStartupMode replay_startup_mode = ReplayStartupMode::TURN_ON_AT_SYSTEM_STARTUP;
        bool try_replay_startup = true;
        bool replay_recording = false;
        int replay_save_duration_min = 0;

        AudioPlayer audio_player;
    
        RegionSelector region_selector;
        bool start_region_capture = false;
        std::function<void()> on_region_selected;

        WindowSelector window_selector;
        bool start_window_capture = false;
        std::function<void()> on_window_selected;

        std::string recording_capture_target;
        std::string screenshot_capture_target;

        std::unique_ptr<CursorTracker> cursor_tracker;
        mgl::Clock cursor_tracker_update_clock;
    };
}