// Strip merging (tool_strip_manager.hpp): the merge walk, the history it
// records in the target, and the revert that replays the history backwards.
// Both strips' items are worked on as copies and applied only once the whole
// result is known, so a refused merge or revert changes nothing.

#include "gui_forms/controls/tool_strip/tool_strip_manager.hpp"

#include "strip_merge.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace gui_forms::detail {

// The private parts of the strips that merging reaches.
class StripMergeAccess final {
public:
    static void apply(ToolStrip& strip, std::vector<ToolStripItemSpec> items) {
        strip.apply_items(std::move(items), true);
    }
    static void apply(MenuStrip& strip, std::vector<MenuStripItemSpec> items) {
        strip.apply_items(std::move(items), true);
    }
    static ToolStripMergeHistory& history(ToolStrip& strip) {
        if (!strip.merge_history_) strip.merge_history_ = std::make_unique<ToolStripMergeHistory>();
        return *strip.merge_history_;
    }
    static MenuStripMergeHistory& history(MenuStrip& strip) {
        if (!strip.merge_history_) strip.merge_history_ = std::make_unique<MenuStripMergeHistory>();
        return *strip.merge_history_;
    }
};

namespace {

// ---- Items at every level ---------------------------------------------------

std::vector<MenuItemSpec>& menu_of(ToolStripItemSpec& item) noexcept { return item.drop_down; }
std::vector<MenuItemSpec>& menu_of(MenuStripItemSpec& item) noexcept { return item.items; }
std::vector<MenuItemSpec>& menu_of(MenuItemSpec& item) noexcept { return item.children; }

// Whether the item is one that holds a menu (it may be empty).
bool holds_menu(const ToolStripItemSpec& item) noexcept {
    const bool result =
        item.kind == ToolStripItemKind::drop_down_button || item.kind == ToolStripItemKind::split_button;
    return result;
}
bool holds_menu(const MenuStripItemSpec&) noexcept { return true; }
bool holds_menu(const MenuItemSpec& item) noexcept { return item.kind == MenuItemKind::submenu; }

char ascii_lower(char character) noexcept {
    if (character >= 'A' && character <= 'Z') return static_cast<char>(character - 'A' + 'a');
    return character;
}

bool equal_ignoring_ascii_case(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) return false;
    }
    return true;
}

// The text an item shows: its own, else its command's.
std::string_view displayed_text(const ToolStripItemSpec& item) noexcept {
    if (!item.text.empty() || !item.command) return item.text;
    return (*item.command).state().text;
}
std::string_view displayed_text(const MenuStripItemSpec& item) noexcept { return item.text; }
std::string_view displayed_text(const MenuItemSpec& item) noexcept {
    if (!item.text.empty() || !item.command) return item.text;
    return (*item.command).state().text;
}

// The target item `source` matches: the first showing equal text, else the
// one at its merge_index.
template <typename Item>
std::optional<std::size_t> find_match(const Item& source, const std::vector<Item>& target) {
    const std::string_view wanted = displayed_text(source);
    for (std::size_t index = 0U; index < target.size(); ++index) {
        if (equal_ignoring_ascii_case(displayed_text(target[index]), wanted)) return index;
    }
    if (source.merge_index && *source.merge_index < target.size()) return *source.merge_index;
    return std::nullopt;
}

template <typename Top>
void keep_removed(MergeStep<Top>& step, Top item) {
    step.removed_top = std::move(item);
}
template <typename Top>
void keep_removed(MergeStep<Top>& step, MenuItemSpec item) {
    step.removed_item = std::move(item);
}

std::vector<std::string> extended(const std::vector<std::string>& path, const std::string& item_id) {
    std::vector<std::string> longer = path;
    longer.push_back(item_id);
    return longer;
}

// ---- The merge walk -----------------------------------------------------------

// Where one collection merges into another: the two collections and their
// paths. Both collections hold items of the same level.
template <typename Item>
struct CollectionPair final {
    std::vector<Item>* source{};
    std::vector<std::string> source_path{};
    std::vector<Item>* target{};
    std::vector<std::string> target_path{};
};

// Moves (*pair.source)[position] into the target at `index`, recording it.
template <typename Top, typename Item>
void move_item(const CollectionPair<Item>& pair, std::size_t position, std::size_t index,
               std::vector<MergeStep<Top>>& steps) {
    MergeStep<Top> step{};
    step.kind = MergeStepKind::moved;
    step.item_id = (*pair.source)[position].stable_id;
    step.target_path = pair.target_path;
    step.target_index = index;
    step.source_path = pair.source_path;
    step.source_index = position;
    Item moving = std::move((*pair.source)[position]);
    (*pair.source).erase((*pair.source).begin() + static_cast<std::ptrdiff_t>(position));
    (*pair.target).insert((*pair.target).begin() + static_cast<std::ptrdiff_t>(index), std::move(moving));
    steps.push_back(std::move(step));
}

// Takes (*pair.target)[index] out of the target, recording it.
template <typename Top, typename Item>
void remove_item(const CollectionPair<Item>& pair, std::size_t index, std::vector<MergeStep<Top>>& steps) {
    MergeStep<Top> step{};
    step.kind = MergeStepKind::removed;
    step.item_id = (*pair.target)[index].stable_id;
    step.target_path = pair.target_path;
    step.target_index = index;
    Item leaving = std::move((*pair.target)[index]);
    (*pair.target).erase((*pair.target).begin() + static_cast<std::ptrdiff_t>(index));
    keep_removed(step, std::move(leaving));
    steps.push_back(std::move(step));
}

template <typename Top, typename Item>
void merge_collection(const CollectionPair<Item>& pair, std::vector<MergeStep<Top>>& steps);

// Merges the source item at `position` by its merge_action. Returns whether
// it left the source (the next item then takes its place).
template <typename Top, typename Item>
bool merge_item(const CollectionPair<Item>& pair, std::size_t position, std::vector<MergeStep<Top>>& steps) {
    std::vector<Item>& source = *pair.source;
    std::vector<Item>& target = *pair.target;
    Item& item = source[position];
    switch (item.merge_action) {
    case MergeAction::append:
        move_item(pair, position, target.size(), steps);
        return true;
    case MergeAction::insert:
        if (!item.merge_index) return false;
        move_item(pair, position, std::min(*item.merge_index, target.size()), steps);
        return true;
    case MergeAction::replace: {
        const std::optional<std::size_t> match = find_match(item, target);
        if (!match) return false;
        remove_item(pair, *match, steps);
        move_item(pair, position, *match, steps);
        return true;
    }
    case MergeAction::remove: {
        const std::optional<std::size_t> match = find_match(item, target);
        if (match) remove_item(pair, *match, steps);
        return false;
    }
    case MergeAction::match_only: {
        const std::optional<std::size_t> match = find_match(item, target);
        if (!match || !holds_menu(item) || menu_of(item).empty() || !holds_menu(target[*match])) return false;
        CollectionPair<MenuItemSpec> inner{};
        inner.source = &menu_of(item);
        inner.source_path = extended(pair.source_path, item.stable_id);
        inner.target = &menu_of(target[*match]);
        inner.target_path = extended(pair.target_path, target[*match].stable_id);
        merge_collection<Top, MenuItemSpec>(inner, steps);
        return false;
    }
    }
    // An action outside the enumeration leaves the item where it is.
    return false;
}

template <typename Top, typename Item>
void merge_collection(const CollectionPair<Item>& pair, std::vector<MergeStep<Top>>& steps) {
    std::size_t position = 0U;
    while (position < (*pair.source).size()) {
        const bool moved = merge_item<Top, Item>(pair, position, steps);
        if (!moved) ++position;
    }
}

// ---- The revert -------------------------------------------------------------

template <typename Item>
std::optional<std::size_t> index_of_id(const std::vector<Item>& items, std::string_view item_id) {
    for (std::size_t index = 0U; index < items.size(); ++index) {
        if (items[index].stable_id == item_id) return index;
    }
    return std::nullopt;
}

// The menu at a non-empty `path`, or null when part of it has gone.
template <typename Top>
std::vector<MenuItemSpec>* find_menu(std::vector<Top>& tops, const std::vector<std::string>& path) {
    const std::optional<std::size_t> top = index_of_id(tops, path.front());
    if (!top || !holds_menu(tops[*top])) return nullptr;
    std::vector<MenuItemSpec>* menu = &menu_of(tops[*top]);
    for (std::size_t depth = 1U; depth < path.size(); ++depth) {
        const std::optional<std::size_t> child = index_of_id(*menu, path[depth]);
        if (!child || !holds_menu((*menu)[*child])) return nullptr;
        menu = &menu_of((*menu)[*child]);
    }
    return menu;
}

template <typename Item>
void insert_clamped(std::vector<Item>& items, std::size_t index, Item item) {
    const std::size_t at = std::min(index, items.size());
    items.insert(items.begin() + static_cast<std::ptrdiff_t>(at), std::move(item));
}

// Moves the item `step` moved back to the source, or drops it when the
// source or its collection has gone.
template <typename Item>
void undo_move(std::vector<Item>* target, std::vector<Item>* source, std::string_view item_id,
               std::size_t source_index) {
    if (target == nullptr) return;
    const std::optional<std::size_t> index = index_of_id(*target, item_id);
    if (!index) return;
    Item returning = std::move((*target)[*index]);
    (*target).erase((*target).begin() + static_cast<std::ptrdiff_t>(*index));
    if (source != nullptr) insert_clamped(*source, source_index, std::move(returning));
}

// Undoes one step on the strips' item copies; `source` is null when the
// source strip has gone.
template <typename Top>
void undo_step(const MergeStep<Top>& step, std::vector<Top>& target, std::vector<Top>* source) {
    if (step.kind == MergeStepKind::removed) {
        if (step.target_path.empty()) {
            if (step.removed_top) insert_clamped(target, step.target_index, *step.removed_top);
            return;
        }
        std::vector<MenuItemSpec>* menu = find_menu(target, step.target_path);
        if (menu != nullptr && step.removed_item) insert_clamped(*menu, step.target_index, *step.removed_item);
        return;
    }
    if (step.target_path.empty()) {
        undo_move(&target, source, step.item_id, step.source_index);
        return;
    }
    std::vector<MenuItemSpec>* target_menu = find_menu(target, step.target_path);
    std::vector<MenuItemSpec>* source_menu = source == nullptr ? nullptr : find_menu(*source, step.source_path);
    undo_move(target_menu, source_menu, step.item_id, step.source_index);
}

// ---- The strips ---------------------------------------------------------------

// The items as the strip shows them now: check-on-click state the strip
// holds goes into the specs, so it survives the move.
std::vector<ToolStripItemSpec> current_items(const ToolStrip& strip) {
    std::vector<ToolStripItemSpec> items = strip.items();
    for (ToolStripItemSpec& item : items) {
        if (item.command || !item.check_on_click) continue;
        item.checked = strip.item_checked(item.stable_id);
    }
    return items;
}

std::vector<MenuStripItemSpec> current_items(const MenuStrip& strip) {
    std::vector<MenuStripItemSpec> items = strip.items();
    return items;
}

// Applies new items to both strips, or neither: when the source's fail, the
// target's earlier items are restored and the failure propagates.
template <typename Strip, typename Top>
void apply_both(Strip& target, std::vector<Top> target_items, Strip* source, std::vector<Top> source_items) {
    std::vector<Top> target_before = current_items(target);
    StripMergeAccess::apply(target, std::move(target_items));
    if (source == nullptr) return;
    try {
        StripMergeAccess::apply(*source, std::move(source_items));
    } catch (...) {
        StripMergeAccess::apply(target, std::move(target_before));
        throw;
    }
}

template <typename Strip, typename Top>
bool merge(Strip& source, Strip& target) {
    if (&source == &target) throw std::invalid_argument("a strip cannot merge into itself");
    StripMerge<Top> record{};
    record.source = source.shared_from_this();
    if (!source.allow_merge() || !target.allow_merge()) return false;
    std::vector<Top> source_items = current_items(source);
    std::vector<Top> target_items = current_items(target);
    CollectionPair<Top> pair{};
    pair.source = &source_items;
    pair.target = &target_items;
    merge_collection<Top, Top>(pair, record.steps);
    if (record.steps.empty()) return false;
    try {
        apply_both(target, std::move(target_items), &source, std::move(source_items));
    } catch (const std::invalid_argument&) {
        return false;
    }
    StripMergeAccess::history(target).merges.push_back(std::move(record));
    return true;
}

// Reverts the newest merge into `target`; the history is popped only once
// both strips accepted their restored items.
template <typename Strip, typename Top, typename History>
void revert_newest(Strip& target, History& history) {
    static_assert(std::is_same_v<decltype(history.merges.back()), StripMerge<Top>&>,
                  "the history holds merges of the strip's top-level items");
    const StripMerge<Top>& record = history.merges.back();
    const std::shared_ptr<Control> source_control = record.source.lock();
    Strip* source = source_control ? static_cast<Strip*>(source_control.get()) : nullptr;
    std::vector<Top> target_items = current_items(target);
    std::vector<Top> source_items{};
    if (source != nullptr) source_items = current_items(*source);
    std::vector<Top>* source_copy = source == nullptr ? nullptr : &source_items;
    for (std::size_t remaining = record.steps.size(); remaining > 0U; --remaining) {
        undo_step(record.steps[remaining - 1U], target_items, source_copy);
    }
    apply_both(target, std::move(target_items), source, std::move(source_items));
    history.merges.pop_back();
}

template <typename Strip, typename Top, typename History>
bool revert_all(Strip& target) {
    History& history = StripMergeAccess::history(target);
    if (history.merges.empty()) return false;
    while (!history.merges.empty()) revert_newest<Strip, Top, History>(target, history);
    return true;
}

template <typename Strip, typename Top, typename History>
bool revert_source(Strip& target, Strip& source) {
    History& history = StripMergeAccess::history(target);
    const Control* const wanted = &source;
    std::optional<std::size_t> newest{};
    for (std::size_t index = 0U; index < history.merges.size(); ++index) {
        const std::shared_ptr<Control> merged = history.merges[index].source.lock();
        if (merged.get() == wanted) newest = index;
    }
    if (!newest) return false;
    std::vector<std::weak_ptr<Control>> later{};
    for (std::size_t index = *newest + 1U; index < history.merges.size(); ++index) {
        later.push_back(history.merges[index].source);
    }
    while (history.merges.size() > *newest) revert_newest<Strip, Top, History>(target, history);
    for (const std::weak_ptr<Control>& weak_source : later) {
        const std::shared_ptr<Control> again = weak_source.lock();
        if (!again) continue;
        const bool merged = merge<Strip, Top>(static_cast<Strip&>(*again), target);
        static_cast<void>(merged);
    }
    return true;
}

} // namespace
} // namespace gui_forms::detail

namespace gui_forms {

bool merge_strips(ToolStrip& source, ToolStrip& target) {
    const bool merged = detail::merge<ToolStrip, ToolStripItemSpec>(source, target);
    return merged;
}

bool merge_strips(MenuStrip& source, MenuStrip& target) {
    const bool merged = detail::merge<MenuStrip, MenuStripItemSpec>(source, target);
    return merged;
}

bool revert_merge(ToolStrip& target) {
    const bool reverted =
        detail::revert_all<ToolStrip, ToolStripItemSpec, detail::ToolStripMergeHistory>(target);
    return reverted;
}

bool revert_merge(MenuStrip& target) {
    const bool reverted =
        detail::revert_all<MenuStrip, MenuStripItemSpec, detail::MenuStripMergeHistory>(target);
    return reverted;
}

bool revert_merge(ToolStrip& target, ToolStrip& source) {
    const bool reverted =
        detail::revert_source<ToolStrip, ToolStripItemSpec, detail::ToolStripMergeHistory>(target, source);
    return reverted;
}

bool revert_merge(MenuStrip& target, MenuStrip& source) {
    const bool reverted =
        detail::revert_source<MenuStrip, MenuStripItemSpec, detail::MenuStripMergeHistory>(target, source);
    return reverted;
}

} // namespace gui_forms
