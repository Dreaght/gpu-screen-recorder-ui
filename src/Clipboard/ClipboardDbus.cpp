#include "../../include/Clipboard/ClipboardDbus.hpp"

#include <string.h>

namespace gsr {

    bool ClipboardDbus::is_supported(const ClipboardDbusTarget &target) {
        fprintf(stderr, "gsr ui: ClipboardDbus: Yeah, target = {%s, %s, %s} is supported!\n", target.service.c_str(), target.path.c_str(), target.interface.c_str());
        return true;
    }

    ClipboardDbus::ClipboardDbus(const ClipboardDbusTarget &target) : target(target) {

    }

    ClipboardDbus::~ClipboardDbus() = default;

    void ClipboardDbus::set_current_file(const std::string &filepath, FileType file_type) {
        if(filepath.empty()) {
            current_file.reset();
            return;
        }

        current_file = create_clipboard_transfer_file(filepath);
        if(!current_file) {
            fprintf(stderr, "gsr ui: error: ClipboardDbus: failed to open clipboard file %s, error: %s\n", filepath.c_str(), strerror(errno));
            return;
        }
        fprintf(stderr, "gsr ui: ClipboardDbus filepath = %s; fd = %d\n", filepath.c_str(), current_file->fd);
        current_file.reset();
    }
}
