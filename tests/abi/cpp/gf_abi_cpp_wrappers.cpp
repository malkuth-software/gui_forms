// The generated C++ wrappers (include/gui_forms/c/gf_abi.hpp) against the
// built library: negotiation, a slot the library did not serve answering
// GF_STATUS_UNSUPPORTED_VERSION instead of calling through, and the two
// owners, ObjectReference and SubscriptionReference, releasing and
// disconnecting exactly once. The program links only the ABI library, as a
// C++ consumer of the installed SDK would.

#include "gui_forms/c/gf_abi.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>

namespace {

using gui_forms::abi::EventsTable;
using gui_forms::abi::ObjectReference;
using gui_forms::abi::ObjectsTable;
using gui_forms::abi::PropertiesTable;
using gui_forms::abi::RuntimeTable;
using gui_forms::abi::SubscriptionReference;
using gui_forms::abi::WindowsTable;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures = failures + 1;
    }
}

gf_utf8_view text_view(std::string_view text) {
    gf_utf8_view view{};
    view.data = text.data();
    view.size = text.size();
    return view;
}

// The invocations and the release of one counting callback.
struct CallbackCounts final {
    std::uint32_t invocations{};
    std::uint32_t releases{};
};

void count_invoke(void* context, gf_event* event) {
    (void)event;
    CallbackCounts* counts = static_cast<CallbackCounts*>(context);
    (*counts).invocations = (*counts).invocations + 1u;
}

void count_release(void* context) {
    CallbackCounts* counts = static_cast<CallbackCounts*>(context);
    (*counts).releases = (*counts).releases + 1u;
}

gf_status set_checked(const PropertiesTable& properties, gf_object check_box, bool checked) {
    gf_value value{};
    value.struct_size = static_cast<std::uint32_t>(sizeof(value));
    value.kind = GF_VALUE_KIND_BOOLEAN;
    value.boolean = checked ? 1u : 0u;
    const gf_status set = properties.set(check_box, text_view("checked"), &value);
    return set;
}

void negotiation(const RuntimeTable& runtime) {
    gf_abi_version_info info{};
    info.struct_size = static_cast<std::uint32_t>(sizeof(info));
    require(runtime.version(&info) == GF_STATUS_OK, "runtime.version through the wrapper");
    require(info.abi_major == GF_ABI_MAJOR, "the wrapper negotiated the header's major");

    // A table served only up to version: the later slots answer without calling through.
    gf_runtime_table partial = runtime.c_table();
    partial.struct_size = static_cast<std::uint32_t>(offsetof(gf_runtime_table, error_detail));
    const RuntimeTable older = RuntimeTable::adopt(partial);
    require(older.version(&info) == GF_STATUS_OK, "a served slot calls through");
    require(older.clear_error() == GF_STATUS_UNSUPPORTED_VERSION, "a slot beyond struct_size is not called");
    const RuntimeTable empty;
    require(empty.version(&info) == GF_STATUS_UNSUPPORTED_VERSION, "an empty wrapper serves nothing");
}

void object_reference(const ObjectsTable& objects) {
    gf_object created = 0u;
    require(objects.create(text_view("label"), text_view("cpp.label"), &created) == GF_STATUS_OK, "create a label");
    ObjectReference label(objects, created);
    gf_object_state state = 99u;
    require(objects.state(label.get(), &state) == GF_STATUS_OK && state == GF_OBJECT_STATE_ALIVE, "the label is alive");

    ObjectReference moved(std::move(label));
    require(label.get() == 0u && moved.get() == created, "a move transfers the reference");
    require(label.reset() == GF_STATUS_OK, "resetting an empty owner releases nothing");
    require(moved.reset() == GF_STATUS_OK, "reset releases the reference");
    require(objects.state(created, &state) == GF_STATUS_STALE_HANDLE, "the released label is stale");

    gf_object kept = 0u;
    require(objects.create(text_view("label"), text_view("cpp.kept"), &kept) == GF_STATUS_OK, "create another");
    ObjectReference detaching(objects, kept);
    const gf_object detached = detaching.detach();
    require(detached == kept && detaching.get() == 0u, "detach gives the reference up");
    require(objects.release(detached) == GF_STATUS_OK, "the detached reference is the caller's to release");
}

void subscription_reference(const ObjectsTable& objects, const EventsTable& events,
                            const PropertiesTable& properties) {
    gf_object created = 0u;
    require(objects.create(text_view("check_box"), text_view("cpp.check"), &created) == GF_STATUS_OK,
            "create a check box");
    const ObjectReference check_box(objects, created);
    CallbackCounts counts{};
    gf_callback callback{};
    callback.struct_size = static_cast<std::uint32_t>(sizeof(callback));
    callback.invoke = &count_invoke;
    callback.release = &count_release;
    callback.context = &counts;
    gf_subscription subscribed = 0u;
    require(events.subscribe(check_box.get(), text_view("check_state_changed"), &callback, &subscribed) ==
                GF_STATUS_OK,
            "subscribe through the wrapper");
    {
        const SubscriptionReference subscription(events, subscribed);
        require(set_checked(properties, check_box.get(), true) == GF_STATUS_OK, "check the box");
        require(counts.invocations == 1u, "the subscribed callback ran");
    }
    require(counts.releases == 1u, "the owner disconnected, and the library released the callback once");
    require(set_checked(properties, check_box.get(), false) == GF_STATUS_OK, "uncheck the box");
    require(counts.invocations == 1u, "a disconnected callback does not run");
}

// The window slots added in 1.1: served by this library, and answering
// GF_STATUS_UNSUPPORTED_VERSION from a table a 1.0 library served.
void window_slots(const ObjectsTable& objects, const WindowsTable& windows) {
    std::uint64_t count = 0u;
    require(windows.interaction_count(&count) == GF_STATUS_OK && count == 7u, "a window raises seven interactions");
    gf_schema_interaction interaction{};
    interaction.struct_size = static_cast<std::uint32_t>(sizeof(interaction));
    require(windows.interaction_at(0u, &interaction) == GF_STATUS_OK && interaction.name.size > 0u,
            "interaction_at through the wrapper");

    gf_object root_made = 0u;
    require(objects.create(text_view("Panel"), text_view("cpp.window"), &root_made) == GF_STATUS_OK,
            "create a window root");
    const ObjectReference root(objects, root_made);
    gf_object window_made = 0u;
    require(windows.create(root.get(), &window_made) == GF_STATUS_OK, "create a window");
    const ObjectReference window(objects, window_made);
    std::uint32_t handled = 9u;
    require(windows.request_help(window.get(), &handled) == GF_STATUS_OK && handled == 0u,
            "an unanswered help request");

    gf_windows_table older_table = windows.c_table();
    older_table.struct_size = static_cast<std::uint32_t>(offsetof(gf_windows_table, interaction_count));
    const WindowsTable older = WindowsTable::adopt(older_table);
    require(older.interaction_count(&count) == GF_STATUS_UNSUPPORTED_VERSION &&
                older.request_help(window.get(), &handled) == GF_STATUS_UNSUPPORTED_VERSION,
            "a 1.0 windows table serves none of the 1.1 slots");

    // Modal and owned windows (since 1.1): refused outside a run, and not
    // served by a table that ends before them.
    gf_owned_window_options owned{};
    owned.struct_size = static_cast<std::uint32_t>(sizeof(owned));
    gf_dialog_result_code result = 99u;
    require(windows.show_owned(root.get(), &owned) == GF_STATUS_INVALID_STATE &&
                windows.show_modal(root.get(), &owned, &result) == GF_STATUS_INVALID_STATE && result == 99u,
            "show_owned and show_modal outside a run");
    gf_windows_table before_modal = windows.c_table();
    before_modal.struct_size = static_cast<std::uint32_t>(offsetof(gf_windows_table, show_modal));
    const WindowsTable without_modal = WindowsTable::adopt(before_modal);
    require(without_modal.show_modal(root.get(), &owned, &result) == GF_STATUS_UNSUPPORTED_VERSION &&
                without_modal.show_owned(root.get(), &owned) == GF_STATUS_UNSUPPORTED_VERSION,
            "a table without the modal slots serves neither");
}

} // namespace

int main() {
    RuntimeTable runtime;
    ObjectsTable objects;
    EventsTable events;
    PropertiesTable properties;
    WindowsTable windows;
    if (RuntimeTable::negotiate(runtime) != GF_STATUS_OK || ObjectsTable::negotiate(objects) != GF_STATUS_OK ||
        EventsTable::negotiate(events) != GF_STATUS_OK || PropertiesTable::negotiate(properties) != GF_STATUS_OK ||
        WindowsTable::negotiate(windows) != GF_STATUS_OK) {
        std::cerr << "FAIL: the tables did not negotiate\n";
        return 1;
    }
    negotiation(runtime);
    object_reference(objects);
    subscription_reference(objects, events, properties);
    window_slots(objects, windows);
    if (failures != 0) {
        std::cerr << failures << " checks failed\n";
        return 1;
    }
    std::cout << "gui_forms_abi_cpp_wrappers: all checks passed\n";
    return 0;
}
