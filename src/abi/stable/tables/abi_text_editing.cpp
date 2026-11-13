// GF_TABLE_TEXT_EDITING: a text field's selection, grapheme navigation,
// edits and history (a text box's, or the editor of a combo box in the
// drop_down style), and a text box's styled text runs. See gf_abi.h.

#include "abi_control_entry.hpp"
#include "abi_tables.hpp"
#include "../values/abi_values.hpp"

#include "gui_forms/controls/combo_box.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/text/text_store/text_store.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity select_slot{GF_TABLE_TEXT_EDITING, 0U};
constexpr SlotIdentity selection_slot{GF_TABLE_TEXT_EDITING, 1U};
constexpr SlotIdentity grapheme_boundary_slot{GF_TABLE_TEXT_EDITING, 2U};
constexpr SlotIdentity replace_range_slot{GF_TABLE_TEXT_EDITING, 3U};
constexpr SlotIdentity undo_slot{GF_TABLE_TEXT_EDITING, 4U};
constexpr SlotIdentity redo_slot{GF_TABLE_TEXT_EDITING, 5U};
constexpr SlotIdentity clear_undo_history_slot{GF_TABLE_TEXT_EDITING, 6U};
constexpr SlotIdentity set_decorations_slot{GF_TABLE_TEXT_EDITING, 7U};
constexpr SlotIdentity replace_decorations_slot{GF_TABLE_TEXT_EDITING, 8U};
constexpr SlotIdentity clear_decorations_slot{GF_TABLE_TEXT_EDITING, 9U};
constexpr SlotIdentity decoration_count_slot{GF_TABLE_TEXT_EDITING, 10U};
constexpr SlotIdentity decoration_at_slot{GF_TABLE_TEXT_EDITING, 11U};
constexpr SlotIdentity utf8_to_utf16_offset_slot{GF_TABLE_TEXT_EDITING, 12U};
constexpr SlotIdentity utf16_to_utf8_offset_slot{GF_TABLE_TEXT_EDITING, 13U};

constexpr const char* not_text_box = "the control is not a text box";
constexpr const char* not_text_field = "the control is not a text box or an editable combo box";

// The text box a text-editing entry acts on: the control itself, or the
// editor of a combo box in the drop_down style (a drop_down_list combo box
// has no editor and is refused like any other control).
[[nodiscard]] gf_status enter_text_field(gf_object object, SlotIdentity slot, std::shared_ptr<TextBox>& box) {
    Control::Ptr control;
    const gf_status entered = enter_live_control(object, slot, control);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    std::shared_ptr<TextBox> field = std::dynamic_pointer_cast<TextBox>(control);
    if (!field) {
        const std::shared_ptr<ComboBox> combo = std::dynamic_pointer_cast<ComboBox>(control);
        if (combo) {
            field = (*combo).editor();
        }
    }
    if (!field) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, not_text_field);
    }
    box = std::move(field);
    return GF_STATUS_OK;
}

// The C vocabularies by value: gf_text_ink_role and gf_text_underline.
constexpr std::array<TextInkRole, 5> ink_roles{TextInkRole::text, TextInkRole::muted, TextInkRole::link,
                                               TextInkRole::visited_link, TextInkRole::notice};
constexpr std::array<TextUnderline, 3> underlines{TextUnderline::none, TextUnderline::single,
                                                  TextUnderline::squiggle};
constexpr std::uint32_t known_decoration_fields =
    GF_TEXT_DECORATION_INK | GF_TEXT_DECORATION_FOREGROUND | GF_TEXT_DECORATION_WEIGHT |
    GF_TEXT_DECORATION_ITALIC | GF_TEXT_DECORATION_UNDERLINE | GF_TEXT_DECORATION_UNDERLINE_COLOR |
    GF_TEXT_DECORATION_BACKGROUND;

// A caller's decoration record as a TextDecoration: false, leaving
// `decoration` unspecified, when a field is out of range, a range lies past
// the text of `text_size` bytes or ends before it starts, or an unknown bit
// or the reserved field is set.
bool decoration_from_record(const gf_text_decoration& record, std::size_t text_size,
                            TextDecoration& decoration) noexcept {
    if ((record.fields & ~known_decoration_fields) != 0U || record.reserved != 0U) return false;
    if (record.start > record.end || record.end > text_size) return false;
    decoration = TextDecoration{};
    decoration.range = Utf8Range{Utf8Offset{static_cast<std::size_t>(record.start)},
                                 Utf8Offset{static_cast<std::size_t>(record.end)}};
    decoration.tag = record.tag;
    TextDecorationStyle& style = decoration.style;
    if ((record.fields & GF_TEXT_DECORATION_INK) != 0U) {
        if (record.ink >= ink_roles.size()) return false;
        style.ink = ink_roles[record.ink];
    }
    if ((record.fields & GF_TEXT_DECORATION_FOREGROUND) != 0U) style.foreground = unpacked_color(record.foreground);
    if ((record.fields & GF_TEXT_DECORATION_WEIGHT) != 0U) {
        if (record.weight < 1U || record.weight > 1000U) return false;
        style.weight = static_cast<std::uint16_t>(record.weight);
    }
    if ((record.fields & GF_TEXT_DECORATION_ITALIC) != 0U) {
        if (record.italic > 1U) return false;
        style.italic = record.italic == 1U;
    }
    if ((record.fields & GF_TEXT_DECORATION_UNDERLINE) != 0U) {
        if (record.underline >= underlines.size()) return false;
        style.underline = underlines[record.underline];
    }
    if ((record.fields & GF_TEXT_DECORATION_UNDERLINE_COLOR) != 0U) {
        style.underline_color = unpacked_color(record.underline_color);
    }
    if ((record.fields & GF_TEXT_DECORATION_BACKGROUND) != 0U) style.background = unpacked_color(record.background);
    return true;
}

// Position of `value` in `table`; every value of the C++ enumerations is
// listed, so it is always found.
template <typename Enumeration, std::size_t Count>
std::uint32_t vocabulary_index(const std::array<Enumeration, Count>& table, Enumeration value) noexcept {
    std::uint32_t index = 0U;
    for (std::size_t position = 0U; position < Count; ++position) {
        if (table[position] == value) index = static_cast<std::uint32_t>(position);
    }
    return index;
}

gf_text_decoration record_from_decoration(const TextDecoration& decoration) noexcept {
    gf_text_decoration record{};
    record.start = decoration.range.start.value();
    record.end = decoration.range.end.value();
    record.tag = decoration.tag;
    const TextDecorationStyle& style = decoration.style;
    if (style.ink.has_value()) {
        record.fields |= GF_TEXT_DECORATION_INK;
        record.ink = vocabulary_index(ink_roles, *style.ink);
    }
    if (style.foreground.has_value()) {
        record.fields |= GF_TEXT_DECORATION_FOREGROUND;
        record.foreground = packed_color(*style.foreground);
    }
    if (style.weight.has_value()) {
        record.fields |= GF_TEXT_DECORATION_WEIGHT;
        record.weight = *style.weight;
    }
    if (style.italic.has_value()) {
        record.fields |= GF_TEXT_DECORATION_ITALIC;
        record.italic = *style.italic ? 1U : 0U;
    }
    if (style.underline.has_value()) {
        record.fields |= GF_TEXT_DECORATION_UNDERLINE;
        record.underline = vocabulary_index(underlines, *style.underline);
    }
    if (style.underline_color.has_value()) {
        record.fields |= GF_TEXT_DECORATION_UNDERLINE_COLOR;
        record.underline_color = packed_color(*style.underline_color);
    }
    if (style.background.has_value()) {
        record.fields |= GF_TEXT_DECORATION_BACKGROUND;
        record.background = packed_color(*style.background);
    }
    return record;
}

// Reads `count` caller records into `decorations` (filled, not appended):
// GF_STATUS_OK, or the failure for a null array with a count or a malformed
// record. Nothing in the box changes here.
gf_status read_decorations(const gf_text_decoration* records, std::uint64_t count, std::size_t text_size,
                           SlotIdentity slot, std::vector<TextDecoration>& decorations) {
    decorations.clear();
    if (count == 0U) return GF_STATUS_OK;
    if (records == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "decorations are null with a count");
    }
    const std::span<const gf_text_decoration> given(records, static_cast<std::size_t>(count));
    decorations.reserve(given.size());
    for (const gf_text_decoration& record : given) {
        TextDecoration decoration{};
        if (!decoration_from_record(record, text_size, decoration)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                        "a decoration lies past the text, ends before it starts or has a field out of range");
        }
        decorations.push_back(decoration);
    }
    return GF_STATUS_OK;
}

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
        const gf_status entered = enter_text_field(object, select_slot, box);
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
        const gf_status entered = enter_text_field(object, selection_slot, box);
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
        const gf_status entered = enter_text_field(object, grapheme_boundary_slot, box);
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
        const gf_status entered = enter_text_field(object, replace_range_slot, box);
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
        const gf_status entered = enter_text_field(object, undo_slot, box);
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
        const gf_status entered = enter_text_field(object, redo_slot, box);
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
        const gf_status entered = enter_text_field(object, clear_undo_history_slot, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*box).clear_undo_history();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(clear_undo_history_slot);
    }
}

gf_status GF_CALL text_editing_set_decorations(gf_object object, std::uint32_t layer,
                                              const gf_text_decoration* decorations, std::uint64_t count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_decorations_slot.table_id, set_decorations_slot.slot_index);
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, set_decorations_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        std::vector<TextDecoration> read;
        const gf_status parsed = read_decorations(decorations, count, (*box).text.read().size(),
                                                  set_decorations_slot, read);
        if (parsed != GF_STATUS_OK) {
            return parsed;
        }
        (*box).set_decorations(TextDecorationLayer{layer}, read);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_decorations_slot);
    }
}

gf_status GF_CALL text_editing_replace_decorations(gf_object object, std::uint32_t layer, std::uint64_t start,
                                                  std::uint64_t end, const gf_text_decoration* decorations,
                                                  std::uint64_t count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(replace_decorations_slot.table_id, replace_decorations_slot.slot_index);
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, replace_decorations_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::size_t size = (*box).text.read().size();
        if (start > end || end > size) {
            return fail(GF_STATUS_INVALID_ARGUMENT, replace_decorations_slot, "the range lies within the text");
        }
        std::vector<TextDecoration> read;
        const gf_status parsed = read_decorations(decorations, count, size, replace_decorations_slot, read);
        if (parsed != GF_STATUS_OK) {
            return parsed;
        }
        const Utf8Range range{Utf8Offset{static_cast<std::size_t>(start)}, Utf8Offset{static_cast<std::size_t>(end)}};
        // TextDecorations::replace_range refuses a decoration outside the
        // range (std::invalid_argument: GF_STATUS_INVALID_ARGUMENT).
        (*box).replace_decorations(TextDecorationLayer{layer}, range, read);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(replace_decorations_slot);
    }
}

gf_status GF_CALL text_editing_clear_decorations(gf_object object) noexcept {
    try {
        const ShadowFrameScope shadow_frame(clear_decorations_slot.table_id, clear_decorations_slot.slot_index);
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, clear_decorations_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        (*box).clear_all_decorations();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(clear_decorations_slot);
    }
}

gf_status GF_CALL text_editing_decoration_count(gf_object object, std::uint32_t layer, std::uint64_t* count) noexcept {
    try {
        const ShadowFrameScope shadow_frame(decoration_count_slot.table_id, decoration_count_slot.slot_index);
        if (count == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, decoration_count_slot, "decoration_count needs an output");
        }
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, decoration_count_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *count = (*box).decorations().items(TextDecorationLayer{layer}).size();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(decoration_count_slot);
    }
}

gf_status GF_CALL text_editing_decoration_at(gf_object object, std::uint32_t layer, std::uint64_t index,
                                             gf_text_decoration* decoration) noexcept {
    try {
        const ShadowFrameScope shadow_frame(decoration_at_slot.table_id, decoration_at_slot.slot_index);
        if (decoration == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, decoration_at_slot, "decoration_at needs an output");
        }
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_typed_control(object, decoration_at_slot, not_text_box, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const std::span<const TextDecoration> items = (*box).decorations().items(TextDecorationLayer{layer});
        if (index >= items.size()) {
            return fail(GF_STATUS_INVALID_ARGUMENT, decoration_at_slot, "the index lies past the layer's decorations");
        }
        *decoration = record_from_decoration(items[static_cast<std::size_t>(index)]);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(decoration_at_slot);
    }
}

// K9: the conversions a UTF-16 caller (the C# Windows Forms layer) needs
// for every selection and decoration call, without copying the text out.
gf_status GF_CALL text_editing_utf8_to_utf16_offset(gf_object object, std::uint64_t utf8_offset,
                                                    std::uint64_t* utf16_offset) noexcept {
    try {
        const ShadowFrameScope shadow_frame(utf8_to_utf16_offset_slot.table_id,
                                            utf8_to_utf16_offset_slot.slot_index);
        if (utf16_offset == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, utf8_to_utf16_offset_slot,
                        "utf8_to_utf16_offset needs an output");
        }
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_text_field(object, utf8_to_utf16_offset_slot, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        Utf8Offset offset;
        if (!text_offset(utf8_offset, (*box).text.read(), offset)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, utf8_to_utf16_offset_slot, "the offset lies past the text");
        }
        // Inside a scalar: std::invalid_argument, GF_STATUS_INVALID_ARGUMENT.
        const Utf16Offset converted = (*box).utf16_offset(offset);
        *utf16_offset = converted.value();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(utf8_to_utf16_offset_slot);
    }
}

gf_status GF_CALL text_editing_utf16_to_utf8_offset(gf_object object, std::uint64_t utf16_offset,
                                                    std::uint64_t* utf8_offset) noexcept {
    try {
        const ShadowFrameScope shadow_frame(utf16_to_utf8_offset_slot.table_id,
                                            utf16_to_utf8_offset_slot.slot_index);
        if (utf8_offset == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, utf16_to_utf8_offset_slot,
                        "utf16_to_utf8_offset needs an output");
        }
        std::shared_ptr<TextBox> box;
        const gf_status entered = enter_text_field(object, utf16_to_utf8_offset_slot, box);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (utf16_offset > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return fail(GF_STATUS_INVALID_ARGUMENT, utf16_to_utf8_offset_slot, "the position lies past the text");
        }
        // Past the text: std::out_of_range; between surrogate halves:
        // std::invalid_argument. Both are GF_STATUS_INVALID_ARGUMENT.
        const Utf8Offset converted = (*box).utf8_offset(Utf16Offset{static_cast<std::size_t>(utf16_offset)});
        *utf8_offset = converted.value();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(utf16_to_utf8_offset_slot);
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
    .set_decorations = &text_editing_set_decorations,
    .replace_decorations = &text_editing_replace_decorations,
    .clear_decorations = &text_editing_clear_decorations,
    .decoration_count = &text_editing_decoration_count,
    .decoration_at = &text_editing_decoration_at,
    .utf8_to_utf16_offset = &text_editing_utf8_to_utf16_offset,
    .utf16_to_utf8_offset = &text_editing_utf16_to_utf8_offset,
};

} // namespace

const gf_text_editing_table& text_editing_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
