#pragma once

#include "Tray.hpp"

#include <functional>
#include <optional>
#include <string>
#include <stdint.h>
#include <dbus/dbus.h>
#include <mglpp/system/Clock.hpp>

namespace gsr {
    enum class TraySniState {
        PENDING,
        REGISTERED,
        UNAVAILABLE
    };

    class TraySni {
    public:
        TraySni() = default;
        TraySni(const TraySni&) = delete;
        TraySni& operator=(const TraySni&) = delete;
        ~TraySni();

        bool init(const TrayPixmap *pixmap);
        void poll();
        void set_pixmap(const TrayPixmap *pixmap);
        TraySniState get_state() const;

        std::function<void(TrayAction action)> on_action;
    private:
        static DBusHandlerResult filter_callback(DBusConnection *connection, DBusMessage *message, void *userdata);
        static DBusHandlerResult item_message_callback(DBusConnection *connection, DBusMessage *message, void *userdata);
        static DBusHandlerResult menu_message_callback(DBusConnection *connection, DBusMessage *message, void *userdata);
        static void register_reply_callback(DBusPendingCall *pending_call, void *userdata);
        static void host_registered_reply_callback(DBusPendingCall *pending_call, void *userdata);

        void close_connection();
        void add_match(const char *rule);
        void register_with_watcher();
        void query_host_registered();
        void send_watcher_call(DBusMessage *message, DBusPendingCallNotifyFunction reply_callback);
        DBusMessage* take_watcher_reply(DBusPendingCall *pending_call);
        void cancel_watcher_call();
        void reset_registration();
        void handle_name_owner_changed(DBusMessage *message);
        void update_menu_language();
        DBusHandlerResult send_reply(DBusMessage *message, DBusMessage *reply);
        DBusMessage* handle_item_message(DBusMessage *message);
        DBusMessage* handle_menu_message(DBusMessage *message);
        bool append_item_property(DBusMessageIter *iter, const char *name);
        void on_menu_event(int32_t id, const char *event_id);
    private:
        DBusConnection *connection = nullptr;
        DBusPendingCall *watcher_call = nullptr;
        mgl::Clock watcher_call_clock;
        bool item_registered = false;
        bool host_registered = false;
        const TrayPixmap *pixmap = nullptr;
        std::optional<TrayAction> pending_action;
        std::string menu_language;
        uint32_t menu_revision = 1;
    };
}
