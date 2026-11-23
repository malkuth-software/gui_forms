#include "linux_desktop_notifications.hpp"

#include "../../../core/failure/fail_fast.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <gio/gio.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gui_forms::host::linux_detail {
namespace {

constexpr const char* service_name = "org.freedesktop.Notifications";
constexpr const char* service_path = "/org/freedesktop/Notifications";
constexpr const char* service_interface = "org.freedesktop.Notifications";
// The action a body click invokes (the specification's "default" key).
constexpr const char* default_action = "default";
// The probe's timeout, in milliseconds: a missing or stuck bus must not
// hold the window's start for long.
constexpr int probe_timeout_milliseconds = 500;

} // namespace

// What a reply or a signal reaches: the live notifications object, or null
// once it is gone. Shared by every call in flight and the subscriptions, so
// a late reply or signal finds the null and does nothing.
struct NotificationAnchor final {
    LinuxDesktopNotifications::Impl* impl{};
};

struct LinuxDesktopNotifications::Impl final {
    // The service's identity of one notification, once Notify replied.
    struct Entry final {
        HostDesktopNotificationId id{};
        std::uint32_t server_id{};
        // Notify has not replied yet: server_id is not known.
        bool pending{true};
        // Withdrawn while pending: the reply closes it and reports nothing.
        bool withdrawn{};
    };

    Sink sink{};
    GDBusConnection* connection{};
    GCancellable* cancellable{};
    guint action_subscription{};
    guint closed_subscription{};
    std::shared_ptr<NotificationAnchor> anchor{gui_forms::detail::make_library_shared<NotificationAnchor>()};
    std::vector<Entry> entries{};

    [[nodiscard]] bool connect();
    [[nodiscard]] Entry* find_id(HostDesktopNotificationId id) noexcept;
    [[nodiscard]] Entry* find_server(std::uint32_t server_id) noexcept;
    void erase(const Entry* entry) noexcept;
    void close_on_server(std::uint32_t server_id) noexcept;
    void report(HostDesktopNotificationId id, HostDesktopNotificationAction action);
    void replied(HostDesktopNotificationId id, GVariant* reply);
    void action_invoked(GVariant* parameters);
    void closed(GVariant* parameters);
};

namespace {

// A call in flight: the anchor and the notification it shows. Owned by
// the call; its reply deletes it.
struct NotifyCall final {
    std::shared_ptr<NotificationAnchor> anchor;
    HostDesktopNotificationId id{};
};

// A subscription's context, freed by GIO when the subscription ends.
struct SignalContext final {
    std::shared_ptr<NotificationAnchor> anchor;
};

void free_signal_context(gpointer data) noexcept {
    SignalContext* const context = static_cast<SignalContext*>(data);
    delete context;
}

void answer_notify_replied(GObject* source, GAsyncResult* result, gpointer data) {
    std::unique_ptr<NotifyCall> call(static_cast<NotifyCall*>(data));
    GError* error = nullptr;
    GVariant* const reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (error != nullptr) g_error_free(error);
    LinuxDesktopNotifications::Impl* const impl = (*(*call).anchor).impl;
    if (impl == nullptr) {
        if (reply != nullptr) g_variant_unref(reply);
        return;
    }
    (*impl).replied((*call).id, reply);
    if (reply != nullptr) g_variant_unref(reply);
}

void notify_replied(GObject* source, GAsyncResult* result, gpointer data) {
    gui_forms::detail::call_failing_fast("linux.notifications.notify_replied", &answer_notify_replied, source,
                                         result, data);
}

void answer_action_invoked(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                           GVariant* parameters, gpointer data) {
    LinuxDesktopNotifications::Impl* const impl = (*(*static_cast<SignalContext*>(data)).anchor).impl;
    if (impl != nullptr) (*impl).action_invoked(parameters);
}

void action_invoked(GDBusConnection* unnamed_0, const gchar* unnamed_1, const gchar* unnamed_2,
                    const gchar* unnamed_3, const gchar* unnamed_4, GVariant* parameters, gpointer data) {
    gui_forms::detail::call_failing_fast("linux.notifications.action_invoked", &answer_action_invoked, unnamed_0,
                                         unnamed_1, unnamed_2, unnamed_3, unnamed_4, parameters, data);
}

void answer_notification_closed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                                GVariant* parameters, gpointer data) {
    LinuxDesktopNotifications::Impl* const impl = (*(*static_cast<SignalContext*>(data)).anchor).impl;
    if (impl != nullptr) (*impl).closed(parameters);
}

void notification_closed(GDBusConnection* unnamed_0, const gchar* unnamed_1, const gchar* unnamed_2,
                         const gchar* unnamed_3, const gchar* unnamed_4, GVariant* parameters, gpointer data) {
    gui_forms::detail::call_failing_fast("linux.notifications.notification_closed", &answer_notification_closed,
                                         unnamed_0, unnamed_1, unnamed_2, unnamed_3, unnamed_4, parameters, data);
}

// Whether the bus names `name` among the strings of an (as) reply.
bool reply_lists(GVariant* reply, const char* name) {
    GVariant* names = nullptr;
    g_variant_get(reply, "(@as)", &names);
    const gsize count = g_variant_n_children(names);
    bool found = false;
    for (gsize index = 0U; index < count && !found; ++index) {
        const gchar* candidate = nullptr;
        g_variant_get_child(names, index, "&s", &candidate);
        found = g_strcmp0(candidate, name) == 0;
    }
    g_variant_unref(names);
    return found;
}

bool probe_service() noexcept {
    GError* error = nullptr;
    GDBusConnection* const connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (connection == nullptr) {
        if (error != nullptr) g_error_free(error);
        return false;
    }
    bool present = false;
    GVariant* const owned = g_dbus_connection_call_sync(
        connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
        g_variant_new("(s)", service_name), G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE,
        probe_timeout_milliseconds, nullptr, &error);
    if (owned != nullptr) {
        gboolean has_owner = FALSE;
        g_variant_get(owned, "(b)", &has_owner);
        present = has_owner != FALSE;
        g_variant_unref(owned);
    } else if (error != nullptr) {
        g_clear_error(&error);
    }
    if (!present) {
        GVariant* const activatable = g_dbus_connection_call_sync(
            connection, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
            "ListActivatableNames", nullptr, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE,
            probe_timeout_milliseconds, nullptr, &error);
        if (activatable != nullptr) {
            present = reply_lists(activatable, service_name);
            g_variant_unref(activatable);
        } else if (error != nullptr) {
            g_clear_error(&error);
        }
    }
    g_object_unref(connection);
    return present;
}

// The urgency hint's byte (the specification's 0 low, 1 normal, 2 critical).
guchar urgency_level(HostDesktopNotificationUrgency urgency) noexcept {
    switch (urgency) {
    case HostDesktopNotificationUrgency::low:
        return 0U;
    case HostDesktopNotificationUrgency::normal:
        return 1U;
    case HostDesktopNotificationUrgency::critical:
        return 2U;
    }
    // HostServices refuses an urgency outside the enumeration first.
    return 1U;
}

} // namespace

bool LinuxDesktopNotifications::service_present() noexcept {
    // Once per process: the answer is the session's, not a window's.
    static const bool present = probe_service();
    return present;
}

LinuxDesktopNotifications::LinuxDesktopNotifications(Sink sink) : impl_(std::make_unique<Impl>()) {
    (*impl_).sink = sink;
    (*(*impl_).anchor).impl = impl_.get();
}

LinuxDesktopNotifications::~LinuxDesktopNotifications() {
    Impl& impl = *impl_;
    // A late reply or signal finds the anchor empty.
    (*impl.anchor).impl = nullptr;
    if (impl.cancellable != nullptr) {
        g_cancellable_cancel(impl.cancellable);
        g_object_unref(impl.cancellable);
    }
    if (impl.connection != nullptr) {
        if (impl.action_subscription != 0U) {
            g_dbus_connection_signal_unsubscribe(impl.connection, impl.action_subscription);
        }
        if (impl.closed_subscription != 0U) {
            g_dbus_connection_signal_unsubscribe(impl.connection, impl.closed_subscription);
        }
        g_object_unref(impl.connection);
    }
}

// The session bus and the two signals, on the first show.
bool LinuxDesktopNotifications::Impl::connect() {
    if (connection != nullptr) return true;
    GError* error = nullptr;
    GDBusConnection* const bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (bus == nullptr) {
        if (error != nullptr) g_error_free(error);
        return false;
    }
    connection = bus;
    cancellable = g_cancellable_new();
    SignalContext* const action_context = new SignalContext{anchor};
    action_subscription = g_dbus_connection_signal_subscribe(
        connection, service_name, service_interface, "ActionInvoked", service_path, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, action_invoked, action_context, free_signal_context);
    SignalContext* const closed_context = new SignalContext{anchor};
    closed_subscription = g_dbus_connection_signal_subscribe(
        connection, service_name, service_interface, "NotificationClosed", service_path, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, notification_closed, closed_context, free_signal_context);
    return true;
}

LinuxDesktopNotifications::Impl::Entry* LinuxDesktopNotifications::Impl::find_id(
    HostDesktopNotificationId id) noexcept {
    for (Entry& entry : entries) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

LinuxDesktopNotifications::Impl::Entry* LinuxDesktopNotifications::Impl::find_server(
    std::uint32_t server_id) noexcept {
    for (Entry& entry : entries) {
        if (!entry.pending && entry.server_id == server_id) return &entry;
    }
    return nullptr;
}

void LinuxDesktopNotifications::Impl::erase(const Entry* entry) noexcept {
    const std::ptrdiff_t index = entry - entries.data();
    entries.erase(entries.begin() + index);
}

void LinuxDesktopNotifications::Impl::close_on_server(std::uint32_t server_id) noexcept {
    if (connection == nullptr) return;
    // Nothing waits for the answer: a service that already closed it
    // answers with an error, which is dropped.
    g_dbus_connection_call(connection, service_name, service_path, service_interface, "CloseNotification",
                           g_variant_new("(u)", server_id), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr,
                           nullptr);
}

// The entry is gone before the sink runs: the application's handler may
// show or withdraw again.
void LinuxDesktopNotifications::Impl::report(HostDesktopNotificationId id, HostDesktopNotificationAction action) {
    const HostDesktopNotificationEvent event{id, action};
    sink(event);
}

void LinuxDesktopNotifications::Impl::replied(HostDesktopNotificationId id, GVariant* reply) {
    Entry* const entry = find_id(id);
    if (entry == nullptr) return;
    if (reply == nullptr) {
        const bool withdrawn = (*entry).withdrawn;
        erase(entry);
        if (!withdrawn) report(id, HostDesktopNotificationAction::failed);
        return;
    }
    guint32 server_id = 0U;
    g_variant_get(reply, "(u)", &server_id);
    if ((*entry).withdrawn) {
        erase(entry);
        close_on_server(server_id);
        return;
    }
    (*entry).server_id = server_id;
    (*entry).pending = false;
}

// ActionInvoked: (us).
void LinuxDesktopNotifications::Impl::action_invoked(GVariant* parameters) {
    guint32 server_id = 0U;
    const gchar* key = nullptr;
    g_variant_get(parameters, "(u&s)", &server_id, &key);
    if (g_strcmp0(key, default_action) != 0) return;
    Entry* const entry = find_server(server_id);
    if (entry == nullptr) return;
    const HostDesktopNotificationId id = (*entry).id;
    erase(entry);
    report(id, HostDesktopNotificationAction::activated);
}

// NotificationClosed: (uu). A notification activated first was erased then,
// and one withdrawn was erased by withdraw: the close that follows finds
// nothing. Every other reason (expired, dismissed, closed by another
// client, undefined) reports dismissed.
void LinuxDesktopNotifications::Impl::closed(GVariant* parameters) {
    guint32 server_id = 0U;
    guint32 reason = 0U;
    g_variant_get(parameters, "(uu)", &server_id, &reason);
    Entry* const entry = find_server(server_id);
    if (entry == nullptr) return;
    const HostDesktopNotificationId id = (*entry).id;
    erase(entry);
    report(id, HostDesktopNotificationAction::dismissed);
}

HostServiceStatus LinuxDesktopNotifications::show(HostDesktopNotificationId notification_id,
                                                  const HostDesktopNotificationRequest& request) {
    Impl& impl = *impl_;
    if (!impl.connect()) return {HostServiceError::backend_failure};
    // The specification's summary is required: without a title the text is
    // the summary. The body may be read as markup, so it is escaped.
    const std::string& summary = request.title.empty() ? request.text : request.title;
    std::string body;
    if (!request.title.empty()) {
        gchar* const escaped = g_markup_escape_text(request.text.c_str(), static_cast<gssize>(request.text.size()));
        body = escaped;
        g_free(escaped);
    }
    const gchar* const program = g_get_prgname();
    GVariantBuilder actions;
    g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&actions, "s", default_action);
    g_variant_builder_add(&actions, "s", "");
    GVariantBuilder hints;
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&hints, "{sv}", "urgency", g_variant_new_byte(urgency_level(request.urgency)));
    if (request.silent) g_variant_builder_add(&hints, "{sv}", "suppress-sound", g_variant_new_boolean(TRUE));
    // -1: the service's own expiry.
    GVariant* const parameters =
        g_variant_new("(susssasa{sv}i)", program != nullptr ? program : "", 0U, "", summary.c_str(), body.c_str(),
                      &actions, &hints, -1);
    impl.entries.push_back(Impl::Entry{notification_id, 0U, true, false});
    NotifyCall* const call = new NotifyCall{impl.anchor, notification_id};
    g_dbus_connection_call(impl.connection, service_name, service_path, service_interface, "Notify", parameters,
                           G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, impl.cancellable, notify_replied, call);
    return {};
}

void LinuxDesktopNotifications::withdraw(HostDesktopNotificationId notification_id) noexcept {
    Impl& impl = *impl_;
    Impl::Entry* const entry = impl.find_id(notification_id);
    if (entry == nullptr) return;
    if ((*entry).pending) {
        (*entry).withdrawn = true;
        return;
    }
    const std::uint32_t server_id = (*entry).server_id;
    impl.erase(entry);
    impl.close_on_server(server_id);
}

} // namespace gui_forms::host::linux_detail
