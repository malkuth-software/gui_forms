// Generated from manifests/abi/gf_abi.json by `python3 tools/abi_bindings.py cpp`. Do not edit.
//
// Header-only C++ wrappers of the GUI.Forms C ABI (gf_abi.h). Each table is a
// class made by negotiation; each method calls exactly one slot, with the
// slot's own signature, and answers GF_STATUS_UNSUPPORTED_VERSION for a slot
// the library did not serve. The wrappers add no second contract: statuses,
// ownership and threading are gf_abi.h's.
#pragma once

#include "gui_forms/c/gf_abi.h"

namespace gui_forms::abi {

// gf_runtime_table, negotiated as GF_TABLE_RUNTIME.
class RuntimeTable {
public:
    RuntimeTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(RuntimeTable& output) noexcept {
        gf_runtime_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_RUNTIME, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = RuntimeTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static RuntimeTable adopt(const gf_runtime_table& adopted_table) noexcept {
        RuntimeTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_runtime_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status version(gf_abi_version_info* info) const noexcept {
        if (table_.struct_size < 16u || table_.version == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.version(info);
        return called;
    }

    [[nodiscard]] gf_status error_detail(gf_error_detail* detail, char* message_buffer, uint64_t message_capacity,
                                         uint64_t* message_required) const noexcept {
        if (table_.struct_size < 24u || table_.error_detail == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.error_detail(detail, message_buffer, message_capacity, message_required);
        return called;
    }

    [[nodiscard]] gf_status clear_error() const noexcept {
        if (table_.struct_size < 32u || table_.clear_error == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.clear_error();
        return called;
    }

    [[nodiscard]] gf_status splashscreen_show(const gf_splashscreen_options* options,
                                              gf_splashscreen* splashscreen) const noexcept {
        if (table_.struct_size < 40u || table_.splashscreen_show == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.splashscreen_show(options, splashscreen);
        return called;
    }

    [[nodiscard]] gf_status splashscreen_set_status(gf_splashscreen splashscreen, gf_utf8_view status) const noexcept {
        if (table_.struct_size < 48u || table_.splashscreen_set_status == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.splashscreen_set_status(splashscreen, status);
        return called;
    }

    [[nodiscard]] gf_status splashscreen_set_progress(gf_splashscreen splashscreen, uint32_t show_progress,
                                                      double progress) const noexcept {
        if (table_.struct_size < 56u || table_.splashscreen_set_progress == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.splashscreen_set_progress(splashscreen, show_progress, progress);
        return called;
    }

    [[nodiscard]] gf_status splashscreen_close(gf_splashscreen splashscreen) const noexcept {
        if (table_.struct_size < 64u || table_.splashscreen_close == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.splashscreen_close(splashscreen);
        return called;
    }

private:
    gf_runtime_table table_;
};

// gf_objects_table, negotiated as GF_TABLE_OBJECTS.
class ObjectsTable {
public:
    ObjectsTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(ObjectsTable& output) noexcept {
        gf_objects_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_OBJECTS, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = ObjectsTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static ObjectsTable adopt(const gf_objects_table& adopted_table) noexcept {
        ObjectsTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_objects_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status create(gf_utf8_view type_name, gf_utf8_view stable_id, gf_object* object) const noexcept {
        if (table_.struct_size < 16u || table_.create == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.create(type_name, stable_id, object);
        return called;
    }

    [[nodiscard]] gf_status retain(gf_object object) const noexcept {
        if (table_.struct_size < 24u || table_.retain == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.retain(object);
        return called;
    }

    [[nodiscard]] gf_status release(gf_object object) const noexcept {
        if (table_.struct_size < 32u || table_.release == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.release(object);
        return called;
    }

    [[nodiscard]] gf_status dispose(gf_object object) const noexcept {
        if (table_.struct_size < 40u || table_.dispose == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.dispose(object);
        return called;
    }

    [[nodiscard]] gf_status state(gf_object object, gf_object_state* state) const noexcept {
        if (table_.struct_size < 48u || table_.state == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.state(object, state);
        return called;
    }

    [[nodiscard]] gf_status type_id(gf_object object, char* buffer, uint64_t capacity,
                                    uint64_t* required) const noexcept {
        if (table_.struct_size < 56u || table_.type_id == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.type_id(object, buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status stable_id(gf_object object, char* buffer, uint64_t capacity,
                                      uint64_t* required) const noexcept {
        if (table_.struct_size < 64u || table_.stable_id == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.stable_id(object, buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status insert_child(gf_object parent, gf_object child, uint64_t index) const noexcept {
        if (table_.struct_size < 72u || table_.insert_child == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.insert_child(parent, child, index);
        return called;
    }

    [[nodiscard]] gf_status remove_child(gf_object parent, gf_object child) const noexcept {
        if (table_.struct_size < 80u || table_.remove_child == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.remove_child(parent, child);
        return called;
    }

    [[nodiscard]] gf_status child_count(gf_object parent, uint64_t* count) const noexcept {
        if (table_.struct_size < 88u || table_.child_count == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.child_count(parent, count);
        return called;
    }

    [[nodiscard]] gf_status child_at(gf_object parent, uint64_t index, gf_object* child) const noexcept {
        if (table_.struct_size < 96u || table_.child_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.child_at(parent, index, child);
        return called;
    }

    [[nodiscard]] gf_status parent(gf_object object, gf_object* parent) const noexcept {
        if (table_.struct_size < 104u || table_.parent == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.parent(object, parent);
        return called;
    }

    [[nodiscard]] gf_status create_with(gf_utf8_view type_name, gf_utf8_view stable_id,
                                        const gf_create_arguments* arguments, gf_object* object) const noexcept {
        if (table_.struct_size < 112u || table_.create_with == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.create_with(type_name, stable_id, arguments, object);
        return called;
    }

private:
    gf_objects_table table_;
};

// gf_properties_table, negotiated as GF_TABLE_PROPERTIES.
class PropertiesTable {
public:
    PropertiesTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(PropertiesTable& output) noexcept {
        gf_properties_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_PROPERTIES, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = PropertiesTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static PropertiesTable adopt(const gf_properties_table& adopted_table) noexcept {
        PropertiesTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_properties_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status get(gf_object object, gf_utf8_view name, gf_value* value, char* text_buffer,
                                uint64_t text_capacity, uint64_t* text_required) const noexcept {
        if (table_.struct_size < 16u || table_.get == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.get(object, name, value, text_buffer, text_capacity, text_required);
        return called;
    }

    [[nodiscard]] gf_status set(gf_object object, gf_utf8_view name, const gf_value* value) const noexcept {
        if (table_.struct_size < 24u || table_.set == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set(object, name, value);
        return called;
    }

    [[nodiscard]] gf_status reset(gf_object object, gf_utf8_view name) const noexcept {
        if (table_.struct_size < 32u || table_.reset == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.reset(object, name);
        return called;
    }

    [[nodiscard]] gf_status element_at(gf_object object, gf_utf8_view name, uint64_t index, gf_value* value,
                                       char* text_buffer, uint64_t text_capacity,
                                       uint64_t* text_required) const noexcept {
        if (table_.struct_size < 40u || table_.element_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.element_at(object, name, index, value, text_buffer, text_capacity,
                                                   text_required);
        return called;
    }

private:
    gf_properties_table table_;
};

// gf_events_table, negotiated as GF_TABLE_EVENTS.
class EventsTable {
public:
    EventsTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(EventsTable& output) noexcept {
        gf_events_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_EVENTS, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = EventsTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static EventsTable adopt(const gf_events_table& adopted_table) noexcept {
        EventsTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_events_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status subscribe(gf_object source, gf_utf8_view interaction, const gf_callback* callback,
                                      gf_subscription* subscription) const noexcept {
        if (table_.struct_size < 16u || table_.subscribe == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.subscribe(source, interaction, callback, subscription);
        return called;
    }

    [[nodiscard]] gf_status subscribe_property(gf_object source, gf_utf8_view property, const gf_callback* callback,
                                               gf_subscription* subscription) const noexcept {
        if (table_.struct_size < 24u || table_.subscribe_property == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.subscribe_property(source, property, callback, subscription);
        return called;
    }

    [[nodiscard]] gf_status disconnect(gf_subscription subscription) const noexcept {
        if (table_.struct_size < 32u || table_.disconnect == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.disconnect(subscription);
        return called;
    }

    [[nodiscard]] gf_status write_field(gf_event* event, uint64_t field, const gf_value* value) const noexcept {
        if (table_.struct_size < 40u || table_.write_field == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.write_field(event, field, value);
        return called;
    }

    [[nodiscard]] gf_status field_element(const gf_event* event, uint64_t field, uint64_t index,
                                          gf_value* element) const noexcept {
        if (table_.struct_size < 48u || table_.field_element == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.field_element(event, field, index, element);
        return called;
    }

private:
    gf_events_table table_;
};

// gf_dispatch_table, negotiated as GF_TABLE_DISPATCH.
class DispatchTable {
public:
    DispatchTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(DispatchTable& output) noexcept {
        gf_dispatch_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_DISPATCH, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = DispatchTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static DispatchTable adopt(const gf_dispatch_table& adopted_table) noexcept {
        DispatchTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_dispatch_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status begin_invoke(gf_object target, const gf_callback* callback) const noexcept {
        if (table_.struct_size < 16u || table_.begin_invoke == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.begin_invoke(target, callback);
        return called;
    }

    [[nodiscard]] gf_status run_pending(uint64_t* invoked) const noexcept {
        if (table_.struct_size < 24u || table_.run_pending == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.run_pending(invoked);
        return called;
    }

private:
    gf_dispatch_table table_;
};

// gf_windows_table, negotiated as GF_TABLE_WINDOWS.
class WindowsTable {
public:
    WindowsTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(WindowsTable& output) noexcept {
        gf_windows_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_WINDOWS, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = WindowsTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static WindowsTable adopt(const gf_windows_table& adopted_table) noexcept {
        WindowsTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_windows_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status run(gf_object window_or_root, const gf_run_options* options) const noexcept {
        if (table_.struct_size < 16u || table_.run == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.run(window_or_root, options);
        return called;
    }

    [[nodiscard]] gf_status request_close(gf_object window_or_root) const noexcept {
        if (table_.struct_size < 24u || table_.request_close == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.request_close(window_or_root);
        return called;
    }

    [[nodiscard]] gf_status create(gf_object root, gf_object* window) const noexcept {
        if (table_.struct_size < 32u || table_.create == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.create(root, window);
        return called;
    }

    [[nodiscard]] gf_status interaction_count(uint64_t* count) const noexcept {
        if (table_.struct_size < 40u || table_.interaction_count == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.interaction_count(count);
        return called;
    }

    [[nodiscard]] gf_status interaction_at(uint64_t interaction, gf_schema_interaction* record) const noexcept {
        if (table_.struct_size < 48u || table_.interaction_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.interaction_at(interaction, record);
        return called;
    }

    [[nodiscard]] gf_status request_help(gf_object window, uint32_t* handled) const noexcept {
        if (table_.struct_size < 56u || table_.request_help == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.request_help(window, handled);
        return called;
    }

    [[nodiscard]] gf_status key_state(gf_object window, uint32_t physical_key, uint32_t* down) const noexcept {
        if (table_.struct_size < 64u || table_.key_state == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.key_state(window, physical_key, down);
        return called;
    }

    [[nodiscard]] gf_status show_modal(gf_object root, const gf_owned_window_options* options,
                                       gf_dialog_result_code* result) const noexcept {
        if (table_.struct_size < 72u || table_.show_modal == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.show_modal(root, options, result);
        return called;
    }

    [[nodiscard]] gf_status show_owned(gf_object root, const gf_owned_window_options* options) const noexcept {
        if (table_.struct_size < 80u || table_.show_owned == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.show_owned(root, options);
        return called;
    }

    [[nodiscard]] gf_status window_properties(gf_object window, gf_window_property_flags* supported,
                                              gf_window_property_flags* applied) const noexcept {
        if (table_.struct_size < 88u || table_.window_properties == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.window_properties(window, supported, applied);
        return called;
    }

    [[nodiscard]] gf_status focused_control(gf_object window_or_root, gf_object* control) const noexcept {
        if (table_.struct_size < 96u || table_.focused_control == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.focused_control(window_or_root, control);
        return called;
    }

private:
    gf_windows_table table_;
};

// gf_schema_table, negotiated as GF_TABLE_SCHEMA.
class SchemaTable {
public:
    SchemaTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(SchemaTable& output) noexcept {
        gf_schema_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_SCHEMA, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = SchemaTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static SchemaTable adopt(const gf_schema_table& adopted_table) noexcept {
        SchemaTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_schema_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status vocabulary_count(gf_schema_vocabulary vocabulary, uint64_t* count) const noexcept {
        if (table_.struct_size < 16u || table_.vocabulary_count == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.vocabulary_count(vocabulary, count);
        return called;
    }

    [[nodiscard]] gf_status vocabulary_at(gf_schema_vocabulary vocabulary, uint64_t index,
                                          gf_utf8_view* name) const noexcept {
        if (table_.struct_size < 24u || table_.vocabulary_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.vocabulary_at(vocabulary, index, name);
        return called;
    }

    [[nodiscard]] gf_status type_count(uint64_t* count) const noexcept {
        if (table_.struct_size < 32u || table_.type_count == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.type_count(count);
        return called;
    }

    [[nodiscard]] gf_status find_type(gf_utf8_view name, uint64_t* type) const noexcept {
        if (table_.struct_size < 40u || table_.find_type == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.find_type(name, type);
        return called;
    }

    [[nodiscard]] gf_status type_at(uint64_t type, gf_schema_type* record) const noexcept {
        if (table_.struct_size < 48u || table_.type_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.type_at(type, record);
        return called;
    }

    [[nodiscard]] gf_status item_system_at(uint64_t type, uint64_t system,
                                           gf_schema_item_system* record) const noexcept {
        if (table_.struct_size < 56u || table_.item_system_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.item_system_at(type, system, record);
        return called;
    }

    [[nodiscard]] gf_status part_at(uint64_t type, uint64_t system, uint64_t part,
                                    gf_schema_part* record) const noexcept {
        if (table_.struct_size < 64u || table_.part_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.part_at(type, system, part, record);
        return called;
    }

    [[nodiscard]] gf_status state_at(uint64_t type, uint64_t system, uint64_t part, uint64_t state,
                                     gf_utf8_view* name) const noexcept {
        if (table_.struct_size < 72u || table_.state_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.state_at(type, system, part, state, name);
        return called;
    }

    [[nodiscard]] gf_status custom_state_at(uint64_t type, uint64_t system, uint64_t state,
                                            gf_utf8_view* name) const noexcept {
        if (table_.struct_size < 80u || table_.custom_state_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.custom_state_at(type, system, state, name);
        return called;
    }

    [[nodiscard]] gf_status property_at(uint64_t type, uint64_t property, gf_schema_property* record) const noexcept {
        if (table_.struct_size < 88u || table_.property_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.property_at(type, property, record);
        return called;
    }

    [[nodiscard]] gf_status choice_at(uint64_t type, uint64_t property, uint64_t choice,
                                      gf_schema_choice* record) const noexcept {
        if (table_.struct_size < 96u || table_.choice_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.choice_at(type, property, choice, record);
        return called;
    }

    [[nodiscard]] gf_status interaction_at(uint64_t type, uint64_t interaction,
                                           gf_schema_interaction* record) const noexcept {
        if (table_.struct_size < 104u || table_.interaction_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.interaction_at(type, interaction, record);
        return called;
    }

    [[nodiscard]] gf_status engine(gf_schema_engine* record) const noexcept {
        if (table_.struct_size < 112u || table_.engine == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.engine(record);
        return called;
    }

    [[nodiscard]] gf_status primitive_at(uint64_t primitive, gf_schema_primitive* record) const noexcept {
        if (table_.struct_size < 120u || table_.primitive_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.primitive_at(primitive, record);
        return called;
    }

    [[nodiscard]] gf_status primitive_name_at(uint64_t primitive, gf_schema_primitive_list list, uint64_t index,
                                              gf_utf8_view* name) const noexcept {
        if (table_.struct_size < 128u || table_.primitive_name_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.primitive_name_at(primitive, list, index, name);
        return called;
    }

    [[nodiscard]] gf_status parameter_at(uint64_t primitive, uint64_t parameter,
                                         gf_schema_parameter* record) const noexcept {
        if (table_.struct_size < 136u || table_.parameter_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.parameter_at(primitive, parameter, record);
        return called;
    }

    [[nodiscard]] gf_status parameter_choice_at(uint64_t primitive, uint64_t parameter, uint64_t choice,
                                                gf_utf8_view* name) const noexcept {
        if (table_.struct_size < 144u || table_.parameter_choice_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.parameter_choice_at(primitive, parameter, choice, name);
        return called;
    }

    [[nodiscard]] gf_status effect_count(uint64_t* count) const noexcept {
        if (table_.struct_size < 152u || table_.effect_count == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.effect_count(count);
        return called;
    }

    [[nodiscard]] gf_status effect_at(uint64_t effect, gf_schema_effect* record) const noexcept {
        if (table_.struct_size < 160u || table_.effect_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.effect_at(effect, record);
        return called;
    }

    [[nodiscard]] gf_status child_property_count(uint64_t type, uint64_t* count) const noexcept {
        if (table_.struct_size < 168u || table_.child_property_count == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.child_property_count(type, count);
        return called;
    }

    [[nodiscard]] gf_status child_property_at(uint64_t type, uint64_t property,
                                              gf_schema_property* record) const noexcept {
        if (table_.struct_size < 176u || table_.child_property_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.child_property_at(type, property, record);
        return called;
    }

    [[nodiscard]] gf_status child_choice_at(uint64_t type, uint64_t property, uint64_t choice,
                                            gf_schema_choice* record) const noexcept {
        if (table_.struct_size < 184u || table_.child_choice_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.child_choice_at(type, property, choice, record);
        return called;
    }

private:
    gf_schema_table table_;
};

// gf_themes_table, negotiated as GF_TABLE_THEMES.
class ThemesTable {
public:
    ThemesTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(ThemesTable& output) noexcept {
        gf_themes_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_THEMES, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = ThemesTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static ThemesTable adopt(const gf_themes_table& adopted_table) noexcept {
        ThemesTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_themes_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status load(const gf_theme_package* package, gf_theme_report* report) const noexcept {
        if (table_.struct_size < 16u || table_.load == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.load(package, report);
        return called;
    }

    [[nodiscard]] gf_status reset() const noexcept {
        if (table_.struct_size < 24u || table_.reset == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.reset();
        return called;
    }

    [[nodiscard]] gf_status install(const gf_theme_package* package, gf_theme_report* report) const noexcept {
        if (table_.struct_size < 32u || table_.install == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.install(package, report);
        return called;
    }

    [[nodiscard]] gf_status remove(gf_utf8_view name, uint32_t* removed) const noexcept {
        if (table_.struct_size < 40u || table_.remove == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.remove(name, removed);
        return called;
    }

    [[nodiscard]] gf_status diagnostic_at(uint64_t index, gf_theme_diagnostic* record) const noexcept {
        if (table_.struct_size < 48u || table_.diagnostic_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.diagnostic_at(index, record);
        return called;
    }

    [[nodiscard]] gf_status diagnostic_text(uint64_t index, gf_theme_diagnostic_text text, char* buffer,
                                            uint64_t capacity, uint64_t* required) const noexcept {
        if (table_.struct_size < 56u || table_.diagnostic_text == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.diagnostic_text(index, text, buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status theme_id(char* buffer, uint64_t capacity, uint64_t* required) const noexcept {
        if (table_.struct_size < 64u || table_.theme_id == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.theme_id(buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status publish_value(gf_utf8_view name, double value) const noexcept {
        if (table_.struct_size < 72u || table_.publish_value == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.publish_value(name, value);
        return called;
    }

    [[nodiscard]] gf_status withdraw_value(gf_utf8_view name) const noexcept {
        if (table_.struct_size < 80u || table_.withdraw_value == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.withdraw_value(name);
        return called;
    }

    [[nodiscard]] gf_status published_value(gf_utf8_view name, double* value, uint32_t* present) const noexcept {
        if (table_.struct_size < 88u || table_.published_value == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.published_value(name, value, present);
        return called;
    }

    [[nodiscard]] gf_status load_image(const uint8_t* bytes, uint64_t size, uint64_t* image) const noexcept {
        if (table_.struct_size < 96u || table_.load_image == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.load_image(bytes, size, image);
        return called;
    }

    [[nodiscard]] gf_status remove_image(uint64_t image) const noexcept {
        if (table_.struct_size < 104u || table_.remove_image == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.remove_image(image);
        return called;
    }

    [[nodiscard]] gf_status load_pixels(const uint8_t* pixels, uint32_t width, uint32_t height, uint64_t row_bytes,
                                        uint64_t* image) const noexcept {
        if (table_.struct_size < 112u || table_.load_pixels == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.load_pixels(pixels, width, height, row_bytes, image);
        return called;
    }

private:
    gf_themes_table table_;
};

// gf_host_table, negotiated as GF_TABLE_HOST.
class HostTable {
public:
    HostTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(HostTable& output) noexcept {
        gf_host_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_HOST, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = HostTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static HostTable adopt(const gf_host_table& adopted_table) noexcept {
        HostTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_host_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status capabilities(gf_host_capabilities* record) const noexcept {
        if (table_.struct_size < 16u || table_.capabilities == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.capabilities(record);
        return called;
    }

    [[nodiscard]] gf_status platform_name(char* buffer, uint64_t capacity, uint64_t* required) const noexcept {
        if (table_.struct_size < 24u || table_.platform_name == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.platform_name(buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status set_cursor(gf_pointer_cursor cursor) const noexcept {
        if (table_.struct_size < 32u || table_.set_cursor == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_cursor(cursor);
        return called;
    }

    [[nodiscard]] gf_status read_clipboard_text(char* buffer, uint64_t capacity, uint64_t* required) const noexcept {
        if (table_.struct_size < 40u || table_.read_clipboard_text == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.read_clipboard_text(buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status write_clipboard_text(gf_utf8_view text) const noexcept {
        if (table_.struct_size < 48u || table_.write_clipboard_text == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.write_clipboard_text(text);
        return called;
    }

    [[nodiscard]] gf_status show_dialog(const gf_dialog_request* request, gf_dialog_result* result) const noexcept {
        if (table_.struct_size < 56u || table_.show_dialog == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.show_dialog(request, result);
        return called;
    }

    [[nodiscard]] gf_status dialog_path_at(uint64_t index, char* buffer, uint64_t capacity,
                                           uint64_t* required) const noexcept {
        if (table_.struct_size < 64u || table_.dialog_path_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.dialog_path_at(index, buffer, capacity, required);
        return called;
    }

private:
    gf_host_table table_;
};

// gf_diagnostics_table, negotiated as GF_TABLE_DIAGNOSTICS.
class DiagnosticsTable {
public:
    DiagnosticsTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(DiagnosticsTable& output) noexcept {
        gf_diagnostics_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_DIAGNOSTICS, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = DiagnosticsTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static DiagnosticsTable adopt(const gf_diagnostics_table& adopted_table) noexcept {
        DiagnosticsTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_diagnostics_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status shadow_frames(gf_shadow_frame* frames, uint64_t capacity, uint64_t* required,
                                          gf_shadow_stack_info* info) const noexcept {
        if (table_.struct_size < 16u || table_.shadow_frames == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.shadow_frames(frames, capacity, required, info);
        return called;
    }

private:
    gf_diagnostics_table table_;
};

// gf_controls_table, negotiated as GF_TABLE_CONTROLS.
class ControlsTable {
public:
    ControlsTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(ControlsTable& output) noexcept {
        gf_controls_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_CONTROLS, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = ControlsTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static ControlsTable adopt(const gf_controls_table& adopted_table) noexcept {
        ControlsTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_controls_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status suspend_layout(gf_object object) const noexcept {
        if (table_.struct_size < 16u || table_.suspend_layout == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.suspend_layout(object);
        return called;
    }

    [[nodiscard]] gf_status resume_layout(gf_object object, uint32_t perform_layout) const noexcept {
        if (table_.struct_size < 24u || table_.resume_layout == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.resume_layout(object, perform_layout);
        return called;
    }

    [[nodiscard]] gf_status perform_layout(gf_object object) const noexcept {
        if (table_.struct_size < 32u || table_.perform_layout == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.perform_layout(object);
        return called;
    }

    [[nodiscard]] gf_status layout_state(gf_object object, gf_layout_transaction* state) const noexcept {
        if (table_.struct_size < 40u || table_.layout_state == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.layout_state(object, state);
        return called;
    }

    [[nodiscard]] gf_status window_bounds(gf_object object, gf_logical_rect* bounds) const noexcept {
        if (table_.struct_size < 48u || table_.window_bounds == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.window_bounds(object, bounds);
        return called;
    }

    [[nodiscard]] gf_status set_pointer_capture(gf_object object, uint32_t captured) const noexcept {
        if (table_.struct_size < 56u || table_.set_pointer_capture == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_pointer_capture(object, captured);
        return called;
    }

    [[nodiscard]] gf_status pointer_capture(gf_object object, uint32_t* captured) const noexcept {
        if (table_.struct_size < 64u || table_.pointer_capture == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.pointer_capture(object, captured);
        return called;
    }

    [[nodiscard]] gf_status scroll_state(gf_object object, gf_scroll_snapshot* state) const noexcept {
        if (table_.struct_size < 72u || table_.scroll_state == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.scroll_state(object, state);
        return called;
    }

    [[nodiscard]] gf_status set_scroll_axis(gf_object object, gf_scroll_orientation orientation,
                                            const gf_scroll_axis* axis) const noexcept {
        if (table_.struct_size < 80u || table_.set_scroll_axis == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_scroll_axis(object, orientation, axis);
        return called;
    }

    [[nodiscard]] gf_status scroll_into_view(gf_object object, gf_object child) const noexcept {
        if (table_.struct_size < 88u || table_.scroll_into_view == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.scroll_into_view(object, child);
        return called;
    }

    [[nodiscard]] gf_status invalidate(gf_object object, const gf_logical_rect* damage) const noexcept {
        if (table_.struct_size < 96u || table_.invalidate == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.invalidate(object, damage);
        return called;
    }

    [[nodiscard]] gf_status focus(gf_object object, uint32_t* focused) const noexcept {
        if (table_.struct_size < 104u || table_.focus == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.focus(object, focused);
        return called;
    }

    [[nodiscard]] gf_status focus_state(gf_object object, uint32_t* focused, uint32_t* contains_focus) const noexcept {
        if (table_.struct_size < 112u || table_.focus_state == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.focus_state(object, focused, contains_focus);
        return called;
    }

private:
    gf_controls_table table_;
};

// gf_text_editing_table, negotiated as GF_TABLE_TEXT_EDITING.
class TextEditingTable {
public:
    TextEditingTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(TextEditingTable& output) noexcept {
        gf_text_editing_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_TEXT_EDITING, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = TextEditingTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static TextEditingTable adopt(const gf_text_editing_table& adopted_table) noexcept {
        TextEditingTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_text_editing_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status select(gf_object object, uint64_t anchor, uint64_t caret) const noexcept {
        if (table_.struct_size < 16u || table_.select == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.select(object, anchor, caret);
        return called;
    }

    [[nodiscard]] gf_status selection(gf_object object, uint64_t* anchor, uint64_t* caret) const noexcept {
        if (table_.struct_size < 24u || table_.selection == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.selection(object, anchor, caret);
        return called;
    }

    [[nodiscard]] gf_status grapheme_boundary(gf_object object, uint64_t position, int32_t direction,
                                              uint64_t* boundary) const noexcept {
        if (table_.struct_size < 32u || table_.grapheme_boundary == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.grapheme_boundary(object, position, direction, boundary);
        return called;
    }

    [[nodiscard]] gf_status replace_range(gf_object object, uint64_t start, uint64_t length, gf_utf8_view replacement,
                                          uint32_t* changed) const noexcept {
        if (table_.struct_size < 40u || table_.replace_range == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.replace_range(object, start, length, replacement, changed);
        return called;
    }

    [[nodiscard]] gf_status undo(gf_object object, uint32_t* changed) const noexcept {
        if (table_.struct_size < 48u || table_.undo == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.undo(object, changed);
        return called;
    }

    [[nodiscard]] gf_status redo(gf_object object, uint32_t* changed) const noexcept {
        if (table_.struct_size < 56u || table_.redo == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.redo(object, changed);
        return called;
    }

    [[nodiscard]] gf_status clear_undo_history(gf_object object) const noexcept {
        if (table_.struct_size < 64u || table_.clear_undo_history == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.clear_undo_history(object);
        return called;
    }

    [[nodiscard]] gf_status set_decorations(gf_object object, uint32_t layer, const gf_text_decoration* decorations,
                                            uint64_t count) const noexcept {
        if (table_.struct_size < 72u || table_.set_decorations == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_decorations(object, layer, decorations, count);
        return called;
    }

    [[nodiscard]] gf_status replace_decorations(gf_object object, uint32_t layer, uint64_t start, uint64_t end,
                                                const gf_text_decoration* decorations, uint64_t count) const noexcept {
        if (table_.struct_size < 80u || table_.replace_decorations == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.replace_decorations(object, layer, start, end, decorations, count);
        return called;
    }

    [[nodiscard]] gf_status clear_decorations(gf_object object) const noexcept {
        if (table_.struct_size < 88u || table_.clear_decorations == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.clear_decorations(object);
        return called;
    }

    [[nodiscard]] gf_status decoration_count(gf_object object, uint32_t layer, uint64_t* count) const noexcept {
        if (table_.struct_size < 96u || table_.decoration_count == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.decoration_count(object, layer, count);
        return called;
    }

    [[nodiscard]] gf_status decoration_at(gf_object object, uint32_t layer, uint64_t index,
                                          gf_text_decoration* decoration) const noexcept {
        if (table_.struct_size < 104u || table_.decoration_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.decoration_at(object, layer, index, decoration);
        return called;
    }

private:
    gf_text_editing_table table_;
};

// gf_overlays_table, negotiated as GF_TABLE_OVERLAYS.
class OverlaysTable {
public:
    OverlaysTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(OverlaysTable& output) noexcept {
        gf_overlays_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_OVERLAYS, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = OverlaysTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static OverlaysTable adopt(const gf_overlays_table& adopted_table) noexcept {
        OverlaysTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_overlays_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status open_overlay(gf_object owner, gf_object overlay) const noexcept {
        if (table_.struct_size < 16u || table_.open_overlay == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.open_overlay(owner, overlay);
        return called;
    }

    [[nodiscard]] gf_status close_overlay(gf_object overlay) const noexcept {
        if (table_.struct_size < 24u || table_.close_overlay == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.close_overlay(overlay);
        return called;
    }

    [[nodiscard]] gf_status set_tool_tip(gf_object tool_tip, gf_object target, gf_utf8_view text) const noexcept {
        if (table_.struct_size < 32u || table_.set_tool_tip == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_tool_tip(tool_tip, target, text);
        return called;
    }

    [[nodiscard]] gf_status show_tool_tip(gf_object tool_tip, gf_object target, uint64_t duration_ms) const noexcept {
        if (table_.struct_size < 40u || table_.show_tool_tip == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.show_tool_tip(tool_tip, target, duration_ms);
        return called;
    }

    [[nodiscard]] gf_status hide_tool_tip(gf_object tool_tip) const noexcept {
        if (table_.struct_size < 48u || table_.hide_tool_tip == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.hide_tool_tip(tool_tip);
        return called;
    }

    [[nodiscard]] gf_status open_anchored(gf_object anchor, gf_object content,
                                          const gf_anchored_overlay_options* options) const noexcept {
        if (table_.struct_size < 56u || table_.open_anchored == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.open_anchored(anchor, content, options);
        return called;
    }

    [[nodiscard]] gf_status close_anchored(gf_object content) const noexcept {
        if (table_.struct_size < 64u || table_.close_anchored == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.close_anchored(content);
        return called;
    }

private:
    gf_overlays_table table_;
};

// gf_property_grid_table, negotiated as GF_TABLE_PROPERTY_GRID.
class PropertyGridTable {
public:
    PropertyGridTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(PropertyGridTable& output) noexcept {
        gf_property_grid_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_PROPERTY_GRID, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = PropertyGridTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static PropertyGridTable adopt(const gf_property_grid_table& adopted_table) noexcept {
        PropertyGridTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_property_grid_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status select_objects(gf_object grid, const gf_object* objects, uint64_t count) const noexcept {
        if (table_.struct_size < 16u || table_.select_objects == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.select_objects(grid, objects, count);
        return called;
    }

    [[nodiscard]] gf_status refresh(gf_object grid) const noexcept {
        if (table_.struct_size < 24u || table_.refresh == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.refresh(grid);
        return called;
    }

    [[nodiscard]] gf_status try_set_text(gf_object grid, gf_utf8_view name, gf_utf8_view text,
                                         uint32_t* accepted) const noexcept {
        if (table_.struct_size < 32u || table_.try_set_text == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.try_set_text(grid, name, text, accepted);
        return called;
    }

    [[nodiscard]] gf_status reset_property(gf_object grid, gf_utf8_view name, uint32_t* reset) const noexcept {
        if (table_.struct_size < 40u || table_.reset_property == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.reset_property(grid, name, reset);
        return called;
    }

    [[nodiscard]] gf_status activate_editor(gf_object grid, gf_utf8_view name, uint32_t* activated) const noexcept {
        if (table_.struct_size < 48u || table_.activate_editor == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.activate_editor(grid, name, activated);
        return called;
    }

private:
    gf_property_grid_table table_;
};

// gf_custom_controls_table, negotiated as GF_TABLE_CUSTOM_CONTROLS.
class CustomControlsTable {
public:
    CustomControlsTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(CustomControlsTable& output) noexcept {
        gf_custom_controls_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_CUSTOM_CONTROLS, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = CustomControlsTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static CustomControlsTable adopt(const gf_custom_controls_table& adopted_table) noexcept {
        CustomControlsTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_custom_controls_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status create(const gf_custom_control_spec* spec, gf_utf8_view stable_id,
                                   gf_object* object) const noexcept {
        if (table_.struct_size < 16u || table_.create == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.create(spec, stable_id, object);
        return called;
    }

    [[nodiscard]] gf_status notify_changed(gf_object object, uint64_t index) const noexcept {
        if (table_.struct_size < 24u || table_.notify_changed == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.notify_changed(object, index);
        return called;
    }

private:
    gf_custom_controls_table table_;
};

// gf_bindings_table, negotiated as GF_TABLE_BINDINGS.
class BindingsTable {
public:
    BindingsTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(BindingsTable& output) noexcept {
        gf_bindings_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_BINDINGS, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = BindingsTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static BindingsTable adopt(const gf_bindings_table& adopted_table) noexcept {
        BindingsTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_bindings_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status bind(const gf_binding_arguments* arguments, gf_object* binding) const noexcept {
        if (table_.struct_size < 16u || table_.bind == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.bind(arguments, binding);
        return called;
    }

    [[nodiscard]] gf_status unbind(gf_object binding) const noexcept {
        if (table_.struct_size < 24u || table_.unbind == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.unbind(binding);
        return called;
    }

    [[nodiscard]] gf_status read_value(gf_object binding, uint32_t* updated) const noexcept {
        if (table_.struct_size < 32u || table_.read_value == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.read_value(binding, updated);
        return called;
    }

    [[nodiscard]] gf_status write_value(gf_object binding, uint32_t* updated) const noexcept {
        if (table_.struct_size < 40u || table_.write_value == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.write_value(binding, updated);
        return called;
    }

    [[nodiscard]] gf_status add_record(gf_object source, const gf_binding_record* record,
                                       uint64_t* index) const noexcept {
        if (table_.struct_size < 48u || table_.add_record == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.add_record(source, record, index);
        return called;
    }

    [[nodiscard]] gf_status insert_record(gf_object source, uint64_t index,
                                          const gf_binding_record* record) const noexcept {
        if (table_.struct_size < 56u || table_.insert_record == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.insert_record(source, index, record);
        return called;
    }

    [[nodiscard]] gf_status remove_record(gf_object source, uint64_t index, uint32_t* removed) const noexcept {
        if (table_.struct_size < 64u || table_.remove_record == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.remove_record(source, index, removed);
        return called;
    }

    [[nodiscard]] gf_status clear_records(gf_object source) const noexcept {
        if (table_.struct_size < 72u || table_.clear_records == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.clear_records(source);
        return called;
    }

    [[nodiscard]] gf_status set_position(gf_object source, int64_t position, uint32_t* moved) const noexcept {
        if (table_.struct_size < 80u || table_.set_position == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_position(source, position, moved);
        return called;
    }

    [[nodiscard]] gf_status position(gf_object source, int64_t* position, uint64_t* count) const noexcept {
        if (table_.struct_size < 88u || table_.position == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.position(source, position, count);
        return called;
    }

    [[nodiscard]] gf_status current_field(gf_object source, gf_utf8_view field, gf_value* value, char* text_buffer,
                                          uint64_t text_capacity, uint64_t* text_required) const noexcept {
        if (table_.struct_size < 96u || table_.current_field == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.current_field(source, field, value, text_buffer, text_capacity, text_required);
        return called;
    }

    [[nodiscard]] gf_status set_current_field(gf_object source, gf_utf8_view field, const gf_value* value,
                                              uint32_t* accepted) const noexcept {
        if (table_.struct_size < 104u || table_.set_current_field == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_current_field(source, field, value, accepted);
        return called;
    }

private:
    gf_bindings_table table_;
};

// gf_subsurfaces_table, negotiated as GF_TABLE_SUBSURFACES.
class SubsurfacesTable {
public:
    SubsurfacesTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(SubsurfacesTable& output) noexcept {
        gf_subsurfaces_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_SUBSURFACES, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = SubsurfacesTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static SubsurfacesTable adopt(const gf_subsurfaces_table& adopted_table) noexcept {
        SubsurfacesTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_subsurfaces_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status set_content(gf_object host, const gf_subsurface_content* content) const noexcept {
        if (table_.struct_size < 16u || table_.set_content == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_content(host, content);
        return called;
    }

    [[nodiscard]] gf_status set_root(gf_object root) const noexcept {
        if (table_.struct_size < 24u || table_.set_root == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_root(root);
        return called;
    }

    [[nodiscard]] gf_status state(gf_object host, gf_subsurface_state* state) const noexcept {
        if (table_.struct_size < 32u || table_.state == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.state(host, state);
        return called;
    }

    [[nodiscard]] gf_status content_epoch(gf_object host, uint64_t* epoch) const noexcept {
        if (table_.struct_size < 40u || table_.content_epoch == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.content_epoch(host, epoch);
        return called;
    }

    [[nodiscard]] gf_status failure(gf_object host, char* buffer, uint64_t capacity,
                                    uint64_t* required) const noexcept {
        if (table_.struct_size < 48u || table_.failure == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.failure(host, buffer, capacity, required);
        return called;
    }

private:
    gf_subsurfaces_table table_;
};

// gf_application_table, negotiated as GF_TABLE_APPLICATION.
class ApplicationTable {
public:
    ApplicationTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(ApplicationTable& output) noexcept {
        gf_application_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_APPLICATION, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = ApplicationTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static ApplicationTable adopt(const gf_application_table& adopted_table) noexcept {
        ApplicationTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_application_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status create(const gf_application_config* config, gf_theme_report* report) const noexcept {
        if (table_.struct_size < 16u || table_.create == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.create(config, report);
        return called;
    }

    [[nodiscard]] gf_status close(uint64_t* windows_alive) const noexcept {
        if (table_.struct_size < 24u || table_.close == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.close(windows_alive);
        return called;
    }

    [[nodiscard]] gf_status state(gf_application_state* state) const noexcept {
        if (table_.struct_size < 32u || table_.state == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.state(state);
        return called;
    }

private:
    gf_application_table table_;
};

// gf_rules_table, negotiated as GF_TABLE_RULES.
class RulesTable {
public:
    RulesTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(RulesTable& output) noexcept {
        gf_rules_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_RULES, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = RulesTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static RulesTable adopt(const gf_rules_table& adopted_table) noexcept {
        RulesTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_rules_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status add(gf_object window, const gf_rule* rule, gf_rule_token* token) const noexcept {
        if (table_.struct_size < 16u || table_.add == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.add(window, rule, token);
        return called;
    }

    [[nodiscard]] gf_status remove(gf_rule_token token) const noexcept {
        if (table_.struct_size < 24u || table_.remove == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.remove(token);
        return called;
    }

    [[nodiscard]] gf_status state(gf_rule_token token, gf_rule_state* state) const noexcept {
        if (table_.struct_size < 32u || table_.state == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.state(token, state);
        return called;
    }

private:
    gf_rules_table table_;
};

// gf_data_grid_table, negotiated as GF_TABLE_DATA_GRID.
class DataGridTable {
public:
    DataGridTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(DataGridTable& output) noexcept {
        gf_data_grid_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_DATA_GRID, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = DataGridTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static DataGridTable adopt(const gf_data_grid_table& adopted_table) noexcept {
        DataGridTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_data_grid_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status column_count(gf_object grid, uint64_t* count) const noexcept {
        if (table_.struct_size < 16u || table_.column_count == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.column_count(grid, count);
        return called;
    }

    [[nodiscard]] gf_status column_at(gf_object grid, uint64_t index, gf_data_grid_column* column, char* text_buffer,
                                      uint64_t text_capacity, uint64_t* text_required) const noexcept {
        if (table_.struct_size < 24u || table_.column_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.column_at(grid, index, column, text_buffer, text_capacity, text_required);
        return called;
    }

    [[nodiscard]] gf_status column_item(gf_object grid, uint64_t column, uint64_t item, char* buffer, uint64_t capacity,
                                        uint64_t* required) const noexcept {
        if (table_.struct_size < 32u || table_.column_item == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.column_item(grid, column, item, buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status insert_column(gf_object grid, uint64_t index,
                                          const gf_data_grid_column* column) const noexcept {
        if (table_.struct_size < 40u || table_.insert_column == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.insert_column(grid, index, column);
        return called;
    }

    [[nodiscard]] gf_status remove_column(gf_object grid, uint64_t index) const noexcept {
        if (table_.struct_size < 48u || table_.remove_column == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.remove_column(grid, index);
        return called;
    }

    [[nodiscard]] gf_status column_index(gf_object grid, gf_utf8_view name, uint64_t* index) const noexcept {
        if (table_.struct_size < 56u || table_.column_index == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.column_index(grid, name, index);
        return called;
    }

    [[nodiscard]] gf_status set_column_header_text(gf_object grid, uint64_t column, gf_utf8_view text) const noexcept {
        if (table_.struct_size < 64u || table_.set_column_header_text == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_column_header_text(grid, column, text);
        return called;
    }

    [[nodiscard]] gf_status set_column_width(gf_object grid, uint64_t column, double width) const noexcept {
        if (table_.struct_size < 72u || table_.set_column_width == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_column_width(grid, column, width);
        return called;
    }

    [[nodiscard]] gf_status set_column_read_only(gf_object grid, uint64_t column, uint32_t read_only) const noexcept {
        if (table_.struct_size < 80u || table_.set_column_read_only == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_column_read_only(grid, column, read_only);
        return called;
    }

    [[nodiscard]] gf_status set_column_hidden(gf_object grid, uint64_t column, uint32_t hidden) const noexcept {
        if (table_.struct_size < 88u || table_.set_column_hidden == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_column_hidden(grid, column, hidden);
        return called;
    }

    [[nodiscard]] gf_status row_count(gf_object grid, uint64_t* count, int64_t* new_row) const noexcept {
        if (table_.struct_size < 96u || table_.row_count == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.row_count(grid, count, new_row);
        return called;
    }

    [[nodiscard]] gf_status insert_rows(gf_object grid, uint64_t index, const gf_data_grid_row* rows,
                                        uint64_t count) const noexcept {
        if (table_.struct_size < 104u || table_.insert_rows == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.insert_rows(grid, index, rows, count);
        return called;
    }

    [[nodiscard]] gf_status append_rows(gf_object grid, const gf_data_grid_row* rows, uint64_t count,
                                        uint64_t* first) const noexcept {
        if (table_.struct_size < 112u || table_.append_rows == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.append_rows(grid, rows, count, first);
        return called;
    }

    [[nodiscard]] gf_status erase_rows(gf_object grid, uint64_t index, uint64_t count) const noexcept {
        if (table_.struct_size < 120u || table_.erase_rows == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.erase_rows(grid, index, count);
        return called;
    }

    [[nodiscard]] gf_status replace_row(gf_object grid, uint64_t index, const gf_data_grid_row* row) const noexcept {
        if (table_.struct_size < 128u || table_.replace_row == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.replace_row(grid, index, row);
        return called;
    }

    [[nodiscard]] gf_status clear_rows(gf_object grid) const noexcept {
        if (table_.struct_size < 136u || table_.clear_rows == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.clear_rows(grid);
        return called;
    }

    [[nodiscard]] gf_status row_id(gf_object grid, uint64_t position, char* buffer, uint64_t capacity,
                                   uint64_t* required) const noexcept {
        if (table_.struct_size < 144u || table_.row_id == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.row_id(grid, position, buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status row_position(gf_object grid, gf_utf8_view row_id, uint64_t* position) const noexcept {
        if (table_.struct_size < 152u || table_.row_position == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.row_position(grid, row_id, position);
        return called;
    }

    [[nodiscard]] gf_status cell_value(gf_object grid, gf_utf8_view row_id, uint64_t column, gf_value_kind as_kind,
                                       gf_value* value, char* text_buffer, uint64_t text_capacity,
                                       uint64_t* text_required) const noexcept {
        if (table_.struct_size < 160u || table_.cell_value == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.cell_value(grid, row_id, column, as_kind, value, text_buffer, text_capacity,
                                                   text_required);
        return called;
    }

    [[nodiscard]] gf_status set_cell_value(gf_object grid, gf_utf8_view row_id, uint64_t column, const gf_value* value,
                                           uint32_t* accepted) const noexcept {
        if (table_.struct_size < 168u || table_.set_cell_value == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_cell_value(grid, row_id, column, value, accepted);
        return called;
    }

    [[nodiscard]] gf_status cell_text(gf_object grid, gf_utf8_view row_id, uint64_t column, char* buffer,
                                      uint64_t capacity, uint64_t* required) const noexcept {
        if (table_.struct_size < 176u || table_.cell_text == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.cell_text(grid, row_id, column, buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status read_cells(gf_object grid, uint64_t column, uint64_t first_row, uint64_t row_count,
                                       gf_value* values, char* text_buffer, uint64_t text_capacity,
                                       uint64_t* text_required) const noexcept {
        if (table_.struct_size < 184u || table_.read_cells == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.read_cells(grid, column, first_row, row_count, values, text_buffer,
                                                   text_capacity, text_required);
        return called;
    }

    [[nodiscard]] gf_status current_cell(gf_object grid, uint32_t* present, uint64_t* row,
                                         uint64_t* column) const noexcept {
        if (table_.struct_size < 192u || table_.current_cell == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.current_cell(grid, present, row, column);
        return called;
    }

    [[nodiscard]] gf_status set_current_cell(gf_object grid, gf_utf8_view row_id, uint64_t column,
                                             uint32_t* moved) const noexcept {
        if (table_.struct_size < 200u || table_.set_current_cell == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_current_cell(grid, row_id, column, moved);
        return called;
    }

    [[nodiscard]] gf_status selected_rows(gf_object grid, uint64_t* positions, uint64_t capacity,
                                          uint64_t* count) const noexcept {
        if (table_.struct_size < 208u || table_.selected_rows == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.selected_rows(grid, positions, capacity, count);
        return called;
    }

    [[nodiscard]] gf_status selected_cells(gf_object grid, gf_data_grid_cell* cells, uint64_t capacity,
                                           uint64_t* count) const noexcept {
        if (table_.struct_size < 216u || table_.selected_cells == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.selected_cells(grid, cells, capacity, count);
        return called;
    }

    [[nodiscard]] gf_status set_row_selected(gf_object grid, gf_utf8_view row_id, uint32_t selected) const noexcept {
        if (table_.struct_size < 224u || table_.set_row_selected == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_row_selected(grid, row_id, selected);
        return called;
    }

    [[nodiscard]] gf_status set_cell_selected(gf_object grid, gf_utf8_view row_id, uint64_t column,
                                              uint32_t selected) const noexcept {
        if (table_.struct_size < 232u || table_.set_cell_selected == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_cell_selected(grid, row_id, column, selected);
        return called;
    }

    [[nodiscard]] gf_status clear_selection(gf_object grid) const noexcept {
        if (table_.struct_size < 240u || table_.clear_selection == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.clear_selection(grid);
        return called;
    }

    [[nodiscard]] gf_status select_all(gf_object grid) const noexcept {
        if (table_.struct_size < 248u || table_.select_all == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.select_all(grid);
        return called;
    }

    [[nodiscard]] gf_status begin_edit(gf_object grid, uint32_t select_all, uint32_t* began) const noexcept {
        if (table_.struct_size < 256u || table_.begin_edit == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.begin_edit(grid, select_all, began);
        return called;
    }

    [[nodiscard]] gf_status end_edit(gf_object grid, uint32_t* committed) const noexcept {
        if (table_.struct_size < 264u || table_.end_edit == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.end_edit(grid, committed);
        return called;
    }

    [[nodiscard]] gf_status cancel_edit(gf_object grid) const noexcept {
        if (table_.struct_size < 272u || table_.cancel_edit == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.cancel_edit(grid);
        return called;
    }

private:
    gf_data_grid_table table_;
};

// gf_docking_table, negotiated as GF_TABLE_DOCKING.
class DockingTable {
public:
    DockingTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(DockingTable& output) noexcept {
        gf_docking_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_DOCKING, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = DockingTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static DockingTable adopt(const gf_docking_table& adopted_table) noexcept {
        DockingTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_docking_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status register_panel(gf_object workspace, const gf_dock_panel* panel) const noexcept {
        if (table_.struct_size < 16u || table_.register_panel == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.register_panel(workspace, panel);
        return called;
    }

    [[nodiscard]] gf_status set_panel_content(gf_object workspace, gf_utf8_view panel,
                                              gf_object content) const noexcept {
        if (table_.struct_size < 24u || table_.set_panel_content == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_panel_content(workspace, panel, content);
        return called;
    }

    [[nodiscard]] gf_status set_panel_title(gf_object workspace, gf_utf8_view panel,
                                            gf_utf8_view title) const noexcept {
        if (table_.struct_size < 32u || table_.set_panel_title == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_panel_title(workspace, panel, title);
        return called;
    }

    [[nodiscard]] gf_status apply_edit(gf_object workspace, const gf_dock_edit* edit,
                                       gf_dock_refusal* refusal) const noexcept {
        if (table_.struct_size < 40u || table_.apply_edit == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.apply_edit(workspace, edit, refusal);
        return called;
    }

    [[nodiscard]] gf_status locate(gf_object workspace, gf_utf8_view panel, gf_dock_location* location) const noexcept {
        if (table_.struct_size < 48u || table_.locate == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.locate(workspace, panel, location);
        return called;
    }

    [[nodiscard]] gf_status save_layout(gf_object workspace, char* buffer, uint64_t capacity,
                                        uint64_t* required) const noexcept {
        if (table_.struct_size < 56u || table_.save_layout == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.save_layout(workspace, buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status load_layout(gf_object workspace, gf_utf8_view xml,
                                        uint64_t* diagnostic_count) const noexcept {
        if (table_.struct_size < 64u || table_.load_layout == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.load_layout(workspace, xml, diagnostic_count);
        return called;
    }

    [[nodiscard]] gf_status activate_panel(gf_object workspace, gf_utf8_view panel) const noexcept {
        if (table_.struct_size < 72u || table_.activate_panel == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.activate_panel(workspace, panel);
        return called;
    }

    [[nodiscard]] gf_status attach_windows(gf_object workspace) const noexcept {
        if (table_.struct_size < 80u || table_.attach_windows == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.attach_windows(workspace);
        return called;
    }

    [[nodiscard]] gf_status document_area(gf_object workspace, uint32_t* node) const noexcept {
        if (table_.struct_size < 88u || table_.document_area == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.document_area(workspace, node);
        return called;
    }

    [[nodiscard]] gf_status panel_at(gf_object workspace, uint32_t stack, uint64_t index, char* buffer,
                                     uint64_t capacity, uint64_t* required) const noexcept {
        if (table_.struct_size < 96u || table_.panel_at == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.panel_at(workspace, stack, index, buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status floating_sites(gf_object workspace, uint32_t* sites, uint64_t capacity,
                                           uint64_t* count) const noexcept {
        if (table_.struct_size < 104u || table_.floating_sites == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.floating_sites(workspace, sites, capacity, count);
        return called;
    }

    [[nodiscard]] gf_status floating_site(gf_object workspace, uint32_t site, gf_logical_rect* frame,
                                          uint32_t* first_stack) const noexcept {
        if (table_.struct_size < 112u || table_.floating_site == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.floating_site(workspace, site, frame, first_stack);
        return called;
    }

    [[nodiscard]] gf_status update_panel(gf_object workspace, const gf_dock_panel* panel) const noexcept {
        if (table_.struct_size < 120u || table_.update_panel == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.update_panel(workspace, panel);
        return called;
    }

    [[nodiscard]] gf_status set_options(gf_object workspace, const gf_dock_options* options) const noexcept {
        if (table_.struct_size < 128u || table_.set_options == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_options(workspace, options);
        return called;
    }

    [[nodiscard]] gf_status set_floating_window_visibility(gf_object workspace, uint32_t site,
                                                           gf_dock_window_visibility visibility) const noexcept {
        if (table_.struct_size < 136u || table_.set_floating_window_visibility == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.set_floating_window_visibility(workspace, site, visibility);
        return called;
    }

private:
    gf_docking_table table_;
};

// gf_windows_paint_endpoint_table, negotiated as GF_TABLE_WINDOWS_PAINT_ENDPOINT.
class WindowsPaintEndpointTable {
public:
    WindowsPaintEndpointTable() noexcept : table_{} {}

    // Negotiates the table at the header's version into output; a refusal
    // leaves output serving nothing.
    [[nodiscard]] static gf_status negotiate(WindowsPaintEndpointTable& output) noexcept {
        gf_windows_paint_endpoint_table negotiated_table{};
        const gf_status negotiated = gf_abi_get_table(GF_TABLE_WINDOWS_PAINT_ENDPOINT, GF_ABI_MAJOR, GF_ABI_MINOR,
                                                      &negotiated_table, sizeof(negotiated_table));
        if (negotiated != GF_STATUS_OK) {
            output = WindowsPaintEndpointTable{};
            return negotiated;
        }
        output.table_ = negotiated_table;
        return GF_STATUS_OK;
    }

    // A table the caller negotiated through gf_abi_get_table.
    [[nodiscard]] static WindowsPaintEndpointTable adopt(
        const gf_windows_paint_endpoint_table& adopted_table) noexcept {
        WindowsPaintEndpointTable adopted;
        adopted.table_ = adopted_table;
        return adopted;
    }

    [[nodiscard]] const gf_windows_paint_endpoint_table& c_table() const noexcept { return table_; }

    [[nodiscard]] gf_status acquire(gf_object control, uint32_t width, uint32_t height, gf_paint_endpoint* endpoint,
                                    uint64_t* compatibility_handle) const noexcept {
        if (table_.struct_size < 16u || table_.acquire == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.acquire(control, width, height, endpoint, compatibility_handle);
        return called;
    }

    [[nodiscard]] gf_status configure(gf_paint_endpoint endpoint, uint32_t width, uint32_t height) const noexcept {
        if (table_.struct_size < 24u || table_.configure == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.configure(endpoint, width, height);
        return called;
    }

    [[nodiscard]] gf_status touch(gf_paint_endpoint endpoint, uint32_t explicit_boundary) const noexcept {
        if (table_.struct_size < 32u || table_.touch == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.touch(endpoint, explicit_boundary);
        return called;
    }

    [[nodiscard]] gf_status drain(gf_paint_endpoint endpoint) const noexcept {
        if (table_.struct_size < 40u || table_.drain == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.drain(endpoint);
        return called;
    }

    [[nodiscard]] gf_status snapshot(gf_paint_endpoint endpoint, char* buffer, uint64_t capacity,
                                     uint64_t* required) const noexcept {
        if (table_.struct_size < 48u || table_.snapshot == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.snapshot(endpoint, buffer, capacity, required);
        return called;
    }

    [[nodiscard]] gf_status submit_bgra(uint64_t compatibility_handle, uint32_t width, uint32_t height,
                                        uint64_t row_bytes, const uint8_t* pixels) const noexcept {
        if (table_.struct_size < 56u || table_.submit_bgra == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.submit_bgra(compatibility_handle, width, height, row_bytes, pixels);
        return called;
    }

    [[nodiscard]] gf_status release(gf_paint_endpoint endpoint) const noexcept {
        if (table_.struct_size < 64u || table_.release == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.release(endpoint);
        return called;
    }

    [[nodiscard]] gf_status get_dc(uint64_t compatibility_handle, uint64_t* device_context) const noexcept {
        if (table_.struct_size < 72u || table_.get_dc == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.get_dc(compatibility_handle, device_context);
        return called;
    }

    [[nodiscard]] gf_status release_dc(uint64_t compatibility_handle, uint64_t device_context) const noexcept {
        if (table_.struct_size < 80u || table_.release_dc == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.release_dc(compatibility_handle, device_context);
        return called;
    }

    [[nodiscard]] gf_status publish_dc(uint64_t device_context) const noexcept {
        if (table_.struct_size < 88u || table_.publish_dc == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.publish_dc(device_context);
        return called;
    }

    [[nodiscard]] gf_status begin_write(uint64_t device_context, gf_paint_write_lease* lease) const noexcept {
        if (table_.struct_size < 96u || table_.begin_write == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.begin_write(device_context, lease);
        return called;
    }

    [[nodiscard]] gf_status end_write(gf_paint_write_lease lease, uint32_t publish) const noexcept {
        if (table_.struct_size < 104u || table_.end_write == nullptr) {
            return GF_STATUS_UNSUPPORTED_VERSION;
        }
        const gf_status called = table_.end_write(lease, publish);
        return called;
    }

private:
    gf_windows_paint_endpoint_table table_;
};

// One reference to a library object, released (objects.release) when the
// owner goes or resets. The ObjectsTable it borrows must outlive it.
class ObjectReference {
public:
    ObjectReference() noexcept : objects_(nullptr), object_(0u) {}
    ObjectReference(const ObjectsTable& objects, gf_object object) noexcept : objects_(&objects), object_(object) {}
    ObjectReference(const ObjectReference& other) = delete;
    ObjectReference& operator=(const ObjectReference& other) = delete;
    ObjectReference(ObjectReference&& other) noexcept : objects_(other.objects_), object_(other.object_) {
        other.objects_ = nullptr;
        other.object_ = 0u;
    }
    ObjectReference& operator=(ObjectReference&& other) noexcept {
        if (this != &other) {
            (void)(*this).reset();
            objects_ = other.objects_;
            object_ = other.object_;
            other.objects_ = nullptr;
            other.object_ = 0u;
        }
        return *this;
    }
    ~ObjectReference() { (void)(*this).reset(); }

    [[nodiscard]] gf_object get() const noexcept { return object_; }

    // Gives the reference to the caller without releasing it.
    [[nodiscard]] gf_object detach() noexcept {
        const gf_object detached = object_;
        objects_ = nullptr;
        object_ = 0u;
        return detached;
    }

    // Releases the reference now; GF_STATUS_OK when there was none. The owner
    // is empty afterwards whatever release answered.
    gf_status reset() noexcept {
        if (objects_ == nullptr || object_ == 0u) {
            objects_ = nullptr;
            object_ = 0u;
            return GF_STATUS_OK;
        }
        const gf_status released = (*objects_).release(object_);
        objects_ = nullptr;
        object_ = 0u;
        return released;
    }

private:
    const ObjectsTable* objects_;
    gf_object object_;
};

// One subscription, disconnected (events.disconnect) when the owner goes or
// resets. The EventsTable it borrows must outlive it.
class SubscriptionReference {
public:
    SubscriptionReference() noexcept : events_(nullptr), subscription_(0u) {}
    SubscriptionReference(const EventsTable& events, gf_subscription subscription) noexcept
        : events_(&events), subscription_(subscription) {}
    SubscriptionReference(const SubscriptionReference& other) = delete;
    SubscriptionReference& operator=(const SubscriptionReference& other) = delete;
    SubscriptionReference(SubscriptionReference&& other) noexcept
        : events_(other.events_), subscription_(other.subscription_) {
        other.events_ = nullptr;
        other.subscription_ = 0u;
    }
    SubscriptionReference& operator=(SubscriptionReference&& other) noexcept {
        if (this != &other) {
            (void)(*this).reset();
            events_ = other.events_;
            subscription_ = other.subscription_;
            other.events_ = nullptr;
            other.subscription_ = 0u;
        }
        return *this;
    }
    ~SubscriptionReference() { (void)(*this).reset(); }

    [[nodiscard]] gf_subscription get() const noexcept { return subscription_; }

    // Disconnects now; GF_STATUS_OK when there was nothing to disconnect. The
    // owner is empty afterwards whatever disconnect answered.
    gf_status reset() noexcept {
        if (events_ == nullptr || subscription_ == 0u) {
            events_ = nullptr;
            subscription_ = 0u;
            return GF_STATUS_OK;
        }
        const gf_status disconnected = (*events_).disconnect(subscription_);
        events_ = nullptr;
        subscription_ = 0u;
        return disconnected;
    }

private:
    const EventsTable* events_;
    gf_subscription subscription_;
};

} // namespace gui_forms::abi
