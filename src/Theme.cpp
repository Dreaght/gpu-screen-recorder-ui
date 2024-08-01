#include "../include/Theme.hpp"
#include "../include/GsrInfo.hpp"
#include <assert.h>

namespace gsr {
    static Theme *theme = nullptr;

    void init_theme(const gsr::GsrInfo &gsr_info) {
        assert(!theme);
        theme = new Theme();

        switch(gsr_info.gpu_info.vendor) {
            case gsr::GpuVendor::UNKNOWN: {
                break;
            }
            case gsr::GpuVendor::AMD: {
                theme->tint_color = mgl::Color(221, 0, 49);
                break;
            }
            case gsr::GpuVendor::INTEL: {
                theme->tint_color = mgl::Color(8, 109, 183);
                break;
            }
            case gsr::GpuVendor::NVIDIA: {
                theme->tint_color = mgl::Color(118, 185, 0);
                break;
            }
        }
    }

    void deinit_theme() {
        assert(theme);
        delete theme;
        theme = nullptr;
    }

    const Theme& get_theme() {
        assert(theme);
        return *theme;
    }
}