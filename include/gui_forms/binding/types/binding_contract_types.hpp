#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/value/binding_value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace gui_forms {

class Binding;
class BindingSource;

enum class DataSourceUpdateMode : std::uint8_t {
    on_validation = 0,
    on_property_changed = 1,
    never = 2,
};

enum class ControlUpdateMode : std::uint8_t {
    on_property_changed = 0,
    never = 1,
};

enum class BindingCompleteContext : std::uint8_t {
    control_update,
    data_source_update,
};

enum class BindingCompleteState : std::uint8_t {
    success,
    data_error,
    exception,
};

// A list change. The changes one notification makes are published in
// order, each relative to the list as the ones before it left it:
// item_added: a row now stands at index; item_removed: the row at index is
// gone; item_moved (ListChangedType.ItemMoved): the row at index now stands
// at to_index; item_changed: the row at index changed (field, or every
// field when empty). stable_id is the row's for item_added and
// item_changed, empty otherwise.
enum class BindingListChangeKind : std::uint8_t {
    reset,
    item_added,
    item_changed,
    item_removed,
    position_changed,
    item_moved,
};

struct GUI_FORMS_API BindingListChange final {
    BindingListChangeKind kind{BindingListChangeKind::reset};
    std::ptrdiff_t index{-1};
    std::string stable_id;
    std::string field;
    bool metadata_changed{};
    // item_moved: where the row now stands; -1 otherwise.
    std::ptrdiff_t to_index{-1};
};

struct GUI_FORMS_API BindingConvertEvent final {
    BindingValue value;
    BindingValueKind desired_kind{BindingValueKind::null};
    bool handled{};
};

struct GUI_FORMS_API BindingCompleteEvent final {
    Binding* binding{};
    BindingCompleteContext context{BindingCompleteContext::control_update};
    BindingCompleteState state{BindingCompleteState::success};
    std::string error_text;
    bool cancel{};
};

struct GUI_FORMS_API BindingOptions final {
    DataSourceUpdateMode data_source_update_mode{
        DataSourceUpdateMode::on_validation};
    ControlUpdateMode control_update_mode{
        ControlUpdateMode::on_property_changed};
    bool formatting_enabled{};
    std::string format_string;
    BindingValue null_value;
    BindingValue data_source_null_value;
};

struct GUI_FORMS_API BindingSourceSnapshot final {
    std::size_t count{};
    std::ptrdiff_t position{-1};
    std::string current_stable_id;
    std::uint64_t revision{};
    // The provider-change generation (BindingSource::generation).
    std::uint64_t generation{};
    std::uint64_t resets{};
    std::uint64_t field_changes{};
    std::uint64_t position_changes{};
    std::uint64_t suspended_mutations{};
    bool binding_suspended{};
    bool editing{};
    bool raise_list_changed_events{true};
};

struct GUI_FORMS_API BindingSnapshot final {
    std::string property_name;
    std::string data_member;
    std::uint64_t control_reads{};
    std::uint64_t source_writes{};
    std::uint64_t successful_updates{};
    std::uint64_t failed_updates{};
    std::uint64_t suppressed_reentrant_updates{};
    bool active{};
};

struct GUI_FORMS_API BindingContextChange final {
    BindingSource* source{};
    bool added{};
};

} // namespace gui_forms
