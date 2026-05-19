#include "../../include/gui/TrimmerPage.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/window/Window.hpp>

namespace gsr {
    TrimmerPage::TrimmerPage(PageStack *page_stack, std::string video_path) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        page_stack(page_stack),
        video_path(std::move(video_path)),
        video_path_text(this->video_path, get_theme().title_font_desc.c_str())
    {

    }

    bool TrimmerPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;
        return true;
    }

    void TrimmerPage::draw(mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return;

        const mgl::vec2f content_page_position = get_content_position();
        const mgl::vec2f content_page_size = get_size();

        mgl::Rectangle background(content_page_size);
        background.set_position(content_page_position);
        background.set_color(get_color_theme().page_bg_color);
        window.draw(background);

        video_path_text.set_position((background.get_position() + mgl::vec2f(background.get_size().x * 0.5f - video_path_text.get_bounds().size.x * 0.5f, 0)).floor());
        window.draw(video_path_text);
    }

    mgl::vec2f TrimmerPage::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = (window_size * mgl::vec2f(0.6666f, 0.7f)).floor();
        return content_page_size;
    }

    mgl::vec2f TrimmerPage::get_content_position() {
        const mgl::vec2f window_size = mgl::vec2f(get_theme().window_width, get_theme().window_height).floor();
        const mgl::vec2f content_page_size = get_size();
        return mgl::vec2f(window_size * 0.5f - content_page_size * 0.5f).floor();
    }
}