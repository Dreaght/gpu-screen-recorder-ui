#include "../../include/Tray/Tray.hpp"
#include "../../include/Tray/TraySni.hpp"
#include "../../include/Tray/TrayXEmbed.hpp"

#include <mglpp/graphics/Image.hpp>

#include <stdio.h>

namespace gsr {
    static bool load_pixmap(const std::string &filepath, TrayPixmap &pixmap) {
        mgl::Image image;
        if(!image.load_from_file(filepath.c_str())) {
            fprintf(stderr, "Error: Tray: failed to load icon %s\n", filepath.c_str());
            return false;
        }

        const int num_channels = image.get_num_channels();
        if(num_channels < 3) {
            fprintf(stderr, "Error: Tray: icon %s is not an rgb(a) image\n", filepath.c_str());
            return false;
        }

        const mgl::vec2i size = image.get_size();
        const size_t num_pixels = (size_t)size.x * (size_t)size.y;
        const unsigned char *image_data = image.data();

        pixmap.width = size.x;
        pixmap.height = size.y;
        pixmap.argb_data.resize(num_pixels * 4);
        for(size_t i = 0; i < num_pixels; ++i) {
            const unsigned char *src = image_data + i * num_channels;
            unsigned char *dst = pixmap.argb_data.data() + i * 4;
            dst[0] = num_channels == 4 ? src[3] : 255;
            dst[1] = src[0];
            dst[2] = src[1];
            dst[3] = src[2];
        }
        return true;
    }

    Tray::Tray() = default;
    Tray::~Tray() = default;

    void Tray::init(const std::string &resources_path, bool enabled) {
        const char *icon_filepaths[] = { "images/tray_idle.png", "images/tray_recording.png", "images/tray_paused.png" };
        static_assert(sizeof(icon_filepaths) / sizeof(icon_filepaths[0]) == sizeof(pixmaps) / sizeof(pixmaps[0]));
        for(size_t i = 0; i < sizeof(pixmaps) / sizeof(pixmaps[0]); ++i) {
            load_pixmap(resources_path + icon_filepaths[i], pixmaps[i]);
        }

        set_enabled(enabled);
    }

    void Tray::poll() {
        if(!enabled)
            return;

        if(sni)
            sni->poll();

        update_backend();

        if(xembed)
            xembed->poll();
    }

    void Tray::set_icon(TrayIcon new_icon) {
        if(new_icon == icon)
            return;

        icon = new_icon;
        if(sni)
            sni->set_pixmap(get_current_pixmap());
        if(xembed)
            xembed->set_pixmap(get_current_pixmap());
    }

    void Tray::set_enabled(bool enabled) {
        if(enabled == this->enabled)
            return;

        this->enabled = enabled;
        if(!enabled) {
            sni.reset();
            xembed.reset();
            xembed_failed = false;
            return;
        }

        sni = std::make_unique<TraySni>();
        sni->on_action = [this](TrayAction action) { run_action(action); };
        if(!sni->init(get_current_pixmap()))
            sni.reset();

        update_backend();
    }

    void Tray::update_backend() {
        const TraySniState sni_state = sni ? sni->get_state() : TraySniState::UNAVAILABLE;
        if(sni_state == TraySniState::REGISTERED) {
            if(xembed) {
                fprintf(stderr, "Info: Tray: a StatusNotifierItem host became available, removing the XEmbed tray icon\n");
                xembed.reset();
            }
        } else if(sni_state == TraySniState::UNAVAILABLE && !xembed && !xembed_failed) {
            fprintf(stderr, "Info: Tray: no StatusNotifierItem host available, using an XEmbed tray icon\n");
            xembed = std::make_unique<TrayXEmbed>();
            xembed->on_action = [this](TrayAction action) { run_action(action); };
            if(!xembed->init(get_current_pixmap())) {
                xembed.reset();
                xembed_failed = true;
            }
        }
    }

    void Tray::run_action(TrayAction action) {
        switch(action) {
            case TrayAction::SHOW_UI:
                if(on_show_ui)
                    on_show_ui();
                break;
            case TrayAction::QUIT:
                if(on_quit)
                    on_quit();
                break;
        }
    }

    const TrayPixmap* Tray::get_current_pixmap() const {
        return &pixmaps[(int)icon];
    }
}
