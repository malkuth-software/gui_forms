#include "text_editing_engine.hpp"

#include "../input_control_utilities.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gui_forms::detail {
using namespace input_control_detail;

TextEditingEngine::TextEditingEngine(TextEditingHost& host, std::string_view text)
    : host_(host), store_(text) {
    selection_ = {store_.utf8_size(), store_.utf8_size()};
}

void TextEditingEngine::replace_whole_text(std::string_view text) {
    last_change_ = text_change_between(store_.utf8(), text);
    store_.set_text(text);
}

void TextEditingEngine::set_text(std::string_view text) {
    // Replacing the whole text abandons any composition it held.
    composing_ = false;
    replace_whole_text(text);
    selection_ = {store_.utf8_size(), store_.utf8_size()};
    undo_.clear();
    redo_.clear();
    history_bytes_ = 0U;
}

bool TextEditingEngine::show_text(std::string_view text, TextSelection selection) {
    if (text == store_.utf8()) return false;
    replace_whole_text(text);
    selection_ = selection;
    return true;
}

std::string TextEditingEngine::selected_text() const {
    const Utf8Offset start = selection_.start();
    const std::size_t length = selection_.length();
    const std::string_view source = store_.utf8();
    const std::string_view selected = source.substr(start.value(), length);
    const std::string result(selected);
    return result;
}

TextEditingEngine::Snapshot TextEditingEngine::snapshot() const {
    const std::string value(store_.utf8());
    const Snapshot result{.text = value, .selection = selection_};
    return result;
}

void TextEditingEngine::apply_snapshot(Snapshot snapshot) {
    host_.editing_restoring();
    replace_whole_text(snapshot.text);
    selection_ = snapshot.selection;
    host_.editing_text_changed();
}

void TextEditingEngine::push_history(std::deque<Snapshot>& history, Snapshot snapshot) {
    history_bytes_ += snapshot.text.size();
    history.push_back(std::move(snapshot));
    while (undo_.size() + redo_.size() > maximum_history_entries_ ||
           history_bytes_ > maximum_history_bytes_) {
        std::deque<Snapshot>* source = !undo_.empty() ? &undo_ : &redo_;
        history_bytes_ -= (*source).front().text.size();
        (*source).pop_front();
    }
}

void TextEditingEngine::clear_redo() noexcept {
    for (const Snapshot& entry : redo_) {
        history_bytes_ -= entry.text.size();
    }
    redo_.clear();
}

void TextEditingEngine::clear_history() noexcept {
    undo_.clear();
    redo_.clear();
    history_bytes_ = 0U;
}

bool TextEditingEngine::undo() {
    host_.editing_require_mutable();
    if (read_only_ || undo_.empty()) {
        return false;
    }
    if (multiline_ && TextBox::validate_multiline_text(undo_.back().text) != TextBox::MultilineValidation::valid) {
        return false;
    }
    Snapshot target = std::move(undo_.back());
    history_bytes_ -= target.text.size();
    undo_.pop_back();
    push_history(redo_, snapshot());
    apply_snapshot(std::move(target));
    return true;
}

bool TextEditingEngine::redo() {
    host_.editing_require_mutable();
    if (read_only_ || redo_.empty()) {
        return false;
    }
    if (multiline_ && TextBox::validate_multiline_text(redo_.back().text) != TextBox::MultilineValidation::valid) {
        return false;
    }
    Snapshot target = std::move(redo_.back());
    history_bytes_ -= target.text.size();
    redo_.pop_back();
    push_history(undo_, snapshot());
    apply_snapshot(std::move(target));
    return true;
}

bool TextEditingEngine::replace(Utf8Offset start, Utf8Offset end, std::string_view replacement, TextEditKind kind) {
    host_.editing_require_mutable();
    const Utf8ValidationResult replacement_validation = validate_utf8(replacement);
    if (read_only_ || start.value() > end.value() ||
        !store_.is_grapheme_boundary(start) ||
        !store_.is_grapheme_boundary(end) ||
        !replacement_validation.valid()) {
        return false;
    }
    if (edit_filter_) {
        TextEdit edit{.kind = kind, .start = start, .end = end, .replacement = replacement};
        const std::uint64_t revision = store_.revision();
        edit_filter_(edit);
        // A refusing filter may have shown a looked-up value; whatever the
        // verdict, the proposal no longer describes a changed text.
        if (store_.revision() != revision) return false;
        if (edit.verdict == TextEditVerdict::refuse) return false;
        if (edit.verdict == TextEditVerdict::rewrite) {
            const bool rewritten = apply_rewrite(edit.rewritten_text, edit.rewritten_caret);
            return rewritten;
        }
    }
    if (multiline_ && replacement.size() > TextBox::maximum_multiline_bytes -
            (text().size() - (end.value() - start.value()))) return false;
    if (multiline_) {
        std::string candidate(text());
        candidate.replace(start.value(), end.value() - start.value(), replacement);
        if (TextBox::validate_multiline_text(candidate) != TextBox::MultilineValidation::valid) return false;
    }
    if (maximum_bytes_ != 0U && replacement.size() > maximum_bytes_ -
            std::min(maximum_bytes_, text().size() - (end.value() - start.value()))) return false;
    const std::size_t removed_scalars =
        store_.scalar_index(end).value() - store_.scalar_index(start).value();
    if (maximum_length_ != 0U &&
        store_.scalar_count().value() - removed_scalars + replacement_validation.scalar_count > maximum_length_) {
        return false;
    }
    const std::string_view existing = store_.utf8().substr(start.value(), end.value() - start.value());
    if (existing == replacement) {
        host_.editing_select({Utf8Offset(start.value() + replacement.size()),
                              Utf8Offset(start.value() + replacement.size())});
        return true;
    }
    Snapshot previous = snapshot();
    static_cast<void>(store_.replace({start, end}, replacement));
    last_change_ = TextChange{.start = start, .removed_bytes = end.value() - start.value(),
                              .inserted_bytes = replacement.size()};
    clear_redo();
    push_history(undo_, std::move(previous));
    Utf8Offset next(start.value() + replacement.size());
    // An insertion may join a following combining sequence or CRLF. Keep the
    // resulting caret on a boundary of the new text, not merely the old text.
    while (!store_.is_grapheme_boundary(next)) next = store_.next_scalar_boundary(next);
    selection_ = {next, next};
    host_.editing_text_changed();
    return true;
}

bool TextEditingEngine::apply_rewrite(std::string_view rewritten, Utf8Offset caret) {
    const Utf8ValidationResult validation = validate_utf8(rewritten);
    if (!validation.valid()) return false;
    if (multiline_ && TextBox::validate_multiline_text(rewritten) != TextBox::MultilineValidation::valid) {
        return false;
    }
    if (maximum_length_ != 0U && validation.scalar_count > maximum_length_) return false;
    if (maximum_bytes_ != 0U && rewritten.size() > maximum_bytes_) return false;
    const bool changes = rewritten != store_.utf8();
    if (changes) {
        Snapshot previous = snapshot();
        replace_whole_text(rewritten);
        clear_redo();
        push_history(undo_, std::move(previous));
    }
    // The caret lands on a grapheme boundary at or after the requested one.
    Utf8Offset next = store_.utf8_size();
    if (caret.value() < next.value() && store_.is_scalar_boundary(caret)) next = caret;
    while (!store_.is_grapheme_boundary(next)) next = store_.next_scalar_boundary(next);
    if (!changes) {
        // Only the caret moves (input that matched a literal, for example).
        host_.editing_select({next, next});
        return true;
    }
    selection_ = {next, next};
    host_.editing_text_changed();
    return true;
}

bool TextEditingEngine::replace_selection(std::string_view replacement) {
    const Utf8Offset start = selection_.start();
    const Utf8Offset end = selection_.end();
    const TextEditKind kind = replacement.empty() ? TextEditKind::deletion : TextEditKind::typing;
    const bool result = replace(start, end, replacement, kind);
    return result;
}

bool TextEditingEngine::delete_selection() {
    if (selection_.empty()) return false;
    const bool result = replace_selection({});
    return result;
}

bool TextEditingEngine::copy() {
    host_.editing_require_mutable();
    HostServices* services = host_.editing_host_services();
    if (selection_.empty() || host_.editing_password_protected() || services == nullptr) {
        return false;
    }
    const std::string value = selected_text();
    const HostServiceStatus operation = (*services).write_clipboard_text(value);
    const bool result = operation.accepted();
    return result;
}

bool TextEditingEngine::cut() {
    host_.editing_require_mutable();
    if (read_only_ || host_.editing_password_protected() || selection_.empty()) {
        return false;
    }
    const bool copied = copy();
    if (!copied) return false;
    const bool result = delete_selection();
    return result;
}

bool TextEditingEngine::paste() {
    host_.editing_require_mutable();
    HostServices* services = host_.editing_host_services();
    if (read_only_ || services == nullptr) {
        return false;
    }
    const HostClipboardTextResult result = (*services).read_clipboard_text();
    if (!result.status.accepted() || !result.has_text) return false;
    const Utf8Offset start = selection_.start();
    const Utf8Offset end = selection_.end();
    const bool replaced = replace(start, end, result.text_utf8, TextEditKind::paste);
    return replaced;
}

Utf8Offset TextEditingEngine::previous_word_boundary(Utf8Offset offset) const {
    if (offset.value() == 0U) return offset;
    Utf8Offset cursor = store_.previous_grapheme_boundary(offset);
    WordClass category = word_class(store_.scalar_at(cursor));
    while (cursor.value() > 0U) {
        const Utf8Offset previous = store_.previous_grapheme_boundary(cursor);
        if (word_class(store_.scalar_at(previous)) != category) break;
        cursor = previous;
    }
    if (category == WordClass::spacing && cursor.value() > 0U) {
        cursor = store_.previous_grapheme_boundary(cursor);
        category = word_class(store_.scalar_at(cursor));
        while (cursor.value() > 0U) {
            const Utf8Offset previous = store_.previous_grapheme_boundary(cursor);
            if (word_class(store_.scalar_at(previous)) != category) break;
            cursor = previous;
        }
    }
    return cursor;
}

Utf8Offset TextEditingEngine::next_word_boundary(Utf8Offset offset) const {
    const Utf8Offset end = store_.utf8_size();
    if (offset == end) return end;
    WordClass category = word_class(store_.scalar_at(offset));
    Utf8Offset cursor = offset;
    while (cursor != end && word_class(store_.scalar_at(cursor)) == category) {
        cursor = store_.next_grapheme_boundary(cursor);
    }
    if (category != WordClass::spacing) {
        while (cursor != end && word_class(store_.scalar_at(cursor)) == WordClass::spacing) {
            cursor = store_.next_grapheme_boundary(cursor);
        }
    }
    return cursor;
}

// Ctrl+Left stops at 0 and at each word start; Ctrl+Right at each later
// word start and at the end. Empty text has no words.
void TextEditingEngine::fill_word_starts(SemanticTextLayout& layout) const {
    const Utf8Offset end = store_.utf8_size();
    if (end.value() == 0U) return;
    Utf8Offset cursor(0U);
    layout.word_starts.push_back(cursor);
    while (cursor != end) {
        cursor = next_word_boundary(cursor);
        layout.word_starts.push_back(cursor);
    }
}

void TextEditingEngine::handle_key(KeyEvent& event) {
    const bool extend = includes(event.modifiers, Modifier::shift);
    const bool command = command_modifier(event.modifiers);
    if (command && event.physical_key == PhysicalKey::a) {
        host_.editing_navigating();
        host_.editing_select({Utf8Offset(0U), store_.utf8_size()});
        event.handled = true;
        return;
    }
    if (command && event.physical_key == PhysicalKey::c) {
        static_cast<void>(copy());
        event.handled = true;
        return;
    }
    if (command && event.physical_key == PhysicalKey::x) {
        const bool cut_done = cut();
        if (cut_done) host_.editing_user_edit(TextEditKind::deletion, false);
        event.handled = true;
        return;
    }
    if (command && event.physical_key == PhysicalKey::v) {
        const bool pasted = paste();
        if (pasted) host_.editing_user_edit(TextEditKind::paste, false);
        event.handled = true;
        return;
    }
    if (command && event.physical_key == PhysicalKey::z) {
        const bool redo_requested = includes(event.modifiers, Modifier::shift);
        if (redo_requested) {
            event.handled = redo();
        } else {
            event.handled = undo();
        }
        return;
    }
    if (command && event.physical_key == PhysicalKey::y) {
        event.handled = redo();
        return;
    }
    if (event.physical_key == PhysicalKey::left || event.physical_key == PhysicalKey::right) {
        // Moving the caret ends a completion and keeps the text.
        host_.editing_navigating();
        const bool line_navigation = includes(event.modifiers, Modifier::meta);
        const bool word_navigation = !line_navigation &&
            (includes(event.modifiers, Modifier::control) || includes(event.modifiers, Modifier::alt));
        Utf8Offset next = selection_.caret;
        if (!extend && !selection_.empty()) {
            next = event.physical_key == PhysicalKey::left ? selection_.start() : selection_.end();
        } else if (line_navigation) {
            next = event.physical_key == PhysicalKey::left ? Utf8Offset(0U) : store_.utf8_size();
        } else if (word_navigation) {
            next = event.physical_key == PhysicalKey::left
                ? previous_word_boundary(selection_.caret)
                : next_word_boundary(selection_.caret);
        } else {
            next = event.physical_key == PhysicalKey::left
                ? store_.previous_grapheme_boundary(selection_.caret)
                : store_.next_grapheme_boundary(selection_.caret);
        }
        host_.editing_select(extend ? TextSelection{selection_.anchor, next} : TextSelection{next, next});
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::home || event.physical_key == PhysicalKey::end) {
        host_.editing_navigating();
        const Utf8Offset next = event.physical_key == PhysicalKey::home ? Utf8Offset(0U) : store_.utf8_size();
        host_.editing_select(extend ? TextSelection{selection_.anchor, next} : TextSelection{next, next});
        event.handled = true;
        return;
    }
    if (!read_only_ && (event.physical_key == PhysicalKey::backspace ||
                        event.physical_key == PhysicalKey::delete_forward)) {
        if (!selection_.empty()) {
            event.handled = delete_selection();
        } else {
            const bool line_deletion = includes(event.modifiers, Modifier::meta);
            const bool word_deletion = !line_deletion &&
                (includes(event.modifiers, Modifier::control) || includes(event.modifiers, Modifier::alt));
            Utf8Offset other{};
            if (line_deletion) {
                other = event.physical_key == PhysicalKey::backspace ? Utf8Offset(0U) : store_.utf8_size();
            } else if (word_deletion) {
                other = event.physical_key == PhysicalKey::backspace
                    ? previous_word_boundary(selection_.caret)
                    : next_word_boundary(selection_.caret);
            } else {
                other = event.physical_key == PhysicalKey::backspace
                    ? store_.previous_grapheme_boundary(selection_.caret)
                    : store_.next_grapheme_boundary(selection_.caret);
            }
            event.handled = replace(std::min(other, selection_.caret), std::max(other, selection_.caret), {},
                                    TextEditKind::deletion);
        }
        if (event.handled) host_.editing_user_edit(TextEditKind::deletion, false);
    }
}

void TextEditingEngine::handle_text_input(TextInputEvent& event) {
    if (read_only_ || !validate_utf8(event.text_utf8).valid()) {
        return;
    }
    Utf8Offset start = selection_.start();
    Utf8Offset end = selection_.end();
    if (event.replacement_start >= 0 && event.replacement_length >= 0) {
        try {
            // Widen validated nonnegative host units before summation; never
            // add two signed 32-bit offsets at the foreign-input boundary.
            const std::size_t first_unit = static_cast<std::size_t>(event.replacement_start);
            const std::size_t unit_count = static_cast<std::size_t>(event.replacement_length);
            const std::size_t last_unit = first_unit + unit_count;
            const Utf16Offset first(first_unit);
            const Utf16Offset last(last_unit);
            start = store_.utf8_offset(first);
            end = store_.utf8_offset(last);
        } catch (const std::out_of_range&) {
            return;
        }
    }
    // An empty composition is the input method cancelling one.
    composing_ = event.composing && !event.text_utf8.empty();
    event.handled = replace(start, end, event.text_utf8, TextEditKind::typing);
    if (event.handled) host_.editing_user_edit(TextEditKind::typing, event.composing);
}

} // namespace gui_forms::detail
