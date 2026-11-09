// GF_TABLE_TEXT_EDITING: a text box's selection, grapheme navigation, edits
// and history. See gf_abi.h.

#include "abi_control_entry.hpp"
#include "abi_tables.hpp"

#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/text/text_store/text_store.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity select_slot{GF_TABLE_TEXT_EDITING, 0U};
constexpr SlotIdentity selection_slot{GF_TABLE_TEXT_EDITING, 1U};
constexpr SlotIdentity grapheme_boundary_slot{GF_TABLE_TEXT_EDITING, 2U};
constexpr SlotIdentity replace_range_slot{GF_TABLE_TEXT_EDITING, 3U};
constexpr SlotIdentity undo_slot{GF_TABLE_TEXT_EDITING, 4U};
constexpr SlotIdentity redo_slot{GF_TABLE_TEXT_EDITING, 5U};
constexpr SlotIdentity clear_undo_history_slot{GF_TABLE_TEXT_EDITING, 6U};

constexpr const char* not_text_box = "the control is not a text box";

// A caller's byte offset as an offset into text: false when it lies past
// the text.
bool text_offset(std::uint64_t position, std::string_view text, Utf8Offset& offset) noexcept {
    if (position > text.size()) {
        return false;
    }
    offset = Utf8Offset{static_cast<std::size_t>(position)};
    return true;
}

gf_status GF_CALL text_editing_select(gf_object object, std::uint64_t anchor, std::uint64_t caret) noexcept {
    try {
        const ShadowFrameScope shadow_frame(select_slot.table_id, select_slot.slot_index);
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, select_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Utf8Offset anchor_offset;
        Utf8Offset caret_offset;
        if (!text_offset(anchor, (*box).text.read(), anchor_offset) || !text_offset(caret, (*box).text.read(), caret_offset)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, select_slot, "a selection lies within the text");
        }
        // TextBox::select refuses a position inside a grapheme cluster
        // (std::out_of_range: GF_STATUS_INVALID_ARGUMENT).
        (*box).select(anchor_offset, caret_offset);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(select_slot);
    }
}

gf_status GF_CALL text_editing_selection(gf_object object, std::uint64_t* anchor, std::uint64_t* caret) noexcept {
    try {
        const ShadowFrameScope shadow_frame(selection_slot.table_id, selection_slot.slot_index);
        if (anchor == nullptr || caret == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, selection_slot, "selection needs anchor and caret outputs");
        }
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, selection_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const TextSelection current = (*box).selection();
        *anchor = current.anchor.value();
        *caret = current.caret.value();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(selection_slot);
    }
}

gf_status GF_CALL text_editing_grapheme_boundary(gf_object object, std::uint64_t position, std::int32_t direction,
                                                 std::uint64_t* boundary) noexcept {
    try {
        const ShadowFrameScope shadow_frame(grapheme_boundary_slot.table_id, grapheme_boundary_slot.slot_index);
        if (boundary == nullptr || (direction != 1 && direction != -1)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, grapheme_boundary_slot,
                        "grapheme_boundary needs an output and a direction of 1 or -1");
        }
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, grapheme_boundary_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Utf8Offset offset;
        if (!text_offset(position, (*box).text.read(), offset)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, grapheme_boundary_slot, "the position lies within the text");
        }
        // A store of the box's text segments it as the box does.
        const TextStore store((*box).text.read());
        if (!store.is_grapheme_boundary(offset)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, grapheme_boundary_slot,
                        "the position is not a grapheme boundary");
        }
        Utf8Offset found = offset;
        if (direction == 1) {
            found = store.next_grapheme_boundary(offset);
        } else {
            found = store.previous_grapheme_boundary(offset);
        }
        *boundary = found.value();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(grapheme_boundary_slot);
    }
}

gf_status GF_CALL text_editing_replace_range(gf_object object, std::uint64_t start, std::uint64_t length,
                                             gf_utf8_view replacement, std::uint32_t* changed) noexcept {
    try {
        const ShadowFrameScope shadow_frame(replace_range_slot.table_id, replace_range_slot.slot_index);
        if (changed == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, replace_range_slot, "replace_range needs a changed output");
        }
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, replace_range_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        std::string_view text;
        const gf_status read = read_text(replacement, replace_range_slot, text);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const std::size_t size = (*box).text.read().size();
        if (start > size || length > size - start) {
            return fail(GF_STATUS_INVALID_ARGUMENT, replace_range_slot, "the range lies within the text");
        }
        const Utf8Offset first{static_cast<std::size_t>(start)};
        const Utf8Offset last{static_cast<std::size_t>(start + length)};
        // The range becomes the selection, which the replacement takes as
        // typing does; the caret ends after it.
        (*box).select(first, last);
        const bool replaced = (*box).replace_selection(text);
        *changed = replaced ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(replace_range_slot);
    }
}

gf_status GF_CALL text_editing_undo(gf_object object, std::uint32_t* changed) noexcept {
    try {
        const ShadowFrameScope shadow_frame(undo_slot.table_id, undo_slot.slot_index);
        if (changed == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, undo_slot, "undo needs a changed output");
        }
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, undo_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const bool undone = (*box).undo();
        *changed = undone ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(undo_slot);
    }
}

gf_status GF_CALL text_editing_redo(gf_object object, std::uint32_t* changed) noexcept {
    try {
        const ShadowFrameScope shadow_frame(redo_slot.table_id, redo_slot.slot_index);
        if (changed == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, redo_slot, "redo needs a changed output");
        }
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, redo_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const bool redone = (*box).redo();
        *changed = redone ? 1U : 0U;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(redo_slot);
    }
}

gf_status GF_CALL text_editing_clear_undo_history(gf_object object) noexcept {
    try {
        const ShadowFrameScope shadow_frame(clear_undo_history_slot.table_id, clear_undo_history_slot.slot_index);
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, clear_undo_history_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*box).clear_undo_history();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(clear_undo_history_slot);
    }
}

constexpr gf_text_editing_table table{
    .struct_size = sizeof(gf_text_editing_table),
    .abi_minor = GF_ABI_MINOR,
    .select = &text_editing_select,
    .selection = &text_editing_selection,
    .grapheme_boundary = &text_editing_grapheme_boundary,
    .replace_range = &text_editing_replace_range,
    .undo = &text_editing_undo,
    .redo = &text_editing_redo,
    .clear_undo_history = &text_editing_clear_undo_history,
};

} // namespace

const gf_text_editing_table& text_editing_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
