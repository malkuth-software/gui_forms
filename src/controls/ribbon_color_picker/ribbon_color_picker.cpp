#include "gui_forms/controls/ribbon_color_picker.hpp"

#include "ribbon_color_palette.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <optional>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// Picker geometry in logical units at text scale 1: a 22-unit face holding a
// 16-unit icon over a 4-unit colour bar, and a 12-unit arrow zone.
constexpr double face_extent = 22.0;
constexpr double arrow_width = 12.0;
constexpr double arrow_glyph = 7.0;
constexpr double icon_inset = 3.0;
constexpr double icon_side = 16.0;
constexpr double bar_height = 4.0;
// Palette geometry: 16-unit cells two apart, inside a 4-unit margin, under a
// 22-unit Automatic row; the recent colours a row below the palette.
constexpr double cell_side = 16.0;
constexpr double cell_gap = 2.0;
constexpr double palette_margin = 4.0;
constexpr double automatic_extent = 22.0;
constexpr double section_gap = 6.0;
constexpr double sample_inset = 2.0;

// The ten standard colours (dark red, red, orange, yellow, light green,
// green, light blue, blue, dark blue, purple), Office's row.
std::vector<Color> standard_colors() {
    std::vector<Color> colors{
        Color::rgba(192, 0, 0),   Color::rgba(255, 0, 0),   Color::rgba(255, 192, 0), Color::rgba(255, 255, 0),
        Color::rgba(146, 208, 80), Color::rgba(0, 176, 80),  Color::rgba(0, 176, 240), Color::rgba(0, 112, 192),
        Color::rgba(0, 32, 96),   Color::rgba(112, 48, 160),
    };
    return colors;
}

Rect centred_square(Rect area, double side) noexcept {
    if (area.width < side || area.height < side) return {};
    const Rect square{std::floor(area.x + (area.width - side) * 0.5),
                      std::floor(area.y + (area.height - side) * 0.5), side, side};
    return square;
}

// A colour sample sits one unit inside its well, so that the well frames it.
constexpr double sample_frame = 1.0;

// The fill inside `well`, `frame` units in from each side.
Rect inset_by(Rect well, double frame) noexcept {
    if (well.width <= frame * 2.0 || well.height <= frame * 2.0) return {};
    const Rect inner{well.x + frame, well.y + frame, well.width - frame * 2.0, well.height - frame * 2.0};
    return inner;
}

std::string color_name(Color color) {
    std::array<char, 8> text{};
    std::snprintf(text.data(), text.size(), "#%02X%02X%02X", static_cast<unsigned int>(color.red),
                  static_cast<unsigned int>(color.green), static_cast<unsigned int>(color.blue));
    return std::string(text.data());
}

} // namespace

// -- construction --------------------------------------------------------------

RibbonColorPicker::RibbonColorPicker(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
    set_paint_plane(PaintPlane::control);
    palette_ = standard_colors();
}

RibbonColorPicker::~RibbonColorPicker() {
    if (palette_overlay_) (*palette_overlay_).detach();
}

void RibbonColorPicker::on_dispose() noexcept {
    close_palette();
    command_value_.disconnect();
    command_state_.disconnect();
    if (palette_overlay_) (*palette_overlay_).detach();
    if (layer_) (*layer_).dispose();
}

// -- model ---------------------------------------------------------------------

void RibbonColorPicker::CommandValueChanged::operator()(const std::optional<Color>& value) const {
    if (picker == nullptr || !(*picker).is_alive()) return;
    (*picker).invalidate(Dirty::paint | Dirty::semantics);
    if ((*picker).palette_overlay_) (*(*picker).palette_overlay_).invalidate(Dirty::arrange | Dirty::paint);
    (*picker).value_changed_.emit(value);
}

void RibbonColorPicker::CommandStateChanged::operator()(const CommandState&) const {
    if (picker == nullptr || !(*picker).is_alive()) return;
    if (!(*picker).enabled_to_use()) (*picker).close_palette();
    (*picker).invalidate(Dirty::paint | Dirty::semantics);
}

// A changed command first drops the old one's subscriptions.
void RibbonColorPicker::prepare_write(const decltype(command)&, std::shared_ptr<ColorCommand>& incoming) {
    require_mutable();
    if (command.read() == incoming) return;
    command_value_.disconnect();
    command_state_.disconnect();
}

void RibbonColorPicker::written(const decltype(command)&) {
    const std::shared_ptr<ColorCommand> bound = command.read();
    if (bound) {
        command_value_ = (*bound).value_changed().subscribe(*this, CommandValueChanged{this});
        command_state_ = (*(*bound).command()).state_changed().subscribe(*this, CommandStateChanged{this});
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

// value is a view: the command's value when there is one, else the
// picker's own.
std::optional<Color> RibbonColorPicker::read_view(const decltype(value)&) const {
    const std::shared_ptr<ColorCommand>& bound = command.read();
    if (bound) return (*bound).value();
    return own_value_;
}


void RibbonColorPicker::write_view(const decltype(value)&, std::optional<Color> incoming) {
    require_mutable();
    const std::shared_ptr<ColorCommand> bound = command.read();
    if (bound) {
        (*bound).set_value(incoming);
        return;
    }
    if (own_value_ == incoming) return;
    own_value_ = incoming;
    invalidate(Dirty::paint | Dirty::semantics);
    value_changed_.emit(own_value_);
}

void RibbonColorPicker::set_palette(std::vector<Color> palette) {
    require_mutable();
    if (palette.empty() || palette.size() > maximum_palette) {
        throw std::invalid_argument("RibbonColorPicker palettes hold 1 to maximum_palette colours");
    }
    for (std::size_t index = 0; index < palette.size(); ++index) {
        for (std::size_t other = 0; other < index; ++other) {
            if (palette[other] == palette[index]) throw std::invalid_argument("RibbonColorPicker palette repeats a colour");
        }
    }
    close_palette();
    palette_ = std::move(palette);
}

void RibbonColorPicker::prepare_write(const decltype(text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("RibbonColorPicker text must be valid UTF-8");
}

void RibbonColorPicker::written(const decltype(text)&) {
    invalidate(Dirty::semantics);
}

bool RibbonColorPicker::enabled_to_use() const {
    const bool own = !command.read() || (*command.read()).state().enabled;
    return own && effectively_enabled();
}

// The user chose a colour (or Automatic): it commits to the command, or
// becomes the picker's value; the palette closes.
void RibbonColorPicker::commit(std::optional<Color> color) {
    if (!enabled_to_use()) return;
    const Control::Ptr lifetime = shared_from_this();
    close_palette();
    if (command.read()) {
        static_cast<void>((*command.read()).commit(color, stable_id().value()));
        return;
    }
    if (own_value_ == color) return;
    own_value_ = color;
    invalidate(Dirty::paint | Dirty::semantics);
    value_changed_.emit(own_value_);
}

// -- geometry ------------------------------------------------------------------

Size RibbonColorPicker::natural_size() const noexcept {
    const double scale = effective_text_scale();
    const Size size{(face_extent + arrow_width) * scale, face_extent * scale};
    return size;
}

Size RibbonColorPicker::measure(Size available) {
    const Size natural = natural_size();
    const Size result{std::min(natural.width, available.width), std::min(natural.height, available.height)};
    return result;
}

RibbonColorPicker::Geometry RibbonColorPicker::geometry() const {
    const double scale = effective_text_scale();
    const Rect client = client_rectangle();
    const double face = std::min(client.width, face_extent * scale);
    Geometry placed;
    placed.face = {0.0, 0.0, face, client.height};
    placed.arrow = {face, 0.0, std::max(0.0, client.width - face), client.height};
    const double side = icon_side * scale;
    const double inset = icon_inset * scale;
    const double top = std::floor((client.height - side - bar_height * scale) * 0.5);
    if (image.read().value != 0U) placed.icon = {inset, top, side, side - bar_height * scale * 0.5};
    placed.bar = {inset, top + side - bar_height * scale * 0.5, side, bar_height * scale};
    placed.sample = inset_by(placed.bar, sample_frame * scale);
    placed.glyph = centred_square(placed.arrow, arrow_glyph * scale);
    return placed;
}

RibbonColorPicker::Zone RibbonColorPicker::zone_at(Point local) const {
    const Geometry placed = geometry();
    if (placed.face.contains(local)) return Zone::face;
    if (placed.arrow.contains(local)) return Zone::arrow;
    return Zone::none;
}

RibbonColorPicker::Parts RibbonColorPicker::own_parts() const {
    const Geometry placed = geometry();
    AnatomyStateSet face;
    AnatomyStateSet arrow;
    if (enabled_to_use()) {
        if (hot_ != Zone::none) {
            face.insert(AnatomyState::hot);
            arrow.insert(AnatomyState::hot);
        }
        if (pressed_ == Zone::face) face.insert(AnatomyState::pressed);
        if (pressed_ == Zone::arrow) arrow.insert(AnatomyState::pressed);
    } else {
        face.insert(AnatomyState::disabled);
        arrow.insert(AnatomyState::disabled);
    }
    if (palette_open_) arrow.insert(AnatomyState::open);
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.face = PartGeometry::from_bounds(placed.face);
    record.face.states = face;
    record.icon = PartGeometry::from_bounds(placed.icon);
    record.icon.states = face;
    record.color_bar = PartGeometry::from_bounds(placed.bar);
    // The current colour (none for Automatic), painted by role as a fill
    // with a content colour (owner decision W-7).
    const std::optional<Color> shown = value.read();
    if (shown) {
        record.color_sample = PartGeometry::from_bounds(placed.sample);
        record.color_sample.content_color = shown;
    }
    record.arrow = PartGeometry::from_bounds(placed.arrow);
    record.arrow.states = arrow;
    record.arrow_glyph = PartGeometry::from_bounds(placed.glyph);
    record.arrow_glyph.states = arrow;
    record.focus = PartGeometry::from_bounds(client_rectangle());
    return record;
}

// -- palette layout ------------------------------------------------------------

std::vector<RibbonColorPicker::Cell> RibbonColorPicker::cells() const {
    const double scale = effective_text_scale();
    const double margin = palette_margin * scale;
    const double cell = cell_side * scale;
    const double gap = cell_gap * scale;
    const double width = static_cast<double>(palette_columns) * (cell + gap) - gap;
    std::vector<Cell> all;
    std::span<const Color> recent{};
    if (command.read()) recent = (*command.read()).recent_colors();
    all.reserve(1U + palette_.size() + recent.size());
    all.push_back(Cell{RibbonPaletteEntryKind::automatic, std::nullopt, {margin, margin, width, automatic_extent * scale}});
    const double grid_top = margin + automatic_extent * scale + section_gap * scale * 0.5;
    for (std::size_t index = 0; index < palette_.size(); ++index) {
        const double column = static_cast<double>(index % palette_columns);
        const double row = static_cast<double>(index / palette_columns);
        all.push_back(Cell{RibbonPaletteEntryKind::palette, palette_[index],
                           {margin + column * (cell + gap), grid_top + row * (cell + gap), cell, cell}});
    }
    const std::size_t rows = (palette_.size() + palette_columns - 1U) / palette_columns;
    const double recent_top = grid_top + static_cast<double>(rows) * (cell + gap) - gap + section_gap * scale;
    for (std::size_t index = 0; index < recent.size(); ++index) {
        all.push_back(Cell{RibbonPaletteEntryKind::recent, recent[index],
                           {margin + static_cast<double>(index) * (cell + gap), recent_top, cell, cell}});
    }
    return all;
}

Size RibbonColorPicker::palette_size() const {
    const double scale = effective_text_scale();
    const std::vector<Cell> all = cells();
    double bottom = 0.0;
    double right = 0.0;
    for (const Cell& cell : all) {
        bottom = std::max(bottom, cell.bounds.y + cell.bounds.height);
        right = std::max(right, cell.bounds.x + cell.bounds.width);
    }
    const Size size{right + palette_margin * scale, bottom + palette_margin * scale};
    return size;
}

// The entry of `all` the value matches: Automatic for none, else the first
// palette or recent entry of that colour, or none when the colour is not
// offered.
std::optional<std::size_t> RibbonColorPicker::current_entry(const std::vector<Cell>& all) const {
    const std::optional<Color> shown = value.read();
    if (!shown) return 0U;
    for (std::size_t entry = 1; entry < all.size(); ++entry) {
        if (all[entry].color == shown) return entry;
    }
    return std::nullopt;
}

RibbonColorPicker::PaletteEntryParts RibbonColorPicker::entry_parts(const std::vector<Cell>& all, std::size_t entry) const {
    const double scale = effective_text_scale();
    const Cell& cell = all[entry];
    const double inset = sample_inset * scale;
    PaletteEntryParts parts;
    parts.entry_face = PartGeometry::from_bounds(cell.bounds);
    if (cell.kind == RibbonPaletteEntryKind::automatic) {
        const Rect well{cell.bounds.x + inset * 2.0, cell.bounds.y + (cell.bounds.height - cell_side * scale) * 0.5,
                        cell_side * scale, cell_side * scale};
        parts.entry_well = PartGeometry::from_bounds(well);
        const double label_x = well.x + well.width + inset * 3.0;
        parts.entry_label = PartGeometry::from_bounds(
            {label_x, cell.bounds.y, std::max(0.0, cell.bounds.x + cell.bounds.width - label_x), cell.bounds.height});
    } else {
        parts.entry_well = PartGeometry::from_bounds(
            {cell.bounds.x + inset, cell.bounds.y + inset, cell.bounds.width - inset * 2.0, cell.bounds.height - inset * 2.0});
    }
    // Automatic shows an empty well; a colour shows its sample inside it.
    if (cell.color) {
        parts.entry_sample = PartGeometry::from_bounds(inset_by(parts.entry_well.bounds, sample_frame * scale));
        parts.entry_sample.content_color = cell.color;
    }
    parts.entry_focus = parts.entry_face;
    const AnatomyStateSet states = entry_states(all, entry);
    parts.entry_face.states = states;
    parts.entry_well.states = states;
    parts.entry_sample.states = states;
    parts.entry_label.states = states;
    parts.entry_focus.states = states;
    return parts;
}

AnatomyStateSet RibbonColorPicker::entry_states(const std::vector<Cell>& all, std::size_t entry) const {
    AnatomyStateSet states;
    if (hot_entry_ == entry) states.insert(AnatomyState::hot);
    if (current_entry(all) == entry) states.insert(AnatomyState::selected);
    const Window* const host = attached_window();
    if (palette_focused_ && focus_entry_ == entry && host != nullptr && (*host).focus_cue_visible()) {
        states.insert(AnatomyState::focus_visible);
    }
    return states;
}

// -- paint ---------------------------------------------------------------------

void RibbonColorPicker::on_paint(Painter& painter, Rect) {
    const Parts own = own_parts();
    PartPaint part;
    part.font = effective_font(FontSpec{FontRole::control, 10.5, 400, false, 0.12});
    part.family = ControlVisualRole::menu_item;
    part.role = PartRole::face;
    part.states = own.face.states;
    part.bounds = own.face.bounds;
    paint_part(painter, *this, part);
    part.role = PartRole::actuator;
    part.states = own.arrow.states;
    part.bounds = own.arrow.bounds;
    paint_part(painter, *this, part);
    if (own.icon.present) {
        part.role = PartRole::icon;
        part.states = own.icon.states;
        part.bounds = own.icon.bounds;
        part.image = image.read();
        paint_part(painter, *this, part);
        part.image = {};
    }
    // The colour bar: a well, and the current colour inside it as a fill
    // with a content colour (none for Automatic).
    part.role = PartRole::well;
    part.states = {};
    part.bounds = own.color_bar.bounds;
    paint_part(painter, *this, part);
    if (own.color_sample.present) {
        part.role = PartRole::fill;
        part.bounds = own.color_sample.bounds;
        part.content_color = own.color_sample.content_color;
        paint_part(painter, *this, part);
        part.content_color.reset();
    }
    part.role = PartRole::glyph;
    part.states = own.arrow_glyph.states;
    part.bounds = own.arrow_glyph.bounds;
    part.mark = PartMark::arrow_down;
    paint_part(painter, *this, part);
    part.mark = PartMark::none;
    AnatomyStateSet focus;
    const Window* const host = attached_window();
    if (focused_ && host != nullptr && (*host).focus_cue_visible()) focus.insert(AnatomyState::focus_visible);
    part.role = PartRole::focus;
    part.states = focus;
    part.bounds = own.focus.bounds;
    paint_part(painter, *this, part);
}

// Paints the palette on its content control, `surface`, whose theme and
// text scale the parts take.
void RibbonColorPicker::palette_paint(Painter& painter) const {
    const Control& surface = *palette_overlay_;
    const std::vector<Cell> all = cells();
    const Size size = palette_size();
    PartPaint part;
    part.font = effective_font(FontSpec{FontRole::control, 10.5, 400, false, 0.12});
    part.family = ControlVisualRole::panel;
    part.role = PartRole::shadow;
    part.bounds = {0.0, 0.0, size.width, size.height};
    paint_part(painter, surface, part);
    part.role = PartRole::face;
    paint_part(painter, surface, part);
    for (std::size_t entry = 0; entry < all.size(); ++entry) {
        const PaletteEntryParts parts = entry_parts(all, entry);
        part.family = ControlVisualRole::menu_item;
        part.role = PartRole::face;
        part.states = parts.entry_face.states;
        part.bounds = parts.entry_face.bounds;
        paint_part(painter, surface, part);
        part.role = PartRole::well;
        part.bounds = parts.entry_well.bounds;
        paint_part(painter, surface, part);
        if (parts.entry_sample.present) {
            part.role = PartRole::fill;
            part.bounds = parts.entry_sample.bounds;
            part.content_color = parts.entry_sample.content_color;
            paint_part(painter, surface, part);
            part.content_color.reset();
        }
        if (parts.entry_label.present) {
            part.role = PartRole::label;
            part.bounds = parts.entry_label.bounds;
            part.text = lend_paint_text("Automatic");
            paint_part(painter, surface, part);
            part.text = {};
        }
        part.role = PartRole::focus;
        part.bounds = parts.entry_focus.bounds;
        paint_part(painter, surface, part);
    }
}

// -- input ---------------------------------------------------------------------

void RibbonColorPicker::set_hot_zone(Zone zone) {
    if (hot_ != zone) {
        hot_ = zone;
        invalidate(Dirty::paint);
    }
}

// The hot zone follows the pointer's moves (the move that enters the
// picker sets it).
void RibbonColorPicker::on_mouse_move(MouseEventArgs& arguments) {
    if (eligible_for_input()) set_hot_zone(zone_at(arguments.location));
    Control::on_mouse_move(arguments);
}

void RibbonColorPicker::on_mouse_leave() {
    if (eligible_for_input()) set_hot_zone(Zone::none);
    Control::on_mouse_leave();
}

void RibbonColorPicker::on_mouse_down(MouseEventArgs& arguments) {
    if (eligible_for_input() && arguments.button == PointerButton::primary && enabled_to_use()) {
        const Control::Ptr lifetime = shared_from_this();
        const Zone zone = zone_at(arguments.location);
        arguments.handled = zone != Zone::none;
        if (zone == Zone::arrow) {
            if (palette_open_) {
                close_palette();
            } else {
                static_cast<void>(open_palette(MenuOpenMode::pointer));
            }
        } else {
            pressed_ = zone;
            invalidate(Dirty::paint);
        }
    }
    Control::on_mouse_down(arguments);
}

void RibbonColorPicker::on_mouse_up(MouseEventArgs& arguments) {
    if (eligible_for_input() && arguments.button == PointerButton::primary && enabled_to_use()) {
        const Control::Ptr lifetime = shared_from_this();
        const Zone zone = zone_at(arguments.location);
        const Zone pressed = pressed_;
        pressed_ = Zone::none;
        invalidate(Dirty::paint);
        // A click on the face applies the current colour again.
        if (pressed == Zone::face && zone == Zone::face) {
            arguments.handled = true;
            commit(value.read());
        }
    }
    Control::on_mouse_up(arguments);
}

void RibbonColorPicker::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled || !enabled_to_use()) return;
    const std::uint32_t key = arguments.physical_key;
    if (key == PhysicalKey::enter || key == PhysicalKey::space) {
        commit(value.read());
    } else if (key == PhysicalKey::down || key == PhysicalKey::f4) {
        static_cast<void>(open_palette(MenuOpenMode::keyboard));
    } else {
        return;
    }
    arguments.handled = true;
}

void RibbonColorPicker::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

void RibbonColorPicker::palette_pointer(PointerEvent& event) {
    if (!palette_open_ || !palette_overlay_) return;
    const Rect absolute = (*palette_overlay_).absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const std::vector<Cell> all = cells();
    std::optional<std::size_t> hit{};
    for (std::size_t entry = 0; entry < all.size(); ++entry) {
        if (all[entry].bounds.contains(local)) hit = entry;
    }
    if (event.action == PointerAction::leave) hit.reset();
    if (event.action == PointerAction::leave || event.action == PointerAction::move ||
        event.action == PointerAction::enter) {
        if (hot_entry_ != hit) {
            hot_entry_ = hit;
            (*palette_overlay_).invalidate(Dirty::paint);
        }
        return;
    }
    if (event.button != PointerButton::primary) return;
    event.handled = hit.has_value();
    // A click is a release over an entry, as in a menu.
    if (event.action == PointerAction::up && hit) commit(all[*hit].color);
}

// Arrows move through the entries (Up and Down to the nearest entry in the
// row above or below), Enter and Space commit, Home and End go to the first
// and last; Escape reaches the layer, which closes the palette.
void RibbonColorPicker::palette_key(KeyEvent& event) {
    if (event.action != KeyAction::down || !palette_open_) return;
    // Automatic is always an entry, so `all` is never empty. The keyboard's
    // entry can lie past the end when the recent colours shrank while the
    // palette was open; it then counts as the last entry.
    const std::vector<Cell> all = cells();
    const std::size_t current = std::min(focus_entry_.value_or(0U), all.size() - 1U);
    const std::uint32_t key = event.physical_key;
    std::optional<std::size_t> next{};
    if (key == PhysicalKey::enter || key == PhysicalKey::space) {
        event.handled = true;
        commit(all[current].color);
        return;
    }
    if (key == PhysicalKey::right) next = std::min(current + 1U, all.size() - 1U);
    if (key == PhysicalKey::left) next = current == 0U ? 0U : current - 1U;
    if (key == PhysicalKey::home) next = 0U;
    if (key == PhysicalKey::end) next = all.size() - 1U;
    if (key == PhysicalKey::up || key == PhysicalKey::down) {
        const Rect from = all[current].bounds;
        const double centre = from.x + from.width * 0.5;
        double best_row = key == PhysicalKey::down ? std::numeric_limits<double>::max()
                                                   : -std::numeric_limits<double>::max();
        double best_distance = std::numeric_limits<double>::max();
        for (std::size_t entry = 0; entry < all.size(); ++entry) {
            const Rect bounds = all[entry].bounds;
            const bool beyond = key == PhysicalKey::down ? bounds.y > from.y + 0.5 : bounds.y < from.y - 0.5;
            if (!beyond) continue;
            const bool nearer_row = key == PhysicalKey::down ? bounds.y < best_row - 0.5 : bounds.y > best_row + 0.5;
            const double distance = std::fabs(bounds.x + bounds.width * 0.5 - centre);
            const bool same_row = std::fabs(bounds.y - best_row) <= 0.5;
            if (nearer_row || (same_row && distance < best_distance)) {
                best_row = bounds.y;
                best_distance = distance;
                next = entry;
            }
        }
        if (!next) next = current;
    }
    if (!next) return;
    focus_entry_ = next;
    (*palette_overlay_).invalidate(Dirty::paint | Dirty::semantics);
    event.handled = true;
}

// -- overlay ---------------------------------------------------------------------

bool RibbonColorPicker::open_palette(MenuOpenMode mode) {
    require_mutable();
    Window* const window = attached_window();
    if (palette_open_ || window == nullptr || !enabled_to_use()) return false;
    const Control::Ptr self = shared_from_this();
    AnchoredOverlayPlacement placement;
    placement.horizontal_alignment = OverlayHorizontalAlignment::near;
    placement.vertical_preference = OverlayVerticalPreference::below;
    placement.gap = 1.0;
    if (!palette_overlay_) {
        const std::string prefix(stable_id().value());
        palette_overlay_ = make_control<RibbonColorPalette>(StableId(prefix + ".palette"), *this);
        layer_ = make_control<AnchoredOverlayLayer>(StableId(prefix + ".palette.layer"), self, placement);
        (*layer_).set_content(palette_overlay_);
        layer_dismissed_ = (*layer_).dismiss_requested().subscribe(
            Delegate<OverlayDismissReason>::bind<RibbonColorPicker, &RibbonColorPicker::on_layer_dismissed>(*this));
    }
    placement.preferred_size = palette_size();
    (*layer_).placement = placement;
    (*palette_overlay_).accessible_name = parse_mnemonic_text(text.read()).display_text;
    const Size client = (*window).client_size();
    (*layer_).set_requested_bounds({0.0, 0.0, client.width, client.height});
    hot_entry_.reset();
    // By keyboard the keyboard starts on the current colour, or on Automatic
    // when the colour is not offered; by pointer only on the current colour.
    const std::vector<Cell> all = cells();
    const std::optional<std::size_t> current = current_entry(all);
    if (mode == MenuOpenMode::pointer) {
        focus_entry_ = current;
    } else {
        focus_entry_ = current.value_or(0U);
    }
    (*layer_).set_content(palette_overlay_);
    overlay_ = (*window).open_overlay(self, layer_);
    palette_open_ = true;
    if (Event<>* closed = overlay_.closed_event()) {
        overlay_revoked_ =
            (*closed).subscribe(*this, Delegate<>::bind<RibbonColorPicker, &RibbonColorPicker::on_overlay_revoked>(*this));
    }
    // In a Popup the palette may reach past the owner onto the desktop; the
    // emptied layer stays in the owner and holds the focus scope.
    const Rect anchor = absolute_bounds();
    const Rect hung{anchor.x, anchor.y, anchor.width, anchor.height + placement.gap};
    const std::optional<Rect> face = drop_down_popup_face(*window, hung, placement.preferred_size, anchor.x);
    bool lifted = false;
    if (face.has_value()) {
        (*layer_).clear_content();
        lifted = lift_.lift(*window, palette_overlay_, *face);
        if (!lifted) (*layer_).set_content(palette_overlay_);
    }
    (*layer_).focusable = lifted;
    if (lifted) {
        lift_closed_ = (*window).popup_window_closed().subscribe(
            Delegate<std::size_t>::bind<RibbonColorPicker, &RibbonColorPicker::on_popup_window_closed>(*this));
    }
    const Control::Ptr preferred_focus = lifted ? Control::Ptr(layer_) : Control::Ptr(palette_overlay_);
    scope_ = (*window).begin_focus_scope(layer_, preferred_focus);
    invalidate(Dirty::paint | Dirty::semantics);
    palette_open_changed_.emit(true);
    return true;
}

void RibbonColorPicker::close_palette() noexcept {
    if (!palette_open_) return;
    palette_open_ = false;
    lift_closed_.disconnect();
    lift_.release();
    Window* const window = attached_window();
    if (window != nullptr && scope_) {
        try {
            static_cast<void>((*window).end_focus_scope(scope_));
        } catch (...) {
        }
    }
    scope_ = {};
    overlay_revoked_.disconnect();
    overlay_.disconnect();
    hot_entry_.reset();
    palette_focused_ = false;
    if (!is_alive()) return;
    try {
        invalidate(Dirty::paint | Dirty::semantics);
        palette_open_changed_.emit(false);
    } catch (...) {
    }
}

void RibbonColorPicker::on_layer_dismissed(OverlayDismissReason) {
    close_palette();
}

void RibbonColorPicker::on_overlay_revoked() {
    if (!palette_open_) return;
    palette_open_ = false;
    lift_closed_.disconnect();
    lift_.release();
    scope_ = {};
    overlay_revoked_.disconnect();
    hot_entry_.reset();
    palette_focused_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
    palette_open_changed_.emit(false);
}

void RibbonColorPicker::on_popup_window_closed(const std::size_t slot) {
    if (slot != Window::picker_popup_window_slot || !lift_.lifted()) return;
    close_palette();
}

Control::Ptr RibbonColorPicker::palette_overlay() const noexcept {
    return palette_overlay_;
}

std::vector<RibbonColorPicker::PaletteEntry> RibbonColorPicker::realized_entries() const {
    std::vector<PaletteEntry> records;
    if (!palette_open_) return records;
    const std::vector<Cell> all = cells();
    const std::string prefix(stable_id().value());
    records.reserve(all.size());
    for (std::size_t entry = 0; entry < all.size(); ++entry) {
        PaletteEntry record;
        record.index = entry;
        record.stable_id = prefix + ".entry." + std::to_string(entry);
        record.kind = all[entry].kind;
        record.color = all[entry].color;
        record.parts = entry_parts(all, entry);
        const AnatomyStateSet states = entry_states(all, entry);
        record.states.hot = states.contains(AnatomyState::hot);
        record.states.selected = states.contains(AnatomyState::selected);
        record.states.focus_visible = states.contains(AnatomyState::focus_visible);
        records.push_back(record);
    }
    return records;
}

// -- semantics -----------------------------------------------------------------

SemanticDescriptor RibbonColorPicker::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::split_button;
    descriptor.name = accessible_name.read().empty() ? parse_mnemonic_text(text.read()).display_text : accessible_name.read();
    const std::optional<Color> shown = value.read();
    descriptor.value = shown ? color_name(*shown) : std::string("Automatic");
    descriptor.description = accessible_description.read();
    descriptor.has_popup = true;
    if (palette_open_) descriptor.states |= SemanticState::expanded;
    descriptor.actions = {SemanticAction::focus, SemanticAction::press, SemanticAction::show_menu,
                          SemanticAction::expand, SemanticAction::collapse};
    descriptor.exposed = true;
    return descriptor;
}

bool RibbonColorPicker::on_semantic_action(SemanticAction action, std::string_view) {
    if (action == SemanticAction::press) {
        if (!enabled_to_use()) return false;
        commit(value.read());
        return true;
    }
    if (action == SemanticAction::show_menu || action == SemanticAction::expand) {
        return open_palette(MenuOpenMode::keyboard);
    }
    if (action == SemanticAction::collapse) {
        close_palette();
        return true;
    }
    if (action == SemanticAction::focus && attached_window() != nullptr) {
        return (*attached_window()).request_focus(shared_from_this());
    }
    return false;
}

std::vector<SemanticNode> RibbonColorPicker::palette_semantic_children() const {
    std::vector<SemanticNode> nodes;
    if (!palette_open_ || !palette_overlay_) return nodes;
    const std::vector<Cell> all = cells();
    const Rect absolute = (*palette_overlay_).absolute_bounds();
    const std::optional<std::size_t> current = current_entry(all);
    const std::string prefix(stable_id().value());
    for (std::size_t entry = 0; entry < all.size(); ++entry) {
        SemanticNode node{};
        node.stable_id = prefix + ".entry." + std::to_string(entry);
        node.role = SemanticRole::button;
        node.name = all[entry].color ? color_name(*all[entry].color) : std::string("Automatic");
        if (all[entry].kind == RibbonPaletteEntryKind::recent) node.description = "Recent colour";
        node.position_in_set = entry + 1U;
        node.set_size = all.size();
        const Rect bounds = all[entry].bounds;
        node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::visible | SemanticState::enabled | SemanticState::focusable;
        if (current == entry) node.states |= SemanticState::selected;
        if (palette_focused_ && focus_entry_ == entry) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::press};
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool RibbonColorPicker::palette_semantic_action(std::string_view id, SemanticAction action) {
    if (action != SemanticAction::press || !palette_open_) return false;
    const std::vector<Cell> all = cells();
    const std::string prefix = std::string(stable_id().value()) + ".entry.";
    for (std::size_t entry = 0; entry < all.size(); ++entry) {
        if (id != prefix + std::to_string(entry)) continue;
        commit(all[entry].color);
        return true;
    }
    return false;
}

// -- anatomy -------------------------------------------------------------------

namespace {

constexpr std::array<AnatomyState, 4> face_part_states{
    {AnatomyState::hot, AnatomyState::pressed, AnatomyState::disabled, AnatomyState::open}};
constexpr std::array<AnatomyState, 3> entry_part_states{
    {AnatomyState::hot, AnatomyState::selected, AnatomyState::focus_visible}};

constexpr std::array<PartField<RibbonColorPicker::Parts>, 8> picker_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &RibbonColorPicker::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = "button", .states = face_part_states},
     .member = &RibbonColorPicker::Parts::face},
    {.declaration = {.name = "icon", .role = PartRole::icon, .composed_from = {}, .states = face_part_states},
     .member = &RibbonColorPicker::Parts::icon},
    {.declaration = {.name = "color_bar", .role = PartRole::well, .composed_from = {}},
     .member = &RibbonColorPicker::Parts::color_bar},
    {.declaration = {.name = "color_sample", .role = PartRole::fill, .composed_from = {}},
     .member = &RibbonColorPicker::Parts::color_sample},
    {.declaration = {.name = "arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = face_part_states},
     .member = &RibbonColorPicker::Parts::arrow},
    {.declaration = {.name = "arrow_glyph", .role = PartRole::glyph, .composed_from = {}, .states = face_part_states},
     .member = &RibbonColorPicker::Parts::arrow_glyph},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &RibbonColorPicker::Parts::focus},
}};

constexpr std::array<PartField<RibbonColorPicker::PaletteEntryParts>, 5> entry_part_fields{{
    {.declaration = {.name = "entry_face", .role = PartRole::face, .composed_from = {}, .states = entry_part_states},
     .member = &RibbonColorPicker::PaletteEntryParts::entry_face},
    {.declaration = {.name = "entry_well", .role = PartRole::well, .composed_from = {}, .states = entry_part_states},
     .member = &RibbonColorPicker::PaletteEntryParts::entry_well},
    {.declaration = {.name = "entry_sample", .role = PartRole::fill, .composed_from = {}, .states = entry_part_states},
     .member = &RibbonColorPicker::PaletteEntryParts::entry_sample},
    {.declaration = {.name = "entry_label", .role = PartRole::label, .composed_from = {}, .states = entry_part_states},
     .member = &RibbonColorPicker::PaletteEntryParts::entry_label},
    {.declaration = {.name = "entry_focus", .role = PartRole::focus, .composed_from = {}, .states = entry_part_states},
     .member = &RibbonColorPicker::PaletteEntryParts::entry_focus},
}};

constexpr std::array<PartDeclaration, 8> picker_parts = part_declarations(picker_part_fields);
constexpr std::array<PartDeclaration, 5> entry_parts_declared = part_declarations(entry_part_fields);
constexpr std::array<AnatomyState, 6> picker_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible, AnatomyState::open,
};
constexpr std::array<ItemSystemDeclaration, 1> picker_item_systems{{
    {.name = "palette_entry", .parts = entry_parts_declared, .states = entry_part_states, .custom_states = {}},
}};

constexpr ControlAnatomy picker_anatomy{
    .control_id = "ribbon_color_picker",
    .parts = picker_parts,
    .states = picker_states,
    .custom_states = {},
    .item_systems = picker_item_systems,
};

// The events RibbonColorPicker adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> picker_interactions{{
    {.name = "value_changed", .payload = "std::optional<Color>", .mutable_payload = false},
    {.name = "palette_open_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& RibbonColorPicker::anatomy() const noexcept {
    return picker_anatomy;
}

AnatomyStateSet RibbonColorPicker::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (palette_open_) states.insert(AnatomyState::open);
    return states;
}

void RibbonColorPicker::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Parts record = own_parts();
    write_parts(anatomy(), picker_part_fields, record, geometry);
}

RibbonColorPicker::Parts RibbonColorPicker::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), picker_part_fields, published.parts(), record);
    return record;
}

void RibbonColorPicker::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), picker_interactions.begin(), picker_interactions.end());
}

// -- the palette's content control ----------------------------------------------

RibbonColorPalette::RibbonColorPalette(StableId stable_id, RibbonColorPicker& picker)
    : Control(std::move(stable_id)), picker_(&picker) {
    focusable = true;
    set_paint_plane(PaintPlane::control);
}

bool RibbonColorPalette::picker_alive() const noexcept {
    const bool result = picker_ != nullptr && (*picker_).is_alive();
    return result;
}

Size RibbonColorPalette::measure(Size available) {
    if (!picker_alive()) return {};
    const Size size = (*picker_).palette_size();
    const Size result{std::min(size.width, available.width), std::min(size.height, available.height)};
    return result;
}

void RibbonColorPalette::on_paint(Painter& painter, Rect) {
    if (picker_alive()) (*picker_).palette_paint(painter);
}

// The palette's mouse and keys are the picker's (palette_pointer,
// palette_key), before the events are raised.
void RibbonColorPalette::forward_mouse(MouseEventArgs& arguments) {
    if (!picker_alive()) return;
    const Control::Ptr lifetime = shared_from_this();
    PointerEvent event = pointer_event_of(arguments);
    (*picker_).palette_pointer(event);
    arguments.handled = event.handled;
}

void RibbonColorPalette::on_mouse_down(MouseEventArgs& arguments) {
    forward_mouse(arguments);
    Control::on_mouse_down(arguments);
}

void RibbonColorPalette::on_mouse_up(MouseEventArgs& arguments) {
    forward_mouse(arguments);
    Control::on_mouse_up(arguments);
}

void RibbonColorPalette::on_mouse_move(MouseEventArgs& arguments) {
    forward_mouse(arguments);
    Control::on_mouse_move(arguments);
}

void RibbonColorPalette::on_mouse_wheel(MouseEventArgs& arguments) {
    forward_mouse(arguments);
    Control::on_mouse_wheel(arguments);
}

void RibbonColorPalette::on_mouse_leave() {
    if (picker_alive()) {
        const Control::Ptr lifetime = shared_from_this();
        PointerEvent leave{};
        leave.action = PointerAction::leave;
        (*picker_).palette_pointer(leave);
    }
    Control::on_mouse_leave();
}

void RibbonColorPalette::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    forward_key(arguments, KeyAction::down);
}

void RibbonColorPalette::on_key_up(KeyEventArgs& arguments) {
    Control::on_key_up(arguments);
    forward_key(arguments, KeyAction::up);
}

// A key no subscriber handled.
void RibbonColorPalette::forward_key(KeyEventArgs& arguments, KeyAction action) {
    if (!picker_alive() || arguments.handled || arguments.released_by_deactivation) return;
    const Control::Ptr lifetime = shared_from_this();
    KeyEvent event = key_event_of(arguments, action);
    (*picker_).palette_key(event);
    arguments.handled = event.handled;
}

void RibbonColorPalette::on_focus_changed(bool focused) {
    if (!picker_alive()) return;
    (*picker_).palette_focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

Insets RibbonColorPalette::visual_outsets() const noexcept {
    const Insets result = overlay_shadow_outsets(*this);
    return result;
}

SemanticDescriptor RibbonColorPalette::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> RibbonColorPalette::semantic_virtual_children() const {
    if (!picker_alive()) return {};
    std::vector<SemanticNode> nodes = (*picker_).palette_semantic_children();
    return nodes;
}

bool RibbonColorPalette::on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                                  std::string_view) {
    if (!picker_alive()) return false;
    const Control::Ptr lifetime = shared_from_this();
    const bool handled = (*picker_).palette_semantic_action(stable_id, action);
    return handled;
}

// property_codemod begin RibbonColorPicker (tools/property_families/ribbon_color_picker.json; generated, do not edit)

void RibbonColorPicker::prepare_write(const decltype(image)&, ImageId&) {
    require_mutable();
}

void RibbonColorPicker::written(const decltype(image)&) {
    invalidate(Dirty::paint);
}

// property_codemod end RibbonColorPicker

} // namespace gui_forms
