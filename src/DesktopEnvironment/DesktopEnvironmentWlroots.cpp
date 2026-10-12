#include "../../include/DesktopEnvironment/DesktopEnvironmentWlroots.hpp"
#include "../../include/Process.hpp"
#include "../../include/WindowUtils.hpp"
#include "../../external/picojson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <limits.h>

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
        std::string focused_app_id;
        CompositorIpc compositor_ipc = CompositorIpc::NONE;

        Display *x11_dpy = nullptr;

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
            focused_app_id.clear();
            return;
        }

        for(const auto &t : toplevels) {
            if(t->closed || !t->activated)
                continue;

            if(app_id_is_ignored(t->app_id))
                continue;

            focused_window_title = t->title;
            focused_app_id = t->app_id;
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
        focused_app_id.clear();
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

    static bool parse_json(const std::string &input, picojson::value &result, const char *command_name) {
        const std::string parse_err = picojson::parse(result, input);
        if(!parse_err.empty()) {
            fprintf(stderr, "error: gsr-ui: failed to parse %s output, error: %s\n", command_name, parse_err.c_str());
            return false;
        }
        return true;
    }

    static const picojson::value* json_object_get(const picojson::object &obj, const char *key) {
        auto it = obj.find(key);
        return it == obj.end() ? nullptr : &it->second;
    }

    // Returns nullptr if the window doesn't have an app id/class
    static const std::string* get_window_app_id(const picojson::object &window_json) {
        const picojson::value *app_id_json = json_object_get(window_json, "app_id");
        if(app_id_json && app_id_json->is<std::string>())
            return &app_id_json->get<std::string>();

        const picojson::value *class_json = json_object_get(window_json, "class");
        if(class_json && class_json->is<std::string>())
            return &class_json->get<std::string>();

        // Sway sets app_id to null for xwayland windows, the class is in window_properties instead
        const picojson::value *window_properties_json = json_object_get(window_json, "window_properties");
        if(!window_properties_json || !window_properties_json->is<picojson::object>())
            return nullptr;

        const picojson::value *window_properties_class_json = json_object_get(window_properties_json->get<picojson::object>(), "class");
        if(window_properties_class_json && window_properties_class_json->is<std::string>())
            return &window_properties_class_json->get<std::string>();

        return nullptr;
    }

    // Returns -1 on failure
    static pid_t get_pid_if_app_id_matches(const picojson::object &window_json, const char *class_name) {
        const std::string *app_id = get_window_app_id(window_json);
        const picojson::value *pid_json = json_object_get(window_json, "pid");
        if(app_id && pid_json && pid_json->is<double>() && *app_id == class_name)
            return (pid_t)pid_json->get<double>();
        return -1;
    }

    // Returns -1 on failure. Finds the first match, since there may be multiple matches.
    // It's ok if we dont get the exact pid, since we are only interested in if it's a game process or not (by type).
    static pid_t hyprctl_get_pid_by_class(const char *class_name) {
        const char *command_args[] = { "hyprctl", "-j", "clients", nullptr };
        std::string output;
        if(exec_program_on_host_get_stdout(command_args, output, false) != 0)
            return -1;

        picojson::value root_json;
        if(!parse_json(output, root_json, "hyprctl -j clients"))
            return -1;

        if(!root_json.is<picojson::array>())
            return -1;

        for(picojson::value &client_val : root_json.get<picojson::array>()) {
            if(!client_val.is<picojson::object>())
                continue;

            const pid_t pid = get_pid_if_app_id_matches(client_val.get<picojson::object>(), class_name);
            if(pid > 0)
                return pid;
        }

        return -1;
    }

    // Returns -1 on failure
    static pid_t sway_get_pid_by_class_in_node(const char *class_name, const picojson::value &node_val) {
        if(!node_val.is<picojson::object>())
            return -1;

        const picojson::object &node_json = node_val.get<picojson::object>();
        const pid_t node_pid = get_pid_if_app_id_matches(node_json, class_name);
        if(node_pid > 0)
            return node_pid;

        for(const char *children_key : { "nodes", "floating_nodes" }) {
            const picojson::value *children = json_object_get(node_json, children_key);
            if(!children || !children->is<picojson::array>())
                continue;

            for(const picojson::value &child_val : children->get<picojson::array>()) {
                const pid_t pid = sway_get_pid_by_class_in_node(class_name, child_val);
                if(pid > 0)
                    return pid;
            }
        }

        return -1;
    }

    // Returns -1 on failure. Finds the first match, since there may be multiple matches.
    // It's ok if we dont get the exact pid, since we are only interested in if it's a game process or not (by type).
    static pid_t sway_get_pid_by_class(const char *class_name) {
        const char *command_args[] = { "swaymsg", "-r", "-t", "get_tree", nullptr };
        std::string output;
        if(exec_program_on_host_get_stdout(command_args, output, false) != 0)
            return -1;

        picojson::value root_json;
        if(!parse_json(output, root_json, "swaymsg -r -t get_tree"))
            return -1;

        return sway_get_pid_by_class_in_node(class_name, root_json);
    }

    // Returns -1 on failure. Finds the first match, since there may be multiple matches.
    // It's ok if we dont get the exact pid, since we are only interested in if it's a game process or not (by type).
    static pid_t niri_get_pid_by_class(const char *class_name) {
        const char *command_args[] = { "niri", "msg", "-j", "windows", nullptr };
        std::string output;
        if(exec_program_on_host_get_stdout(command_args, output, false) != 0)
            return -1;

        picojson::value root_json;
        if(!parse_json(output, root_json, "niri msg -j windows"))
            return -1;

        if(!root_json.is<picojson::array>())
            return -1;

        for(const picojson::value &window_val : root_json.get<picojson::array>()) {
            if(!window_val.is<picojson::object>())
                continue;

            const pid_t pid = get_pid_if_app_id_matches(window_val.get<picojson::object>(), class_name);
            if(pid > 0)
                return pid;
        }

        return -1;
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

    DesktopEnvironmentWlroots::DesktopEnvironmentWlroots(struct wl_display *dpy, Display *x11_dpy) : impl(std::make_unique<Impl>()) {
        impl->display = dpy;
        impl->x11_dpy = x11_dpy;
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
        pid_t pid = -1;
        if(!impl->focused_window_title.empty()) {
            switch(impl->compositor_ipc) {
                case CompositorIpc::NONE: {
                    fprintf(stderr, "gsr ui: warning: DesktopEnvironmentWlroots::get_focused_window: unknown compositor or compositor doesn't support getting the focused window process id. Trying to get the focused window with x11 instead\n");

                    if(!impl->x11_dpy)
                        break;

                    FocusedWindow focused_window;
                    Window window = None;
                    focused_window.title = get_window_name_at_cursor_position(impl->x11_dpy, "gsr-ui", &window);
                    if(focused_window.title.empty()) {
                        window = None;
                        focused_window.title = get_focused_window_name(impl->x11_dpy, WindowCaptureType::FOCUSED, false, "gsr-ui", &window);
                    }

                    if(window != None)
                        focused_window.pid = get_window_pid(impl->x11_dpy, window);

                    return focused_window;
                }
                case CompositorIpc::HYPRLAND: {
                    pid = hyprctl_get_pid_by_class(impl->focused_app_id.c_str());
                    break;
                }
                case CompositorIpc::SWAY: {
                    pid = sway_get_pid_by_class(impl->focused_app_id.c_str());
                    break;
                }
                case CompositorIpc::NIRI: {
                    // Niri runs x11 applications through xwayland-satellite, so niri reports the pid of xwayland-satellite for them.
                    // The real pid is only available from the x11 window, where xwayland-satellite uses WM_CLASS as the app id
                    if(impl->x11_dpy) {
                        const Window window = get_top_level_window_by_class(impl->x11_dpy, impl->focused_app_id);
                        if(window != None)
                            pid = get_window_pid(impl->x11_dpy, window);
                    }

                    if(pid <= 0)
                        pid = niri_get_pid_by_class(impl->focused_app_id.c_str());
                    break;
                }
            }
        }
        return FocusedWindow{ impl->focused_window_title, pid };
    }
}
