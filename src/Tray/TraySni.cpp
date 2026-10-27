#include "../../include/Tray/TraySni.hpp"
#include "../../include/Translation.hpp"

#include <stdio.h>
#include <string.h>

namespace gsr {
    static const char *item_path = "/StatusNotifierItem";
    static const char *menu_path = "/MenuBar";
    static const char *watcher_service = "org.kde.StatusNotifierWatcher";
    static const char *watcher_path = "/StatusNotifierWatcher";
    static const char *item_interface = "org.kde.StatusNotifierItem";
    static const char *menu_interface = "com.canonical.dbusmenu";
    static const char *tray_title = "GPU Screen Recorder";

    static const char *item_introspection_xml = R"xml(<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="WindowId" type="i" access="read"/>
    <property name="IconThemePath" type="s" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconPixmap" type="a(iiay)" access="read"/>
    <property name="OverlayIconName" type="s" access="read"/>
    <property name="OverlayIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionIconName" type="s" access="read"/>
    <property name="AttentionIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionMovieName" type="s" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <method name="ContextMenu">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="Activate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="SecondaryActivate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="Scroll">
      <arg name="delta" type="i" direction="in"/>
      <arg name="orientation" type="s" direction="in"/>
    </method>
    <signal name="NewTitle"/>
    <signal name="NewIcon"/>
    <signal name="NewAttentionIcon"/>
    <signal name="NewOverlayIcon"/>
    <signal name="NewToolTip"/>
    <signal name="NewStatus">
      <arg name="status" type="s"/>
    </signal>
  </interface>
  <interface name="org.freedesktop.DBus.Properties">
    <method name="Get">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="property_name" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="GetAll">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="properties" type="a{sv}" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.DBus.Introspectable">
    <method name="Introspect">
      <arg name="xml_data" type="s" direction="out"/>
    </method>
  </interface>
</node>
)xml";

    static const char *menu_introspection_xml = R"xml(<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout">
      <arg name="parentId" type="i" direction="in"/>
      <arg name="recursionDepth" type="i" direction="in"/>
      <arg name="propertyNames" type="as" direction="in"/>
      <arg name="revision" type="u" direction="out"/>
      <arg name="layout" type="(ia{sv}av)" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg name="ids" type="ai" direction="in"/>
      <arg name="propertyNames" type="as" direction="in"/>
      <arg name="properties" type="a(ia{sv})" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg name="id" type="i" direction="in"/>
      <arg name="name" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="Event">
      <arg name="id" type="i" direction="in"/>
      <arg name="eventId" type="s" direction="in"/>
      <arg name="data" type="v" direction="in"/>
      <arg name="timestamp" type="u" direction="in"/>
    </method>
    <method name="EventGroup">
      <arg name="events" type="a(isvu)" direction="in"/>
      <arg name="idErrors" type="ai" direction="out"/>
    </method>
    <method name="AboutToShow">
      <arg name="id" type="i" direction="in"/>
      <arg name="needUpdate" type="b" direction="out"/>
    </method>
    <method name="AboutToShowGroup">
      <arg name="ids" type="ai" direction="in"/>
      <arg name="updatesNeeded" type="ai" direction="out"/>
      <arg name="idErrors" type="ai" direction="out"/>
    </method>
    <signal name="ItemsPropertiesUpdated">
      <arg name="updatedProps" type="a(ia{sv})"/>
      <arg name="removedProps" type="a(ias)"/>
    </signal>
    <signal name="LayoutUpdated">
      <arg name="revision" type="u"/>
      <arg name="parent" type="i"/>
    </signal>
    <signal name="ItemActivationRequested">
      <arg name="id" type="i"/>
      <arg name="timestamp" type="u"/>
    </signal>
  </interface>
  <interface name="org.freedesktop.DBus.Properties">
    <method name="Get">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="property_name" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="GetAll">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="properties" type="a{sv}" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.DBus.Introspectable">
    <method name="Introspect">
      <arg name="xml_data" type="s" direction="out"/>
    </method>
  </interface>
</node>
)xml";

    static const char *item_property_names[] = {
        "Category", "Id", "Title", "Status", "WindowId", "IconThemePath", "IconName", "IconPixmap",
        "OverlayIconName", "OverlayIconPixmap", "AttentionIconName", "AttentionIconPixmap", "AttentionMovieName",
        "ToolTip", "ItemIsMenu", "Menu"
    };

    static const char *menu_property_names[] = { "Version", "TextDirection", "Status", "IconThemePath" };

    static constexpr int32_t menu_root_id = 0;

    struct MenuItemProperty {
        const char *name;
        const char *value;
    };

    using PropertyAppender = std::function<bool(DBusMessageIter *iter, const char *name)>;

    static const TrayMenuItem* find_menu_item(int32_t id) {
        for(const TrayMenuItem &menu_item : tray_menu_items) {
            if(menu_item.id == id)
                return &menu_item;
        }
        return nullptr;
    }

    static MenuItemProperty get_menu_item_property(int32_t id) {
        if(id == menu_root_id)
            return { "children-display", "submenu" };

        const TrayMenuItem *menu_item = find_menu_item(id);
        if(menu_item)
            return { "label", TR(menu_item->label) };

        return { nullptr, nullptr };
    }

    static bool menu_item_exists(int32_t id) {
        return get_menu_item_property(id).name != nullptr;
    }

    static void append_variant(DBusMessageIter *iter, int type, const void *value) {
        const char signature[2] = { (char)type, '\0' };
        DBusMessageIter variant_iter;
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, signature, &variant_iter);
        dbus_message_iter_append_basic(&variant_iter, type, value);
        dbus_message_iter_close_container(iter, &variant_iter);
    }

    static void append_variant_string(DBusMessageIter *iter, const char *str) {
        append_variant(iter, DBUS_TYPE_STRING, &str);
    }

    static void append_variant_object_path(DBusMessageIter *iter, const char *path) {
        append_variant(iter, DBUS_TYPE_OBJECT_PATH, &path);
    }

    static void append_variant_bool(DBusMessageIter *iter, bool value) {
        const dbus_bool_t dbus_value = value;
        append_variant(iter, DBUS_TYPE_BOOLEAN, &dbus_value);
    }

    static void append_variant_int32(DBusMessageIter *iter, int32_t value) {
        append_variant(iter, DBUS_TYPE_INT32, &value);
    }

    static void append_variant_uint32(DBusMessageIter *iter, uint32_t value) {
        append_variant(iter, DBUS_TYPE_UINT32, &value);
    }

    static void append_variant_empty_string_array(DBusMessageIter *iter) {
        DBusMessageIter variant_iter;
        DBusMessageIter array_iter;
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "as", &variant_iter);
        dbus_message_iter_open_container(&variant_iter, DBUS_TYPE_ARRAY, "s", &array_iter);
        dbus_message_iter_close_container(&variant_iter, &array_iter);
        dbus_message_iter_close_container(iter, &variant_iter);
    }

    static void append_int32_array(DBusMessageIter *iter, const std::vector<int32_t> &values) {
        DBusMessageIter array_iter;
        dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "i", &array_iter);
        if(!values.empty()) {
            const int32_t *data = values.data();
            dbus_message_iter_append_fixed_array(&array_iter, DBUS_TYPE_INT32, &data, (int)values.size());
        }
        dbus_message_iter_close_container(iter, &array_iter);
    }

    static void append_pixmaps(DBusMessageIter *iter, const TrayPixmap *pixmap) {
        DBusMessageIter array_iter;
        dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "(iiay)", &array_iter);
        if(pixmap && !pixmap->argb_data.empty()) {
            DBusMessageIter struct_iter;
            dbus_message_iter_open_container(&array_iter, DBUS_TYPE_STRUCT, nullptr, &struct_iter);
            dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_INT32, &pixmap->width);
            dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_INT32, &pixmap->height);

            DBusMessageIter bytes_iter;
            dbus_message_iter_open_container(&struct_iter, DBUS_TYPE_ARRAY, "y", &bytes_iter);
            const unsigned char *data = pixmap->argb_data.data();
            dbus_message_iter_append_fixed_array(&bytes_iter, DBUS_TYPE_BYTE, &data, (int)pixmap->argb_data.size());
            dbus_message_iter_close_container(&struct_iter, &bytes_iter);

            dbus_message_iter_close_container(&array_iter, &struct_iter);
        }
        dbus_message_iter_close_container(iter, &array_iter);
    }

    static void append_variant_pixmaps(DBusMessageIter *iter, const TrayPixmap *pixmap) {
        DBusMessageIter variant_iter;
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "a(iiay)", &variant_iter);
        append_pixmaps(&variant_iter, pixmap);
        dbus_message_iter_close_container(iter, &variant_iter);
    }

    static void append_variant_tooltip(DBusMessageIter *iter, const char *title) {
        const char *empty = "";
        DBusMessageIter variant_iter;
        DBusMessageIter struct_iter;
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "(sa(iiay)ss)", &variant_iter);
        dbus_message_iter_open_container(&variant_iter, DBUS_TYPE_STRUCT, nullptr, &struct_iter);
        dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &empty);
        append_pixmaps(&struct_iter, nullptr);
        dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &title);
        dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &empty);
        dbus_message_iter_close_container(&variant_iter, &struct_iter);
        dbus_message_iter_close_container(iter, &variant_iter);
    }

    static void append_menu_item_properties(DBusMessageIter *iter, int32_t id) {
        const MenuItemProperty property = get_menu_item_property(id);
        DBusMessageIter array_iter;
        DBusMessageIter entry_iter;
        dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &array_iter);
        dbus_message_iter_open_container(&array_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &entry_iter);
        dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &property.name);
        append_variant_string(&entry_iter, property.value);
        dbus_message_iter_close_container(&array_iter, &entry_iter);
        dbus_message_iter_close_container(iter, &array_iter);
    }

    static void append_menu_layout(DBusMessageIter *iter, int32_t id, int32_t recursion_depth) {
        DBusMessageIter struct_iter;
        DBusMessageIter children_iter;
        dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, nullptr, &struct_iter);
        dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_INT32, &id);
        append_menu_item_properties(&struct_iter, id);

        dbus_message_iter_open_container(&struct_iter, DBUS_TYPE_ARRAY, "v", &children_iter);
        if(id == menu_root_id && recursion_depth != 0) {
            for(const TrayMenuItem &menu_item : tray_menu_items) {
                DBusMessageIter variant_iter;
                dbus_message_iter_open_container(&children_iter, DBUS_TYPE_VARIANT, "(ia{sv}av)", &variant_iter);
                append_menu_layout(&variant_iter, menu_item.id, recursion_depth - 1);
                dbus_message_iter_close_container(&children_iter, &variant_iter);
            }
        }
        dbus_message_iter_close_container(&struct_iter, &children_iter);

        dbus_message_iter_close_container(iter, &struct_iter);
    }

    static bool append_menu_property(DBusMessageIter *iter, const char *name) {
        if(strcmp(name, "Version") == 0)
            append_variant_uint32(iter, 3);
        else if(strcmp(name, "TextDirection") == 0)
            append_variant_string(iter, "ltr");
        else if(strcmp(name, "Status") == 0)
            append_variant_string(iter, "normal");
        else if(strcmp(name, "IconThemePath") == 0)
            append_variant_empty_string_array(iter);
        else
            return false;
        return true;
    }

    static DBusMessage* create_invalid_args_error(DBusMessage *message, DBusError *err) {
        DBusMessage *reply = dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS, err->message);
        dbus_error_free(err);
        return reply;
    }

    static DBusMessage* create_introspect_reply(DBusMessage *message, const char *xml) {
        DBusMessage *reply = dbus_message_new_method_return(message);
        if(reply)
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
        return reply;
    }

    template <size_t N>
    static DBusMessage* handle_properties_message(DBusMessage *message, const char *interface_name, const char *(&property_names)[N], const PropertyAppender &append_property) {
        DBusError err;
        dbus_error_init(&err);

        if(dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "Get")) {
            const char *requested_interface = nullptr;
            const char *property_name = nullptr;
            if(!dbus_message_get_args(message, &err, DBUS_TYPE_STRING, &requested_interface, DBUS_TYPE_STRING, &property_name, DBUS_TYPE_INVALID))
                return create_invalid_args_error(message, &err);

            if(requested_interface[0] != '\0' && strcmp(requested_interface, interface_name) != 0)
                return dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_INTERFACE, requested_interface);

            DBusMessage *reply = dbus_message_new_method_return(message);
            if(!reply)
                return nullptr;

            DBusMessageIter iter;
            dbus_message_iter_init_append(reply, &iter);
            if(!append_property(&iter, property_name)) {
                dbus_message_unref(reply);
                return dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_PROPERTY, property_name);
            }
            return reply;
        } else if(dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "GetAll")) {
            const char *requested_interface = nullptr;
            if(!dbus_message_get_args(message, &err, DBUS_TYPE_STRING, &requested_interface, DBUS_TYPE_INVALID))
                return create_invalid_args_error(message, &err);

            DBusMessage *reply = dbus_message_new_method_return(message);
            if(!reply)
                return nullptr;

            DBusMessageIter iter;
            DBusMessageIter array_iter;
            dbus_message_iter_init_append(reply, &iter);
            dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &array_iter);
            if(requested_interface[0] == '\0' || strcmp(requested_interface, interface_name) == 0) {
                for(const char *property_name : property_names) {
                    DBusMessageIter entry_iter;
                    dbus_message_iter_open_container(&array_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &entry_iter);
                    dbus_message_iter_append_basic(&entry_iter, DBUS_TYPE_STRING, &property_name);
                    append_property(&entry_iter, property_name);
                    dbus_message_iter_close_container(&array_iter, &entry_iter);
                }
            }
            dbus_message_iter_close_container(&iter, &array_iter);
            return reply;
        } else if(dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "Set")) {
            return dbus_message_new_error(message, DBUS_ERROR_PROPERTY_READ_ONLY, "Properties are read-only");
        }

        return nullptr;
    }

    static constexpr double watcher_call_timeout_seconds = 5.0;

    static bool is_missing_service_error(DBusMessage *reply) {
        return dbus_message_is_error(reply, DBUS_ERROR_SERVICE_UNKNOWN) || dbus_message_is_error(reply, DBUS_ERROR_NAME_HAS_NO_OWNER);
    }

    static void print_error_reply(const char *description, DBusMessage *reply) {
        const char *error_message = "";
        dbus_message_get_args(reply, nullptr, DBUS_TYPE_STRING, &error_message, DBUS_TYPE_INVALID);
        fprintf(stderr, "Warning: TraySni: %s: %s: %s\n", description, dbus_message_get_error_name(reply), error_message);
    }

    static bool get_variant_bool(DBusMessage *reply, bool *value) {
        DBusMessageIter iter;
        DBusMessageIter variant_iter;
        if(!dbus_message_iter_init(reply, &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT)
            return false;

        dbus_message_iter_recurse(&iter, &variant_iter);
        if(dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_BOOLEAN)
            return false;

        dbus_bool_t dbus_value = false;
        dbus_message_iter_get_basic(&variant_iter, &dbus_value);
        *value = dbus_value;
        return true;
    }

    TraySni::~TraySni() {
        close_connection();
    }

    bool TraySni::init(const TrayPixmap *pixmap) {
        this->pixmap = pixmap;
        menu_language = Translation::instance().get_current_language();

        DBusError err;
        dbus_error_init(&err);

        connection = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
        if(!connection) {
            fprintf(stderr, "Error: TraySni::init: failed to connect to the session bus: %s\n", err.message);
            dbus_error_free(&err);
            return false;
        }
        dbus_connection_set_exit_on_disconnect(connection, false);

        static const DBusObjectPathVTable item_vtable = { nullptr, item_message_callback, nullptr, nullptr, nullptr, nullptr };
        static const DBusObjectPathVTable menu_vtable = { nullptr, menu_message_callback, nullptr, nullptr, nullptr, nullptr };
        if(!dbus_connection_register_object_path(connection, item_path, &item_vtable, this)
            || !dbus_connection_register_object_path(connection, menu_path, &menu_vtable, this)
            || !dbus_connection_add_filter(connection, filter_callback, this, nullptr))
        {
            fprintf(stderr, "Error: TraySni::init: failed to register dbus objects\n");
            close_connection();
            return false;
        }

        add_match("type='signal',sender='" DBUS_SERVICE_DBUS "',interface='" DBUS_INTERFACE_DBUS "',member='NameOwnerChanged',arg0='org.kde.StatusNotifierWatcher'");
        add_match("type='signal',sender='org.kde.StatusNotifierWatcher',interface='org.kde.StatusNotifierWatcher'");

        register_with_watcher();
        return true;
    }

    void TraySni::poll() {
        if(!connection)
            return;

        if(!dbus_connection_read_write(connection, 0)) {
            fprintf(stderr, "Error: TraySni: lost connection to the session bus\n");
            close_connection();
            return;
        }

        while(dbus_connection_dispatch(connection) == DBUS_DISPATCH_DATA_REMAINS) {}

        if(watcher_call && watcher_call_clock.get_elapsed_time_seconds() >= watcher_call_timeout_seconds) {
            fprintf(stderr, "Warning: TraySni: the StatusNotifierWatcher didn't respond in time\n");
            cancel_watcher_call();
        }

        update_menu_language();
        dbus_connection_flush(connection);

        const std::optional<TrayAction> action = pending_action;
        pending_action.reset();
        if(action && on_action)
            on_action(*action);
    }

    void TraySni::set_pixmap(const TrayPixmap *pixmap) {
        this->pixmap = pixmap;
        if(!connection)
            return;

        DBusMessage *signal = dbus_message_new_signal(item_path, item_interface, "NewIcon");
        if(!signal)
            return;

        dbus_connection_send(connection, signal, nullptr);
        dbus_message_unref(signal);
    }

    TraySniState TraySni::get_state() const {
        if(!connection)
            return TraySniState::UNAVAILABLE;
        else if(watcher_call)
            return TraySniState::PENDING;
        else if(item_registered && host_registered)
            return TraySniState::REGISTERED;
        else
            return TraySniState::UNAVAILABLE;
    }

    void TraySni::close_connection() {
        reset_registration();
        if(!connection)
            return;

        dbus_connection_close(connection);
        dbus_connection_unref(connection);
        connection = nullptr;
    }

    void TraySni::add_match(const char *rule) {
        DBusError err;
        dbus_error_init(&err);
        dbus_bus_add_match(connection, rule, &err);
        if(dbus_error_is_set(&err)) {
            fprintf(stderr, "Warning: TraySni: failed to add match rule %s: %s\n", rule, err.message);
            dbus_error_free(&err);
        }
    }

    void TraySni::register_with_watcher() {
        reset_registration();

        DBusMessage *message = dbus_message_new_method_call(watcher_service, watcher_path, watcher_service, "RegisterStatusNotifierItem");
        if(!message)
            return;

        dbus_message_append_args(message, DBUS_TYPE_STRING, &item_path, DBUS_TYPE_INVALID);
        send_watcher_call(message, register_reply_callback);
    }

    void TraySni::query_host_registered() {
        DBusMessage *message = dbus_message_new_method_call(watcher_service, watcher_path, DBUS_INTERFACE_PROPERTIES, "Get");
        if(!message)
            return;

        const char *property_name = "IsStatusNotifierHostRegistered";
        dbus_message_append_args(message, DBUS_TYPE_STRING, &watcher_service, DBUS_TYPE_STRING, &property_name, DBUS_TYPE_INVALID);
        send_watcher_call(message, host_registered_reply_callback);
    }

    // Don't block waiting for the reply, the watcher may call back into this object before it replies
    void TraySni::send_watcher_call(DBusMessage *message, DBusPendingCallNotifyFunction reply_callback) {
        cancel_watcher_call();
        dbus_message_set_auto_start(message, false);

        if(dbus_connection_send_with_reply(connection, message, &watcher_call, DBUS_TIMEOUT_INFINITE) && watcher_call) {
            if(dbus_pending_call_set_notify(watcher_call, reply_callback, this, nullptr))
                watcher_call_clock.restart();
            else
                cancel_watcher_call();
        }
        dbus_message_unref(message);
    }

    DBusMessage* TraySni::take_watcher_reply(DBusPendingCall *pending_call) {
        DBusMessage *reply = dbus_pending_call_steal_reply(pending_call);
        dbus_pending_call_unref(watcher_call);
        watcher_call = nullptr;
        return reply;
    }

    void TraySni::cancel_watcher_call() {
        if(!watcher_call)
            return;

        dbus_pending_call_cancel(watcher_call);
        dbus_pending_call_unref(watcher_call);
        watcher_call = nullptr;
    }

    void TraySni::reset_registration() {
        cancel_watcher_call();
        item_registered = false;
        host_registered = false;
    }

    void TraySni::register_reply_callback(DBusPendingCall *pending_call, void *userdata) {
        TraySni *self = (TraySni*)userdata;
        DBusMessage *reply = self->take_watcher_reply(pending_call);
        if(!reply)
            return;

        if(dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
            if(!is_missing_service_error(reply))
                print_error_reply("failed to register the tray icon", reply);
        } else {
            self->item_registered = true;
            self->query_host_registered();
        }
        dbus_message_unref(reply);
    }

    void TraySni::host_registered_reply_callback(DBusPendingCall *pending_call, void *userdata) {
        TraySni *self = (TraySni*)userdata;
        DBusMessage *reply = self->take_watcher_reply(pending_call);
        if(!reply)
            return;

        bool is_host_registered = false;
        if(dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
            print_error_reply("failed to check if a tray host is registered, assuming it is", reply);
            is_host_registered = true;
        } else if(!get_variant_bool(reply, &is_host_registered)) {
            fprintf(stderr, "Warning: TraySni: IsStatusNotifierHostRegistered is not a boolean, assuming a tray host is registered\n");
            is_host_registered = true;
        }
        self->host_registered = is_host_registered;
        dbus_message_unref(reply);
    }

    void TraySni::handle_name_owner_changed(DBusMessage *message) {
        const char *name = nullptr;
        const char *old_owner = nullptr;
        const char *new_owner = nullptr;
        if(!dbus_message_get_args(message, nullptr, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old_owner, DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID)
            || strcmp(name, watcher_service) != 0)
        {
            return;
        }

        if(new_owner[0] != '\0')
            register_with_watcher();
        else
            reset_registration();
    }

    void TraySni::update_menu_language() {
        const std::string &language = Translation::instance().get_current_language();
        if(language == menu_language)
            return;

        menu_language = language;
        ++menu_revision;

        DBusMessage *signal = dbus_message_new_signal(menu_path, menu_interface, "LayoutUpdated");
        if(!signal)
            return;

        const dbus_int32_t parent_id = menu_root_id;
        dbus_message_append_args(signal, DBUS_TYPE_UINT32, &menu_revision, DBUS_TYPE_INT32, &parent_id, DBUS_TYPE_INVALID);
        dbus_connection_send(connection, signal, nullptr);
        dbus_message_unref(signal);
    }

    DBusHandlerResult TraySni::filter_callback(DBusConnection*, DBusMessage *message, void *userdata) {
        TraySni *self = (TraySni*)userdata;
        if(dbus_message_is_signal(message, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
            self->handle_name_owner_changed(message);
        } else if(dbus_message_is_signal(message, watcher_service, "StatusNotifierHostRegistered")) {
            if(self->item_registered)
                self->host_registered = true;
        } else if(dbus_message_is_signal(message, watcher_service, "StatusNotifierHostUnregistered")) {
            if(self->item_registered)
                self->query_host_registered();
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    DBusHandlerResult TraySni::item_message_callback(DBusConnection*, DBusMessage *message, void *userdata) {
        TraySni *self = (TraySni*)userdata;
        return self->send_reply(message, self->handle_item_message(message));
    }

    DBusHandlerResult TraySni::menu_message_callback(DBusConnection*, DBusMessage *message, void *userdata) {
        TraySni *self = (TraySni*)userdata;
        return self->send_reply(message, self->handle_menu_message(message));
    }

    DBusHandlerResult TraySni::send_reply(DBusMessage *message, DBusMessage *reply) {
        if(!reply)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        if(!dbus_message_get_no_reply(message))
            dbus_connection_send(connection, reply, nullptr);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    bool TraySni::append_item_property(DBusMessageIter *iter, const char *name) {
        if(strcmp(name, "Category") == 0)
            append_variant_string(iter, "ApplicationStatus");
        else if(strcmp(name, "Id") == 0)
            append_variant_string(iter, "gpu-screen-recorder-ui");
        else if(strcmp(name, "Title") == 0)
            append_variant_string(iter, tray_title);
        else if(strcmp(name, "Status") == 0)
            append_variant_string(iter, "Active");
        else if(strcmp(name, "WindowId") == 0)
            append_variant_int32(iter, 0);
        else if(strcmp(name, "IconThemePath") == 0 || strcmp(name, "IconName") == 0 || strcmp(name, "OverlayIconName") == 0
            || strcmp(name, "AttentionIconName") == 0 || strcmp(name, "AttentionMovieName") == 0)
            append_variant_string(iter, "");
        else if(strcmp(name, "IconPixmap") == 0)
            append_variant_pixmaps(iter, pixmap);
        else if(strcmp(name, "OverlayIconPixmap") == 0 || strcmp(name, "AttentionIconPixmap") == 0)
            append_variant_pixmaps(iter, nullptr);
        else if(strcmp(name, "ToolTip") == 0)
            append_variant_tooltip(iter, tray_title);
        else if(strcmp(name, "ItemIsMenu") == 0)
            append_variant_bool(iter, false);
        else if(strcmp(name, "Menu") == 0)
            append_variant_object_path(iter, menu_path);
        else
            return false;
        return true;
    }

    DBusMessage* TraySni::handle_item_message(DBusMessage *message) {
        if(dbus_message_is_method_call(message, DBUS_INTERFACE_INTROSPECTABLE, "Introspect"))
            return create_introspect_reply(message, item_introspection_xml);

        if(dbus_message_has_interface(message, DBUS_INTERFACE_PROPERTIES)) {
            return handle_properties_message(message, item_interface, item_property_names, [this](DBusMessageIter *iter, const char *name) {
                return append_item_property(iter, name);
            });
        }

        if(dbus_message_is_method_call(message, item_interface, "Activate")) {
            pending_action = TrayAction::SHOW_UI;
            return dbus_message_new_method_return(message);
        }

        if(dbus_message_is_method_call(message, item_interface, "SecondaryActivate")
            || dbus_message_is_method_call(message, item_interface, "ContextMenu")
            || dbus_message_is_method_call(message, item_interface, "Scroll"))
        {
            return dbus_message_new_method_return(message);
        }

        return nullptr;
    }

    DBusMessage* TraySni::handle_menu_message(DBusMessage *message) {
        if(dbus_message_is_method_call(message, DBUS_INTERFACE_INTROSPECTABLE, "Introspect"))
            return create_introspect_reply(message, menu_introspection_xml);

        if(dbus_message_has_interface(message, DBUS_INTERFACE_PROPERTIES))
            return handle_properties_message(message, menu_interface, menu_property_names, append_menu_property);

        DBusError err;
        dbus_error_init(&err);

        if(dbus_message_is_method_call(message, menu_interface, "GetLayout")) {
            dbus_int32_t parent_id = 0;
            dbus_int32_t recursion_depth = 0;
            if(!dbus_message_get_args(message, &err, DBUS_TYPE_INT32, &parent_id, DBUS_TYPE_INT32, &recursion_depth, DBUS_TYPE_INVALID))
                return create_invalid_args_error(message, &err);

            if(!menu_item_exists(parent_id))
                return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS, "Unknown menu item");

            DBusMessage *reply = dbus_message_new_method_return(message);
            if(!reply)
                return nullptr;

            DBusMessageIter iter;
            dbus_message_iter_init_append(reply, &iter);
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &menu_revision);
            append_menu_layout(&iter, parent_id, recursion_depth);
            return reply;
        } else if(dbus_message_is_method_call(message, menu_interface, "GetGroupProperties")) {
            dbus_int32_t *ids = nullptr;
            int num_ids = 0;
            if(!dbus_message_get_args(message, &err, DBUS_TYPE_ARRAY, DBUS_TYPE_INT32, &ids, &num_ids, DBUS_TYPE_INVALID))
                return create_invalid_args_error(message, &err);

            std::vector<int32_t> requested_ids(ids, ids + num_ids);
            if(requested_ids.empty()) {
                requested_ids.push_back(menu_root_id);
                for(const TrayMenuItem &menu_item : tray_menu_items) {
                    requested_ids.push_back(menu_item.id);
                }
            }

            DBusMessage *reply = dbus_message_new_method_return(message);
            if(!reply)
                return nullptr;

            DBusMessageIter iter;
            DBusMessageIter array_iter;
            dbus_message_iter_init_append(reply, &iter);
            dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(ia{sv})", &array_iter);
            for(int32_t id : requested_ids) {
                if(!menu_item_exists(id))
                    continue;

                DBusMessageIter struct_iter;
                dbus_message_iter_open_container(&array_iter, DBUS_TYPE_STRUCT, nullptr, &struct_iter);
                dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_INT32, &id);
                append_menu_item_properties(&struct_iter, id);
                dbus_message_iter_close_container(&array_iter, &struct_iter);
            }
            dbus_message_iter_close_container(&iter, &array_iter);
            return reply;
        } else if(dbus_message_is_method_call(message, menu_interface, "GetProperty")) {
            dbus_int32_t id = 0;
            const char *name = nullptr;
            if(!dbus_message_get_args(message, &err, DBUS_TYPE_INT32, &id, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID))
                return create_invalid_args_error(message, &err);

            const MenuItemProperty property = get_menu_item_property(id);
            if(!property.name || strcmp(property.name, name) != 0)
                return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS, "Unknown menu item property");

            DBusMessage *reply = dbus_message_new_method_return(message);
            if(!reply)
                return nullptr;

            DBusMessageIter iter;
            dbus_message_iter_init_append(reply, &iter);
            append_variant_string(&iter, property.value);
            return reply;
        } else if(dbus_message_is_method_call(message, menu_interface, "Event")) {
            dbus_int32_t id = 0;
            const char *event_id = nullptr;
            if(!dbus_message_get_args(message, &err, DBUS_TYPE_INT32, &id, DBUS_TYPE_STRING, &event_id, DBUS_TYPE_INVALID))
                return create_invalid_args_error(message, &err);

            on_menu_event(id, event_id);
            return dbus_message_new_method_return(message);
        } else if(dbus_message_is_method_call(message, menu_interface, "EventGroup")) {
            DBusMessageIter iter;
            if(!dbus_message_iter_init(message, &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
                return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS, "Expected a(isvu)");

            std::vector<int32_t> id_errors;
            DBusMessageIter array_iter;
            dbus_message_iter_recurse(&iter, &array_iter);
            while(dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRUCT) {
                DBusMessageIter struct_iter;
                dbus_message_iter_recurse(&array_iter, &struct_iter);

                dbus_int32_t id = 0;
                const char *event_id = "";
                if(dbus_message_iter_get_arg_type(&struct_iter) == DBUS_TYPE_INT32) {
                    dbus_message_iter_get_basic(&struct_iter, &id);
                    dbus_message_iter_next(&struct_iter);
                }
                if(dbus_message_iter_get_arg_type(&struct_iter) == DBUS_TYPE_STRING)
                    dbus_message_iter_get_basic(&struct_iter, &event_id);

                if(menu_item_exists(id))
                    on_menu_event(id, event_id);
                else
                    id_errors.push_back(id);

                dbus_message_iter_next(&array_iter);
            }

            DBusMessage *reply = dbus_message_new_method_return(message);
            if(!reply)
                return nullptr;

            DBusMessageIter reply_iter;
            dbus_message_iter_init_append(reply, &reply_iter);
            append_int32_array(&reply_iter, id_errors);
            return reply;
        } else if(dbus_message_is_method_call(message, menu_interface, "AboutToShow")) {
            DBusMessage *reply = dbus_message_new_method_return(message);
            if(!reply)
                return nullptr;

            const dbus_bool_t need_update = false;
            dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &need_update, DBUS_TYPE_INVALID);
            return reply;
        } else if(dbus_message_is_method_call(message, menu_interface, "AboutToShowGroup")) {
            DBusMessage *reply = dbus_message_new_method_return(message);
            if(!reply)
                return nullptr;

            DBusMessageIter iter;
            dbus_message_iter_init_append(reply, &iter);
            append_int32_array(&iter, {});
            append_int32_array(&iter, {});
            return reply;
        }

        return nullptr;
    }

    void TraySni::on_menu_event(int32_t id, const char *event_id) {
        if(strcmp(event_id, "clicked") != 0)
            return;

        const TrayMenuItem *menu_item = find_menu_item(id);
        if(menu_item)
            pending_action = menu_item->action;
    }
}
