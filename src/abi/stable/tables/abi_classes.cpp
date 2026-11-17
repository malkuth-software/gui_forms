// GF_TABLE_CLASSES (since 1.3): objects made from class tables. See
// gf_abi_classes.h and ../classes/abi_class_bridge.hpp.

#include "abi_tables.hpp"

#include "../classes/abi_class_bridge.hpp"
#include "../registry/abi_registry.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity create_slot{GF_TABLE_CLASSES, 0U};
constexpr SlotIdentity base_raiser_slot{GF_TABLE_CLASSES, 1U};

// The description of class_id, null when it has no class table.
const ClassDescription* find_description(gf_class_id class_id) noexcept {
    const std::span<const ClassDescription> all = class_descriptions();
    std::size_t index = 0U;
    for (index = 0U; index < all.size(); ++index) {
        if (all[index].class_id == class_id) {
            return &all[index];
        }
    }
    return nullptr;
}

// enter_ui bound the thread for this call once *entered_ui is set.
gf_status classes_create_body(const gf_class_header* table, gf_utf8_view stable_id, void* instance, gf_object* object,
                              bool* entered_ui) {
    if (object == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "create needs an object output");
    }
    if (table == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, "create needs a class table");
    }
    // The header's size and minor are checked before its class is read.
    if ((*table).struct_size < class_header_size_1_3 || (*table).struct_size % 8U != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot,
                    "a class table's header is at least the 1.3 gf_class_header, a multiple of 8 bytes");
    }
    if ((*table).abi_minor > GF_ABI_MINOR) {
        return fail(GF_STATUS_UNSUPPORTED_VERSION, create_slot, "the class table is newer than this library");
    }
    const ClassDescription* description = find_description((*table).class_id);
    if (description == nullptr) {
        return fail(GF_STATUS_NOT_FOUND, create_slot, "no class table has this class id");
    }
    ClassTableView view{};
    const gf_status validated = ClassTableView::read(table, (*description).segments, create_slot, view);
    if (validated != GF_STATUS_OK) {
        return validated;
    }
    std::string_view id_text;
    const gf_status id_read = read_text(stable_id, create_slot, id_text);
    if (id_read != GF_STATUS_OK) {
        return id_read;
    }
    if (!valid_stable_id(id_text)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, create_slot, stable_id_rule);
    }
    const gf_status entered = registry().enter_ui(create_slot, true);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    *entered_ui = true;
    // The bridge is armed only once the registry holds it: a throw before
    // then (construction, add_reference's storage) calls no release, and
    // the caller still owns instance (A-gf-abi-01-F6).
    const Control::Ptr made = (*(*description).create)(StableId(std::string(id_text)), view, instance);
    if (made == nullptr) {
        registry().unbind_if_idle();
        return fail(GF_STATUS_UNSUPPORTED_CAPABILITY, create_slot,
                    "this class is not made from a stable id alone; it has no class table object yet");
    }
    const gf_object identity = registry().add_reference(made);
    ClassBridge* const bridge = dynamic_cast<ClassBridge*>(made.get());
    if (bridge != nullptr) (*bridge).arm_class_table();
    *object = identity;
    return GF_STATUS_OK;
}

gf_status GF_CALL classes_create(const gf_class_header* table, gf_utf8_view stable_id, void* instance,
                                 gf_object* object) noexcept {
    bool entered_ui = false;
    bool* const entered_flag = &entered_ui;
    const gf_status status =
        call_slot(create_slot, classes_create_body, table, stable_id, instance, object, entered_flag);
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

gf_status classes_base_raiser_body(gf_class_id class_id, std::uint32_t entry, gf_base_raiser* raiser) {
    if (raiser == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, base_raiser_slot, "base_raiser needs an output");
    }
    const gf_base_raiser found = find_base_raiser(class_id, entry);
    if (found == nullptr) {
        return fail(GF_STATUS_NOT_FOUND, base_raiser_slot, "no segment has this class id and entry");
    }
    *raiser = found;
    return GF_STATUS_OK;
}

gf_status GF_CALL classes_base_raiser(gf_class_id class_id, std::uint32_t entry, gf_base_raiser* raiser) noexcept {
    return call_slot(base_raiser_slot, classes_base_raiser_body, class_id, entry, raiser);
}

constexpr gf_classes_table table{
    .struct_size = static_cast<std::uint32_t>(sizeof(gf_classes_table)),
    .abi_minor = GF_ABI_MINOR,
    .create = &classes_create,
    .base_raiser = &classes_base_raiser,
};

} // namespace

const gf_classes_table& classes_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
