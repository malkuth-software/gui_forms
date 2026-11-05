#include "gui_forms/controls/panel/rich_text_box/rich_text_box.hpp"

#include "../input_control_utilities.hpp"
#include "../panel_anatomy.hpp"
#include "../text_editing/text_editing_engine.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace input_control_detail;

namespace {

// The caret shows and hides for this long each.
constexpr FrameInterval caret_blink_interval = std::chrono::milliseconds(530);

// Throws for a source the box does not take.
void require_source(std::string_view source) {
    if (!validate_utf8(source).valid()) throw std::invalid_argument("RichTextBox markdown is not valid UTF-8");
    if (source.size() > RichTextBox::maximum_bytes) {
        throw std::length_error("RichTextBox markdown exceeds maximum_bytes");
    }
}

[[nodiscard]] bool ascii_punctuation(char character) noexcept {
    const bool punctuation = (character >= '!' && character <= '/') || (character >= ':' && character <= '@') ||
                             (character >= '[' && character <= '`') || (character >= '{' && character <= '~');
    return punctuation;
}

// Plain text as markdown that reads back as it: every ASCII punctuation
// character escaped, a line's leading blanks as character references, each
// line feed a hard line break. Trailing line feeds are dropped, as a hard
// break cannot end a paragraph.
[[nodiscard]] std::string escaped_markdown(std::string_view text) {
    std::string_view rest = text;
    while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r')) rest.remove_suffix(1U);
    std::string result{};
    result.reserve(rest.size() + rest.size() / 8U);
    bool line_start = true;
    for (std::size_t index = 0U; index < rest.size(); ++index) {
        const char character = rest[index];
        if (character == '\r') continue;
        if (character == '\n') {
            result.append("\\\n");
            line_start = true;
            continue;
        }
        if (line_start && character == ' ') {
            result.append("&#32;");
            continue;
        }
        if (line_start && character == '\t') {
            result.append("&#9;");
            continue;
        }
        line_start = false;
        if (ascii_punctuation(character)) result.push_back('\\');
        result.push_back(character);
    }
    return result;
}

} // namespace

// -- the editing engine's host -----------------------------------------------

// How the box's editing engine reaches the box: each call forwards to the
// box, which keeps its parse, layout, invalidation and event order.
class RichTextBox::EditingHost final : public detail::TextEditingHost {
public:
    explicit EditingHost(RichTextBox& box) noexcept : box_(box) {}

    void editing_require_mutable() const override {
        box_.require_mutable();
    }
    void editing_restoring() override {}
    void editing_text_changed() override {
        box_.source_changed();
    }
    void editing_select(TextSelection selection) override {
        box_.set_selection(selection);
    }
    void editing_navigating() override {}
    void editing_user_edit(TextEditKind, bool) override {}
    [[nodiscard]] HostServices* editing_host_services() const override {
        Window* owner = box_.window();
        if (owner == nullptr) return nullptr;
        HostServices* services = (*owner).host_services();
        return services;
    }
    [[nodiscard]] bool editing_password_protected() const override {
        return false;
    }

private:
    RichTextBox& box_;
};

// -- RichTextBox --------------------------------------------------------------

RichTextBox::RichTextBox(StableId stable_id, std::string_view markdown)
    : Panel(std::move(stable_id)), editing_host_(std::make_unique<EditingHost>(*this)) {
    require_source(markdown);
    editing_ = std::make_unique<detail::TextEditingEngine>(*editing_host_, markdown);
    (*editing_).set_maximum_bytes(maximum_bytes);
    set_paint_plane(PaintPlane::control);
    set_border_style(BorderStyle::sunken);
    set_focusable(true);
    cursor = CursorKind::text;
    set_tool_tip_presentation(ToolTipPresentation::field_hint);
    Panel::set_auto_scroll(true);
    reparse();
    heading_menu_ = std::make_unique<ContextMenu>(std::string((*this).stable_id().value()) + ".heading-menu");
    heading_invoked_ = (*heading_menu_).item_invoked().subscribe(
        *this, Delegate<const MenuItemInvocation&>::bind<RichTextBox, &RichTextBox::heading_chosen>(*this));
    heading_open_changed_ = (*heading_menu_).open_changed().subscribe(
        *this, Delegate<bool>::bind<RichTextBox, &RichTextBox::heading_menu_open_changed>(*this));
    define_bindable_property({
        {"text", BindingValueKind::text, "Appearance", "The plain text of the document.",
         BindingValue{std::string{}}, invalidation::text_content},
        detail::BoundMemberFunction<BindingValue (RichTextBox::*)() const>(*this, &RichTextBox::text_property_value),
        detail::ConvertedPropertySetter<RichTextBox, std::string>(
            *this, &RichTextBox::set_text_property, BindingValueKind::text,
            "RichTextBox.Text binding requires text"),
        detail::EventChangeConnector<const std::string&>(text_changed_), {}, {}});
}

RichTextBox::~RichTextBox() {
    if (heading_menu_) (*heading_menu_).close();
}

void RichTextBox::on_dispose() noexcept {
    if (heading_menu_) (*heading_menu_).dispose();
    Panel::on_dispose();
}

BindingValue RichTextBox::text_property_value() const {
    const BindingValue result{plain_text_};
    return result;
}

void RichTextBox::set_text_property(std::string text) {
    set_text(text);
}

// -- the document ---------------------------------------------------------------

std::string_view RichTextBox::markdown() const noexcept {
    const std::string_view source = (*editing_).text();
    return source;
}

void RichTextBox::reparse() {
    const MarkdownParseResult parsed = parse_markdown((*editing_).text(), document_);
    // A source within the box's bounds always parses; a failure leaves the
    // document empty, and the source is shown as markup.
    if (!parsed.succeeded()) document_.clear();
    markdown_plain_text(document_, plain_text_);
}

void RichTextBox::source_changed() {
    const std::string previous = std::move(plain_text_);
    ++content_revision_;
    reparse();
    reset_caret_blink();
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    publish_change(content_changed_);
    if (previous != plain_text_) publish_change(text_changed_, plain_text_);
    publish_change(selection_changed_, (*editing_).selection());
}

void RichTextBox::adopt(std::string_view source) {
    (*editing_).set_text(source);
    horizontal_offset_ = 0.0;
    vertical_offset_ = 0.0;
    source_changed();
}

void RichTextBox::set_markdown(std::string_view markdown) {
    require_mutable();
    require_source(markdown);
    adopt(markdown);
}

void RichTextBox::set_text(std::string_view text) {
    require_mutable();
    if (!validate_utf8(text).valid()) throw std::invalid_argument("RichTextBox text is not valid UTF-8");
    const std::string source = escaped_markdown(text);
    require_source(source);
    adopt(source);
}

RtfConversionResult RichTextBox::set_rtf(std::span<const std::byte> rtf) {
    require_mutable();
    std::string source{};
    const RtfConversionResult result = import_rtf(rtf, source);
    if (!result.succeeded()) return result;
    if (source.size() > maximum_bytes) {
        const RtfConversionResult refused = RtfConversionResult::failed(RtfConversionError::limit_exceeded, 0U);
        return refused;
    }
    adopt(source);
    return result;
}

RtfConversionResult RichTextBox::rtf(std::vector<std::byte>& output) const {
    const RtfConversionResult result = export_rtf((*editing_).text(), output);
    return result;
}

void RichTextBox::clear() {
    require_mutable();
    adopt({});
}

// -- settings -------------------------------------------------------------------

bool RichTextBox::read_only() const noexcept {
    const bool result = (*editing_).read_only();
    return result;
}

void RichTextBox::set_read_only(bool read_only) {
    require_mutable();
    if ((*editing_).read_only() == read_only) return;
    (*editing_).set_read_only(read_only);
    invalidate(Dirty::paint | Dirty::semantics);
}

void RichTextBox::set_word_wrap(bool enabled) {
    require_mutable();
    if (word_wrap_ == enabled) return;
    word_wrap_ = enabled;
    layout_width_ = -1.0;
    horizontal_offset_ = 0.0;
    reset_caret_blink();
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void RichTextBox::set_accepts_tab(bool enabled) {
    require_mutable();
    accepts_tab_ = enabled;
}

void RichTextBox::set_control_bar_visible(bool visible) {
    require_mutable();
    if (control_bar_visible_ == visible) return;
    control_bar_visible_ = visible;
    hot_button_.reset();
    pressed_button_.reset();
    if (heading_menu_) (*heading_menu_).close();
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void RichTextBox::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) throw std::invalid_argument("RichTextBox font specification is invalid");
    set_font_from_code(true);
    if (font_ == font) return;
    font_ = font;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void RichTextBox::set_auto_scroll(bool enabled) {
    require_mutable();
    if (auto_scroll() == enabled) return;
    const Point previous = scroll_offset();
    Panel::set_auto_scroll(enabled);
    layout_client_ = {-1.0, -1.0};
    if (enabled) return;
    adjust_scrollbars(false);
    horizontal_offset_ = previous.x;
    vertical_offset_ = previous.y;
}

void RichTextBox::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    ensure_layout();
    Control::arrange(final_bounds);
}

void RichTextBox::on_scroll_position_changed() {
    if (!auto_scroll()) return;
    // Explicit scrolling must not snap back to an offscreen caret on paint.
    reveal_pending_ = false;
}

// -- selection ------------------------------------------------------------------

TextSelection RichTextBox::selection() const noexcept {
    const TextSelection result = (*editing_).selection();
    return result;
}

void RichTextBox::select(Utf8Offset anchor, Utf8Offset caret) {
    require_mutable();
    const TextStore& store = (*editing_).store();
    if (!store.is_grapheme_boundary(anchor) || !store.is_grapheme_boundary(caret)) {
        throw std::out_of_range("RichTextBox selection must use grapheme boundaries");
    }
    set_selection({anchor, caret});
}

void RichTextBox::select_all() {
    require_mutable();
    set_selection({Utf8Offset(0U), (*editing_).store().utf8_size()});
}

std::string RichTextBox::selected_text() const {
    const std::string result = (*editing_).selected_text();
    return result;
}

void RichTextBox::set_selection(TextSelection selection, bool reveal) {
    const bool upstream = std::exchange(next_upstream_, false);
    if ((*editing_).selection() == selection) {
        if (reveal) reset_caret_blink();
        caret_upstream_ = upstream;
        invalidate(Dirty::paint);
        return;
    }
    (*editing_).set_selection_value(selection);
    if (reveal) reset_caret_blink();
    caret_upstream_ = upstream;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(selection_changed_, (*editing_).selection());
}

// -- editing --------------------------------------------------------------------

bool RichTextBox::can_undo() const noexcept {
    const bool result = (*editing_).can_undo();
    return result;
}

bool RichTextBox::can_redo() const noexcept {
    const bool result = (*editing_).can_redo();
    return result;
}

bool RichTextBox::undo() {
    const bool result = (*editing_).undo();
    return result;
}

bool RichTextBox::redo() {
    const bool result = (*editing_).redo();
    return result;
}

void RichTextBox::clear_undo_history() {
    require_mutable();
    (*editing_).clear_history();
}

bool RichTextBox::replace_selection(std::string_view replacement) {
    const bool result = (*editing_).replace_selection(replacement);
    return result;
}

bool RichTextBox::delete_selection() {
    const bool result = (*editing_).delete_selection();
    return result;
}

bool RichTextBox::copy() {
    const bool result = (*editing_).copy();
    return result;
}

bool RichTextBox::cut() {
    const bool result = (*editing_).cut();
    return result;
}

bool RichTextBox::paste() {
    const bool result = (*editing_).paste();
    return result;
}

// -- keyboard -------------------------------------------------------------------

bool RichTextBox::vertical_key(const KeyEvent& event, bool extend, bool command) {
    const std::uint32_t key = event.physical_key;
    const bool home_end = key == PhysicalKey::home || key == PhysicalKey::end;
    const bool up = key == PhysicalKey::up || key == PhysicalKey::page_up;
    const bool page = key == PhysicalKey::page_up || key == PhysicalKey::page_down;
    if (!home_end && !up && !page && key != PhysicalKey::down) return false;
    ensure_layout();
    const TextSelection selection = (*editing_).selection();
    const std::size_t row = caret_row();
    Utf8Offset next{};
    double goal = preferred_x_;
    if (home_end) {
        const bool first = key == PhysicalKey::home;
        if (command) {
            next = first ? Utf8Offset(0U) : (*editing_).store().utf8_size();
        } else {
            next = Utf8Offset(first ? rows_[row].offsets.front() : rows_[row].offsets.back());
        }
        next_upstream_ = !command && !first;
    } else {
        if (goal < 0.0) goal = boundary_x(rows_[row], selection.caret);
        std::size_t target = row;
        if (page) {
            const double step = std::max(1.0, text_rectangle().height);
            const double y = up ? rows_[row].y - step : rows_[row].y + step;
            target = row_at_y(std::max(0.0, y));
        } else if (up) {
            target = row == 0U ? 0U : row - 1U;
        } else {
            target = std::min(rows_.size() - 1U, row + 1U);
        }
        next = position_in_row(target, goal);
        next_upstream_ = next.value() == rows_[target].offsets.back();
    }
    set_selection(extend ? TextSelection{selection.anchor, next} : TextSelection{next, next});
    if (!home_end) preferred_x_ = goal;
    return true;
}

void RichTextBox::on_key(KeyEvent& event) {
    if (!focused_ || !enabled.read() || event.action != KeyAction::down) return;
    const bool extend = includes(event.modifiers, Modifier::shift);
    const bool command = command_modifier(event.modifiers);
    if (accepts_tab_ && event.physical_key == PhysicalKey::tab && !command) {
        if (!read_only()) static_cast<void>(replace_selection("\t"));
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::enter) {
        // A composition is the input method's to commit. Enter continues a
        // list or a quote; Shift+Enter is a plain line feed.
        if (!(*editing_).composing() && !read_only()) {
            const bool continued = !extend && continue_list();
            if (!continued) static_cast<void>(replace_selection("\n"));
        }
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::escape) {
        if ((*editing_).composing()) {
            event.handled = true;
        } else if (selecting_ && has_pointer_capture()) {
            // Escape ends a selection drag, keeping what it selected so far.
            selecting_ = false;
            set_pointer_capture(false);
            event.handled = true;
        }
        // Otherwise nothing here to cancel: the key goes on unhandled.
        return;
    }
    if (command && !extend && (event.physical_key == PhysicalKey::b || event.physical_key == PhysicalKey::i ||
                               event.physical_key == PhysicalKey::k)) {
        // The control bar's keys: bold, italic and link.
        if (event.physical_key == PhysicalKey::b) {
            static_cast<void>(toggle_bold());
        } else if (event.physical_key == PhysicalKey::i) {
            static_cast<void>(toggle_italic());
        } else {
            static_cast<void>(insert_link());
        }
        event.handled = true;
        return;
    }
    if (vertical_key(event, extend, command)) {
        event.handled = true;
        return;
    }
    // The editing keys: select all, the clipboard, undo and redo, Left and
    // Right, Backspace and Delete.
    (*editing_).handle_key(event);
}

void RichTextBox::on_text_input(TextInputEvent& event) {
    if (!focused_ || !enabled.read()) return;
    (*editing_).handle_text_input(event);
}

// -- pointer --------------------------------------------------------------------

void RichTextBox::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const Rect absolute = absolute_bounds();
    const double local_x = event.position.x - absolute.x;
    const double local_y = event.position.y - absolute.y;
    const Point local{local_x, local_y};
    if (!selecting_ && control_bar_visible_ && event.action != PointerAction::wheel && bar_pointer(event, local)) {
        return;
    }
    if (auto_scroll() && (!selecting_ || event.action == PointerAction::wheel)) {
        ensure_layout();
        Panel::on_pointer(event);
        if (event.handled || event.action == PointerAction::wheel) return;
        if (!viewport_rectangle().contains(local)) return;
    }
    if (event.action == PointerAction::wheel) {
        ensure_layout();
        const double step = rows_.empty() ? 16.0 : rows_.front().height;
        vertical_offset_ = std::clamp(vertical_offset_ - event.wheel_delta.y * step * 3.0, 0.0,
                                      std::max(0.0, document_height() - text_rectangle().height));
        reveal_pending_ = false;
        invalidate(Dirty::paint);
        event.handled = true;
        return;
    }
    const bool command = command_modifier(event.modifiers);
    if (event.action == PointerAction::down && event.button == PointerButton::primary) {
        if (window() != nullptr) static_cast<void>((*window()).request_focus(shared_from_this()));
        if (command) {
            // Ctrl or Cmd with a click opens a link; the caret stays.
            const std::optional<std::string> target = link_at(local_x, local_y);
            if (target) {
                event.handled = true;
                publish_change(link_clicked_, *target);
                return;
            }
        }
        const Utf8Offset position = position_at(local_x, local_y);
        const bool extend = includes(event.modifiers, Modifier::shift);
        const TextSelection selection = (*editing_).selection();
        set_selection(extend ? TextSelection{selection.anchor, position} : TextSelection{position, position});
        selecting_ = true;
        set_pointer_capture(true);
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::move && selecting_ && has_pointer_capture()) {
        const Utf8Offset position = position_at(local_x, local_y);
        set_selection({(*editing_).selection().anchor, position});
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::move) {
        // The hand over a link while Ctrl or Cmd is held; the text cursor
        // elsewhere.
        const bool over_link = command && link_at(local_x, local_y).has_value();
        const CursorKind wanted = over_link ? CursorKind::hand : CursorKind::text;
        if (cursor.read() != wanted) cursor = wanted;
        return;
    }
    if (event.action == PointerAction::up && event.button == PointerButton::primary && selecting_) {
        const Utf8Offset position = position_at(local_x, local_y);
        set_selection({(*editing_).selection().anchor, position});
        selecting_ = false;
        set_pointer_capture(false);
        event.handled = true;
    }
}

// -- focus, caret and paint -----------------------------------------------------

void RichTextBox::reset_caret_blink() {
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

void RichTextBox::schedule_caret_blink() {
    if (focused_ && attached() && window() != nullptr) {
        // ADR-016: a UI timer the box stops on blur and detachment and
        // replaces when the caret moves, so the caret stays visible for one
        // whole blink interval after each movement.
        const FrameTime now = FrameClock::now();
        const std::shared_ptr<RichTextBox> self =
            std::static_pointer_cast<RichTextBox>(shared_from_this());
        caret_frame_ = (*window()).schedule_ui_timer(
            *this, caret_blink_interval, now + caret_blink_interval, CaretTick{self});
    }
}

void RichTextBox::CaretTick::operator()(FrameTime now) const {
    static_cast<void>(now);
    const std::shared_ptr<RichTextBox> live = box.lock();
    if (live) (*live).toggle_caret();
}

void RichTextBox::on_focus_changed(bool focused) {
    focused_ = focused;
    selecting_ = false;
    (*editing_).end_composition();
    if (focused_) {
        reset_caret_blink();
    } else {
        caret_frame_.disconnect();
        caret_visible_ = false;
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void RichTextBox::toggle_caret() {
    if (!focused_) return;
    if (!caret_painted_) {
        caret_frame_.disconnect();
        caret_waiting_for_paint_ = true;
        return;
    }
    caret_painted_ = false;
    caret_visible_ = !caret_visible_;
    invalidate(Dirty::paint);
}

void RichTextBox::on_detached_from_window() noexcept {
    caret_frame_.disconnect();
    focused_ = false;
    selecting_ = false;
    hot_button_.reset();
    pressed_button_.reset();
    (*editing_).end_composition();
    if (heading_menu_) (*heading_menu_).close();
}

void RichTextBox::on_paint(Painter& painter, Rect damage) {
    caret_painted_ = true;
    if (caret_waiting_for_paint_) {
        caret_waiting_for_paint_ = false;
        schedule_caret_blink();
    }
    const Rect bounds = local_bounds();
    const bool themed = !has_background_override() && !has_style_override();
    // The field is the editor's well and its focus ring a part, drawn by
    // the theme's engine; an overridden field keeps the panel's own paint.
    AnatomyStateSet states = anatomy_states();
    states.erase(AnatomyState::hot);
    if (themed) {
        PartPaint well;
        well.family = ControlVisualRole::editor;
        well.role = PartRole::well;
        well.bounds = bounds;
        well.states = states;
        paint_part(painter, *this, well);
    } else {
        Panel::on_paint(painter, damage);
    }
    paint_bar(painter);
    static_cast<void>(paint_rows(painter));
    if (!focused_) return;
    PartPaint ring;
    ring.family = ControlVisualRole::editor;
    ring.role = PartRole::focus;
    ring.bounds = {1.0, 1.0, std::max(0.0, bounds.width - 2.0), std::max(0.0, bounds.height - 2.0)};
    ring.states = states;
    ring.states.insert(AnatomyState::focus_visible);
    paint_part(painter, *this, ring);
}

// -- semantics ------------------------------------------------------------------

SemanticDescriptor RichTextBox::semantic_descriptor() const {
    const TextSelection selection = (*editing_).selection();
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::text_box;
    descriptor.name = accessible_name.read();
    // The value is the source, as shown: the text layout and the selection
    // are offsets into it.
    descriptor.value = std::string((*editing_).text());
    descriptor.description = accessible_description.read();
    if (read_only()) descriptor.states |= SemanticState::read_only;
    descriptor.text_selection = SemanticTextSelection{.anchor = selection.anchor, .caret = selection.caret};
    descriptor.multiline = true;
    descriptor.actions = {SemanticAction::focus};
    if (!read_only()) descriptor.actions.push_back(SemanticAction::set_value);
    descriptor.exposed = true;
    return descriptor;
}

bool RichTextBox::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::set_value && !read_only()) {
        const Utf8Offset end = (*editing_).store().utf8_size();
        const bool replaced = (*editing_).replace(Utf8Offset(0U), end, value, TextEditKind::value);
        return replaced;
    }
    const bool handled = Panel::on_semantic_action(action, value);
    return handled;
}

SemanticTextResult RichTextBox::on_semantic_text_layout(SemanticTextLayout& layout) {
    ensure_layout();
    const Point offset = scroll_offset();
    const Point origin{text_left_ - offset.x, text_rectangle().y - offset.y};
    layout.lines.reserve(rows_.size());
    for (std::size_t index = 0U; index < rows_.size(); ++index) {
        const Row& row = rows_[index];
        const double top = origin.y + row.y;
        const Rect line_box{origin.x + row.positions.front(), top,
                            std::max(0.0, row.positions.back() - row.positions.front()), row.height};
        const TextRange<Utf8Offset> range{Utf8Offset(row.offsets.front()), Utf8Offset(row.offsets.back())};
        layout.lines.push_back({.range = range, .bounds = rectangle_to_window(line_box)});
        for (std::size_t boundary = 1U; boundary < row.offsets.size(); ++boundary) {
            const double left = row.positions[boundary - 1U];
            const double right = row.positions[boundary];
            const Rect box{origin.x + left, top, std::max(0.0, right - left), row.height};
            const TextRange<Utf8Offset> grapheme{Utf8Offset(row.offsets[boundary - 1U]),
                                                 Utf8Offset(row.offsets[boundary])};
            layout.graphemes.push_back({.range = grapheme, .line = LineIndex(index), .bounds = rectangle_to_window(box)});
        }
    }
    (*editing_).fill_word_starts(layout);
    return SemanticTextResult::done;
}

SemanticTextResult RichTextBox::on_set_semantic_text_selection(SemanticTextSelection selection) {
    const TextStore& store = (*editing_).store();
    if (!store.is_grapheme_boundary(selection.anchor) || !store.is_grapheme_boundary(selection.caret)) {
        return SemanticTextResult::off_boundary;
    }
    set_selection({selection.anchor, selection.caret});
    return SemanticTextResult::done;
}

// -- anatomy --------------------------------------------------------------------

namespace {

constexpr std::array<PartField<RichTextBox::Parts>, 7> rich_text_box_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &RichTextBox::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &RichTextBox::Parts::edge},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &RichTextBox::Parts::well},
    {.declaration = {.name = "bar", .role = PartRole::region, .composed_from = {}},
     .member = &RichTextBox::Parts::bar},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = {}},
     .member = &RichTextBox::Parts::content},
    {.declaration = {.name = "caret", .role = PartRole::caret, .composed_from = {}},
     .member = &RichTextBox::Parts::caret},
    {.declaration = {.name = "selection", .role = PartRole::selection, .composed_from = {}},
     .member = &RichTextBox::Parts::selection},
}};

constexpr std::array<StateField<RichTextBox::States>, 10> rich_text_box_state_fields{{
    {.state = AnatomyState::normal, .member = &RichTextBox::States::normal},
    {.state = AnatomyState::pressed, .member = &RichTextBox::States::pressed},
    {.state = AnatomyState::captured, .member = &RichTextBox::States::captured},
    {.state = AnatomyState::enabled, .member = &RichTextBox::States::enabled},
    {.state = AnatomyState::disabled, .member = &RichTextBox::States::disabled},
    {.state = AnatomyState::focused, .member = &RichTextBox::States::focused},
    {.state = AnatomyState::focus_visible, .member = &RichTextBox::States::focus_visible},
    {.state = AnatomyState::read_only, .member = &RichTextBox::States::read_only},
    {.state = AnatomyState::invalid, .member = &RichTextBox::States::invalid},
    {.state = AnatomyState::pending, .member = &RichTextBox::States::pending},
}};

constexpr std::array<PartField<RichTextBox::ItemParts>, 3> rich_text_box_item_part_fields{{
    {.declaration = {.name = "button", .role = PartRole::face, .composed_from = {}},
     .member = &RichTextBox::ItemParts::button},
    {.declaration = {.name = "button_label", .role = PartRole::label, .composed_from = {}},
     .member = &RichTextBox::ItemParts::button_label},
    {.declaration = {.name = "button_glyph", .role = PartRole::glyph, .composed_from = {}},
     .member = &RichTextBox::ItemParts::button_glyph},
}};

constexpr std::array<StateField<RichTextBox::ItemStates>, 6> rich_text_box_item_state_fields{{
    {.state = AnatomyState::hot, .member = &RichTextBox::ItemStates::hot},
    {.state = AnatomyState::pressed, .member = &RichTextBox::ItemStates::pressed},
    {.state = AnatomyState::checked, .member = &RichTextBox::ItemStates::checked},
    {.state = AnatomyState::indeterminate, .member = &RichTextBox::ItemStates::indeterminate},
    {.state = AnatomyState::disabled, .member = &RichTextBox::ItemStates::disabled},
    {.state = AnatomyState::open, .member = &RichTextBox::ItemStates::open},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 7> rich_text_box_parts =
    joined_declarations(scrollable_detail::parts, part_declarations(rich_text_box_extra_part_fields));
constexpr std::array<AnatomyState, 10> rich_text_box_states = state_declarations(rich_text_box_state_fields);
constexpr std::array<PartDeclaration, 3> rich_text_box_item_parts =
    part_declarations(rich_text_box_item_part_fields);
constexpr std::array<AnatomyState, 6> rich_text_box_item_states =
    state_declarations(rich_text_box_item_state_fields);

constexpr std::array<ItemSystemDeclaration, 1> rich_text_box_item_systems{{
    {.name = "bar_button", .parts = rich_text_box_item_parts, .states = rich_text_box_item_states,
     .custom_states = {}},
}};

constexpr ControlAnatomy rich_text_box_anatomy{
    .control_id = "rich_text_box",
    .parts = rich_text_box_parts,
    .states = rich_text_box_states,
    .custom_states = {},
    .item_systems = rich_text_box_item_systems,
};

// The events RichTextBox adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 4> rich_text_box_interactions{{
    {.name = "text_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "content_changed", .payload = "", .mutable_payload = false},
    {.name = "selection_changed", .payload = "TextSelection", .mutable_payload = false},
    {.name = "link_clicked", .payload = "std::string", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& RichTextBox::anatomy() const noexcept {
    return rich_text_box_anatomy;
}

RichTextBox::Parts RichTextBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(), record.scrollable);
    read_parts(anatomy(), rich_text_box_extra_part_fields, published.parts(), record);
    return record;
}

RichTextBox::States RichTextBox::states() const {
    States record;
    read_states(rich_text_box_state_fields, anatomy_states(), record);
    return record;
}

void RichTextBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    // The caret and selection follow the layout of the last paint, which is
    // what the window shows.
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
    if (control_bar_visible_) {
        record.bar = PartGeometry::from_bounds({1.0, 1.0, std::max(0.0, bounds.width - 2.0), bar_height() - 1.0});
    }
    const Rect viewport = text_rectangle();
    record.content = PartGeometry::from_bounds(viewport);
    if (!rows_.empty()) {
        const TextSelection selection = (*editing_).selection();
        const Point offset = scroll_offset();
        const Point origin{text_left_ - offset.x, viewport.y - offset.y};
        if (selection.empty()) {
            const Row& row = rows_[caret_row()];
            const double x = origin.x + boundary_x(row, selection.caret);
            // The one-unit caret stroke is centred on its boundary.
            const Rect caret{x - 0.5, origin.y + row.y, 1.0, row.height};
            record.caret = PartGeometry::from_bounds(Rect::intersection(caret, viewport));
        } else {
            Rect united{};
            bool any = false;
            for (const Row& row : rows_) {
                const std::optional<Rect> band = selection_band(row);
                if (!band) continue;
                const Rect placed{origin.x + (*band).x, origin.y + (*band).y, (*band).width, (*band).height};
                united = any ? Rect::united(united, placed) : placed;
                any = true;
            }
            if (any) record.selection = PartGeometry::from_bounds(Rect::intersection(united, viewport));
        }
    }
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), rich_text_box_extra_part_fields, record, geometry);
}

void RichTextBox::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const ItemParts record = bar_item_parts(item);
    write_item_parts(anatomy().item_systems.front(), rich_text_box_item_part_fields, record, geometry);
}

std::vector<RichTextBox::Item> RichTextBox::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records{};
    records.reserve(count);
    for (std::size_t item = 0U; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record{};
        record.index = item;
        record.stable_id = realized_item_stable_id(item);
        record.button = static_cast<RichTextBarButton>(item);
        read_item_parts(anatomy().item_systems.front(), rich_text_box_item_part_fields, published.parts(),
                        record.parts);
        read_states(rich_text_box_item_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(std::move(record));
    }
    return records;
}

AnatomyStateSet RichTextBox::anatomy_states() const {
    AnatomyStateSet states = ScrollableControl::anatomy_states();
    if (read_only()) states.insert(AnatomyState::read_only);
    if (visual_status() == ControlVisualStatus::invalid) {
        states.insert(AnatomyState::invalid);
    } else if (visual_status() == ControlVisualStatus::pending) {
        states.insert(AnatomyState::pending);
    } else {
        // Normal: no status state.
    }
    return states;
}

void RichTextBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), rich_text_box_interactions.begin(), rich_text_box_interactions.end());
}

} // namespace gui_forms
