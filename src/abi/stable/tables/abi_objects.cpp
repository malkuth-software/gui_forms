// GF_TABLE_OBJECTS: controls, references and the tree. See gf_abi.h.

#include "abi_tables.hpp"

#include "../registry/abi_registry.hpp"

#include "../schema/abi_create_with.hpp"
#include "../schema/abi_schema.hpp"
#include "../windows/abi_window_handle.hpp"

#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/controls/docking/dock_workspace/dock_workspace.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms::abi1 {
namespace {

// The slot indices of gf_objects_table, for failure records.
constexpr SlotIdentity create_slot{GF_TABLE_OBJECTS, 0U};
constexpr SlotIdentity retain_slot{GF_TABLE_OBJECTS, 1U};
constexpr SlotIdentity release_slot{GF_TABLE_OBJECTS, 2U};
constexpr SlotIdentity dispose_slot{GF_TABLE_OBJECTS, 3U};
constexpr SlotIdentity state_slot{GF_TABLE_OBJECTS, 4U};
constexpr SlotIdentity type_id_slot{GF_TABLE_OBJECTS, 5U};
constexpr SlotIdentity stable_id_slot{GF_TABLE_OBJECTS, 6U};
constexpr SlotIdentity insert_child_slot{GF_TABLE_OBJECTS, 7U};
constexpr SlotIdentity remove_child_slot{GF_TABLE_OBJECTS, 8U};
constexpr SlotIdentity child_count_slot{GF_TABLE_OBJECTS, 9U};
constexpr SlotIdentity child_at_slot{GF_TABLE_OBJECTS, 10U};
constexpr SlotIdentity parent_slot{GF_TABLE_OBJECTS, 11U};
constexpr SlotIdentity create_with_slot{GF_TABLE_OBJECTS, 12U};

// The 1.0 size of gf_create_arguments.
constexpr std::uint32_t create_arguments_size_1_0 = 56U;
static_assert(sizeof(gf_create_arguments) == create_arguments_size_1_0);

gf_status require_alive(const Control& control, SlotIdentity slot) noexcept {
    if (!control.is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the object has been disposed");
    }
    return GF_STATUS_OK;
}

// The live control behind object, on the UI thread.
gf_status enter_with_object(gf_object object, SlotIdentity slot, Control::Ptr& control) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const gf_status found = registry().find_object(object, slot, control);
    return found;
}

// The component (a control among them) behind object, on the UI thread.
gf_status enter_with_component(gf_object object, SlotIdentity slot, Component::Ptr& component) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const gf_status found = registry().find_component(object, slot, component);
    return found;
}

// Reads a type name and a stable id as create and create_with take them;
// type receives the catalog index.
gf_status read_type_and_id(gf_utf8_view type_name, gf_utf8_view stable_id, SlotIdentity slot, std::size_t& type,
                           std::string_view& id_text) {
    std::string_view type_text;
    const gf_status type_read = read_text(type_name, slot, type_text);
    if (type_read != GF_STATUS_OK) {
        return type_read;
    }
    const gf_status id_read = read_text(stable_id, slot, id_text);
    if (id_read != GF_STATUS_OK) {
        return id_read;
    }
    if (id_text.empty() || id_text.find('\0') != std::string_view::npos) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a stable id is non-empty and has no NUL byte");
    }
    if (type_text.empty()) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a type name is not empty");
    }
    type = find_catalog_type(type_text);
    if (type == catalog_type_count()) {
        return fail(GF_STATUS_NOT_FOUND, slot, "the schema has no type of that name");
    }
    return GF_STATUS_OK;
}

gf_status GF_CALL objects_create(gf_utf8_view type_name, gf_utf8_view stable_id, gf_object* object) noexcept {
    try {
        const ShadowFrameScope shadow_frame(create_slot.table_id, create_slot.slot_index);
        if (object == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "create needs an object output");
        }
        std::size_t type = 0U;
        std::string_view id_text;
        const gf_status read = read_type_and_id(type_name, stable_id, create_slot, type, id_text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const gf_status entered = registry().enter_ui(create_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const Component::Ptr made = create_catalog_type(type, StableId(std::string(id_text)));
        if (made == nullptr) {
            // Bound for this call alone when nothing else holds the thread.
            registry().unbind_if_idle();
            return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, create_slot,
                        "create makes controls from a stable id and components from nothing; this type needs "
                        "another object or a window (create_with)");
        }
        *object = registry().add_reference(made);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(create_slot);
    }
}

gf_status GF_CALL objects_retain(gf_object object) noexcept {
    try {
        const ShadowFrameScope shadow_frame(retain_slot.table_id, retain_slot.slot_index);
        const gf_status retained = registry().retain(object, retain_slot);
        return retained;
    } catch (...) {
        return fail_with_current_exception(retain_slot);
    }
}

gf_status GF_CALL objects_release(gf_object object) noexcept {
    try {
        const ShadowFrameScope shadow_frame(release_slot.table_id, release_slot.slot_index);
        const gf_status released = registry().release(object, release_slot);
        return released;
    } catch (...) {
        return fail_with_current_exception(release_slot);
    }
}

gf_status GF_CALL objects_dispose(gf_object object) noexcept {
    try {
        const ShadowFrameScope shadow_frame(dispose_slot.table_id, dispose_slot.slot_index);
        Component::Ptr component;
        const gf_status entered = enter_with_component(object, dispose_slot, component);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*component).dispose();
        registry().end_disposed_subscriptions();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(dispose_slot);
    }
}

// A control's stable id; a component's own id when its type has one, else
// empty.
std::string_view stable_id_of(const Component& component) noexcept {
    const Control* control = dynamic_cast<const Control*>(&component);
    if (control != nullptr) {
        return (*control).stable_id().value();
    }
    const ContextMenu* menu = dynamic_cast<const ContextMenu*>(&component);
    if (menu != nullptr) {
        return (*menu).stable_id();
    }
    const DockWorkspace* workspace = dynamic_cast<const DockWorkspace*>(&component);
    if (workspace != nullptr) {
        return (*workspace).stable_id();
    }
    const WindowHandle* window = dynamic_cast<const WindowHandle*>(&component);
    if (window != nullptr) {
        return (*window).stable_id();
    }
    return std::string_view{};
}

gf_object_state object_state_of(ComponentState state) noexcept {
    switch (state) {
    case ComponentState::alive:
        return GF_OBJECT_STATE_ALIVE;
    case ComponentState::disposing:
        return GF_OBJECT_STATE_DISPOSING;
    case ComponentState::disposed:
        return GF_OBJECT_STATE_DISPOSED;
    }
    return GF_OBJECT_STATE_DISPOSED;
}

gf_status GF_CALL objects_state(gf_object object, gf_object_state* state) noexcept {
    try {
        const ShadowFrameScope shadow_frame(state_slot.table_id, state_slot.slot_index);
        if (state == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, state_slot, "state needs an output");
        }
        Component::Ptr component;
        const gf_status entered = enter_with_component(object, state_slot, component);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *state = object_state_of((*component).component_state());
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(state_slot);
    }
}

gf_status GF_CALL objects_type_id(gf_object object, char* buffer, std::uint64_t capacity,
                                  std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(type_id_slot.table_id, type_id_slot.slot_index);
        Component::Ptr component;
        const gf_status entered = enter_with_component(object, type_id_slot, component);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        // A window is not a schema type; its id is the windows table's.
        const bool window = dynamic_cast<const WindowHandle*>(component.get()) != nullptr;
        const std::string_view id = window ? std::string_view("window") : catalog_type_id_of(*component);
        const gf_status written = write_text(id, buffer, capacity, required, type_id_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(type_id_slot);
    }
}

gf_status GF_CALL objects_stable_id(gf_object object, char* buffer, std::uint64_t capacity,
                                    std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(stable_id_slot.table_id, stable_id_slot.slot_index);
        Component::Ptr component;
        const gf_status entered = enter_with_component(object, stable_id_slot, component);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::string_view id = stable_id_of(*component);
        const gf_status written = write_text(id, buffer, capacity, required, stable_id_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(stable_id_slot);
    }
}

gf_status GF_CALL objects_insert_child(gf_object parent, gf_object child, std::uint64_t index) noexcept {
    try {
        const ShadowFrameScope shadow_frame(insert_child_slot.table_id, insert_child_slot.slot_index);
        Control::Ptr parent_control;
        Control::Ptr child_control;
        const gf_status entered = enter_with_object(parent, insert_child_slot, parent_control);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status found = registry().find_object(child, insert_child_slot, child_control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        if (require_alive(*parent_control, insert_child_slot) != GF_STATUS_OK ||
            require_alive(*child_control, insert_child_slot) != GF_STATUS_OK) {
            return GF_STATUS_DISPOSED;
        }
        if ((*child_control).parent()) {
            return fail(GF_STATUS_INVALID_STATE, insert_child_slot, "the child already has a parent");
        }
        Control::Ptr ancestor = parent_control;
        while (ancestor) {
            if (ancestor == child_control) {
                return fail(GF_STATUS_INVALID_ARGUMENT, insert_child_slot, "a control cannot be its own ancestor");
            }
            ancestor = (*ancestor).parent();
        }
        const std::uint64_t count = (*parent_control).children().size();
        if (index > count) {
            return fail(GF_STATUS_INVALID_ARGUMENT, insert_child_slot, "the index is past the child count");
        }
        (*parent_control).add_child(child_control);
        if (index != count) {
            // add_child puts the child last (on top). set_child_index counts
            // from the top, as WinForms does: position index among count + 1
            // children is top index count - index.
            const std::size_t from_top = static_cast<std::size_t>(count - index);
            static_cast<void>((*parent_control).set_child_index((*child_control).runtime_id(), from_top));
        }
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(insert_child_slot);
    }
}

gf_status GF_CALL objects_remove_child(gf_object parent, gf_object child) noexcept {
    try {
        const ShadowFrameScope shadow_frame(remove_child_slot.table_id, remove_child_slot.slot_index);
        Control::Ptr parent_control;
        Control::Ptr child_control;
        const gf_status entered = enter_with_object(parent, remove_child_slot, parent_control);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const gf_status found = registry().find_object(child, remove_child_slot, child_control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        if (require_alive(*parent_control, remove_child_slot) != GF_STATUS_OK) {
            return GF_STATUS_DISPOSED;
        }
        if ((*child_control).parent() != parent_control) {
            return fail(GF_STATUS_INVALID_ARGUMENT, remove_child_slot, "the child is not a child of the parent");
        }
        // The caller's reference keeps the removed child; it ends when that
        // reference is released.
        static_cast<void>((*parent_control).remove_child((*child_control).runtime_id()));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(remove_child_slot);
    }
}

gf_status GF_CALL objects_child_count(gf_object parent, std::uint64_t* count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(child_count_slot.table_id, child_count_slot.slot_index);
        if (count == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, child_count_slot, "child_count needs an output");
        }
        Control::Ptr control;
        const gf_status entered = enter_with_object(parent, child_count_slot, control);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *count = (*control).children().size();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(child_count_slot);
    }
}

gf_status GF_CALL objects_child_at(gf_object parent, std::uint64_t index, gf_object* child) noexcept {
    try {
        const ShadowFrameScope shadow_frame(child_at_slot.table_id, child_at_slot.slot_index);
        if (child == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, child_at_slot, "child_at needs an output");
        }
        Control::Ptr control;
        const gf_status entered = enter_with_object(parent, child_at_slot, control);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::span<const Control::Ptr> children = (*control).children();
        if (index >= children.size()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, child_at_slot, "the index is past the last child");
        }
        *child = registry().add_reference(children[static_cast<std::size_t>(index)]);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(child_at_slot);
    }
}

gf_status GF_CALL objects_parent(gf_object object, gf_object* parent) noexcept {
    try {
        const ShadowFrameScope shadow_frame(parent_slot.table_id, parent_slot.slot_index);
        if (parent == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, parent_slot, "parent needs an output");
        }
        Control::Ptr control;
        const gf_status entered = enter_with_object(object, parent_slot, control);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const Control::Ptr parent_control = (*control).parent();
        *parent = parent_control ? registry().add_reference(parent_control) : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(parent_slot);
    }
}

gf_status GF_CALL objects_create_with(gf_utf8_view type_name, gf_utf8_view stable_id,
                                      const gf_create_arguments* arguments, gf_object* object) noexcept {
    try {
        const ShadowFrameScope shadow_frame(create_with_slot.table_id, create_with_slot.slot_index);
        if (object == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, create_with_slot, "create_with needs an object output");
        }
        if (arguments == nullptr || (*arguments).struct_size < create_arguments_size_1_0) {
            return fail(GF_STATUS_INVALID_ARGUMENT, create_with_slot,
                        "create_with needs gf_create_arguments of at least the 1.0 size");
        }
        if (((*arguments).flags & ~static_cast<gf_create_flags>(GF_CREATE_FLAGS_COMBINABLE)) != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, create_with_slot, "unknown gf_create_flags bits are set");
        }
        CreateRequest request{};
        std::string_view id_text;
        const gf_status read = read_type_and_id(type_name, stable_id, create_with_slot, request.type, id_text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const gf_status read_choices = read_enumeration((*arguments).enumeration, (*arguments).choices, (*arguments).choice_count,
                             ((*arguments).flags & GF_CREATE_FLAGS_COMBINABLE) != 0U, create_with_slot,
                             request.enumeration);
        if (read_choices != GF_STATUS_OK) {
            return read_choices;
        }
        request.value = (*arguments).value;
        const gf_status entered = registry().enter_ui(create_with_slot, true);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if ((*arguments).owner != 0U) {
            const gf_status found = registry().find_component((*arguments).owner, create_with_slot, request.owner);
            if (found != GF_STATUS_OK) {
                registry().unbind_if_idle();
                return found;
            }
        }
        Component::Ptr made;
        const gf_status created =
            create_from_request(request, StableId(std::string(id_text)), create_with_slot, made);
        if (created != GF_STATUS_OK) {
            registry().unbind_if_idle();
            return created;
        }
        *object = registry().add_reference(made);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(create_with_slot);
    }
}

constexpr gf_objects_table table{
    .struct_size = sizeof(gf_objects_table),
    .abi_minor = GF_ABI_MINOR,
    .create = &objects_create,
    .retain = &objects_retain,
    .release = &objects_release,
    .dispose = &objects_dispose,
    .state = &objects_state,
    .type_id = &objects_type_id,
    .stable_id = &objects_stable_id,
    .insert_child = &objects_insert_child,
    .remove_child = &objects_remove_child,
    .child_count = &objects_child_count,
    .child_at = &objects_child_at,
    .parent = &objects_parent,
    .create_with = &objects_create_with,
};

} // namespace

const gf_objects_table& objects_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
