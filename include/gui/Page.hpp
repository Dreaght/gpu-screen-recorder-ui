#pragma once

#include "Widget.hpp"
#include <vector>
#include <memory>

namespace gsr {
    class Page : public Widget {
    public:
        Page() = default;
        Page(const Page&) = delete;
        Page& operator=(const Page&) = delete;
        virtual ~Page() = default;

        void add_widget(std::unique_ptr<Widget> widget);
    protected:
        std::vector<std::unique_ptr<Widget>> widgets;
    };
}