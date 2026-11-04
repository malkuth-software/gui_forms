#pragma once

// Strip merging (Windows Forms' ToolStripManager.Merge and RevertMerge): a
// source strip's items join a target strip of the same kind, each by its
// spec's merge_action and merge_index, and a revert restores both strips.
//
// A merge walks the source's top-level items in order. An item is matched
// in the target collection at its own level by its text (ASCII
// case-insensitive; an empty text matches an empty text), else by its
// merge_index as a position:
//   append      moves to the end of the target collection;
//   insert      with a merge_index, moves into the target collection there
//               (at the end when past it); without one, stays;
//   replace     takes the matched item's place, and the matched item leaves
//               the target; without a match, stays;
//   remove      the matched item leaves the target; the item stays;
//   match_only  stays; when it holds a non-empty menu and the matched item
//               holds a menu, its menu's items merge into that menu the
//               same way, recursively.
// Moved items leave the source. A menu a merge empties stays valid and is
// not shown until it has items again.
//
// The target keeps the merge's history (oldest first) and observes, without
// owning, its source; both strips must be owned by shared pointers
// (make_control). Divergences from Windows Forms, recorded in
// manifests/winforms-divergences.json: case folding is ASCII only; only
// strips of the same kind merge (ToolStrip family with ToolStrip family,
// MenuStrip with MenuStrip); a merge whose result repeats an item ID is
// refused; a hosted item (combo box, text box, progress bar) that moves is
// recreated, so its control's state is not carried; a match_only item
// recurses only into a matched item that already holds a menu.
//
// All functions run on the strips' UI thread.

#include "gui_forms/controls/menu_strip/menu_strip.hpp"
#include "gui_forms/controls/tool_strip/tool_strip.hpp"

namespace gui_forms {

// Merges `source` into `target`. Returns whether any item moved or left;
// false, changing nothing, when either strip does not allow merging, when
// nothing applies, or when the result would be invalid (a repeated item
// ID). Throws std::invalid_argument when `source` is `target`, and
// std::bad_weak_ptr when `source` is not owned by a shared pointer.
[[nodiscard]] bool merge_strips(ToolStrip& source, ToolStrip& target);
[[nodiscard]] bool merge_strips(MenuStrip& source, MenuStrip& target);

// Reverts every merge into `target`, newest first, restoring the sources
// that still exist (items of a destroyed source are dropped from the
// target). Returns whether there was a merge to revert. Each merge reverts
// whole or not at all: when restoring one would make either strip invalid
// (items added since with a repeated ID), std::invalid_argument propagates,
// the newer merges stay reverted, and that merge and the older ones stay in
// place with their history.
bool revert_merge(ToolStrip& target);
bool revert_merge(MenuStrip& target);

// Reverts the newest merge of `source` into `target`: the merges after it
// are reverted first and then merged again, in their order, from the
// sources that still exist. Returns whether `source` had merged into
// `target`. Failures as revert_merge(target).
bool revert_merge(ToolStrip& target, ToolStrip& source);
bool revert_merge(MenuStrip& target, MenuStrip& source);

} // namespace gui_forms
