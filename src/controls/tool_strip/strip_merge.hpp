#pragma once

// Strip merging (Windows Forms' ToolStripManager.Merge and RevertMerge): the
// history a target strip keeps of the merges into it, so that a revert
// restores both strips exactly. Internal to the controls library.

#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/controls/menu_strip/menu_strip.hpp"
#include "gui_forms/controls/tool_strip/tool_strip.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gui_forms::detail {

// What one step of a merge did to the target.
enum class MergeStepKind : std::uint8_t {
    // A source item moved into the target (append, insert, and the second
    // half of replace).
    moved,
    // A target item left the target (remove, and the first half of replace).
    removed,
};

// One step of a merge. A collection is named by the stable IDs leading to it
// from the strip's top level: empty for the top level, then the item whose
// menu it is, then each submenu. Revert finds collections and items by these
// IDs and skips what has since gone.
template <typename Top>
struct MergeStep final {
    MergeStepKind kind{MergeStepKind::moved};
    std::string item_id{};
    // Where the item now is (moved) or was (removed) in the target.
    std::vector<std::string> target_path{};
    std::size_t target_index{};
    // Where a moved item was in the source.
    std::vector<std::string> source_path{};
    std::size_t source_index{};
    // A removed item, kept for the revert: a top-level item when its path
    // is empty, else a menu item.
    std::optional<Top> removed_top{};
    std::optional<MenuItemSpec> removed_item{};
};

// One merge into a target: its source, which the target does not own, and
// its steps in order.
template <typename Top>
struct StripMerge final {
    std::weak_ptr<Control> source{};
    std::vector<MergeStep<Top>> steps{};
};

struct ToolStripMergeHistory final {
    // Oldest first.
    std::vector<StripMerge<ToolStripItemSpec>> merges{};
};

struct MenuStripMergeHistory final {
    // Oldest first.
    std::vector<StripMerge<MenuStripItemSpec>> merges{};
};

} // namespace gui_forms::detail
