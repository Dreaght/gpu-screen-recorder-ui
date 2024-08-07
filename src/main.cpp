
#include "../include/gui/StaticPage.hpp"
#include "../include/gui/ScrollablePage.hpp"
#include "../include/gui/DropdownButton.hpp"
#include "../include/gui/Button.hpp"
#include "../include/gui/RadioButton.hpp"
#include "../include/gui/Entry.hpp"
#include "../include/gui/CheckBox.hpp"
#include "../include/gui/ComboBox.hpp"
#include "../include/gui/Label.hpp"
#include "../include/gui/List.hpp"
#include "../include/Process.hpp"
#include "../include/Theme.hpp"
#include "../include/GsrInfo.hpp"
#include "../include/window_texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <sys/wait.h>
#include <optional>
#include <signal.h>
#include <assert.h>
#include <stack>

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <mglpp/mglpp.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/Clock.hpp>

// TODO: Make keyboard controllable for steam deck (and other controllers).
// TODO: Keep track of gpu screen recorder run by other programs to not allow recording at the same time, or something.
// TODO: Remove gpu-screen-recorder-overlay-daemon and handle that alt+z global hotkey here instead, to show/hide the window
// without restaring the program. Or make the daemon handle gpu screen recorder program state and pass that to the overlay.
// TODO: Add systray by using org.kde.StatusNotifierWatcher/etc dbus directly.
// TODO: Dont allow replay and record/stream at the same time. If we want to allow that then do that in gpu screen recorder instead
// to make it more efficient by doing record/replay/stream with the same encoded packets.
// TODO: Make sure the overlay always stays on top. Test with starting the overlay and then opening youtube in fullscreen.

#include <vector>

extern "C" {
#include <mgl/mgl.h>
}

const mgl::Color bg_color(0, 0, 0, 100);

static void usage() {
    fprintf(stderr, "usage: window-overlay\n");
    exit(1);
}

static void startup_error(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}

#define _NET_WM_STATE_REMOVE  0
#define _NET_WM_STATE_ADD     1
#define _NET_WM_STATE_TOGGLE  2

static Bool set_window_wm_state(Display *display, Window window, Atom atom) {
    Atom net_wm_state_atom = XInternAtom(display, "_NET_WM_STATE", False);
    if(!net_wm_state_atom) {
        fprintf(stderr, "Error: failed to find atom _NET_WM_STATE\n");
        return False;
    }

    XClientMessageEvent xclient;
    memset(&xclient, 0, sizeof(xclient));

    xclient.type = ClientMessage;
    xclient.window = window;
    xclient.message_type = net_wm_state_atom;
    xclient.format = 32;
    xclient.data.l[0] = _NET_WM_STATE_ADD;
    xclient.data.l[1] = atom;
    xclient.data.l[2] = 0;
    xclient.data.l[3] = 0;
    xclient.data.l[4] = 0;

    XSendEvent(display, DefaultRootWindow(display), False, SubstructureRedirectMask | SubstructureNotifyMask, (XEvent*)&xclient);
    XFlush(display);
    return True;
}

static Bool make_window_sticky(Display* display, Window window) {
    Atom net_wm_state_sticky_atom = XInternAtom(display, "_NET_WM_STATE_STICKY", False);
    if(!net_wm_state_sticky_atom) {
        fprintf(stderr, "Error: failed to find atom _NET_WM_STATE_STICKY\n");
        return False;
    }
    
    return set_window_wm_state(display, window, net_wm_state_sticky_atom);
}

static bool is_compositor_running(Display *dpy, int screen) {
    char prop_name[20];
    snprintf(prop_name, sizeof(prop_name), "_NET_WM_CM_S%d", screen);
    Atom prop_atom = XInternAtom(dpy, prop_name, False);
    return XGetSelectionOwner(dpy, prop_atom) != None;
}

static mgl::Texture texture_from_ximage(XImage *img) {
    uint8_t *texture_data = (uint8_t*)malloc(img->width * img->height * 3);
    // TODO:

    for(int y = 0; y < img->height; ++y) {
        for(int x = 0; x < img->width; ++x) {
            unsigned long pixel = XGetPixel(img, x, y);
            unsigned char red = (pixel & img->red_mask) >> 16;
            unsigned char green = (pixel & img->green_mask) >> 8;
            unsigned char blue = pixel & img->blue_mask;

            const size_t texture_data_index = (x + y * img->width) * 3;
            texture_data[texture_data_index + 0] = red;
            texture_data[texture_data_index + 1] = green;
            texture_data[texture_data_index + 2] = blue;
        }
    }

    mgl::Texture texture;
    // TODO:
    texture.load_from_memory(texture_data, img->width, img->height, MGL_IMAGE_FORMAT_RGB);
    free(texture_data);
    return texture;
}

static sig_atomic_t running = 1;
static void sigint_handler(int dummy) {
    (void)dummy;
    running = 0;
}

static char hex_value_to_str(uint8_t v) {
    if(v <= 9)
        return '0' + v;
    else if(v >= 10 && v <= 15)
        return 'A' + (v - 10);
    else
        return '0';
}

// Excludes alpha
static std::string color_to_hex_str(mgl::Color color) {
    std::string result;
    result.resize(6);

    result[0] = hex_value_to_str((color.r & 0xF0) >> 4);
    result[1] = hex_value_to_str(color.r & 0x0F);

    result[2] = hex_value_to_str((color.g & 0xF0) >> 4);
    result[3] = hex_value_to_str(color.g & 0x0F);

    result[4] = hex_value_to_str((color.b & 0xF0) >> 4);
    result[5] = hex_value_to_str(color.b & 0x0F);

    return result;
}

static Window get_window_at_cursor_position(Display *display) {
    Window root_window = None;
    Window window = None;
    int dummy_i;
    unsigned int dummy_u;
    int cursor_pos_x = 0;
    int cursor_pos_y = 0;
    XQueryPointer(display, DefaultRootWindow(display), &root_window, &window, &dummy_i, &dummy_i, &cursor_pos_x, &cursor_pos_y, &dummy_u);
    return window;
}

struct DrawableGeometry {
    int x, y, width, height;
};

static bool get_drawable_geometry(Display *display, Drawable drawable, DrawableGeometry *geometry) {
    geometry->x = 0;
    geometry->y = 0;
    geometry->width = 0;
    geometry->height = 0;

    Window root_window;
    unsigned int w, h;
    unsigned int dummy_border, dummy_depth;
    Status s = XGetGeometry(display, drawable, &root_window, &geometry->x, &geometry->y, &w, &h, &dummy_border, &dummy_depth);

    geometry->width = w;
    geometry->height = h;
    return s != Success;
}

static bool diff_int(int a, int b, int difference) {
    return std::abs(a - b) <= difference;
}

static bool is_window_fullscreen_on_monitor(Display *display, Window window, const mgl_monitor *monitor) {
    if(!window)
        return false;

    DrawableGeometry geometry;
    if(!get_drawable_geometry(display, window, &geometry))
        return false;

    const int margin = 2;
    return diff_int(geometry.x, monitor->pos.x, margin) && diff_int(geometry.y, monitor->pos.y, margin)
        && diff_int(geometry.width, monitor->size.x, margin) && diff_int(geometry.height, monitor->size.y, margin);
}

// Returns the first monitor if not found. Assumes there is at least one monitor connected.
static const mgl_monitor* find_monitor_by_cursor_position(mgl::Window &window) {
    const mgl_window *win = window.internal_window();
    assert(win->num_monitors > 0);
    for(int i = 0; i < win->num_monitors; ++i) {
        const mgl_monitor *mon = &win->monitors[i];
        if(mgl::IntRect({ mon->pos.x, mon->pos.y }, { mon->size.x, mon->size.y }).contains({ win->cursor_position.x, win->cursor_position.y }))
            return mon;
    }
    return &win->monitors[0];
}

/*
{
    {
        gsr::List::Orientation::VERTICAL,
        "Record area:",
        {
            {"Window, "window"},
            {"Focused window", "focused"},
        },
    },
    {
        gsr::List::Orientation::VERTICAL,
        "Video quality:",
        {
            {"Medium, "medium"},
            {"High", "high"},
            {"Very high", "very_high"},
        },
    }
}
*/

static void add_widgets_to_settings_page(mgl::vec2i window_size, mgl::vec2f settings_page_position, mgl::vec2f settings_page_size, gsr::Page *settings_page, gsr::Page *settings_content_page, const gsr::GsrInfo &gsr_info, const std::vector<gsr::AudioDevice> &audio_devices, std::function<void()> settings_back_button_callback) {
    auto back_button = std::make_unique<gsr::Button>(&gsr::get_theme().title_font, "Back", mgl::vec2f(window_size.x / 10, window_size.y / 15), gsr::get_theme().scrollable_page_bg_color);
    back_button->set_position(settings_page_position + mgl::vec2f(settings_page_size.x + window_size.x / 50, 0.0f).floor());
    back_button->set_border_scale(0.003f);
    back_button->on_click = settings_back_button_callback;
    settings_page->add_widget(std::move(back_button));

    auto settings_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
    {
        auto view_radio_button = std::make_unique<gsr::RadioButton>(&gsr::get_theme().body_font);
        view_radio_button->add_item("Simple view", "simple");
        view_radio_button->add_item("Advanced view", "advanced");
        view_radio_button->set_horizontal_alignment(gsr::Widget::Alignment::CENTER);
        gsr::RadioButton *view_radio_button_ptr = view_radio_button.get();
        settings_list->add_widget(std::move(view_radio_button));

        gsr::Widget *color_range_list_ptr = nullptr;
        gsr::Widget *codec_list_ptr = nullptr;
        gsr::Widget *framerate_mode_list_ptr = nullptr;

        auto record_area_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
        {
            record_area_list->add_widget(std::make_unique<gsr::Label>(&gsr::get_theme().body_font, "Record area:", gsr::get_theme().text_color));
            auto record_area_box = std::make_unique<gsr::ComboBox>(&gsr::get_theme().body_font);
            // TODO: Show options not supported but disable them
            if(gsr_info.supported_capture_options.window)
                record_area_box->add_item("Window", "window");
            if(gsr_info.supported_capture_options.focused)
                record_area_box->add_item("Focused window", "focused");
            if(gsr_info.supported_capture_options.screen)
                record_area_box->add_item("All monitors", "screen");
            for(const auto &monitor : gsr_info.supported_capture_options.monitors) {
                char name[256];
                snprintf(name, sizeof(name), "Monitor %s (%dx%d)", monitor.name.c_str(), monitor.size.x, monitor.size.y);
                record_area_box->add_item(name, monitor.name);
            }
            if(gsr_info.supported_capture_options.portal)
                record_area_box->add_item("Desktop portal", "portal");

            if(!gsr_info.supported_capture_options.monitors.empty())
                record_area_box->set_selected_item(gsr_info.supported_capture_options.monitors.front().name);
            else if(gsr_info.supported_capture_options.portal)
                record_area_box->set_selected_item("portal");

            record_area_list->add_widget(std::move(record_area_box));
        }
        settings_list->add_widget(std::move(record_area_list));

        auto audio_device_section_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
        {
            audio_device_section_list->add_widget(std::make_unique<gsr::Label>(&gsr::get_theme().body_font, "Audio:", gsr::get_theme().text_color));
            auto audio_devices_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
            gsr::List *audio_devices_list_ptr = audio_devices_list.get();

            auto add_audio_track_button = std::make_unique<gsr::Button>(&gsr::get_theme().body_font, "Add audio track", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
            auto add_audio_track = [&audio_devices, audio_devices_list_ptr]() {
                auto audio_device_list = std::make_unique<gsr::List>(gsr::List::Orientation::HORIZONTAL, gsr::List::Alignment::CENTER);
                gsr::List *audio_device_list_ptr = audio_device_list.get();
                {
                    audio_device_list->add_widget(std::make_unique<gsr::Label>(&gsr::get_theme().body_font, "  ", gsr::get_theme().text_color));
                    auto audio_device_box = std::make_unique<gsr::ComboBox>(&gsr::get_theme().body_font);
                    for(const auto &audio_device : audio_devices) {
                        audio_device_box->add_item(audio_device.description, audio_device.name);
                    }
                    audio_device_list->add_widget(std::move(audio_device_box));
                    auto remove_audio_track_button = std::make_unique<gsr::Button>(&gsr::get_theme().body_font, "Remove", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
                    remove_audio_track_button->on_click = [=]() {
                        audio_devices_list_ptr->remove_widget(audio_device_list_ptr);
                    };
                    audio_device_list->add_widget(std::move(remove_audio_track_button));
                }
                audio_devices_list_ptr->add_widget(std::move(audio_device_list));
            };
            add_audio_track_button->on_click = add_audio_track;
            audio_device_section_list->add_widget(std::move(add_audio_track_button));

            for(int i = 0; i < 3; ++i) {
                add_audio_track();
            }
            audio_device_section_list->add_widget(std::move(audio_devices_list));
        }
        settings_list->add_widget(std::move(audio_device_section_list));

        auto quality_list = std::make_unique<gsr::List>(gsr::List::Orientation::HORIZONTAL);
        {
            auto video_quality_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
            {
                video_quality_list->add_widget(std::make_unique<gsr::Label>(&gsr::get_theme().body_font, "Video quality:", gsr::get_theme().text_color));
                auto video_quality_box = std::make_unique<gsr::ComboBox>(&gsr::get_theme().body_font);
                video_quality_box->add_item("Medium", "medium");
                video_quality_box->add_item("High (Recommended for live streaming)", "high");
                video_quality_box->add_item("Very high (Recommended)", "very_high");
                video_quality_box->add_item("Ultra", "ultra");
                video_quality_box->set_selected_item("very_high");
                video_quality_list->add_widget(std::move(video_quality_box));
            }
            quality_list->add_widget(std::move(video_quality_list));

            auto color_range_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
            {
                color_range_list->add_widget(std::make_unique<gsr::Label>(&gsr::get_theme().body_font, "Color range:", gsr::get_theme().text_color));
                auto color_range_box = std::make_unique<gsr::ComboBox>(&gsr::get_theme().body_font);
                color_range_box->add_item("Limited", "limited");
                color_range_box->add_item("Full", "full");
                color_range_list->add_widget(std::move(color_range_box));
            }
            color_range_list_ptr = color_range_list.get();
            quality_list->add_widget(std::move(color_range_list));
        }
        settings_list->add_widget(std::move(quality_list));

        auto codec_list = std::make_unique<gsr::List>(gsr::List::Orientation::HORIZONTAL);
        {
            auto video_codec_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
            {
                video_codec_list->add_widget(std::make_unique<gsr::Label>(&gsr::get_theme().body_font, "Video codec:", gsr::get_theme().text_color));
                auto video_codec_box = std::make_unique<gsr::ComboBox>(&gsr::get_theme().body_font);
                // TODO: Show options not supported but disable them
                video_codec_box->add_item("Auto (Recommended)", "auto");
                if(gsr_info.supported_video_codecs.h264)
                    video_codec_box->add_item("H264", "h264");
                if(gsr_info.supported_video_codecs.hevc)
                    video_codec_box->add_item("HEVC", "hevc");
                if(gsr_info.supported_video_codecs.av1)
                    video_codec_box->add_item("AV1", "av1");
                if(gsr_info.supported_video_codecs.vp8)
                    video_codec_box->add_item("VP8", "vp8");
                if(gsr_info.supported_video_codecs.vp9)
                    video_codec_box->add_item("VP9", "vp9");
                // TODO: Add hdr options
                if(gsr_info.supported_video_codecs.h264_software)
                    video_codec_box->add_item("H264 Software Encoder (Slow, not recommended)", "h264_software");
                video_codec_list->add_widget(std::move(video_codec_box));
            }
            codec_list->add_widget(std::move(video_codec_list));

            auto audio_codec_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
            {
                audio_codec_list->add_widget(std::make_unique<gsr::Label>(&gsr::get_theme().body_font, "Audio codec:", gsr::get_theme().text_color));
                auto audio_codec_box = std::make_unique<gsr::ComboBox>(&gsr::get_theme().body_font);
                audio_codec_box->add_item("Opus (Recommended)", "opus");
                audio_codec_box->add_item("AAC", "aac");
                audio_codec_list->add_widget(std::move(audio_codec_box));
            }
            codec_list->add_widget(std::move(audio_codec_list));
        }
        codec_list_ptr = codec_list.get();
        settings_list->add_widget(std::move(codec_list));

        auto framerate_info_list = std::make_unique<gsr::List>(gsr::List::Orientation::HORIZONTAL);
        {
            auto framerate_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
            {
                framerate_list->add_widget(std::make_unique<gsr::Label>(&gsr::get_theme().body_font, "Frame rate:", gsr::get_theme().text_color));
                auto framerate_entry = std::make_unique<gsr::Entry>(&gsr::get_theme().body_font, "60", gsr::get_theme().body_font.get_character_size() * 3);
                framerate_entry->validate_handler = gsr::create_entry_validator_integer_in_range(1, 500);
                framerate_list->add_widget(std::move(framerate_entry));
            }
            framerate_info_list->add_widget(std::move(framerate_list));

            auto framerate_mode_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
            {
                framerate_mode_list->add_widget(std::make_unique<gsr::Label>(&gsr::get_theme().body_font, "Frame rate mode:", gsr::get_theme().text_color));
                auto framerate_mode_box = std::make_unique<gsr::ComboBox>(&gsr::get_theme().body_font);
                framerate_mode_box->add_item("Auto (Recommended)", "auto");
                framerate_mode_box->add_item("Constant", "cfr");
                framerate_mode_box->add_item("Variable", "vfr");
                framerate_mode_list->add_widget(std::move(framerate_mode_box));
            }
            framerate_mode_list_ptr = framerate_mode_list.get();
            framerate_info_list->add_widget(std::move(framerate_mode_list));
        }
        settings_list->add_widget(std::move(framerate_info_list));

        auto file_list = std::make_unique<gsr::List>(gsr::List::Orientation::HORIZONTAL);
        {
            auto save_directory_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
            {
                save_directory_list->add_widget(std::make_unique<gsr::Label>(&gsr::get_theme().body_font, "Directory to save the video:", gsr::get_theme().text_color));
                // TODO:
                save_directory_list->add_widget(std::make_unique<gsr::Entry>(&gsr::get_theme().body_font, "/home/dec05eba/Videos", gsr::get_theme().body_font.get_character_size() * 20));
            }
            file_list->add_widget(std::move(save_directory_list));

            auto container_list = std::make_unique<gsr::List>(gsr::List::Orientation::VERTICAL);
            {
                container_list->add_widget(std::make_unique<gsr::Label>(&gsr::get_theme().body_font, "Container:", gsr::get_theme().text_color));
                auto container_box = std::make_unique<gsr::ComboBox>(&gsr::get_theme().body_font);
                container_box->add_item("mp4", "mp4");
                container_box->add_item("mkv", "matroska");
                container_box->add_item("flv", "flv");
                container_box->add_item("mov", "mov");
                container_box->add_item("ts", "mpegts");
                container_box->add_item("m3u8", "hls");
                container_list->add_widget(std::move(container_box));
            }
            file_list->add_widget(std::move(container_list));
        }
        settings_list->add_widget(std::move(file_list));

        settings_list->add_widget(std::make_unique<gsr::CheckBox>(&gsr::get_theme().body_font, "Record cursor"));
        settings_list->add_widget(std::make_unique<gsr::CheckBox>(&gsr::get_theme().body_font, "Show recording started notification"));
        //settings_list->add_widget(std::make_unique<gsr::CheckBox>(&gsr::get_theme().body_font, "Show recording stopped notification"));
        settings_list->add_widget(std::make_unique<gsr::CheckBox>(&gsr::get_theme().body_font, "Show video saved notification"));

        view_radio_button_ptr->on_selection_changed = [=](const std::string &text, const std::string &id) {
            (void)text;
            const bool advanced_view = id == "advanced";
            color_range_list_ptr->set_visible(advanced_view);
            codec_list_ptr->set_visible(advanced_view);
            framerate_mode_list_ptr->set_visible(advanced_view);
        };

        view_radio_button_ptr->on_selection_changed("Simple", "simple");
    }
    settings_content_page->add_widget(std::move(settings_list));
}

int main(int argc, char **argv) {
    if(argc != 1)
        usage();

    signal(SIGINT, sigint_handler);

    gsr::GsrInfo gsr_info;
    // TODO:
    gsr::GsrInfoExitStatus gsr_info_exit_status = gsr::get_gpu_screen_recorder_info(&gsr_info);
    if(gsr_info_exit_status != gsr::GsrInfoExitStatus::OK) {
        fprintf(stderr, "error: failed to get gpu-screen-recorder info\n");
        exit(1);
    }

    const std::vector<gsr::AudioDevice> audio_devices = gsr::get_audio_devices();

    std::string resources_path;
    if(access("images/gpu_screen_recorder_logo.png", F_OK) == 0) {
        resources_path = "./";
    } else {
#ifdef GSR_OVERLAY_RESOURCES_PATH
        resources_path = GSR_OVERLAY_RESOURCES_PATH "/";
#else
        resources_path = "/usr/share/gsr-overlay/";
#endif
    }

    mgl::Init init;
    mgl_context *context = mgl_get_context();
    Display *display = (Display*)context->connection;

    egl_functions egl_funcs;
    egl_funcs.eglGetError = (decltype(egl_funcs.eglGetError))context->gl.eglGetProcAddress("eglGetError");
    egl_funcs.eglCreateImage = (decltype(egl_funcs.eglCreateImage))context->gl.eglGetProcAddress("eglCreateImage");
    egl_funcs.eglDestroyImage = (decltype(egl_funcs.eglDestroyImage))context->gl.eglGetProcAddress("eglDestroyImage");
    egl_funcs.glEGLImageTargetTexture2DOES = (decltype(egl_funcs.glEGLImageTargetTexture2DOES))context->gl.eglGetProcAddress("glEGLImageTargetTexture2DOES");

    if(!egl_funcs.eglGetError || !egl_funcs.eglCreateImage || !egl_funcs.eglDestroyImage || !egl_funcs.glEGLImageTargetTexture2DOES) {
        fprintf(stderr, "Error: required opengl functions not available on your system\n");
        exit(1);
    }

    const bool compositor_running = is_compositor_running(display, DefaultScreen(display));

    mgl::vec2i window_size = { 1280, 720 };
    mgl::vec2i window_pos = { 0, 0 };

    mgl::Window::CreateParams window_create_params;
    window_create_params.size = window_size;
    window_create_params.min_size = window_size;
    window_create_params.max_size = window_size;
    window_create_params.position = window_pos;
    window_create_params.hidden = true;
    window_create_params.override_redirect = true;
    window_create_params.background_color = bg_color;
    window_create_params.support_alpha = true;
    window_create_params.window_type = MGL_WINDOW_TYPE_NOTIFICATION;
    window_create_params.render_api = MGL_RENDER_API_EGL;

    mgl::Window window;
    if(!window.create("gsr overlay", window_create_params))
        startup_error("failed to create window");

    mgl_window *win = window.internal_window();
    if(win->num_monitors == 0)
        startup_error("no monitors found");

    const mgl_monitor *focused_monitor = find_monitor_by_cursor_position(window);
    window_pos.x = focused_monitor->pos.x;
    window_pos.y = focused_monitor->pos.y;
    window_size.x = focused_monitor->size.x;
    window_size.y = focused_monitor->size.y;

    window.set_size_limits(window_size, window_size);
    window.set_size(window_size);
    window.set_position(window_pos);

    if(!gsr::init_theme(gsr_info, window_size, resources_path)) {
        fprintf(stderr, "Error: failed to load theme\n");
        exit(1);
    }

    unsigned char data = 2; // Prefer being composed to allow transparency
    XChangeProperty(display, window.get_system_handle(), XInternAtom(display, "_NET_WM_BYPASS_COMPOSITOR", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

    data = 1;
    XChangeProperty(display, window.get_system_handle(), XInternAtom(display, "GAMESCOPE_EXTERNAL_OVERLAY", False), XA_CARDINAL, 32, PropModeReplace, &data, 1);

    mgl::Texture replay_button_texture;
    if(!replay_button_texture.load_from_file((resources_path + "images/replay.png").c_str()))
        startup_error("failed to load texture: images/replay.png");

    mgl::Texture record_button_texture;
    if(!record_button_texture.load_from_file((resources_path + "images/record.png").c_str()))
        startup_error("failed to load texture: images/record.png");

    mgl::Texture stream_button_texture;
    if(!stream_button_texture.load_from_file((resources_path + "images/stream.png").c_str()))
        startup_error("failed to load texture: images/stream.png");

    WindowTexture window_texture;
    bool window_texture_loaded = false;

    mgl_texture window_texture_tex;
    memset(&window_texture_tex, 0, sizeof(window_texture_tex));
    mgl::Texture window_texture_texture;
    mgl::Sprite window_texture_sprite;

    mgl::Texture screenshot_texture;
    if(!compositor_running) {
        const Window window_at_cursor_position = get_window_at_cursor_position(display);
        if(is_window_fullscreen_on_monitor(display, window_at_cursor_position, focused_monitor) && window_at_cursor_position)
            window_texture_loaded = window_texture_init(&window_texture, display, mgl_window_get_egl_display(window.internal_window()), window_at_cursor_position, egl_funcs) == 0;

        if(window_texture_loaded && window_texture.texture_id) {
            DrawableGeometry geometry;
            get_drawable_geometry(display, (Drawable)window_texture.pixmap, &geometry);

            window_texture_tex.id = window_texture.texture_id;
            window_texture_tex.width = geometry.width;
            window_texture_tex.height = geometry.height;
            window_texture_tex.format = MGL_TEXTURE_FORMAT_RGBA;
            window_texture_tex.max_width = 1 << 15;
            window_texture_tex.max_height = 1 << 15;
            window_texture_tex.pixel_coordinates = false;
            window_texture_tex.mipmap = false;
            window_texture_texture = mgl::Texture::reference(window_texture_tex);
            window_texture_sprite.set_texture(&window_texture_texture);
        } else {
            XImage *img = XGetImage(display, DefaultRootWindow(display), window_pos.x, window_pos.y, window_size.x, window_size.y, AllPlanes, ZPixmap);
            if(!img)
                fprintf(stderr, "Error: failed to take a screenshot\n");

            if(img) {
                screenshot_texture = texture_from_ximage(img);
                XDestroyImage(img);
                img = NULL;
            }
        }
    }

    mgl::Sprite screenshot_sprite;
    if(screenshot_texture.is_valid())
        screenshot_sprite.set_texture(&screenshot_texture);

    mgl::Rectangle bg_screenshot_overlay(window_size.to_vec2f());
    bg_screenshot_overlay.set_color(bg_color);

    gsr::StaticPage front_page(window_size.to_vec2f());

    const mgl::vec2f settings_page_size(window_size.x * 0.3333f, window_size.y * 0.7f);
    const mgl::vec2f settings_page_position = (window_size.to_vec2f() * 0.5f - settings_page_size * 0.5f).floor();
    const float settings_body_margin = 0.02f;

    auto replay_settings_content = std::make_unique<gsr::ScrollablePage>(settings_page_size);
    gsr::ScrollablePage *replay_settings_content_ptr = replay_settings_content.get();
    replay_settings_content->set_position(settings_page_position);
    replay_settings_content->set_margins(settings_body_margin, settings_body_margin, settings_body_margin, settings_body_margin);

    auto record_settings_content = std::make_unique<gsr::ScrollablePage>(settings_page_size);
    gsr::ScrollablePage *record_settings_content_ptr = record_settings_content.get();
    record_settings_content->set_position(settings_page_position);
    record_settings_content->set_margins(settings_body_margin, settings_body_margin, settings_body_margin, settings_body_margin);

    auto stream_settings_content = std::make_unique<gsr::ScrollablePage>(settings_page_size);
    gsr::ScrollablePage *stream_settings_content_ptr = stream_settings_content.get();
    stream_settings_content->set_position(settings_page_position);
    stream_settings_content->set_margins(settings_body_margin, settings_body_margin, settings_body_margin, settings_body_margin);

    gsr::StaticPage replay_settings_page(window_size.to_vec2f());
    replay_settings_page.add_widget(std::move(replay_settings_content));

    gsr::StaticPage record_settings_page(window_size.to_vec2f());
    record_settings_page.add_widget(std::move(record_settings_content));

    gsr::StaticPage stream_settings_page(window_size.to_vec2f());
    stream_settings_page.add_widget(std::move(stream_settings_content));

    std::stack<gsr::Page*> page_stack;
    page_stack.push(&front_page);

    struct MainButton {
        gsr::DropdownButton* button;
        gsr::GsrMode mode;
    };

    const int num_frontpage_buttons = 3;

    const char *titles[num_frontpage_buttons] = {
        "Instant Replay",
        "Record",
        "Livestream"
    };

    const char *descriptions_off[num_frontpage_buttons] = {
        "Off",
        "Not recording",
        "Not streaming"
    };

    const char *descriptions_on[num_frontpage_buttons] = {
        "On",
        "Recording",
        "Streaming"
    };

    mgl::Texture *textures[num_frontpage_buttons] = {
        &replay_button_texture,
        &record_button_texture,
        &stream_button_texture
    };

    const int button_height = window_size.y / 5.0f;
    const int button_width = button_height;

    std::vector<MainButton> main_buttons;

    for(int i = 0; i < num_frontpage_buttons; ++i) {
        auto button = std::make_unique<gsr::DropdownButton>(&gsr::get_theme().title_font, &gsr::get_theme().body_font, titles[i], descriptions_on[i], descriptions_off[i], textures[i], mgl::vec2f(button_width, button_height));
        button->add_item("Start", "start");
        button->add_item("Settings", "settings");
        gsr::DropdownButton *button_ptr = button.get();
        front_page.add_widget(std::move(button));

        MainButton main_button = {
            button_ptr,
            gsr::GsrMode::Unknown
        };

        main_buttons.push_back(std::move(main_button));
    }

    gsr::GsrMode gsr_mode = gsr::GsrMode::Unknown;

    auto update_overlay_shape = [&]() {
        fprintf(stderr, "update overlay shape!\n");
        const int spacing = 0;// * get_config().scale;
        const int combined_spacing = spacing * std::max(0, (int)main_buttons.size() - 1);

        const int per_button_width = main_buttons[0].button->get_size().x;// * get_config().scale;
        const mgl::vec2i overlay_desired_size(per_button_width * (int)main_buttons.size() + combined_spacing, main_buttons[0].button->get_size().y);

        const mgl::vec2i main_buttons_start_pos = mgl::vec2i(window_size.x*0.5f, window_size.y*0.25f) - overlay_desired_size/2;
        mgl::vec2i main_button_pos = main_buttons_start_pos;

        // if(!gsr_mode.has_value()) {
        //     gsr_mode = gsr::GsrMode::Unknown;
        //     pid_t gpu_screen_recorder_process = -1;
        //     gsr::is_gpu_screen_recorder_running(gpu_screen_recorder_process, gsr_mode.value());
        // }

        for(size_t i = 0; i < main_buttons.size(); ++i) {
            if(main_buttons[i].mode != gsr::GsrMode::Unknown && main_buttons[i].mode == gsr_mode) {
                main_buttons[i].button->set_activated(true);
            } else {
                main_buttons[i].button->set_activated(false);
            }

            main_buttons[i].button->set_position(main_button_pos.to_vec2f());
            main_button_pos.x += per_button_width + combined_spacing;
        }
    };

    // Replay
    main_buttons[0].button->on_click = [&](const std::string &id) {
        if(id == "settings") {
            page_stack.push(&replay_settings_page);
            return;
        }
        /*
        char window_to_record_str[32];
        snprintf(window_to_record_str, sizeof(window_to_record_str), "%ld", target_window);

        const char *args[] = {
            "gpu-screen-recorder", "-w", window_to_record_str,
            "-c", "mp4",
            "-f", "60",
            "-o", "/home/dec05eba/Videos/gpu-screen-recorder.mp4",
            nullptr
        };
        gsr::exec_program_daemonized(args);
        */
    };
    main_buttons[0].mode = gsr::GsrMode::Replay;

    // TODO: Monitor /tmp/gpu-screen-recorder and update ui to match state

    pid_t gpu_screen_recorder_process = -1;
    // Record
    main_buttons[1].button->on_click = [&](const std::string &id) {
        if(id == "settings") {
            page_stack.push(&record_settings_page);
            return;
        }

        if(id != "start")
            return;

        // window.close();
        // usleep(1000 * 50); // 50 milliseconds

        const std::string tint_color_as_hex = color_to_hex_str(gsr::get_theme().tint_color);

        if(gpu_screen_recorder_process != -1) {
            kill(gpu_screen_recorder_process, SIGINT);
            int status;
            if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
                perror("waitpid failed");
                /* Ignore... */
            }
            // window.set_visible(false);
            // window.close();
            // return;
            //exit(0);
            gpu_screen_recorder_process = -1;
            gsr_mode = gsr::GsrMode::Unknown;
            update_overlay_shape();
            main_buttons[1].button->set_item_label(id, "Start");

            // TODO: Show this with a slight delay to make sure it doesn't show up in the video
            const std::string record_image_filepath = resources_path + "images/record.png";
            const char *notification_args[] = {
                "gsr-notify", "--text", "Recording has been saved", "--timeout", "3.0",
                "--icon", record_image_filepath.c_str(),
                "--icon-color", "ffffff", "--bg-color", tint_color_as_hex.c_str(),
                nullptr
            };
            gsr::exec_program_daemonized(notification_args);
            return;
        }

        const char *args[] = {
            "gpu-screen-recorder", "-w", "screen",
            "-c", "mp4",
            "-f", "60",
            "-o", "/home/dec05eba/Videos/gpu-screen-recorder.mp4",
            nullptr
        };
        gpu_screen_recorder_process = gsr::exec_program(args);
        if(gpu_screen_recorder_process == -1) {
            // TODO: Show notification failed to start
        } else {
            gsr_mode = gsr::GsrMode::Record;
            update_overlay_shape();
            main_buttons[1].button->set_item_label(id, "Stop");
        }

        // TODO: Start recording after this notification has disappeared to make sure it doesn't show up in the video.
        // Make clear to the user that the recording starts after the notification is gone.
        // Maybe have the option in notification to show timer until its getting hidden, then the notification can say:
        // Starting recording in 3...
        // 2...
        // 1...
        // TODO: Do not run this is a daemon. Instead get the pid and when launching another notification close the current notification
        // program and start another one. This can also be used to check when the notification has finished by checking with waitpid NOWAIT
        // to see when the program has exit.
        const std::string record_image_filepath = resources_path + "images/record.png";
        const char *notification_args[] = {
            "gsr-notify", "--text", "Recording has started", "--timeout", "3.0",
            "--icon", record_image_filepath.c_str(),
            "--icon-color", tint_color_as_hex.c_str(), "--bg-color", tint_color_as_hex.c_str(),
            nullptr
        };
        gsr::exec_program_daemonized(notification_args);
        //exit(0);
        // window.set_visible(false);
        // window.close();

        // TODO: Show notification with args:
        // "Recording has started" 3.0 ./images/record.png 76b900
    };
    main_buttons[1].mode = gsr::GsrMode::Record;

    // Stream
    main_buttons[2].button->on_click = [&](const std::string &id) {
        if(id == "settings") {
            page_stack.push(&stream_settings_page);
            return;
        }
    };
    main_buttons[2].mode = gsr::GsrMode::Stream;

    update_overlay_shape();

    window.set_visible(true);
    make_window_sticky(display, window.get_system_handle());

    Cursor default_cursor = XCreateFontCursor(display, XC_arrow);

    // TODO: Retry if these fail.
    // TODO: Hmm, these dont work in owlboy. Maybe owlboy uses xi2 and that breaks this (does it?).
    XGrabPointer(display, window.get_system_handle(), True,
       ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
       Button1MotionMask | Button2MotionMask | Button3MotionMask | Button4MotionMask | Button5MotionMask |
       ButtonMotionMask,
       GrabModeAsync, GrabModeAsync, None, default_cursor, CurrentTime);
    XGrabKeyboard(display, window.get_system_handle(), True, GrabModeAsync, GrabModeAsync, CurrentTime);

    XSetInputFocus(display, window.get_system_handle(), RevertToParent, CurrentTime);
    XFlush(display);

    window.set_fullscreen(true);

    //XGrabServer(display);

    mgl::Rectangle top_bar_background(mgl::vec2f(window_size.x, window_size.y*0.06f).floor());
    top_bar_background.set_color(mgl::Color(0, 0, 0, 180));

    mgl::Text top_bar_text("GPU Screen Recorder", gsr::get_theme().top_bar_font);
    //top_bar_text.set_color(gsr::get_theme().tint_color);
    top_bar_text.set_position((top_bar_background.get_position() + top_bar_background.get_size()*0.5f - top_bar_text.get_bounds().size*0.5f).floor());

    const int num_settings_pages = 3;

    gsr::Page *settings_pages[num_settings_pages] = {
        &replay_settings_page,
        &record_settings_page,
        &stream_settings_page
    };

    gsr::Page *settings_content_pages[num_settings_pages] = {
        replay_settings_content_ptr,
        record_settings_content_ptr,
        stream_settings_content_ptr
    };

    const auto settings_back_button_callback = [&] {
        page_stack.pop();
    };

    for(int i = 0; i < num_settings_pages; ++i) {
        gsr::Page *settings_page = settings_pages[i];
        gsr::Page *settings_content_page = settings_content_pages[i];
        add_widgets_to_settings_page(window_size, settings_page_position, settings_page_size, settings_page, settings_content_page, gsr_info, audio_devices, settings_back_button_callback);
    }

    mgl::Texture close_texture;
    if(!close_texture.load_from_file((resources_path + "images/cross.png").c_str()))
        startup_error("failed to load texture: images/cross.png");

    mgl::Sprite close_sprite(&close_texture);
    close_sprite.set_height(int(top_bar_background.get_size().y * 0.3f));
    close_sprite.set_position(mgl::vec2f(window_size.x - close_sprite.get_size().x - 50.0f, top_bar_background.get_size().y * 0.5f - close_sprite.get_size().y * 0.5f).floor());

    mgl::Texture logo_texture;
    if(!logo_texture.load_from_file((resources_path + "images/gpu_screen_recorder_logo.png").c_str()))
        startup_error("failed to load texture: images/gpu_screen_recorder_logo.png");

    mgl::Sprite logo_sprite(&logo_texture);
    logo_sprite.set_height((int)(top_bar_background.get_size().y * 0.65f));

    logo_sprite.set_position(mgl::vec2f(
        (top_bar_background.get_size().y - logo_sprite.get_size().y) * 0.5f,
        top_bar_background.get_size().y * 0.5f - logo_sprite.get_size().y * 0.5f
    ).floor());

    mgl::Event event;

    event.type = mgl::Event::MouseMoved;
    event.mouse_move.x = window.get_mouse_position().x;
    event.mouse_move.y = window.get_mouse_position().y;
    page_stack.top()->on_event(event, window, mgl::vec2f(0.0f, 0.0f));

    const auto render = [&] {
        if(window_texture_loaded && window_texture.texture_id) {
            window.clear(mgl::Color(0, 0, 0, 255));
            window.draw(window_texture_sprite);
        } else if(screenshot_texture.is_valid()) {
            window.clear(bg_color);
            window.draw(screenshot_sprite);
            window.draw(bg_screenshot_overlay);
        }
        window.draw(top_bar_background);
        window.draw(top_bar_text);
        window.draw(logo_sprite);
        window.draw(close_sprite);
        page_stack.top()->draw(window, mgl::vec2f(0.0f, 0.0f));
        window.display();
    };

    while(window.is_open()) {
        if(page_stack.empty() || !running) {
            running = false;
            goto quit;
        }

        while(window.poll_event(event)) {
            page_stack.top()->on_event(event, window, mgl::vec2f(0.0f, 0.0f));
            if(event.type == mgl::Event::KeyReleased) {
                if(event.key.code == mgl::Keyboard::Escape && !page_stack.empty())
                    page_stack.pop();
            }

            if(page_stack.empty())
                break;
        }

        // if(state_update_timer.get_elapsed_time_seconds() >= state_update_timeout_sec) {
        //     state_update_timer.restart();
        //     update_overlay_shape();
        // }

        if(!page_stack.empty())
            render();
    }

    quit:
    fprintf(stderr, "shutting down!\n");
    if(window_texture_loaded)
        window_texture_deinit(&window_texture);
    gsr::deinit_theme();
    window.close();

    if(gpu_screen_recorder_process != -1) {
        kill(gpu_screen_recorder_process, SIGINT);
        int status;
        if(waitpid(gpu_screen_recorder_process, &status, 0) == -1) {
            perror("waitpid failed");
            /* Ignore... */
        }
        gpu_screen_recorder_process = -1;
    }

    return 0;
}
