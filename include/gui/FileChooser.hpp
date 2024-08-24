#pragma once

#include "Widget.hpp"
#include <functional>
#include <vector>

#include <mglpp/graphics/Text.hpp>
#include <mglpp/system/Clock.hpp>

// This currently only supports displaying folders
// TODO: Support files as well

namespace gsr {
    class FileChooser : public Widget {
    public:
        FileChooser(const char *start_directory, mgl::vec2f size);
        FileChooser(const FileChooser&) = delete;
        FileChooser& operator=(const FileChooser&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;

        void set_current_directory(const char *directory);
    private:
        struct Folder {
            mgl::Text text;
            time_t last_modified_seconds = 0;
        };

        mgl::vec2f size;
        mgl::Text current_directory_text;
        int mouse_over_item = -1;
        int selected_item = -1;
        std::vector<Folder> folders;
        mgl::Clock double_click_timer;
        int times_clicked_within_timer = 0;
    };
}