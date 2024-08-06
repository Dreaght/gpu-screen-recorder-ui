#include "../../include/gui/List.hpp"
#include "../../include/Theme.hpp"

static float floor(float f) {
    return (int)f;
}

namespace gsr {
    // TODO: Add homogeneous option, using a specified max size of this list.
    static const mgl::vec2f spacing_scale(0.009f, 0.009f);

    List::List(Orientation orientation, Alignment content_alignment) : orientation(orientation), content_alignment(content_alignment) {}
    
    bool List::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(!visible)
            return true;

        // We want to store the selected child widget since it can change in the event loop below
        Widget *selected_widget = selected_child_widget;
        if(selected_widget) {
            if(!selected_widget->on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
                return false;
        }

        // Process widgets by visibility (backwards)
        for(auto it = widgets.rbegin(), end = widgets.rend(); it != end; ++it) {
            // Ignore offset because widgets are positioned with offset in ::draw, this solution is simpler
            if(it->get() != selected_widget) {
                if(!(*it)->on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
                    return false;
            }
        }

        return true;
    }

    // TODO: Maybe call this from main on all widgets instead of from draw
    void List::update() {
        //widgets.insert(widgets.back(), std::make_move_iterator(add_queue.begin()), std::make_move_iterator(add_queue.end()));
        std::move(add_queue.begin(), add_queue.end(), std::back_inserter(widgets));
        add_queue.clear();

        for(Widget *widget_to_remove : remove_queue) {
            remove_widget_immediate(widget_to_remove);
        }
        remove_queue.clear();
    }

    void List::remove_widget_immediate(Widget *widget) {
        for(auto it = widgets.begin(), end = widgets.end(); it != end; ++it) {
            if(it->get() == widget) {
                widgets.erase(it);
                return;
            }
        }
    }

    void List::draw(mgl::Window &window, mgl::vec2f offset) {
        update();

        if(!visible)
            return;

        mgl::vec2f draw_pos = position + offset;
        offset = {0.0f, 0.0f};
        Widget *selected_widget = selected_child_widget;

        // TODO: Handle start/end alignment
        const mgl::vec2f size = get_size();
        const mgl::vec2f parent_size = parent_widget ? parent_widget->get_size() : mgl::vec2f(0.0f, 0.0f);

        const mgl::vec2f spacing = (spacing_scale * get_theme().window_height).floor();
        switch(orientation) {
            case Orientation::VERTICAL: {
                for(auto &widget : widgets) {
                    if(!widget->visible)
                        continue;

                    // TODO: Do this parent widget alignment for horizontal alignment and for other types of widget alignment
                    // and other widgets.
                    // Also take this widget alignment into consideration in get_size.
                    if(widget->get_horizontal_alignment() == Widget::Alignment::CENTER && parent_size.x > 0.001f)
                        offset.x = floor(parent_size.x * 0.5f - widget->get_size().x * 0.5f);
                    else if(content_alignment == Alignment::CENTER)
                        offset.x = floor(size.x * 0.5f - widget->get_size().x * 0.5f);
                    else
                        offset.x = 0.0f;
                    widget->set_position(draw_pos + offset);
                    if(widget.get() != selected_widget)
                        widget->draw(window, mgl::vec2f(0.0f, 0.0f));
                    draw_pos.y += widget->get_size().y + spacing.y;
                }
                break;
            }
            case Orientation::HORIZONTAL: {
                for(auto &widget : widgets) {
                    if(!widget->visible)
                        continue;

                    if(content_alignment == Alignment::CENTER)
                        offset.y = floor(size.y * 0.5f - widget->get_size().y * 0.5f);
                    widget->set_position(draw_pos + offset);
                    if(widget.get() != selected_widget)
                        widget->draw(window, mgl::vec2f(0.0f, 0.0f));
                    draw_pos.x += widget->get_size().x + spacing.x;
                }
                break;
            }
        }

        if(selected_widget)
            selected_widget->draw(window, mgl::vec2f(0.0f, 0.0f));
    }

    // void List::remove_child_widget(Widget *widget) {
    //     for(auto it = widgets.begin(), end = widgets.end(); it != end; ++it) {
    //         if(it->get() == widget) {
    //             widgets.erase(it);
    //             return;
    //         }
    //     }
    // }

    void List::add_widget(std::unique_ptr<Widget> widget) {
        widget->parent_widget = this;
        // TODO: Maybe only do this if this is called inside an event handler
        //widgets.push_back(std::move(widget));
        add_queue.push_back(std::move(widget));
    }

    void List::remove_widget(Widget *widget) {
        for(auto it = add_queue.begin(), end = add_queue.end(); it != end; ++it) {
            if(it->get() == widget) {
                add_queue.erase(it);
                return;
            }
        }
        // TODO: Maybe only do this if this is called inside an event handler
        remove_queue.push_back(widget);
    }

    // TODO: Cache result
    mgl::vec2f List::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        mgl::vec2f size;
        const mgl::vec2f spacing = (spacing_scale * get_theme().window_height).floor();
        switch(orientation) {
            case Orientation::VERTICAL: {
                for(auto &widget : widgets) {
                    if(!widget->visible)
                        continue;

                    const auto widget_size = widget->get_size();
                    size.x = std::max(size.x, widget_size.x);
                    size.y += widget_size.y + spacing.y;
                }
                break;
            }
            case Orientation::HORIZONTAL: {
                for(auto &widget : widgets) {
                    if(!widget->visible)
                        continue;

                    const auto widget_size = widget->get_size();
                    size.x += widget_size.x + spacing.x;
                    size.y = std::max(size.y, widget_size.y);
                }
                break;
            }
        }
        return size;
    }
}