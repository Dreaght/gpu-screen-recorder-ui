#pragma once

#include "Clipboard.hpp"
#include "ClipboardTransfer.hpp"

namespace gsr {
    struct ClipboardDbusTarget {
        std::string service;
        std::string path;
        std::string interface;
    };

    class ClipboardDbus : public Clipboard {
    public:
        static bool is_supported(const ClipboardDbusTarget &target);

        explicit ClipboardDbus(const ClipboardDbusTarget &target);
        ~ClipboardDbus() override;
        ClipboardDbus(const ClipboardDbus&) = delete;
        ClipboardDbus& operator=(const ClipboardDbus&) = delete;

        void set_current_file(const std::string &filepath, FileType file_type) override;
    private:
        ClipboardDbusTarget target;
        ClipboardTransferFilePtr current_file;
    };
}
