#include "gui_forms/controls/value_bar.hpp"

#include "../range_control_rendering.hpp"
#include "value_text.hpp"
#include "../../../core/drag_engine/scrub_gesture.hpp"
#include "../../../core/theme/contrast/theme_contrast.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace range_control_detail;

namespace {

// The ValueBarStyle and ValueUnit enumerations, numbered as declared.
// Static storage; any thread may read them.
std::shared_ptr<const PropertyEnumDescriptor> value_bar_style_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(
            PropertyEnumDescriptor{"value_bar_style", {{"fill", 0}, {"field", 1}}, false});
    return value;
}

std::shared_ptr<const PropertyEnumDescriptor> value_unit_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "value_unit", {{"none", 0}, {"percent", 1}, {"length", 2}, {"angle", 3}, {"time", 4}}, false});
    return value;
}

// The bar's measures, in logical units: the preferred size, the field
// style's arrow width, the text's inline padding and the overrange
// marker's width.
constexpr double value_bar_preferred_width = 160.0;
constexpr double value_bar_preferred_height = 22.0;
constexpr double value_bar_arrow_width = 14.0;
constexpr double value_bar_text_padding = 6.0;
constexpr double value_bar_overrange_width = 3.0;

} // namespace

// The drag engine's client and the scrub's target for one ValueBar, which
// owns it (and so outlives the gesture inside it). Every call is on the
// bar's root's thread, from the engine's dispatch.
class ValueBarScrub final : public ScrubTarget {
public:
    explicit ValueBarScrub(ValueBar& bar) noexcept : bar_(&bar), gesture(bar, *this) {}

    [[nodiscard]] bool begin_scrub(ScrubRange& range) override;
    void preview_scrub(double value) override;
    void end_scrub(ScrubEnd end, double value) override;
    [[nodiscard]] std::size_t describe_scrub(double value, std::span<char> out) const override;

private:
    ValueBar* bar_;
    // The value when the scrub began: the restore target, and what a commit
    // is compared with (the previews have already written the value).
    double start_{};

public:
    ScrubGesture gesture;
};

// The scrub runs over the soft range at the bar's rate; Ctrl snaps to the
// snap step (else the small change); the keyboard mode steps by the small
// change. A disabled or editing bar has nothing to scrub.
bool ValueBarScrub::begin_scrub(ScrubRange& range) {
    ValueBar& bar = *bar_;
    if (!bar.enabled.read() || bar.editing_) return false;
    start_ = bar.value.read();
    range.start = start_;
    range.minimum = bar.effective_soft_minimum();
    range.maximum = bar.effective_soft_maximum();
    range.rate = bar.scrub_rate_in_force();
    const std::optional<double> snap = bar.snap_step.read();
    range.snap_step = snap ? *snap : bar.small_change.read();
    range.keyboard_step = bar.small_change.read();
    // Right to left the fill grows from the right, so dragging left raises.
    range.reversed = bar.effective_layout_direction() == LayoutDirection::right_to_left;
    bar.invalidate(Dirty::paint);
    return true;
}

void ValueBarScrub::preview_scrub(double value) {
    static_cast<void>((*bar_).set_value_from_input(value, RangeAction::thumb_track));
}

// A commit writes the final value as the thumb's position and raises
// value_committed when it differs from the start; a restore writes the
// start value back and raises nothing.
void ValueBarScrub::end_scrub(ScrubEnd end, double value) {
    ValueBar& bar = *bar_;
    static_cast<void>(bar.set_value_from_input(value, RangeAction::thumb_position));
    bar.invalidate(Dirty::paint | Dirty::semantics);
    if (end != ScrubEnd::committed) return;
    const double committed = bar.value.read();
    if (committed != start_) bar.value_committed_.emit(committed);
}

// The value as the bar shows it, written without allocating.
std::size_t ValueBarScrub::describe_scrub(double value, std::span<char> out) const {
    const ValueBar& bar = *bar_;
    const std::size_t length = value_text::format_value_text(value, bar.unit.read(), bar.precision.read(), out);
    return length;
}

ValueBar::ValueBar(StableId stable_id) : RangeControl(std::move(stable_id)) {
    focusable = true;
    cursor = CursorKind::resize_horizontal;
    scrub_ = std::make_unique<ValueBarScrub>(*this);
}

ValueBar::~ValueBar() = default;

// The inline editor: a borderless TextBox over the content area, hidden
// until begin_edit. Focus leaving it commits (EditorValidatingCallback).
void ValueBar::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    editor_ = make_control<TextBox>(StableId(prefix + ".editor"));
    (*editor_).border_style = BorderStyle::none;
    (*editor_).visible = false;
    add_child(editor_);
    const std::weak_ptr<ValueBar> weak = std::static_pointer_cast<ValueBar>(shared_from_this());
    editor_validating_ = (*editor_).validating().subscribe(*this, EditorValidatingCallback{weak});
}

// Focus leaving the open editor (to anything but the bar, which
// close_editor focuses itself) commits its text; a refused text cancels
// the edit, since the focus has already gone.
void ValueBar::EditorValidatingCallback::operator()(ControlValidationEvent&) const {
    const std::shared_ptr<ValueBar> bar = target.lock();
    if (!bar || !(*bar).editing_ || (*bar).closing_) return;
    if ((*bar).commit_editor_text(false)) return;
    (*bar).edit_defect_ = ValueTextDefect::none;
    (*bar).close_editor(false);
}

// -- ranges and text ----------------------------------------------------------

double ValueBar::effective_soft_minimum() const noexcept {
    const double low = minimum.read();
    const double high = maximum.read();
    const std::optional<double> soft_low = soft_minimum.read();
    const std::optional<double> soft_high = soft_maximum.read();
    const double first = std::clamp(soft_low ? *soft_low : low, low, high);
    const double second = std::clamp(soft_high ? *soft_high : high, low, high);
    const double result = std::min(first, second);
    return result;
}

double ValueBar::effective_soft_maximum() const noexcept {
    const double low = minimum.read();
    const double high = maximum.read();
    const std::optional<double> soft_low = soft_minimum.read();
    const std::optional<double> soft_high = soft_maximum.read();
    const double first = std::clamp(soft_low ? *soft_low : low, low, high);
    const double second = std::clamp(soft_high ? *soft_high : high, low, high);
    const double result = std::max(first, second);
    return result;
}

double ValueBar::soft_proportion() const noexcept {
    const double low = effective_soft_minimum();
    const double high = effective_soft_maximum();
    const double current = value.read();
    const double span = high - low;
    if (span <= 0.0) return current >= high ? 1.0 : 0.0;
    const double proportion = std::clamp((current - low) / span, 0.0, 1.0);
    return proportion;
}

bool ValueBar::overrange() const noexcept {
    const double current = value.read();
    const bool outside = current < effective_soft_minimum() || current > effective_soft_maximum();
    return outside;
}

std::string ValueBar::formatted_value() const {
    std::string text{};
    value_text::format_value_text(value.read(), unit.read(), precision.read(), text);
    return text;
}

// Without a scrub rate, dragging across the content area covers the soft
// range; an empty soft range scrubs by a tenth of the small change per
// unit.
double ValueBar::scrub_rate_in_force() const noexcept {
    const std::optional<double> authored = scrub_rate.read();
    if (authored) return *authored;
    const double span = effective_soft_maximum() - effective_soft_minimum();
    if (span <= 0.0) return small_change.read() * scrub_fine_factor;
    const double width = std::max(1.0, content_bounds().width);
    const double rate = span / width;
    return rate;
}

double ValueBar::key_step(Modifier modifiers) const noexcept {
    if (has_modifier(modifiers, Modifier::control)) return large_change.read();
    if (has_modifier(modifiers, Modifier::shift)) return small_change.read() * scrub_fine_factor;
    return small_change.read();
}

// -- user edits ---------------------------------------------------------------

bool ValueBar::commit_value(double requested, RangeAction action) {
    if (!std::isfinite(requested)) return false;
    const double before = value.read();
    static_cast<void>(set_value_from_input(requested, action));
    const double after = value.read();
    if (after == before) return false;
    value_committed_.emit(after);
    return true;
}

bool ValueBar::step_by(double delta) {
    const RangeAction action = delta < 0.0 ? RangeAction::small_decrement : RangeAction::small_increment;
    const bool changed = commit_value(value.read() + delta, action);
    return changed;
}

bool ValueBar::reset_to_default() {
    const bool changed = commit_value(default_value.read(), RangeAction::thumb_position);
    return changed;
}

bool ValueBar::negate() {
    const bool changed = commit_value(-value.read(), RangeAction::thumb_position);
    return changed;
}

ValueTextDefect ValueBar::apply_text(std::string_view text) {
    const value_text::ValueTextResult parsed = value_text::parse_value_text(text, unit.read());
    if (parsed.defect != ValueTextDefect::none) return parsed.defect;
    static_cast<void>(commit_value(parsed.value, RangeAction::thumb_position));
    return ValueTextDefect::none;
}

bool ValueBar::copy_value() {
    Window* const host = window();
    HostServices* const services = host != nullptr ? (*host).host_services() : nullptr;
    if (services == nullptr) return false;
    const std::string text = formatted_value();
    const HostServiceStatus status = (*services).write_clipboard_text(text);
    return status.accepted();
}

bool ValueBar::paste_value() {
    Window* const host = window();
    HostServices* const services = host != nullptr ? (*host).host_services() : nullptr;
    if (services == nullptr) return false;
    const HostClipboardTextResult result = (*services).read_clipboard_text();
    if (!result.status.accepted() || !result.has_text) return false;
    const bool applied = apply_text(result.text_utf8) == ValueTextDefect::none;
    return applied;
}

bool ValueBar::scrubbing() const noexcept {
    const bool running = scrub_ && (*scrub_).gesture.state().scrubbing;
    return running;
}

// -- the inline editor --------------------------------------------------------

bool ValueBar::begin_edit() {
    if (editing_) return true;
    if (!enabled.read() || !editor_ || scrubbing()) return false;
    editing_ = true;
    edit_defect_ = ValueTextDefect::none;
    opened_text_ = formatted_value();
    (*editor_).text = opened_text_;
    (*editor_).visible = true;
    set_child_layout(editor_, content_bounds());
    if (window() != nullptr) static_cast<void>((*window()).request_focus(editor_));
    (*editor_).select_all();
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

bool ValueBar::commit_edit() {
    const bool committed = commit_editor_text(true);
    return committed;
}

bool ValueBar::commit_editor_text(bool restore_focus) {
    if (!editing_ || !editor_) return false;
    const std::string text = (*editor_).text.read();
    if (text == opened_text_) {
        edit_defect_ = ValueTextDefect::none;
        close_editor(restore_focus);
        return true;
    }
    const value_text::ValueTextResult parsed = value_text::parse_value_text(text, unit.read());
    if (parsed.defect != ValueTextDefect::none) {
        edit_defect_ = parsed.defect;
        invalidate(Dirty::paint | Dirty::semantics);
        return false;
    }
    edit_defect_ = ValueTextDefect::none;
    // Closed first, so value_committed's subscribers see the bar as shown.
    close_editor(restore_focus);
    static_cast<void>(commit_value(parsed.value, RangeAction::thumb_position));
    return true;
}

void ValueBar::cancel_edit() {
    if (!editing_) return;
    edit_defect_ = ValueTextDefect::none;
    close_editor(true);
}

void ValueBar::close_editor(bool restore_focus) {
    closing_ = true;
    editing_ = false;
    opened_text_.clear();
    // The focus returns to the bar only from the editor: an edit closed
    // from code while the focus is elsewhere leaves it there.
    Window* const host = window();
    if (restore_focus && host != nullptr && editor_ && (*host).focused_control() == editor_) {
        static_cast<void>((*host).request_focus(shared_from_this()));
    }
    if (editor_) (*editor_).visible = false;
    closing_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- geometry -----------------------------------------------------------------

Size ValueBar::measure(Size available) {
    const Rect requested = requested_bounds();
    const double width = requested.width > 0.0 ? requested.width : value_bar_preferred_width;
    const double height = requested.height > 0.0 ? requested.height : value_bar_preferred_height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

void ValueBar::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    if (editor_) set_child_layout(editor_, content_bounds());
}

Rect ValueBar::track_bounds() const noexcept {
    const Rect track = local_bounds();
    return track;
}

Rect ValueBar::fill_bounds() const noexcept {
    if (presentation.read() == ValueBarStyle::field) return {};
    Rect filled = track_bounds();
    const double width = filled.width * soft_proportion();
    // From the right edge under RightToLeft, as a progress bar fills.
    if (effective_layout_direction() == LayoutDirection::right_to_left) filled.x += filled.width - width;
    filled.width = width;
    return filled;
}

Rect ValueBar::content_bounds() const noexcept {
    const Rect track = track_bounds();
    const double inset = presentation.read() == ValueBarStyle::field ? value_bar_arrow_width : value_bar_text_padding;
    const double width = std::max(0.0, track.width - 2.0 * inset);
    const Rect content{track.x + std::min(inset, track.width * 0.5), track.y, width, track.height};
    return content;
}

Rect ValueBar::decrement_bounds() const noexcept {
    if (presentation.read() != ValueBarStyle::field) return {};
    const Rect track = track_bounds();
    const Rect arrow{track.x, track.y, std::min(value_bar_arrow_width, track.width * 0.5), track.height};
    // The decrement arrow is at the minimum's end: the right under RightToLeft.
    return directed_rect(arrow, track, effective_layout_direction());
}

Rect ValueBar::increment_bounds() const noexcept {
    if (presentation.read() != ValueBarStyle::field) return {};
    const Rect track = track_bounds();
    const double width = std::min(value_bar_arrow_width, track.width * 0.5);
    const Rect arrow{track.right() - width, track.y, width, track.height};
    return directed_rect(arrow, track, effective_layout_direction());
}

Rect ValueBar::overrange_bounds() const noexcept {
    if (presentation.read() != ValueBarStyle::fill || !overrange()) return {};
    const Rect track = track_bounds();
    const double width = std::min(value_bar_overrange_width, track.width);
    const bool above = value.read() > effective_soft_maximum();
    const Rect marker{above ? track.right() - width : track.x, track.y, width, track.height};
    return directed_rect(marker, track, effective_layout_direction());
}

namespace {

// The field style's arrows are hot under the pointer and pressed while
// held.
constexpr std::array<AnatomyState, 2> value_bar_arrow_states{{AnatomyState::hot, AnatomyState::pressed}};

constexpr std::array<PartField<ValueBar::Parts>, 12> value_bar_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ValueBar::Parts::region},
    {.declaration = {.name = "track", .role = PartRole::track, .composed_from = {}},
     .member = &ValueBar::Parts::track},
    {.declaration = {.name = "fill", .role = PartRole::fill, .composed_from = {}},
     .member = &ValueBar::Parts::fill},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &ValueBar::Parts::label},
    {.declaration = {.name = "value", .role = PartRole::label, .composed_from = {}},
     .member = &ValueBar::Parts::value},
    {.declaration = {.name = "decrement_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = value_bar_arrow_states},
     .member = &ValueBar::Parts::decrement_arrow},
    {.declaration = {.name = "increment_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = value_bar_arrow_states},
     .member = &ValueBar::Parts::increment_arrow},
    {.declaration = {.name = "decrement_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = value_bar_arrow_states},
     .member = &ValueBar::Parts::decrement_glyph},
    {.declaration = {.name = "increment_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = value_bar_arrow_states},
     .member = &ValueBar::Parts::increment_glyph},
    {.declaration = {.name = "overrange", .role = PartRole::indicator, .composed_from = {}},
     .member = &ValueBar::Parts::overrange},
    {.declaration = {.name = "validation", .role = PartRole::validation, .composed_from = {}},
     .member = &ValueBar::Parts::validation},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ValueBar::Parts::focus},
}};

constexpr std::array<StateField<ValueBar::States>, 9> value_bar_state_fields{{
    {.state = AnatomyState::normal, .member = &ValueBar::States::normal},
    {.state = AnatomyState::hot, .member = &ValueBar::States::hot},
    {.state = AnatomyState::pressed, .member = &ValueBar::States::pressed},
    {.state = AnatomyState::captured, .member = &ValueBar::States::captured},
    {.state = AnatomyState::enabled, .member = &ValueBar::States::enabled},
    {.state = AnatomyState::disabled, .member = &ValueBar::States::disabled},
    {.state = AnatomyState::focused, .member = &ValueBar::States::focused},
    {.state = AnatomyState::focus_visible, .member = &ValueBar::States::focus_visible},
    {.state = AnatomyState::invalid, .member = &ValueBar::States::invalid},
}};

constexpr std::array<PartDeclaration, 12> value_bar_parts = part_declarations(value_bar_part_fields);
constexpr std::array<AnatomyState, 9> value_bar_states = state_declarations(value_bar_state_fields);

constexpr ControlAnatomy value_bar_anatomy{
    .control_id = "value_bar",
    .parts = value_bar_parts,
    .states = value_bar_states,
    .custom_states = {},
};

// The events ValueBar adds to those of RangeControl, in declaration order.
constexpr std::array<InteractionDeclaration, 1> value_bar_interactions{{
    {.name = "value_committed", .payload = "double", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& ValueBar::anatomy() const noexcept {
    return value_bar_anatomy;
}

void ValueBar::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    RangeControl::append_interactions(interactions);
    interactions.insert(interactions.end(), value_bar_interactions.begin(), value_bar_interactions.end());
}

void ValueBar::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect track = track_bounds();
    const Rect content = content_bounds();
    Parts record;
    record.region = PartGeometry::from_bounds(local_bounds());
    record.track = PartGeometry::from_bounds(track);
    record.fill = PartGeometry::from_bounds(fill_bounds());
    if (!label.read().empty()) record.label = PartGeometry::from_bounds(content);
    record.value = PartGeometry::from_bounds(content);
    record.decrement_arrow = PartGeometry::from_bounds(decrement_bounds());
    record.increment_arrow = PartGeometry::from_bounds(increment_bounds());
    record.decrement_glyph = PartGeometry::from_bounds(decrement_bounds());
    record.increment_glyph = PartGeometry::from_bounds(increment_bounds());
    record.overrange = PartGeometry::from_bounds(overrange_bounds());
    if (edit_defect_ != ValueTextDefect::none) record.validation = PartGeometry::from_bounds(track);
    record.focus = PartGeometry::from_bounds(track);
    write_parts(anatomy(), value_bar_part_fields, record, geometry);
}

AnatomyStateSet ValueBar::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (scrubbing()) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::pressed);
        states.insert(AnatomyState::captured);
    }
    if (edit_defect_ != ValueTextDefect::none) states.insert(AnatomyState::invalid);
    return states;
}

ValueBar::Parts ValueBar::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), value_bar_part_fields, published.parts(), record);
    return record;
}

ValueBar::States ValueBar::states() const {
    States record;
    read_states(value_bar_state_fields, anatomy_states(), record);
    return record;
}

// -- painting -----------------------------------------------------------------

Color ValueBar::over_fill_text_colour(AnatomyStateSet states) const {
    const Theme& theme = effective_theme();
    const ControlVisualContext context = part_visual_context(*this, states);
    const ResolvedControlRecipe label_recipe = theme.resolve(*this, ControlVisualRole::editor, context, states, "label");
    const std::optional<Color> authored = label_recipe.recipe().fill_text;
    if (authored) return *authored;
    AnatomyStateSet fill_states = states;
    fill_states.insert(AnatomyState::selected);
    const ControlVisualContext fill_context = part_visual_context(*this, fill_states);
    const ResolvedControlRecipe fill_recipe =
        theme.resolve(*this, ControlVisualRole::progress, fill_context, fill_states, "fill");
    const Color text = fill_recipe.recipe().text;
    return text;
}

// The label at the content's start and the value at its end, or the value
// centred without a label. The half over the track is keyed (the theme's
// `::part(label)` and `::part(value)` rules style it); the half over the
// fill is drawn unkeyed in `over_fill`, as an authored foreground.
void ValueBar::paint_texts(Painter& painter, AnatomyStateSet states, std::optional<Color> over_fill) const {
    const FontSpec font = text_font(effective_theme().structure().typography.field);
    const std::string shown = formatted_value();
    const Rect content = content_bounds();
    const std::string& name = label.read();
    PartPaint text;
    text.family = ControlVisualRole::editor;
    text.role = PartRole::label;
    text.bounds = content;
    text.states = states;
    text.font = font;
    text.authored_foreground = over_fill;
    if (!name.empty()) {
        text.text = lend_paint_text(name);
        text.alignment = PartTextAlignment::start;
        if (!over_fill) text.key = PartKey{"label", {}, {}};
        paint_part(painter, *this, text);
    }
    text.text = lend_paint_text(shown);
    text.alignment = name.empty() ? PartTextAlignment::center : PartTextAlignment::end;
    text.key = std::nullopt;
    if (!over_fill) text.key = PartKey{"value", {}, {}};
    paint_part(painter, *this, text);
}

// The track, the fill (fill style) and its overrange marker, the texts in
// two clipped passes, the arrows (field style, while hovered or focused),
// the validation mark of a refused edit and the focus cue. While the
// editor is open it covers the content and the texts are not drawn.
void ValueBar::on_paint(Painter& painter, Rect) {
    const AnatomyStateSet states = anatomy_states();
    const bool field = presentation.read() == ValueBarStyle::field;
    const Rect track = track_bounds();
    PartPaint track_part;
    track_part.family = ControlVisualRole::editor;
    track_part.role = PartRole::track;
    track_part.bounds = track;
    track_part.states = states;
    track_part.key = PartKey{"track", {}, {}};
    paint_part(painter, *this, track_part);
    const Rect fill = fill_bounds();
    if (!field) {
        PartPaint fill_part;
        fill_part.family = ControlVisualRole::progress;
        fill_part.role = PartRole::fill;
        fill_part.bounds = fill;
        fill_part.states = states;
        fill_part.states.insert(AnatomyState::selected);
        fill_part.key = PartKey{"fill", {}, {}};
        paint_part(painter, *this, fill_part);
        const Rect marker = overrange_bounds();
        if (!marker.empty()) {
            PartPaint overrange_part;
            overrange_part.family = ControlVisualRole::progress;
            overrange_part.role = PartRole::indicator;
            overrange_part.bounds = marker;
            overrange_part.states = states;
            overrange_part.key = PartKey{"overrange", {}, {}};
            paint_part(painter, *this, overrange_part);
        }
    }
    if (!editing_) {
        const double split = field ? track.x : fill.right();
        painter.save();
        painter.clip_rect(Rect{split, track.y, std::max(0.0, track.right() - split), track.height});
        paint_texts(painter, states, std::nullopt);
        painter.restore();
        if (!field && !fill.empty()) {
            painter.save();
            painter.clip_rect(fill);
            paint_texts(painter, states, over_fill_text_colour(states));
            painter.restore();
        }
    }
    const bool arrows_shown = field && !editing_ && (states.contains(AnatomyState::hot) || focused_);
    if (arrows_shown) {
        const std::array<Rect, 2> arrows{decrement_bounds(), increment_bounds()};
        const std::array<PartMark, 2> marks{PartMark::arrow_left, PartMark::arrow_right};
        const std::array<std::string_view, 2> arrow_names{"decrement_arrow", "increment_arrow"};
        const std::array<std::string_view, 2> glyph_names{"decrement_glyph", "increment_glyph"};
        for (std::size_t index = 0U; index < arrows.size(); ++index) {
            PartPaint arrow;
            arrow.family = ControlVisualRole::button;
            arrow.role = PartRole::actuator;
            arrow.bounds = arrows[index];
            arrow.states = states;
            arrow.key = PartKey{arrow_names[index], {}, {}};
            paint_part(painter, *this, arrow);
            arrow.role = PartRole::glyph;
            arrow.mark = marks[index];
            arrow.key = PartKey{glyph_names[index], {}, {}};
            paint_part(painter, *this, arrow);
        }
    }
    if (edit_defect_ != ValueTextDefect::none) {
        PartPaint validation;
        validation.family = ControlVisualRole::editor;
        validation.role = PartRole::validation;
        validation.bounds = track;
        validation.states = states;
        validation.key = PartKey{"validation", {}, {}};
        paint_part(painter, *this, validation);
    }
    if (focused_ && !editing_) {
        PartPaint focus;
        focus.family = ControlVisualRole::editor;
        focus.role = PartRole::focus;
        focus.bounds = track;
        focus.states = states;
        focus.states.insert(AnatomyState::focus_visible);
        focus.key = PartKey{"focus", {}, {}};
        paint_part(painter, *this, focus);
    }
}

// The two halves of the label measured as drawn: the label part's text
// colour over the track's face, and the fill text colour over the fill's
// face (the fill over the track).
ValueBarContrast ValueBar::fill_text_contrast() const {
    const Theme& theme = effective_theme();
    const AnatomyStateSet states = anatomy_states();
    const ControlVisualContext context = part_visual_context(*this, states);
    const ResolvedControlRecipe label_recipe = theme.resolve(*this, ControlVisualRole::editor, context, states, "label");
    const ResolvedControlRecipe track_recipe = theme.resolve(*this, ControlVisualRole::editor, context, states, "track");
    AnatomyStateSet fill_states = states;
    fill_states.insert(AnatomyState::selected);
    const ControlVisualContext fill_context = part_visual_context(*this, fill_states);
    const ResolvedControlRecipe fill_recipe =
        theme.resolve(*this, ControlVisualRole::progress, fill_context, fill_states, "fill");
    const Color over_fill = over_fill_text_colour(states);
    const detail::FillTextContrast measured = detail::measure_fill_text_contrast(
        theme, context, ControlVisualRole::progress, label_recipe.recipe().text, track_recipe.recipe().material,
        over_fill, fill_recipe.recipe().material);
    ValueBarContrast result{};
    result.over_track = measured.over_track.ratio;
    result.over_fill = measured.over_fill.ratio;
    result.minimum = measured.over_track.minimum;
    result.passes = measured.over_track.verdict != detail::ContrastVerdict::fail &&
                    measured.over_fill.verdict != detail::ContrastVerdict::fail;
    return result;
}

// -- input --------------------------------------------------------------------

// A press on an arrow (field style) steps; any other primary press arms
// the scrub, whose release before the threshold opens the editor. Ctrl and
// the wheel step while hovered; a plain wheel is left to the parent.
void ValueBar::on_pointer(PointerEvent& event) {
    if (!enabled.read() || editing_ || !scrub_) return;
    if (event.action == PointerAction::wheel) {
        if (!has_modifier(event.modifiers, Modifier::control) || event.wheel_delta.y == 0.0) return;
        const double delta = event.wheel_delta.y > 0.0 ? small_change.read() : -small_change.read();
        static_cast<void>(step_by(delta));
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::down && event.button == PointerButton::primary) {
        const Point local = point_from_window(event.position);
        if (decrement_bounds().contains(local)) {
            static_cast<void>(step_by(-small_change.read()));
            event.handled = true;
            return;
        }
        if (increment_bounds().contains(local)) {
            static_cast<void>(step_by(small_change.read()));
            event.handled = true;
            return;
        }
        if ((*scrub_).gesture.press(event, ScrubPointerLock::clamped)) event.handled = true;
        return;
    }
    const ScrubPointerResult result = (*scrub_).gesture.pointer(event);
    if (result == ScrubPointerResult::ignored) return;
    event.handled = true;
    if (result == ScrubPointerResult::clicked) static_cast<void>(begin_edit());
}

// The open editor's keys, seen before the editor: Enter and Tab commit,
// Escape cancels.
void ValueBar::on_key_preview(KeyEvent& event) {
    if (!editing_ || event.action != KeyAction::down) return;
    if (event.physical_key == PhysicalKey::enter || event.physical_key == PhysicalKey::tab) {
        static_cast<void>(commit_edit());
        event.handled = true;
    } else if (event.physical_key == PhysicalKey::escape) {
        cancel_edit();
        event.handled = true;
    }
}

void ValueBar::on_key(KeyEvent& event) {
    if (event.action != KeyAction::down || !enabled.read() || editing_ || !scrub_) return;
    if ((*scrub_).gesture.key(event)) {
        event.handled = true;
        return;
    }
    const Modifier modifiers = event.modifiers;
    const bool control = has_modifier(modifiers, Modifier::control);
    bool recognized = true;
    // Left and Right move the fill's edge that way: under RightToLeft Left
    // steps up.
    const bool mirrored = effective_layout_direction() == LayoutDirection::right_to_left;
    std::uint32_t key = event.physical_key;
    if (mirrored && key == PhysicalKey::left) key = PhysicalKey::right;
    else if (mirrored && key == PhysicalKey::right) key = PhysicalKey::left;
    switch (key) {
    case PhysicalKey::enter:
    case PhysicalKey::f2:
        static_cast<void>(begin_edit());
        break;
    case PhysicalKey::left:
    case PhysicalKey::down:
        static_cast<void>(step_by(-key_step(modifiers)));
        break;
    case PhysicalKey::right:
    case PhysicalKey::up:
        static_cast<void>(step_by(key_step(modifiers)));
        break;
    case PhysicalKey::page_down:
        static_cast<void>(commit_value(value.read() - large_change.read(), RangeAction::large_decrement));
        break;
    case PhysicalKey::page_up:
        static_cast<void>(commit_value(value.read() + large_change.read(), RangeAction::large_increment));
        break;
    case PhysicalKey::home:
        static_cast<void>(commit_value(effective_soft_minimum(), RangeAction::first));
        break;
    case PhysicalKey::end:
        static_cast<void>(commit_value(effective_soft_maximum(), RangeAction::last));
        break;
    case PhysicalKey::backspace:
        static_cast<void>(reset_to_default());
        break;
    case PhysicalKey::minus:
    case PhysicalKey::keypad_minus:
        recognized = !control;
        if (recognized) static_cast<void>(negate());
        break;
    case PhysicalKey::c:
        recognized = control;
        if (recognized) static_cast<void>(copy_value());
        break;
    case PhysicalKey::v:
        recognized = control;
        if (recognized) static_cast<void>(paste_value());
        break;
    default:
        recognized = false;
        break;
    }
    if (recognized) event.handled = true;
}

void ValueBar::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

bool ValueBar::begin_keyboard_drag() {
    if (!enabled.read() || editing_ || !scrub_) return false;
    const bool started = (*scrub_).gesture.begin_keyboard();
    return started;
}

// -- semantics ----------------------------------------------------------------

SemanticDescriptor ValueBar::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role =
        presentation.read() == ValueBarStyle::fill ? SemanticRole::slider : SemanticRole::numeric_field;
    const std::string& authored_name = accessible_name.read();
    descriptor.name = authored_name.empty() ? label.read() : authored_name;
    descriptor.description = accessible_description.read();
    descriptor.value = formatted_value();
    descriptor.numeric_value = value.read();
    descriptor.minimum_value = minimum.read();
    descriptor.maximum_value = maximum.read();
    descriptor.small_step = small_change.read();
    descriptor.large_step = large_change.read();
    descriptor.actions = {SemanticAction::focus, SemanticAction::increment, SemanticAction::decrement,
                          SemanticAction::set_value};
    descriptor.exposed = true;
    // The editor is published (as an edit) only while it is open.
    descriptor.include_descendants = editing_;
    return descriptor;
}

// Increment and decrement step by the small change; set_value takes text
// as typed (arithmetic and units), clamped to the hard range.
bool ValueBar::on_semantic_action(SemanticAction action, std::string_view value_text) {
    if (action == SemanticAction::increment || action == SemanticAction::decrement) {
        const double delta = action == SemanticAction::increment ? small_change.read() : -small_change.read();
        static_cast<void>(step_by(delta));
        return true;
    }
    if (action == SemanticAction::set_value) {
        const bool applied = apply_text(value_text) == ValueTextDefect::none;
        return applied;
    }
    return RangeControl::on_semantic_action(action, value_text);
}

// property_codemod begin ValueBar (tools/property_families/value_bar.json; generated, do not edit)

namespace {

PropertyDescriptor value_bar_soft_minimum_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "soft_minimum";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "Low end of the scrub and fill range; null for the hard minimum.";
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.nullable = true;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_value_bar_soft_minimum() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        value_bar_soft_minimum_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor value_bar_soft_maximum_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "soft_maximum";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "High end of the scrub and fill range; null for the hard maximum.";
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.nullable = true;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_value_bar_soft_maximum() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        value_bar_soft_maximum_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor value_bar_snap_step_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "snap_step";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "Ctrl snapping grid while scrubbing; null for the small change.";
    descriptor.invalidation_effects = Dirty::semantics;
    descriptor.bindable = false;
    descriptor.nullable = true;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_value_bar_snap_step() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        value_bar_snap_step_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor value_bar_precision_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "precision";
    descriptor.kind = BindingValueKind::signed_integer;
    descriptor.category = "Appearance";
    descriptor.description = "Decimals the value shows, 0 to 12.";
    descriptor.default_value = BindingValue{std::int64_t{3}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_value_bar_precision() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        value_bar_precision_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor value_bar_unit_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "unit";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "Unit the value is shown and typed in.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"value_unit", "none", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = value_unit_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_value_bar_unit() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        value_bar_unit_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor value_bar_default_value_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "default_value";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "Value the reset key restores.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_value_bar_default_value() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        value_bar_default_value_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor value_bar_presentation_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "presentation";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "Fill (slider) or field (number with step arrows) style.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"value_bar_style", "fill", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::hit_test | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = value_bar_style_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_value_bar_presentation() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        value_bar_presentation_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor value_bar_label_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "label";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "Name shown at the bar's start; empty centres the value.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_value_bar_label() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        value_bar_label_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor value_bar_scrub_rate_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "scrub_rate";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Behavior";
    descriptor.description = "Value per unit of scrub travel; null for the soft range over the width.";
    descriptor.invalidation_effects = Dirty::semantics;
    descriptor.bindable = false;
    descriptor.nullable = true;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_value_bar_scrub_rate() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        value_bar_scrub_rate_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<ValueUnit>::type() {
    return PropertyType{BindingValueKind::enumeration, false, value_unit_enum()};
}

BindingValue PropertyValueTraits<ValueUnit>::to_binding(ValueUnit value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = value_unit_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

ValueUnit PropertyValueTraits<ValueUnit>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<ValueUnit>(choice.value);
}

PropertyType PropertyValueTraits<ValueBarStyle>::type() {
    return PropertyType{BindingValueKind::enumeration, false, value_bar_style_enum()};
}

BindingValue PropertyValueTraits<ValueBarStyle>::to_binding(ValueBarStyle value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = value_bar_style_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

ValueBarStyle PropertyValueTraits<ValueBarStyle>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<ValueBarStyle>(choice.value);
}

const std::array<PropertyEntry, ValueBar::value_bar_property_count>& ValueBar::value_bar_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 9U> own{{
        {"soft_minimum", &describe_value_bar_soft_minimum,
         &read_cell_entry<ValueBar, decltype(&ValueBar::soft_minimum), &ValueBar::soft_minimum>,
         &write_cell_entry<ValueBar, decltype(&ValueBar::soft_minimum), &ValueBar::soft_minimum>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"soft_maximum", &describe_value_bar_soft_maximum,
         &read_cell_entry<ValueBar, decltype(&ValueBar::soft_maximum), &ValueBar::soft_maximum>,
         &write_cell_entry<ValueBar, decltype(&ValueBar::soft_maximum), &ValueBar::soft_maximum>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"snap_step", &describe_value_bar_snap_step,
         &read_cell_entry<ValueBar, decltype(&ValueBar::snap_step), &ValueBar::snap_step>,
         &write_cell_entry<ValueBar, decltype(&ValueBar::snap_step), &ValueBar::snap_step>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"precision", &describe_value_bar_precision,
         &read_cell_entry<ValueBar, decltype(&ValueBar::precision), &ValueBar::precision>,
         &write_cell_entry<ValueBar, decltype(&ValueBar::precision), &ValueBar::precision>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"unit", &describe_value_bar_unit,
         &read_cell_entry<ValueBar, decltype(&ValueBar::unit), &ValueBar::unit>,
         &write_cell_entry<ValueBar, decltype(&ValueBar::unit), &ValueBar::unit>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"default_value", &describe_value_bar_default_value,
         &read_cell_entry<ValueBar, decltype(&ValueBar::default_value), &ValueBar::default_value>,
         &write_cell_entry<ValueBar, decltype(&ValueBar::default_value), &ValueBar::default_value>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"presentation", &describe_value_bar_presentation,
         &read_cell_entry<ValueBar, decltype(&ValueBar::presentation), &ValueBar::presentation>,
         &write_cell_entry<ValueBar, decltype(&ValueBar::presentation), &ValueBar::presentation>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"label", &describe_value_bar_label,
         &read_cell_entry<ValueBar, decltype(&ValueBar::label), &ValueBar::label>,
         &write_cell_entry<ValueBar, decltype(&ValueBar::label), &ValueBar::label>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"scrub_rate", &describe_value_bar_scrub_rate,
         &read_cell_entry<ValueBar, decltype(&ValueBar::scrub_rate), &ValueBar::scrub_rate>,
         &write_cell_entry<ValueBar, decltype(&ValueBar::scrub_rate), &ValueBar::scrub_rate>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ValueBar's property table names a property twice");
    static const std::array<PropertyEntry, value_bar_property_count> entries =
        join_property_tables(RangeControl::range_control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ValueBar::property_table() const noexcept {
    const std::array<PropertyEntry, value_bar_property_count>& entries = value_bar_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ValueBar::prepare_write(const decltype(soft_minimum)&, std::optional<double>& incoming) {
    require_mutable();
    if (incoming) require_finite(*incoming, "soft minimum must be finite");
}

void ValueBar::written(const decltype(soft_minimum)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void ValueBar::prepare_write(const decltype(soft_maximum)&, std::optional<double>& incoming) {
    require_mutable();
    if (incoming) require_finite(*incoming, "soft maximum must be finite");
}

void ValueBar::written(const decltype(soft_maximum)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void ValueBar::prepare_write(const decltype(snap_step)&, std::optional<double>& incoming) {
    require_mutable();
    if (incoming) {
        require_finite(*incoming, "snap step must be finite");
        if (*incoming <= 0.0) throw std::invalid_argument("snap step must be positive");
    }
}

void ValueBar::written(const decltype(snap_step)&) {
    invalidate(Dirty::semantics);
}

void ValueBar::prepare_write(const decltype(precision)&, std::int32_t& incoming) {
    require_mutable();
    if (incoming < 0 || incoming > maximum_value_bar_precision) {
        throw std::invalid_argument("precision must be from 0 to 12");
    }
}

void ValueBar::written(const decltype(precision)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void ValueBar::prepare_write(const decltype(unit)&, ValueUnit&) {
    require_mutable();
}

void ValueBar::written(const decltype(unit)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void ValueBar::prepare_write(const decltype(default_value)&, double& incoming) {
    require_mutable();
    require_finite(incoming, "default value must be finite");
}

void ValueBar::written(const decltype(default_value)&) {
    invalidate(Dirty::semantics);
}

void ValueBar::prepare_write(const decltype(presentation)&, ValueBarStyle&) {
    require_mutable();
}

void ValueBar::written(const decltype(presentation)&) {
    invalidate(Dirty::layout | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ValueBar::prepare_write(const decltype(label)&, std::string&) {
    require_mutable();
}

void ValueBar::written(const decltype(label)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void ValueBar::prepare_write(const decltype(scrub_rate)&, std::optional<double>& incoming) {
    require_mutable();
    if (incoming) {
        require_finite(*incoming, "scrub rate must be finite");
        if (*incoming <= 0.0) throw std::invalid_argument("scrub rate must be positive");
    }
}

void ValueBar::written(const decltype(scrub_rate)&) {
    invalidate(Dirty::semantics);
}

// property_codemod end ValueBar

} // namespace gui_forms
