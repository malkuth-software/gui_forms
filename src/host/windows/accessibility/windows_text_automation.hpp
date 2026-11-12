#pragma once

// UI Automation's TextPattern for editable text (stage A), on the A-T1 core:
// Window::semantic_text_layout for the units and boxes, and
// Window::set_semantic_text_selection for Select. Win32, COM and UIA types
// stay inside src/host/windows/.
//
// Which nodes: a control's own text box node that publishes a text
// selection (TextBox, MaskedTextBox, TokenBox's editor). A password box
// publishes none (A-T2), so it offers no TextPattern.
//
// Offsets: a range holds two UTF-8 byte offsets into the node's value, as
// the core does; text crosses to UI Automation as UTF-16 only in GetText
// and FindText. Units (A-T2): Character is a grapheme (a line break is one
// character), Word follows the control's own word rule, Line a visual line,
// Paragraph a logical line with its break, and Format, Page and Document
// the whole text. No unit is a sentence.
//
// Lifetime and staleness (house style 6 and 9): a range holds a shared
// reference to its window's SemanticSource and its element's identity, and
// nothing else; COM reference counting ends its life. Every call reads the
// current snapshot (and, only when it needs boxes or units, the layout,
// reused while the snapshot is: SemanticSource::text_layout), clamps the
// range to the text as it now is (to
// its end, then back to the start of a character), and answers
// UIA_E_ELEMENTNOTAVAILABLE once the window has gone or the node has left
// the tree, never another node's text.
//
// Threading: as every provider (windows_automation.hpp), ranges use COM
// threading, so every call arrives on the UI thread.
//
// Errors: E_INVALIDARG for a null out-parameter, an unknown endpoint or a
// range of another element or another provider; UIA_E_ELEMENTNOTAVAILABLE
// as above; UIA_E_INVALIDOPERATION when the control refuses a selection
// (an offset off a grapheme boundary, a control outside the focus scope)
// or the node no longer offers text; E_OUTOFMEMORY when an allocation
// fails.

#include "windows_semantic_source.hpp"

#include <uiautomation.h>

#include <memory>

namespace gui_forms::host {

// Whether `node` offers the TextPattern (see above).
[[nodiscard]] bool text_pattern(const SemanticNode& node) noexcept;

// ITextProvider and ITextProvider2 for the element `id` of `source`'s
// window; UiaElement forwards its methods here. Each fills its
// out-parameters with new references the caller owns, or null and an error.
//
// The selection: one range from the selection's start to its end, which is
// degenerate at the caret when nothing is selected.
[[nodiscard]] HRESULT text_selection_ranges(const std::shared_ptr<SemanticSource>& source,
                                            SemanticElementId id, SAFEARRAY** out);
// One range from the first grapheme whose box meets the node's bounds to
// the last; degenerate at the start for empty text.
[[nodiscard]] HRESULT text_visible_ranges(const std::shared_ptr<SemanticSource>& source,
                                          SemanticElementId id, SAFEARRAY** out);
// A degenerate range at the grapheme edge nearest `point`, in screen pixels.
[[nodiscard]] HRESULT text_range_from_point(const std::shared_ptr<SemanticSource>& source,
                                            SemanticElementId id, UiaPoint point,
                                            ITextRangeProvider** out);
// The whole text.
[[nodiscard]] HRESULT text_document_range(const std::shared_ptr<SemanticSource>& source,
                                          SemanticElementId id, ITextRangeProvider** out);
// A degenerate range at the caret; `active` says whether the node has focus.
[[nodiscard]] HRESULT text_caret_range(const std::shared_ptr<SemanticSource>& source,
                                       SemanticElementId id, BOOL* active,
                                       ITextRangeProvider** out);

// Raises TextSelectionChanged when a text node's selection changed from
// `before` to `after`, and TextChanged when its text did. UI thread only.
void raise_text_changes(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                        const SemanticNode& before, const SemanticNode& after) noexcept;

} // namespace gui_forms::host
