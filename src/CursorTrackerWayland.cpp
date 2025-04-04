#include "../include/CursorTrackerWayland.hpp"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <wayland-client.h>
#include "xdg-output-unstable-v1-client-protocol.h"

namespace gsr {
    static const int MAX_CONNECTORS = 32;
    static const int CONNECTOR_TYPE_COUNTS = 32;
    static const uint32_t plane_property_all = 0xF;

    typedef struct {
        int type;
        int count;
    } drm_connector_type_count;

    typedef enum {
        PLANE_PROPERTY_CRTC_X      = 1 << 0,
        PLANE_PROPERTY_CRTC_Y      = 1 << 1,
        PLANE_PROPERTY_CRTC_ID     = 1 << 2,
        PLANE_PROPERTY_TYPE_CURSOR = 1 << 3,
    } plane_property_mask;

    typedef struct {
        uint64_t crtc_id;
        mgl::vec2i size;
    } drm_connector;

    typedef struct {
        drm_connector connectors[MAX_CONNECTORS];
        int num_connectors;
    } drm_connectors;

    /* Returns plane_property_mask */
    static uint32_t plane_get_properties(int drm_fd, uint32_t plane_id, int *crtc_x, int *crtc_y, int *crtc_id, bool *is_cursor) {
        *crtc_x = 0;
        *crtc_y = 0;
        *crtc_id = 0;
        *is_cursor = false;

        uint32_t property_mask = 0;

        drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);
        if(!props)
            return property_mask;

        for(uint32_t i = 0; i < props->count_props; ++i) {
            drmModePropertyPtr prop = drmModeGetProperty(drm_fd, props->props[i]);
            if(!prop)
                continue;

            // SRC_* values are fixed 16.16 points
            const uint32_t type = prop->flags & (DRM_MODE_PROP_LEGACY_TYPE | DRM_MODE_PROP_EXTENDED_TYPE);
            if((type & DRM_MODE_PROP_SIGNED_RANGE) && strcmp(prop->name, "CRTC_X") == 0) {
                *crtc_x = (int)props->prop_values[i];
                property_mask |= PLANE_PROPERTY_CRTC_X;
            } else if((type & DRM_MODE_PROP_SIGNED_RANGE) && strcmp(prop->name, "CRTC_Y") == 0) {
                *crtc_y = (int)props->prop_values[i];
                property_mask |= PLANE_PROPERTY_CRTC_Y;
            } else if((type & DRM_MODE_PROP_OBJECT) && strcmp(prop->name, "CRTC_ID") == 0) {
                *crtc_id = (int)props->prop_values[i];
                property_mask |= PLANE_PROPERTY_CRTC_ID;
            } else if((type & DRM_MODE_PROP_ENUM) && strcmp(prop->name, "type") == 0) {
                const uint64_t current_enum_value = props->prop_values[i];
                for(int j = 0; j < prop->count_enums; ++j) {
                    if(prop->enums[j].value == current_enum_value && strcmp(prop->enums[j].name, "Cursor") == 0) {
                        property_mask |= PLANE_PROPERTY_TYPE_CURSOR;
                        break;
                    }
                }
            }

            drmModeFreeProperty(prop);
        }

        drmModeFreeObjectProperties(props);
        return property_mask;
    }

    static bool connector_get_property_by_name(int drm_fd, drmModeConnectorPtr props, const char *name, uint64_t *result) {
        for(int i = 0; i < props->count_props; ++i) {
            drmModePropertyPtr prop = drmModeGetProperty(drm_fd, props->props[i]);
            if(!prop)
                continue;

            if(strcmp(name, prop->name) == 0) {
                *result = props->prop_values[i];
                drmModeFreeProperty(prop);
                return true;
            }
            drmModeFreeProperty(prop);
        }
        return false;
    }

    static drm_connector_type_count* drm_connector_types_get_index(drm_connector_type_count *type_counts, int *num_type_counts, int connector_type) {
        for(int i = 0; i < *num_type_counts; ++i) {
            if(type_counts[i].type == connector_type)
                return &type_counts[i];
        }

        if(*num_type_counts == CONNECTOR_TYPE_COUNTS)
            return NULL;

        const int index = *num_type_counts;
        type_counts[index].type = connector_type;
        type_counts[index].count = 0;
        ++*num_type_counts;
        return &type_counts[index];
    }

    // Note: this monitor name logic is kept in sync with gpu screen recorder
    static std::string get_monitor_name_from_crtc_id(int drm_fd, uint32_t crtc_id) {
        std::string result;
        drmModeResPtr resources = drmModeGetResources(drm_fd);
        if(!resources)
            return result;

        drm_connector_type_count type_counts[CONNECTOR_TYPE_COUNTS];
        int num_type_counts = 0;

        for(int i = 0; i < resources->count_connectors; ++i) {
            uint64_t connector_crtc_id = 0;
            drmModeConnectorPtr connector = drmModeGetConnectorCurrent(drm_fd, resources->connectors[i]);
            if(!connector)
                continue;

            drm_connector_type_count *connector_type = drm_connector_types_get_index(type_counts, &num_type_counts, connector->connector_type);
            const char *connection_name = drmModeGetConnectorTypeName(connector->connector_type);
            if(connector_type)
                ++connector_type->count;

            if(connector->connection != DRM_MODE_CONNECTED)
                goto next;

            if(connector_type && connector_get_property_by_name(drm_fd, connector, "CRTC_ID", &connector_crtc_id) && connector_crtc_id == crtc_id) {
                result = connection_name;
                result += "-";
                result += std::to_string(connector_type->count);
                drmModeFreeConnector(connector);
                break;
            }

            next:
            drmModeFreeConnector(connector);
        }

        drmModeFreeResources(resources);
        return result;
    }

    // Name is the crtc name. TODO: verify if this works on all wayland compositors
    static const WaylandOutput* get_wayland_monitor_by_name(const std::vector<WaylandOutput> &monitors, const std::string &name) {
        for(const WaylandOutput &monitor : monitors) {
            if(monitor.name == name)
                return &monitor;
        }
        return nullptr;
    }

    static WaylandOutput* get_wayland_monitor_by_output(CursorTrackerWayland &cursor_tracker_wayland, struct wl_output *output) {
        for(WaylandOutput &monitor : cursor_tracker_wayland.monitors) {
            if(monitor.output == output)
                return &monitor;
        }
        return nullptr;
    }

    static void output_handle_geometry(void *data, struct wl_output *wl_output,
        int32_t x, int32_t y, int32_t phys_width, int32_t phys_height,
        int32_t subpixel, const char *make, const char *model,
        int32_t transform) {
        (void)wl_output;
        (void)phys_width;
        (void)phys_height;
        (void)subpixel;
        (void)make;
        (void)model;
        CursorTrackerWayland *cursor_tracker_wayland = (CursorTrackerWayland*)data;
        WaylandOutput *monitor = get_wayland_monitor_by_output(*cursor_tracker_wayland, wl_output);
        if(!monitor)
            return;

        monitor->pos.x = x;
        monitor->pos.y = y;
        monitor->transform = transform;
    }

    static void output_handle_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
        (void)wl_output;
        (void)flags;
        (void)refresh;
        CursorTrackerWayland *cursor_tracker_wayland = (CursorTrackerWayland*)data;
        WaylandOutput *monitor = get_wayland_monitor_by_output(*cursor_tracker_wayland, wl_output);
        if(!monitor)
            return;

        monitor->size.x = width;
        monitor->size.y = height;
    }

    static void output_handle_done(void *data, struct wl_output *wl_output) {
        (void)data;
        (void)wl_output;
    }

    static void output_handle_scale(void* data, struct wl_output *wl_output, int32_t factor) {
        (void)data;
        (void)wl_output;
        (void)factor;
    }

    static void output_handle_name(void *data, struct wl_output *wl_output, const char *name) {
        (void)wl_output;
        CursorTrackerWayland *cursor_tracker_wayland = (CursorTrackerWayland*)data;
        WaylandOutput *monitor = get_wayland_monitor_by_output(*cursor_tracker_wayland, wl_output);
        if(!monitor)
            return;

        monitor->name = name;
    }

    static void output_handle_description(void *data, struct wl_output *wl_output, const char *description) {
        (void)data;
        (void)wl_output;
        (void)description;
    }

    static const struct wl_output_listener output_listener = {
        output_handle_geometry,
        output_handle_mode,
        output_handle_done,
        output_handle_scale,
        output_handle_name,
        output_handle_description,
    };

    static void registry_add_object(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
        (void)version;
        CursorTrackerWayland *cursor_tracker_wayland = (CursorTrackerWayland*)data;
        if(strcmp(interface, wl_output_interface.name) == 0) {
            if(version < 4) {
                fprintf(stderr, "Warning: wl output interface version is < 4, expected >= 4\n");
                return;
            }

            struct wl_output *output = (struct wl_output*)wl_registry_bind(registry, name, &wl_output_interface, 4);
            cursor_tracker_wayland->monitors.push_back(
                WaylandOutput{
                    name,
                    output,
                    nullptr,
                    mgl::vec2i{0, 0},
                    mgl::vec2i{0, 0},
                    0,
                    ""
                });
            wl_output_add_listener(output, &output_listener, cursor_tracker_wayland);
        } else if(strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
            if(version < 1) {
                fprintf(stderr, "Warning: xdg output interface version is < 1, expected >= 1\n");
                return;
            }

            if(cursor_tracker_wayland->xdg_output_manager) {
                zxdg_output_manager_v1_destroy(cursor_tracker_wayland->xdg_output_manager);
                cursor_tracker_wayland->xdg_output_manager = NULL;
            }
            cursor_tracker_wayland->xdg_output_manager = (struct zxdg_output_manager_v1*)wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 1);
        }
    }

    static void registry_remove_object(void *data, struct wl_registry *registry, uint32_t name) {
        (void)data;
        (void)registry;
        (void)name;
        // TODO: Remove output
    }

    static struct wl_registry_listener registry_listener = {
        registry_add_object,
        registry_remove_object,
    };

    static void xdg_output_logical_position(void *data, struct zxdg_output_v1 *zxdg_output_v1, int32_t x, int32_t y) {
        (void)zxdg_output_v1;
        WaylandOutput *monitor = (WaylandOutput*)data;
        monitor->pos.x = x;
        monitor->pos.y = y;
    }

    static void xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output, int32_t width, int32_t height) {
        (void)data;
        (void)xdg_output;
        (void)width;
        (void)height;
    }

    static void xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output) {
        (void)data;
        (void)xdg_output;
    }

    static void xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name) {
        (void)data;
        (void)xdg_output;
        (void)name;
    }

    static void xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output, const char *description) {
        (void)data;
        (void)xdg_output;
        (void)description;
    }

    static const struct zxdg_output_v1_listener xdg_output_listener = {
        xdg_output_logical_position,
        xdg_output_handle_logical_size,
        xdg_output_handle_done,
        xdg_output_handle_name,
        xdg_output_handle_description,
    };

    /* Returns nullptr if not found */
    static const drm_connector* get_drm_connector_by_crtc_id(const drm_connectors *connectors, uint32_t crtc_id) {
        for(int i = 0; i < connectors->num_connectors; ++i) {
            if(connectors->connectors[i].crtc_id == crtc_id)
                return &connectors->connectors[i];
        }
        return nullptr;
    }

    static void get_drm_connectors(int drm_fd, drm_connectors *drm_connectors) {
        drm_connectors->num_connectors = 0;
        drmModeResPtr resources = drmModeGetResources(drm_fd);
        if(!resources)
            return;

        for(int i = 0; i < resources->count_connectors && drm_connectors->num_connectors < MAX_CONNECTORS; ++i) {
            drmModeConnectorPtr connector = nullptr;
            drmModeCrtcPtr crtc = nullptr;

            connector = drmModeGetConnectorCurrent(drm_fd, resources->connectors[i]);
            if(!connector)
                continue;

            uint64_t crtc_id = 0;
            connector_get_property_by_name(drm_fd, connector, "CRTC_ID", &crtc_id);
            if(crtc_id == 0)
                goto next;

            crtc = drmModeGetCrtc(drm_fd, crtc_id);
            if(!crtc)
                goto next;

            drm_connectors->connectors[drm_connectors->num_connectors].crtc_id = crtc_id;
            drm_connectors->connectors[drm_connectors->num_connectors].size = mgl::vec2i{(int)crtc->width, (int)crtc->height};
            ++drm_connectors->num_connectors;

            next:
            if(crtc)
                drmModeFreeCrtc(crtc);

            if(connector)
                drmModeFreeConnector(connector);
        }
        drmModeFreeResources(resources);
    }

    CursorTrackerWayland::CursorTrackerWayland(const char *card_path) {
        drm_fd = open(card_path, O_RDONLY);
        if(drm_fd <= 0) {
            fprintf(stderr, "Error: CursorTrackerWayland: failed to open %s\n", card_path);
            return;
        }

        drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
        drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);
    }

    CursorTrackerWayland::~CursorTrackerWayland() {
        if(drm_fd > 0)
            close(drm_fd);
    }

    void CursorTrackerWayland::update() {
        if(drm_fd <= 0)
            return;

        drm_connectors connectors;
        connectors.num_connectors = 0;
        get_drm_connectors(drm_fd, &connectors);

        drmModePlaneResPtr planes = drmModeGetPlaneResources(drm_fd);
        if(!planes)
            return;

        for(uint32_t i = 0; i < planes->count_planes; ++i) {
            drmModePlanePtr plane = nullptr;
            const drm_connector *connector = nullptr;
            int crtc_x = 0;
            int crtc_y = 0;
            int crtc_id = 0;
            bool is_cursor = false;
            uint32_t property_mask = 0;

            plane = drmModeGetPlane(drm_fd, planes->planes[i]);
            if(!plane)
                goto next;

            if(!plane->fb_id)
                goto next;

            property_mask = plane_get_properties(drm_fd, planes->planes[i], &crtc_x, &crtc_y, &crtc_id, &is_cursor);
            if(property_mask != plane_property_all || crtc_id <= 0)
                goto next;

            connector = get_drm_connector_by_crtc_id(&connectors, crtc_id);
            if(!connector)
                goto next;

            if(crtc_x >= 0 && crtc_x <= connector->size.x && crtc_y >= 0 && crtc_y <= connector->size.y) {
                latest_cursor_position.x = crtc_x;
                latest_cursor_position.y = crtc_y;
                latest_crtc_id = crtc_id;
                drmModeFreePlane(plane);
                break;
            }

            next:
            drmModeFreePlane(plane);
        }

        drmModeFreePlaneResources(planes);
    }

    void CursorTrackerWayland::set_monitor_outputs_from_xdg_output(struct wl_display *dpy) {
        if(!xdg_output_manager) {
            fprintf(stderr, "Warning: CursorTrackerWayland::set_monitor_outputs_from_xdg_output: zxdg_output_manager not found. Registered monitor positions might be incorrect\n");
            return;
        }

        for(WaylandOutput &monitor : monitors) {
            monitor.xdg_output = zxdg_output_manager_v1_get_xdg_output(xdg_output_manager, monitor.output);
            zxdg_output_v1_add_listener(monitor.xdg_output, &xdg_output_listener, &monitor);
        }

        // Fetch xdg_output
        wl_display_roundtrip(dpy);
    }

    std::optional<CursorInfo> CursorTrackerWayland::get_latest_cursor_info() {
        if(drm_fd <= 0 || latest_crtc_id == -1)
            return std::nullopt;

        std::string monitor_name = get_monitor_name_from_crtc_id(drm_fd, latest_crtc_id);
        if(monitor_name.empty())
            return std::nullopt;

        struct wl_display *dpy = wl_display_connect(nullptr);
        if(!dpy) {
            fprintf(stderr, "Error: CursorTrackerWayland::get_latest_cursor_info: failed to connect to the wayland server\n");
            return std::nullopt;
        }

        monitors.clear();
        struct wl_registry *registry = wl_display_get_registry(dpy);
        wl_registry_add_listener(registry, &registry_listener, this);

        // Fetch globals
        wl_display_roundtrip(dpy);

        // Fetch wl_output
        wl_display_roundtrip(dpy);

        set_monitor_outputs_from_xdg_output(dpy);

        mgl::vec2i cursor_position = latest_cursor_position;
        const WaylandOutput *wayland_monitor = get_wayland_monitor_by_name(monitors, monitor_name);
        if(!wayland_monitor)
            return std::nullopt;

        cursor_position = wayland_monitor->pos + latest_cursor_position;
        for(WaylandOutput &monitor : monitors) {
            if(monitor.output) {
                wl_output_destroy(monitor.output);
                monitor.output = nullptr;
            }

            if(monitor.xdg_output) {
                zxdg_output_v1_destroy(monitor.xdg_output);
                monitor.xdg_output = nullptr;
            }
        }
        monitors.clear();

        if(xdg_output_manager) {
            zxdg_output_manager_v1_destroy(xdg_output_manager);
            xdg_output_manager = nullptr;
        }

        wl_registry_destroy(registry);
        wl_display_disconnect(dpy);

        return CursorInfo{ cursor_position, std::move(monitor_name) };
    }
}