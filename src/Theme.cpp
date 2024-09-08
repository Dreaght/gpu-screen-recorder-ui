#include "../include/Theme.hpp"
#include "../include/GsrInfo.hpp"

#include <assert.h>

namespace gsr {
    static Theme *theme = nullptr;

    bool Theme::set_window_size(mgl::vec2i window_size) {
        if(std::abs(window_size.x - window_width) < 0.1f && std::abs(window_size.y - window_height) < 0.1f)
            return true;

        window_width = window_size.x;
        window_height = window_size.y;

        if(!theme->title_font.load_from_file(theme->title_font_file, std::max(16.0f, window_size.y * 0.019f)))
            return false;

        if(!theme->top_bar_font.load_from_file(theme->title_font_file, std::max(23.0f, window_size.y * 0.03f)))
            return false;

        if(!theme->body_font.load_from_file(theme->body_font_file, std::max(13.0f, window_size.y * 0.015f)))
            return false;

        return true;
    }

    bool init_theme(const GsrInfo &gsr_info, const std::string &resources_path) {
        if(theme)
            return true;

        theme = new Theme();

        switch(gsr_info.gpu_info.vendor) {
            case GpuVendor::UNKNOWN: {
                break;
            }
            case GpuVendor::AMD: {
                theme->tint_color = mgl::Color(221, 0, 49);
                break;
            }
            case GpuVendor::INTEL: {
                theme->tint_color = mgl::Color(8, 109, 183);
                break;
            }
            case GpuVendor::NVIDIA: {
                theme->tint_color = mgl::Color(118, 185, 0);
                break;
            }
        }

        if(!theme->body_font_file.load((resources_path + "fonts/NotoSans-Regular.ttf").c_str(), mgl::MemoryMappedFile::LoadOptions{true, false}))
            goto error;

        if(!theme->title_font_file.load((resources_path + "fonts/NotoSans-Bold.ttf").c_str(), mgl::MemoryMappedFile::LoadOptions{true, false}))
            goto error;

        if(!theme->combobox_arrow_texture.load_from_file((resources_path + "images/combobox_arrow.png").c_str()))
            goto error;

        if(!theme->settings_texture.load_from_file((resources_path + "images/settings.png").c_str()))
            goto error;

        if(!theme->folder_texture.load_from_file((resources_path + "images/folder.png").c_str()))
            goto error;

        if(!theme->up_arrow_texture.load_from_file((resources_path + "images/up_arrow.png").c_str()))
            goto error;

        if(!theme->replay_button_texture.load_from_file((resources_path + "images/replay.png").c_str()))
            goto error;

        if(!theme->record_button_texture.load_from_file((resources_path + "images/record.png").c_str()))
            goto error;

        if(!theme->stream_button_texture.load_from_file((resources_path + "images/stream.png").c_str()))
            goto error;

        if(!theme->close_texture.load_from_file((resources_path + "images/cross.png").c_str()))
            goto error;

        if(!theme->logo_texture.load_from_file((resources_path + "images/gpu_screen_recorder_logo.png").c_str()))
            goto error;

        return true;

        error:
        deinit_theme();
        return false;
    }

    void deinit_theme() {
        if(theme) {
            delete theme;
            theme = nullptr;
        }
    }

    Theme& get_theme() {
        assert(theme);
        return *theme;
    }
}