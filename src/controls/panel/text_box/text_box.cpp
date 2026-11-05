#include "gui_forms/controls/panel/text_box/text_box.hpp"

#include "text_box_auto_complete_session.hpp"
#include "../text_editing/text_editing_engine.hpp"

#include "../input_control_utilities.hpp"
#include "gui_forms/detail/algorithm/binary_search.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace {

// The caret shows and hides for this long each.
constexpr FrameInterval caret_blink_interval = std::chrono::milliseconds(530);

} // namespace
using namespace input_control_detail;

TextBox::MultilineValidation TextBox::validate_multiline_text(std::string_view text) {
    if (text.size() > maximum_multiline_bytes) return MultilineValidation::document_too_large;
    const Utf8ValidationResult validation = validate_utf8(text);
    if (!validation.valid()) return MultilineValidation::invalid_utf8;
    const TextStore candidate(text);
    const std::size_t line_count = candidate.line_count();
    for (std::size_t i = 0U; i < line_count; ++i) {
        const LineIndex line_index(i);
        const Utf8Range range = candidate.line_content_range(line_index);
        const std::size_t line_bytes = range.end.value() - range.start.value();
        if (line_bytes > maximum_multiline_line_bytes) return MultilineValidation::line_too_long;
    }
    return MultilineValidation::valid;
}

// -- the editing engine's host -----------------------------------------------

// How TextBox's editing engine reaches the box: each call forwards to the
// box's own reaction, so the box keeps its cache updates, invalidation and
// event order. The box owns the host and outlives the engine that borrows it.
class TextBox::EditingHost final : public detail::TextEditingHost {
public:
    explicit EditingHost(TextBox& box) noexcept : box_(box) {}

    void editing_require_mutable() const override;
    void editing_restoring() override;
    void editing_text_changed() override;
    void editing_select(TextSelection selection) override;
    void editing_navigating() override;
    void editing_user_edit(TextEditKind kind, bool composing) override;
    [[nodiscard]] HostServices* editing_host_services() const override;
    [[nodiscard]] bool editing_password_protected() const override;

private:
    TextBox& box_;
};

void TextBox::EditingHost::editing_require_mutable() const {
    box_.require_mutable();
}

void TextBox::EditingHost::editing_restoring() {
    box_.end_auto_complete(false);
}

void TextBox::EditingHost::editing_text_changed() {
    box_.text_edited();
}

void TextBox::EditingHost::editing_select(TextSelection selection) {
    box_.set_selection(selection);
}

void TextBox::EditingHost::editing_navigating() {
    // Moving the caret ends a completion and keeps the text.
    box_.end_auto_complete(false);
}

void TextBox::EditingHost::editing_user_edit(TextEditKind kind, bool composing) {
    box_.auto_complete_after_edit(kind, composing);
}

HostServices* TextBox::EditingHost::editing_host_services() const {
    Window* owner = box_.window();
    if (owner == nullptr) return nullptr;
    HostServices* services = (*owner).host_services();
    return services;
}

bool TextBox::EditingHost::editing_password_protected() const {
    const bool masked = box_.password_protected();
    return masked;
}

// -- TextBox -------------------------------------------------------------------

TextBox::TextBox(StableId stable_id, std::string text)
    : Panel(std::move(stable_id)),
      editing_host_(std::make_unique<EditingHost>(*this)),
      editing_(std::make_unique<detail::TextEditingEngine>(*editing_host_, text)) {
    set_paint_plane(PaintPlane::control);
    set_border_style(BorderStyle::sunken);
    set_focusable(true);
    set_cursor(CursorKind::text);
    set_tool_tip_presentation(ToolTipPresentation::field_hint);
    auto_complete_custom_source_ = std::make_shared<AutoCompleteStringCollection>();
    define_bindable_property({
        {"text", BindingValueKind::text, "Appearance",
         "Editable UTF-8 text content.", BindingValue{std::string{}},
         invalidation::text_content},
        detail::BoundMemberFunction<BindingValue (TextBox::*)() const>(
            *this, &TextBox::text_property_value),
        detail::ConvertedPropertySetter<TextBox, std::string>(
            *this, &TextBox::set_text, BindingValueKind::text,
            "TextBox.Text binding requires text"),
        detail::EventChangeConnector<const std::string&>(text_changed_), {}, {}});
}

TextBox::~TextBox() {
    // The window may close the list after this box is gone; its revocation
    // and collection callbacks must not reach a destroyed box.
    if (auto_complete_session_) {
        detail::TextBoxAutoCompleteSession& session = *auto_complete_session_;
        session.revocation.disconnect();
        session.collection_change.disconnect();
    }
}

std::string_view TextBox::text() const noexcept {
    const std::string_view result = (*editing_).text();
    return result;
}

bool TextBox::read_only() const noexcept {
    const bool result = (*editing_).read_only();
    return result;
}

std::size_t TextBox::maximum_length() const noexcept {
    const std::size_t result = (*editing_).maximum_length();
    return result;
}

TextSelection TextBox::selection() const noexcept {
    const TextSelection result = (*editing_).selection();
    return result;
}

bool TextBox::can_undo() const noexcept {
    const bool result = (*editing_).can_undo();
    return result;
}

bool TextBox::can_redo() const noexcept {
    const bool result = (*editing_).can_redo();
    return result;
}

void TextBox::set_edit_filter(Delegate<TextEdit&> filter) noexcept {
    (*editing_).set_edit_filter(filter);
}

BindingValue TextBox::text_property_value() const {
    const std::string value((*editing_).text());
    const BindingValue result{value};
    return result;
}

void TextBox::set_text(std::string text) {
    require_mutable();
    if (multiline_ && validate_multiline_text(text) != MultilineValidation::valid) {
        throw std::invalid_argument("Multiline TextBox requires valid UTF-8, at most 1 MiB total and 4096 bytes per line");
    }
    if (text == (*editing_).text()) {
        return;
    }
    end_auto_complete(false);
    // The engine ends any composition and the history with the old text.
    (*editing_).set_text(text);
    layout_positions_.clear();
    layout_offsets_.clear();
    horizontal_offset_ = 0.0;
    vertical_offset_ = 0.0;
    reset_caret_blink();
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    const std::string value((*editing_).text());
    publish_change(text_changed_, value);
    publish_change(selection_changed_, (*editing_).selection());
}

void TextBox::text_edited() {
    layout_positions_.clear();
    layout_offsets_.clear();
    reset_caret_blink();
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    const std::string value((*editing_).text());
    publish_change(text_changed_, value);
    publish_change(selection_changed_, (*editing_).selection());
}

std::string TextBox::empty_field_text() const {
    if (!placeholder_.empty()) return placeholder_;
    if (!focused_) return {};
    return field_hint_text();
}

void TextBox::set_placeholder_text(std::string text) {
    require_mutable();
    if (!validate_utf8(text).valid()) {
        throw std::invalid_argument("TextBox placeholder must be valid UTF-8");
    }
    if (placeholder_ == text) {
        return;
    }
    placeholder_ = std::move(text);
    invalidate(Dirty::paint | Dirty::semantics);
}

void TextBox::set_read_only(bool read_only) {
    require_mutable();
    if ((*editing_).read_only() == read_only) {
        return;
    }
    end_auto_complete(false);
    (*editing_).set_read_only(read_only);
    invalidate(Dirty::paint | Dirty::semantics);
}

void TextBox::set_multiline(bool enabled_value) {
    require_mutable();
    if (multiline_ == enabled_value) return;
    if (enabled_value && (password_protected() || validate_multiline_text(text()) != MultilineValidation::valid)) {
        throw std::invalid_argument("Multiline TextBox requires unmasked valid UTF-8, at most 1 MiB total and 4096 bytes per line");
    }
    end_auto_complete(false);
    multiline_ = enabled_value;
    (*editing_).set_multiline(enabled_value);
    multiline_width_ = -1.0;
    horizontal_offset_ = 0.0;
    vertical_offset_ = 0.0;
    reset_caret_blink();
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void TextBox::set_word_wrap(bool enabled_value) {
    require_mutable();
    if (word_wrap_ == enabled_value) return;
    word_wrap_ = enabled_value;
    multiline_width_ = -1.0;
    horizontal_offset_ = 0.0;
    reset_caret_blink();
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void TextBox::set_newline_sequence(std::string sequence) {
    require_mutable();
    if (sequence != "\n" && sequence != "\r" && sequence != "\r\n") {
        throw std::invalid_argument("TextBox newline must be LF, CR, or CRLF");
    }
    newline_ = std::move(sequence);
}

void TextBox::set_accepts_tab(bool enabled_value) {
    require_mutable();
    accepts_tab_ = enabled_value;
}

void TextBox::set_accepts_enter(bool enabled_value) {
    require_mutable();
    accepts_enter_ = enabled_value;
}

void TextBox::set_accepts_escape(bool enabled_value) {
    require_mutable();
    accepts_escape_ = enabled_value;
}

void TextBox::set_maximum_length(std::size_t length) {
    require_mutable();
    if (length > maximum_configured_length_) {
        throw std::out_of_range(
            "TextBox maximum length must be zero or no greater than 16 Mi scalars");
    }
    if ((*editing_).maximum_length() == length) return;
    (*editing_).set_maximum_length(length);
    invalidate(Dirty::semantics);
}

void TextBox::set_password_character(char32_t character) {
    require_mutable();
    if (multiline_ && character != U'\0') {
        throw std::invalid_argument("Multiline TextBox does not support password masking");
    }
    if (!valid_password_character(character)) {
        throw std::invalid_argument(
            "TextBox password character must be one printable Unicode scalar");
    }
    if (password_character_ == character) return;
    end_auto_complete(false);
    password_character_ = character;
    layout_positions_.clear();
    layout_offsets_.clear();
    horizontal_offset_ = 0.0;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void TextBox::set_use_system_password_character(bool enabled_value) {
    require_mutable();
    if (multiline_ && enabled_value) {
        throw std::invalid_argument("Multiline TextBox does not support password masking");
    }
    if (use_system_password_character_ == enabled_value) return;
    end_auto_complete(false);
    use_system_password_character_ = enabled_value;
    layout_positions_.clear();
    layout_offsets_.clear();
    horizontal_offset_ = 0.0;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void TextBox::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) {
        throw std::invalid_argument("TextBox font specification is invalid");
    }
    if (font_ == font) {
        return;
    }
    font_ = font;
    layout_positions_.clear();
    layout_offsets_.clear();
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void TextBox::select(Utf8Offset anchor, Utf8Offset caret) {
    require_mutable();
    const TextStore& store = (*editing_).store();
    if (!store.is_grapheme_boundary(anchor) ||
        !store.is_grapheme_boundary(caret)) {
        throw std::out_of_range("TextBox selection must use grapheme boundaries");
    }
    set_selection({anchor, caret});
}

void TextBox::select_all() {
    set_selection({Utf8Offset(0U), (*editing_).store().utf8_size()});
}

std::string TextBox::selected_text() const {
    const std::string result = (*editing_).selected_text();
    return result;
}

void TextBox::clear_undo_history() {
    require_mutable();
    (*editing_).clear_history();
}

bool TextBox::undo() {
    const bool result = (*editing_).undo();
    return result;
}

bool TextBox::redo() {
    const bool result = (*editing_).redo();
    return result;
}

bool TextBox::replace_selection(std::string_view replacement) {
    const bool result = (*editing_).replace_selection(replacement);
    return result;
}

bool TextBox::delete_selection() {
    const bool result = (*editing_).delete_selection();
    return result;
}

bool TextBox::copy() {
    const bool result = (*editing_).copy();
    return result;
}

bool TextBox::cut() {
    const bool result = (*editing_).cut();
    return result;
}

bool TextBox::paste() {
    const bool result = (*editing_).paste();
    return result;
}

void TextBox::set_selection(TextSelection selection, bool reveal_caret) {
    const bool upstream = std::exchange(next_upstream_, false);
    if ((*editing_).selection() == selection) {
        if (reveal_caret) {
            reset_caret_blink();
        }
        caret_upstream_ = upstream;
        invalidate(Dirty::paint);
        return;
    }
    (*editing_).set_selection_value(selection);
    if (reveal_caret) {
        reset_caret_blink();
    }
    caret_upstream_ = upstream;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(selection_changed_, (*editing_).selection());
}

double TextBox::boundary_x(Utf8Offset offset) const noexcept {
    if (layout_positions_.empty() ||
        layout_positions_.size() != layout_offsets_.size() ||
        layout_text_scale_ != effective_text_scale()) {
        const GraphemeIndex index = (*editing_).store().grapheme_index(offset);
        const FontSpec font = effective_font(font_);
        const double grapheme_count = static_cast<double>(index.value());
        const double position = grapheme_count * font.size * 0.55;
        return position;
    }
    const std::size_t found = detail::lower_bound_index(
        std::span<const std::uint64_t>(layout_offsets_), offset.value());
    const std::size_t index = found == layout_offsets_.size()
        ? layout_offsets_.size() - 1U
        : found;
    return layout_positions_[index];
}

std::string TextBox::display_text() const {
    if (!password_protected()) {
        const std::string result((*editing_).store().utf8());
        return result;
    }
    const char32_t mask = use_system_password_character_
        ? U'\u2022' : password_character_;
    const std::string encoded = utf8_scalar(mask);
    std::string result{};
    const std::size_t count = (*editing_).store().grapheme_count().value();
    const std::size_t maximum_count = result.max_size() / encoded.size();
    if (count > maximum_count) throw std::length_error("TextBox mask exceeds string capacity");
    const std::size_t capacity = encoded.size() * count;
    result.reserve(capacity);
    for (std::size_t index = 0U; index < count; ++index) {
        result += encoded;
    }
    return result;
}

void TextBox::rebuild_multiline_rows(const double width) {
    const FontSpec font = effective_font(font_);
    // Device-pixel rounding can change logical advances/ascent while the
    // provider address, authored font and logical viewport remain unchanged.
    const Window* owner = window();
    double device_scale = 1.0;
    const TextMetricsProvider* provider = nullptr;
    if (owner != nullptr) {
        device_scale = (*owner).scale();
        provider = (*owner).text_metrics_provider();
    }
    if (!visual_lines_.empty() && multiline_revision_ == (*editing_).store().revision() &&
        multiline_font_ == font && multiline_width_ == width &&
        multiline_provider_ == provider && multiline_device_scale_ == device_scale) return;
    // Invalidate before modifying retained rows: an allocation/metrics failure
    // leaves the cache uncommitted and the next call must rebuild it.
    multiline_width_ = -1.0;
    document_width_ = 0.0;
    const ResolvedTextLayout metrics = resolve_text_layout_utf8("Mg", font);
    // The line box rule prepared text uses: the shaped height, or ascent,
    // descent and the primary face's line gap, whichever is larger.
    line_height_ = std::max(1.0, std::max(metrics.logical_size.height,
        metrics.ascent + metrics.descent + metrics.line_gap));
    line_ascent_ = metrics.ascent > 0.0 ? metrics.ascent : font.size;
    const ResolvedTextLayout tab_metrics = resolve_text_layout_utf8("    ", font);
    const double tab_width = std::max(1.0, tab_metrics.logical_size.width);
    const std::string_view source = text();
    const std::size_t logical_count = (*editing_).store().line_count();
    std::size_t maximum_line_bytes = 0U;
    for (std::size_t logical = 0U; logical < logical_count; ++logical) {
        const Utf8Range range = (*editing_).store().line_content_range(LineIndex(logical));
        const std::size_t line_bytes = range.end.value() - range.start.value();
        maximum_line_bytes = std::max(maximum_line_bytes, line_bytes);
    }
    // One call-owned scratch row is reused across all visual rows. Byte bounds
    // dominate grapheme/run counts (4096 maximum), so the measured inner walk
    // cannot grow storage. Retained output rows reuse their existing capacity.
    const std::size_t boundary_capacity = maximum_line_bytes + 1U;
    VisualLine line{};
    line.offsets.reserve(boundary_capacity);
    line.positions.reserve(boundary_capacity);
    line.runs.reserve(maximum_line_bytes);
    visual_lines_.reserve(logical_count);
    std::size_t row_count = 0U;
    // Keep complete visual-row shaping runs. Prefix measurement is quadratic
    // within the admitted 4096-byte logical-line bound, cached across paints.
    // Tabs are explicit shaping boundaries; arbitrary chunk cuts are forbidden.
    for (std::size_t logical = 0U; logical < logical_count; ++logical) {
        const Utf8Range range = (*editing_).store().line_content_range(LineIndex(logical));
        std::size_t cursor = range.start.value();
        do {
            line.offsets.clear();
            line.positions.clear();
            line.runs.clear();
            line.offsets.push_back(cursor);
            line.positions.push_back(0.0);
            std::size_t last_space{};
            bool wrapped{};
            while (cursor < range.end.value() && !wrapped) {
                const std::size_t run_start = cursor;
                const double run_x = line.positions.back();
                const bool tab = source[cursor] == '\t';
                if (tab) {
                    const double x = (std::floor(run_x / tab_width) + 1.0) * tab_width;
                    if (word_wrap_ && x > width && line.offsets.size() > 1U) {
                        wrapped = true;
                        break;
                    }
                    const Utf8Offset position(cursor);
                    const Utf8Offset following = (*editing_).store().next_grapheme_boundary(position);
                    cursor = following.value();
                    line.offsets.push_back(cursor);
                    line.positions.push_back(x);
                    last_space = line.offsets.size() - 1U;
                    continue;
                }
                std::size_t run_end = cursor;
                while (cursor < range.end.value() && source[cursor] != '\t') {
                    const Utf8Offset position(cursor);
                    const Utf8Offset following = (*editing_).store().next_grapheme_boundary(position);
                    const std::size_t next = following.value();
                    const std::size_t prefix_bytes = next - run_start;
                    const std::string_view prefix = source.substr(run_start, prefix_bytes);
                    const ResolvedTextLayout prefix_metrics = resolve_text_layout_utf8(prefix, font);
                    const double x = run_x + prefix_metrics.logical_size.width;
                    if (word_wrap_ && x > width && line.offsets.size() > 1) {
                        wrapped = true;
                        break;
                    }
                    cursor = next;
                    run_end = next;
                    line.offsets.push_back(next);
                    line.positions.push_back(x);
                    const char32_t scalar = (*editing_).store().scalar_at(position);
                    if (scalar == U' ' || scalar == U'\t') last_space = line.offsets.size() - 1;
                }
                if (run_end != run_start) line.runs.push_back({run_start, run_end, run_x});
            }
            if (wrapped && last_space > 0 && last_space + 1 < line.offsets.size()) {
                cursor = line.offsets[last_space];
                line.offsets.resize(last_space + 1);
                line.positions.resize(last_space + 1);
                while (!line.runs.empty() && line.runs.back().start >= cursor) line.runs.pop_back();
                if (!line.runs.empty()) line.runs.back().end = std::min(line.runs.back().end, cursor);
            }
            document_width_ = std::max(document_width_, line.positions.back());
            if (row_count == visual_lines_.size()) {
                visual_lines_.emplace_back();
            }
            // Copy only the live row; scratch ownership stays in this call.
            visual_lines_[row_count] = line;
            ++row_count;
        } while (cursor < range.end.value());
    }
    visual_lines_.resize(row_count);
    multiline_revision_ = (*editing_).store().revision();
    multiline_font_ = font;
    multiline_width_ = width;
    multiline_provider_ = provider;
    multiline_device_scale_ = device_scale;
    reveal_pending_ = true;
}

std::size_t TextBox::visual_line_count() {
    require_mutable();
    if (!multiline_) return 1;
    ensure_multiline_layout();
    const std::size_t count = visual_lines_.size();
    return count;
}

std::size_t TextBox::caret_line() const {
    struct BeforeVisualLine final {
        bool operator()(const std::size_t value, const VisualLine& line) const {
            const bool before = value < line.offsets.front();
            return before;
        }
    };
    const std::span<const VisualLine> rows(visual_lines_);
    const BeforeVisualLine before{};
    const std::size_t found = detail::upper_bound_index(rows, (*editing_).selection().caret.value(), before);
    std::size_t row = found == 0U ? 0U : found - 1U;
    if (caret_upstream_ && row > 0 && visual_lines_[row - 1].offsets.back() == (*editing_).selection().caret.value()) --row;
    return row;
}

double TextBox::multiline_boundary_x(std::size_t line, Utf8Offset offset) const {
    const VisualLine& row = visual_lines_[line];
    const std::span<const std::size_t> offsets(row.offsets);
    const std::size_t found = detail::lower_bound_index(offsets, offset.value());
    const std::size_t index = std::min(found, row.positions.size() - 1U);
    return row.positions[index];
}

Utf8Offset TextBox::position_in_line(std::size_t line, double x) const {
    const VisualLine& row = visual_lines_[line];
    // Prefix advances need not be monotonic for every script. Nearest logical
    // boundary is deterministic without assuming sorted visual positions.
    std::size_t best{};
    double distance = std::abs(x - row.positions.front());
    for (std::size_t i = 1; i < row.positions.size(); ++i) {
        const double candidate = std::abs(x - row.positions[i]);
        if (candidate <= distance) { best = i; distance = candidate; }
    }
    const Utf8Offset result(row.offsets[best]);
    return result;
}

Utf8Offset TextBox::multiline_position_at(double x, double y) {
    ensure_multiline_layout();
    const Point offset = scroll_offset();
    const double row = std::max(0.0, std::floor((y - 4.0 + offset.y) / line_height_));
    const std::size_t index = std::min(static_cast<std::size_t>(row), visual_lines_.size() - 1);
    const Utf8Offset position = position_in_line(index, x - text_left_ + offset.x);
    next_upstream_ = position.value() == visual_lines_[index].offsets.back();
    return position;
}

void TextBox::reveal_multiline_caret() {
    const Rect viewport = multiline_text_rectangle();
    const double width = std::max(1.0, viewport.width);
    const double height = std::max(1.0, viewport.height);
    Point offset = scroll_offset();
    if (reveal_pending_) {
        const std::size_t row = caret_line();
        const double x = multiline_boundary_x(row, (*editing_).selection().caret);
        const double y = static_cast<double>(row) * line_height_;
        if (y < offset.y) offset.y = y;
        if (y + line_height_ > offset.y + height) offset.y = y + line_height_ - height;
        if (x < offset.x) offset.x = x;
        if (x > offset.x + width - 1.0) offset.x = x - width + 1.0;
        reveal_pending_ = false;
    }
    set_multiline_scroll(offset);
}

AnatomyStateSet TextBox::text_states() const {
    AnatomyStateSet states = anatomy_states();
    states.erase(AnatomyState::hot);
    return states;
}

// A text run's part: the editor's content, or the selection's for the run
// inside a selection band. An overridden field's ink is authored: its text
// colour, or the highlight inside the band (T4-1).
PartPaint TextBox::text_part_template(FontSpec font, bool selected) const {
    const bool themed = !has_background_override() && !has_style_override();
    PartPaint part;
    part.family = selected ? ControlVisualRole::selection : ControlVisualRole::editor;
    part.role = PartRole::content;
    part.states = text_states();
    if (selected) part.states.insert(AnatomyState::selected);
    part.font = font;
    if (!themed) part.authored_foreground = selected ? style().highlight : style().text;
    return part;
}

// A selection band: the selection's plate, the accent as its ground in an
// overridden field (T4-1).
void TextBox::paint_selection_band(Painter& painter, Rect band) const {
    const bool themed = !has_background_override() && !has_style_override();
    PartPaint plate;
    plate.family = ControlVisualRole::selection;
    plate.role = PartRole::selection;
    plate.bounds = band;
    plate.states = text_states();
    plate.states.insert(AnatomyState::selected);
    plate.states.insert(AnatomyState::focused);
    if (!themed) plate.authored_background = style().accent;
    paint_part(painter, *this, plate);
}

void TextBox::paint_caret(Painter& painter, Rect caret) const {
    PartPaint part;
    part.family = ControlVisualRole::editor;
    part.role = PartRole::caret;
    part.bounds = caret;
    part.states = text_states();
    if (has_background_override() || has_style_override()) part.authored_foreground = style().text;
    paint_part(painter, *this, part);
}

Rect TextBox::paint_multiline(Painter& painter) {
    ensure_multiline_layout();
    reveal_multiline_caret();
    const Rect viewport = multiline_text_rectangle();
    const double width = viewport.width;
    const double height = viewport.height;
    const Point offset = scroll_offset();
    const FontSpec font = effective_font(font_);
    // Text runs, selection bands, the cue and the caret are parts the
    // theme's engine draws (text_part_template); a style or background
    // override supplies the text's ink and the band's ground (T4-1).
    const PartPaint text_part = text_part_template(font, false);
    const PartPaint selected_part = text_part_template(font, true);
    painter.save();
    painter.clip_rect({text_left_, 4.0, width, height});
    const std::size_t first = static_cast<std::size_t>(offset.y / line_height_);
    const std::size_t last = std::min(visual_lines_.size(), first +
        static_cast<std::size_t>(std::ceil(height / line_height_)) + 1);
    for (std::size_t i = first; i < last; ++i) {
        const VisualLine& row = visual_lines_[i];
        const double y = 4.0 + static_cast<double>(i) * line_height_ - offset.y;
        const double origin = text_left_ - offset.x;
        const bool selected = focused_ && !(*editing_).selection().empty() &&
            (*editing_).selection().start().value() <= row.offsets.back() &&
            (*editing_).selection().end().value() > row.offsets.front();
        Rect selection_bounds{};
        if (selected) {
            const double left = multiline_boundary_x(i, (*editing_).selection().start());
            double right = multiline_boundary_x(i, (*editing_).selection().end());
            if ((*editing_).selection().end().value() > row.offsets.back()) right += std::max(3.0, font.size * 0.35);
            selection_bounds = {origin + left, y, std::max(0.0, right - left), line_height_};
            paint_selection_band(painter, selection_bounds);
        }
        const std::string_view source = text();
        if (!selected) {
            for (const VisualRun& run : row.runs) {
                const Point baseline{origin + run.x, y + line_ascent_};
                PartPaint run_part = text_part;
                run_part.text = source.substr(run.start, run.end - run.start);
                paint_text_part_at(painter, *this, run_part, baseline, 0.0);
            }
            continue;
        }
        // Each glyph is drawn once: in the selection's ink inside the band,
        // in the text's ink to either side.
        const double band_right = selection_bounds.x + selection_bounds.width;
        const std::array<Rect, 3> spans{{
            {text_left_, y, std::max(0.0, selection_bounds.x - text_left_), line_height_},
            selection_bounds,
            {band_right, y, std::max(0.0, text_left_ + width - band_right), line_height_},
        }};
        for (std::size_t span = 0U; span < spans.size(); ++span) {
            if (spans[span].width <= 0.0) continue;
            painter.save();
            painter.clip_rect(spans[span]);
            for (const VisualRun& run : row.runs) {
                const Point baseline{origin + run.x, y + line_ascent_};
                PartPaint run_part = span == 1U ? selected_part : text_part;
                run_part.text = source.substr(run.start, run.end - run.start);
                paint_text_part_at(painter, *this, run_part, baseline, 0.0);
            }
            painter.restore();
        }
    }
    if (text().empty()) {
        const std::string empty_text = empty_field_text();
        if (!empty_text.empty()) {
            PartPaint cue = text_part;
            cue.tone = PartTextTone::secondary;
            cue.authored_foreground.reset();
            cue.text = empty_text;
            paint_text_part_at(painter, *this, cue, {text_left_, 4.0 + line_ascent_}, width);
        }
    }
    Rect caret_bounds{};
    if (focused_) {
        const std::size_t row = caret_line();
        const double x = text_left_ - offset.x + multiline_boundary_x(row, (*editing_).selection().caret);
        const double y = 4.0 + static_cast<double>(row) * line_height_ - offset.y;
        caret_bounds = {x, y, 0.0, line_height_};
        if (caret_visible_) paint_caret(painter, {x - 0.5, y, 1.0, line_height_});
    }
    painter.restore();
    return caret_bounds;
}

Utf8Offset TextBox::position_at(double local_x) const noexcept {
    const double content_x = std::max(0.0,
        local_x - text_left_ + horizontal_offset_);
    if (layout_positions_.empty() ||
        layout_positions_.size() != layout_offsets_.size() ||
        layout_text_scale_ != effective_text_scale()) {
        const FontSpec font = effective_font(font_);
        const std::size_t grapheme = std::min(
            static_cast<std::size_t>(std::lround(content_x /
                                                  (font.size * 0.55))),
            (*editing_).store().grapheme_count().value());
        const GraphemeIndex index(grapheme);
        const Utf8Offset position = (*editing_).store().utf8_offset(index);
        return position;
    }
    const std::size_t index = detail::lower_bound_index(
        std::span<const double>(layout_positions_), content_x);
    if (index == 0U) {
        const Utf8Offset position(layout_offsets_.front());
        return position;
    }
    if (index == layout_positions_.size()) {
        const Utf8Offset position(layout_offsets_.back());
        return position;
    }
    const double left_distance = content_x - layout_positions_[index - 1U];
    const double right_distance = layout_positions_[index] - content_x;
    const std::size_t nearest = left_distance < right_distance ? index - 1U : index;
    const Utf8Offset position(layout_offsets_[nearest]);
    return position;
}

void TextBox::on_paint(Painter& painter, Rect damage) {
    caret_painted_ = true;
    if (caret_waiting_for_paint_) {
        caret_waiting_for_paint_ = false;
        schedule_caret_blink();
    }
    caret_damage_.valid = false;
    const Rect bounds = local_bounds();
    const bool themed = !has_background_override() && !has_style_override();
    // The field is the editor's well, drawn by the theme's engine; an
    // overridden field keeps the panel's own paint.
    if (themed) {
        if (border_style() == BorderStyle::none) {
            // BorderStyle::none is a field without a frame, as in Windows
            // Forms: the editor material's ground only. Composites that
            // hold a borderless TextBox (ComboBox, the up-downs, TokenBox,
            // BreadcrumbTrail, the grid's cell editor) draw the frame
            // themselves (sweep defect D12). The engine's well has its
            // frame, so the ground is drawn from the editor recipe here.
            const ControlVisualRecipe& editor_recipe =
                effective_theme().resolve(ControlVisualRole::editor, visual_context(false, false, false, focused_));
            SurfaceMaterial material = editor_recipe.material;
            material.border.reset();
            paint_surface_material(painter, bounds, material);
        } else {
            PartPaint well;
            well.family = ControlVisualRole::editor;
            well.role = PartRole::well;
            well.bounds = bounds;
            well.states = text_states();
            paint_part(painter, *this, well);
        }
    } else {
        Panel::on_paint(painter, damage);
    }
    if (multiline_) {
        const Rect caret_bounds = paint_multiline(painter);
        const Rect text_clip = multiline_text_rectangle();
        retain_caret_damage(caret_bounds, text_clip);
        return;
    }
    const FontSpec font = effective_font(font_);
    const double right = std::max(text_left_, bounds.width - 4.0);
    const double viewport = std::max(0.0, right - text_left_);
    const std::string presented = display_text();
    const std::string mask = password_protected()
        ? utf8_scalar(use_system_password_character_ ? U'\u2022'
                                                     : password_character_)
        : std::string{};
    layout_positions_.clear();
    layout_offsets_.clear();
    layout_text_scale_ = effective_text_scale();
    const std::size_t count = (*editing_).store().grapheme_count().value();
    layout_positions_.reserve(count + 1U);
    layout_offsets_.reserve(count + 1U);
    const bool masked = password_protected();
    const std::string_view presentation(presented);
    // The last prefix is the whole presentation: its measurement serves the
    // text parts, so the text is measured once.
    Size presented_size{};
    for (std::size_t index = 0U; index <= count; ++index) {
        const Utf8Offset offset = (*editing_).store().utf8_offset(GraphemeIndex(index));
        layout_offsets_.push_back(offset.value());
        const std::size_t presentation_offset = masked
            ? index * mask.size() : offset.value();
        const std::string_view prefix = presentation.substr(0U, presentation_offset);
        const Size prefix_size = painter.measure_text_utf8(prefix, font);
        layout_positions_.push_back(prefix_size.width);
        presented_size = prefix_size;
    }
    const double caret_content_x = boundary_x((*editing_).selection().caret);
    if (caret_content_x < horizontal_offset_) {
        horizontal_offset_ = caret_content_x;
    } else if (caret_content_x > horizontal_offset_ + viewport) {
        horizontal_offset_ = caret_content_x - viewport;
    }
    horizontal_offset_ = std::clamp(horizontal_offset_, 0.0,
        std::max(0.0, layout_positions_.back() - viewport));

    const double origin_x = text_left_ - horizontal_offset_;
    const double selection_x = origin_x + boundary_x((*editing_).selection().start());
    const double selection_end_x = origin_x + boundary_x((*editing_).selection().end());
    const double baseline = std::max(font.size,
        (bounds.height + font.size) * 0.5 - 1.0);
    painter.save();
    painter.clip_rect({text_left_, 2.0, viewport,
                       std::max(0.0, bounds.height - 4.0)});
    if (focused_ && !(*editing_).selection().empty()) {
        const Rect selection_bounds{
            selection_x, 3.0,
            std::max(0.0, selection_end_x - selection_x),
            std::max(0.0, bounds.height - 6.0)};
        paint_selection_band(painter, selection_bounds);
    }
    PartPaint text_part = text_part_template(font, false);
    text_part.text = presented;
    text_part.measured_text = presented_size;
    if (!(*editing_).store().utf8().empty() && (!focused_ || (*editing_).selection().empty())) {
        paint_text_part_at(painter, *this, text_part, {origin_x, baseline}, 0.0);
    } else if (!(*editing_).store().utf8().empty()) {
        // Each glyph is drawn once: in the selection's ink inside the band,
        // in the text's ink to either side.
        PartPaint selected_part = text_part_template(font, true);
        selected_part.text = presented;
        selected_part.measured_text = presented_size;
        const double band_width = std::max(0.0, selection_end_x - selection_x);
        const double band_right = selection_x + band_width;
        const double height = std::max(0.0, bounds.height - 4.0);
        const std::array<Rect, 3> spans{{
            {text_left_, 2.0, std::max(0.0, selection_x - text_left_), height},
            {selection_x, 2.0, band_width, height},
            {band_right, 2.0, std::max(0.0, text_left_ + viewport - band_right), height},
        }};
        for (std::size_t span = 0U; span < spans.size(); ++span) {
            if (spans[span].width <= 0.0) continue;
            painter.save();
            painter.clip_rect(spans[span]);
            paint_text_part_at(painter, *this, span == 1U ? selected_part : text_part,
                               {origin_x, baseline}, 0.0);
            painter.restore();
        }
    } else {
        const std::string empty_text = empty_field_text();
        if (!empty_text.empty()) {
            PartPaint cue = text_part_template(font, false);
            cue.tone = PartTextTone::secondary;
            cue.authored_foreground.reset();
            cue.text = empty_text;
            paint_text_part_at(painter, *this, cue, {text_left_, baseline}, viewport);
        }
    }
    if (focused_ && caret_visible_ && (*editing_).selection().empty()) {
        const double caret_x = origin_x + caret_content_x;
        paint_caret(painter, {caret_x - 0.5, 4.0, 1.0, std::max(0.0, bounds.height - 8.0)});
    }
    painter.restore();
    if (focused_) {
        PartPaint ring;
        ring.family = ControlVisualRole::editor;
        ring.role = PartRole::focus;
        ring.bounds = {1.0, 1.0, std::max(0.0, bounds.width - 2.0),
                       std::max(0.0, bounds.height - 2.0)};
        ring.states = text_states();
        ring.states.insert(AnatomyState::focus_visible);
        paint_part(painter, *this, ring);
    }
    if ((*editing_).selection().empty()) {
        const double caret_x = origin_x + caret_content_x;
        const Rect caret_bounds{caret_x, 4.0, 0.0,
                                std::max(0.0, bounds.height - 8.0)};
        const Rect text_clip{text_left_, 2.0, viewport,
                             std::max(0.0, bounds.height - 4.0)};
        retain_caret_damage(caret_bounds, text_clip);
    }
}

void TextBox::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) {
        return;
    }
    const Rect absolute = absolute_bounds();
    const double local_x = event.position.x - absolute.x;
    const double local_y = event.position.y - absolute.y;
    if (multiline_ && auto_scroll() && (!selecting_ || event.action == PointerAction::wheel)) {
        ensure_multiline_layout();
        Panel::on_pointer(event);
        if (event.handled || event.action == PointerAction::wheel) return;
        const Point local{local_x, local_y};
        if (!viewport_rectangle().contains(local)) return;
    }
    if (multiline_ && event.action == PointerAction::wheel) {
        ensure_multiline_layout();
        vertical_offset_ = std::clamp(vertical_offset_ - event.wheel_delta.y * line_height_ * 3.0,
            0.0, std::max(0.0, static_cast<double>(visual_lines_.size()) * line_height_ -
                std::max(0.0, local_bounds().height - 8.0)));
        reveal_pending_ = false;
        invalidate(Dirty::paint);
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::down &&
        event.button == PointerButton::primary) {
        if (window() != nullptr) {
            static_cast<void>((*window()).request_focus(shared_from_this()));
        }
        // Placing the caret ends a completion and keeps the text.
        end_auto_complete(false);
        const Utf8Offset position = multiline_ ? multiline_position_at(local_x, local_y) : position_at(local_x);
        const bool extend = includes(event.modifiers, Modifier::shift);
        set_selection(extend ? TextSelection{(*editing_).selection().anchor, position}
                             : TextSelection{position, position});
        selecting_ = true;
        set_pointer_capture(true);
        event.handled = true;
    } else if (event.action == PointerAction::move && selecting_ &&
               has_pointer_capture()) {
        const Utf8Offset position = multiline_ ? multiline_position_at(local_x, local_y) : position_at(local_x);
        const TextSelection selected{.anchor = (*editing_).selection().anchor, .caret = position};
        set_selection(selected);
        event.handled = true;
    } else if (event.action == PointerAction::up &&
               event.button == PointerButton::primary && selecting_) {
        const Utf8Offset position = multiline_ ? multiline_position_at(local_x, local_y) : position_at(local_x);
        const TextSelection selected{.anchor = (*editing_).selection().anchor, .caret = position};
        set_selection(selected);
        selecting_ = false;
        set_pointer_capture(false);
        event.handled = true;
    }
}

void TextBox::on_key(KeyEvent& event) {
    if (!focused_ || !enabled() || event.action != KeyAction::down) {
        return;
    }
    if (auto_complete_key(event)) {
        event.handled = true;
        return;
    }
    const bool extend = includes(event.modifiers, Modifier::shift);
    const bool command = command_modifier(event.modifiers);
    if (multiline_ && accepts_tab_ && event.physical_key == PhysicalKey::tab && !command) {
        if (!(*editing_).read_only()) static_cast<void>(replace_selection("\t"));
        event.handled = true;
        return;
    }
    if (multiline_ && (event.physical_key == PhysicalKey::up ||
        event.physical_key == PhysicalKey::down ||
        event.physical_key == PhysicalKey::page_up ||
        event.physical_key == PhysicalKey::page_down ||
        event.physical_key == PhysicalKey::home ||
        event.physical_key == PhysicalKey::end)) {
        ensure_multiline_layout();
        const std::size_t row = caret_line();
        const bool home_end = event.physical_key == PhysicalKey::home ||
                              event.physical_key == PhysicalKey::end;
        Utf8Offset next{};
        double goal = preferred_x_;
        if (home_end) {
            const bool first = event.physical_key == PhysicalKey::home;
            next = command ? (first ? Utf8Offset(0) : (*editing_).store().utf8_size())
                : Utf8Offset(first ? visual_lines_[row].offsets.front()
                                   : visual_lines_[row].offsets.back());
            next_upstream_ = !command && !first;
        } else {
            if (goal < 0.0) goal = multiline_boundary_x(row, (*editing_).selection().caret);
            const bool up = event.physical_key == PhysicalKey::up ||
                            event.physical_key == PhysicalKey::page_up;
            const bool page = event.physical_key == PhysicalKey::page_up ||
                              event.physical_key == PhysicalKey::page_down;
            const std::size_t step = page ? static_cast<std::size_t>(std::max(1.0,
                std::floor(multiline_text_rectangle().height / line_height_))) : 1U;
            const std::size_t target = up ? row - std::min(row, step)
                : std::min(visual_lines_.size() - 1, row + step);
            next = position_in_line(target, goal);
            next_upstream_ = next.value() == visual_lines_[target].offsets.back();
        }
        set_selection(extend ? TextSelection{(*editing_).selection().anchor, next} : TextSelection{next, next});
        if (!home_end) preferred_x_ = goal;
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::enter) {
        // An open suggestion list or appended completion took Enter above.
        if ((*editing_).composing()) {
            // The composition is the input method's to commit; the box keeps
            // the key from reaching the containers or the accept button.
            event.handled = true;
            return;
        }
        if (multiline_) {
            if (!(*editing_).read_only()) static_cast<void>(replace_selection(newline_));
            event.handled = true;
            return;
        }
        if (accepts_enter_ && event.modifiers == Modifier::none) {
            const std::string value(text());
            committed_.emit(value);
            event.handled = true;
        }
        // Otherwise nothing here to commit: the key goes on unhandled.
        return;
    }
    if (event.physical_key == PhysicalKey::escape) {
        // An open suggestion list or appended completion took Escape above.
        if ((*editing_).composing()) {
            // The composition is the input method's to cancel; the box keeps
            // the key from reaching the containers or the cancel button.
            event.handled = true;
            return;
        }
        if (selecting_ && has_pointer_capture()) {
            // Escape ends a selection drag, keeping what it selected so far.
            selecting_ = false;
            set_pointer_capture(false);
            event.handled = true;
            return;
        }
        if (accepts_escape_ && event.modifiers == Modifier::none) {
            cancelled_.emit();
            event.handled = true;
        }
        // Otherwise nothing here to cancel: the key goes on unhandled.
        return;
    }
    // The editing keys: select all, the clipboard, undo and redo, Left and
    // Right, Home and End of the text, Backspace and Delete.
    (*editing_).handle_key(event);
}

void TextBox::on_text_input(TextInputEvent& event) {
    if (!focused_ || !enabled()) {
        return;
    }
    (*editing_).handle_text_input(event);
}

void TextBox::reset_caret_blink() {
    caret_damage_.valid = false;
    caret_upstream_ = false;
    reveal_pending_ = true;
    preferred_x_ = -1.0;
    caret_visible_ = true;
    caret_frame_.disconnect();
    // A box focused in an occluded window (hidden, minimized or covered)
    // starts its blink at the window's next paint, so the window holds no
    // wake meanwhile (the macOS idle-visibility probe; ADR-016).
    const bool occluded = window() != nullptr && (*window()).occluded();
    caret_waiting_for_paint_ = occluded;
    if (!occluded) {
        schedule_caret_blink();
    }
}

void TextBox::schedule_caret_blink() {
    if (focused_ && attached() && window() != nullptr) {
        // ADR-016: a UI timer the box stops on blur and detachment and
        // replaces when the caret moves, so the caret stays visible for one
        // whole blink interval after each movement.
        const FrameTime now = FrameClock::now();
        const std::shared_ptr<TextBox> self =
            std::static_pointer_cast<TextBox>(shared_from_this());
        caret_frame_ = (*window()).schedule_ui_timer(
            *this, caret_blink_interval, now + caret_blink_interval, CaretTick{self});
    }
}

void TextBox::CaretTick::operator()(FrameTime now) const {
    static_cast<void>(now);
    const std::shared_ptr<TextBox> live = box.lock();
    if (live) (*live).toggle_caret();
}

void TextBox::on_focus_changed(bool focused) {
    caret_damage_.valid = false;
    focused_ = focused;
    selecting_ = false;
    (*editing_).end_composition();
    if (focused_) {
        reset_caret_blink();
    } else {
        caret_frame_.disconnect();
        caret_visible_ = false;
        // Leaving the box ends a completion and keeps the text.
        end_auto_complete(false);
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void TextBox::toggle_caret() {
    if (!focused_) {
        return;
    }
    if (!caret_painted_) {
        caret_frame_.disconnect();
        caret_waiting_for_paint_ = true;
        return;
    }
    caret_painted_ = false;
    caret_visible_ = !caret_visible_;
    if (caret_damage_current()) {
        invalidate(caret_damage_.rectangle);
    } else {
        invalidate(Dirty::paint);
    }
}

void TextBox::retain_caret_damage(const Rect line_bounds, const Rect clip) {
    const Window* owner = window();
    if (!focused_ || owner == nullptr || !line_bounds.finite() || line_bounds.height <= 0.0) return;
    const double scale = (*owner).scale();
    if (!std::isfinite(scale) || scale <= 0.0) return;
    // Half of the one-DIP stroke plus one device pixel for antialiasing,
    // including both endpoint caps. The same text clip constrains actual ink.
    const double padding = 0.5 + 1.0 / scale;
    const Rect padded{line_bounds.x - padding, line_bounds.y - padding,
                      line_bounds.width + 2.0 * padding,
                      line_bounds.height + 2.0 * padding};
    const Rect clipped = Rect::intersection(padded, clip);
    const Rect damage = Rect::intersection(clipped, client_rectangle());
    if (damage.empty()) return;
    CaretDamage candidate{};
    candidate.rectangle = damage;
    candidate.bounds = local_bounds();
    candidate.font = effective_font(font_);
    candidate.selection = (*editing_).selection();
    candidate.scroll = scroll_offset();
    candidate.provider = (*owner).text_metrics_provider();
    candidate.revision = (*editing_).store().revision();
    candidate.scale = scale;
    candidate.valid = true;
    caret_damage_ = candidate;
}

bool TextBox::caret_damage_current() const {
    const Window* owner = window();
    if (!caret_damage_.valid || owner == nullptr || (multiline_ && reveal_pending_) ||
        has_dirty(dirty(), Dirty::measure | Dirty::arrange | Dirty::paint)) return false;
    const Point scroll = scroll_offset();
    const bool current = caret_damage_.bounds == local_bounds() &&
        caret_damage_.font == effective_font(font_) &&
        caret_damage_.selection == (*editing_).selection() && caret_damage_.scroll == scroll &&
        caret_damage_.provider == (*owner).text_metrics_provider() &&
        caret_damage_.revision == (*editing_).store().revision() && caret_damage_.scale == (*owner).scale();
    return current;
}

SemanticDescriptor TextBox::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::text_box;
    descriptor.name = accessible_name();
    descriptor.value = password_protected() ? std::string{} : std::string(text());
    descriptor.description = accessible_description().empty()
        ? placeholder_ : accessible_description();
    if ((*editing_).read_only()) descriptor.states |= SemanticState::read_only;
    if (password_protected()) descriptor.states |= SemanticState::protected_content;
    // The selection and caret index the value (A-T1, 12.13); a password box
    // publishes neither its text nor where in it the caret is.
    if (!password_protected()) {
        descriptor.text_selection = SemanticTextSelection{.anchor = (*editing_).selection().anchor, .caret = (*editing_).selection().caret};
    }
    descriptor.multiline = multiline_;
    descriptor.actions = {SemanticAction::focus};
    if (!(*editing_).read_only()) descriptor.actions.push_back(SemanticAction::set_value);
    // A suggesting box opens a list; it is expanded while the list is open.
    const bool suggesting = auto_complete_mode_ == AutoCompleteMode::suggest ||
                            auto_complete_mode_ == AutoCompleteMode::suggest_append;
    if (suggesting && auto_complete_active()) descriptor.has_popup = true;
    if (auto_complete_open()) descriptor.states |= SemanticState::expanded;
    descriptor.exposed = true;
    return descriptor;
}

bool TextBox::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::set_value && !(*editing_).read_only()) {
        end_auto_complete(false);
        const Utf8Offset start(0U);
        const Utf8Offset end = (*editing_).store().utf8_size();
        const bool replaced = (*editing_).replace(start, end, value, TextEditKind::value);
        return replaced;
    }
    const bool handled = Panel::on_semantic_action(action, value);
    return handled;
}

// -- editable text for hosts (A-T1) ------------------------------------------

SemanticTextResult TextBox::on_semantic_text_layout(SemanticTextLayout& layout) {
    if (password_protected()) return SemanticTextResult::no_text;
    const std::size_t graphemes = (*editing_).store().grapheme_count().value();
    layout.graphemes.reserve(graphemes);
    if (multiline_) {
        // Builds the visual rows as painting would, when they are stale.
        ensure_multiline_layout();
        fill_multiline_text_layout(layout);
    } else {
        fill_single_line_text_layout(layout);
    }
    (*editing_).fill_word_starts(layout);
    return SemanticTextResult::done;
}

SemanticTextResult TextBox::on_set_semantic_text_selection(SemanticTextSelection selection) {
    if (password_protected()) return SemanticTextResult::no_text;
    // A boundary lies within the text; one inside a combining sequence is
    // not a boundary.
    const TextStore& store = (*editing_).store();
    if (!store.is_grapheme_boundary(selection.anchor) || !store.is_grapheme_boundary(selection.caret)) {
        return SemanticTextResult::off_boundary;
    }
    set_selection({selection.anchor, selection.caret});
    return SemanticTextResult::done;
}

// The one line spans the text as last painted (estimated before the first
// paint, as boundary_x is), scrolled by the horizontal offset; its height is
// the selection band's.
void TextBox::fill_single_line_text_layout(SemanticTextLayout& layout) const {
    const Rect bounds = local_bounds();
    const double origin = text_left_ - horizontal_offset_;
    const double top = 3.0;
    const double height = std::max(0.0, bounds.height - 6.0);
    const Utf8Offset end = (*editing_).store().utf8_size();
    const double right = boundary_x(end);
    const Rect line_box{origin, top, right, height};
    layout.lines.push_back({.range = {Utf8Offset(0U), end}, .bounds = rectangle_to_window(line_box)});
    Utf8Offset cursor(0U);
    while (cursor != end) {
        const Utf8Offset next = (*editing_).store().next_grapheme_boundary(cursor);
        const double left = boundary_x(cursor);
        const double following = boundary_x(next);
        const Rect box{origin + left, top, std::max(0.0, following - left), height};
        layout.graphemes.push_back({.range = {cursor, next}, .line = LineIndex(0U), .bounds = rectangle_to_window(box)});
        cursor = next;
    }
}

// Each visual row's offsets are its grapheme boundaries, from the row's
// start to its end (a wrapped row ends after its last space; a logical
// line's break belongs to no row), with the prefix advance of each.
void TextBox::fill_multiline_text_layout(SemanticTextLayout& layout) const {
    const Point offset = scroll_offset();
    const double origin = text_left_ - offset.x;
    const std::size_t rows = visual_lines_.size();
    layout.lines.reserve(rows);
    for (std::size_t row = 0U; row < rows; ++row) {
        const VisualLine& line = visual_lines_[row];
        const double top = 4.0 + static_cast<double>(row) * line_height_ - offset.y;
        const Utf8Offset start(line.offsets.front());
        const Utf8Offset finish(line.offsets.back());
        const Rect line_box{origin, top, line.positions.back(), line_height_};
        layout.lines.push_back({.range = {start, finish}, .bounds = rectangle_to_window(line_box)});
        const std::size_t boundaries = line.offsets.size();
        for (std::size_t index = 1U; index < boundaries; ++index) {
            const double left = line.positions[index - 1U];
            const double right = line.positions[index];
            const Rect box{origin + left, top, std::max(0.0, right - left), line_height_};
            const TextRange<Utf8Offset> range{Utf8Offset(line.offsets[index - 1U]), Utf8Offset(line.offsets[index])};
            layout.graphemes.push_back({.range = range, .line = LineIndex(row), .bounds = rectangle_to_window(box)});
        }
    }
}

void TextBox::on_detached_from_window() noexcept {
    discard_auto_complete_list();
    caret_damage_.valid = false;
    caret_frame_.disconnect();
    focused_ = false;
    selecting_ = false;
    (*editing_).end_composition();
}


namespace {

constexpr std::array<PartField<TextBox::Parts>, 6> text_box_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &TextBox::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &TextBox::Parts::edge},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &TextBox::Parts::well},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = {}},
     .member = &TextBox::Parts::content},
    {.declaration = {.name = "caret", .role = PartRole::caret, .composed_from = {}},
     .member = &TextBox::Parts::caret},
    {.declaration = {.name = "selection", .role = PartRole::selection, .composed_from = {}},
     .member = &TextBox::Parts::selection},
}};

constexpr std::array<StateField<TextBox::States>, 10> text_box_state_fields{{
    {.state = AnatomyState::normal, .member = &TextBox::States::normal},
    {.state = AnatomyState::pressed, .member = &TextBox::States::pressed},
    {.state = AnatomyState::captured, .member = &TextBox::States::captured},
    {.state = AnatomyState::enabled, .member = &TextBox::States::enabled},
    {.state = AnatomyState::disabled, .member = &TextBox::States::disabled},
    {.state = AnatomyState::focused, .member = &TextBox::States::focused},
    {.state = AnatomyState::focus_visible, .member = &TextBox::States::focus_visible},
    {.state = AnatomyState::read_only, .member = &TextBox::States::read_only},
    {.state = AnatomyState::invalid, .member = &TextBox::States::invalid},
    {.state = AnatomyState::pending, .member = &TextBox::States::pending},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 6>
    text_box_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(text_box_extra_part_fields));
constexpr std::array<AnatomyState, 10> text_box_states =
    state_declarations(text_box_state_fields);

constexpr ControlAnatomy text_box_anatomy{
    .control_id = "text_box",
    .parts = text_box_parts,
    .states = text_box_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& TextBox::anatomy() const noexcept {
    return text_box_anatomy;
}

TextBox::Parts TextBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), text_box_extra_part_fields, published.parts(), record);
    return record;
}

TextBox::States TextBox::states() const {
    States record;
    read_states(text_box_state_fields, anatomy_states(), record);
    return record;
}

void TextBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    // Text is laid out while painting, so the caret and selection follow the
    // layout of the last paint, which is what the window shows; before the
    // first paint they use the estimated layout.
    const Rect bounds = local_bounds();
    const bool themed = !has_background_override() && !has_style_override();
    const Panel::Parts panel = panel_parts();
    Parts record;
    record.scrollable = panel.scrollable;
    if (!themed) {
        record.face = panel.face;
        record.edge = panel.edge;
    }
    record.well = PartGeometry::from_bounds(bounds);
    if (multiline_) {
        const Rect viewport = multiline_text_rectangle();
        record.content = PartGeometry::from_bounds(viewport);
        if (!visual_lines_.empty()) {
            const Point offset = scroll_offset();
            if ((*editing_).selection().empty()) {
                const std::size_t row = caret_line();
                const double x = text_left_ - offset.x + multiline_boundary_x(row, (*editing_).selection().caret);
                const double y = 4.0 + static_cast<double>(row) * line_height_ - offset.y;
                // The one-unit caret stroke is centred on its boundary.
                const Rect caret{x - 0.5, y, 1.0, line_height_};
                record.caret = PartGeometry::from_bounds(Rect::intersection(caret, viewport));
            } else {
                // The union of the selected rows' highlights, as paint draws them.
                const FontSpec font = effective_font(font_);
                Rect united{};
                bool any = false;
                for (std::size_t index = 0; index < visual_lines_.size(); ++index) {
                    const VisualLine& row = visual_lines_[index];
                    const bool selected = (*editing_).selection().start().value() <= row.offsets.back() &&
                                          (*editing_).selection().end().value() > row.offsets.front();
                    if (!selected) continue;
                    const double y = 4.0 + static_cast<double>(index) * line_height_ - offset.y;
                    const double origin = text_left_ - offset.x;
                    const double left = multiline_boundary_x(index, (*editing_).selection().start());
                    double right = multiline_boundary_x(index, (*editing_).selection().end());
                    if ((*editing_).selection().end().value() > row.offsets.back()) {
                        right += std::max(3.0, font.size * 0.35);
                    }
                    const Rect highlight{origin + left, y, std::max(0.0, right - left), line_height_};
                    united = any ? Rect::united(united, highlight) : highlight;
                    any = true;
                }
                if (any) {
                    record.selection = PartGeometry::from_bounds(Rect::intersection(united, viewport));
                }
            }
        }
    } else {
        const double right = std::max(text_left_, bounds.width - 4.0);
        const double viewport_width = std::max(0.0, right - text_left_);
        const Rect content{text_left_, 2.0, viewport_width, std::max(0.0, bounds.height - 4.0)};
        record.content = PartGeometry::from_bounds(content);
        const double origin_x = text_left_ - horizontal_offset_;
        if ((*editing_).selection().empty()) {
            const double caret_x = origin_x + boundary_x((*editing_).selection().caret);
            const Rect caret{caret_x - 0.5, 4.0, 1.0, std::max(0.0, bounds.height - 8.0)};
            record.caret = PartGeometry::from_bounds(Rect::intersection(caret, content));
        } else {
            const double start_x = origin_x + boundary_x((*editing_).selection().start());
            const double end_x = origin_x + boundary_x((*editing_).selection().end());
            const Rect highlight{start_x, 3.0, std::max(0.0, end_x - start_x),
                                 std::max(0.0, bounds.height - 6.0)};
            record.selection = PartGeometry::from_bounds(Rect::intersection(highlight, content));
        }
    }
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), text_box_extra_part_fields, record, geometry);
}

AnatomyStateSet TextBox::anatomy_states() const {
    AnatomyStateSet states = ScrollableControl::anatomy_states();
    if ((*editing_).read_only()) {
        states.insert(AnatomyState::read_only);
    }
    if (visual_status() == ControlVisualStatus::invalid) {
        states.insert(AnatomyState::invalid);
    } else if (visual_status() == ControlVisualStatus::pending) {
        states.insert(AnatomyState::pending);
    }
    return states;
}

namespace {

// The events TextBox adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 6> text_box_interactions{{
    {.name = "text_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "selection_changed", .payload = "TextSelection", .mutable_payload = false},
    {.name = "committed", .payload = "std::string", .mutable_payload = false},
    {.name = "cancelled", .payload = "", .mutable_payload = false},
    {.name = "auto_complete_open_changed", .payload = "bool", .mutable_payload = false},
    {.name = "auto_complete_accepted", .payload = "std::string", .mutable_payload = false},
}};

} // namespace

void TextBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), text_box_interactions.begin(), text_box_interactions.end());
}

} // namespace gui_forms
