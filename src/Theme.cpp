#include "../include/Theme.hpp"
#include "../include/GsrInfo.hpp"
#include <assert.h>

namespace gsr {
    static Theme *theme = nullptr;

    bool init_theme(const GsrInfo &gsr_info, mgl::vec2i window_size, const std::string &resources_path) {
        if(theme)
            return true;

        theme = new Theme();

        theme->window_width = window_size.x;
        theme->window_height = window_size.y;

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

        if(!theme->body_font_file.load("/usr/share/fonts/noto/NotoSans-Regular.ttf", mgl::MemoryMappedFile::LoadOptions{true, false}))
            goto error;

        if(!theme->title_font_file.load("/usr/share/fonts/noto/NotoSans-Bold.ttf", mgl::MemoryMappedFile::LoadOptions{true, false}))
            goto error;

        if(!theme->title_font.load_from_file(theme->title_font_file, std::max(16.0f, window_size.y * 0.019f)))
            goto error;

        if(!theme->top_bar_font.load_from_file(theme->title_font_file, std::max(23.0f, window_size.y * 0.03f)))
            goto error;

        if(!theme->body_font.load_from_file(theme->body_font_file, std::max(13.0f, window_size.y * 0.015f)))
            goto error;

        if(!theme->combobox_arrow_texture.load_from_file((resources_path + "images/combobox_arrow.png").c_str(), {false, false, false}))
            goto error;

        if(!theme->settings_texture.load_from_file((resources_path + "images/settings.png").c_str(), {false, false, false}))
            goto error;

        if(!theme->folder_texture.load_from_file((resources_path + "images/folder.png").c_str(), {false, false, false}))
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