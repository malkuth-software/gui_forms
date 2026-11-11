// GF_TABLE_BINDINGS: bindings between control properties and the fields of
// a binding source's records, over the C++ Binding and BindingSource. See
// gf_abi.h.

#include "abi_tables.hpp"

#include "../registry/abi_registry.hpp"
#include "../values/abi_values.hpp"

#include "gui_forms/binding/binding/binding.hpp"
#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity bind_slot{GF_TABLE_BINDINGS, 0U};
constexpr SlotIdentity unbind_slot{GF_TABLE_BINDINGS, 1U};
constexpr SlotIdentity read_value_slot{GF_TABLE_BINDINGS, 2U};
constexpr SlotIdentity write_value_slot{GF_TABLE_BINDINGS, 3U};
constexpr SlotIdentity add_record_slot{GF_TABLE_BINDINGS, 4U};
constexpr SlotIdentity insert_record_slot{GF_TABLE_BINDINGS, 5U};
constexpr SlotIdentity remove_record_slot{GF_TABLE_BINDINGS, 6U};
constexpr SlotIdentity clear_records_slot{GF_TABLE_BINDINGS, 7U};
constexpr SlotIdentity set_position_slot{GF_TABLE_BINDINGS, 8U};
constexpr SlotIdentity position_slot{GF_TABLE_BINDINGS, 9U};
constexpr SlotIdentity current_field_slot{GF_TABLE_BINDINGS, 10U};
constexpr SlotIdentity set_current_field_slot{GF_TABLE_BINDINGS, 11U};
constexpr SlotIdentity add_records_slot{GF_TABLE_BINDINGS, 12U};

constexpr std::uint32_t arguments_size_1_0 = 96U;
constexpr std::uint32_t record_size_1_0 = 40U;
static_assert(sizeof(gf_binding_arguments) == arguments_size_1_0);
static_assert(sizeof(gf_binding_record) == record_size_1_0);
static_assert(sizeof(gf_binding_field) == 24U);

constexpr const char* not_source = "the object is not a binding source";
constexpr const char* not_binding = "the object is not a binding";

// The UI thread, then the live component behind object as Typed.
template <typename Typed>
gf_status enter_typed_component(gf_object object, SlotIdentity slot, const char* refusal,
                                std::shared_ptr<Typed>& typed) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr component;
    const gf_status found = registry().find_component(object, slot, component);
    if (found != GF_STATUS_OK) {
        return found;
    }
    std::shared_ptr<Typed> cast = std::dynamic_pointer_cast<Typed>(component);
    if (!cast) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, refusal);
    }
    if (!(*cast).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the object has been disposed");
    }
    typed = std::move(cast);
    return GF_STATUS_OK;
}

// A value a caller may leave null: monostate then.
gf_status read_optional_value(const gf_value* value, SlotIdentity slot, BindingValue& converted) {
    if (value == nullptr) {
        converted = BindingValue{};
        return GF_STATUS_OK;
    }
    return read_value(value, slot, converted);
}

// A caller's record, read and validated whole.
gf_status read_record(const gf_binding_record* record, SlotIdentity slot, BindingRecord& converted) {
    if (record == nullptr || (*record).struct_size < record_size_1_0 || (*record).editable > 1U ||
        (*record).field_count > GF_BINDING_MAXIMUM_FIELDS ||
        ((*record).fields == nullptr && (*record).field_count != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a record needs struct_size of at least 40, editable 0 or 1, and at most 1024 fields");
    }
    std::string_view stable_id;
    const gf_status identified = read_text((*record).stable_id, slot, stable_id);
    if (identified != GF_STATUS_OK) {
        return identified;
    }
    converted.stable_id = std::string(stable_id);
    converted.editable = (*record).editable == 1U;
    for (std::uint64_t index = 0U; index < (*record).field_count; ++index) {
        const gf_binding_field& field = (*record).fields[index];
        std::string_view name;
        const gf_status named = read_text(field.name, slot, name);
        if (named != GF_STATUS_OK) {
            return named;
        }
        if (name.empty()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a field's name is not empty");
        }
        BindingValue value;
        const gf_status valued = read_optional_value(field.value, slot, value);
        if (valued != GF_STATUS_OK) {
            return valued;
        }
        const bool inserted = converted.fields.emplace(std::string(name), std::move(value)).second;
        if (!inserted) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a record's field names are distinct");
        }
    }
    return GF_STATUS_OK;
}

gf_status read_options(const gf_binding_arguments& arguments, BindingOptions& options) {
    if (arguments.source_update_mode > GF_SOURCE_UPDATE_NEVER ||
        arguments.control_update_mode > GF_CONTROL_UPDATE_NEVER || arguments.formatting_enabled > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, bind_slot,
                    "the update modes are gf_source_update_mode and gf_control_update_mode values, "
                    "formatting_enabled 0 or 1");
    }
    options.data_source_update_mode = static_cast<DataSourceUpdateMode>(arguments.source_update_mode);
    options.control_update_mode = static_cast<ControlUpdateMode>(arguments.control_update_mode);
    options.formatting_enabled = arguments.formatting_enabled == 1U;
    std::string_view format;
    const gf_status formatted = read_text(arguments.format_string, bind_slot, format);
    if (formatted != GF_STATUS_OK) {
        return formatted;
    }
    options.format_string = std::string(format);
    const gf_status null_read = read_optional_value(arguments.null_value, bind_slot, options.null_value);
    if (null_read != GF_STATUS_OK) {
        return null_read;
    }
    return read_optional_value(arguments.source_null_value, bind_slot, options.data_source_null_value);
}

gf_status GF_CALL bindings_bind(const gf_binding_arguments* arguments, gf_object* binding) noexcept {
    try {
        const ShadowFrameScope shadow_frame(bind_slot.table_id, bind_slot.slot_index);
        if (arguments == nullptr || binding == nullptr || (*arguments).struct_size < arguments_size_1_0) {
            return fail(GF_STATUS_INVALID_ARGUMENT, bind_slot,
                        "bind needs gf_binding_arguments of at least the 1.0 size and a binding output");
        }
        BindingOptions options;
        const gf_status optioned = read_options(*arguments, options);
        if (optioned != GF_STATUS_OK) {
            return optioned;
        }
        std::string_view property;
        std::string_view member;
        const gf_status named = read_text((*arguments).property, bind_slot, property);
        if (named != GF_STATUS_OK) {
            return named;
        }
        const gf_status membered = read_text((*arguments).data_member, bind_slot, member);
        if (membered != GF_STATUS_OK) {
            return membered;
        }
        if (property.empty()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, bind_slot, "a binding names its property");
        }
        std::shared_ptr<BindingSource> source;
        const gf_status sourced = enter_typed_component((*arguments).source, bind_slot, not_source, source);
        if (sourced != GF_STATUS_OK) {
            return sourced;
        }
        Control::Ptr target;
        const gf_status found = registry().find_object((*arguments).target, bind_slot, target);
        if (found != GF_STATUS_OK) {
            return found;
        }
        if (!(*target).is_alive()) {
            return fail(GF_STATUS_DISPOSED, bind_slot, "the target has been disposed");
        }
        const std::shared_ptr<Binding> made =
            (*target).data_bindings().add(std::string(property), source, std::string(member), std::move(options));
        *binding = registry().add_reference(made);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(bind_slot);
    }
}

gf_status GF_CALL bindings_unbind(gf_object binding) noexcept {
    try {
        const ShadowFrameScope shadow_frame(unbind_slot.table_id, unbind_slot.slot_index);
        std::shared_ptr<Binding> typed;
        const gf_status entered = enter_typed_component(binding, unbind_slot, not_binding, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Control* target = (*typed).target();
        if (target == nullptr || !(*target).data_bindings().remove(*typed)) {
            return fail(GF_STATUS_INVALID_STATE, unbind_slot, "the binding is no longer bound");
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(unbind_slot);
    }
}

gf_status GF_CALL bindings_read_value(gf_object binding, std::uint32_t* updated) noexcept {
    try {
        const ShadowFrameScope shadow_frame(read_value_slot.table_id, read_value_slot.slot_index);
        if (updated == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, read_value_slot, "read_value needs an output");
        }
        std::shared_ptr<Binding> typed;
        const gf_status entered = enter_typed_component(binding, read_value_slot, not_binding, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *updated = (*typed).read_value() ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(read_value_slot);
    }
}

gf_status GF_CALL bindings_write_value(gf_object binding, std::uint32_t* updated) noexcept {
    try {
        const ShadowFrameScope shadow_frame(write_value_slot.table_id, write_value_slot.slot_index);
        if (updated == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, write_value_slot, "write_value needs an output");
        }
        std::shared_ptr<Binding> typed;
        const gf_status entered = enter_typed_component(binding, write_value_slot, not_binding, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *updated = (*typed).write_value() ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(write_value_slot);
    }
}

gf_status GF_CALL bindings_add_record(gf_object source, const gf_binding_record* record,
                                      std::uint64_t* index) noexcept {
    try {
        const ShadowFrameScope shadow_frame(add_record_slot.table_id, add_record_slot.slot_index);
        if (index == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, add_record_slot, "add_record needs an index output");
        }
        BindingRecord converted;
        const gf_status read = read_record(record, add_record_slot, converted);
        if (read != GF_STATUS_OK) {
            return read;
        }
        std::shared_ptr<BindingSource> typed;
        const gf_status entered = enter_typed_component(source, add_record_slot, not_source, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *index = static_cast<std::uint64_t>((*typed).add(std::move(converted)));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(add_record_slot);
    }
}

// Everything BindingSource::add would refuse for a batch, checked before
// the first record goes in so a refusal adds none: additions disabled, a
// stable id that breaks the StableId rule, two fields with one canonical
// name, or a stable id repeated within the batch or already in the source.
gf_status check_batch(const BindingSource& source, const std::vector<BindingRecord>& batch, SlotIdentity slot) {
    if (!source.allow_new()) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the source's additions are disabled");
    }
    std::set<std::string> identities;
    for (const BindingRecord& existing : source.records()) {
        identities.insert(existing.stable_id);
    }
    for (const BindingRecord& record : batch) {
        if (!valid_stable_id(record.stable_id)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a record's stable id breaks the StableId rule");
        }
        if (!identities.insert(record.stable_id).second) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a record's stable id is repeated or already present");
        }
        std::set<std::string> names;
        for (const std::pair<const std::string, BindingValue>& field : record.fields) {
            const std::string canonical = canonical_binding_name(field.first);
            if (!names.insert(canonical).second) {
                return fail(GF_STATUS_INVALID_ARGUMENT, slot, "two of a record's fields share a canonical name");
            }
        }
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL bindings_add_records(gf_object source, const gf_binding_record* records, std::uint64_t count,
                                       std::uint64_t* first) noexcept {
    try {
        const ShadowFrameScope shadow_frame(add_records_slot.table_id, add_records_slot.slot_index);
        if (first == nullptr || (records == nullptr && count != 0U)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, add_records_slot, "add_records needs records and a first output");
        }
        if (count > GF_BINDING_MAXIMUM_RECORDS_PER_CALL) {
            return fail(GF_STATUS_LIMIT_EXCEEDED, add_records_slot,
                        "add_records takes at most GF_BINDING_MAXIMUM_RECORDS_PER_CALL records");
        }
        // The array rule: the caller's sizeof is the stride, at least the
        // 1.0 size and a multiple of 8, so a later minor's larger record
        // still steps right.
        std::size_t stride = 0U;
        if (count != 0U) {
            stride = static_cast<std::size_t>((*records).struct_size);
            if (stride < record_size_1_0 || stride % 8U != 0U) {
                return fail(GF_STATUS_INVALID_ARGUMENT, add_records_slot,
                            "a record's struct_size is at least 40 and a multiple of 8");
            }
        }
        // Every record is read and validated before the source changes.
        std::vector<BindingRecord> converted;
        converted.reserve(static_cast<std::size_t>(count));
        const unsigned char* cursor = reinterpret_cast<const unsigned char*>(records);
        for (std::uint64_t index = 0U; index < count; ++index) {
            const gf_binding_record* record = reinterpret_cast<const gf_binding_record*>(cursor);
            if ((*record).struct_size != (*records).struct_size) {
                return fail(GF_STATUS_INVALID_ARGUMENT, add_records_slot,
                            "every record repeats the first one's struct_size");
            }
            BindingRecord one;
            const gf_status read = read_record(record, add_records_slot, one);
            if (read != GF_STATUS_OK) {
                return read;
            }
            converted.push_back(std::move(one));
            cursor += stride;
        }
        std::shared_ptr<BindingSource> typed;
        const gf_status entered = enter_typed_component(source, add_records_slot, not_source, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status checked = check_batch(*typed, converted, add_records_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        const std::uint64_t start = static_cast<std::uint64_t>((*typed).count());
        // One reset for the call: the per-record list changes are held
        // while the records go in, then the source resets its bindings
        // once, as Windows Forms' RaiseListChangedEvents = false then
        // ResetBindings does.
        const bool raising = (*typed).raise_list_changed_events();
        (*typed).set_raise_list_changed_events(false);
        try {
            for (BindingRecord& record : converted) {
                static_cast<void>((*typed).add(std::move(record)));
            }
        } catch (...) {
            (*typed).set_raise_list_changed_events(raising);
            throw;
        }
        (*typed).set_raise_list_changed_events(raising);
        if (raising && count != 0U) {
            (*typed).reset_bindings();
        }
        *first = start;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(add_records_slot);
    }
}

gf_status GF_CALL bindings_insert_record(gf_object source, std::uint64_t index,
                                         const gf_binding_record* record) noexcept {
    try {
        const ShadowFrameScope shadow_frame(insert_record_slot.table_id, insert_record_slot.slot_index);
        BindingRecord converted;
        const gf_status read = read_record(record, insert_record_slot, converted);
        if (read != GF_STATUS_OK) {
            return read;
        }
        std::shared_ptr<BindingSource> typed;
        const gf_status entered = enter_typed_component(source, insert_record_slot, not_source, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (index > (*typed).count()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, insert_record_slot, "the index is past the records");
        }
        static_cast<void>((*typed).insert(static_cast<std::size_t>(index), std::move(converted)));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(insert_record_slot);
    }
}

gf_status GF_CALL bindings_remove_record(gf_object source, std::uint64_t index, std::uint32_t* removed) noexcept {
    try {
        const ShadowFrameScope shadow_frame(remove_record_slot.table_id, remove_record_slot.slot_index);
        if (removed == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, remove_record_slot, "remove_record needs an output");
        }
        std::shared_ptr<BindingSource> typed;
        const gf_status entered = enter_typed_component(source, remove_record_slot, not_source, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (index >= (*typed).count()) {
            *removed = 0U;
            return GF_STATUS_OK;
        }
        *removed = (*typed).remove_at(static_cast<std::size_t>(index)) ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(remove_record_slot);
    }
}

gf_status GF_CALL bindings_clear_records(gf_object source) noexcept {
    try {
        const ShadowFrameScope shadow_frame(clear_records_slot.table_id, clear_records_slot.slot_index);
        std::shared_ptr<BindingSource> typed;
        const gf_status entered = enter_typed_component(source, clear_records_slot, not_source, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*typed).clear();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(clear_records_slot);
    }
}

gf_status GF_CALL bindings_set_position(gf_object source, std::int64_t position, std::uint32_t* moved) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_position_slot.table_id, set_position_slot.slot_index);
        if (moved == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_position_slot, "set_position needs an output");
        }
        std::shared_ptr<BindingSource> typed;
        const gf_status entered = enter_typed_component(source, set_position_slot, not_source, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (position < -1 || (position >= 0 && static_cast<std::uint64_t>(position) >= (*typed).count())) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_position_slot, "the position is -1 or a record's");
        }
        *moved = (*typed).set_position(static_cast<std::ptrdiff_t>(position)) ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_position_slot);
    }
}

gf_status GF_CALL bindings_position(gf_object source, std::int64_t* position, std::uint64_t* count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(position_slot.table_id, position_slot.slot_index);
        if (position == nullptr || count == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, position_slot, "position needs both outputs");
        }
        std::shared_ptr<BindingSource> typed;
        const gf_status entered = enter_typed_component(source, position_slot, not_source, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *position = static_cast<std::int64_t>((*typed).position());
        *count = static_cast<std::uint64_t>((*typed).count());
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(position_slot);
    }
}

gf_status GF_CALL bindings_current_field(gf_object source, gf_utf8_view field, gf_value* value, char* text_buffer,
                                         std::uint64_t text_capacity, std::uint64_t* text_required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(current_field_slot.table_id, current_field_slot.slot_index);
        const gf_status checked =
            check_value_output(value, text_buffer, text_capacity, text_required, current_field_slot);
        if (checked != GF_STATUS_OK) {
            return checked;
        }
        std::string_view name;
        const gf_status named = read_text(field, current_field_slot, name);
        if (named != GF_STATUS_OK) {
            return named;
        }
        std::shared_ptr<BindingSource> typed;
        const gf_status entered = enter_typed_component(source, current_field_slot, not_source, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::optional<BindingValue> current = (*typed).current_field(name);
        if (!current) {
            return fail(GF_STATUS_NOT_FOUND, current_field_slot, "there is no current record or no such field");
        }
        return write_value_output(*current, value, text_buffer, text_capacity, text_required, current_field_slot);
    } catch (...) {
        return fail_with_current_exception(current_field_slot);
    }
}

gf_status GF_CALL bindings_set_current_field(gf_object source, gf_utf8_view field, const gf_value* value,
                                             std::uint32_t* accepted) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_current_field_slot.table_id, set_current_field_slot.slot_index);
        if (accepted == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, set_current_field_slot, "set_current_field needs an output");
        }
        std::string_view name;
        const gf_status named = read_text(field, set_current_field_slot, name);
        if (named != GF_STATUS_OK) {
            return named;
        }
        BindingValue converted;
        const gf_status valued = read_value(value, set_current_field_slot, converted);
        if (valued != GF_STATUS_OK) {
            return valued;
        }
        std::shared_ptr<BindingSource> typed;
        const gf_status entered = enter_typed_component(source, set_current_field_slot, not_source, typed);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *accepted = (*typed).set_current_field(name, std::move(converted)) ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_current_field_slot);
    }
}

constexpr gf_bindings_table table{
    .struct_size = sizeof(gf_bindings_table),
    .abi_minor = GF_ABI_MINOR,
    .bind = &bindings_bind,
    .unbind = &bindings_unbind,
    .read_value = &bindings_read_value,
    .write_value = &bindings_write_value,
    .add_record = &bindings_add_record,
    .insert_record = &bindings_insert_record,
    .remove_record = &bindings_remove_record,
    .clear_records = &bindings_clear_records,
    .set_position = &bindings_set_position,
    .position = &bindings_position,
    .current_field = &bindings_current_field,
    .set_current_field = &bindings_set_current_field,
    .add_records = &bindings_add_records,
};

} // namespace

const gf_bindings_table& bindings_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
