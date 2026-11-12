#pragma once

#include "gui_forms/binding/collection_cell/collection_cell.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {

// An ordered list of text items edited in place (in-place collections
// design, owner 2026-11-07; the ListBox pilot): `(*list).items.read()`,
// `(*list).items[i]`, `(*list).items = {...}`, `items.insert(i, x)`,
// `items.erase(i, n)`, `items.replace(i, x)` and `items.clear()`.
//
// A standard-layout struct whose only member is the cell, taken by the
// declaring class as a base. Each edit runs the declaring class's
// prepare_edit hook (it may refuse by throwing: nothing changes), the
// store, then its written hook. The declaring class takes the pending
// damage at its flush through take_dirty.
template <typename Declaring>
struct TextItemsCapability {
    GUI_FORMS_LOCALIZABLE(items) CollectionCell<std::string, TextItemsCapability<Declaring>> items;

    static void prepare_edit(TextItemsCapability& capability, const CollectionEdit& edit,
                             std::span<std::string> incoming) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.prepare_edit(std::as_const(capability.items), edit, incoming);
    }

    static void written(TextItemsCapability& capability, const CollectionEdit& edit) {
        Declaring& control = static_cast<Declaring&>(capability);
        control.written(std::as_const(capability.items), edit);
    }

protected:
    // The replaced positions below the watermark, appended to `replaced`,
    // and the watermark (CollectionDirty::take). For the declaring class's
    // flush.
    [[nodiscard]] static std::optional<std::size_t> take_dirty(TextItemsCapability& capability,
                                                               std::vector<std::uint32_t>& replaced) {
        const std::optional<std::size_t> structural_from = capability.items.take_dirty(replaced);
        return structural_from;
    }

    // Reorders the items in place by `order` and records every position
    // written, without hooks: for a declaring class that keeps its items
    // sorted. `order` must be a strict weak ordering whose equivalent items
    // are equal, so the result does not depend on the sort's stability;
    // std::sort allocates nothing, so this cannot fail part way.
    template <typename Order>
    static void sort_items(TextItemsCapability& capability, Order order) {
        capability.items.mark_all();
        const std::span<std::string> elements = capability.items.owner_elements();
        std::sort(elements.begin(), elements.end(), order);
    }
};

} // namespace gui_forms
