#include "gui_forms/controls/button_text_box.hpp"

#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <stxlib/result.hpp>
#include <stxlib/unique.hpp>

namespace gui_forms {

// Where everything sits for a size: the well, the editor's slot, and
// each button's face (empty for a hidden one), by button position.
struct ButtonTextBox::Placement final {
    Rect well{};
    Rect content{};
    std::vector<Rect> faces{};
};

struct ButtonTextBox::State final {
    std::shared_ptr<TextBox> editor_{};
    Event<const std::string&> text_changed_;
    // Paint scratch.
    std::string paint_text_{};
    // The visible buttons' positions, leading ones first: what the item
    // system and the semantic children list.
    [[nodiscard]] static std::vector<std::size_t> visible_order(const ButtonTextBox& self);
};

stxlib::Unique<ButtonTextBox::State> ButtonTextBox::make_state() {
    stxlib::OperationResult<stxlib::Unique<State>> made = stxlib::Unique<State>::make();
    if (made.failed()) throw std::bad_alloc();
    stxlib::Unique<State> state = std::move(made).take_value();
    return state;
}

namespace {

// The field's metrics, in logical units: the well's frame (the borderless
// editor sits inside it, as SearchField's does), the space between the
// frame and a button face, the space before the text at an end without a
// button, the gap between a face and the text, a caption's padding on
// each side, the least height and the natural width.
constexpr double well_frame = 1.0;
constexpr double face_inset = 2.0;
constexpr double text_inset = 4.0;
constexpr double gap = 2.0;
constexpr double caption_padding = 6.0;
constexpr double mark_side = 12.0;
constexpr double minimum_height = 26.0;
constexpr double natural_width = 220.0;

constexpr std::array<PartField<ButtonTextBox::Parts>, 4> button_text_box_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}, .states = {}},
     .member = &ButtonTextBox::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}, .states = {}},
     .member = &ButtonTextBox::Parts::well},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = "text_box", .states = {}},
     .member = &ButtonTextBox::Parts::content},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
     .member = &ButtonTextBox::Parts::focus},
}};

constexpr std::array<PartField<ButtonTextBox::ButtonParts>, 3> button_text_box_button_part_fields{{
    {.declaration = {.name = "button", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &ButtonTextBox::ButtonParts::button},
    {.declaration = {.name = "button_glyph", .role = PartRole::glyph, .composed_from = {}, .states = {}},
     .member = &ButtonTextBox::ButtonParts::button_glyph},
    {.declaration = {.name = "button_label", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &ButtonTextBox::ButtonParts::button_label},
}};

constexpr std::array<StateField<ButtonTextBox::ButtonStates>, 3> button_text_box_button_state_fields{{
    {.state = AnatomyState::hot, .member = &ButtonTextBox::ButtonStates::hot},
    {.state = AnatomyState::pressed, .member = &ButtonTextBox::ButtonStates::pressed},
    {.state = AnatomyState::disabled, .member = &ButtonTextBox::ButtonStates::disabled},
}};

constexpr std::array<PartDeclaration, 4> button_text_box_parts = part_declarations(button_text_box_part_fields);
constexpr std::array<PartDeclaration, 3> button_text_box_button_parts =
    part_declarations(button_text_box_button_part_fields);
constexpr std::array<AnatomyState, 3> button_text_box_button_states =
    state_declarations(button_text_box_button_state_fields);
constexpr std::array<AnatomyState, 5> button_text_box_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ItemSystemIndex button_system{0U};

constexpr std::array<ItemSystemDeclaration, 1> button_text_box_item_systems{{
    {.name = "button", .parts = button_text_box_button_parts, .states = button_text_box_button_states,
     .custom_states = {}},
}};

constexpr ControlAnatomy button_text_box_anatomy{
    .control_id = "button_text_box",
    .parts = button_text_box_parts,
    .states = button_text_box_states,
    .custom_states = {},
    .item_systems = button_text_box_item_systems,
};

// The events ButtonTextBox adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> button_text_box_interactions{{
    {.name = "text_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "button_clicked", .payload = "EditButtonClick", .mutable_payload = false},
}};

PartMark kind_mark(EditButtonKind kind) noexcept {
    switch (kind) {
    case EditButtonKind::ellipsis: return PartMark::more;
    case EditButtonKind::drop_down: return PartMark::chevron_down;
    case EditButtonKind::close: return PartMark::close;
    case EditButtonKind::search: return PartMark::search;
    case EditButtonKind::caption: return PartMark::none;
    }
    return PartMark::none;
}

// The name a mark button without a caption is published by.
std::string_view kind_name(EditButtonKind kind) noexcept {
    switch (kind) {
    case EditButtonKind::ellipsis: return "More";
    case EditButtonKind::drop_down: return "Open";
    case EditButtonKind::close: return "Clear";
    case EditButtonKind::search: return "Search";
    case EditButtonKind::caption: return "";
    }
    return "";
}

// Mirrors a rectangle about the middle of a width (a right-to-left field);
// an empty one stays empty.
void mirror_across(Rect& rect, double width) noexcept {
    if (!rect.empty()) rect.x = width - rect.x - rect.width;
}

void validate_buttons(const std::vector<EditButton>& buttons) {
    if (buttons.size() > ButtonTextBox::maximum_buttons) {
        throw std::invalid_argument("ButtonTextBox takes at most maximum_buttons buttons");
    }
    std::unordered_set<std::string_view> seen;
    for (const EditButton& button : buttons) {
        if (!valid_stable_id(button.stable_id)) throw std::invalid_argument("ButtonTextBox button ID is not valid");
        if (!seen.insert(button.stable_id).second) throw std::invalid_argument("ButtonTextBox button ID repeats");
        if (!validate_utf8(button.caption).valid() || !validate_utf8(button.description).valid()) {
            throw std::invalid_argument("ButtonTextBox button text is not valid UTF-8");
        }
    }
}

} // namespace

ButtonTextBox::ButtonTextBox(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::control);
    focusable = false;
    cursor = stxlib::Optional<CursorKind>::of(CursorKind::text);
}

std::shared_ptr<TextBox> ButtonTextBox::editor() const noexcept {
    return (*state_).editor_;
}

Event<const std::string&>& ButtonTextBox::text_changed() noexcept {
    return (*state_).text_changed_;
}

ButtonTextBox::~ButtonTextBox() {
    editor_text_.disconnect();
    editor_focus_.disconnect();
}

// The editor draws no border of its own (the field's well is the ground).
void ButtonTextBox::initialize_control_tree() {
    State& state = *state_;
    if (state.editor_) return;
    std::string id(stable_id().value());
    id += ".editor";
    state.editor_ = make_control<TextBox>(StableId(id));
    (*state.editor_).border_style = BorderStyle::none;
    add_child(state.editor_);
    editor_text_ = (*state.editor_).text_changed().subscribe(
        *this, Delegate<const std::string&>::bind<ButtonTextBox, &ButtonTextBox::editor_text_changed>(*this));
    editor_focus_ = (*state.editor_).focus_observed().subscribe(
        *this, Delegate<bool>::bind<ButtonTextBox, &ButtonTextBox::editor_focus_changed>(*this));
}

Control::Ptr ButtonTextBox::focus_target() const { return (*state_).editor_; }

// -- buttons ----------------------------------------------------------------------------------------

void ButtonTextBox::set_buttons(std::vector<EditButton> buttons) {
    require_mutable();
    validate_buttons(buttons);
    buttons_ = std::move(buttons);
    hot_button_.reset();
    if (pressed_button_.present()) {
        pressed_button_.reset();
        set_pointer_capture(false);
    }
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void ButtonTextBox::set_button_enabled(std::string_view button_id, bool enabled) {
    require_mutable();
    const stxlib::Optional<std::size_t> button = index_of(button_id);
    if (button.absent()) throw std::out_of_range("ButtonTextBox has no button with this ID");
    EditButton& record = buttons_[*button.value()];
    if (record.enabled == enabled) return;
    record.enabled = enabled;
    if (!enabled && pressed_button_ == button) {
        pressed_button_.reset();
        set_pointer_capture(false);
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void ButtonTextBox::set_button_visible(std::string_view button_id, bool visible) {
    require_mutable();
    const stxlib::Optional<std::size_t> button = index_of(button_id);
    if (button.absent()) throw std::out_of_range("ButtonTextBox has no button with this ID");
    EditButton& record = buttons_[*button.value()];
    if (record.visible == visible) return;
    record.visible = visible;
    if (!visible && hot_button_ == button) hot_button_.reset();
    if (!visible && pressed_button_ == button) {
        pressed_button_.reset();
        set_pointer_capture(false);
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

stxlib::Optional<std::size_t> ButtonTextBox::index_of(std::string_view button_id) const {
    for (std::size_t button = 0; button < buttons_.size(); ++button) {
        if (buttons_[button].stable_id == button_id) return stxlib::Optional<std::size_t>::of(button);
    }
    return {};
}

stxlib::Optional<std::size_t> ButtonTextBox::button_at(Point local) const {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    for (std::size_t button = 0; button < buttons_.size(); ++button) {
        if (!placed.faces[button].empty() && placed.faces[button].contains(local)) {
            return stxlib::Optional<std::size_t>::of(button);
        }
    }
    return {};
}

bool ButtonTextBox::perform_button_click(std::string_view button_id) {
    require_mutable();
    const stxlib::Optional<std::size_t> button = index_of(button_id);
    if (button.absent()) throw std::out_of_range("ButtonTextBox has no button with this ID");
    if (!button_acts(*button.value())) return false;
    raise_click(*button.value());
    return true;
}

bool ButtonTextBox::button_acts(std::size_t button) const noexcept {
    const bool acts = button < buttons_.size() && buttons_[button].visible && buttons_[button].enabled &&
                      effectively_enabled();
    return acts;
}

stxlib::Optional<std::size_t> ButtonTextBox::default_button() const {
    for (std::size_t button = 0; button < buttons_.size(); ++button) {
        if (buttons_[button].placement == EditButtonPlacement::trailing && button_acts(button)) {
            return stxlib::Optional<std::size_t>::of(button);
        }
    }
    for (std::size_t button = 0; button < buttons_.size(); ++button) {
        if (button_acts(button)) return stxlib::Optional<std::size_t>::of(button);
    }
    return {};
}

void ButtonTextBox::raise_click(std::size_t button) {
    EditButtonClick click{buttons_[button].stable_id};
    publish_change(button_clicked_, click);
}

std::vector<std::size_t> ButtonTextBox::State::visible_order(const ButtonTextBox& self) {
    std::vector<std::size_t> order;
    order.reserve(self.buttons_.size());
    for (const EditButtonPlacement end : {EditButtonPlacement::leading, EditButtonPlacement::trailing}) {
        for (std::size_t button = 0; button < self.buttons_.size(); ++button) {
            if (self.buttons_[button].visible && self.buttons_[button].placement == end) order.push_back(button);
        }
    }
    return order;
}

namespace {

std::string_view button_name(const EditButton& button) noexcept {
    if (!button.caption.empty()) return button.caption;
    return kind_name(button.kind);
}

} // namespace

// -- editor -----------------------------------------------------------------------------------------

void ButtonTextBox::editor_text_changed(const std::string&) {
    const Control::Ptr retained = shared_from_this();
    invalidate(Dirty::paint | Dirty::semantics);
    on_text_changed();
}

void ButtonTextBox::editor_focus_changed(bool focused) {
    editor_focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

void ButtonTextBox::on_detached_from_window() noexcept {
    editor_focused_ = false;
    hot_button_.reset();
    pressed_button_.reset();
}

bool ButtonTextBox::focus_visible() const noexcept {
    const Window* const host = window();
    const bool result = editor_focused_ && host != nullptr && (*host).focus_cue_visible();
    return result;
}

// -- layout -----------------------------------------------------------------------------------------

// A mark button's face is square; a caption button's is its caption's
// width with padding, and at least square.
double ButtonTextBox::face_width(const EditButton& button) const {
    const Rect client = client_rectangle();
    const double side = std::max(0.0, client.height - (well_frame + face_inset) * 2.0);
    if (button.kind != EditButtonKind::caption) return side;
    const FontSpec font = (*state_).editor_ ? text_font((*(*state_).editor_).font.read()) : effective_font(FontSpec{});
    const ResolvedTextLayout metrics = resolve_text_layout_utf8(button.caption, font);
    const double width = std::max(side, std::ceil(metrics.logical_size.width) + caption_padding * 2.0);
    return width;
}

// The leading buttons sit from the start edge in and the trailing ones from
// the text out, each face inset in the well; the editor fills what is left,
// centred down the well at its natural height. Right to left the rectangle
// is mirrored.
ButtonTextBox::Placement ButtonTextBox::placement(Size size) const {
    Placement placed;
    placed.well = Rect{0.0, 0.0, size.width, size.height};
    placed.faces.assign(buttons_.size(), Rect{});
    const double face_y = well_frame + face_inset;
    const double face_height = std::max(0.0, size.height - face_y * 2.0);
    double start = well_frame;
    bool leading_any = false;
    for (std::size_t button = 0; button < buttons_.size(); ++button) {
        const EditButton& record = buttons_[button];
        if (!record.visible || record.placement != EditButtonPlacement::leading) continue;
        const double width = face_width(record);
        placed.faces[button] = Rect{start + face_inset, face_y, width, face_height};
        start += face_inset + width;
        leading_any = true;
    }
    double trailing_width = 0.0;
    bool trailing_any = false;
    for (const EditButton& record : buttons_) {
        if (!record.visible || record.placement != EditButtonPlacement::trailing) continue;
        trailing_width += face_inset + face_width(record);
        trailing_any = true;
    }
    double end = std::max(start, size.width - well_frame - trailing_width);
    double cursor_x = end;
    for (std::size_t button = 0; button < buttons_.size(); ++button) {
        const EditButton& record = buttons_[button];
        if (!record.visible || record.placement != EditButtonPlacement::trailing) continue;
        const double width = face_width(record);
        placed.faces[button] = Rect{cursor_x, face_y, width, face_height};
        cursor_x += width + face_inset;
    }
    const double content_x = start + (leading_any ? gap : text_inset);
    const double content_end = end - (trailing_any ? gap : text_inset);
    const double inner_height = std::max(0.0, size.height - well_frame * 2.0);
    const double content_height = std::min(inner_height, editor_height_ > 0.0 ? editor_height_ : inner_height);
    const double content_y = std::max(well_frame, std::floor(size.height * 0.5 - content_height * 0.5));
    placed.content = Rect{content_x, content_y, std::max(0.0, content_end - content_x), content_height};
    if (effective_layout_direction() == LayoutDirection::right_to_left) {
        mirror_across(placed.content, size.width);
        for (Rect& face : placed.faces) mirror_across(face, size.width);
    }
    return placed;
}

// The natural size is natural_width wide and tall enough for the editor's
// line; a requested size wins on either axis.
Size ButtonTextBox::measure(Size available) {
    const Control::Ptr retained = (*state_).editor_;
    double editor_height = 0.0;
    if (retained && is_current_layout_child(retained)) {
        const Size desired = (*retained).measure(available);
        if (!is_alive()) return Size{};
        editor_height = desired.height;
    }
    editor_height_ = editor_height;
    double width = natural_width;
    double height = std::max(minimum_height, std::ceil(editor_height) + 6.0);
    const Rect requested = requested_bounds();
    if (requested.width > 0.0) width = requested.width;
    if (requested.height > 0.0) height = requested.height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

// A parent that places the field by its requested bounds never measures
// it, so the editor is measured again here.
void ButtonTextBox::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Control::Ptr retained = (*state_).editor_;
    if (!retained || !is_current_layout_child(retained)) return;
    const Rect client = client_rectangle();
    const Size desired = (*retained).measure(Size{client.width, client.height});
    if (!is_alive()) return;
    editor_height_ = desired.height;
    const Placement placed = placement(Size{client.width, client.height});
    set_child_layout(retained, placed.content);
}

ButtonTextBox::ButtonParts ButtonTextBox::button_parts(const Placement& placed, std::size_t button) const {
    ButtonParts parts;
    const Rect face = placed.faces[button];
    parts.button = PartGeometry::from_bounds(face);
    if (buttons_[button].kind == EditButtonKind::caption) {
        parts.button_label = PartGeometry::from_bounds(
            Rect{face.x + caption_padding, face.y, std::max(0.0, face.width - caption_padding * 2.0), face.height});
    } else {
        const double side = std::min({mark_side, face.width, face.height});
        parts.button_glyph = PartGeometry::from_bounds(Rect{std::floor(face.x + (face.width - side) * 0.5),
                                                            std::floor(face.y + (face.height - side) * 0.5), side,
                                                            side});
    }
    const AnatomyStateSet states = button_states(button);
    parts.button.states = states;
    if (parts.button_glyph.present) parts.button_glyph.states = states;
    if (parts.button_label.present) parts.button_label.states = states;
    return parts;
}

AnatomyStateSet ButtonTextBox::button_states(std::size_t button) const {
    AnatomyStateSet states;
    if (!buttons_[button].enabled || !effectively_enabled()) {
        states.insert(AnatomyState::disabled);
        return states;
    }
    if (hot_button_ == stxlib::Optional<std::size_t>::of(button)) states.insert(AnatomyState::hot);
    if (pressed_button_ == stxlib::Optional<std::size_t>::of(button)) states.insert(AnatomyState::pressed);
    return states;
}

// -- painting ---------------------------------------------------------------------------------------

// The well, then each button as a flat face in the editor family (drawn
// only when hot or pressed by today's recipe) with its mark or caption,
// then the focus cue.
void ButtonTextBox::on_paint(Painter& painter, Rect) {
    State& state = *state_;
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const AnatomyStateSet states = anatomy_states();
    PartPaint well{.role = PartRole::well, .bounds = placed.well, .states = states,
                   .family = ControlVisualRole::editor};
    paint_part(painter, *this, well);
    for (const std::size_t button : State::visible_order(*this)) {
        const ButtonParts parts = button_parts(placed, button);
        PartPaint face{.role = PartRole::face, .bounds = parts.button.bounds, .states = parts.button.states,
                       .family = ControlVisualRole::button};
        face.frame = PartFrame::none;
        paint_part(painter, *this, face);
        if (parts.button_glyph.present) {
            PartPaint glyph{.role = PartRole::glyph, .bounds = parts.button_glyph.bounds,
                            .states = parts.button_glyph.states, .family = ControlVisualRole::button};
            glyph.mark = kind_mark(buttons_[button].kind);
            paint_part(painter, *this, glyph);
        }
        if (parts.button_label.present) {
            PartPaint label{.role = PartRole::label, .bounds = parts.button_label.bounds,
                            .states = parts.button_label.states, .family = ControlVisualRole::button};
            label.alignment = PartTextAlignment::center;
            label.font = state.editor_ ? text_font((*state.editor_).font.read()) : effective_font(FontSpec{});
            state.paint_text_ = buttons_[button].caption;
            label.text = lend_paint_text(state.paint_text_);
            paint_part(painter, *this, label);
        }
    }
    PartPaint focus{.role = PartRole::focus, .bounds = placed.well, .states = states,
                    .family = ControlVisualRole::editor};
    paint_part(painter, *this, focus);
}

// -- input ------------------------------------------------------------------------------------------

stxlib::Optional<std::size_t> ButtonTextBox::button_at_window(Point window_position) const {
    const Rect absolute = absolute_bounds();
    const Point local{window_position.x - absolute.x, window_position.y - absolute.y};
    return button_at(local);
}

void ButtonTextBox::set_hot_button(stxlib::Optional<std::size_t> button) {
    if (button == hot_button_) return;
    hot_button_ = button;
    invalidate(Dirty::paint);
}

void ButtonTextBox::on_mouse_leave() {
    set_hot_button({});
    Control::on_mouse_leave();
}

void ButtonTextBox::on_mouse_move(MouseEventArgs& arguments) {
    if (eligible_for_input()) set_hot_button(button_at_window(arguments.window_position));
    Control::on_mouse_move(arguments);
}

// A press on an enabled button presses it; a press anywhere gives the
// editor the focus.
void ButtonTextBox::on_mouse_down(MouseEventArgs& arguments) {
    if (eligible_for_input() && arguments.button == PointerButton::primary) {
        const stxlib::Optional<std::size_t> over = button_at_window(arguments.window_position);
        Window* const host = window();
        if (host != nullptr && (*state_).editor_) static_cast<void>((*host).request_focus((*state_).editor_));
        if (!is_alive()) return;
        arguments.handled = true;
        if (over.present() && button_acts(*over.value())) {
            pressed_button_ = over;
            set_pointer_capture(true);
            invalidate(Dirty::paint);
        }
    }
    Control::on_mouse_down(arguments);
}

void ButtonTextBox::on_mouse_up(MouseEventArgs& arguments) {
    if (eligible_for_input() && arguments.button == PointerButton::primary && pressed_button_.present()) {
        const std::size_t pressed = *pressed_button_.value();
        pressed_button_.reset();
        set_pointer_capture(false);
        invalidate(Dirty::paint);
        arguments.handled = true;
        const stxlib::Optional<std::size_t> over = button_at_window(arguments.window_position);
        if (over == stxlib::Optional<std::size_t>::of(pressed) && button_acts(pressed)) raise_click(pressed);
        if (!is_alive()) return;
    }
    Control::on_mouse_up(arguments);
}

// The window ends a primary press's capture after its release, when the
// press has already ended; any other loss forgets it.
void ButtonTextBox::on_mouse_capture_changed() {
    Control::on_mouse_capture_changed();
    if (pressed_button_.absent()) return;
    pressed_button_.reset();
    invalidate(Dirty::paint);
}

void ButtonTextBox::on_key_bubble(KeyEvent& event) {
    Control::on_key_bubble(event);
    if (event.handled || event.action != KeyAction::down) return;
    const bool f4 = event.physical_key == PhysicalKey::f4 && event.modifiers == Modifier::none;
    const bool alt_down = event.physical_key == PhysicalKey::down && event.modifiers == Modifier::alt;
    if (!f4 && !alt_down) return;
    const stxlib::Optional<std::size_t> button = default_button();
    if (button.absent()) return;
    event.handled = true;
    raise_click(*button.value());
}

// -- semantics --------------------------------------------------------------------------------------

SemanticDescriptor ButtonTextBox::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::text_box;
    descriptor.name = accessible_name.read();
    descriptor.description =
        accessible_description.read().empty() ? placeholder_text.read() : accessible_description.read();
    descriptor.value = text.read();
    if ((*state_).editor_) descriptor.text_selection = (*(*state_).editor_).semantic_descriptor().text_selection;
    if (read_only.read()) descriptor.states |= SemanticState::read_only;
    descriptor.actions = {SemanticAction::focus, SemanticAction::set_value};
    descriptor.exposed = true;
    // One node for the field (the editor is its implementation, as
    // SearchField's is); the buttons are its virtual children.
    descriptor.include_descendants = false;
    return descriptor;
}

std::vector<SemanticNode> ButtonTextBox::semantic_virtual_children() const {
    std::vector<SemanticNode> children{};
    const std::vector<std::size_t> order = State::visible_order(*this);
    children.reserve(order.size());
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const Rect absolute = absolute_bounds();
    for (std::size_t position = 0; position < order.size(); ++position) {
        const std::size_t button = order[position];
        const EditButton& record = buttons_[button];
        const Rect face = placed.faces[button];
        SemanticNode node{};
        node.stable_id = record.stable_id;
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::button;
        node.name = std::string(button_name(record));
        node.description = record.description;
        node.has_popup = record.kind == EditButtonKind::drop_down || record.kind == EditButtonKind::ellipsis;
        node.bounds = {absolute.x + face.x, absolute.y + face.y, face.width, face.height};
        node.states = SemanticState::visible;
        if (button_acts(button)) node.states |= SemanticState::enabled;
        node.actions = {SemanticAction::press};
        children.push_back(std::move(node));
    }
    return children;
}

bool ButtonTextBox::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    if (action != SemanticAction::press) return false;
    const stxlib::Optional<std::size_t> button = index_of(id);
    if (button.absent() || !button_acts(*button.value())) return false;
    raise_click(*button.value());
    return true;
}

// The text requests of the field's node reach the editor, whose text is
// the published value.
SemanticTextResult ButtonTextBox::on_semantic_text_layout(SemanticTextLayout& layout) {
    if (!(*state_).editor_) return SemanticTextResult::no_text;
    const SemanticTextResult result = (*(*state_).editor_).on_semantic_text_layout(layout);
    return result;
}

SemanticTextResult ButtonTextBox::on_set_semantic_text_selection(SemanticTextSelection selection) {
    if (!(*state_).editor_) return SemanticTextResult::no_text;
    const SemanticTextResult result = (*(*state_).editor_).on_set_semantic_text_selection(selection);
    return result;
}

// Focus goes to the editor; a set value is written as the text, which the
// editor validates, and is refused while read only.
bool ButtonTextBox::on_semantic_action(SemanticAction action, std::string_view value) {
    State& state = *state_;
    Window* const host = window();
    if (action == SemanticAction::focus && state.editor_ && host != nullptr) {
        const bool focused = (*host).request_focus(state.editor_);
        return focused;
    }
    if (action == SemanticAction::set_value && state.editor_ && eligible_for_input() && !read_only.read()) {
        if (!validate_utf8(value).valid()) return false;
        text = std::string(value);
        return true;
    }
    const bool handled = Control::on_semantic_action(action, value);
    return handled;
}

// -- anatomy ----------------------------------------------------------------------------------------

const ControlAnatomy& ButtonTextBox::anatomy() const noexcept {
    return button_text_box_anatomy;
}

AnatomyStateSet ButtonTextBox::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (editor_focused_) states.insert(AnatomyState::focused);
    if (focus_visible()) states.insert(AnatomyState::focus_visible);
    return states;
}

void ButtonTextBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.well = PartGeometry::from_bounds(placed.well);
    record.content = PartGeometry::from_bounds(placed.content);
    record.focus = PartGeometry::from_bounds(placed.well);
    write_parts(anatomy(), button_text_box_part_fields, record, geometry);
}

ButtonTextBox::Parts ButtonTextBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), button_text_box_part_fields, published.parts(), record);
    return record;
}

std::size_t ButtonTextBox::realized_item_count() const {
    return State::visible_order(*this).size();
}

std::string ButtonTextBox::realized_item_stable_id(std::size_t item) const {
    const std::vector<std::size_t> order = State::visible_order(*this);
    if (item >= order.size()) throw std::out_of_range("ButtonTextBox realized item is past the visible buttons");
    return buttons_[order[item]].stable_id;
}

AnatomyStateSet ButtonTextBox::realized_item_anatomy_states(std::size_t item) const {
    const std::vector<std::size_t> order = State::visible_order(*this);
    if (item >= order.size()) throw std::out_of_range("ButtonTextBox realized item is past the visible buttons");
    return button_states(order[item]);
}

ItemSystemIndex ButtonTextBox::publish_realized_item_system(std::size_t item) const {
    if (item >= State::visible_order(*this).size()) {
        throw std::out_of_range("ButtonTextBox realized item is past the visible buttons");
    }
    return button_system;
}

void ButtonTextBox::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const std::vector<std::size_t> order = State::visible_order(*this);
    if (item >= order.size()) throw std::out_of_range("ButtonTextBox realized item is past the visible buttons");
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const ButtonParts record = button_parts(placed, order[item]);
    write_item_parts(anatomy().item_systems[button_system.value], button_text_box_button_part_fields, record,
                     geometry);
}

std::vector<ButtonTextBox::RealizedButton> ButtonTextBox::realized_buttons() const {
    const std::vector<std::size_t> order = State::visible_order(*this);
    std::vector<RealizedButton> records;
    records.reserve(order.size());
    for (std::size_t item = 0; item < order.size(); ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        RealizedButton record;
        record.index = order[item];
        record.stable_id = buttons_[order[item]].stable_id;
        read_item_parts(anatomy().item_systems[button_system.value], button_text_box_button_part_fields,
                        published.parts(), record.parts);
        read_states(button_text_box_button_state_fields, button_states(order[item]), record.states);
        records.push_back(record);
    }
    return records;
}

void ButtonTextBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), button_text_box_interactions.begin(), button_text_box_interactions.end());
}

// -- properties -------------------------------------------------------------------------------------

// The text, placeholder and read-only views: the inner editor owns them
// (see TextBox) and validates a write. Before the editor is made they read
// empty and a write is refused.
void ButtonTextBox::write_view(const decltype(text)&, std::string incoming) {
    require_mutable();
    if (!(*state_).editor_) throw std::logic_error("ButtonTextBox has no editor yet");
    (*(*state_).editor_).text = std::move(incoming);
}

std::string ButtonTextBox::read_view(const decltype(text)&) const {
    if (!(*state_).editor_) return std::string{};
    std::string value = (*(*state_).editor_).text.read();
    return value;
}

void ButtonTextBox::write_view(const decltype(placeholder_text)&, std::string incoming) {
    require_mutable();
    if (!(*state_).editor_) throw std::logic_error("ButtonTextBox has no editor yet");
    (*(*state_).editor_).placeholder_text = std::move(incoming);
    invalidate(Dirty::semantics);
}

std::string ButtonTextBox::read_view(const decltype(placeholder_text)&) const {
    if (!(*state_).editor_) return std::string{};
    std::string value = (*(*state_).editor_).placeholder_text.read();
    return value;
}

void ButtonTextBox::write_view(const decltype(read_only)&, bool incoming) {
    require_mutable();
    if (!(*state_).editor_) throw std::logic_error("ButtonTextBox has no editor yet");
    (*(*state_).editor_).read_only = incoming;
    invalidate(Dirty::paint | Dirty::semantics);
}

bool ButtonTextBox::read_view(const decltype(read_only)&) const {
    if (!(*state_).editor_) return false;
    const bool value = (*(*state_).editor_).read_only.read();
    return value;
}

// property_codemod begin ButtonTextBox (tools/property_families/button_text_box.json; generated, do not edit)

namespace {

PropertyDescriptor button_text_box_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The field's text.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_button_text_box_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        button_text_box_text_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor button_text_box_placeholder_text_descriptor() {
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

const PropertyDescriptor& describe_button_text_box_placeholder_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        button_text_box_placeholder_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor button_text_box_read_only_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "read_only";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Behavior";
    descriptor.description = "Whether the user may select and copy the text but not change it.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_button_text_box_read_only() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        button_text_box_read_only_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

namespace {

// The table's text connector: text_changed_ lives in State, so the entry
// reaches it through the public accessor rather than a member pointer.
SubscriptionToken connect_button_text_box_text_changed(Component& owner, Component& subscriber, std::function<void()> changed) {
    ButtonTextBox& box = static_cast<ButtonTextBox&>(owner);
    const detail::EventChangeConnector connector(box.text_changed());
    SubscriptionToken token = connector(subscriber, std::move(changed));
    return token;
}

} // namespace

const std::array<PropertyEntry, ButtonTextBox::button_text_box_property_count>& ButtonTextBox::button_text_box_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"text", &describe_button_text_box_text,
         &read_cell_entry<ButtonTextBox, decltype(&ButtonTextBox::text), &ButtonTextBox::text>,
         &write_cell_entry<ButtonTextBox, decltype(&ButtonTextBox::text), &ButtonTextBox::text>,
         nullptr, nullptr, nullptr,
         &connect_button_text_box_text_changed},
        {"placeholder_text", &describe_button_text_box_placeholder_text,
         &read_cell_entry<ButtonTextBox, decltype(&ButtonTextBox::placeholder_text), &ButtonTextBox::placeholder_text>,
         &write_cell_entry<ButtonTextBox, decltype(&ButtonTextBox::placeholder_text), &ButtonTextBox::placeholder_text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"read_only", &describe_button_text_box_read_only,
         &read_cell_entry<ButtonTextBox, decltype(&ButtonTextBox::read_only), &ButtonTextBox::read_only>,
         &write_cell_entry<ButtonTextBox, decltype(&ButtonTextBox::read_only), &ButtonTextBox::read_only>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ButtonTextBox's property table names a property twice");
    static const std::array<PropertyEntry, button_text_box_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ButtonTextBox::property_table() const noexcept {
    const std::array<PropertyEntry, button_text_box_property_count>& entries = button_text_box_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void ButtonTextBox::on_text_changed() {
    publish_change((*state_).text_changed_, text.read());
}

// property_codemod end ButtonTextBox

} // namespace gui_forms
