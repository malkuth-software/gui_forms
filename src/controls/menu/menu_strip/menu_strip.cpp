#include "gui_forms/controls/menu_strip.hpp"

#include "../menu_utilities.hpp"
#include "../../frame_chrome/frame_chrome.hpp"
#include "../../tool_strip/strip_merge.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace gui_forms {

using menu_detail::menu_display_text;
using menu_detail::validate_specs;

namespace {

constexpr double menu_strip_height = 24.0;

// The maximized MDI child's buttons at the strip's end (logical units,
// scaled with the text): 16-unit buttons 1 unit apart, 3 units from the end,
// with 9-unit glyphs, as a document frame's caption.
constexpr double frame_button_side = 16.0;
constexpr double frame_button_gap = 1.0;
constexpr double frame_button_end_inset = 3.0;
constexpr double frame_glyph_side = 9.0;
constexpr std::array<MenuStripFrameButton, 3> frame_buttons{
    MenuStripFrameButton::minimize, MenuStripFrameButton::restore, MenuStripFrameButton::close};
constexpr std::array<std::string_view, 3> frame_button_names{"frame-minimize", "frame-restore", "frame-close"};
constexpr std::array<std::string_view, 3> frame_button_labels{"Minimize", "Restore", "Close"};

// The window list's rows (MenuStripItemSpec::window_list): their item IDs
// and the most rows before "More Windows...".
constexpr std::string_view window_list_prefix = "mdi-window-list.";
constexpr std::size_t window_list_rows = 9U;

std::uint8_t frame_code(MenuStripFrameButton button) noexcept {
    const std::uint8_t code = static_cast<std::uint8_t>(static_cast<std::uint8_t>(button) + 1U);
    return code;
}

// A title as a menu row's text: '&' doubled, so it shows and names no
// mnemonic.
std::string escaped_title(std::string_view title) {
    std::string escaped{};
    escaped.reserve(title.size());
    for (const char character : title) {
        escaped.push_back(character);
        if (character == '&') escaped.push_back('&');
    }
    return escaped;
}

// Appends the window list to a menu about to open: a separator after the
// menu's own rows, one row per MDI child of `window`'s document area (the
// active one checked) up to nine, and "More Windows..." past them. Nothing
// without children.
void append_window_list(Window& window, std::vector<MenuItemSpec>& menu) {
    const BorrowedSpan<DockPanelId> children = window.mdi_children();
    if (children.size() == 0U) return;
    WindowDocumentArea* const area = window.document_area();
    const stxlib::Optional<DockPanelId> active = window.active_mdi_child();
    if (!menu.empty()) {
        menu.push_back(MenuItemSpec(std::string(window_list_prefix) + "separator", MenuItemKind::separator));
    }
    const std::size_t shown = std::min(children.size(), window_list_rows);
    for (std::size_t index = 0U; index < shown; ++index) {
        const DockPanelId& child = children[index];
        std::string title = child.value;
        if (area != nullptr) {
            const BorrowedText named = (*area).document_title(child);
            if (!named.empty()) title = std::string(named.data(), named.size());
        }
        const std::string number = std::to_string(index + 1U);
        MenuItemSpec row(std::string(window_list_prefix) + number, MenuItemKind::check, {},
                         "&" + number + " " + escaped_title(title));
        row.checked = active.present() && *active.value() == child;
        menu.push_back(std::move(row));
    }
    if (children.size() > window_list_rows) {
        menu.push_back(MenuItemSpec(std::string(window_list_prefix) + "more", MenuItemKind::command, {},
                                    "&More Windows..."));
    }
}

// The window list row an item ID names: its child's position, or none (not
// a row, or "More Windows...").
stxlib::Optional<std::size_t> window_list_row(std::string_view item_id) noexcept {
    if (item_id.size() <= window_list_prefix.size() ||
        item_id.substr(0U, window_list_prefix.size()) != window_list_prefix) {
        return stxlib::Optional<std::size_t>::none();
    }
    const std::string_view number = item_id.substr(window_list_prefix.size());
    if (number.size() != 1U || number[0] < '1' || number[0] > '9') return stxlib::Optional<std::size_t>::none();
    return stxlib::Optional<std::size_t>::of(static_cast<std::size_t>(number[0] - '1'));
}

} // namespace

MenuStrip::MenuStrip(StableId stable_id)
    : Control(std::move(stable_id)),
      overlay_(std::make_unique<ContextMenu>(
          std::string((*this).stable_id().value()) + ".menu")) {
    focusable = true;
    cursor = stxlib::Optional<CursorKind>::of(CursorKind::arrow);
    set_paint_plane(PaintPlane::control);
    // This owner retains the overlay and subscriptions. Bound handlers borrow
    // this MenuStrip; disposal closes the overlay before owner teardown.
    (*overlay_).set_root_navigation_handler(
        detail::BoundMemberFunction<bool (MenuStrip::*)(int)>(
            *this, &MenuStrip::navigate_root));
    (*overlay_).set_outside_pointer_handler(
        detail::BoundMemberFunction<
            bool (MenuStrip::*)(const PointerEvent&)>(
                *this, &MenuStrip::handle_overlay_pointer));
    overlay_invoked_ = (*overlay_).item_invoked().subscribe(
        *this, Delegate<const MenuItemInvocation&>::bind<
            MenuStrip, &MenuStrip::on_overlay_item_invoked>(*this));
    overlay_changed_ = (*overlay_).open_changed().subscribe(
        *this, Delegate<bool>::bind<
            MenuStrip, &MenuStrip::on_overlay_open_changed>(*this));
}

void MenuStrip::on_overlay_item_invoked(
    const MenuItemInvocation& invocation) {
    const std::size_t* const active = active_index_.value();
    if (active == nullptr || *active >= items_.size()) return;
    // A window list row activates its child instead.
    const stxlib::Optional<std::size_t> row = window_list_row(invocation.item_id);
    if (row.present() && items_[*active].window_list) {
        Window* const host = attached_window();
        if (host == nullptr) return;
        const BorrowedSpan<DockPanelId> children = (*host).mdi_children();
        if (*row.value() >= children.size()) return;
        const DockPanelId child = children[*row.value()];
        static_cast<void>((*host).activate_mdi_child(child));
        return;
    }
    MenuStripInvocation forwarded{
        std::string(stable_id().value()),
        items_[*active].stable_id, invocation};
    item_invoked_.emit(forwarded);
}

void MenuStrip::on_overlay_open_changed(bool open_state) {
    if (open_state || switching_ || active_index_.absent()) return;
    active_index_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(open_changed_, active_index_);
}

MenuStrip::~MenuStrip() {
    close();
}

void MenuStrip::set_items(std::vector<MenuStripItemSpec> items) {
    apply_items(std::move(items), false);
}

void MenuStrip::apply_items(std::vector<MenuStripItemSpec> items, bool merged) {
    require_mutable();
    std::unordered_set<std::string> identities{};
    for (const gui_forms::MenuStripItemSpec& item : items) {
        const std::pair<std::unordered_set<std::string>::iterator, bool> insertion =
            identities.insert(item.stable_id);
        if (!valid_stable_id(item.stable_id) ||
            item.text.empty() || !validate_utf8(item.text).valid() ||
            (item.items.empty() && !merged) || !insertion.second) {
            throw std::invalid_argument(
                "MenuStrip items require unique IDs following the StableId segment rule, text, and menu contents");
        }
        std::unordered_set<std::string> child_identities{};
        std::size_t count{};
        validate_specs(item.items, child_identities, 1U, count, merged);
    }
    close();
    items_ = std::move(items);
    bool selection_retained = false;
    for (const MenuStripItemSpec& item : items_) {
        if (item.stable_id == selected_item_id_ && item.visible) {
            selection_retained = true;
            break;
        }
    }
    if (!selection_retained) selected_item_id_.clear();
    hot_index_.reset();
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

std::string MenuStrip::read_view(const decltype(selected_item_id)&) const {
    std::string id = selected_item_id_;
    return id;
}

void MenuStrip::write_view(const decltype(selected_item_id)&, std::string incoming) {
    require_mutable();
    const std::string_view stable_id = incoming;
    if (!stable_id.empty()) {
        bool visible = false;
        for (const MenuStripItemSpec& item : items_) {
            if (item.stable_id == stable_id) {
                visible = item.visible;
                break;
            }
        }
        if (!visible) {
            throw std::out_of_range(
                "MenuStrip selected item must identify a visible top-level item");
        }
    }
    if (selected_item_id_ == stable_id) return;
    selected_item_id_ = std::move(incoming);
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
}

void MenuStrip::prepare_write(const decltype(item_padding)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 4.0 || incoming > 64.0) {
        throw std::invalid_argument(
            "MenuStrip item padding must be finite and between 4 and 64");
    }
}

// An open menu was placed for the old item extents, so it closes.
void MenuStrip::written(const decltype(item_padding)&) {
    close();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

bool MenuStrip::open(const std::size_t index, const MenuOpenMode mode) {
    require_mutable();
    if (mode != MenuOpenMode::keyboard && mode != MenuOpenMode::pointer)
        throw std::invalid_argument("MenuStrip opening mode is invalid");
    if (index >= items_.size() || !items_[index].visible ||
        !items_[index].enabled || !attached_window()) return false;
    if (holds(active_index_, index) && (*overlay_).is_open()) return true;
    // A merge may have left the menu empty, or holding only empty submenus.
    std::vector<MenuItemSpec> menu = menu_detail::prune_empty_submenus(items_[index].items);
    if (items_[index].window_list) {
        Window* const host = attached_window();
        append_window_list(*host, menu);
    }
    if (menu.empty()) return false;
    // A menu whose commands are all hidden would show nothing: refuse before
    // the strip changes (A-gf-controls-06-F3).
    if (menu_detail::snapshot_items(menu).empty()) return false;
    switching_ = (*overlay_).is_open();
    if (switching_) (*overlay_).close();
    active_index_.set(index);
    hot_index_.set(index);
    (*overlay_).set_items(std::move(menu));
    const std::vector<Rect> bounds = item_bounds();
    const Rect absolute = absolute_bounds();
    // The drop-down hangs from the item's reading-start corner: its left
    // edge, or its right edge right to left (the menu then hangs leftward).
    const bool right_to_left = effective_layout_direction() == LayoutDirection::right_to_left;
    const double anchor_x = right_to_left ? bounds[index].x + bounds[index].width : bounds[index].x;
    (*overlay_).show(shared_from_this(),
                 {absolute.x + anchor_x,
                  absolute.y + bounds[index].y + bounds[index].height - 1.0}, mode);
    switching_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(open_changed_, active_index_);
    return true;
}

void MenuStrip::prepare_write(const decltype(font)&, FontSpec& incoming) {
    require_mutable();
    if (!valid_font_spec(incoming)) {
        throw std::invalid_argument("MenuStrip font specification is invalid");
    }
    set_font_from_code(true);
}

void MenuStrip::written(const decltype(font)&) {
    close();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

void MenuStrip::close() noexcept {
    if (!overlay_) return;
    (*overlay_).close();
}

bool MenuStrip::is_open() const noexcept {
    const bool result = overlay_ && (*overlay_).is_open();
    return result;
}

Size MenuStrip::measure(Size available) {
    const double preferred = preferred_height();
    const double height = std::min(available.height, preferred);
    const Size result{.width = available.width, .height = height};
    return result;
}

double MenuStrip::preferred_height() const {
    FontSpec item_font = text_font(font.read());
    item_font.weight = std::max<std::uint16_t>(item_font.weight, 700U);
    const ResolvedTextLayout metrics = resolve_text_layout_utf8("Mg", item_font);
    const double text_scale = effective_text_scale();
    const double result = std::max(menu_strip_height * text_scale,
                    metrics.logical_size.height + 10.0 * text_scale);
    return result;
}

std::vector<Rect> MenuStrip::item_bounds() const {
    std::vector<Rect> result(items_.size());
    const FontSpec normal_font = text_font(font.read());
    FontSpec selected_font = normal_font;
    selected_font.weight = std::max<std::uint16_t>(selected_font.weight, 700U);
    const double scale = effective_text_scale();
    const double height = std::max(preferred_height(), committed_arranged_bounds().height);
    double x{};
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!items_[index].visible) continue;
        const std::string display = use_mnemonic.read()
            ? menu_display_text(items_[index].text) : items_[index].text;
        const ResolvedTextLayout normal = resolve_text_layout_utf8(display, normal_font);
        const ResolvedTextLayout selected = resolve_text_layout_utf8(display, selected_font);
        const double text_width = std::max(normal.logical_size.width, selected.logical_size.width);
        const double width = std::max(42.0 * scale,
            item_padding.read() * 2.0 * scale + text_width);
        result[index] = {x, 0.0, width, height};
        x += width;
    }
    // A right-to-left strip runs its items from the right edge (WinForms'
    // MenuStrip under RightToLeft).
    if (effective_layout_direction() == LayoutDirection::right_to_left) {
        const Rect frame{0.0, 0.0, committed_arranged_bounds().width, height};
        for (std::size_t index = 0; index < result.size(); ++index) {
            if (items_[index].visible) result[index] = horizontally_mirrored(result[index], frame);
        }
    }
    return result;
}

stxlib::Optional<std::size_t> MenuStrip::index_at(Point window_point) const {
    const Rect absolute = absolute_bounds();
    if (!absolute.contains(window_point)) return stxlib::Optional<std::size_t>::none();
    const Point local{window_point.x - absolute.x, window_point.y - absolute.y};
    const std::vector<Rect> bounds = item_bounds();
    for (std::size_t index = 0; index < bounds.size(); ++index) {
        if (items_[index].visible && bounds[index].contains(local)) return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

bool MenuStrip::holds(const stxlib::Optional<std::size_t>& held, std::size_t index) noexcept {
    const std::size_t* const current = held.value();
    const bool result = current != nullptr && *current == index;
    return result;
}

stxlib::Optional<std::size_t> MenuStrip::next_enabled(
    std::size_t start, int direction) const {
    if (items_.empty()) return stxlib::Optional<std::size_t>::none();
    for (std::size_t step = 1U; step <= items_.size(); ++step) {
        const std::ptrdiff_t raw = static_cast<std::ptrdiff_t>(start) +
            static_cast<std::ptrdiff_t>(direction) *
                static_cast<std::ptrdiff_t>(step);
        const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(items_.size());
        const std::size_t index = static_cast<std::size_t>((raw % count + count) % count);
        if (items_[index].visible && items_[index].enabled) return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

bool MenuStrip::navigate_root(int direction) {
    const std::size_t* const active = active_index_.value();
    if (active == nullptr) return false;
    const stxlib::Optional<std::size_t> found = next_enabled(*active, direction);
    const std::size_t* const next = found.value();
    if (next == nullptr) return false;
    const bool opened = open(*next);
    return opened;
}

bool MenuStrip::handle_overlay_pointer(const PointerEvent& event) {
    if (event.action != PointerAction::move &&
        event.action != PointerAction::down) return false;
    const stxlib::Optional<std::size_t> found = index_at(event.position);
    const std::size_t* const index = found.value();
    if (index == nullptr || !items_[*index].enabled) return false;
    set_hot(found);
    if (event.action == PointerAction::move && !holds(active_index_, *index)) {
        const bool opened = open(*index, MenuOpenMode::pointer);
        return opened;
    }
    if (event.action == PointerAction::down) {
        if (holds(active_index_, *index)) close();
        else static_cast<void>(open(*index, MenuOpenMode::pointer));
        return true;
    }
    return true;
}

void MenuStrip::set_hot(stxlib::Optional<std::size_t> index) {
    if (hot_index_ == index) return;
    hot_index_ = index;
    invalidate(Dirty::paint | Dirty::semantics);
}

void MenuStrip::on_paint(Painter& painter, Rect) {
    const Rect surface{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    // The strip's face and each item's plate are parts (the menu_strip
    // anatomy's `face` and `menu_item`), so a theme's `menu-strip::part(...)`
    // rules select them; an item is keyed by its stable ID.
    PartPaint face;
    face.family = ControlVisualRole::panel;
    face.role = PartRole::face;
    face.bounds = surface;
    face.key.set(PartKey{"face", {}, {}});
    paint_part(painter, *this, face);
    PartPaint rule;
    rule.family = ControlVisualRole::panel;
    rule.role = PartRole::divider;
    rule.bounds = {0.0, std::max(0.0, surface.height - 1.5), surface.width, 1.0};
    paint_part(painter, *this, rule);
    const std::vector<Rect> bounds = item_bounds();
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!items_[index].visible) continue;
        const Rect item = bounds[index];
        const bool active = holds(active_index_, index);
        const bool hot = holds(hot_index_, index);
        const bool selected = selected_item_id_ == items_[index].stable_id;
        // An open item shows pressed.
        AnatomyStateSet plate_states;
        if (hot) plate_states.insert(AnatomyState::hot);
        if (active) plate_states.insert(AnatomyState::pressed);
        if (selected) plate_states.insert(AnatomyState::selected);
        if (focused_ && hot) plate_states.insert(AnatomyState::focus_visible);
        if (!items_[index].enabled) plate_states.insert(AnatomyState::disabled);
        // Painted in every state, keyed by the item: the built-in rule's
        // dithered bulge is flat at rest and rises while the item is hot,
        // and an open (pressed) or selected item shows the plate.
        PartPaint plate;
        plate.family = ControlVisualRole::menu_item;
        plate.role = PartRole::face;
        plate.bounds = {item.x + 1.0, 1.0, item.width - 2.0,
                        std::max(0.0, item.height - 2.0)};
        plate.states = plate_states;
        plate.key.set(PartKey{"menu_item", items_[index].stable_id, stxlib::Optional<std::size_t>::of(index)});
        paint_part(painter, *this, plate);
        FontSpec item_font = text_font(font.read());
        if (active || selected) {
            item_font.weight = std::max<std::uint16_t>(item_font.weight, 700U);
        }
        const std::string text = use_mnemonic.read() ? menu_display_text(items_[index].text)
                                               : items_[index].text;
        const double text_x = item.x + item_padding.read() * effective_text_scale();
        PartPaint label;
        label.family = ControlVisualRole::menu_item;
        label.role = PartRole::label;
        label.states = plate_states;
        label.text = lend_paint_text(text);
        label.font = item_font;
        paint_text_part_at(painter, *this, label,
                           {text_x, item.y + std::max(item_font.size, item.height * 0.5 + item_font.size * 0.35)},
                           std::max(0.0, item.x + item.width - text_x));
    }
    const std::size_t* const hot_item = hot_index_.value();
    if (focused_ && active_index_.absent() && hot_item != nullptr) {
        const Rect item = bounds[*hot_item];
        PartPaint ring;
        ring.family = ControlVisualRole::menu_item;
        ring.role = PartRole::focus;
        ring.bounds = {item.x + 2.0, 2.0, std::max(0.0, item.width - 4.0),
                       std::max(0.0, item.height - 4.0)};
        ring.states.insert(AnatomyState::hot);
        ring.states.insert(AnatomyState::focus_visible);
        paint_part(painter, *this, ring);
    }
    if (!frame_buttons_) return;
    // A maximized MDI child's buttons, in the in-window frames' shared
    // chrome (frame_chrome.hpp).
    const double glyph = frame_glyph_side * effective_text_scale();
    const std::array<frame_chrome::FrameButtonGlyph, 3> glyphs{frame_chrome::FrameButtonGlyph::minimize,
                                                               frame_chrome::FrameButtonGlyph::restore,
                                                               frame_chrome::FrameButtonGlyph::close};
    for (std::size_t index = 0U; index < frame_buttons.size(); ++index) {
        const Rect button = document_frame_button_bounds(frame_buttons[index]);
        AnatomyStateSet states;
        if (frame_hot_ == frame_code(frame_buttons[index])) states.insert(AnatomyState::hot);
        if (frame_pressed_ == frame_code(frame_buttons[index])) states.insert(AnatomyState::pressed);
        if (!effectively_enabled()) states.insert(AnatomyState::disabled);
        const Rect mark{button.x + (button.width - glyph) * 0.5, button.y + (button.height - glyph) * 0.5, glyph,
                        glyph};
        frame_chrome::paint_frame_button(painter, *this, button, mark, glyphs[index], states);
    }
}

// -- the multiple document interface ---------------------------------------------

stxlib::Optional<std::size_t> MenuStrip::mdi_window_list_item() const noexcept {
    for (std::size_t index = 0U; index < items_.size(); ++index) {
        if (items_[index].visible && items_[index].window_list) return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

void MenuStrip::set_document_frame_buttons(bool shown) {
    require_mutable();
    if (frame_buttons_ == shown) return;
    frame_buttons_ = shown;
    frame_hot_ = 0U;
    frame_pressed_ = 0U;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// The buttons sit at the strip's end, centred across it: minimize, restore,
// close toward the end (mirrored right to left, at the start).
Rect MenuStrip::document_frame_button_bounds(MenuStripFrameButton button) const {
    if (!frame_buttons_) return Rect{};
    const double scale = effective_text_scale();
    const Rect frame{0.0, 0.0, committed_arranged_bounds().width, committed_arranged_bounds().height};
    const double side = std::min(frame_button_side * scale, frame.height);
    const double gap = frame_button_gap * scale;
    const double top = (frame.height - side) * 0.5;
    const std::size_t from_end = frame_buttons.size() - 1U - static_cast<std::size_t>(button);
    const double right = frame.width - frame_button_end_inset * scale - static_cast<double>(from_end) * (side + gap);
    Rect bounds{right - side, top, side, side};
    if (effective_layout_direction() == LayoutDirection::right_to_left) bounds = horizontally_mirrored(bounds, frame);
    return bounds;
}

std::uint8_t MenuStrip::frame_button_at(Point local) const {
    if (!frame_buttons_) return 0U;
    for (const MenuStripFrameButton button : frame_buttons) {
        if (document_frame_button_bounds(button).contains(local)) return frame_code(button);
    }
    return 0U;
}

void MenuStrip::set_frame_hot(std::uint8_t button) {
    if (frame_hot_ == button) return;
    frame_hot_ = button;
    invalidate(Dirty::paint);
}

void MenuStrip::on_mouse_up(MouseEventArgs& arguments) {
    if (arguments.button == PointerButton::primary && frame_pressed_ != 0U) {
        const std::uint8_t pressed = frame_pressed_;
        frame_pressed_ = 0U;
        invalidate(Dirty::paint);
        arguments.handled = true;
        if (frame_button_at(arguments.location) == pressed && effectively_enabled()) {
            const Control::Ptr lifetime = shared_from_this();
            const MenuStripFrameButton button = static_cast<MenuStripFrameButton>(pressed - 1U);
            frame_button_pressed_.emit(button);
        }
    }
    Control::on_mouse_up(arguments);
}

void MenuStrip::on_mouse_move(MouseEventArgs& arguments) {
    set_frame_hot(frame_button_at(arguments.location));
    const stxlib::Optional<std::size_t> found = index_at(arguments.window_position);
    const std::size_t* const index = found.value();
    set_hot(found);
    if (is_open() && index != nullptr && !holds(active_index_, *index) &&
        items_[*index].enabled) {
        static_cast<void>(open(*index, MenuOpenMode::pointer));
    }
    arguments.handled = hot_index_.present();
    Control::on_mouse_move(arguments);
}

void MenuStrip::on_mouse_leave() {
    set_frame_hot(0U);
    if (!is_open()) set_hot(stxlib::Optional<std::size_t>::none());
    Control::on_mouse_leave();
}

void MenuStrip::on_mouse_down(MouseEventArgs& arguments) {
    const std::uint8_t frame_button = arguments.button == PointerButton::primary
        ? frame_button_at(arguments.location) : std::uint8_t{0U};
    if (frame_button != 0U && effectively_enabled()) {
        close();
        frame_pressed_ = frame_button;
        invalidate(Dirty::paint);
        arguments.handled = true;
    } else if (arguments.button == PointerButton::primary) {
        const stxlib::Optional<std::size_t> found = index_at(arguments.window_position);
        const std::size_t* const index = found.value();
        if (index != nullptr && items_[*index].enabled) {
            if (holds(active_index_, *index) && is_open()) close();
            else static_cast<void>(open(*index, MenuOpenMode::pointer));
            arguments.handled = true;
        }
    }
    Control::on_mouse_down(arguments);
}

void MenuStrip::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled || !focused_) return;
    stxlib::Optional<std::size_t> held = active_index_.present() ? active_index_ : hot_index_;
    if (held.absent()) {
        for (std::size_t index = 0; index < items_.size(); ++index) {
            if (items_[index].visible && items_[index].enabled) {
                held.set(index);
                break;
            }
        }
    }
    const std::size_t* const current = held.value();
    if (current == nullptr) return;
    if (arguments.physical_key == PhysicalKey::left ||
        arguments.physical_key == PhysicalKey::right) {
        // Toward the reading end moves on.
        const std::uint32_t onward = effective_layout_direction() == LayoutDirection::right_to_left
            ? PhysicalKey::left : PhysicalKey::right;
        const stxlib::Optional<std::size_t> next = next_enabled(*current,
            arguments.physical_key == onward ? 1 : -1);
        const std::size_t* const next_index = next.value();
        if (next_index != nullptr) {
            if (is_open()) static_cast<void>(open(*next_index));
            else set_hot(next);
        }
    } else if (arguments.physical_key == PhysicalKey::down ||
               arguments.physical_key == PhysicalKey::enter ||
               arguments.physical_key == PhysicalKey::space) {
        static_cast<void>(open(*current));
    } else if (arguments.physical_key == PhysicalKey::escape) {
        close();
    } else {
        return;
    }
    arguments.handled = true;
}

void MenuStrip::on_focus_changed(bool focused) {
    focused_ = focused;
    if (focused_ && hot_index_.absent()) {
        for (std::size_t index = 0; index < items_.size(); ++index) {
            if (items_[index].visible && items_[index].enabled) {
                hot_index_.set(index);
                break;
            }
        }
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

SemanticDescriptor MenuStrip::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::menu_bar;
    descriptor.name = accessible_name.read().empty() ? "Application menu" : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> MenuStrip::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    nodes.reserve(items_.size());
    const Rect absolute = absolute_bounds();
    const std::vector<Rect> bounds = item_bounds();
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!items_[index].visible) continue;
        SemanticNode node{};
        node.stable_id = items_[index].stable_id;
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::menu_bar_item;
        node.name = use_mnemonic.read()
            ? menu_display_text(items_[index].text) : items_[index].text;
        if (use_mnemonic.read()) node.access_key = mnemonic_access_key(items_[index].text);
        node.bounds = {absolute.x + bounds[index].x, absolute.y + bounds[index].y,
                       bounds[index].width, bounds[index].height};
        node.states = SemanticState::visible | SemanticState::focusable;
        if (effectively_enabled() && items_[index].enabled) {
            node.states |= SemanticState::enabled;
        }
        if (selected_item_id_ == items_[index].stable_id) {
            node.states |= SemanticState::selected;
        }
        if (holds(active_index_, index)) {
            node.states |= SemanticState::expanded;
        }
        if (focused_ && holds(hot_index_, index)) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::focus, SemanticAction::press,
                        SemanticAction::expand, SemanticAction::collapse};
        nodes.push_back(std::move(node));
    }
    if (!frame_buttons_) return nodes;
    // A maximized MDI child's buttons.
    for (std::size_t index = 0U; index < frame_buttons.size(); ++index) {
        const Rect button = document_frame_button_bounds(frame_buttons[index]);
        SemanticNode node{};
        node.stable_id = std::string(stable_id().value()) + "." + std::string(frame_button_names[index]);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::button;
        node.name = std::string(frame_button_labels[index]);
        node.bounds = {absolute.x + button.x, absolute.y + button.y, button.width, button.height};
        node.states = SemanticState::visible;
        if (effectively_enabled()) {
            node.states |= SemanticState::enabled;
            node.actions = {SemanticAction::press};
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

namespace {

// The index of the visible, enabled top-level menu `id` among `items`, or none.
stxlib::Optional<std::size_t> find_top_level(const std::vector<MenuStripItemSpec>& items, std::string_view id) {
    for (std::size_t index = 0U; index < items.size(); ++index) {
        const MenuStripItemSpec& item = items[index];
        if (item.stable_id != id) continue;
        if (!item.visible || !item.enabled) return stxlib::Optional<std::size_t>::none();
        return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

} // namespace

bool MenuStrip::on_semantic_child_action(std::string_view id,
                                         SemanticAction action,
                                         std::string_view) {
    if (frame_buttons_ && action == SemanticAction::press && effectively_enabled()) {
        for (std::size_t index = 0U; index < frame_buttons.size(); ++index) {
            const std::string button_id = std::string(stable_id().value()) + "." + std::string(frame_button_names[index]);
            if (id != button_id) continue;
            const Control::Ptr lifetime = shared_from_this();
            frame_button_pressed_.emit(frame_buttons[index]);
            return true;
        }
    }
    const stxlib::Optional<std::size_t> found = find_top_level(items_, id);
    if (found.absent()) return false;
    const std::size_t index = *found.value();
    if (action == SemanticAction::collapse) {
        if (!holds(active_index_, index)) return false;
        close();
        return true;
    }
    if (action != SemanticAction::focus && action != SemanticAction::press &&
        action != SemanticAction::expand) return false;
    // Focus first: its handlers may replace the items or drop the strip, so
    // the menu is found again after it (commit, then notify; house style 5),
    // and `lifetime` keeps the strip alive across them.
    const Control::Ptr lifetime = shared_from_this();
    if (window()) static_cast<void>((*window()).request_focus(lifetime));
    if (!is_alive()) return true;
    const stxlib::Optional<std::size_t> again = find_top_level(items_, id);
    if (again.absent()) return true;
    set_hot(again);
    if (action != SemanticAction::focus) {
        const bool opened = open(*again.value());
        return opened;
    }
    return true;
}

bool MenuStrip::mnemonic_matches(char32_t character) const noexcept {
    if (!use_mnemonic.read()) return false;
    for (const MenuStripItemSpec& item : items_) {
        if (item.visible && item.enabled &&
            is_mnemonic(character, item.text)) return true;
    }
    return false;
}

bool MenuStrip::process_mnemonic_self(char32_t character) {
    if (!mnemonic_matches(character)) return false;
    std::vector<std::size_t> matches{};
    for (std::size_t index = 0U; index < items_.size(); ++index) {
        if (items_[index].visible && items_[index].enabled &&
            is_mnemonic(character, items_[index].text)) {
            matches.push_back(index);
        }
    }
    if (matches.empty()) return false;
    std::size_t selected = matches.front();
    const stxlib::Optional<std::size_t> held = active_index_.present() ? active_index_ : hot_index_;
    const std::size_t* const current = held.value();
    if (current != nullptr) {
        const std::vector<std::size_t>::iterator found =
            std::find(matches.begin(), matches.end(), *current);
        if (found != matches.end()) {
            const std::vector<std::size_t>::iterator next = std::next(found);
            selected = next == matches.end() ? matches.front() : *next;
        }
    }
    // The focus handlers may replace the items or drop the strip: the menu
    // is found again by its ID after them, and `lifetime` keeps the strip
    // alive across them.
    const std::string selected_id = items_[selected].stable_id;
    const Control::Ptr lifetime = shared_from_this();
    if (window() && !(*window()).request_focus(lifetime)) {
        // The mnemonic is still owned, but prevent-mode validation may reject
        // the focus transaction that would authorize opening the overlay.
        return true;
    }
    if (!is_alive()) return true;
    const stxlib::Optional<std::size_t> again = find_top_level(items_, selected_id);
    if (again.absent()) return true;
    set_hot(again);
    const bool opened = open(*again.value());
    return opened;
}

void MenuStrip::on_dispose() noexcept {
    close();
    if (overlay_) (*overlay_).dispose();
}

namespace {

constexpr std::array<PartField<MenuStrip::Parts>, 5> menu_strip_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}, .states = {}},
     .member = &MenuStrip::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &MenuStrip::Parts::face},
    {.declaration = {.name = "divider", .role = PartRole::divider, .composed_from = {}, .states = {}},
     .member = &MenuStrip::Parts::divider},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
     .member = &MenuStrip::Parts::focus},
    {.declaration = {.name = "gutter", .role = PartRole::well, .composed_from = {}, .states = {}},
     .member = &MenuStrip::Parts::gutter},
}};

constexpr std::array<PartField<MenuStrip::ItemParts>, 3> menu_strip_item_part_fields{{
    {.declaration = {.name = "menu_item", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &MenuStrip::ItemParts::menu_item},
    {.declaration = {.name = "menu_label", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &MenuStrip::ItemParts::menu_label},
    {.declaration = {.name = "menu_focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
     .member = &MenuStrip::ItemParts::menu_focus},
}};

constexpr std::array<StateField<MenuStrip::ItemStates>, 4> menu_strip_item_state_fields{{
    {.state = AnatomyState::hot, .member = &MenuStrip::ItemStates::hot},
    {.state = AnatomyState::open, .member = &MenuStrip::ItemStates::open},
    {.state = AnatomyState::selected, .member = &MenuStrip::ItemStates::selected},
    {.state = AnatomyState::disabled, .member = &MenuStrip::ItemStates::disabled},
}};

constexpr std::array<PartDeclaration, 3> menu_strip_item_parts =
    part_declarations(menu_strip_item_part_fields);
constexpr std::array<AnatomyState, 4> menu_strip_item_states =
    state_declarations(menu_strip_item_state_fields);

constexpr std::array<PartDeclaration, 5> menu_strip_parts =
    part_declarations(menu_strip_part_fields);
constexpr std::array<AnatomyState, 5> menu_strip_states{
    AnatomyState::normal,
    AnatomyState::enabled,
    AnatomyState::disabled,
    AnatomyState::focused,
    AnatomyState::focus_visible,
};

constexpr std::array<ItemSystemDeclaration, 1> menu_strip_item_systems{{
    {.name = "item", .parts = menu_strip_item_parts, .states = menu_strip_item_states,
     .custom_states = {}},
}};

constexpr ControlAnatomy menu_strip_anatomy{
    .control_id = "menu_strip",
    .parts = menu_strip_parts,
    .states = menu_strip_states,
    .custom_states = {},
    .item_systems = menu_strip_item_systems,
};

} // namespace

const ControlAnatomy& MenuStrip::anatomy() const noexcept {
    return menu_strip_anatomy;
}

void MenuStrip::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect surface{0.0, 0.0, committed_arranged_bounds().width,
                       committed_arranged_bounds().height};
    Parts record;
    record.region = PartGeometry::from_bounds(surface);
    record.face = PartGeometry::from_bounds(surface);
    // The one-unit rule along the strip's bottom.
    const Rect rule{0.0, std::max(0.0, surface.height - 1.5), surface.width, 1.0};
    record.divider = PartGeometry::from_bounds(Rect::intersection(rule, surface));
    record.focus = PartGeometry::from_bounds(surface);
    // The gutter belongs to the drop-down panels (ContextMenu's MenuPanel),
    // not to the strip: empty here.
    record.gutter = PartGeometry{};
    write_parts(anatomy(), menu_strip_part_fields, record, geometry);
}

MenuStrip::Parts MenuStrip::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), menu_strip_part_fields, published.parts(), record);
    return record;
}

// As ToolStrip's: an item past the realized ones is refused here rather than
// answered with a past-the-end index the accessors would use.
std::size_t MenuStrip::realized_item_index(std::size_t item) const {
    std::size_t seen = 0U;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!items_[index].visible) continue;
        if (seen == item) return index;
        ++seen;
    }
    throw std::out_of_range("MenuStrip realized item is past the realized items");
}

std::size_t MenuStrip::realized_item_count() const {
    std::size_t count = 0U;
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (items_[index].visible) ++count;
    }
    return count;
}

std::string MenuStrip::realized_item_stable_id(std::size_t item) const {
    return items_[realized_item_index(item)].stable_id;
}

void MenuStrip::publish_realized_item_geometry(std::size_t item,
                                          std::span<PartGeometry> geometry) const {
    ItemParts record;

    const std::size_t index = realized_item_index(item);
    const Rect bounds = item_bounds()[index];
    const bool emphasised = holds(active_index_, index) ||
                            selected_item_id_ == items_[index].stable_id;
    FontSpec item_font = text_font(font.read());
    if (emphasised) {
        item_font.weight = std::max<std::uint16_t>(item_font.weight, 700U);
    }
    const std::string display = use_mnemonic.read()
        ? menu_display_text(items_[index].text) : items_[index].text;
    const double text_width = measure_text_extent_utf8(display, item_font).logical_size.width;
    const double text_x = bounds.x + item_padding.read() * effective_text_scale();
    const Rect label{text_x, bounds.y, text_width, bounds.height};
    // The highlight on_paint lays inside the item, one unit in.
    const Rect face{bounds.x + 1.0, 1.0, std::max(0.0, bounds.width - 2.0),
                    std::max(0.0, bounds.height - 2.0)};
    record.menu_item = PartGeometry::from_bounds(face);
    record.menu_label = PartGeometry::from_bounds(Rect::intersection(label, bounds));
    record.menu_focus = PartGeometry::from_bounds(bounds);
    write_item_parts(anatomy().item_systems.front(), menu_strip_item_part_fields, record, geometry);
}

AnatomyStateSet MenuStrip::realized_item_anatomy_states(std::size_t item) const {
    AnatomyStateSet states;
    const std::size_t index = realized_item_index(item);
    if (holds(hot_index_, index)) {
        states.insert(AnatomyState::hot);
    }
    if (holds(active_index_, index)) {
        states.insert(AnatomyState::open);
    }
    if (selected_item_id_ == items_[index].stable_id) {
        states.insert(AnatomyState::selected);
    }
    if (!items_[index].enabled) {
        states.insert(AnatomyState::disabled);
    }
    return states;
}

std::vector<MenuStrip::Item> MenuStrip::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record;
        record.index = realized_item_index(item);
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), menu_strip_item_part_fields, published.parts(), record.parts);
        read_states(menu_strip_item_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events MenuStrip adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> menu_strip_interactions{{
    {.name = "item_invoked", .payload = "MenuStripInvocation", .mutable_payload = false},
    {.name = "open_changed", .payload = "stxlib::Optional<std::size_t>", .mutable_payload = false},
}};

} // namespace

void MenuStrip::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), menu_strip_interactions.begin(), menu_strip_interactions.end());
}

// property_codemod begin MenuStrip (tools/property_families/menu_strip.json; generated, do not edit)

void MenuStrip::prepare_write(const decltype(use_mnemonic)&, bool&) {
    require_mutable();
}

void MenuStrip::written(const decltype(use_mnemonic)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void MenuStrip::prepare_write(const decltype(allow_merge)&, bool&) {
    require_mutable();
}

void MenuStrip::written(const decltype(allow_merge)&) {}

// property_codemod end MenuStrip

} // namespace gui_forms
