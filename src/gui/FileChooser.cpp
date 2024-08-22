#include "../../include/gui/FileChooser.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

namespace gsr {
    static const float current_directory_padding_top_scale = 0.004629f;
    static const float current_directory_padding_bottom_scale = 0.004629f;
    static const float current_directory_padding_left_scale = 0.004629f;
    static const float current_directory_padding_right_scale = 0.004629f;
    static const float spacing_between_current_directory_and_content = 0.015f;
    static const int num_columns = 6;
    static const float content_padding_top_scale = 0.015f;
    static const float content_padding_bottom_scale = 0.015f;
    static const float content_padding_left_scale = 0.015f;
    static const float content_padding_right_scale = 0.015f;

    FileChooser::FileChooser(const char *start_directory, mgl::vec2f content_size) :
        content_size(content_size), current_directory_text(start_directory, get_theme().body_font)
    {
        set_current_directory(start_directory);
    }

    bool FileChooser::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f offset) {
        if(!visible)
            return true;

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            if(double_click_timer.get_elapsed_time_seconds() <= get_theme().double_click_timeout_seconds) {
                ++times_clicked_within_timer;
            } else {
                times_clicked_within_timer = 1;
            }
            double_click_timer.restart();

            selected_item = mouse_over_item;

            if(selected_item != -1 && times_clicked_within_timer > 0 && times_clicked_within_timer % 2 == 0) {
                char new_directory[PATH_MAX];
                snprintf(new_directory, sizeof(new_directory), "%s/%s", current_directory_text.get_string().c_str(), folders[selected_item].get_string().c_str());
                set_current_directory(new_directory);
            }
        }
        return true;
    }

    void FileChooser::draw(mgl::Window &window, mgl::vec2f offset) {
        mouse_over_item = -1;

        if(!visible)
            return;

        const mgl::vec2f draw_pos = position + offset;
        const mgl::vec2f current_directory_padding(
            current_directory_padding_left_scale * get_theme().window_height + current_directory_padding_right_scale * get_theme().window_height,
            current_directory_padding_top_scale * get_theme().window_height + current_directory_padding_bottom_scale * get_theme().window_height
        );

        mgl::Rectangle current_directory_background(mgl::vec2f(content_size.x, current_directory_text.get_bounds().size.y + current_directory_padding.y).floor());
        current_directory_background.set_color(mgl::Color(0, 0, 0, 120));
        current_directory_background.set_position(draw_pos.floor());
        window.draw(current_directory_background);

        current_directory_text.set_color(get_theme().text_color);
        current_directory_text.set_position((draw_pos + mgl::vec2f(current_directory_padding.x, current_directory_background.get_size().y * 0.5f - current_directory_text.get_bounds().size.y * 0.5f)).floor());
        window.draw(current_directory_text);

        mgl::Rectangle content_background(content_size.floor());
        content_background.set_color(mgl::Color(0, 0, 0, 120));
        content_background.set_position((draw_pos + mgl::vec2f(0.0f, current_directory_background.get_size().y + spacing_between_current_directory_and_content * get_theme().window_height)).floor());
        window.draw(content_background);

        const mgl::vec2f mouse_pos = window.get_mouse_position().to_vec2f();

        const int content_padding_top = content_padding_top_scale * get_theme().window_height;
        const int content_padding_bottom = content_padding_bottom_scale * get_theme().window_height;
        const int content_padding_left = content_padding_left_scale * get_theme().window_height;
        const int content_padding_right = content_padding_right_scale * get_theme().window_height;

        const float folder_width = (int)((content_size.x - (content_padding_left + content_padding_right) * num_columns) / num_columns);
        const float width_per_item_after = content_padding_right + folder_width + content_padding_left;
        mgl::vec2f folder_pos = content_background.get_position() + mgl::vec2f(content_padding_left, content_padding_top);
        for(int i = 0; i < (int)folders.size(); ++i) {
            auto &folder_text = folders[i];

            mgl::Sprite folder_sprite(&get_theme().folder_texture);
            folder_sprite.set_position(folder_pos.floor());
            folder_sprite.set_width((int)folder_width);

            const mgl::vec2f item_pos = folder_pos - mgl::vec2f(content_padding_left, content_padding_top);
            const mgl::vec2f item_size = folder_sprite.get_size() + mgl::vec2f(content_padding_left + content_padding_right, content_padding_top + content_padding_bottom);
            if(i == selected_item) {
                mgl::Rectangle selected_item_background(item_size.floor());
                selected_item_background.set_position(item_pos.floor());
                selected_item_background.set_color(get_theme().tint_color);
                window.draw(selected_item_background);
            }
            if(!has_parent_with_selected_child_widget() && mouse_over_item == -1 && mgl::FloatRect(item_pos, item_size).contains(mouse_pos)) {
                // mgl::Rectangle selected_item_background(item_size.floor());
                // selected_item_background.set_position(item_pos.floor());
                // selected_item_background.set_color(mgl::Color(20, 20, 20, 150));
                // window.draw(selected_item_background);
                const float border_scale = 0.0015f;
                draw_rectangle_outline(window, item_pos.floor(), item_size.floor(), get_theme().tint_color, border_scale * get_theme().window_height);
                mouse_over_item = i;
            }

            window.draw(folder_sprite);

            // TODO: Dont allow text to go further left/right than item_pos (on the left side) and item_pos + item_size (on the right side).
            folder_text.set_position(folder_sprite.get_position() + mgl::vec2f(folder_sprite.get_size().x * 0.5f - folder_text.get_bounds().size.x * 0.5f, folder_sprite.get_size().y));
            window.draw(folder_text);

            folder_pos.x += width_per_item_after;
            if(folder_pos.x + folder_width > content_background.get_position().x + content_size.x) {
                folder_pos.x = content_background.get_position().x + content_padding_left;
                folder_pos.y += content_padding_bottom + folder_sprite.get_size().y + content_padding_top;
            }
        }
    }

    mgl::vec2f FileChooser::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const mgl::vec2f current_directory_padding(
            current_directory_padding_left_scale * get_theme().window_height + current_directory_padding_right_scale * get_theme().window_height,
            current_directory_padding_top_scale * get_theme().window_height + current_directory_padding_bottom_scale * get_theme().window_height
        );
        return {content_size.x, current_directory_text.get_bounds().size.y + current_directory_padding.y + spacing_between_current_directory_and_content * get_theme().window_height + content_size.y};
    }

    static bool is_dir(struct dirent *dir, const char *parent_directory) {
        if(dir->d_type == DT_DIR)
            return true;

        char filepath[PATH_MAX];
        snprintf(filepath, sizeof(filepath), "%s/%s", parent_directory, dir->d_name);

        struct stat st;
        return stat(filepath, &st) != -1 && S_ISDIR(st.st_mode);
    }

    void FileChooser::set_current_directory(const char *directory) {
        folders.clear();
        selected_item = -1;
        mouse_over_item = -1;
        current_directory_text.set_string(directory);

        DIR *d = opendir(directory);
        if(!d) {
            fprintf(stderr, "gsr-overlay error: failed to open directory: %s, error: %s\n", directory, strerror(errno));
            return;
        }

        struct dirent *dir = NULL;
        while((dir = readdir(d)) != NULL) {
            /* Ignore hidden files */
            if(dir->d_name[0] == '.' || !is_dir(dir, directory))
                continue;
            folders.push_back(mgl::Text(dir->d_name, get_theme().body_font));
        }

        closedir(d);
    }
}