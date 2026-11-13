#include "gui_forms/controls/content_text_editor.hpp"

#include "../panel/input_control_utilities.hpp"
#include "../panel/text_editing/caret_blink.hpp"
#include "../panel/text_editing/text_editing_engine.hpp"
#include "../scrollable_control/scrollable_anatomy.hpp"
#include "../../core/content/channel/content_channel.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>

namespace gui_forms {

namespace {

// The device extent of a logical one at `scale`, rounded outward; zero when
// it is empty or does not fit (as ContentSurface sizes its region).
[[nodiscard]] std::uint32_t editor_device_extent(double logical, double scale) noexcept {
    const double extent = std::ceil(logical * scale);
    if (!std::isfinite(extent) || extent < 1.0) return 0U;
    if (extent > static_cast<double>(std::numeric_limits<std::int32_t>::max())) return 0U;
    return static_cast<std::uint32_t>(extent);
}

// The x of `offset` within `cluster`, whose range holds it: its leading
// edge at the start, its trailing edge at the end, and between them the
// fraction of the cluster's bytes before the offset (a ligature's inner
// carets, when the consumer gives no caret stops for them).
[[nodiscard]] double cluster_x(const ContentTextCluster& cluster, Utf8Offset offset) noexcept {
    const double span = static_cast<double>(cluster.range.end.value() - cluster.range.start.value());
    const double before = static_cast<double>(offset.value() - cluster.range.start.value());
    const double fraction = span > 0.0 ? before / span : 0.0;
    const double advance = fraction * cluster.bounds.width;
    const double result = cluster.right_to_left ? cluster.bounds.right() - advance : cluster.bounds.x + advance;
    return result;
}

// Orders clusters by line, for finding a line's first cluster.
struct ClusterLineBefore final {
    [[nodiscard]] bool operator()(const ContentTextCluster& cluster, std::size_t line) const noexcept {
        const bool result = cluster.line < line;
        return result;
    }
};

// Finds the first cluster or line that ends after an offset.
struct EndsAfter final {
    [[nodiscard]] bool operator()(Utf8Offset offset, const ContentTextCluster& cluster) const noexcept {
        const bool result = offset < cluster.range.end;
        return result;
    }
    [[nodiscard]] bool operator()(Utf8Offset offset, const ContentTextLine& line) const noexcept {
        const bool result = offset < line.range.end;
        return result;
    }
};

// Orders caret stops by offset.
struct CaretBefore final {
    [[nodiscard]] bool operator()(const ContentCaretStop& stop, Utf8Offset offset) const noexcept {
        const bool result = stop.offset < offset;
        return result;
    }
};

[[nodiscard]] const ContentCaretStop* find_caret_stop(const ContentTextLayout& layout, Utf8Offset offset) noexcept {
    const std::vector<ContentCaretStop>::const_iterator found =
        std::lower_bound(layout.carets.begin(), layout.carets.end(), offset, CaretBefore{});
    if (found == layout.carets.end() || (*found).offset != offset) return nullptr;
    const ContentCaretStop* result = &*found;
    return result;
}

// The caret bar's width, centred on its boundary (TextBox's).
constexpr double editor_caret_width = 1.0;

// The events ContentTextEditor adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 3> content_text_editor_interactions{{
    {.name = "layout_requested", .payload = "std::uint64_t", .mutable_payload = false},
    {.name = "text_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "selection_changed", .payload = "TextSelection", .mutable_payload = false},
}};

constexpr std::array<PartField<ContentTextEditor::Parts>, 4> content_text_editor_extra_part_fields{{
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = {}},
     .member = &ContentTextEditor::Parts::content},
    {.declaration = {.name = "caret", .role = PartRole::caret, .composed_from = {}},
     .member = &ContentTextEditor::Parts::caret},
    {.declaration = {.name = "selection", .role = PartRole::selection, .composed_from = {}},
     .member = &ContentTextEditor::Parts::selection},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ContentTextEditor::Parts::focus},
}};

constexpr std::array<StateField<ContentTextEditor::States>, 6> content_text_editor_state_fields{{
    {.state = AnatomyState::normal, .member = &ContentTextEditor::States::normal},
    {.state = AnatomyState::enabled, .member = &ContentTextEditor::States::enabled},
    {.state = AnatomyState::disabled, .member = &ContentTextEditor::States::disabled},
    {.state = AnatomyState::focused, .member = &ContentTextEditor::States::focused},
    {.state = AnatomyState::focus_visible, .member = &ContentTextEditor::States::focus_visible},
    {.state = AnatomyState::read_only, .member = &ContentTextEditor::States::read_only},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 4> content_text_editor_parts =
    joined_declarations(scrollable_detail::parts, part_declarations(content_text_editor_extra_part_fields));
constexpr std::array<AnatomyState, 6> content_text_editor_states =
    state_declarations(content_text_editor_state_fields);

constexpr ControlAnatomy content_text_editor_anatomy{
    .control_id = "content_text_editor",
    .parts = content_text_editor_parts,
    .states = content_text_editor_states,
    .custom_states = {},
};

} // namespace

// How the editing engine reaches the editor: each call forwards to the
// editor's own reaction, so the editor keeps its invalidation and event
// order. The editor owns the host and outlives the engine that borrows it.
class ContentTextEditor::EditingHost final : public detail::TextEditingHost {
public:
    explicit EditingHost(ContentTextEditor& editor) noexcept : editor_(editor) {}

    void editing_require_mutable() const override { editor_.require_mutable(); }
    void editing_restoring() override {}
    void editing_text_changed() override { editor_.text_edited(); }
    void editing_select(TextSelection selection) override { editor_.set_selection(selection); }
    void editing_navigating() override {}
    void editing_user_edit(TextEditKind kind, bool composing) override {
        static_cast<void>(kind);
        static_cast<void>(composing);
    }
    [[nodiscard]] HostServices* editing_host_services() const override {
        Window* const owner = editor_.window();
        if (owner == nullptr) return nullptr;
        HostServices* const services = (*owner).host_services();
        return services;
    }
    // The consumer's text is never masked: a password field is TextBox's.
    [[nodiscard]] bool editing_password_protected() const override { return false; }

private:
    ContentTextEditor& editor_;
};

// The caret blink timer's target. Observes the editor; a destroyed one is
// skipped.
class ContentTextEditor::CaretTick final {
public:
    explicit CaretTick(std::weak_ptr<ContentTextEditor> editor) noexcept : editor_(std::move(editor)) {}
    void operator()(FrameTime now) const {
        static_cast<void>(now);
        const std::shared_ptr<ContentTextEditor> live = editor_.lock();
        if (live) (*live).toggle_caret();
    }

private:
    std::weak_ptr<ContentTextEditor> editor_;
};

// The frame channel's notices arrive here on the UI thread (posted through
// the window's dispatcher): a first frame repaints the editor. The editor
// owns it and is destroyed on the same thread, so the back reference is
// valid whenever a notice runs.
class ContentTextEditor::FrameNotices final : public detail::ContentChannelListener {
public:
    explicit FrameNotices(ContentTextEditor& editor) noexcept : editor_(editor) {}
    void content_notice() override { editor_.invalidate(Dirty::paint); }

private:
    ContentTextEditor& editor_;
};

ContentTextEditor::ContentTextEditor(StableId stable_id, std::string initial_text)
    : TextBoxBase(std::move(stable_id)),
      editing_host_(std::make_unique<EditingHost>(*this)),
      editing_(std::make_unique<detail::TextEditingEngine>(*editing_host_, initial_text)),
      notices_(std::make_shared<FrameNotices>(*this)) {
    focusable = true;
    cursor = CursorKind::text;
}

ContentTextEditor::~ContentTextEditor() {
    caret_timer_.disconnect();
    if (channel_) detail::revoke_content_channel(*channel_);
}

std::string ContentTextEditor::text() const {
    std::string result((*editing_).text());
    return result;
}

void ContentTextEditor::set_text(std::string_view text) {
    require_mutable();
    // The store refuses text that is not UTF-8 before anything changes.
    (*editing_).set_text(text);
    text_edited();
}

std::uint64_t ContentTextEditor::text_revision() const noexcept {
    const std::uint64_t revision = (*editing_).store().revision();
    return revision;
}

void ContentTextEditor::set_read_only(bool read_only) {
    require_mutable();
    if ((*editing_).read_only() == read_only) return;
    (*editing_).set_read_only(read_only);
    invalidate(Dirty::paint | Dirty::semantics);
}

bool ContentTextEditor::read_only() const noexcept {
    const bool result = (*editing_).read_only();
    return result;
}

void ContentTextEditor::set_multiline(bool multiline) {
    require_mutable();
    if (multiline_ == multiline) return;
    multiline_ = multiline;
    (*editing_).set_multiline(multiline);
    invalidate(Dirty::semantics);
}

bool ContentTextEditor::redo() {
    const bool changed = (*editing_).redo();
    return changed;
}

bool ContentTextEditor::can_redo() const noexcept {
    const bool result = (*editing_).can_redo();
    return result;
}

ContentLayoutResult ContentTextEditor::submit_layout(ContentTextLayout layout) {
    require_mutable();
    const ContentLayoutResult result = validate_content_text_layout(layout, (*editing_).store(), text_revision());
    if (result != ContentLayoutResult::accepted) return result;
    layout_ = std::move(layout);
    has_layout_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    return result;
}

bool ContentTextEditor::layout_current() const noexcept {
    const bool result = has_layout_ && layout_.text_revision == text_revision();
    return result;
}

ContentFrameSink ContentTextEditor::open_frames(bool opaque) {
    require_mutable();
    std::shared_ptr<detail::ContentChannel> fresh = detail::make_content_channel(ContentPixelSize{}, opaque);
    if (!fresh) throw std::bad_alloc();
    if (channel_) detail::revoke_content_channel(*channel_);
    channel_ = std::move(fresh);
    surface_ = (*channel_).surface;
    Window* const window = attached_window();
    if (window != nullptr) detail::set_content_listener(*channel_, (*window).dispatcher(), notices_);
    register_presentation();
    update_request();
    invalidate(Dirty::paint);
    ContentFrameSink sink = detail::ContentSinkAccess::make(channel_);
    return sink;
}

std::optional<Rect> ContentTextEditor::caret_rectangle() const {
    if (!layout_current()) return std::nullopt;
    const std::optional<Rect> caret = caret_at((*editing_).selection().caret);
    return caret;
}

std::vector<Rect> ContentTextEditor::selection_rectangles() const {
    std::vector<Rect> boxes{};
    const TextSelection selection = (*editing_).selection();
    if (!layout_current() || selection.empty()) return boxes;
    const Utf8Offset start = selection.start();
    const Utf8Offset end = selection.end();
    std::vector<ContentTextCluster>::const_iterator cluster =
        std::upper_bound(layout_.clusters.begin(), layout_.clusters.end(), start, EndsAfter{});
    for (; cluster != layout_.clusters.end() && (*cluster).range.start < end; ++cluster) {
        const ContentTextCluster& covered = *cluster;
        const Utf8Offset from = std::max(start, covered.range.start);
        const Utf8Offset to = std::min(end, covered.range.end);
        const double first_x = cluster_x(covered, from);
        const double second_x = cluster_x(covered, to);
        const ContentTextLine& line = layout_.lines[covered.line];
        const Rect box{std::min(first_x, second_x), line.bounds.y, std::abs(second_x - first_x), line.bounds.height};
        boxes.push_back(box);
    }
    return boxes;
}

Utf8Offset ContentTextEditor::position_at(Point local) const {
    const Utf8Offset end = (*editing_).store().utf8_size();
    if (!layout_current() || layout_.lines.empty()) return end;
    // The nearest line by its vertical distance from the point (0 inside
    // it); the first such line wins a tie.
    const std::size_t line_count = layout_.lines.size();
    std::size_t nearest = 0U;
    double nearest_distance = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < line_count; ++index) {
        const Rect& box = layout_.lines[index].bounds;
        double distance = 0.0;
        if (local.y < box.y) distance = box.y - local.y;
        if (local.y >= box.bottom()) distance = local.y - box.bottom();
        if (distance < nearest_distance) {
            nearest = index;
            nearest_distance = distance;
        }
        if (distance == 0.0) break;
    }
    const Utf8Offset position = position_on_line(nearest, local.x);
    return position;
}

void ContentTextEditor::set_draws_selection(bool draws) {
    require_mutable();
    if (draws_selection_ == draws) return;
    draws_selection_ = draws;
    invalidate(Dirty::paint);
}

detail::TextEditingEngine* ContentTextEditor::editing_engine() const noexcept {
    detail::TextEditingEngine* const engine = editing_.get();
    return engine;
}

void ContentTextEditor::apply_selection(TextSelection selection) {
    set_selection(selection);
}

void ContentTextEditor::text_edited() {
    preferred_x_ = -1.0;
    reset_caret_blink();
    invalidate(Dirty::paint | Dirty::semantics);
    const std::string current((*editing_).text());
    publish_change(text_changed_, current);
    publish_change(selection_changed_, (*editing_).selection());
    publish_change(layout_requested_, text_revision());
}

void ContentTextEditor::set_selection(TextSelection selection) {
    const bool moved = !((*editing_).selection() == selection);
    if (moved) (*editing_).set_selection_value(selection);
    reset_caret_blink();
    if (!moved) {
        invalidate(Dirty::paint);
        return;
    }
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(selection_changed_, (*editing_).selection());
}

std::optional<std::size_t> ContentTextEditor::line_of(Utf8Offset offset) const {
    if (!layout_current() || layout_.lines.empty()) return std::nullopt;
    // A line holds the offsets from its start to before its end; its end
    // belongs to it only when no later line starts there (a wrapped line's
    // end is the next line's start, where the caret goes).
    const std::vector<ContentTextLine>::const_iterator after =
        std::upper_bound(layout_.lines.begin(), layout_.lines.end(), offset, EndsAfter{});
    if (after != layout_.lines.end() && (*after).range.start <= offset) {
        const std::size_t index = static_cast<std::size_t>(after - layout_.lines.begin());
        return index;
    }
    if (after == layout_.lines.begin()) return std::nullopt;
    const std::vector<ContentTextLine>::const_iterator before = after - 1;
    if ((*before).range.end != offset) return std::nullopt;
    const std::size_t index = static_cast<std::size_t>(before - layout_.lines.begin());
    return index;
}

double ContentTextEditor::boundary_x_on_line(std::size_t line, Utf8Offset offset) const {
    const ContentCaretStop* const stop = find_caret_stop(layout_, offset);
    if (stop != nullptr) {
        const double centre = (*stop).caret.x + (*stop).caret.width / 2.0;
        return centre;
    }
    const std::vector<ContentTextCluster>::const_iterator first =
        std::lower_bound(layout_.clusters.begin(), layout_.clusters.end(), line, ClusterLineBefore{});
    const std::vector<ContentTextCluster>::const_iterator last =
        std::lower_bound(first, layout_.clusters.end(), line + 1U, ClusterLineBefore{});
    const std::vector<ContentTextCluster>::const_iterator holding = std::upper_bound(first, last, offset, EndsAfter{});
    if (holding != last && (*holding).range.start <= offset) {
        const double x = cluster_x(*holding, offset);
        return x;
    }
    if (holding != first) {
        const ContentTextCluster& previous = *(holding - 1);
        if (previous.range.end == offset) {
            const double x = cluster_x(previous, offset);
            return x;
        }
    }
    const double left = layout_.lines[line].bounds.x;
    return left;
}

std::optional<Rect> ContentTextEditor::caret_at(Utf8Offset offset) const {
    if (!layout_current()) return std::nullopt;
    const ContentCaretStop* const stop = find_caret_stop(layout_, offset);
    if (stop != nullptr) {
        const Rect caret = (*stop).caret;
        return caret;
    }
    const std::optional<std::size_t> line = line_of(offset);
    if (!line) return std::nullopt;
    const double x = boundary_x_on_line(*line, offset);
    const Rect& box = layout_.lines[*line].bounds;
    const Rect caret{x - editor_caret_width / 2.0, box.y, editor_caret_width, box.height};
    return caret;
}

Utf8Offset ContentTextEditor::position_on_line(std::size_t line, double x) const {
    const TextStore& store = (*editing_).store();
    const Utf8Range range = layout_.lines[line].range;
    Utf8Offset best = range.start;
    double best_distance = std::abs(boundary_x_on_line(line, best) - x);
    Utf8Offset cursor = range.start;
    while (cursor < range.end) {
        cursor = store.next_grapheme_boundary(cursor);
        const double distance = std::abs(boundary_x_on_line(line, cursor) - x);
        if (distance < best_distance) {
            best = cursor;
            best_distance = distance;
        }
    }
    return best;
}

bool ContentTextEditor::move_by_line(KeyEvent& event) {
    const std::uint32_t key = event.physical_key;
    const bool vertical = key == PhysicalKey::up || key == PhysicalKey::down;
    const bool home_end = key == PhysicalKey::home || key == PhysicalKey::end;
    if (!multiline_ || (!vertical && !home_end)) return false;
    const TextSelection selection = (*editing_).selection();
    const std::optional<std::size_t> row = line_of(selection.caret);
    if (!row) {
        // Without a current layout Up and Down do nothing (the engine's Home
        // and End still go to the ends of the text).
        if (vertical) event.handled = true;
        return vertical;
    }
    const bool extend = includes(event.modifiers, Modifier::shift);
    const bool command = command_modifier(event.modifiers);
    Utf8Offset next{};
    double goal = preferred_x_;
    if (home_end) {
        const bool first = key == PhysicalKey::home;
        const ContentTextLine& line = layout_.lines[*row];
        if (command) {
            next = first ? Utf8Offset(0U) : (*editing_).store().utf8_size();
        } else {
            next = first ? line.range.start : line.range.end;
        }
    } else {
        if (goal < 0.0) goal = boundary_x_on_line(*row, selection.caret);
        const bool up = key == PhysicalKey::up;
        const std::size_t last = layout_.lines.size() - 1U;
        std::size_t target = *row;
        if (up && target > 0U) target -= 1U;
        if (!up && target < last) target += 1U;
        next = position_on_line(target, goal);
    }
    const TextSelection moved = extend ? TextSelection{selection.anchor, next} : TextSelection{next, next};
    set_selection(moved);
    // set_selection forgets the goal; Up and Down keep it.
    if (vertical) preferred_x_ = goal;
    event.handled = true;
    return true;
}

void ContentTextEditor::handle_key(KeyEvent& event) {
    if (!focused_ || !enabled.read() || event.action != KeyAction::down) return;
    if (move_by_line(event)) return;
    if (event.physical_key == PhysicalKey::enter) {
        if ((*editing_).composing()) {
            // The composition is the input method's to commit.
            event.handled = true;
            return;
        }
        if (multiline_) {
            if (!(*editing_).read_only()) static_cast<void>((*editing_).replace_selection("\n"));
            event.handled = true;
        }
        // A single-line editor lets Enter go on to the accept button.
        return;
    }
    if (event.physical_key == PhysicalKey::escape) {
        if ((*editing_).composing()) {
            event.handled = true;
            return;
        }
        if (selecting_ && has_pointer_capture()) {
            // Escape ends a selection drag, keeping what it selected so far.
            selecting_ = false;
            set_pointer_capture(false);
            event.handled = true;
        }
        return;
    }
    // The editing keys: select all, the clipboard, undo and redo, Left and
    // Right, Home and End of the text, Backspace and Delete.
    (*editing_).handle_key(event);
}

void ContentTextEditor::handle_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    if (event.action == PointerAction::down && event.button == PointerButton::primary) {
        if (window() != nullptr) static_cast<void>((*window()).request_focus(shared_from_this()));
        const Utf8Offset position = position_at(local);
        const bool extend = includes(event.modifiers, Modifier::shift);
        const TextSelection placed = extend ? TextSelection{(*editing_).selection().anchor, position}
                                            : TextSelection{position, position};
        set_selection(placed);
        selecting_ = true;
        set_pointer_capture(true);
        event.handled = true;
        return;
    }
    const bool dragging = event.action == PointerAction::move && selecting_ && has_pointer_capture();
    const bool releasing = event.action == PointerAction::up && event.button == PointerButton::primary && selecting_;
    if (!dragging && !releasing) return;
    const Utf8Offset position = position_at(local);
    const TextSelection extended{.anchor = (*editing_).selection().anchor, .caret = position};
    set_selection(extended);
    if (releasing) {
        selecting_ = false;
        set_pointer_capture(false);
    }
    event.handled = true;
}

void ContentTextEditor::on_mouse_down(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_down(arguments);
}

void ContentTextEditor::on_mouse_up(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_up(arguments);
}

void ContentTextEditor::on_mouse_move(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_move(arguments);
}

// The key event is raised first; an unhandled key reaches handle_key.
void ContentTextEditor::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled) return;
    KeyEvent event = key_event_of(arguments, KeyAction::down);
    handle_key(event);
    if (event.handled) arguments.handled = true;
}

void ContentTextEditor::on_text_input(TextInputEvent& event) {
    if (!focused_ || !enabled.read()) return;
    (*editing_).handle_text_input(event);
}

void ContentTextEditor::on_focus_changed(bool focused) {
    focused_ = focused;
    selecting_ = false;
    (*editing_).end_composition();
    if (focused_) {
        reset_caret_blink();
    } else {
        caret_timer_.disconnect();
        caret_visible_ = false;
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void ContentTextEditor::reset_caret_blink() {
    preferred_x_ = -1.0;
    caret_visible_ = true;
    caret_timer_.disconnect();
    if (!focused_ || !attached() || window() == nullptr) return;
    // A UI timer that rests while the window is occluded (ADR-016), replaced
    // on every caret movement so the caret stays shown for a whole phase.
    const std::optional<FrameInterval> phase = detail::caret_blink_phase(*window());
    if (!phase) return;
    const FrameTime now = FrameClock::now();
    const std::shared_ptr<ContentTextEditor> self = std::static_pointer_cast<ContentTextEditor>(shared_from_this());
    const detail::CaretBlinkSchedule schedule{window()};
    caret_timer_ = schedule(*this, *phase, now + *phase, CaretTick{std::weak_ptr<ContentTextEditor>(self)});
}

void ContentTextEditor::toggle_caret() {
    if (!focused_) return;
    caret_visible_ = !caret_visible_;
    const std::optional<Rect> caret = caret_rectangle();
    if (caret) {
        invalidate(*caret);
    } else {
        invalidate(Dirty::paint);
    }
}

std::optional<Rect> ContentTextEditor::text_input_caret() const {
    const std::optional<Rect> caret = caret_rectangle();
    return caret;
}

void ContentTextEditor::register_presentation() {
    Window* const window = attached_window();
    if (window == nullptr) return;
    std::shared_ptr<LiveSurface> shown = surface_;
    if (!shown) {
        // The window keeps a control's registration until it leaves, so an
        // editor without frames registers an empty surface.
        if (!placeholder_) {
            LiveSurfaceDescription description;
            description.width = 1U;
            description.height = 1U;
            placeholder_ = LiveSurface::create(description);
        }
        shown = placeholder_;
    }
    if (!shown) return;
    const Control::Ptr self = shared_from_this();
    static_cast<void>((*window).queue_live_surface_presentation(self, std::move(shown)));
}

void ContentTextEditor::update_request() {
    if (!channel_) return;
    Window* const window = attached_window();
    if (window == nullptr) return;
    const Rect bounds = committed_arranged_bounds();
    const double scale = (*window).scale();
    const ContentPixelSize size{editor_device_extent(bounds.width, scale), editor_device_extent(bounds.height, scale)};
    static_cast<void>(detail::set_content_request(*channel_, size, scale));
}

void ContentTextEditor::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), content_text_editor_interactions.begin(),
                        content_text_editor_interactions.end());
}

void ContentTextEditor::arrange(Rect final_bounds) {
    Panel::arrange(final_bounds);
    const Rect bounds = committed_arranged_bounds();
    const Size size{bounds.width, bounds.height};
    update_request();
    if (size == arranged_size_) return;
    arranged_size_ = size;
    // A new size may wrap the text differently: the consumer lays it out
    // again for the same revision.
    publish_change(layout_requested_, text_revision());
}

void ContentTextEditor::on_paint(Painter& painter, Rect local_damage) {
    static_cast<void>(local_damage);
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width, committed_arranged_bounds().height};
    if (surface_) painter.draw_live_surface(surface_, bounds, 1.0);
    if (!draws_selection_ || !layout_current()) return;
    const AnatomyStateSet states = anatomy_states();
    const std::vector<Rect> highlight = selection_rectangles();
    for (const Rect& box : highlight) {
        PartPaint plate;
        plate.family = ControlVisualRole::selection;
        plate.role = PartRole::selection;
        plate.bounds = box;
        plate.states = states;
        plate.states.insert(AnatomyState::selected);
        plate.states.insert(AnatomyState::focused);
        paint_part(painter, *this, plate);
    }
    if (!focused_ || !caret_visible_ || !(*editing_).selection().empty()) return;
    const std::optional<Rect> caret = caret_rectangle();
    if (!caret) return;
    PartPaint bar;
    bar.family = ControlVisualRole::editor;
    bar.role = PartRole::caret;
    bar.bounds = *caret;
    bar.states = states;
    paint_part(painter, *this, bar);
}

const ControlAnatomy& ContentTextEditor::anatomy() const noexcept {
    return content_text_editor_anatomy;
}

SemanticDescriptor ContentTextEditor::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::text_box;
    descriptor.name = accessible_name.read();
    descriptor.value = std::string((*editing_).text());
    descriptor.description = accessible_description.read();
    if ((*editing_).read_only()) descriptor.states |= SemanticState::read_only;
    const TextSelection selection = (*editing_).selection();
    descriptor.text_selection = SemanticTextSelection{.anchor = selection.anchor, .caret = selection.caret};
    descriptor.multiline = multiline_;
    descriptor.actions = {SemanticAction::focus};
    if (!(*editing_).read_only()) descriptor.actions.push_back(SemanticAction::set_value);
    descriptor.exposed = true;
    return descriptor;
}

bool ContentTextEditor::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::set_value && !(*editing_).read_only()) {
        const Utf8Offset start(0U);
        const Utf8Offset end = (*editing_).store().utf8_size();
        const bool replaced = (*editing_).replace(start, end, value, TextEditKind::value);
        return replaced;
    }
    const bool handled = Panel::on_semantic_action(action, value);
    return handled;
}

// The text's layout for assistive technology (A-T1): the consumer's lines,
// and each cluster divided into its graphemes (a ligature's by the fraction
// of its bytes), in window coordinates. Without a current layout the text
// is still published: one line and its graphemes, with empty boxes.
SemanticTextResult ContentTextEditor::on_semantic_text_layout(SemanticTextLayout& layout) {
    const TextStore& store = (*editing_).store();
    const std::size_t graphemes = store.grapheme_count().value();
    layout.graphemes.reserve(graphemes);
    if (layout_current()) {
        layout.lines.reserve(layout_.lines.size());
        for (const ContentTextLine& line : layout_.lines) {
            layout.lines.push_back({.range = line.range, .bounds = rectangle_to_window(line.bounds)});
        }
        for (const ContentTextCluster& cluster : layout_.clusters) {
            Utf8Offset cursor = cluster.range.start;
            while (cursor < cluster.range.end) {
                const Utf8Offset next = store.next_grapheme_boundary(cursor);
                const double first_x = cluster_x(cluster, cursor);
                const double second_x = cluster_x(cluster, next);
                const Rect box{std::min(first_x, second_x), cluster.bounds.y, std::abs(second_x - first_x),
                               cluster.bounds.height};
                const TextRange<Utf8Offset> range{cursor, next};
                layout.graphemes.push_back(
                    {.range = range, .line = LineIndex(cluster.line), .bounds = rectangle_to_window(box)});
                cursor = next;
            }
        }
    } else {
        const Utf8Offset end = store.utf8_size();
        layout.lines.push_back({.range = {Utf8Offset(0U), end}, .bounds = Rect{}});
        Utf8Offset cursor(0U);
        while (cursor < end) {
            const Utf8Offset next = store.next_grapheme_boundary(cursor);
            layout.graphemes.push_back({.range = {cursor, next}, .line = LineIndex(0U), .bounds = Rect{}});
            cursor = next;
        }
    }
    (*editing_).fill_word_starts(layout);
    return SemanticTextResult::done;
}

SemanticTextResult ContentTextEditor::on_set_semantic_text_selection(SemanticTextSelection selection) {
    const TextStore& store = (*editing_).store();
    if (!store.is_grapheme_boundary(selection.anchor) || !store.is_grapheme_boundary(selection.caret)) {
        return SemanticTextResult::off_boundary;
    }
    set_selection({selection.anchor, selection.caret});
    return SemanticTextResult::done;
}

void ContentTextEditor::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = client_rectangle();
    const Panel::Parts panel = panel_parts();
    Parts record;
    record.scrollable = panel.scrollable;
    if (surface_) record.content = PartGeometry::from_bounds(bounds);
    if (focused_ && caret_visible_ && (*editing_).selection().empty()) {
        const std::optional<Rect> caret = caret_rectangle();
        if (caret) record.caret = PartGeometry::from_bounds(*caret);
    }
    const std::vector<Rect> highlight = selection_rectangles();
    if (!highlight.empty()) {
        Rect united = highlight.front();
        for (const Rect& box : highlight) united = Rect::united(united, box);
        record.selection = PartGeometry::from_bounds(united);
    }
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), content_text_editor_extra_part_fields, record, geometry);
}

ContentTextEditor::Parts ContentTextEditor::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(), record.scrollable);
    read_parts(anatomy(), content_text_editor_extra_part_fields, published.parts(), record);
    return record;
}

ContentTextEditor::States ContentTextEditor::states() const {
    States record;
    read_states(content_text_editor_state_fields, anatomy_states(), record);
    return record;
}

AnatomyStateSet ContentTextEditor::anatomy_states() const {
    AnatomyStateSet states = Panel::anatomy_states();
    if ((*editing_).read_only()) states.insert(AnatomyState::read_only);
    return states;
}

void ContentTextEditor::on_attached_to_window() {
    Panel::on_attached_to_window();
    Window* const window = attached_window();
    if (window != nullptr && channel_) {
        detail::set_content_listener(*channel_, (*window).dispatcher(), notices_);
    }
    register_presentation();
    update_request();
    if (focused_) reset_caret_blink();
}

void ContentTextEditor::on_detaching_from_window(SurfaceRoot& former_window) noexcept {
    caret_timer_.disconnect();
    if (channel_) {
        detail::set_content_listener(*channel_, WindowDispatcher{}, std::weak_ptr<detail::ContentChannelListener>{});
    }
    Panel::on_detaching_from_window(former_window);
}

} // namespace gui_forms
