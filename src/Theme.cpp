#include "../include/Theme.hpp"
#include "../include/GsrInfo.hpp"
#include <assert.h>

namespace gsr {
    static Theme theme;
    static bool initialized = false;

    void init_theme(const gsr::GsrInfo &gsr_info) {
        switch(gsr_info.gpu_info.vendor) {
            case gsr::GpuVendor::UNKNOWN: {
                break;
            }
            case gsr::GpuVendor::AMD: {
                theme.tint_color = mgl::Color(221, 0, 49);
                break;
            }
            case gsr::GpuVendor::INTEL: {
                theme.tint_color = mgl::Color(8, 109, 183);
                break;
            }
            case gsr::GpuVendor::NVIDIA: {
                theme.tint_color = mgl::Color(118, 185, 0);
                break;
            }
        }
        initialized = true;
    }

    const Theme& get_theme() {
        assert(initialized);
        return theme;
    }
}