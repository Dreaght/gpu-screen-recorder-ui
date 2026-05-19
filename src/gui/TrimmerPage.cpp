#include "../../include/gui/TrimmerPage.hpp"
#include "../../include/Theme.hpp"
#include "../../include/gui/VideoPlayer.hpp"
#include "../../include/gui/Utils.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>

namespace gsr {
    TrimmerPage::TrimmerPage(PageStack *page_stack, std::string video_path, const mgl::vec2i size) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        page_stack(page_stack),
        video_path(std::move(video_path)),
        size(size),
        video_path_text(this->video_path, get_theme().title_font_desc.c_str())
    {
        auto player = std::make_unique<VideoPlayer>(TrimmerPage::get_size(), this->video_path);
        player->set_position({0.0f, 0.0f});
        video_player_ptr = player.get();
        Page::add_widget(std::move(player));
    }

    bool TrimmerPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        const mgl::vec2f content_page_position = get_content_position();
        const mgl::vec2f content_page_size = get_size();
        if(video_player_ptr)
            video_player_ptr->set_size(content_page_size);
        Widget *selected_widget = selected_child_widget;

        if(selected_widget) {
            if(!selected_widget->on_event(event, window, content_page_position))
                return false;
        }

        return widgets.for_each_reverse([selected_widget, &window, &event, content_page_position](std::unique_ptr<Widget> &widget) {
            Widget *p = widget.get();
            if(p != selected_widget) {
                if(!p->on_event(event, window, content_page_position))
                    return false;
            }
            return true;
        });
    }

    void TrimmerPage::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f content_page_position = get_content_position();
        const mgl::vec2f content_page_size = get_size();
        if(video_player_ptr)
            video_player_ptr->set_size(content_page_size);

        video_path_text.set_position((content_page_position + mgl::vec2f(
            content_page_size.x * 0.5f - video_path_text.get_bounds().size.x * 0.5f,
            - video_path_text.get_bounds().size.y * 1.5f
            )).floor());
        window.draw(video_path_text);

        Widget *selected_widget = selected_child_widget;

        const mgl::Scissor prev_scissor = window.get_scissor();
        window.set_scissor(scissor_get_sub_area(prev_scissor, {content_page_position.to_vec2i(), content_page_size.to_vec2i()}));

        for(size_t i = 0; i < widgets.size(); ++i) {
            auto &widget = widgets[i];
            if(widget.get() != selected_widget)
                widget->draw(window, content_page_position);
        }

        if(selected_widget)
            selected_widget->draw(window, content_page_position);

        window.set_scissor(prev_scissor);
    }

    mgl::vec2f TrimmerPage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size.to_vec2f();
    }

    mgl::vec2f TrimmerPage::get_content_position() {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = get_size();
        return mgl::vec2f(window_size * 0.5f - content_page_size * 0.5f).floor();
    }
}
