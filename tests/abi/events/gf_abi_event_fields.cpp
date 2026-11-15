// Stage C5: a C++ event raised directly reaches a C ABI callback as named
// fields (gf_abi.h, "Event fields"), and what the callback writes with
// events.write_field reaches the C++ payload when the callback returns.
//
// The program links the ABI's objects and the C++ library statically, so
// both sides share one process: it makes and subscribes objects through
// gf_abi_get_table alone, then finds the C++ control behind a handle in the
// ABI's registry and raises its events itself, with payloads no property
// change can produce (a cancellable tab selection, a label edit, a
// selection change, an event of two arguments).

#include "gui_forms/c/gf_abi.h"

#include "../../../src/abi/stable/events/abi_event_fields.hpp"
#include "../../../src/abi/stable/registry/abi_registry.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/component/failures/component_failures.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/list_view.hpp"
#include "gui_forms/controls/plot3d_view.hpp"
#include "gui_forms/controls/tab_control.hpp"
#include "gui_forms/controls/tab_page.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// N12's measurement: every allocation of the process is counted, so the
// cost of one emission through the ABI forwarder is the count across it.
// Single threaded: the program raises its events on its one thread.
//
// Under AddressSanitizer on Windows the replacement collides with the
// runtime's own operator new and delete (both defined at link time), so the
// sanitized build keeps the runtime's and skips the count; normal builds
// still measure and assert it.
#if defined(_WIN32) && defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GF_EVENT_FIELDS_COUNT_ALLOCATIONS 0
#endif
#endif
#ifndef GF_EVENT_FIELDS_COUNT_ALLOCATIONS
#define GF_EVENT_FIELDS_COUNT_ALLOCATIONS 1
#endif

namespace event_fields_allocation_probe {
std::uint64_t allocations = 0U;
constexpr bool counted = GF_EVENT_FIELDS_COUNT_ALLOCATIONS != 0;
} // namespace event_fields_allocation_probe

#if GF_EVENT_FIELDS_COUNT_ALLOCATIONS
void* operator new(std::size_t bytes) {
    event_fields_allocation_probe::allocations = event_fields_allocation_probe::allocations + 1U;
    void* memory = std::malloc(bytes == 0U ? 1U : bytes);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}
void* operator new[](std::size_t bytes) {
    void* memory = ::operator new(bytes);
    return memory;
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
#endif

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures = failures + 1;
    }
}

void require_ok(gf_status status, std::string_view call) {
    if (status != GF_STATUS_OK) {
        throw std::runtime_error(std::string(call) + " failed with status " + std::to_string(status));
    }
}

gf_utf8_view text_view(std::string_view text) {
    gf_utf8_view view{};
    view.data = text.data();
    view.size = text.size();
    return view;
}

std::string_view view_text(gf_utf8_view view) {
    const std::string_view text(view.data, static_cast<std::size_t>(view.size));
    return text;
}

struct Tables final {
    gf_objects_table objects{};
    gf_events_table events{};
};

Tables load_tables() {
    Tables tables;
    tables.objects.struct_size = sizeof(gf_objects_table);
    tables.events.struct_size = sizeof(gf_events_table);
    require_ok(gf_abi_get_table(GF_TABLE_OBJECTS, GF_ABI_MAJOR, GF_ABI_MINOR, &tables.objects, sizeof(tables.objects)),
               "objects table");
    require_ok(gf_abi_get_table(GF_TABLE_EVENTS, GF_ABI_MAJOR, GF_ABI_MINOR, &tables.events, sizeof(tables.events)),
               "events table");
    return tables;
}

gf_object create(const Tables& tables, std::string_view type, std::string_view stable_id) {
    gf_object object = 0U;
    require_ok(tables.objects.create(text_view(type), text_view(stable_id), &object), "create");
    return object;
}

// The C++ control behind a handle, as the ABI's registry holds it.
template <typename Type>
std::shared_ptr<Type> control_of(gf_object object) {
    Control::Ptr control;
    require_ok(abi1::registry().find_object(object, abi1::SlotIdentity{GF_TABLE_OBJECTS, 0U}, control),
               "find_object");
    const std::shared_ptr<Type> typed = std::dynamic_pointer_cast<Type>(control);
    if (!typed) {
        throw std::runtime_error("the object is not of the expected type");
    }
    return typed;
}

// One field of an event by name, or null.
const gf_event_field* field_named(const gf_event& event, std::string_view name, std::uint64_t& index) {
    for (index = 0U; index < event.field_count; ++index) {
        if (view_text(event.fields[index].name) == name) {
            return &event.fields[index];
        }
    }
    return nullptr;
}

gf_value blank_value(gf_value_kind kind) {
    gf_value value{};
    value.struct_size = sizeof(gf_value);
    value.kind = kind;
    return value;
}

// What a callback saw and did, for the checks after the emission.
struct Probe final {
    const Tables* tables = nullptr;
    int invoked = 0;
    std::vector<std::string> names;
    std::vector<gf_value_kind> kinds;
    std::vector<gf_event_field_flags> flags;
    bool payload_null = false;
    gf_object lent = 0U;
    std::string action_name;
    gf_status cancel_written = GF_STATUS_INTERNAL;
    gf_status object_refused = GF_STATUS_INTERNAL;
    gf_status kind_refused = GF_STATUS_INTERNAL;
    gf_status choice_refused = GF_STATUS_INTERNAL;
    gf_status past_end = GF_STATUS_INTERNAL;
    bool record_changed = false;
    std::uint64_t previous_count = 0U;
    std::uint64_t first_previous = 0U;
    gf_status element_past_end = GF_STATUS_INTERNAL;
    gf_value_kind active_kind = GF_VALUE_KIND_TEXT;
    gf_value_kind active_declared = GF_VALUE_KIND_NULL;
    gf_status label_written = GF_STATUS_INTERNAL;
    std::string first_name;
    std::string flag_names;
    bool keep_lent = false;
};

void record_names(Probe& probe, const gf_event& event) {
    probe.invoked = probe.invoked + 1;
    probe.names.clear();
    probe.kinds.clear();
    probe.flags.clear();
    for (std::uint64_t index = 0U; index < event.field_count; ++index) {
        probe.names.emplace_back(view_text(event.fields[index].name));
        probe.kinds.push_back(event.fields[index].kind);
        probe.flags.push_back(event.fields[index].flags);
    }
    probe.payload_null = event.payload == nullptr;
}

// selecting: cancels through the cancel field and tries refused writes.
void GF_CALL selecting_invoke(void* context, gf_event* event) {
    Probe& probe = *static_cast<Probe*>(context);
    record_names(probe, *event);
    std::uint64_t index = 0U;
    const gf_event_field* page = field_named(*event, "page", index);
    if (page != nullptr) {
        probe.lent = (*(*page).value).unsigned_integer;
        const gf_value value = blank_value(GF_VALUE_KIND_OBJECT);
        probe.object_refused = (*probe.tables).events.write_field(event, index, &value);
        if (probe.keep_lent && probe.lent != 0U) {
            require_ok((*probe.tables).objects.retain(probe.lent), "retain the lent page");
        }
    }
    const gf_event_field* action = field_named(*event, "action", index);
    if (action != nullptr) {
        probe.action_name = view_text((*(*action).value).text);
        gf_value unknown = blank_value(GF_VALUE_KIND_ENUMERATION);
        unknown.signed_integer = 99;
        probe.choice_refused = (*probe.tables).events.write_field(event, index, &unknown);
    }
    const gf_event_field* cancel = field_named(*event, "cancel", index);
    if (cancel != nullptr) {
        const gf_value wrong = blank_value(GF_VALUE_KIND_TEXT);
        probe.kind_refused = (*probe.tables).events.write_field(event, index, &wrong);
        gf_value yes = blank_value(GF_VALUE_KIND_BOOLEAN);
        yes.boolean = 1U;
        probe.cancel_written = (*probe.tables).events.write_field(event, index, &yes);
        probe.record_changed = (*(*cancel).value).boolean == 1U;
    }
    const gf_value yes = blank_value(GF_VALUE_KIND_BOOLEAN);
    probe.past_end = (*probe.tables).events.write_field(event, (*event).field_count, &yes);
}

void cancellable_selection(const Tables& tables) {
    const gf_object tabs = create(tables, "tab_control", "fields.tabs");
    const gf_object page = create(tables, "tab_page", "fields.page");
    const std::shared_ptr<TabControl> control = control_of<TabControl>(tabs);
    const std::shared_ptr<TabPage> page_control = control_of<TabPage>(page);
    // The page's handle is released, so the event must lend it an identity.
    require_ok(tables.objects.release(page), "release the page handle");

    Probe probe;
    probe.tables = &tables;
    gf_callback callback{};
    callback.struct_size = sizeof(gf_callback);
    callback.invoke = &selecting_invoke;
    callback.context = &probe;
    gf_subscription subscription = 0U;
    require_ok(tables.events.subscribe(tabs, text_view("selecting"), &callback, &subscription), "subscribe selecting");

    TabControlCancelEvent event{page_control, 0U, TabControlAction::selecting, false};
    (*control).selecting().emit(event);
    require(probe.invoked == 1, "selecting reached the callback once");
    const std::vector<std::string> expected{"page", "index", "action", "cancel"};
    require(probe.names == expected, "a struct payload's fields are its members in order");
    require(probe.kinds.size() == 4U && probe.kinds[0] == GF_VALUE_KIND_OBJECT &&
                probe.kinds[1] == GF_VALUE_KIND_UNSIGNED && probe.kinds[2] == GF_VALUE_KIND_ENUMERATION &&
                probe.kinds[3] == GF_VALUE_KIND_BOOLEAN,
            "the fields have the members' kinds");
    require(probe.flags.size() == 4U && (probe.flags[0] & GF_EVENT_FIELD_WRITABLE) == 0U &&
                (probe.flags[3] & GF_EVENT_FIELD_WRITABLE) != 0U,
            "a mutable payload's scalars are writable and its objects are not");
    require(probe.payload_null, "a struct payload is not a single payload value");
    require(probe.lent != 0U, "the page is lent an identity");
    require(probe.action_name == "selecting", "an enumeration field carries its choice's name");
    require(probe.object_refused == GF_STATUS_INVALID_ARGUMENT, "an object field is not writable");
    require(probe.kind_refused == GF_STATUS_INVALID_ARGUMENT, "a value of another kind is refused");
    require(probe.choice_refused == GF_STATUS_INVALID_ARGUMENT, "an unknown choice is refused");
    require(probe.past_end == GF_STATUS_NOT_FOUND, "an index past the fields is not found");
    require(probe.cancel_written == GF_STATUS_OK && probe.record_changed,
            "a written field changes the record at once");
    require(event.cancel, "what the callback wrote reached the C++ payload");
    require(event.action == TabControlAction::selecting && event.index == 0U,
            "fields the callback did not write keep their values");
    require(tables.objects.retain(probe.lent) == GF_STATUS_STALE_HANDLE,
            "an identity lent for the callback ends with it");

    probe.keep_lent = true;
    TabControlCancelEvent kept{page_control, 1U, TabControlAction::selecting, false};
    (*control).selecting().emit(kept);
    require(probe.lent != 0U && tables.objects.retain(probe.lent) == GF_STATUS_OK,
            "a lent identity the callback retained lives on");
    require_ok(tables.objects.release(probe.lent), "release the retained page (second reference)");
    require_ok(tables.objects.release(probe.lent), "release the retained page");

    require_ok(tables.events.disconnect(subscription), "disconnect selecting");
    require_ok(tables.objects.release(tabs), "release the tab control");
}

// selection_changed: collections read on demand, an absent optional.
void GF_CALL selection_invoke(void* context, gf_event* event) {
    Probe& probe = *static_cast<Probe*>(context);
    record_names(probe, *event);
    std::uint64_t index = 0U;
    const gf_event_field* previous = field_named(*event, "previous", index);
    if (previous != nullptr) {
        probe.previous_count = (*(*previous).value).unsigned_integer;
        gf_value element = blank_value(GF_VALUE_KIND_NULL);
        if ((*probe.tables).events.field_element(event, index, 1U, &element) == GF_STATUS_OK &&
            element.kind == GF_VALUE_KIND_UNSIGNED) {
            probe.first_previous = element.unsigned_integer;
        }
        probe.element_past_end =
            (*probe.tables).events.field_element(event, index, probe.previous_count, &element);
    }
    const gf_event_field* active = field_named(*event, "active_index", index);
    if (active != nullptr) {
        probe.active_kind = (*(*active).value).kind;
        probe.active_declared = (*active).kind;
    }
}

void collections(const Tables& tables) {
    const gf_object list = create(tables, "list_box", "fields.list");
    const std::shared_ptr<ListBox> control = control_of<ListBox>(list);
    Probe probe;
    probe.tables = &tables;
    gf_callback callback{};
    callback.struct_size = sizeof(gf_callback);
    callback.invoke = &selection_invoke;
    callback.context = &probe;
    gf_subscription subscription = 0U;
    require_ok(tables.events.subscribe(list, text_view("selection_changed"), &callback, &subscription),
               "subscribe selection_changed");
    const ListSelectionChange change{{4U, 7U}, {9U}, std::nullopt};
    (*control).selection_changed().emit(change);
    require(probe.invoked == 1, "selection_changed reached the callback");
    require(probe.previous_count == 2U && probe.first_previous == 7U,
            "a collection field holds its count and reads its elements on demand");
    require(probe.element_past_end == GF_STATUS_NOT_FOUND, "an element past the end is not found");
    require(probe.active_kind == GF_VALUE_KIND_NULL && probe.active_declared == GF_VALUE_KIND_UNSIGNED,
            "an absent optional is NULL with its declared kind");
    require_ok(tables.events.disconnect(subscription), "disconnect selection_changed");
    require_ok(tables.objects.release(list), "release the list box");
}

// before_label_edit: an optional text written, then cleared.
struct LabelWrite final {
    const Tables* tables = nullptr;
    bool clear = false;
    gf_status status = GF_STATUS_INTERNAL;
};

void GF_CALL label_invoke(void* context, gf_event* event) {
    LabelWrite& write = *static_cast<LabelWrite*>(context);
    std::uint64_t index = 0U;
    const gf_event_field* label = field_named(*event, "label", index);
    if (label == nullptr) {
        return;
    }
    gf_value value = blank_value(write.clear ? GF_VALUE_KIND_NULL : GF_VALUE_KIND_TEXT);
    // The callback's own text: the library copies it.
    char renamed[] = "renamed";
    value.text.data = renamed;
    value.text.size = write.clear ? 0U : 7U;
    write.status = (*write.tables).events.write_field(event, index, &value);
    std::memset(renamed, 'x', sizeof(renamed) - 1U);
}

void optional_text(const Tables& tables) {
    const gf_object view = create(tables, "list_view", "fields.view");
    const std::shared_ptr<ListView> control = control_of<ListView>(view);
    LabelWrite write;
    write.tables = &tables;
    gf_callback callback{};
    callback.struct_size = sizeof(gf_callback);
    callback.invoke = &label_invoke;
    callback.context = &write;
    gf_subscription subscription = 0U;
    require_ok(tables.events.subscribe(view, text_view("before_label_edit"), &callback, &subscription),
               "subscribe before_label_edit");
    ListViewLabelEdit edit{"list", 0U, "item", std::nullopt, false};
    (*control).before_label_edit().emit(edit);
    require(write.status == GF_STATUS_OK && edit.label.has_value() && *edit.label == "renamed",
            "an absent optional text is written, and the library kept its own copy");
    write.clear = true;
    (*control).before_label_edit().emit(edit);
    require(write.status == GF_STATUS_OK && !edit.label.has_value(), "NULL clears an optional");
    require_ok(tables.events.disconnect(subscription), "disconnect before_label_edit");
    require_ok(tables.objects.release(view), "release the list view");
}

// The checked build's lent scratch (borrow sanitizer study section 2.5): a
// callback that keeps a field's text past its invoke. Mode `kept_record`
// reads the kept text after the emission; under AddressSanitizer the
// library poisoned it when the invoke returned, so the read is reported and
// the process ends before it prints "survived" (registered only in the
// checked sanitizer tree).
struct KeptText final {
    const char* text = nullptr;
    std::uint64_t size = 0U;
};

void GF_CALL keep_text_invoke(void* context, gf_event* event) {
    KeptText& kept = *static_cast<KeptText*>(context);
    for (std::uint64_t index = 0U; index < (*event).field_count; ++index) {
        const gf_value& value = *(*event).fields[index].value;
        if (value.kind == GF_VALUE_KIND_TEXT && value.text.size != 0U) {
            kept.text = value.text.data;
            kept.size = value.text.size;
            return;
        }
    }
}

int kept_record_mode() {
    const Tables tables = load_tables();
    const gf_object view = create(tables, "list_view", "fields.kept");
    const std::shared_ptr<ListView> control = control_of<ListView>(view);
    KeptText kept;
    gf_callback callback{};
    callback.struct_size = sizeof(gf_callback);
    callback.invoke = &keep_text_invoke;
    callback.context = &kept;
    gf_subscription subscription = 0U;
    require_ok(tables.events.subscribe(view, text_view("before_label_edit"), &callback, &subscription),
               "subscribe before_label_edit");
    ListViewLabelEdit edit{"list", 0U, "item", std::nullopt, false};
    (*control).before_label_edit().emit(edit);
    if (kept.text == nullptr) {
        std::cerr << "FAIL: no text field was lent\n";
        return 1;
    }
    // The invoke is over: this read is the caller's defect the checked
    // build exposes.
    const char first = kept.text[0];
    std::cout << "read " << static_cast<int>(first) << "\nsurvived\n";
    return 0;
}

// initialization_completed: two arguments, one a combination of flags.
void GF_CALL two_arguments_invoke(void* context, gf_event* event) {
    Probe& probe = *static_cast<Probe*>(context);
    record_names(probe, *event);
    if ((*event).field_count >= 1U) {
        probe.flag_names = view_text((*(*event).fields[0].value).text);
    }
}

void two_arguments(const Tables& tables) {
    const gf_object panel = create(tables, "panel", "fields.panel");
    const Control::Ptr control = control_of<Control>(panel);
    Probe probe;
    probe.tables = &tables;
    gf_callback callback{};
    callback.struct_size = sizeof(gf_callback);
    callback.invoke = &two_arguments_invoke;
    callback.context = &probe;
    gf_subscription subscription = 0U;
    require_ok(tables.events.subscribe(panel, text_view("initialization_completed"), &callback, &subscription),
               "subscribe initialization_completed");
    (*control).initialization_completed().emit(Dirty::measure | Dirty::paint, true);
    const std::vector<std::string> expected{"value_1", "value_2"};
    require(probe.names == expected, "several arguments are value_1, value_2");
    require(probe.payload_null, "several arguments are not a single payload value");
    require(probe.flag_names == "measure, paint", "a combination of flags is named by its choices");
    (*control).initialization_completed().emit(Dirty::layout, false);
    require(probe.flag_names == "layout", "a combination that is itself a choice takes that choice's name");
    require_ok(tables.events.disconnect(subscription), "disconnect initialization_completed");
    require_ok(tables.objects.release(panel), "release the panel");
}

// The fixed-size array fields (since 1.5): a plot's camera_changed, hovered
// and picked reach an ABI callback with their std::array members as
// COLLECTION fields whose elements are lent in place.
struct ArrayProbe final {
    const Tables* tables = nullptr;
    int invoked = 0;
    std::vector<std::string> names;
    std::vector<gf_value_kind> kinds;
    bool payload_null = false;
    // The first array field the callback looked for, as lent.
    std::uint64_t array_count = 0U;
    std::uint64_t array_element_count = 0U;
    std::vector<double> array_numbers;
    bool elements_numbers = false;
    double element_read = 0.0;
    gf_status element_status = GF_STATUS_INTERNAL;
    gf_status element_past_end = GF_STATUS_INTERNAL;
    gf_status write_refused = GF_STATUS_INTERNAL;
    // The second array field (a hit's surface_parameter).
    std::uint64_t second_count = 0U;
    std::vector<double> second_numbers;
    std::string kind_name;
    std::uint64_t item = 0U;
    std::string projection_name;
    std::string_view array_name;
};

// Reads a fixed-size array field of numbers as lent: its count, its records
// in place, and that every record is a NUMBER.
void read_numbers(const gf_event_field& field, std::uint64_t& count, std::uint64_t& element_count,
                  std::vector<double>& numbers, bool& all_numbers) {
    const gf_value& value = *field.value;
    count = value.unsigned_integer;
    element_count = value.element_count;
    numbers.clear();
    all_numbers = value.elements != nullptr;
    for (std::uint64_t index = 0U; index < value.element_count && value.elements != nullptr; ++index) {
        const gf_value& element = value.elements[index];
        all_numbers = all_numbers && element.kind == GF_VALUE_KIND_NUMBER;
        numbers.push_back(element.number);
    }
}

void GF_CALL array_invoke(void* context, gf_event* event) {
    ArrayProbe& probe = *static_cast<ArrayProbe*>(context);
    probe.invoked = probe.invoked + 1;
    probe.names.clear();
    probe.kinds.clear();
    for (std::uint64_t index = 0U; index < (*event).field_count; ++index) {
        probe.names.emplace_back(view_text((*event).fields[index].name));
        probe.kinds.push_back((*event).fields[index].kind);
    }
    probe.payload_null = (*event).payload == nullptr;
    std::uint64_t index = 0U;
    const gf_event_field* array = field_named(*event, probe.array_name, index);
    if (array != nullptr) {
        read_numbers(*array, probe.array_count, probe.array_element_count, probe.array_numbers,
                     probe.elements_numbers);
        gf_value element = blank_value(GF_VALUE_KIND_NULL);
        probe.element_status = (*probe.tables).events.field_element(event, index, 1U, &element);
        probe.element_read = element.number;
        probe.element_past_end =
            (*probe.tables).events.field_element(event, index, (*(*array).value).element_count, &element);
        // Not a mutable payload: the array is not writable.
        gf_value replacement = blank_value(GF_VALUE_KIND_COLLECTION);
        probe.write_refused = (*probe.tables).events.write_field(event, index, &replacement);
    }
    const gf_event_field* surface = field_named(*event, "surface_parameter", index);
    if (surface != nullptr) {
        std::uint64_t element_count = 0U;
        bool all_numbers = false;
        read_numbers(*surface, probe.second_count, element_count, probe.second_numbers, all_numbers);
        probe.elements_numbers = probe.elements_numbers && all_numbers;
    }
    const gf_event_field* kind = field_named(*event, "kind", index);
    if (kind != nullptr) {
        probe.kind_name = view_text((*(*kind).value).text);
    }
    const gf_event_field* item = field_named(*event, "item", index);
    if (item != nullptr) {
        probe.item = (*(*item).value).unsigned_integer;
    }
    const gf_event_field* projection = field_named(*event, "projection", index);
    if (projection != nullptr) {
        probe.projection_name = view_text((*(*projection).value).text);
    }
}

gf_subscription subscribe_array(const Tables& tables, gf_object object, std::string_view name, ArrayProbe& probe) {
    gf_callback callback{};
    callback.struct_size = sizeof(gf_callback);
    callback.invoke = &array_invoke;
    callback.context = &probe;
    gf_subscription subscription = 0U;
    require_ok(tables.events.subscribe(object, text_view(name), &callback, &subscription), "subscribe a plot event");
    return subscription;
}

void plot_camera(const Tables& tables) {
    const gf_object plot = create(tables, "plot3d_view", "fields.plot");
    const std::shared_ptr<Plot3DView> view = control_of<Plot3DView>(plot);
    ArrayProbe probe;
    probe.tables = &tables;
    probe.array_name = "target";
    const gf_subscription subscription = subscribe_array(tables, plot, "camera_changed", probe);
    plots::Plot3DCamera camera = (*view).camera();
    camera.elevation = 12.0;
    camera.azimuth = -40.0;
    camera.target = std::array<double, 3>{0.25, -0.5, 0.125};
    camera.projection = plots::Plot3DProjection::orthographic;
    require((*view).set_camera(camera), "the plot's camera moves");
    const plots::Plot3DCamera& moved = (*view).camera();
    require(probe.invoked == 1, "camera_changed reached the callback once");
    const std::vector<std::string> expected{"elevation", "azimuth", "roll", "zoom", "target", "projection",
                                            "focal_length"};
    require(probe.names == expected, "camera_changed's fields are the camera's members in order");
    require(probe.kinds.size() == 7U && probe.kinds[0] == GF_VALUE_KIND_NUMBER &&
                probe.kinds[4] == GF_VALUE_KIND_COLLECTION && probe.kinds[5] == GF_VALUE_KIND_ENUMERATION &&
                probe.kinds[6] == GF_VALUE_KIND_NUMBER,
            "the camera's fields have the members' kinds, the target a collection");
    require(probe.payload_null, "a struct payload is not a single payload value");
    require(probe.array_count == 3U && probe.array_element_count == 3U && probe.elements_numbers,
            "the target lends its three NUMBER elements in place, its count in both places");
    const std::vector<double> target{moved.target[0], moved.target[1], moved.target[2]};
    require(probe.array_numbers == target, "the lent elements are the camera's target in order");
    require(probe.element_status == GF_STATUS_OK && probe.element_read == moved.target[1],
            "field_element reads the same elements");
    require(probe.element_past_end == GF_STATUS_NOT_FOUND, "an element past the array's count is not found");
    require(probe.write_refused == GF_STATUS_INVALID_ARGUMENT, "an array of an immutable payload is not writable");
    require(probe.projection_name == "orthographic", "the camera's projection is named");
    require_ok(tables.events.disconnect(subscription), "disconnect camera_changed");

    ArrayProbe hovered;
    hovered.tables = &tables;
    hovered.array_name = "data_point";
    ArrayProbe picked;
    picked.tables = &tables;
    picked.array_name = "data_point";
    const gf_subscription hovered_subscription = subscribe_array(tables, plot, "hovered", hovered);
    const gf_subscription picked_subscription = subscribe_array(tables, plot, "picked", picked);
    plots::Plot3DHit hit{};
    hit.kind = plots::Plot3DHitKind::scatter;
    hit.item = plots::Plot3DItemId{7U};
    hit.index = 3U;
    hit.data_point = std::array<double, 3>{1.5, -2.0, 0.5};
    hit.surface_parameter = std::array<double, 2>{0.25, 0.75};
    hit.depth = 4.0;
    (*view).hovered().emit(hit);
    (*view).picked().emit(hit);
    const std::vector<std::string> hit_fields{"kind", "item", "index", "data_point", "surface_parameter", "depth"};
    const std::vector<double> data_point{1.5, -2.0, 0.5};
    const std::vector<double> surface_parameter{0.25, 0.75};
    const std::array<ArrayProbe*, 2> probes{&hovered, &picked};
    for (ArrayProbe* seen : probes) {
        require((*seen).invoked == 1, "a hit event reached its callback once");
        require((*seen).names == hit_fields, "a hit's fields are its members in order");
        require((*seen).kinds.size() == 6U && (*seen).kinds[3] == GF_VALUE_KIND_COLLECTION &&
                    (*seen).kinds[4] == GF_VALUE_KIND_COLLECTION,
                "a hit's arrays are collections");
        require((*seen).array_count == 3U && (*seen).array_numbers == data_point && (*seen).elements_numbers,
                "a hit's data point lends its three numbers");
        require((*seen).second_count == 2U && (*seen).second_numbers == surface_parameter,
                "a hit's surface parameter lends its two numbers");
        require((*seen).kind_name == "scatter" && (*seen).item == 7U, "a hit's kind and item are fields");
    }
    require_ok(tables.events.disconnect(hovered_subscription), "disconnect hovered");
    require_ok(tables.events.disconnect(picked_subscription), "disconnect picked");
    require_ok(tables.objects.release(plot), "release the plot");
}

// A writable fixed-size array (a mutable argument's), on EventFields itself:
// a write replaces the whole array after every element is checked, the lent
// records and field_element show it, and read_field returns it to the C++
// array. No payload declares one yet, so the fields are built directly.
gf_value number_value(double number) {
    gf_value value = blank_value(GF_VALUE_KIND_NUMBER);
    value.number = number;
    return value;
}

// A caller's record from a later minor: larger than gf_value, so its
// array is stepped by its own struct_size.
struct LargerValue final {
    gf_value value{};
    std::uint64_t later = 0U;
};

void writable_arrays() {
    const abi1::SlotIdentity slot{GF_TABLE_EVENTS, 0U};
    abi1::EventFields fields;
    require(fields.claim(), "fresh fields are claimed");
    fields.set_writable(true);
    std::array<double, 3> point{1.0, 2.0, 3.0};
    std::array<std::int8_t, 2> pair{1, 2};
    abi1::append_field(fields, "point", point);
    abi1::append_field(fields, "pair", pair);
    gf_event event{};
    fields.publish(event, false);
    require(event.field_count == 2U && event.fields[0].kind == GF_VALUE_KIND_COLLECTION &&
                (event.fields[0].flags & GF_EVENT_FIELD_WRITABLE) != 0U,
            "a mutable argument's array is a writable collection");
    const gf_value& lent = *event.fields[0].value;
    require(lent.element_count == 3U && lent.elements != nullptr && lent.elements[2].number == 3.0 &&
                lent.elements[1].kind == GF_VALUE_KIND_NUMBER,
            "the array's elements are lent in place");
    require((*event.fields[1].value).elements[0].kind == GF_VALUE_KIND_SIGNED,
            "an array of a signed integer lends SIGNED elements");

    std::array<gf_value, 2> two{number_value(4.0), number_value(5.0)};
    gf_value wrong_count = blank_value(GF_VALUE_KIND_COLLECTION);
    wrong_count.elements = two.data();
    wrong_count.element_count = two.size();
    require(fields.write(0U, &wrong_count, slot) == GF_STATUS_INVALID_ARGUMENT,
            "an array is written with exactly its count");
    std::array<gf_value, 3> mixed{number_value(4.0), blank_value(GF_VALUE_KIND_SIGNED), number_value(6.0)};
    gf_value wrong_kind = blank_value(GF_VALUE_KIND_COLLECTION);
    wrong_kind.elements = mixed.data();
    wrong_kind.element_count = mixed.size();
    require(fields.write(0U, &wrong_kind, slot) == GF_STATUS_INVALID_ARGUMENT,
            "every element is of the array's element kind");
    std::array<gf_value, 3> unbounded{number_value(4.0), number_value(5.0),
                                      number_value(std::numeric_limits<double>::infinity())};
    gf_value not_finite = blank_value(GF_VALUE_KIND_COLLECTION);
    not_finite.elements = unbounded.data();
    not_finite.element_count = unbounded.size();
    require(fields.write(0U, &not_finite, slot) == GF_STATUS_INVALID_ARGUMENT, "every element is checked");
    require(lent.elements[0].number == 1.0, "a refused write leaves the array as it was");
    gf_value large = blank_value(GF_VALUE_KIND_SIGNED);
    large.signed_integer = 300;
    std::array<gf_value, 2> out_of_range{blank_value(GF_VALUE_KIND_SIGNED), large};
    gf_value too_large = blank_value(GF_VALUE_KIND_COLLECTION);
    too_large.elements = out_of_range.data();
    too_large.element_count = out_of_range.size();
    require(fields.write(1U, &too_large, slot) == GF_STATUS_INVALID_ARGUMENT,
            "an element is admitted by the element type's range");

    std::array<LargerValue, 3> later{};
    const std::array<double, 3> numbers{4.0, 5.0, 6.0};
    for (std::size_t index = 0U; index < later.size(); ++index) {
        later[index].value = number_value(numbers[index]);
        later[index].value.struct_size = sizeof(LargerValue);
        later[index].later = 0xFFFFFFFFFFFFFFFFULL;
    }
    gf_value replacement = blank_value(GF_VALUE_KIND_COLLECTION);
    replacement.elements = &later[0].value;
    replacement.element_count = later.size();
    require(fields.write(0U, &replacement, slot) == GF_STATUS_OK,
            "a whole array of the element kind is written, stepped by its records' size");
    const gf_value& rewritten = *event.fields[0].value;
    require(rewritten.element_count == 3U && rewritten.elements[1].number == 5.0 &&
                rewritten.elements[1].struct_size == sizeof(gf_value),
            "the lent records show the written elements, kept at the library's size");
    gf_value element = blank_value(GF_VALUE_KIND_NULL);
    require(fields.element(0U, 2U, &element, slot) == GF_STATUS_OK && element.number == 6.0,
            "field_element reads the written elements");
    // The records the field lent, passed back unchanged.
    gf_value same = blank_value(GF_VALUE_KIND_COLLECTION);
    same.elements = rewritten.elements;
    same.element_count = rewritten.element_count;
    require(fields.write(0U, &same, slot) == GF_STATUS_OK && rewritten.elements[0].number == 4.0,
            "the lent records may be written back as they are");

    fields.end_publication();
    abi1::read_field(fields, "point", point);
    abi1::read_field(fields, "pair", pair);
    require(point[0] == 4.0 && point[1] == 5.0 && point[2] == 6.0, "the written array reaches the C++ array");
    require(pair[0] == 1 && pair[1] == 2, "an array the callback did not write keeps its values");
    fields.release();
}

// The key events (since 1.1): a control's key_down and key_press reach an
// ABI callback with their fields, and handled written there ends the key's
// route; the window's raw key_state_changed is not writable.
struct KeyProbe final {
    const Tables* tables = nullptr;
    int invoked = 0;
    std::uint64_t physical_key = 0U;
    std::uint64_t key_code = 0U;
    bool repeat = false;
    std::string text;
    std::uint64_t key_char = 0U;
    std::string modifiers;
    gf_status handled_written = GF_STATUS_INTERNAL;
};

void GF_CALL key_invoke(void* context, gf_event* event) {
    KeyProbe& probe = *static_cast<KeyProbe*>(context);
    probe.invoked = probe.invoked + 1;
    std::uint64_t index = 0U;
    const gf_event_field* physical = field_named(*event, "physical_key", index);
    if (physical != nullptr) probe.physical_key = (*(*physical).value).unsigned_integer;
    const gf_event_field* code = field_named(*event, "key_code", index);
    if (code != nullptr) probe.key_code = (*(*code).value).unsigned_integer;
    const gf_event_field* repeat = field_named(*event, "repeat", index);
    if (repeat != nullptr) probe.repeat = (*(*repeat).value).boolean == 1U;
    const gf_event_field* modifiers = field_named(*event, "modifiers", index);
    if (modifiers != nullptr) probe.modifiers = std::string(view_text((*(*modifiers).value).text));
    const gf_event_field* text = field_named(*event, "text", index);
    if (text != nullptr) probe.text = std::string(view_text((*(*text).value).text));
    const gf_event_field* key_char = field_named(*event, "key_char", index);
    if (key_char != nullptr) probe.key_char = (*(*key_char).value).unsigned_integer;
    const gf_event_field* handled = field_named(*event, "handled", index);
    if (handled != nullptr) {
        gf_value yes = blank_value(GF_VALUE_KIND_BOOLEAN);
        yes.boolean = 1U;
        probe.handled_written = (*probe.tables).events.write_field(event, index, &yes);
    }
}

void key_events(const Tables& tables) {
    const gf_object root_object = create(tables, "panel", "fields.keys");
    const gf_object button_object = create(tables, "button", "fields.keys.button");
    const std::shared_ptr<Panel> root = control_of<Panel>(root_object);
    const std::shared_ptr<Button> button = control_of<Button>(button_object);
    (*button).set_requested_bounds({0.0, 0.0, 60.0, 20.0});
    (*root).add_child(button);
    Window window(root, Size{120.0, 60.0});
    window.perform_layout();
    (*button).focusable = true;
    require(window.request_focus(button), "the button takes the focus");

    KeyProbe down;
    down.tables = &tables;
    gf_callback down_callback{};
    down_callback.struct_size = sizeof(gf_callback);
    down_callback.invoke = &key_invoke;
    down_callback.context = &down;
    gf_subscription down_subscription = 0U;
    require_ok(tables.events.subscribe(button_object, text_view("key_down"), &down_callback, &down_subscription),
               "subscribe key_down");
    KeyEvent pressed;
    pressed.action = KeyAction::down;
    pressed.physical_key = PhysicalKey::a;
    pressed.modifiers = Modifier::shift;
    pressed.repeat = true;
    require(window.dispatch_key(pressed), "handled written by the ABI callback ends the key's route");
    require(down.invoked == 1 && down.physical_key == PhysicalKey::a && down.key_code == 0x41U && down.repeat &&
                down.modifiers == "shift" && down.handled_written == GF_STATUS_OK,
            "key_down carries physical_key, key_code, repeat and modifiers, and handled is writable");

    KeyProbe press;
    press.tables = &tables;
    gf_callback press_callback = down_callback;
    press_callback.context = &press;
    gf_subscription press_subscription = 0U;
    require_ok(tables.events.subscribe(button_object, text_view("key_press"), &press_callback, &press_subscription),
               "subscribe key_press");
    // U+00E9 (e with acute accent), written as its UTF-8 bytes.
    TextInputEvent typed;
    typed.text_utf8 = "\xC3\xA9";
    require(window.dispatch_text(typed), "a handled key_press keeps the text from the control");
    require(press.invoked == 1 && press.text == "\xC3\xA9" && press.key_char == 0xE9U &&
                press.handled_written == GF_STATUS_OK,
            "key_press carries the text and its character");

    require_ok(tables.events.disconnect(down_subscription), "disconnect key_down");
    require_ok(tables.events.disconnect(press_subscription), "disconnect key_press");
    require_ok(tables.objects.release(button_object), "release the key button");
    require_ok(tables.objects.release(root_object), "release the key root");
}

// A callback that reads nothing, so an emission's count is the forwarder's.
void GF_CALL quiet_invoke(void* context, gf_event* event) {
    int& invoked = *static_cast<int*>(context);
    invoked = invoked + 1;
    static_cast<void>(event);
}

// The allocations of rounds key_down dispatches to a button with
// subscribers ABI subscriptions, per dispatch.
double allocations_per_key_down(const Tables& tables, int subscribers) {
    const gf_object root_object = create(tables, "panel", "fields.measure");
    const gf_object button_object = create(tables, "button", "fields.measure.button");
    const std::shared_ptr<Panel> root = control_of<Panel>(root_object);
    const std::shared_ptr<Button> button = control_of<Button>(button_object);
    (*button).set_requested_bounds({0.0, 0.0, 60.0, 20.0});
    (*root).add_child(button);
    Window window(root, Size{120.0, 60.0});
    window.perform_layout();
    (*button).focusable = true;
    require(window.request_focus(button), "the measured button takes the focus");
    int invoked = 0;
    gf_callback callback{};
    callback.struct_size = sizeof(gf_callback);
    callback.invoke = &quiet_invoke;
    callback.context = &invoked;
    std::vector<gf_subscription> subscriptions(static_cast<std::size_t>(subscribers), 0U);
    for (gf_subscription& subscription : subscriptions) {
        require_ok(tables.events.subscribe(button_object, text_view("key_down"), &callback, &subscription),
                   "subscribe the measured key_down");
    }
    KeyEvent pressed;
    pressed.action = KeyAction::down;
    pressed.physical_key = PhysicalKey::a;
    // One dispatch first, so lazily built tables are not counted.
    static_cast<void>(window.dispatch_key(pressed));
    constexpr int rounds = 64;
    const std::uint64_t before = event_fields_allocation_probe::allocations;
    for (int round = 0; round < rounds; ++round) {
        static_cast<void>(window.dispatch_key(pressed));
    }
    const std::uint64_t after = event_fields_allocation_probe::allocations;
    require(invoked == subscribers * (rounds + 1), "every measured dispatch reaches every subscriber");
    for (const gf_subscription subscription : subscriptions) {
        require_ok(tables.events.disconnect(subscription), "disconnect the measured key_down");
    }
    require_ok(tables.objects.release(button_object), "release the measured button");
    require_ok(tables.objects.release(root_object), "release the measured root");
    const double per_dispatch = static_cast<double>(after - before) / static_cast<double>(rounds);
    return per_dispatch;
}

// N12: what one emission through the ABI forwarder allocates, beside the
// route's own cost (no subscriber). Each subscription reuses its fields, so
// after its first emission a subscriber adds no allocation (before the
// reuse: +11 per subscriber per key_down).
void emission_allocations(const Tables& tables) {
    if (!event_fields_allocation_probe::counted) {
        std::cout << "N12 key_down allocations: not counted under AddressSanitizer on Windows\n";
        return;
    }
    const double none = allocations_per_key_down(tables, 0);
    const double one = allocations_per_key_down(tables, 1);
    const double four = allocations_per_key_down(tables, 4);
    std::cout << "N12 key_down allocations per dispatch: route " << none << ", 1 subscriber +" << (one - none)
              << ", 4 subscribers +" << (four - none) << '\n';
    require(one == none && four == none, "an ABI subscriber adds no allocation per emission once warmed");
}

// The status a core failure reaches the C ABI as (N6): the core's own
// failure types and its limit report name the header's statuses rather
// than INVALID_STATE, and the ABI's text check is the text module's strict
// validator (N7).
gf_status status_of_thrown(int which) {
    const abi1::SlotIdentity slot{GF_TABLE_OBJECTS, 0U};
    try {
        if (which == 0) throw WrongThreadError("wrong thread");
        if (which == 1) throw DisposedError("disposed");
        if (which == 2) throw std::length_error("limit");
        throw std::logic_error("state");
    } catch (...) {
        const gf_status status = abi1::fail_with_current_exception(slot);
        return status;
    }
}

// A slot body that throws as status_of_thrown does, or returns its own
// failure (which 4), for the slot gate (abi1::call_slot).
gf_status throwing_slot_body(int which) {
    if (which == 0) throw WrongThreadError("wrong thread");
    if (which == 1) throw DisposedError("disposed");
    if (which == 2) throw std::length_error("limit");
    if (which == 3) throw std::logic_error("state");
    if (which == 4) return abi1::fail(GF_STATUS_NOT_FOUND, abi1::SlotIdentity{GF_TABLE_OBJECTS, 1U}, "its own");
    // Longer than an STXlib error record's 200 bytes.
    throw std::invalid_argument(std::string(300U, 'x'));
}

gf_status status_through_gate(int which) {
    const abi1::SlotIdentity slot{GF_TABLE_OBJECTS, 1U};
    const gf_status status = abi1::call_slot(slot, throwing_slot_body, which);
    return status;
}

void failure_statuses() {
    require(status_through_gate(0) == GF_STATUS_WRONG_THREAD, "the slot gate maps a wrong-thread failure");
    require(status_through_gate(1) == GF_STATUS_DISPOSED, "the slot gate maps a disposed failure");
    require(status_through_gate(2) == GF_STATUS_LIMIT_EXCEEDED, "the slot gate maps a limit");
    require(status_through_gate(3) == GF_STATUS_INVALID_STATE, "the slot gate maps another logic error");
    require(status_through_gate(4) == GF_STATUS_NOT_FOUND, "a body's own failure passes through the slot gate");
    require(status_through_gate(5) == GF_STATUS_INVALID_ARGUMENT && abi1::thread_failure().message_size == 300U,
            "the slot gate keeps a message longer than an error record holds");
    require(abi1::thread_failure().slot.table_id == GF_TABLE_OBJECTS && abi1::thread_failure().slot.slot_index == 1U,
            "the slot gate's failure names its slot");
    require(status_of_thrown(0) == GF_STATUS_WRONG_THREAD, "a core wrong-thread failure is GF_STATUS_WRONG_THREAD");
    require(status_of_thrown(1) == GF_STATUS_DISPOSED, "a core disposed failure is GF_STATUS_DISPOSED");
    require(status_of_thrown(2) == GF_STATUS_LIMIT_EXCEEDED, "a core limit is GF_STATUS_LIMIT_EXCEEDED");
    require(status_of_thrown(3) == GF_STATUS_INVALID_STATE, "another logic error is GF_STATUS_INVALID_STATE");
    const abi1::SlotIdentity slot{GF_TABLE_OBJECTS, 0U};
    std::string_view read;
    const std::string_view well_formed = "caf\xC3\xA9 \xF0\x9F\x98\x80";
    require(abi1::read_text(text_view(well_formed), slot, read) == GF_STATUS_OK && read == well_formed,
            "read_text admits well-formed UTF-8");
    const std::string_view overlong = "\xC0\xAF";
    const std::string_view surrogate = "\xED\xA0\x80";
    const std::string_view truncated = "\xE2\x82";
    require(abi1::read_text(text_view(overlong), slot, read) == GF_STATUS_INVALID_ARGUMENT,
            "read_text refuses an overlong encoding");
    require(abi1::read_text(text_view(surrogate), slot, read) == GF_STATUS_INVALID_ARGUMENT,
            "read_text refuses an encoded surrogate");
    require(abi1::read_text(text_view(truncated), slot, read) == GF_STATUS_INVALID_ARGUMENT,
            "read_text refuses a truncated sequence");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "kept_record") {
        return kept_record_mode();
    }
    try {
        const Tables tables = load_tables();
        cancellable_selection(tables);
        collections(tables);
        optional_text(tables);
        two_arguments(tables);
        plot_camera(tables);
        writable_arrays();
        key_events(tables);
        emission_allocations(tables);
        failure_statuses();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " event field checks failed\n";
        return 1;
    }
    std::cout << "event fields: all checks passed\n";
    return 0;
}
