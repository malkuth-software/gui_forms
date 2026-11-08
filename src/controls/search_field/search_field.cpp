#include "gui_forms/controls/search_field/search_field.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// The field's metrics, in logical units: the space inside the well before
// the search mark and after the clear mark, the marks' side, the gap
// between a mark and the editor, the least height, the natural width, and
// the busy strip's height above the well's bottom edge.
constexpr double inset = 6.0;
constexpr double mark_side = 16.0;
constexpr double gap = 4.0;
constexpr double minimum_height = 28.0;
constexpr double natural_width = 220.0;
constexpr double activity_height = 2.0;

constexpr std::array<AnatomyState, 2> clear_part_states{{AnatomyState::hot, AnatomyState::pressed}};

constexpr std::array<PartField<SearchField::Parts>, 7> search_field_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &SearchField::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &SearchField::Parts::well},
    {.declaration = {.name = "icon", .role = PartRole::glyph, .composed_from = {}},
     .member = &SearchField::Parts::icon},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = "text_box"},
     .member = &SearchField::Parts::content},
    {.declaration = {.name = "actuator", .role = PartRole::glyph, .composed_from = {}, .states = clear_part_states},
     .member = &SearchField::Parts::actuator},
    {.declaration = {.name = "activity", .role = PartRole::activity, .composed_from = {}},
     .member = &SearchField::Parts::activity},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &SearchField::Parts::focus},
}};

constexpr std::array<PartDeclaration, 7> search_field_parts = part_declarations(search_field_part_fields);

constexpr std::array<AnatomyState, 6> search_field_states{
    AnatomyState::normal,  AnatomyState::enabled,       AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible, AnatomyState::pending,
};

constexpr ControlAnatomy search_field_anatomy{
    .control_id = "search_field",
    .parts = search_field_parts,
    .states = search_field_states,
    .custom_states = {},
};

// The events SearchField adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 3> search_field_interactions{{
    {.name = "text_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "search_requested", .payload = "std::string", .mutable_payload = false},
    {.name = "cleared", .payload = "", .mutable_payload = false},
}};

} // namespace

struct SearchField::SearchDelayCallback final {
    std::weak_ptr<SearchField> field;

    void operator()(FrameTime) const {
        const std::shared_ptr<SearchField> retained = field.lock();
        if (!retained) return;
        (*retained).raise_search();
    }
};

SearchField::SearchField(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::control);
    set_focusable(false);
    cursor = CursorKind::text;
}

SearchField::~SearchField() {
    search_timer_.disconnect();
    editor_text_.disconnect();
    editor_commit_.disconnect();
    editor_cancel_.disconnect();
    editor_focus_.disconnect();
}

// The editor draws no border of its own (the field's well is the ground).
// It accepts Enter, which searches now, and accepts Escape only while it
// has text, so that Escape in an empty field goes on to the window.
void SearchField::initialize_control_tree() {
    if (editor_) return;
    std::string id(stable_id().value());
    id += ".editor";
    editor_ = make_control<TextBox>(StableId(id));
    (*editor_).border_style = BorderStyle::none;
    (*editor_).accepts_enter = true;
    (*editor_).accepts_escape = false;
    add_child(editor_);
    editor_text_ = (*editor_).text_changed().subscribe(
        *this, Delegate<const std::string&>::bind<SearchField, &SearchField::editor_text_changed>(*this));
    editor_commit_ = (*editor_).committed().subscribe(
        *this, Delegate<const std::string&>::bind<SearchField, &SearchField::editor_committed>(*this));
    editor_cancel_ =
        (*editor_).cancelled().subscribe(*this, Delegate<>::bind<SearchField, &SearchField::editor_cancelled>(*this));
    editor_focus_ = (*editor_).focus_observed().subscribe(
        *this, Delegate<bool>::bind<SearchField, &SearchField::editor_focus_changed>(*this));
}

void SearchField::search_now() {
    require_mutable();
    raise_search();
}

// The text empties through the editor (text_changed follows, and starts a
// delay this drops); cleared and the empty query are raised after it.
bool SearchField::clear() {
    require_mutable();
    if (!editor_ || text.read().empty()) return false;
    const Control::Ptr retained = shared_from_this();
    text = std::string{};
    if (!is_alive()) return true;
    search_timer_.disconnect();
    publish_change(cleared_);
    if (!is_alive()) return true;
    raise_search();
    return true;
}

void SearchField::raise_search() {
    search_timer_.disconnect();
    const std::string query = text.read();
    publish_change(search_requested_, query);
}

void SearchField::schedule_search() {
    search_timer_.disconnect();
    Window* const host = window();
    const std::chrono::milliseconds delay = search_delay.read();
    if (host == nullptr || delay.count() == 0) {
        raise_search();
        return;
    }
    const std::shared_ptr<SearchField> self = std::static_pointer_cast<SearchField>(shared_from_this());
    const std::weak_ptr<SearchField> weak = self;
    search_timer_ =
        (*host).schedule_ui_timer(*this, std::chrono::hours(24), FrameClock::now() + delay, SearchDelayCallback{weak});
}

void SearchField::editor_text_changed(const std::string& value) {
    const Control::Ptr retained = shared_from_this();
    const bool has_text = !value.empty();
    if ((*editor_).accepts_escape.read() != has_text) (*editor_).accepts_escape = has_text;
    if (!has_text) {
        hot_clear_ = false;
        pressed_clear_ = false;
    }
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    on_text_changed();
    if (!is_alive()) return;
    schedule_search();
}

void SearchField::editor_committed(const std::string&) {
    raise_search();
}

void SearchField::editor_cancelled() {
    static_cast<void>(clear());
}

void SearchField::editor_focus_changed(bool focused) {
    editor_focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

void SearchField::on_detached_from_window() noexcept {
    search_timer_.disconnect();
    if (window() != nullptr) (*window()).motion_clock().withdraw_control_ambient(*this);
    busy_phase_ = 0.0;
    editor_focused_ = false;
    hot_clear_ = false;
    pressed_clear_ = false;
}

bool SearchField::busy_moves() const noexcept {
    const Window* const host = window();
    if (host == nullptr || !busy.read()) return false;
    const bool moves = !(*host).motion_clock().ambient_suppressed();
    return moves;
}

void SearchField::update_busy_ambient(bool painting) {
    if (window() == nullptr) return;
    MotionClock& clock = (*window()).motion_clock();
    if (!busy_moves()) {
        clock.withdraw_control_ambient(*this);
        return;
    }
    if (!painting && !clock.control_ambient(*this)) return;
    // A frame per 60 Hz refresh: the segment moves about a pixel a frame
    // across a field of ordinary width.
    const FrameInterval interval = std::chrono::ceil<FrameInterval>(std::chrono::duration<double>(1.0 / 60.0));
    const bool started = clock.enlist_control_ambient(*this, interval);
    if (!started) return;
    // The sweep starts or resumes from the strip's start, with no catch-up
    // for a pause.
    busy_start_ = clock.now();
    busy_phase_ = 0.0;
    busy_paints_seen_ = ~std::uint64_t{0};
}

bool SearchField::on_ambient_frame(FrameTime now) {
    if (busy_paints_ == busy_paints_seen_) return false;
    busy_paints_seen_ = busy_paints_;
    const double elapsed = std::chrono::duration<double>(now - busy_start_).count();
    const double period = std::chrono::duration<double>(busy_sweep_period).count();
    busy_phase_ = std::fmod(std::max(0.0, elapsed) / period, 1.0);
    invalidate(Dirty::paint);
    return true;
}

bool SearchField::focus_visible() const noexcept {
    const Window* const host = window();
    const bool result = editor_focused_ && host != nullptr && (*host).focus_cue_visible();
    return result;
}

// The search mark at the start and the clear mark's room at the end are
// kept whether or not there is text, so the editor does not move as the
// mark comes and goes; the editor fills the space between, centred down
// the well at its natural height.
SearchField::Placement SearchField::placement(Size size) const {
    Placement placed;
    placed.well = Rect{0.0, 0.0, size.width, size.height};
    const double centre = size.height * 0.5;
    const double mark_y = std::floor(centre - mark_side * 0.5);
    placed.icon = Rect{inset, mark_y, mark_side, mark_side};
    const double clear_x = std::max(placed.icon.x + mark_side, size.width - inset - mark_side);
    if (!text.read().empty()) placed.clear = Rect{clear_x, mark_y, mark_side, mark_side};
    const double content_x = placed.icon.x + mark_side + gap;
    const double content_width = std::max(0.0, clear_x - gap - content_x);
    const double content_height = std::min(size.height, editor_height_ > 0.0 ? editor_height_ : size.height);
    placed.content = Rect{content_x, std::floor(centre - content_height * 0.5), content_width, content_height};
    if (busy.read()) {
        const Rect strip{2.0, std::max(0.0, size.height - activity_height - 2.0), std::max(0.0, size.width - 4.0),
                         activity_height};
        placed.activity = strip;
        if (busy_moves()) {
            // The segment runs from the strip's start to its end, then
            // starts again; it is always wholly inside the strip.
            const double segment = strip.width * busy_segment_share;
            const double left = strip.x + (strip.width - segment) * busy_phase_;
            placed.activity = Rect{left, strip.y, segment, activity_height};
        }
    }
    return placed;
}

// The natural size is natural_width wide and tall enough for the editor's
// line; a requested size wins on either axis.
Size SearchField::measure(Size available) {
    const Control::Ptr retained = editor_;
    double editor_height = 0.0;
    if (retained && is_current_layout_child(retained)) {
        const Size desired = (*retained).measure(available);
        if (!is_alive()) return Size{};
        editor_height = desired.height;
    }
    editor_height_ = editor_height;
    double width = natural_width;
    double height = std::max(minimum_height, std::ceil(editor_height) + 4.0);
    const Rect requested = requested_bounds();
    if (requested.width > 0.0) width = requested.width;
    if (requested.height > 0.0) height = requested.height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

// A parent that places the field by its requested bounds never measures
// it, so the editor is measured again here.
void SearchField::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Control::Ptr retained = editor_;
    if (!retained || !is_current_layout_child(retained)) return;
    const Rect client = client_rectangle();
    const Size desired = (*retained).measure(Size{client.width, client.height});
    if (!is_alive()) return;
    editor_height_ = desired.height;
    const Placement placed = placement(Size{client.width, client.height});
    set_child_layout(retained, placed.content);
}

void SearchField::on_paint(Painter& painter, Rect) {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const AnatomyStateSet states = anatomy_states();
    PartPaint well{.role = PartRole::well, .bounds = placed.well, .states = states,
                   .family = ControlVisualRole::editor};
    paint_part(painter, *this, well);
    PartPaint icon{.role = PartRole::glyph, .bounds = placed.icon, .states = states,
                   .family = ControlVisualRole::editor};
    icon.mark = PartMark::search;
    paint_part(painter, *this, icon);
    if (!text.read().empty()) {
        AnatomyStateSet clear_states = states;
        if (hot_clear_) clear_states.insert(AnatomyState::hot);
        if (pressed_clear_) clear_states.insert(AnatomyState::pressed);
        PartPaint clear_mark{.role = PartRole::glyph, .bounds = placed.clear, .states = clear_states,
                             .family = ControlVisualRole::editor};
        clear_mark.mark = PartMark::close;
        paint_part(painter, *this, clear_mark);
    }
    if (busy.read()) {
        ++busy_paints_;
        update_busy_ambient(true);
        PartPaint activity{.role = PartRole::activity, .bounds = placed.activity, .states = states,
                           .family = ControlVisualRole::progress};
        paint_part(painter, *this, activity);
    }
    PartPaint focus{.role = PartRole::focus, .bounds = placed.well, .states = states,
                    .family = ControlVisualRole::editor};
    paint_part(painter, *this, focus);
}

// The clear mark acts on release over it, as a button does, and gives the
// editor the focus; a press elsewhere in the well focuses the editor.
void SearchField::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const bool over = !text.read().empty() && placed.clear.contains(event.position);
    if (event.action == PointerAction::move || event.action == PointerAction::leave) {
        const bool hot = event.action == PointerAction::move && over;
        if (hot != hot_clear_) {
            hot_clear_ = hot;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        Window* const host = window();
        if (host != nullptr && editor_) static_cast<void>((*host).request_focus(editor_));
        if (!is_alive()) return;
        event.handled = true;
        if (over) {
            pressed_clear_ = true;
            set_pointer_capture(true);
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.action == PointerAction::up && pressed_clear_) {
        pressed_clear_ = false;
        set_pointer_capture(false);
        invalidate(Dirty::paint);
        event.handled = true;
        if (over) static_cast<void>(clear());
    }
}

SemanticDescriptor SearchField::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::search_box;
    descriptor.name = accessible_name.read();
    descriptor.description =
        accessible_description.read().empty() ? placeholder_text.read() : accessible_description.read();
    descriptor.value = text.read();
    // The editor shows the value, so its selection and caret index it.
    if (editor_) descriptor.text_selection = (*editor_).semantic_descriptor().text_selection;
    if (busy.read()) descriptor.states |= SemanticState::busy;
    descriptor.actions = {SemanticAction::focus, SemanticAction::set_value};
    descriptor.exposed = true;
    // One node (coordinator ruling): the editor is the field's
    // implementation, not a node of its own, as a MaskedTextBox's is.
    descriptor.include_descendants = false;
    return descriptor;
}

// The text requests of the one published node reach the editor, whose
// text is the published value.
SemanticTextResult SearchField::on_semantic_text_layout(SemanticTextLayout& layout) {
    if (!editor_) return SemanticTextResult::no_text;
    const SemanticTextResult result = (*editor_).on_semantic_text_layout(layout);
    return result;
}

SemanticTextResult SearchField::on_set_semantic_text_selection(SemanticTextSelection selection) {
    if (!editor_) return SemanticTextResult::no_text;
    const SemanticTextResult result = (*editor_).on_set_semantic_text_selection(selection);
    return result;
}

// Focus goes to the editor; a set value is written as the text, which the
// editor validates (std::invalid_argument is reported as not handled).
bool SearchField::on_semantic_action(SemanticAction action, std::string_view value) {
    Window* const host = window();
    if (action == SemanticAction::focus && editor_ && host != nullptr) {
        const bool focused = (*host).request_focus(editor_);
        return focused;
    }
    if (action == SemanticAction::set_value && editor_ && eligible_for_input()) {
        if (!validate_utf8(value).valid()) return false;
        text = std::string(value);
        return true;
    }
    const bool handled = Control::on_semantic_action(action, value);
    return handled;
}

const ControlAnatomy& SearchField::anatomy() const noexcept {
    return search_field_anatomy;
}

AnatomyStateSet SearchField::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (editor_focused_) states.insert(AnatomyState::focused);
    if (focus_visible()) states.insert(AnatomyState::focus_visible);
    if (busy.read()) states.insert(AnatomyState::pending);
    return states;
}

void SearchField::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.well = PartGeometry::from_bounds(placed.well);
    record.icon = PartGeometry::from_bounds(placed.icon);
    record.content = PartGeometry::from_bounds(placed.content);
    record.actuator = PartGeometry::from_bounds(placed.clear);
    if (hot_clear_) record.actuator.states.insert(AnatomyState::hot);
    if (pressed_clear_) record.actuator.states.insert(AnatomyState::pressed);
    record.activity = PartGeometry::from_bounds(placed.activity);
    record.focus = PartGeometry::from_bounds(placed.well);
    write_parts(anatomy(), search_field_part_fields, record, geometry);
}

SearchField::Parts SearchField::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), search_field_part_fields, published.parts(), record);
    return record;
}

void SearchField::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), search_field_interactions.begin(), search_field_interactions.end());
}

// The text and placeholder views: the inner editor owns both (see
// TextBox), and validates a write. Before the editor is made they read
// empty and a write is refused.
void SearchField::write_view(const decltype(text)&, std::string incoming) {
    require_mutable();
    if (!editor_) throw std::logic_error("SearchField has no editor yet");
    (*editor_).text = std::move(incoming);
}

std::string SearchField::read_view(const decltype(text)&) const {
    if (!editor_) return std::string{};
    std::string value = (*editor_).text.read();
    return value;
}

void SearchField::write_view(const decltype(placeholder_text)&, std::string incoming) {
    require_mutable();
    if (!editor_) throw std::logic_error("SearchField has no editor yet");
    (*editor_).placeholder_text = std::move(incoming);
    invalidate(Dirty::semantics);
}

std::string SearchField::read_view(const decltype(placeholder_text)&) const {
    if (!editor_) return std::string{};
    std::string value = (*editor_).placeholder_text.read();
    return value;
}

void SearchField::prepare_write(const decltype(busy)&, bool&) {
    require_mutable();
}

// Turning busy on shows the strip, whose next paint starts the sweep;
// turning it off withdraws the field from the motion clock.
void SearchField::written(const decltype(busy)&) {
    invalidate(Dirty::paint | Dirty::semantics);
    if (busy.read()) return;
    busy_phase_ = 0.0;
    update_busy_ambient(false);
}

// property_codemod begin SearchField (tools/property_families/search_field.json; generated, do not edit)

namespace {

PropertyDescriptor search_field_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The query text.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_search_field_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        search_field_text_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor search_field_placeholder_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "placeholder_text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The hint the empty field shows.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_search_field_placeholder_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        search_field_placeholder_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor search_field_busy_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "busy";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether a search is running.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_search_field_busy() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        search_field_busy_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, SearchField::search_field_property_count>& SearchField::search_field_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"text", &describe_search_field_text,
         &read_cell_entry<SearchField, decltype(&SearchField::text), &SearchField::text>,
         &write_cell_entry<SearchField, decltype(&SearchField::text), &SearchField::text>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<SearchField, decltype(&SearchField::text_changed_), &SearchField::text_changed_>},
        {"placeholder_text", &describe_search_field_placeholder_text,
         &read_cell_entry<SearchField, decltype(&SearchField::placeholder_text), &SearchField::placeholder_text>,
         &write_cell_entry<SearchField, decltype(&SearchField::placeholder_text), &SearchField::placeholder_text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"busy", &describe_search_field_busy,
         &read_cell_entry<SearchField, decltype(&SearchField::busy), &SearchField::busy>,
         &write_cell_entry<SearchField, decltype(&SearchField::busy), &SearchField::busy>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "SearchField's property table names a property twice");
    static const std::array<PropertyEntry, search_field_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> SearchField::property_table() const noexcept {
    const std::array<PropertyEntry, search_field_property_count>& entries = search_field_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void SearchField::prepare_write(const decltype(search_delay)&, std::chrono::milliseconds& incoming) {
    require_mutable();
    if (incoming.count() < 0 || incoming > std::chrono::milliseconds(10000)) {
        throw std::invalid_argument("SearchField search delay must be between 0 ms and 10 s");
    }
}

void SearchField::written(const decltype(search_delay)&) {}

void SearchField::on_text_changed() {
    publish_change(text_changed_, text.read());
}

// property_codemod end SearchField

} // namespace gui_forms
