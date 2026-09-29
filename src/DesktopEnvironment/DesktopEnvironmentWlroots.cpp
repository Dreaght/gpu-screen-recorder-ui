#include "../../include/DesktopEnvironment/DesktopEnvironmentWlroots.hpp"
#include "../../include/Process.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <json_object.h>
#include <json_tokener.h>

#include <wayland-client.h>
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

namespace gsr {
    // Compositors that have an ipc that can be used to get the process id of the focused window,
    // which wlr-foreign-toplevel-management doesn't provide
    enum class CompositorIpc {
        NONE,
        HYPRLAND,
        NIRI,
        SWAY
    };

    namespace {
        struct ToplevelState {
            DesktopEnvironmentWlroots::Impl *owner = nullptr;
            struct zwlr_foreign_toplevel_handle_v1 *handle = nullptr;
            std::string title;
            std::string app_id;
            std::string pending_title;
            std::string pending_app_id;
            bool pending_activated = false;
            bool activated = false;
            bool closed = false;
        };
    }

    struct DesktopEnvironmentWlroots::Impl {
        struct wl_display *display = nullptr;
        struct wl_registry *registry = nullptr;
        struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager = nullptr;
        std::vector<std::unique_ptr<ToplevelState>> toplevels;
        std::string focused_window_title;
        CompositorIpc compositor_ipc = CompositorIpc::NONE;

        ~Impl() { teardown(); }
        void teardown();
        void recompute_focused_title();
    };

    namespace {
        bool app_id_is_ignored(const std::string &app_id) {
            return app_id == "gsr-ui" || app_id == "gsr-notify";
        }

        void toplevel_handle_title(void *data, struct zwlr_foreign_toplevel_handle_v1*, const char *title) {
            ToplevelState *top = (ToplevelState*)data;
            top->pending_title = title ? title : "";
        }

        void toplevel_handle_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1*, const char *app_id) {
            ToplevelState *top = (ToplevelState*)data;
            top->pending_app_id = app_id ? app_id : "";
        }

        void toplevel_handle_output_enter(void*, struct zwlr_foreign_toplevel_handle_v1*, struct wl_output*) {}
        void toplevel_handle_output_leave(void*, struct zwlr_foreign_toplevel_handle_v1*, struct wl_output*) {}

        void toplevel_handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1*, struct wl_array *states) {
            ToplevelState *top = (ToplevelState*)data;
            top->pending_activated = false;
            uint32_t *p = (uint32_t*)states->data;
            const size_t n = states->size / sizeof(uint32_t);
            for(size_t i = 0; i < n; ++i) {
                if(p[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
                    top->pending_activated = true;
                    break;
                }
            }
        }

        void toplevel_handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1*);

        void toplevel_handle_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
            ToplevelState *top = (ToplevelState*)data;
            top->closed = true;
            top->activated = false;
            zwlr_foreign_toplevel_handle_v1_destroy(handle);
            top->handle = nullptr;
        }

        void toplevel_handle_parent(void*, struct zwlr_foreign_toplevel_handle_v1*, struct zwlr_foreign_toplevel_handle_v1*) {}

        const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_handle_listener = {
            toplevel_handle_title, toplevel_handle_app_id,
            toplevel_handle_output_enter, toplevel_handle_output_leave,
            toplevel_handle_state, toplevel_handle_done,
            toplevel_handle_closed, toplevel_handle_parent,
        };

        void toplevel_handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1*) {
            ToplevelState *top = (ToplevelState*)data;
            top->title = top->pending_title;
            top->app_id = top->pending_app_id;
            top->activated = top->pending_activated;
            if(top->owner)
                top->owner->recompute_focused_title();
        }

        void toplevel_manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1*,
            struct zwlr_foreign_toplevel_handle_v1 *handle)
        {
            DesktopEnvironmentWlroots::Impl *impl = (DesktopEnvironmentWlroots::Impl*)data;
            auto top = std::make_unique<ToplevelState>();
            top->owner = impl;
            top->handle = handle;

            zwlr_foreign_toplevel_handle_v1_add_listener(handle, &toplevel_handle_listener, top.get());
            impl->toplevels.push_back(std::move(top));
        }

        void toplevel_manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1*) {
            DesktopEnvironmentWlroots::Impl *impl = (DesktopEnvironmentWlroots::Impl*)data;
            impl->toplevel_manager = nullptr;
        }

        const struct zwlr_foreign_toplevel_manager_v1_listener toplevel_manager_listener = {
            toplevel_manager_toplevel, toplevel_manager_finished,
        };

        void registry_global(void *data, struct wl_registry *registry, uint32_t name,
            const char *interface, uint32_t version)
        {
            DesktopEnvironmentWlroots::Impl *impl = (DesktopEnvironmentWlroots::Impl*)data;
            if(strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
                impl->toplevel_manager = (struct zwlr_foreign_toplevel_manager_v1*)wl_registry_bind(
                    registry, name, &zwlr_foreign_toplevel_manager_v1_interface, version >= 3 ? 3 : version);
                zwlr_foreign_toplevel_manager_v1_add_listener(impl->toplevel_manager, &toplevel_manager_listener, impl);
            }
        }

        void registry_global_remove(void*, struct wl_registry*, uint32_t) {}

        const struct wl_registry_listener registry_listener = {
            registry_global, registry_global_remove,
        };
    }

    void DesktopEnvironmentWlroots::Impl::recompute_focused_title() {
        if(toplevels.empty()) {
            focused_window_title.clear();
            return;
        }

        for(const auto &t : toplevels) {
            if(t->closed || !t->activated)
                continue;
            if(app_id_is_ignored(t->app_id))
                continue;
            focused_window_title = t->title;
            return;
        }
    }

    void DesktopEnvironmentWlroots::Impl::teardown() {
        for(auto &t : toplevels) {
            if(t->handle) {
                zwlr_foreign_toplevel_handle_v1_destroy(t->handle);
                t->handle = nullptr;
            }
        }
        toplevels.clear();

        if(toplevel_manager) {
            zwlr_foreign_toplevel_manager_v1_stop(toplevel_manager);
            zwlr_foreign_toplevel_manager_v1_destroy(toplevel_manager);
            toplevel_manager = nullptr;
        }

        if(registry) {
            wl_registry_destroy(registry);
            registry = nullptr;
        }

        display = nullptr;
        focused_window_title.clear();
    }

    namespace {
        struct ToplevelManagerProbe { bool found = false; };

        void toplevel_manager_probe_global(void *data, struct wl_registry*, uint32_t,
            const char *interface, uint32_t)
        {
            ToplevelManagerProbe *p = (ToplevelManagerProbe*)data;
            if(strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0)
                p->found = true;
        }

        void toplevel_manager_probe_global_remove(void*, struct wl_registry*, uint32_t) {}

        const struct wl_registry_listener toplevel_manager_probe_registry_listener = {
            toplevel_manager_probe_global, toplevel_manager_probe_global_remove,
        };
    }

    static CompositorIpc get_compositor_ipc() {
        const char *xdg_current_desktop = getenv("XDG_CURRENT_DESKTOP");
        if(!xdg_current_desktop)
            return CompositorIpc::NONE;
        else if(strstr(xdg_current_desktop, "Hyprland"))
            return CompositorIpc::HYPRLAND;
        else if(strstr(xdg_current_desktop, "niri"))
            return CompositorIpc::NIRI;
        else if(strstr(xdg_current_desktop, "sway"))
            return CompositorIpc::SWAY;
        else
            return CompositorIpc::NONE;
    }

    static json_object* sway_find_focused_node(json_object *node) {
        json_object *focused = nullptr;
        if(json_object_object_get_ex(node, "focused", &focused) && json_object_is_type(focused, json_type_boolean) && json_object_get_boolean(focused))
            return node;

        for(const char *children_key : { "nodes", "floating_nodes" }) {
            json_object *children = nullptr;
            if(!json_object_object_get_ex(node, children_key, &children) || !json_object_is_type(children, json_type_array))
                continue;

            const size_t num_children = json_object_array_length(children);
            for(size_t i = 0; i < num_children; ++i) {
                json_object *focused_node = sway_find_focused_node(json_object_array_get_idx(children, i));
                if(focused_node)
                    return focused_node;
            }
        }

        return nullptr;
    }

    // Returns false if the compositor ipc failed. |focused_window| is left empty if there is no focused window
    static bool get_focused_window_from_compositor_ipc(CompositorIpc compositor_ipc, FocusedWindow &focused_window) {
        const char *hyprland_args[] = { "hyprctl", "-j", "activewindow", nullptr };
        const char *niri_args[] = { "niri", "msg", "--json", "focused-window", nullptr };
        const char *sway_args[] = { "swaymsg", "-r", "-t", "get_tree", nullptr };

        const char **args = nullptr;
        const char *app_id_key = "app_id";
        const char *title_key = "title";
        switch(compositor_ipc) {
            case CompositorIpc::NONE:
                return false;
            case CompositorIpc::HYPRLAND:
                args = hyprland_args;
                app_id_key = "class";
                break;
            case CompositorIpc::NIRI:
                args = niri_args;
                break;
            case CompositorIpc::SWAY:
                args = sway_args;
                title_key = "name";
                break;
        }

        std::string output;
        if(exec_program_on_host_get_stdout(args, output, false) != 0)
            return false;

        json_object *root = json_tokener_parse(output.c_str());
        if(!root)
            return false;

        // Hyprland outputs an empty object and niri outputs null if there is no focused window.
        // For sway the focused node can also be a workspace (when it's empty), which doesn't have a pid
        json_object *window = compositor_ipc == CompositorIpc::SWAY ? sway_find_focused_node(root) : root;
        json_object *app_id = nullptr;
        json_object *title = nullptr;
        json_object *pid = nullptr;
        if(json_object_is_type(window, json_type_object)) {
            json_object_object_get_ex(window, app_id_key, &app_id);
            json_object_object_get_ex(window, title_key, &title);
            json_object_object_get_ex(window, "pid", &pid);
        }

        const std::string app_id_str = json_object_is_type(app_id, json_type_string) ? json_object_get_string(app_id) : "";
        if(!app_id_is_ignored(app_id_str) && json_object_is_type(pid, json_type_int) && json_object_get_int(pid) > 0) {
            if(json_object_is_type(title, json_type_string))
                focused_window.title = json_object_get_string(title);
            focused_window.pid = json_object_get_int(pid);
        }

        json_object_put(root);
        return true;
    }

    bool DesktopEnvironmentWlroots::is_supported(struct wl_display *dpy) {
        if(!dpy)
            return false;

        ToplevelManagerProbe probe;
        struct wl_registry *registry = wl_display_get_registry(dpy);
        if(!registry)
            return false;

        wl_registry_add_listener(registry, &toplevel_manager_probe_registry_listener, &probe);
        wl_display_roundtrip(dpy);
        wl_registry_destroy(registry);
        return probe.found;
    }

    DesktopEnvironmentWlroots::DesktopEnvironmentWlroots(struct wl_display *dpy) : impl(std::make_unique<Impl>()) {
        impl->display = dpy;
        impl->compositor_ipc = get_compositor_ipc();
    }

    DesktopEnvironmentWlroots::~DesktopEnvironmentWlroots() = default;

    bool DesktopEnvironmentWlroots::start() {
        if(!impl->display) {
            fprintf(stderr, "Error: DesktopEnvironmentWlroots: no wayland display\n");
            return false;
        }
        if(impl->registry) {
            fprintf(stderr, "Error: DesktopEnvironmentWlroots: already started\n");
            return false;
        }

        impl->registry = wl_display_get_registry(impl->display);
        wl_registry_add_listener(impl->registry, &registry_listener, impl.get());
        wl_display_roundtrip(impl->display);

        if(!impl->toplevel_manager) {
            fprintf(stderr, "Error: DesktopEnvironmentWlroots: wlr-foreign-toplevel-management-v1 not available\n");
            return false;
        }

        wl_display_roundtrip(impl->display);
        impl->recompute_focused_title();
        return true;
    }

    void DesktopEnvironmentWlroots::update() {
    }

    FocusedWindow DesktopEnvironmentWlroots::get_focused_window() {
        // The title and process id are both taken from the compositor ipc (if available) so that they are from the same window
        FocusedWindow focused_window;
        if(get_focused_window_from_compositor_ipc(impl->compositor_ipc, focused_window))
            return focused_window;

        // wlr-foreign-toplevel-management doesn't provide the process id of the window
        return FocusedWindow{ impl->focused_window_title, -1 };
    }
}
