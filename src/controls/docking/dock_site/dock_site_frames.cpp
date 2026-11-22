#include "gui_forms/controls/dock_site.hpp"
#include "dock_site_children.hpp"

#include "dock_site_internal.hpp"
#include "../../frame_chrome/frame_chrome.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The frames presentation's realization in the main site (DK decisions 60
// to 63): one DocumentFrame per document of the document area, stacked by
// the layout's frame orders and placed by the frame memories, its chrome
// drawn in the in-window frames' shared parts (frame_chrome.hpp); moving
// and resizing by the caption and border, the caption buttons, the window's
// document area binding, and a maximized frame's buttons on the main menu
// strip.

namespace gui_forms {
namespace {

using dock_site_detail::centred;
using dock_site_detail::first_focusable;
using dock_site_detail::focus_candidate;
using dock_site_detail::moved;
using dock_site_detail::to_window;

// Geometry in logical units, scaled with the text: the border a normal
// frame is resized by, a minimized frame's icon, the caption buttons (as a
// tool stack's caption: 16-unit buttons 1 unit apart, 3 units from the
// end), the label's insets, the accent keyline and the smallest frame a
// resize leaves.
constexpr double frame_border = 4.0;
constexpr double frame_icon_width = 160.0;
constexpr double frame_button_side = 16.0;
constexpr double frame_button_gap = 1.0;
constexpr double frame_button_end_inset = 3.0;
constexpr double frame_glyph_side = 9.0;
constexpr double frame_label_inset = 6.0;
constexpr double frame_label_gap = 4.0;
constexpr double frame_accent_height = 2.0;
constexpr double frame_minimum_width = 120.0;

// The edges a border drag moves.
constexpr std::uint8_t edge_left = 1U;
constexpr std::uint8_t edge_top = 2U;
constexpr std::uint8_t edge_right = 4U;
constexpr std::uint8_t edge_bottom = 8U;

bool has_edge(std::uint8_t edges, std::uint8_t edge) noexcept {
    const bool has = (edges & edge) != 0U;
    return has;
}

CursorKind cursor_for(std::uint8_t edges) noexcept {
    const bool left = has_edge(edges, edge_left);
    const bool right = has_edge(edges, edge_right);
    const bool top = has_edge(edges, edge_top);
    const bool bottom = has_edge(edges, edge_bottom);
    if ((left && top) || (right && bottom)) return CursorKind::resize_diagonal_down;
    if ((right && top) || (left && bottom)) return CursorKind::resize_diagonal_up;
    if (left || right) return CursorKind::resize_horizontal;
    if (top || bottom) return CursorKind::resize_vertical;
    return CursorKind::arrow;
}

double clamped(double value, double low, double high) noexcept {
    if (high < low) return low;
    const double result = std::min(std::max(value, low), high);
    return result;
}

// A normal frame kept inside an area of `area`: no larger than it, and
// moved in from any edge it crosses.
Rect kept_inside(Rect bounds, Size area) noexcept {
    Rect inside = bounds;
    inside.width = std::min(inside.width, std::max(0.0, area.width));
    inside.height = std::min(inside.height, std::max(0.0, area.height));
    inside.x = clamped(inside.x, 0.0, area.width - inside.width);
    inside.y = clamped(inside.y, 0.0, area.height - inside.height);
    return inside;
}

// A frame's stacking: its layout order and its position in tree order.
struct StackedFrame final {
    std::uint32_t order{};
    std::size_t position{};
};

// Front first: the higher order, and on a tie the later in tree order.
struct FrontFirst final {
    [[nodiscard]] bool operator()(const StackedFrame& left, const StackedFrame& right) const noexcept {
        if (left.order != right.order) return left.order > right.order;
        const bool later = left.position > right.position;
        return later;
    }
};

} // namespace

// -- the public surface -------------------------------------------------------------

bool DockSite::document_frames_shown() const noexcept {
    if (workspace_ == nullptr || floating()) return false;
    const bool frames =
        (*workspace_).layout().state().document_presentation == DockDocumentPresentation::frames;
    return frames;
}

Control::Ptr DockSite::document_frame(const DockPanelId& document) const {
    const FrameSlot* const slot = frame_for(document);
    if (slot == nullptr) return Control::Ptr{};
    return (*slot).frame;
}

Rect DockSite::document_frame_bounds(const DockPanelId& document) const {
    const FrameSlot* const slot = frame_for(document);
    if (slot == nullptr) return Rect{};
    return (*slot).bounds;
}

DockFrameMetrics DockSite::frame_metrics() const {
    DockFrameMetrics metrics{};
    metrics.caption_height = caption_height();
    metrics.icon_width = frame_icon_width * effective_text_scale();
    metrics.icons_at_bottom = workspace_ == nullptr || (*workspace_).frame_icons_at_bottom();
    return metrics;
}

// -- the frames -----------------------------------------------------------------------

DockSite::FrameSlot* DockSite::frame_for(const DockPanelId& panel) noexcept {
    for (FrameSlot& slot : frames_) {
        if (slot.frame && slot.panel == panel) return &slot;
    }
    return nullptr;
}

const DockSite::FrameSlot* DockSite::frame_for(const DockPanelId& panel) const noexcept {
    for (const FrameSlot& slot : frames_) {
        if (slot.frame && slot.panel == panel) return &slot;
    }
    return nullptr;
}

std::size_t DockSite::frame_index_of(const Control& control) const {
    for (std::size_t index = 0; index < frames_.size(); ++index) {
        const Control::Ptr& frame = frames_[index].frame;
        if (!frame) continue;
        if ((*frame).runtime_id() == control.runtime_id() || (*frame).contains(control)) return index;
    }
    return frames_.size();
}

// One frame per document of the area in tree order, reused by the document's
// identity; frames whose documents left go, giving their content back. The
// frames stack from `first_index` (counted from the top) down, the highest
// order in front.
void DockSite::realize_frames(std::size_t first_index) {
    std::vector<DockPanelId> documents{};
    {
        const BorrowedSpan<DockPanelId> listed = (*workspace_).area_documents();
        documents.reserve(listed.size());
        for (std::size_t index = 0; index < listed.size(); ++index) documents.push_back(listed[index]);
    }
    detail::LendingVector<FrameSlot> next{};
    next.reserve(documents.size());
    for (const DockPanelId& document : documents) {
        FrameSlot* const existing = frame_for(document);
        if (existing != nullptr) {
            next.push_back(std::move(*existing));
            (*existing).frame.reset();
        } else {
            ++frame_serial_;
            const std::string identity =
                std::string(stable_id().value()) + ".frame." + std::to_string(frame_serial_);
            FrameSlot slot{};
            slot.panel = document;
            slot.frame = make_control<DocumentFrame>(StableId(identity), *this, document);
            add_child(slot.frame);
            next.push_back(std::move(slot));
        }
        const stxlib::Optional<DockFrameMemory> memory = (*workspace_).frame_memory(document);
        const DockFrameMemory* const frame = memory.value();
        next.back().state = frame != nullptr ? (*frame).state : DockFrameState::normal;
        next.back().order = frame != nullptr ? (*frame).order : 0U;
    }
    for (FrameSlot& slot : frames_) remove_frame(slot);
    frames_ = std::move(next);
    if (frame_gesture_.active && frame_for(frame_gesture_.panel) == nullptr) frame_gesture_ = FrameGesture{};
    std::vector<StackedFrame> stacking{};
    stacking.reserve(frames_.size());
    for (std::size_t position = 0; position < frames_.size(); ++position) {
        stacking.push_back(StackedFrame{frames_[position].order, position});
    }
    std::sort(stacking.begin(), stacking.end(), FrontFirst{});
    for (std::size_t place = 0; place < stacking.size(); ++place) {
        const Control::Ptr& frame = frames_[stacking[place].position].frame;
        static_cast<void>(set_child_index_from_top((*frame).runtime_id(), first_index + place));
        (*frame).invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
    }
}

void DockSite::remove_frame(FrameSlot& slot) noexcept {
    if (!slot.frame) return;
    DocumentFrame& frame = static_cast<DocumentFrame&>(*slot.frame);
    frame.release_site();
    try {
        static_cast<void>(frame.take_content());
        const Control::Ptr parent = frame.parent();
        if (parent) static_cast<void>((*parent).remove_child(frame.runtime_id()));
    } catch (...) {
    }
    slot.frame.reset();
}

void DockSite::release_frames() noexcept {
    release_frame_buttons_on_strip();
    for (FrameSlot& slot : frames_) remove_frame(slot);
    frames_.clear();
    frame_gesture_ = FrameGesture{};
    frame_hot_ = FrameButtonHit{};
    frame_pressed_ = FrameButtonHit{};
}

// Each frame at its memory's place in the document area: its normal bounds
// (kept inside the area; a frame being moved or resized at the gesture's),
// its icon while minimized, the whole area while maximized.
void DockSite::arrange_frames() {
    if (frames_.empty() || workspace_ == nullptr) return;
    const DockFrameMetrics metrics = frame_metrics();
    const Size area{document_area_.width, document_area_.height};
    const Point origin{document_area_.x, document_area_.y};
    for (FrameSlot& slot : frames_) {
        if (!slot.frame) continue;
        const stxlib::Optional<DockFrameMemory> memory = (*workspace_).frame_memory(slot.panel);
        DockFrameMemory frame{};
        if (memory.present()) frame = *memory.value();
        if (frame_gesture_.active && frame_gesture_.panel == slot.panel && frame.state == DockFrameState::normal) {
            frame.bounds = frame_gesture_.bounds;
        }
        Rect shown = dock_frame_shown_bounds(frame, area, metrics);
        if (frame.state == DockFrameState::normal) shown = kept_inside(shown, area);
        slot.state = frame.state;
        slot.order = frame.order;
        slot.bounds = moved(shown, origin);
        set_child_layout_collapsed(slot.frame, false);
        set_child_layout(slot.frame, slot.bounds);
    }
}

// The factory is application code and may edit the layout, which realizes
// the frames again: the frame is held across it and the content given only
// if the document's slot still holds that frame.
void DockSite::supply_frame_content(const DockPanelId& panel) {
    const FrameSlot* const slot = frame_for(panel);
    if (workspace_ == nullptr || slot == nullptr || (*slot).state == DockFrameState::minimized) return;
    const Control::Ptr frame = (*slot).frame;
    Control::Ptr content = (*workspace_).panel_content(panel);
    if (!content) {
        content = (*workspace_).create_panel_content(panel);
        if (!content) return;
        const FrameSlot* const after = frame_for(panel);
        if (after == nullptr || (*after).frame != frame) return;
    }
    DocumentFrame& shown = static_cast<DocumentFrame&>(*frame);
    if (shown.content() == content) return;
    shown.show_content(content);
}

// The frame comes to the front, then focus goes into its content: the
// control that last had focus there, else its first focusable control.
void DockSite::focus_frame(const DockPanelId& panel) {
    Window* const host = attached_window();
    if (host == nullptr || workspace_ == nullptr) return;
    raise_frame(panel, DockEditOrigin::program);
    supply_frame_content(panel);
    const FrameSlot* const slot = frame_for(panel);
    if (slot == nullptr || (*slot).state == DockFrameState::minimized) return;
    const Control::Ptr frame = (*slot).frame;
    std::vector<Control::Ptr> remembered{};
    for (const RememberedFocus& entry : remembered_focus_) {
        if (!(entry.panel == panel)) continue;
        Control::Ptr control = entry.control.lock();
        if (control) remembered.push_back(std::move(control));
    }
    for (const Control::Ptr& control : remembered) {
        if (focus_candidate(*control) && (*frame).contains(*control) && (*host).request_focus(control)) return;
    }
    const Control::Ptr content = static_cast<DocumentFrame&>(*frame).content();
    if (!content) return;
    if (focus_candidate(*content) && (*host).request_focus(content)) return;
    const Control::Ptr inner = first_focusable(content);
    if (inner) static_cast<void>((*host).request_focus(inner));
}

// A select edit brings the frame to the front (its order rises above the
// others'); nothing when it is in front alone already.
void DockSite::raise_frame(const DockPanelId& panel, DockEditOrigin origin) {
    const FrameSlot* const slot = frame_for(panel);
    if (workspace_ == nullptr || slot == nullptr) return;
    bool front = true;
    for (const FrameSlot& other : frames_) {
        if (!other.frame || other.panel == panel) continue;
        if (other.order >= (*slot).order) front = false;
    }
    if (front) return;
    DockTransaction transaction{};
    transaction.origin = origin;
    transaction.edits.push_back(DockEdit::select(panel));
    static_cast<void>((*workspace_).apply(transaction));
}

// Focus entered a frame: its document is active and its frame in front, and
// a control in its content is remembered. The frame and the site are held
// and checked again across the handlers (as on_focus_moved does).
bool DockSite::note_frame_focus(const Control::Ptr& focused) {
    const std::size_t index = frame_index_of(*focused);
    if (index >= frames_.size()) return false;
    const DockPanelId panel = frames_[index].panel;
    const Control::Ptr frame = frames_[index].frame;
    const Control::Ptr retained = weak_from_this().lock();
    raise_frame(panel, DockEditOrigin::user);
    if (workspace_ == nullptr || !is_alive()) return true;
    (*workspace_).note_active(panel, DockActivationCause::focus);
    if (workspace_ == nullptr || !is_alive()) return true;
    const FrameSlot* const slot = frame_for(panel);
    if (slot == nullptr || (*slot).frame != frame) return true;
    const Control::Ptr content = static_cast<DocumentFrame&>(*frame).content();
    const bool in_content =
        content && ((*content).runtime_id() == (*focused).runtime_id() || (*content).contains(*focused));
    if (in_content) remember_focus(panel, focused);
    return true;
}

void DockSite::remember_focus(const DockPanelId& panel, const Control::Ptr& focused) {
    for (RememberedFocus& entry : remembered_focus_) {
        if (!(entry.panel == panel)) continue;
        entry.control = focused;
        return;
    }
    RememberedFocus entry{};
    entry.panel = panel;
    entry.control = focused;
    remembered_focus_.push_back(std::move(entry));
}

// -- geometry and paint ---------------------------------------------------------------

bool DockSite::frame_caption_hidden(const FrameSlot& slot) const noexcept {
    const bool hidden = slot.state == DockFrameState::maximized && frame_strip_.lock() != nullptr;
    return hidden;
}

DockSite::FrameGeometry DockSite::frame_geometry(const FrameSlot& slot) const {
    FrameGeometry placed{};
    const double scale = effective_text_scale();
    const double width = slot.bounds.width;
    const double height = slot.bounds.height;
    const bool minimized = slot.state == DockFrameState::minimized;
    const bool normal = slot.state == DockFrameState::normal;
    placed.border = normal ? std::min(frame_border * scale, std::min(width, height) * 0.25) : 0.0;
    const double border = placed.border;
    const double inner_width = std::max(0.0, width - border * 2.0);
    if (!frame_caption_hidden(slot)) {
        const double caption = minimized ? height : std::min(caption_height(), std::max(0.0, height - border * 2.0));
        placed.caption = {border, border, inner_width, caption};
        const double button = std::min(frame_button_side * scale, caption);
        const double gap = frame_button_gap * scale;
        const double top = placed.caption.y + (caption - button) * 0.5;
        const double end = placed.caption.right() - frame_button_end_inset * scale;
        placed.close = {end - button, top, button, button};
        placed.maximize = {placed.close.x - gap - button, top, button, button};
        placed.minimize = {placed.maximize.x - gap - button, top, button, button};
        const double glyph = std::min(frame_glyph_side * scale, button);
        placed.close_glyph = centred(placed.close, glyph, glyph);
        placed.maximize_glyph = centred(placed.maximize, glyph, glyph);
        placed.minimize_glyph = centred(placed.minimize, glyph, glyph);
        const double label_start = placed.caption.x + frame_label_inset * scale;
        const double label_end = placed.minimize.x - frame_label_gap * scale;
        placed.label = {label_start, placed.caption.y, std::max(0.0, label_end - label_start), caption};
    }
    if (!minimized) {
        const double top = border + placed.caption.height;
        placed.client = {border, top, inner_width, std::max(0.0, height - top - border)};
    }
    return placed;
}

DockSite::FramePart DockSite::frame_part_at(const FrameSlot& slot, Point local, std::uint8_t& edges) const {
    edges = 0U;
    const FrameGeometry placed = frame_geometry(slot);
    if (placed.close.contains(local)) return FramePart::close;
    if (placed.maximize.contains(local)) return FramePart::maximize;
    if (placed.minimize.contains(local)) return FramePart::minimize;
    if (placed.border > 0.0) {
        const double grab = placed.border;
        if (local.x < grab) edges = static_cast<std::uint8_t>(edges | edge_left);
        if (local.x >= slot.bounds.width - grab) edges = static_cast<std::uint8_t>(edges | edge_right);
        if (local.y < grab) edges = static_cast<std::uint8_t>(edges | edge_top);
        if (local.y >= slot.bounds.height - grab) edges = static_cast<std::uint8_t>(edges | edge_bottom);
        if (edges != 0U) return FramePart::border;
    }
    if (placed.caption.contains(local)) return FramePart::caption;
    return FramePart::none;
}

bool DockSite::frame_current(const FrameSlot& slot) const {
    if (workspace_ == nullptr) return false;
    const stxlib::Optional<DockPanelId> active = (*workspace_).area_active_document();
    const bool current = active.present() && *active.value() == slot.panel;
    return current;
}

AnatomyStateSet DockSite::frame_button_states(const FrameSlot& slot, FramePart part) const {
    AnatomyStateSet states{};
    if (frame_hot_.part == part && frame_hot_.panel == slot.panel) states.insert(AnatomyState::hot);
    if (frame_pressed_.part == part && frame_pressed_.panel == slot.panel) states.insert(AnatomyState::pressed);
    bool available = effectively_enabled();
    if (part == FramePart::close) available = available && tab_closable(slot.panel);
    if (!available) states.insert(AnatomyState::disabled);
    return states;
}

// The chrome (the in-window frames' shared parts), then the caption
// buttons: minimize (restore while minimized), maximize (restore while
// maximized) and close.
void DockSite::paint_frame(Painter& painter, const Control& frame, const DockPanelId& panel) const {
    const FrameSlot* const slot = frame_for(panel);
    if (slot == nullptr || workspace_ == nullptr) return;
    const FrameGeometry placed = frame_geometry(*slot);
    const std::string title = (*workspace_).panel_title(panel);
    const bool current = frame_current(*slot);
    frame_chrome::FrameChromePaint chrome{};
    chrome.face = Rect{0.0, 0.0, (*slot).bounds.width, (*slot).bounds.height};
    chrome.edge = placed.border > 0.0;
    chrome.caption = placed.caption;
    chrome.label = placed.label;
    chrome.title = title;
    chrome.font = caption_font();
    if (current) chrome.caption_states.insert(AnatomyState::current);
    if (!effectively_enabled()) chrome.caption_states.insert(AnatomyState::disabled);
    chrome.accent = current;
    chrome.accent_height = frame_accent_height * effective_text_scale();
    frame_chrome::paint_frame_chrome(painter, frame, chrome);
    if (placed.caption.empty()) return;
    const frame_chrome::FrameButtonGlyph first = (*slot).state == DockFrameState::minimized
                                                     ? frame_chrome::FrameButtonGlyph::restore
                                                     : frame_chrome::FrameButtonGlyph::minimize;
    const frame_chrome::FrameButtonGlyph second = (*slot).state == DockFrameState::maximized
                                                      ? frame_chrome::FrameButtonGlyph::restore
                                                      : frame_chrome::FrameButtonGlyph::maximize;
    frame_chrome::paint_frame_button(painter, frame, placed.minimize, placed.minimize_glyph, first,
                                     frame_button_states(*slot, FramePart::minimize));
    frame_chrome::paint_frame_button(painter, frame, placed.maximize, placed.maximize_glyph, second,
                                     frame_button_states(*slot, FramePart::maximize));
    frame_chrome::paint_frame_button(painter, frame, placed.close, placed.close_glyph,
                                     frame_chrome::FrameButtonGlyph::close,
                                     frame_button_states(*slot, FramePart::close));
}

// -- input -------------------------------------------------------------------------------

// A frame's pointer input, in the frame's coordinates (`location`) and the
// window's (`window_position`, which the gesture follows, since the frame
// moves under the pointer). A press on a caption button arms it and the
// release on the same button runs it; a double-click on the caption
// maximizes or restores; a press on the caption or border of a normal frame
// moves or resizes it until the release, which commits one user edit.
void DockSite::frame_mouse(const DockPanelId& panel, MouseEventArgs& arguments) {
    if (workspace_ == nullptr) return;
    const Control::Ptr keep = shared_from_this();
    if (frame_gesture_.active) {
        if (arguments.action == PointerAction::move) {
            continue_frame_gesture(arguments.window_position);
            arguments.handled = true;
            return;
        }
        if (arguments.action == PointerAction::up && arguments.button == PointerButton::primary) {
            end_frame_gesture(false);
            arguments.handled = true;
            return;
        }
    }
    const FrameSlot* const slot = frame_for(panel);
    if (slot == nullptr) return;
    std::uint8_t edges = 0U;
    const FramePart part = frame_part_at(*slot, arguments.location, edges);
    const bool button = part == FramePart::minimize || part == FramePart::maximize || part == FramePart::close;
    if (arguments.action == PointerAction::move) {
        FrameButtonHit hit{};
        if (button) {
            hit.panel = panel;
            hit.part = part;
        }
        set_frame_hot(hit);
        const CursorKind wanted = part == FramePart::border ? cursor_for(edges) : CursorKind::arrow;
        Control& frame = *(*slot).frame;
        if (!(frame.cursor.read() == stxlib::Optional<CursorKind>::of(wanted))) {
            frame.cursor = stxlib::Optional<CursorKind>::of(wanted);
        }
        return;
    }
    if (arguments.button != PointerButton::primary) return;
    if (arguments.action == PointerAction::up) {
        const FrameButtonHit pressed = frame_pressed_;
        frame_pressed_ = FrameButtonHit{};
        if (pressed.part == FramePart::none) return;
        invalidate_frame(pressed.panel);
        arguments.handled = true;
        if (!(pressed.panel == panel) || pressed.part != part) return;
        static_cast<void>(run_frame_button(panel, part));
        return;
    }
    if (arguments.action != PointerAction::down || part == FramePart::none) return;
    arguments.handled = true;
    if (button) {
        if (frame_button_states(*slot, part).contains(AnatomyState::disabled)) return;
        frame_pressed_.panel = panel;
        frame_pressed_.part = part;
        invalidate_frame(panel);
        return;
    }
    if (part == FramePart::caption && arguments.clicks >= 2U) {
        const FramePart toggle =
            (*slot).state == DockFrameState::minimized ? FramePart::minimize : FramePart::maximize;
        static_cast<void>(run_frame_button(panel, toggle));
        return;
    }
    if ((*slot).state != DockFrameState::normal) return;
    begin_frame_gesture(panel, part == FramePart::border ? edges : std::uint8_t{0U}, arguments.window_position);
}

void DockSite::frame_mouse_left(const DockPanelId& panel) {
    if (frame_hot_.panel == panel) set_frame_hot(FrameButtonHit{});
}

void DockSite::set_frame_hot(const FrameButtonHit& hit) {
    if (frame_hot_.part == hit.part && frame_hot_.panel == hit.panel) return;
    const DockPanelId previous = frame_hot_.panel;
    frame_hot_ = hit;
    invalidate_frame(previous);
    invalidate_frame(hit.panel);
}

void DockSite::invalidate_frame(const DockPanelId& panel) {
    const FrameSlot* const slot = frame_for(panel);
    if (slot != nullptr) (*(*slot).frame).invalidate(Dirty::paint | Dirty::semantics);
}

void DockSite::begin_frame_gesture(const DockPanelId& panel, std::uint8_t edges, Point window_point) {
    const FrameSlot* const slot = frame_for(panel);
    if (slot == nullptr) return;
    FrameGesture gesture{};
    gesture.active = true;
    gesture.panel = panel;
    gesture.edges = edges;
    gesture.press = window_point;
    gesture.start = moved((*slot).bounds, Point{-document_area_.x, -document_area_.y});
    gesture.bounds = gesture.start;
    frame_gesture_ = std::move(gesture);
}

// The frame follows the pointer: moved by a caption drag, its dragged edges
// moved by a border drag (no smaller than the smallest frame), and kept
// inside the area either way.
void DockSite::continue_frame_gesture(Point window_point) {
    if (!frame_gesture_.active) return;
    const double scale = effective_text_scale();
    const double dx = window_point.x - frame_gesture_.press.x;
    const double dy = window_point.y - frame_gesture_.press.y;
    const Rect start = frame_gesture_.start;
    const std::uint8_t edges = frame_gesture_.edges;
    const double minimum_width = frame_minimum_width * scale;
    const double minimum_height = caption_height() + frame_border * scale * 2.0;
    Rect next = start;
    if (edges == 0U) {
        next.x = start.x + dx;
        next.y = start.y + dy;
    } else {
        if (has_edge(edges, edge_left)) {
            const double left = std::min(start.x + dx, start.right() - minimum_width);
            next.x = left;
            next.width = start.right() - left;
        }
        if (has_edge(edges, edge_right)) next.width = std::max(minimum_width, start.width + dx);
        if (has_edge(edges, edge_top)) {
            const double top = std::min(start.y + dy, start.bottom() - minimum_height);
            next.y = top;
            next.height = start.bottom() - top;
        }
        if (has_edge(edges, edge_bottom)) next.height = std::max(minimum_height, start.height + dy);
    }
    next = kept_inside(next, Size{document_area_.width, document_area_.height});
    if (next == frame_gesture_.bounds) return;
    frame_gesture_.bounds = next;
    invalidate(Dirty::arrange | Dirty::paint);
}

void DockSite::end_frame_gesture(bool cancelled) {
    if (!frame_gesture_.active) return;
    FrameGesture gesture = std::move(frame_gesture_);
    frame_gesture_ = FrameGesture{};
    invalidate(Dirty::arrange | Dirty::paint);
    if (cancelled || gesture.bounds == gesture.start || workspace_ == nullptr) return;
    static_cast<void>((*workspace_).set_frame_bounds(gesture.panel, gesture.bounds, DockEditOrigin::user));
}

// Minimizing the active frame activates the next frame used before it that
// is not minimized; restoring a minimized one activates it (maximizing
// activates through the workspace), as WinForms' MDI does.
bool DockSite::run_frame_button(const DockPanelId& panel, FramePart part) {
    if (workspace_ == nullptr) return false;
    const FrameSlot* const slot = frame_for(panel);
    if (slot == nullptr) return false;
    const DockPanelId document = panel;
    if (part == FramePart::close) {
        if (!tab_closable(document)) return false;
        const DockCloseOutcome outcome = (*workspace_).request_close(document);
        const bool ran = outcome != DockCloseOutcome::refused && outcome != DockCloseOutcome::busy;
        return ran;
    }
    const DockFrameState state = (*slot).state;
    DockFrameState next = state;
    if (part == FramePart::minimize) {
        next = state == DockFrameState::minimized ? DockFrameState::normal : DockFrameState::minimized;
    } else if (part == FramePart::maximize) {
        next = state == DockFrameState::maximized ? DockFrameState::normal : DockFrameState::maximized;
    } else {
        return false;
    }
    const stxlib::Optional<DockPanelId> active = (*workspace_).area_active_document();
    const bool was_active = active.present() && *active.value() == document;
    const DockTransactionResult result = (*workspace_).set_frame_state(document, next, DockEditOrigin::user);
    if (!result.applied || workspace_ == nullptr) return result.applied;
    if (next == DockFrameState::normal && state == DockFrameState::minimized) {
        static_cast<void>((*workspace_).activate_panel(document, DockActivationCause::pointer));
    } else if (next == DockFrameState::minimized && was_active) {
        std::vector<DockPanelId> candidates{};
        for (const DockPanelId& recent : (*workspace_).document_history()) candidates.push_back(recent);
        for (const FrameSlot& other : frames_) candidates.push_back(other.panel);
        for (const DockPanelId& candidate : candidates) {
            const FrameSlot* const other = frame_for(candidate);
            if (other == nullptr || candidate == document || (*other).state == DockFrameState::minimized) continue;
            static_cast<void>((*workspace_).activate_panel(candidate, DockActivationCause::pointer));
            break;
        }
    }
    return true;
}

// -- the main menu strip --------------------------------------------------------------

// While a frame is maximized and the window's main menu strip is a
// MenuStrip, the strip shows the frame's buttons and the frame hides its
// caption (WinForms' MDI); the frames are laid out again whenever that
// changes.
void DockSite::sync_frame_buttons_on_strip() {
    std::shared_ptr<MenuStrip> wanted{};
    if (document_frames_shown()) {
        bool maximized = false;
        for (const FrameSlot& slot : frames_) {
            if (slot.frame && slot.state == DockFrameState::maximized) maximized = true;
        }
        Window* const host = attached_window();
        if (maximized && host != nullptr) wanted = std::dynamic_pointer_cast<MenuStrip>((*host).main_menu_strip());
    }
    const Control::Ptr held = frame_strip_.lock();
    if (wanted && held == wanted) return;
    release_frame_buttons_on_strip();
    if (!wanted) return;
    (*wanted).set_document_frame_buttons(true);
    frame_strip_token_ = (*wanted).frame_button_pressed().subscribe(
        *this, Delegate<MenuStripFrameButton>::bind<DockSite, &DockSite::on_strip_frame_button>(*this));
    frame_strip_ = wanted;
    for (const FrameSlot& slot : frames_) {
        if (slot.frame) (*slot.frame).invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
    }
}

void DockSite::release_frame_buttons_on_strip() noexcept {
    frame_strip_token_.disconnect();
    const Control::Ptr held = frame_strip_.lock();
    frame_strip_.reset();
    if (!held) return;
    try {
        (*std::static_pointer_cast<MenuStrip>(held)).set_document_frame_buttons(false);
        for (const FrameSlot& slot : frames_) {
            if (slot.frame) (*slot.frame).invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
        }
    } catch (...) {
    }
}

void DockSite::on_strip_frame_button(MenuStripFrameButton button) {
    DockPanelId maximized{};
    bool found = false;
    for (const FrameSlot& slot : frames_) {
        if (!slot.frame || slot.state != DockFrameState::maximized) continue;
        maximized = slot.panel;
        found = true;
    }
    if (!found) return;
    const Control::Ptr keep = shared_from_this();
    switch (button) {
    case MenuStripFrameButton::minimize:
        static_cast<void>(run_frame_button(maximized, FramePart::minimize));
        return;
    case MenuStripFrameButton::restore:
        static_cast<void>(run_frame_button(maximized, FramePart::maximize));
        return;
    case MenuStripFrameButton::close:
        static_cast<void>(run_frame_button(maximized, FramePart::close));
        return;
    default:
        // A value outside the enumeration presses no frame button.
        return;
    }
}

// -- the window's document area --------------------------------------------------------

void DockSite::bind_document_area() {
    if (workspace_ == nullptr || floating() || (*workspace_).site() != this) return;
    Window* const host = attached_window();
    if (host == nullptr) return;
    (*host).bind_document_area((*workspace_).document_area());
}

void DockSite::unbind_document_area() noexcept {
    if (workspace_ == nullptr || floating() || (*workspace_).site() != this) return;
    WindowDocumentArea& area = (*workspace_).document_area();
    Window* const bound = area.bound_window();
    if (bound != nullptr) (*bound).unbind_document_area(area);
}

// -- DocumentFrame ----------------------------------------------------------------------

void DockSite::DocumentFrame::show_content(const Control::Ptr& content) {
    const Control::Ptr previous = content_.lock();
    if (previous == content) return;
    if (previous) static_cast<void>(remove_child((*previous).runtime_id()));
    content_.reset();
    if (!content) return;
    const Control::Ptr parent = (*content).parent();
    if (parent && (*parent).runtime_id() != runtime_id()) {
        static_cast<void>((*parent).remove_child((*content).runtime_id()));
    }
    if (!(*content).parent()) add_child(content);
    content_ = content;
    invalidate(Dirty::arrange | Dirty::paint);
}

Control::Ptr DockSite::DocumentFrame::take_content() noexcept {
    const Control::Ptr content = content_.lock();
    content_.reset();
    if (!content) return Control::Ptr{};
    try {
        Control::Ptr taken = remove_child((*content).runtime_id());
        return taken;
    } catch (...) {
        return Control::Ptr{};
    }
}

// The content fills the client area below the caption; a minimized frame
// (its icon) shows none.
void DockSite::DocumentFrame::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Control::Ptr content = content_.lock();
    if (!content) return;
    const FrameSlot* const slot = site_ != nullptr ? (*site_).frame_for(panel_) : nullptr;
    if (slot == nullptr || (*slot).state == DockFrameState::minimized) {
        set_child_layout_collapsed(content, true);
        return;
    }
    const FrameGeometry placed = (*site_).frame_geometry(*slot);
    set_child_layout_collapsed(content, false);
    set_child_layout(content, placed.client);
}

SemanticDescriptor DockSite::DocumentFrame::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::pane;
    if (site_ != nullptr && (*site_).workspace_ != nullptr) descriptor.name = (*(*site_).workspace_).panel_title(panel_);
    descriptor.exposed = true;
    descriptor.include_descendants = true;
    return descriptor;
}

std::string DockSite::DocumentFrame::button_identity(std::string_view button) const {
    std::string identity = std::string(stable_id().value()) + "." + std::string(button);
    return identity;
}

// The caption buttons, as the pane's children: minimize (Restore while
// minimized), maximize (Restore while maximized) and close.
std::vector<SemanticNode> DockSite::DocumentFrame::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    if (site_ == nullptr) return nodes;
    const FrameSlot* const slot = (*site_).frame_for(panel_);
    if (slot == nullptr) return nodes;
    const FrameGeometry placed = (*site_).frame_geometry(*slot);
    if (placed.caption.empty()) return nodes;
    const Rect absolute = absolute_bounds();
    const std::array<FramePart, 3> parts{FramePart::minimize, FramePart::maximize, FramePart::close};
    const std::array<Rect, 3> bounds{placed.minimize, placed.maximize, placed.close};
    const std::array<std::string_view, 3> names{"minimize", "maximize", "close"};
    const std::array<std::string_view, 3> labels{
        (*slot).state == DockFrameState::minimized ? std::string_view("Restore") : std::string_view("Minimize"),
        (*slot).state == DockFrameState::maximized ? std::string_view("Restore") : std::string_view("Maximize"),
        std::string_view("Close")};
    nodes.reserve(parts.size());
    for (std::size_t index = 0; index < parts.size(); ++index) {
        SemanticNode node{};
        node.stable_id = button_identity(names[index]);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::button;
        node.name = std::string(labels[index]);
        node.bounds = to_window(absolute, bounds[index]);
        node.states = SemanticState::visible;
        if (!(*site_).frame_button_states(*slot, parts[index]).contains(AnatomyState::disabled)) {
            node.states |= SemanticState::enabled;
            node.actions = {SemanticAction::press};
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool DockSite::DocumentFrame::on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                                      std::string_view value) {
    if (site_ != nullptr && action == SemanticAction::press) {
        const Control::Ptr keep = shared_from_this();
        const DockPanelId panel = panel_;
        if (stable_id == button_identity("minimize")) return (*site_).run_frame_button(panel, FramePart::minimize);
        if (stable_id == button_identity("maximize")) return (*site_).run_frame_button(panel, FramePart::maximize);
        if (stable_id == button_identity("close")) return (*site_).run_frame_button(panel, FramePart::close);
    }
    const bool handled = Control::on_semantic_child_action(stable_id, action, value);
    return handled;
}

void DockSite::DocumentFrame::on_mouse_down(MouseEventArgs& arguments) {
    const Control::Ptr keep = shared_from_this();
    const DockPanelId panel = panel_;
    if (site_ != nullptr) (*site_).frame_mouse(panel, arguments);
    Control::on_mouse_down(arguments);
}

void DockSite::DocumentFrame::on_mouse_up(MouseEventArgs& arguments) {
    const Control::Ptr keep = shared_from_this();
    const DockPanelId panel = panel_;
    if (site_ != nullptr) (*site_).frame_mouse(panel, arguments);
    Control::on_mouse_up(arguments);
}

void DockSite::DocumentFrame::on_mouse_move(MouseEventArgs& arguments) {
    const Control::Ptr keep = shared_from_this();
    const DockPanelId panel = panel_;
    if (site_ != nullptr) (*site_).frame_mouse(panel, arguments);
    Control::on_mouse_move(arguments);
}

void DockSite::DocumentFrame::on_mouse_leave() {
    if (site_ != nullptr) (*site_).frame_mouse_left(panel_);
    Control::on_mouse_leave();
}

} // namespace gui_forms
