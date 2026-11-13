#include "gui_forms/controls/text_box_base.hpp"

#include "../text_editing/text_editing_engine.hpp"

#include <stdexcept>
#include <utility>

namespace gui_forms {

TextBoxBase::TextBoxBase(StableId stable_id, UndoHistory history)
    : Panel(std::move(stable_id)), undo_history_(history) {}

TextBoxBase::~TextBoxBase() = default;

TextSelection TextBoxBase::selection() const noexcept {
    const detail::TextEditingEngine* const engine = editing_engine();
    if (engine == nullptr) return TextSelection{};
    const TextSelection result = (*engine).selection();
    return result;
}

void TextBoxBase::select(Utf8Offset anchor, Utf8Offset caret) {
    require_mutable();
    const detail::TextEditingEngine* const engine = editing_engine();
    if (engine == nullptr) throw std::logic_error("TextBoxBase selection needs the control's editing engine");
    const TextStore& store = (*engine).store();
    if (!store.is_grapheme_boundary(anchor) || !store.is_grapheme_boundary(caret)) {
        throw std::out_of_range("TextBoxBase selection must use grapheme boundaries");
    }
    const TextSelection chosen{anchor, caret};
    apply_selection(chosen);
}

void TextBoxBase::select_all() {
    require_mutable();
    const detail::TextEditingEngine* const engine = editing_engine();
    if (engine == nullptr) return;
    const TextSelection whole{Utf8Offset(0U), (*engine).store().utf8_size()};
    apply_selection(whole);
}

std::string TextBoxBase::selected_text() const {
    const detail::TextEditingEngine* const engine = editing_engine();
    if (engine == nullptr) return std::string{};
    std::string result = (*engine).selected_text();
    return result;
}

bool TextBoxBase::can_undo() const noexcept {
    if (undo_history_ == UndoHistory::none) return false;
    const detail::TextEditingEngine* const engine = editing_engine();
    const bool result = engine != nullptr && (*engine).can_undo();
    return result;
}

bool TextBoxBase::undo() {
    if (undo_history_ == UndoHistory::none) return false;
    detail::TextEditingEngine* const engine = editing_engine();
    if (engine == nullptr) return false;
    const bool result = (*engine).undo();
    return result;
}

void TextBoxBase::clear_undo_history() {
    require_mutable();
    if (undo_history_ == UndoHistory::none) return;
    detail::TextEditingEngine* const engine = editing_engine();
    if (engine == nullptr) return;
    (*engine).clear_history();
}

bool TextBoxBase::copy() {
    detail::TextEditingEngine* const engine = editing_engine();
    if (engine == nullptr) return false;
    const bool result = (*engine).copy();
    return result;
}

bool TextBoxBase::cut() {
    detail::TextEditingEngine* const engine = editing_engine();
    if (engine == nullptr) return false;
    const bool result = (*engine).cut();
    return result;
}

bool TextBoxBase::paste() {
    detail::TextEditingEngine* const engine = editing_engine();
    if (engine == nullptr) return false;
    const bool result = (*engine).paste();
    return result;
}

detail::TextEditingEngine* TextBoxBase::engine_of(const TextBoxBase& other) noexcept {
    detail::TextEditingEngine* const engine = other.editing_engine();
    return engine;
}

} // namespace gui_forms
