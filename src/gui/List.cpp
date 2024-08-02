#include "../../include/gui/List.hpp"

namespace gsr {
    // TODO: Make this modifiable, multiple by window size.
    // TODO: Add homogeneous option, using a specified max size of this list.
    static const mgl::vec2f spacing(30.0f, 10.0f);

    List::List(Orientation orientation) : orientation(orientation) {}
    
    bool List::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f) {
        if(selected_child_widget) {
            if(!selected_child_widget->on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
                return false;
        }

        // Process widgets by visibility (backwards)
        for(auto it = widgets.rbegin(), end = widgets.rend(); it != end; ++it) {
            // Ignore offset because widgets are positioned with offset in ::draw, this solution is simpler
            if(it->get() != selected_child_widget) {
                if(!(*it)->on_event(event, window, mgl::vec2f(0.0f, 0.0f)))
                    return false;
            }
        }

        return true;
    }

    void List::draw(mgl::Window &window, mgl::vec2f offset) {
        mgl::vec2f draw_pos = position + offset;

        // TODO: Do same vertical/horizontal alignment for items
        switch(orientation) {
            case Orientation::VERTICAL: {
                for(auto &widget : widgets) {
                    widget->set_position(draw_pos);
                    if(widget.get() != selected_child_widget)
                        widget->draw(window, mgl::vec2f(0.0f, 0.0f));
                    draw_pos.y += widget->get_size().y + spacing.y;
                }
                break;
            }
            case Orientation::HORIZONTAL: {
                for(auto &widget : widgets) {
                    widget->set_position(draw_pos);
                    if(widget.get() != selected_child_widget)
                        widget->draw(window, mgl::vec2f(0.0f, 0.0f));
                    draw_pos.x += widget->get_size().x + spacing.x;
                }
                break;
            }
        }

        if(selected_child_widget)
            selected_child_widget->draw(window, mgl::vec2f(0.0f, 0.0f));
    }

    void List::add_widget(std::unique_ptr<Widget> widget) {
        widget->parent_widget = this;
        widgets.push_back(std::move(widget));
    }

    // TODO: Cache result
    mgl::vec2f List::get_size() {
        mgl::vec2f size;
        switch(orientation) {
            case Orientation::VERTICAL: {
                for(auto &widget : widgets) {
                    const auto widget_size = widget->get_size();
                    size.x = std::max(size.x, widget_size.x);
                    size.y += widget_size.y + spacing.y;
                }
                break;
            }
            case Orientation::HORIZONTAL: {
                for(auto &widget : widgets) {
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