// GF_TABLE_CUSTOM_CONTROLS: controls the caller defines over a stock base.
// See gf_abi.h and ../custom/abi_custom_control.hpp.

#include "abi_control_entry.hpp"
#include "abi_tables.hpp"

#include "../custom/abi_composite.hpp"
#include "../custom/abi_custom_control.hpp"
#include "../schema/abi_create_with.hpp"
#include "../schema/abi_schema.hpp"
#include "../values/abi_values.hpp"

#include "gui_forms/composites/composite_control/composite_control.hpp"
#include "gui_forms/schema.hpp"

#include "../../../core/control_language/composite_definition/composite_definition.hpp"
#include "../ic/abi_custom_controls_ic_checks.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity create_slot{GF_TABLE_CUSTOM_CONTROLS, 0U};
constexpr SlotIdentity notify_changed_slot{GF_TABLE_CUSTOM_CONTROLS, 1U};
// Since 1.5: composite controls.
constexpr SlotIdentity load_composite_slot{GF_TABLE_CUSTOM_CONTROLS, 2U};
constexpr SlotIdentity create_composite_slot{GF_TABLE_CUSTOM_CONTROLS, 3U};
constexpr SlotIdentity perform_composite_slot{GF_TABLE_CUSTOM_CONTROLS, 4U};
constexpr SlotIdentity composite_child_slot{GF_TABLE_CUSTOM_CONTROLS, 5U};

constexpr std::uint32_t spec_size_1_0 = ic_custom_controls::spec_size_1_0;
constexpr std::uint32_t callbacks_size_1_0 = 48U;
constexpr std::uint32_t property_size_1_0 = 96U;
constexpr std::uint32_t paint_target_size_1_0 = 72U;
static_assert(sizeof(gf_custom_control_callbacks) == callbacks_size_1_0);
// The 1.2 spec adds the layout pointer (K11); the layout record's first
// size holds measure, hit_test and semantics.
constexpr std::uint32_t spec_size_1_2 = 88U;
static_assert(sizeof(gf_custom_control_spec) == spec_size_1_2);
constexpr std::uint32_t layout_size_1_2 = 32U;
static_assert(sizeof(gf_custom_layout_callbacks) == layout_size_1_2);
static_assert(sizeof(gf_custom_property) == property_size_1_0);
static_assert(sizeof(gf_paint_target) == paint_target_size_1_0);
// The 1.5 composite record: two sizes, two views, the types and their count.
constexpr std::uint32_t composite_spec_size_1_5 = 56U;
static_assert(sizeof(gf_composite_spec) == composite_spec_size_1_5);
// A document a composite is loaded from, at most (a `.control` file is a
// few kilobytes; the generated header embeds at most 60,000 bytes).
constexpr std::size_t maximum_composite_document_bytes = 1048576U;

constexpr std::size_t maximum_text_bytes = 256U;
// Descriptions are prose for the property grid; longer is not useful.
constexpr std::size_t maximum_description_bytes = 4096U;

constexpr gf_custom_property_flags known_property_flags =
    GF_CUSTOM_PROPERTY_READ_ONLY | GF_CUSTOM_PROPERTY_COMBINABLE | GF_CUSTOM_PROPERTY_CHANGE_NOTIFICATIONS;

// The property kind of a gf_value_kind a custom property may declare: every
// kind but NULL, OBJECT and COLLECTION. The numbering is shared
// (value_kind_of is its inverse).
bool custom_property_kind(gf_value_kind kind, BindingValueKind& converted) noexcept {
    if (kind == GF_VALUE_KIND_NULL || kind == GF_VALUE_KIND_OBJECT || kind >= GF_VALUE_KIND_COLLECTION) {
        return false;
    }
    converted = static_cast<BindingValueKind>(kind);
    return value_kind_of(converted) == kind;
}

gf_status read_bounded_text(gf_utf8_view view, std::size_t maximum, const char* refusal, std::string& text) {
    std::string_view read;
    const gf_status status = read_text(view, create_slot, read);
    if (status != GF_STATUS_OK) {
        return status;
    }
    if (read.size() > maximum) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, refusal);
    }
    text = std::string(read);
    return GF_STATUS_OK;
}

// One caller property, read into a definition; names distinct from the
// definitions read before it.
gf_status read_property(const gf_custom_property& property, const std::vector<CustomPropertyDefinition>& earlier,
                        CustomPropertyDefinition& definition) {
    if ((property.flags & ~known_property_flags) != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "unknown gf_custom_property_flags bits are set");
    }
    std::string name;
    const gf_status named =
        read_bounded_text(property.name, maximum_text_bytes, "a property's name has 1 to 256 bytes", name);
    if (named != GF_STATUS_OK) {
        return named;
    }
    if (name.empty()) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "a property's name has 1 to 256 bytes");
    }
    const std::string canonical = registered_name(name);
    if (!is_registered_name(canonical)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "a property's name is not a registered name");
    }
    for (const CustomPropertyDefinition& previous : earlier) {
        if (previous.descriptor.name == canonical) {
            return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "a custom control's property names are distinct");
        }
    }
    BindingValueKind kind = BindingValueKind::null;
    if (!custom_property_kind(property.kind, kind)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "a custom property's kind is a gf_value_kind other than NULL, OBJECT and COLLECTION");
    }
    PropertyDescriptor descriptor{};
    descriptor.name = canonical;
    descriptor.kind = kind;
    const gf_status categorized =
        read_bounded_text(property.category, maximum_text_bytes, "a property's category has at most 256 bytes",
                          descriptor.category);
    if (categorized != GF_STATUS_OK) {
        return categorized;
    }
    const gf_status described = read_bounded_text(property.description, maximum_description_bytes,
                                                  "a property's description has at most 4096 bytes",
                                                  descriptor.description);
    if (described != GF_STATUS_OK) {
        return described;
    }
    std::optional<PropertyEnumDescriptor> enumeration;
    const gf_status enumerated =
        read_enumeration(property.enumeration, property.choices, property.choice_count,
                         (property.flags & GF_CUSTOM_PROPERTY_COMBINABLE) != 0U, create_slot, enumeration);
    if (enumerated != GF_STATUS_OK) {
        return enumerated;
    }
    if (enumeration.has_value() != (kind == BindingValueKind::enumeration)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "an ENUMERATION property names its enumeration and choices; another kind has none");
    }
    if (enumeration) {
        descriptor.enumeration = std::make_shared<const PropertyEnumDescriptor>(std::move(*enumeration));
    }
    descriptor.writable = (property.flags & GF_CUSTOM_PROPERTY_READ_ONLY) == 0U;
    if (property.default_value != nullptr) {
        BindingValue read;
        const gf_status valued = read_value(property.default_value, create_slot, read);
        if (valued != GF_STATUS_OK) {
            return valued;
        }
        const std::optional<BindingValue> converted = convert_property_value(read, descriptor);
        if (!converted) {
            return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "a property's default is not a value it holds");
        }
        descriptor.default_value = *converted;
    }
    definition.descriptor = std::move(descriptor);
    definition.change_notifications = (property.flags & GF_CUSTOM_PROPERTY_CHANGE_NOTIFICATIONS) != 0U;
    return GF_STATUS_OK;
}

// The spec, validated whole before anything is made.
gf_status read_spec(const gf_custom_control_spec& spec, std::vector<CustomPropertyDefinition>& definitions) {
    if (spec.base != GF_CUSTOM_BASE_SCROLLABLE_CONTROL && spec.base != GF_CUSTOM_BASE_USER_CONTROL) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "the base is not a gf_custom_base");
    }
    if ((spec.flags & ~static_cast<gf_custom_control_flags>(GF_CUSTOM_CONTROL_FOCUSABLE)) != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "unknown gf_custom_control_flags bits are set");
    }
    const gf_custom_control_callbacks& callbacks = spec.callbacks;
    if (callbacks.struct_size < callbacks_size_1_0 || callbacks.reserved != 0U || callbacks.paint == nullptr ||
        callbacks.get_property == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "the callbacks need struct_size of at least 48, reserved zero, paint and get_property");
    }
    if (spec.property_count > GF_CUSTOM_MAXIMUM_PROPERTIES ||
        (spec.property_count != 0U && (spec.properties == nullptr || spec.property_size < property_size_1_0))) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "at most 256 properties, of property_size at least 96 bytes when there are any");
    }
    definitions.reserve(static_cast<std::size_t>(spec.property_count));
    bool writable = false;
    const unsigned char* cursor = reinterpret_cast<const unsigned char*>(spec.properties);
    for (std::uint64_t index = 0U; index < spec.property_count; ++index) {
        // property_size is the caller's stride: a later minor's record is
        // longer, and this library reads its 1.0 prefix.
        const gf_custom_property* property = reinterpret_cast<const gf_custom_property*>(cursor);
        CustomPropertyDefinition definition{};
        const gf_status read = read_property(*property, definitions, definition);
        if (read != GF_STATUS_OK) {
            return read;
        }
        writable = writable || definition.descriptor.writable;
        definitions.push_back(std::move(definition));
        cursor += spec.property_size;
    }
    if (writable && callbacks.set_property == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "a writable property needs set_property");
    }
    return GF_STATUS_OK;
}

// enter_ui bound the thread for this call once *entered_ui is set.
gf_status custom_controls_create_body(const gf_custom_control_spec* spec, gf_utf8_view stable_id, gf_object* object,
                                      bool* entered_ui) {
    if (!ic_custom_controls::admits_create_outputs(spec, object) ||
        !ic_custom_controls::admits_create_spec_size((*spec).struct_size)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "create needs a gf_custom_control_spec of at least the 1.0 size and an object output");
    }
    std::string_view id_text;
    const gf_status read_id = read_text(stable_id, create_slot, id_text);
    if (read_id != GF_STATUS_OK) {
        return read_id;
    }
    if (!valid_stable_id(id_text)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, stable_id_rule);
    }
    std::vector<CustomPropertyDefinition> definitions;
    const gf_status read = read_spec(*spec, definitions);
    if (read != GF_STATUS_OK) {
        return read;
    }
    // The layout record (since 1.2), read only when the spec covers its
    // pointer, checked before anything is made.
    const gf_custom_layout_callbacks* layout_given = nullptr;
    if ((*spec).struct_size >= spec_size_1_2) {
        layout_given = (*spec).layout;
    }
    if (layout_given != nullptr &&
        ((*layout_given).struct_size < layout_size_1_2 || (*layout_given).struct_size % 8U != 0U ||
         (*layout_given).reserved != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "the layout callbacks need struct_size of at least 32, a multiple of 8, and reserved zero");
    }
    const gf_status entered = registry().enter_ui(create_slot, true);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    *entered_ui = true;
    // The control copies the callbacks at their 1.0 size; until it
    // exists, a failure calls none of them, release among them. Only
    // the bytes the caller declared are read (N14): a later minor's
    // larger record leaves an older caller's fields zero, not read past
    // its record.
    gf_custom_control_callbacks callbacks{};
    const std::size_t declared = static_cast<std::size_t>((*spec).callbacks.struct_size);
    const std::size_t copied = declared < sizeof(gf_custom_control_callbacks) ? declared
                                                                               : sizeof(gf_custom_control_callbacks);
    std::memcpy(&callbacks, &(*spec).callbacks, copied);
    callbacks.struct_size = callbacks_size_1_0;
    gf_custom_layout_callbacks layout{};
    layout.struct_size = layout_size_1_2;
    if (layout_given != nullptr) {
        // A later minor's larger record is read up to this one's size.
        const std::size_t layout_declared = static_cast<std::size_t>((*layout_given).struct_size);
        const std::size_t layout_copied = layout_declared < sizeof(gf_custom_layout_callbacks)
                                              ? layout_declared
                                              : sizeof(gf_custom_layout_callbacks);
        std::memcpy(&layout, layout_given, layout_copied);
        layout.struct_size = layout_size_1_2;
    }
    const bool focusable = ((*spec).flags & GF_CUSTOM_CONTROL_FOCUSABLE) != 0U;
    // Release is armed only once the registry holds the control: a throw
    // before then (construction, add_reference's storage) destroys it
    // without calling release, and the caller still owns its context
    // (A-gf-abi-03-F6).
    if ((*spec).base == GF_CUSTOM_BASE_USER_CONTROL) {
        const std::shared_ptr<CustomControl<UserControl>> made = std::make_shared<CustomControl<UserControl>>(
            StableId(std::string(id_text)), callbacks, layout, definitions, focusable);
        const gf_object identity = registry().add_reference(made);
        (*made).arm_release();
        *object = identity;
    } else {
        const std::shared_ptr<CustomControl<ScrollableControl>> made =
            std::make_shared<CustomControl<ScrollableControl>>(StableId(std::string(id_text)), callbacks, layout,
                                                               definitions, focusable);
        const gf_object identity = registry().add_reference(made);
        (*made).arm_release();
        *object = identity;
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL custom_controls_create(const gf_custom_control_spec* spec, gf_utf8_view stable_id,
                                         gf_object* object) noexcept {
    bool entered_ui = false;
    bool* const entered_flag = &entered_ui;
    const gf_status status =
        call_slot(create_slot, custom_controls_create_body, spec, stable_id, object, entered_flag);
    if (status != GF_STATUS_OK && entered_ui) {
        // A failure after enter_ui bound the thread for this call must not
        // leave the library bound to it (N10). Nothing to undo otherwise.
        try {
            registry().unbind_if_idle();
        } catch (...) {
        }
    }
    return status;
}

gf_status custom_controls_notify_changed_body(gf_object object, std::uint64_t index) {
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, notify_changed_slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    if (index > GF_CUSTOM_MAXIMUM_PROPERTIES) {
        return fail(GF_STATUS_INVALID_ARGUMENT, notify_changed_slot, "the index is past the properties");
    }
    const CustomNotifyResult result = notify_custom_changed(*control, static_cast<std::size_t>(index));
    if (result == CustomNotifyResult::not_custom) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, notify_changed_slot, "the control is not a custom control");
    }
    if (result == CustomNotifyResult::not_notifying) {
        return fail(GF_STATUS_INVALID_ARGUMENT, notify_changed_slot,
                    "the property does not exist or was declared without change notifications");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL custom_controls_notify_changed(gf_object object, std::uint64_t index) noexcept {
    return call_slot(notify_changed_slot, custom_controls_notify_changed_body, object, index);
}

// -- composite controls (since 1.5) ------------------------------------------

// The caller's child type names, read whole before anything is made.
gf_status read_child_type_names(const gf_composite_spec& spec, std::vector<std::string_view>& names) {
    if (spec.child_type_count > GF_COMPOSITE_MAXIMUM_CHILD_TYPES ||
        (spec.child_type_count != 0U && spec.child_types == nullptr)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, load_composite_slot,
                    "at most 64 child types, and child_types when there are any");
    }
    names.reserve(static_cast<std::size_t>(spec.child_type_count));
    for (std::uint64_t index = 0U; index < spec.child_type_count; ++index) {
        std::string_view name;
        const gf_status read = read_text(spec.child_types[index], load_composite_slot, name);
        if (read != GF_STATUS_OK) {
            return read;
        }
        if (name.empty() || name.size() > maximum_text_bytes) {
            return fail(GF_STATUS_INVALID_ARGUMENT, load_composite_slot, "a child type has 1 to 256 bytes");
        }
        names.push_back(name);
    }
    return GF_STATUS_OK;
}

// The refusal's diagnostics, one line each, as the failure's message.
std::string joined_diagnostics(const std::vector<std::string>& diagnostics) {
    std::string joined = "the document is refused";
    for (const std::string& line : diagnostics) {
        joined += '\n';
        joined += line;
    }
    return joined;
}

gf_status load_definition(const gf_composite_spec& spec, gf_object& definition) {
    if (spec.reserved != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, load_composite_slot, "reserved is zero");
    }
    std::string_view file;
    const gf_status read_file = read_text(spec.file, load_composite_slot, file);
    if (read_file != GF_STATUS_OK) {
        return read_file;
    }
    std::string_view document;
    const gf_status read_document = read_text(spec.document, load_composite_slot, document);
    if (read_document != GF_STATUS_OK) {
        return read_document;
    }
    if (file.size() > maximum_text_bytes || document.empty() || document.size() > maximum_composite_document_bytes) {
        return fail(GF_STATUS_INVALID_ARGUMENT, load_composite_slot,
                    "file has at most 256 bytes and the document 1 byte to 1 MiB");
    }
    std::vector<std::string_view> names;
    const gf_status named = read_child_type_names(spec, names);
    if (named != GF_STATUS_OK) {
        return named;
    }
    const gf_status entered = registry().enter_ui(load_composite_slot, true);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    CompositeChildTypesRead types{};
    read_composite_child_types(names, types);
    if (types.refused) {
        std::string refusal(types.refusal);
        refusal += ": ";
        refusal += names[types.refused_index];
        return fail(GF_STATUS_INVALID_ARGUMENT, load_composite_slot, refusal);
    }
    const CompositeLoadResult loaded = load_composite_definition(file, document, types.types);
    if (!loaded) {
        return fail(GF_STATUS_INVALID_ARGUMENT, load_composite_slot, joined_diagnostics(loaded.diagnostics));
    }
    // Listed in the schema table while the handle holds it; a throw after
    // the listing destroys the handle, which forgets it.
    const std::uint64_t listing = record_loaded_composite(loaded.definition);
    std::shared_ptr<CompositeDefinitionHandle> handle;
    try {
        handle = std::make_shared<CompositeDefinitionHandle>(loaded.definition, listing);
    } catch (...) {
        forget_loaded_composite(listing);
        throw;
    }
    definition = registry().add_reference(handle);
    return GF_STATUS_OK;
}

gf_status custom_controls_load_composite_body(const gf_composite_spec* spec, gf_object* definition) {
    if (spec == nullptr || definition == nullptr || (*spec).struct_size < composite_spec_size_1_5) {
        return fail(GF_STATUS_INVALID_ARGUMENT, load_composite_slot,
                    "load_composite needs a gf_composite_spec of at least the 1.5 size and a definition output");
    }
    const gf_status loaded = load_definition(*spec, *definition);
    return loaded;
}

gf_status GF_CALL custom_controls_load_composite(const gf_composite_spec* spec, gf_object* definition) noexcept {
    const gf_status status = call_slot(load_composite_slot, custom_controls_load_composite_body, spec, definition);
    if (status != GF_STATUS_OK) {
        // As create: a failure after enter_ui leaves no binding (N10);
        // unbind_if_idle does nothing while anything else holds the thread.
        try {
            registry().unbind_if_idle();
        } catch (...) {
        }
    }
    return status;
}

gf_status make_composite(gf_object definition, gf_utf8_view stable_id, gf_object& object) {
    std::string_view id_text;
    const gf_status read_id = read_text(stable_id, create_composite_slot, id_text);
    if (read_id != GF_STATUS_OK) {
        return read_id;
    }
    if (!valid_stable_id(id_text)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_composite_slot, stable_id_rule);
    }
    const gf_status entered = registry().enter_ui(create_composite_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr found;
    const gf_status looked = registry().find_component(definition, create_composite_slot, found);
    if (looked != GF_STATUS_OK) {
        return looked;
    }
    const std::shared_ptr<CompositeDefinitionHandle> handle =
        std::dynamic_pointer_cast<CompositeDefinitionHandle>(found);
    if (!handle) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, create_composite_slot, "create_composite takes a definition");
    }
    if (!(*handle).is_alive() || (*handle).definition() == nullptr) {
        return fail(GF_STATUS_DISPOSED, create_composite_slot, "the definition has been disposed");
    }
    const Control::Ptr made = make_control<CompositeControl>(StableId(std::string(id_text)), (*handle).definition());
    object = registry().add_reference(made);
    return GF_STATUS_OK;
}

gf_status custom_controls_create_composite_body(gf_object definition, gf_utf8_view stable_id,
                                                gf_object* object) {
    if (!ic_custom_controls::admits_create_composite_object(object)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_composite_slot, "create_composite needs an object output");
    }
    const gf_status made = make_composite(definition, stable_id, *object);
    return made;
}

gf_status GF_CALL custom_controls_create_composite(gf_object definition, gf_utf8_view stable_id,
                                                   gf_object* object) noexcept {
    return call_slot(create_composite_slot, custom_controls_create_composite_body, definition, stable_id, object);
}

// The composite `object` names, entered on the UI thread.
gf_status enter_composite(gf_object object, SlotIdentity slot, std::shared_ptr<CompositeControl>& composite) {
    const gf_status entered =
        enter_typed_control<CompositeControl>(object, slot, "the control is not a composite control", composite);
    return entered;
}

gf_status perform_command(gf_object control, gf_utf8_view command, std::uint32_t* performed) {
    std::string_view name;
    const gf_status read = read_text(command, perform_composite_slot, name);
    if (read != GF_STATUS_OK) {
        return read;
    }
    std::shared_ptr<CompositeControl> composite;
    const gf_status entered = enter_composite(control, perform_composite_slot, composite);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    bool declared = false;
    for (const control_language::CompositeCommandDefinition& listed : (*composite).definition().commands) {
        if (listed.name == name) declared = true;
    }
    if (!declared) {
        return fail(GF_STATUS_NOT_FOUND, perform_composite_slot, "the composite declares no command of that name");
    }
    const bool ran = (*composite).perform(name);
    if (performed != nullptr) *performed = ran ? 1U : 0U;
    return GF_STATUS_OK;
}

gf_status custom_controls_perform_composite_body(gf_object control, gf_utf8_view command,
                                                 std::uint32_t* performed) {
    const gf_status reported = perform_command(control, command, performed);
    return reported;
}

gf_status GF_CALL custom_controls_perform_composite(gf_object control, gf_utf8_view command,
                                                    std::uint32_t* performed) noexcept {
    return call_slot(perform_composite_slot, custom_controls_perform_composite_body, control, command, performed);
}

gf_status find_child(gf_object control, gf_utf8_view local_id, gf_object& child) {
    std::string_view local;
    const gf_status read = read_text(local_id, composite_child_slot, local);
    if (read != GF_STATUS_OK) {
        return read;
    }
    std::shared_ptr<CompositeControl> composite;
    const gf_status entered = enter_composite(control, composite_child_slot, composite);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const Control::Ptr found = (*composite).child(local);
    if (!found) {
        return fail(GF_STATUS_NOT_FOUND, composite_child_slot, "the composite has no child of that local id");
    }
    child = registry().add_reference(found);
    return GF_STATUS_OK;
}

gf_status custom_controls_composite_child_body(gf_object control, gf_utf8_view local_id,
                                               gf_object* child) {
    if (!ic_custom_controls::admits_composite_child_child(child)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, composite_child_slot, "composite_child needs a child output");
    }
    const gf_status found = find_child(control, local_id, *child);
    return found;
}

gf_status GF_CALL custom_controls_composite_child(gf_object control, gf_utf8_view local_id,
                                                  gf_object* child) noexcept {
    return call_slot(composite_child_slot, custom_controls_composite_child_body, control, local_id, child);
}

constexpr gf_custom_controls_table table{
    .struct_size = sizeof(gf_custom_controls_table),
    .abi_minor = GF_ABI_MINOR,
    .create = &custom_controls_create,
    .notify_changed = &custom_controls_notify_changed,
    .load_composite = &custom_controls_load_composite,
    .create_composite = &custom_controls_create_composite,
    .perform_composite = &custom_controls_perform_composite,
    .composite_child = &custom_controls_composite_child,
};

} // namespace

const gf_custom_controls_table& custom_controls_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
