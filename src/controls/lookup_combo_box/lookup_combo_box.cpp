#include "gui_forms/controls/lookup_combo_box.hpp"

#include "gui_forms/controls/anchored_overlay_layer.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

// Geometry in logical units: the field's natural size and its drop-down
// button's width, the text inset, a list row's and the header's height,
// and the cell padding.
constexpr double natural_width = 200.0;
constexpr double natural_height = 24.0;
constexpr double button_width = 24.0;
constexpr double text_inset = 7.0;
constexpr double row_height = 22.0;
constexpr double header_height = 24.0;
constexpr double cell_padding = 6.0;
constexpr double disclosure_side = 8.0;

constexpr std::array<PartField<LookupComboBox::Parts>, 6> lookup_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &LookupComboBox::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &LookupComboBox::Parts::well},
    {.declaration = {.name = "label", .role = PartRole::content, .composed_from = {}},
     .member = &LookupComboBox::Parts::label},
    {.declaration = {.name = "actuator", .role = PartRole::actuator, .composed_from = {}},
     .member = &LookupComboBox::Parts::actuator},
    {.declaration = {.name = "glyph", .role = PartRole::disclosure, .composed_from = {}},
     .member = &LookupComboBox::Parts::glyph},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &LookupComboBox::Parts::focus},
}};

constexpr std::array<PartDeclaration, 6> lookup_parts = part_declarations(lookup_part_fields);

constexpr std::array<AnatomyState, 8> lookup_states{
    AnatomyState::normal,  AnatomyState::hot,      AnatomyState::pressed, AnatomyState::enabled,
    AnatomyState::disabled, AnatomyState::focused, AnatomyState::focus_visible, AnatomyState::active_tool,
};

constexpr ControlAnatomy lookup_anatomy{
    .control_id = "lookup_combo_box",
    .parts = lookup_parts,
    .states = lookup_states,
    .custom_states = {},
};

char ascii_lower(char letter) noexcept {
    if (letter >= 'A' && letter <= 'Z') return static_cast<char>(letter - 'A' + 'a');
    return letter;
}

// Whether `haystack` contains `needle`, ignoring ASCII case; an empty
// needle is contained everywhere.
bool contains_folded(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    const std::size_t last = haystack.size() - needle.size();
    for (std::size_t start = 0U; start <= last; ++start) {
        bool matched = true;
        for (std::size_t offset = 0U; offset < needle.size(); ++offset) {
            if (ascii_lower(haystack[start + offset]) != ascii_lower(needle[offset])) {
                matched = false;
                break;
            }
        }
        if (matched) return true;
    }
    return false;
}

// The length of `text` without its last UTF-8 scalar (text is valid UTF-8).
std::size_t without_last_scalar(std::string_view text) noexcept {
    std::size_t end = text.size();
    if (end == 0U) return 0U;
    --end;
    while (end > 0U && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) --end;
    return end;
}

} // namespace

// The open list: a header row and the filtered rows, each cell clipped to
// its column. It takes no focus; the combo box keeps the keys and drives
// the highlight. A press on a row raises row_activated with its position.
class LookupComboBox::List final : public Control {
public:
    List(StableId stable_id, LookupComboBox& owner) : Control(std::move(stable_id)), owner_(owner) {
        set_paint_plane(PaintPlane::overlay);
        focusable = false;
        cursor = CursorKind::arrow;
    }

    [[nodiscard]] Event<std::size_t>& row_activated() noexcept { return row_activated_; }

    // The scroll position, as the first row shown.
    void reveal(std::size_t position) {
        const std::size_t visible = visible_rows();
        if (position < first_) first_ = position;
        if (visible > 0U && position >= first_ + visible) first_ = position + 1U - visible;
        invalidate(Dirty::paint);
    }

    [[nodiscard]] std::size_t visible_rows() const noexcept {
        const double body = std::max(0.0, client_rectangle().height - header_height);
        const std::size_t count = static_cast<std::size_t>(std::floor(body / row_height));
        return count;
    }

    [[nodiscard]] Size measure(Size available) override { return available; }

    void on_paint(Painter& painter, Rect) override {
        const Rect client = client_rectangle();
        const AnatomyStateSet states = base_anatomy_states();
        PartPaint shadow{.role = PartRole::shadow, .bounds = client, .states = states,
                         .family = ControlVisualRole::tool_tip};
        paint_part(painter, *this, shadow);
        PartPaint face{.role = PartRole::face, .bounds = client, .states = states,
                       .family = ControlVisualRole::menu_item};
        paint_part(painter, *this, face);
        const FontSpec font = owner_.field_font();
        double x = 0.0;
        for (const LookupColumn& column : owner_.columns_) {
            PartPaint header{.role = PartRole::caption,
                             .bounds = Rect{x + cell_padding, 0.0, std::max(0.0, column.width - cell_padding * 2.0),
                                            header_height},
                             .states = states,
                             .family = ControlVisualRole::menu_item};
            header.text = column.header;
            header.font = font;
            header.tone = PartTextTone::secondary;
            paint_part(painter, *this, header);
            x += column.width;
        }
        const PartPaint rule{.role = PartRole::divider, .bounds = Rect{0.0, header_height - 1.0, client.width, 1.0},
                             .states = states, .family = ControlVisualRole::menu_item};
        paint_part(painter, *this, rule);
        const std::vector<std::size_t>& shown = owner_.filtered_;
        const std::size_t visible = visible_rows();
        const std::size_t end = std::min(shown.size(), first_ + visible);
        for (std::size_t position = first_; position < end; ++position) {
            const double y = header_height + row_height * static_cast<double>(position - first_);
            const bool highlighted = owner_.highlighted_ && *owner_.highlighted_ == position;
            AnatomyStateSet row_states = states;
            if (highlighted) row_states.insert(AnatomyState::selected);
            if (highlighted) {
                const PartPaint selection{.role = PartRole::selection, .bounds = Rect{0.0, y, client.width, row_height},
                                          .states = row_states, .family = ControlVisualRole::menu_item};
                paint_part(painter, *this, selection);
            }
            const LookupRow& row = owner_.rows_[shown[position]];
            double cell_x = 0.0;
            for (std::size_t column = 0U; column < owner_.columns_.size(); ++column) {
                const double width = owner_.columns_[column].width;
                if (column < row.cells.size()) {
                    PartPaint cell{.role = PartRole::label,
                                   .bounds = Rect{cell_x + cell_padding, y,
                                                  std::max(0.0, width - cell_padding * 2.0), row_height},
                                   .states = row_states,
                                   .family = ControlVisualRole::menu_item};
                    cell.text = row.cells[column];
                    cell.font = font;
                    paint_part(painter, *this, cell);
                }
                cell_x += width;
            }
        }
    }

    void on_pointer(PointerEvent& event) override {
        const Rect absolute = absolute_bounds();
        const double y = event.position.y - absolute.y - header_height;
        if (event.action == PointerAction::wheel) {
            event.handled = true;
            if (event.wheel_delta.y > 0.0 && first_ > 0U) --first_;
            const std::size_t visible = visible_rows();
            if (event.wheel_delta.y < 0.0 && first_ + visible < owner_.filtered_.size()) ++first_;
            invalidate(Dirty::paint);
            return;
        }
        if (event.button != PointerButton::primary) return;
        if (event.action == PointerAction::down || event.action == PointerAction::up) event.handled = true;
        if (event.action != PointerAction::up || y < 0.0) return;
        const std::size_t position = first_ + static_cast<std::size_t>(std::floor(y / row_height));
        if (position < owner_.filtered_.size()) row_activated_.emit(position);
    }

private:
    // The combo box owns the list while it is open and releases it on
    // closing, before the combo box itself can go.
    LookupComboBox& owner_;
    std::size_t first_{0U};
    Event<std::size_t> row_activated_;
};

LookupComboBox::LookupComboBox(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
    cursor = CursorKind::arrow;
}

LookupComboBox::~LookupComboBox() {
    release_overlay();
}

void LookupComboBox::set_columns(std::vector<LookupColumn> columns) {
    require_mutable();
    for (const LookupColumn& column : columns) {
        if (!validate_utf8(column.header).valid()) {
            throw std::invalid_argument("LookupComboBox column headers must be valid UTF-8");
        }
        if (!std::isfinite(column.width) || column.width <= 0.0) {
            throw std::invalid_argument("LookupComboBox column widths must be finite and positive");
        }
    }
    columns_ = std::move(columns);
    invalidate(Dirty::paint | Dirty::semantics);
    if (list_) (*list_).invalidate(Dirty::paint);
}

void LookupComboBox::set_rows(std::vector<LookupRow> rows) {
    require_mutable();
    std::unordered_set<std::string_view> keys{};
    keys.reserve(rows.size());
    for (const LookupRow& row : rows) {
        if (row.key.empty() || !validate_utf8(row.key).valid() || !keys.insert(row.key).second) {
            throw std::invalid_argument("LookupComboBox row keys must be distinct, non-empty and valid UTF-8");
        }
        for (const std::string& cell : row.cells) {
            if (!validate_utf8(cell).valid()) throw std::invalid_argument("LookupComboBox cells must be valid UTF-8");
        }
    }
    keys.clear();
    rows_ = std::move(rows);
    refilter();
    if (!selected_key.read().empty() && !row_of(selected_key.read())) selected_key = std::string{};
    invalidate(Dirty::paint | Dirty::semantics);
}

std::optional<std::size_t> LookupComboBox::row_of(std::string_view key) const noexcept {
    for (std::size_t index = 0U; index < rows_.size(); ++index) {
        if (rows_[index].key == key) return index;
    }
    return std::nullopt;
}

std::string LookupComboBox::shown_text() const {
    if (dropped_down_ && !filter_.empty()) return filter_;
    const std::optional<std::size_t> row = row_of(selected_key.read());
    if (!row) return {};
    const std::vector<std::string>& cells = rows_[*row].cells;
    const std::size_t column = display_column.read();
    if (column >= cells.size()) return {};
    return cells[column];
}

void LookupComboBox::refilter() {
    filtered_.clear();
    filtered_.reserve(rows_.size());
    for (std::size_t index = 0U; index < rows_.size(); ++index) {
        bool matched = filter_.empty();
        for (const std::string& cell : rows_[index].cells) {
            if (matched) break;
            matched = contains_folded(cell, filter_);
        }
        if (matched) filtered_.push_back(index);
    }
    highlighted_.reset();
    const std::optional<std::size_t> chosen = row_of(selected_key.read());
    for (std::size_t position = 0U; position < filtered_.size(); ++position) {
        if (chosen && filtered_[position] == *chosen) highlighted_ = position;
    }
    if (!highlighted_ && !filtered_.empty() && !filter_.empty()) highlighted_ = 0U;
    if (list_) {
        if (highlighted_) (*list_).reveal(*highlighted_);
        (*list_).invalidate(Dirty::paint);
    }
}

FontSpec LookupComboBox::field_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.field);
    return font;
}

LookupComboBox::Placement LookupComboBox::placement() const {
    Placement placed;
    const Rect bounds = client_rectangle();
    const double button = std::min(button_width, bounds.width);
    placed.well = bounds;
    placed.actuator = Rect{std::max(0.0, bounds.width - button), 1.0, std::max(0.0, button - 1.0),
                           std::max(0.0, bounds.height - 2.0)};
    placed.glyph = Rect{std::floor(placed.actuator.x + (placed.actuator.width - disclosure_side) * 0.5),
                        std::floor((bounds.height - disclosure_side) * 0.5), disclosure_side, disclosure_side};
    placed.label = Rect{text_inset, 0.0, std::max(0.0, bounds.width - button - text_inset - 3.0), bounds.height};
    return placed;
}

Size LookupComboBox::measure(Size available) {
    const Rect requested = requested_bounds();
    const double width = requested.width > 0.0 ? requested.width : natural_width;
    const double height = requested.height > 0.0 ? requested.height : natural_height;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

AnatomyStateSet LookupComboBox::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (hot_) states.insert(AnatomyState::hot);
    if (dropped_down_) states.insert(AnatomyState::active_tool);
    return states;
}

void LookupComboBox::on_paint(Painter& painter, Rect) {
    const Placement placed = placement();
    const AnatomyStateSet states = anatomy_states();
    PartPaint well{.role = PartRole::well, .bounds = placed.well, .states = states,
                   .family = ControlVisualRole::editor};
    well.key = PartKey{"well", {}, {}};
    paint_part(painter, *this, well);
    PartPaint drop_down{.role = PartRole::actuator, .bounds = placed.actuator, .states = states,
                        .family = ControlVisualRole::choice};
    drop_down.key = PartKey{"actuator", {}, {}};
    paint_part(painter, *this, drop_down);
    PartPaint disclosure{.role = PartRole::disclosure, .bounds = placed.glyph, .states = states,
                         .family = ControlVisualRole::choice};
    disclosure.mark = dropped_down_ ? PartMark::arrow_up : PartMark::arrow_down;
    paint_part(painter, *this, disclosure);
    const std::string shown = shown_text();
    PartPaint content{.role = PartRole::content, .bounds = placed.label, .states = states,
                      .family = ControlVisualRole::editor};
    content.text = shown;
    content.font = field_font();
    paint_part(painter, *this, content);
    if (focused_ || dropped_down_) {
        PartPaint ring{.role = PartRole::focus, .bounds = placed.well, .states = states,
                       .family = ControlVisualRole::editor};
        ring.states.insert(AnatomyState::focus_visible);
        paint_part(painter, *this, ring);
    }
}

void LookupComboBox::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    if (event.action == PointerAction::move || event.action == PointerAction::leave) {
        const bool hot = event.action == PointerAction::move;
        if (hot != hot_) {
            hot_ = hot;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.button != PointerButton::primary || event.action != PointerAction::down) return;
    event.handled = true;
    Window* const host = attached_window();
    if (host != nullptr) static_cast<void>((*host).request_focus(shared_from_this()));
    if (dropped_down_) {
        close_drop_down();
    } else {
        open_drop_down();
    }
}

void LookupComboBox::move_highlight(std::ptrdiff_t delta, bool to_edge) {
    if (filtered_.empty()) return;
    const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(filtered_.size()) - 1;
    std::ptrdiff_t next = 0;
    if (to_edge) {
        next = delta < 0 ? 0 : last;
    } else if (highlighted_) {
        next = std::clamp(static_cast<std::ptrdiff_t>(*highlighted_) + delta, std::ptrdiff_t{0}, last);
    } else {
        next = delta < 0 ? last : 0;
    }
    highlighted_ = static_cast<std::size_t>(next);
    if (list_) (*list_).reveal(*highlighted_);
}

void LookupComboBox::step_selection(std::ptrdiff_t delta) {
    if (rows_.empty()) return;
    const std::optional<std::size_t> chosen = row_of(selected_key.read());
    const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(rows_.size()) - 1;
    std::ptrdiff_t next = 0;
    if (chosen) next = std::clamp(static_cast<std::ptrdiff_t>(*chosen) + delta, std::ptrdiff_t{0}, last);
    choose_row(static_cast<std::size_t>(next));
}

void LookupComboBox::choose_row(std::size_t row) {
    if (row >= rows_.size()) return;
    if (selected_key.read() != rows_[row].key) selected_key = rows_[row].key;
}

void LookupComboBox::on_key(KeyEvent& event) {
    if (event.action != KeyAction::down) return;
    const bool alt = has_modifier(event.modifiers, Modifier::alt);
    const std::uint32_t key = event.physical_key;
    if (key == PhysicalKey::f4 || (alt && (key == PhysicalKey::down || key == PhysicalKey::up))) {
        event.handled = true;
        if (!dropped_down_) {
            open_drop_down();
            return;
        }
        if (key == PhysicalKey::up && highlighted_) choose_row(filtered_[*highlighted_]);
        close_drop_down();
        return;
    }
    if (!dropped_down_) {
        if (key == PhysicalKey::up || key == PhysicalKey::left) {
            step_selection(-1);
            event.handled = true;
        } else if (key == PhysicalKey::down || key == PhysicalKey::right) {
            step_selection(1);
            event.handled = true;
        } else if (key == PhysicalKey::home && !rows_.empty()) {
            choose_row(0U);
            event.handled = true;
        } else if (key == PhysicalKey::end && !rows_.empty()) {
            choose_row(rows_.size() - 1U);
            event.handled = true;
        }
        return;
    }
    const std::ptrdiff_t page =
        list_ ? static_cast<std::ptrdiff_t>(std::max<std::size_t>(1U, (*list_).visible_rows() - 1U)) : 1;
    bool recognized = true;
    switch (key) {
    case PhysicalKey::up: move_highlight(-1, false); break;
    case PhysicalKey::down: move_highlight(1, false); break;
    case PhysicalKey::page_up: move_highlight(-page, false); break;
    case PhysicalKey::page_down: move_highlight(page, false); break;
    case PhysicalKey::home: move_highlight(-1, true); break;
    case PhysicalKey::end: move_highlight(1, true); break;
    case PhysicalKey::enter:
        if (highlighted_) choose_row(filtered_[*highlighted_]);
        close_drop_down();
        break;
    case PhysicalKey::escape: close_drop_down(); break;
    case PhysicalKey::backspace:
        if (!filter_.empty()) {
            filter_.resize(without_last_scalar(filter_));
            refilter();
        }
        break;
    default: recognized = false; break;
    }
    if (recognized) {
        event.handled = true;
        invalidate(Dirty::paint | Dirty::semantics);
    }
}

void LookupComboBox::on_text_input(TextInputEvent& event) {
    if (event.composing || event.text_utf8.empty() || !validate_utf8(event.text_utf8).valid()) return;
    // Control characters arrive as keys; only printable text filters.
    const unsigned char first = static_cast<unsigned char>(event.text_utf8.front());
    if (first < 0x20U || first == 0x7FU) return;
    event.handled = true;
    if (!dropped_down_) open_drop_down();
    if (!dropped_down_) return;
    filter_ += event.text_utf8;
    refilter();
    invalidate(Dirty::paint | Dirty::semantics);
}

void LookupComboBox::on_focus_changed(bool focused) {
    focused_ = focused;
    if (!focused) close_drop_down();
    invalidate(Dirty::paint | Dirty::semantics);
}

void LookupComboBox::open_drop_down() {
    require_mutable();
    Window* const host = attached_window();
    if (dropped_down_ || host == nullptr) return;
    filter_.clear();
    refilter();
    double width = 0.0;
    for (const LookupColumn& column : columns_) width += column.width;
    const Rect field = absolute_bounds();
    width = std::max(width, field.width);
    const std::size_t shown_rows =
        std::max<std::size_t>(1U, std::min<std::size_t>(rows_.size(), max_drop_down_rows.read()));
    const double height = header_height + row_height * static_cast<double>(shown_rows) + 2.0;
    AnchoredOverlayPlacement placement{};
    placement.preferred_size = Size{width, height};
    const std::string prefix(stable_id().value());
    layer_ = make_control<AnchoredOverlayLayer>(StableId(prefix + ".overlay.layer"), shared_from_this(), placement);
    list_ = make_control<List>(StableId(prefix + ".overlay.list"), *this);
    (*layer_).set_content(list_);
    overlay_ = (*host).open_overlay(shared_from_this(), layer_);
    activation_ = (*list_).row_activated().subscribe(
        *this, Delegate<std::size_t>::bind<LookupComboBox, &LookupComboBox::on_row_activated>(*this));
    dismissal_ = (*layer_).dismiss_requested().subscribe(
        *this, Delegate<OverlayDismissReason>::bind<LookupComboBox, &LookupComboBox::on_dismiss>(*this));
    if (Event<>* revoked = overlay_.closed_event()) {
        overlay_revoked_ = (*revoked).subscribe(
            *this, Delegate<>::bind<LookupComboBox, &LookupComboBox::on_overlay_revoked>(*this));
    }
    dropped_down_ = true;
    if (highlighted_) (*list_).reveal(*highlighted_);
    invalidate(Dirty::paint | Dirty::semantics);
    drop_down_changed_.emit(true);
}

void LookupComboBox::release_overlay() noexcept {
    activation_.disconnect();
    dismissal_.disconnect();
    overlay_revoked_.disconnect();
    if (layer_) (*layer_).clear_content();
    overlay_.disconnect();
    list_.reset();
    layer_.reset();
}

void LookupComboBox::close_drop_down() {
    if (!dropped_down_) return;
    dropped_down_ = false;
    release_overlay();
    filter_.clear();
    refilter();
    invalidate(Dirty::paint | Dirty::semantics);
    drop_down_changed_.emit(false);
}

void LookupComboBox::on_row_activated(std::size_t position) {
    if (position >= filtered_.size()) return;
    choose_row(filtered_[position]);
    close_drop_down();
}

void LookupComboBox::on_dismiss(OverlayDismissReason) {
    close_drop_down();
}

void LookupComboBox::on_overlay_revoked() {
    if (!dropped_down_) return;
    dropped_down_ = false;
    activation_.disconnect();
    dismissal_.disconnect();
    overlay_revoked_.disconnect();
    list_.reset();
    layer_.reset();
    filter_.clear();
    refilter();
    invalidate(Dirty::paint | Dirty::semantics);
    drop_down_changed_.emit(false);
}

SemanticDescriptor LookupComboBox::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::combo_box;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.value = shown_text();
    descriptor.has_popup = true;
    if (dropped_down_) descriptor.states |= SemanticState::expanded;
    descriptor.states |= SemanticState::read_only;
    descriptor.actions = {SemanticAction::focus, dropped_down_ ? SemanticAction::collapse : SemanticAction::expand};
    descriptor.exposed = true;
    return descriptor;
}

bool LookupComboBox::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::expand && !dropped_down_) {
        open_drop_down();
        return dropped_down_;
    }
    if (action == SemanticAction::collapse && dropped_down_) {
        close_drop_down();
        return true;
    }
    const bool handled = Control::on_semantic_action(action, value);
    return handled;
}

const ControlAnatomy& LookupComboBox::anatomy() const noexcept {
    return lookup_anatomy;
}

void LookupComboBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Placement placed = placement();
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.well = PartGeometry::from_bounds(placed.well);
    record.label = PartGeometry::from_bounds(placed.label);
    record.actuator = PartGeometry::from_bounds(placed.actuator);
    record.glyph = PartGeometry::from_bounds(placed.glyph);
    if (focused_ || dropped_down_) record.focus = PartGeometry::from_bounds(placed.well);
    write_parts(anatomy(), lookup_part_fields, record, geometry);
}

LookupComboBox::Parts LookupComboBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), lookup_part_fields, published.parts(), record);
    return record;
}

void LookupComboBox::on_dispose() noexcept {
    dropped_down_ = false;
    release_overlay();
    Control::on_dispose();
}

// -- property hooks --------------------------------------------------------------

void LookupComboBox::prepare_write(const decltype(selected_key)&, std::string& incoming) {
    require_mutable();
    if (!incoming.empty() && !row_of(incoming)) {
        throw std::invalid_argument("LookupComboBox selected key names no row");
    }
}

void LookupComboBox::written(const decltype(selected_key)&) {
    invalidate(Dirty::paint | Dirty::semantics);
    selected_key_changed_.emit(selected_key.read());
}

void LookupComboBox::prepare_write(const decltype(display_column)&, std::uint32_t&) {
    require_mutable();
}

void LookupComboBox::written(const decltype(display_column)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

// property_codemod begin LookupComboBox (tools/property_families/lookup_combo_box.json; generated, do not edit)

namespace {

PropertyDescriptor lookup_combo_box_selected_key_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "selected_key";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Behavior";
    descriptor.description = "The key of the chosen row; empty for none.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_lookup_combo_box_selected_key() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        lookup_combo_box_selected_key_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor lookup_combo_box_display_column_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "display_column";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Appearance";
    descriptor.description = "The column the field shows.";
    descriptor.default_value = BindingValue{std::uint64_t{0U}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_lookup_combo_box_display_column() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        lookup_combo_box_display_column_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor lookup_combo_box_max_drop_down_rows_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "max_drop_down_rows";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Behavior";
    descriptor.description = "The rows the open list shows before it scrolls.";
    descriptor.default_value = BindingValue{std::uint64_t{8U}};
    descriptor.invalidation_effects = Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_lookup_combo_box_max_drop_down_rows() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        lookup_combo_box_max_drop_down_rows_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, LookupComboBox::lookup_combo_box_property_count>& LookupComboBox::lookup_combo_box_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"selected_key", &describe_lookup_combo_box_selected_key,
         &read_cell_entry<LookupComboBox, decltype(&LookupComboBox::selected_key), &LookupComboBox::selected_key>,
         &write_cell_entry<LookupComboBox, decltype(&LookupComboBox::selected_key), &LookupComboBox::selected_key>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<LookupComboBox, decltype(&LookupComboBox::selected_key_changed_), &LookupComboBox::selected_key_changed_>},
        {"display_column", &describe_lookup_combo_box_display_column,
         &read_cell_entry<LookupComboBox, decltype(&LookupComboBox::display_column), &LookupComboBox::display_column>,
         &write_cell_entry<LookupComboBox, decltype(&LookupComboBox::display_column), &LookupComboBox::display_column>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"max_drop_down_rows", &describe_lookup_combo_box_max_drop_down_rows,
         &read_cell_entry<LookupComboBox, decltype(&LookupComboBox::max_drop_down_rows), &LookupComboBox::max_drop_down_rows>,
         &write_cell_entry<LookupComboBox, decltype(&LookupComboBox::max_drop_down_rows), &LookupComboBox::max_drop_down_rows>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "LookupComboBox's property table names a property twice");
    static const std::array<PropertyEntry, lookup_combo_box_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> LookupComboBox::property_table() const noexcept {
    const std::array<PropertyEntry, lookup_combo_box_property_count>& entries = lookup_combo_box_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void LookupComboBox::prepare_write(const decltype(max_drop_down_rows)&, std::uint32_t& incoming) {
    require_mutable();
    if (incoming == 0U || incoming > 64U) throw std::invalid_argument("LookupComboBox drop-down rows must be from 1 to 64");
}

void LookupComboBox::written(const decltype(max_drop_down_rows)&) {
    invalidate(Dirty::semantics);
}

// property_codemod end LookupComboBox

namespace {

// The events LookupComboBox adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> lookup_combo_box_interactions{{
    {.name = "selected_key_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "drop_down_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

void LookupComboBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), lookup_combo_box_interactions.begin(), lookup_combo_box_interactions.end());
}

} // namespace gui_forms
