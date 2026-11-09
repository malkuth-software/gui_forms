// The drag engine (roadmap 2.1, phase E1; the declarative dragging study's
// section 6): list reorder forward and back, every cancelling end leaving
// the model untouched, release outside declared targets, Ctrl suppression,
// ties by declaration order, chaining, the keyboard path and keyboard
// reachability equal to pointer reachability, event-rate independence,
// reduced-motion equivalence, refusals at the caps, the list producer's
// inverse against a full scan, the drop-zone map, foreign declared
// targets, the rules' drag conditions and action, the trace text, seeded
// property runs, and no allocation from the first dragging frame to the end.
// Phase E4: the typed drag interactions in order (and a handler that
// cancels) and the announcements' live region.
// Headless: a window that is never shown.

#include "gui_forms/gui_forms.hpp"
#include "../src/core/drag_engine/drag_engine.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

// Counts allocation attempts on this thread while enabled. Replacement
// allocation is confined to this executable.
namespace drag_allocation_probe {
thread_local bool enabled{};
thread_local std::size_t attempts{};

void* allocate(const std::size_t bytes) {
    if (enabled) ++attempts;
    const std::size_t extent = bytes == 0U ? 1U : bytes;
    void* memory = std::malloc(extent);
    if (!memory) throw std::bad_alloc();
    return memory;
}

void* allocate_aligned(const std::size_t bytes, const std::size_t alignment) {
    if (enabled) ++attempts;
    const std::size_t extent = bytes == 0U ? 1U : bytes;
    void* memory = nullptr;
#ifdef _WIN32
    memory = _aligned_malloc(extent, alignment);
#else
    const int status = posix_memalign(&memory, alignment, extent);
    if (status != 0) throw std::bad_alloc();
#endif
    if (!memory) throw std::bad_alloc();
    return memory;
}

void release_aligned(void* const memory) noexcept {
#ifdef _WIN32
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

class CountScope final {
public:
    CountScope() {
        attempts = 0U;
        enabled = true;
    }
    ~CountScope() { enabled = false; }
    CountScope(const CountScope&) = delete;
    CountScope& operator=(const CountScope&) = delete;
};
} // namespace drag_allocation_probe

void* operator new(const std::size_t bytes) {
    void* const memory = drag_allocation_probe::allocate(bytes);
    return memory;
}
void* operator new[](const std::size_t bytes) {
    void* const memory = drag_allocation_probe::allocate(bytes);
    return memory;
}
void operator delete(void* const memory) noexcept { std::free(memory); }
void operator delete[](void* const memory) noexcept { std::free(memory); }
void operator delete(void* const memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* const memory, std::size_t) noexcept { std::free(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment) {
    void* const memory = drag_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment) {
    void* const memory = drag_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void operator delete(void* const memory, const std::align_val_t) noexcept {
    drag_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t) noexcept {
    drag_allocation_probe::release_aligned(memory);
}
void operator delete(void* const memory, std::size_t, const std::align_val_t) noexcept {
    drag_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, std::size_t, const std::align_val_t) noexcept {
    drag_allocation_probe::release_aligned(memory);
}
// The nothrow forms too: the standard library uses them (std::stable_sort's
// temporary buffer), and every form must come from the same allocator as its
// matching delete, or a sanitizer reports an allocation/deallocation mismatch.
void* operator new(const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = drag_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = drag_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = drag_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = drag_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    drag_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    drag_allocation_probe::release_aligned(memory);
}


namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// Five items of 20 units in a column at x 10, y 10 onward.
constexpr double item_x = 10.0;
constexpr double item_width = 100.0;
constexpr double item_height = 20.0;
constexpr double first_item_y = 10.0;

std::vector<Rect> uniform_slots(std::size_t count) {
    std::vector<Rect> slots;
    slots.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const double y = first_item_y + static_cast<double>(index) * item_height;
        slots.push_back(Rect{item_x, y, item_width, item_height});
    }
    return slots;
}

Point centre(Rect rect) {
    const Point point{rect.x + rect.width * 0.5, rect.y + rect.height * 0.5};
    return point;
}

PointerEvent pointer_event(PointerAction action, Point position, Modifier modifiers = Modifier::none) {
    PointerEvent event{};
    event.action = action;
    event.button = action == PointerAction::move ? PointerButton::none : PointerButton::primary;
    event.position = position;
    event.modifiers = modifiers;
    return event;
}

KeyEvent key_down(std::uint32_t physical_key, Modifier modifiers = Modifier::none) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = physical_key;
    event.modifiers = modifiers;
    return event;
}

// What a test client's plan declares.
enum class PlanShape : std::uint8_t {
    list,
    list_chained,
    landings,
    targets,
    suppressed_targets,
    foreign,
    too_many_candidates,
    too_many_operators,
    bad_child,
};

// A client over a list model (`order`): a drop moves the item at `subject`
// to the dropped slot. Its records are reserved so that previews and ends
// never allocate while the allocation probe counts.
class TestClient final : public DragClient {
public:
    TestClient() {
        previews.reserve(4096U);
        ends.reserve(64U);
        order.reserve(64U);
    }

    PlanShape shape{PlanShape::list};
    std::vector<int> order{};
    std::vector<Rect> slots{};
    std::vector<DragLanding> declared{};
    std::size_t subject{};
    DragMetric metric{DragMetric::axis_y};
    double stickiness{};
    double snap_radius{5.0};
    bool settles{};
    bool refuse_odd{};
    Control* container{};
    std::vector<std::uint32_t> previews{};
    std::vector<DragOutcome> ends{};
    std::size_t chains{};
    // The typed interactions this client raises, or none.
    DragInteractionEvents* events{};

    void reset_list(std::size_t count, std::size_t dragged) {
        slots = uniform_slots(count);
        order.clear();
        for (std::size_t index = 0U; index < count; ++index) order.push_back(static_cast<int>(index));
        subject = dragged;
    }

    DragPlanRefusal plan_drag(DragPlanBuilder& builder, const DragSubject&) override {
        DragNodeIndex root = no_drag_node;
        const std::span<const DragLanding> landings(declared.data(), declared.size());
        switch (shape) {
        case PlanShape::list:
        case PlanShape::list_chained: {
            DragListGeometry list{};
            list.axis = DragAxis::y;
            list.slots = std::span<const Rect>(slots.data(), slots.size());
            list.subject = subject;
            const DragCandidateRange range = builder.add_list(list);
            root = builder.nearest(range, metric, stickiness);
            if (shape == PlanShape::list_chained) root = builder.snap(root, snap_radius, DragChain::on);
            break;
        }
        case PlanShape::landings: {
            const DragCandidateRange range = builder.add_landings(landings);
            root = builder.nearest(range, metric, stickiness);
            break;
        }
        case PlanShape::targets:
        case PlanShape::suppressed_targets: {
            const DragCandidateRange range = builder.add_landings(landings);
            root = builder.target(range);
            if (shape == PlanShape::suppressed_targets) root = builder.suppress(root, Modifier::control);
            break;
        }
        case PlanShape::foreign:
            root = builder.foreign_targets(landings);
            break;
        case PlanShape::too_many_candidates: {
            const std::vector<DragLanding> many(maximum_drag_candidates + 1U, DragLanding{});
            const DragCandidateRange range = builder.add_landings(std::span<const DragLanding>(many.data(), many.size()));
            root = builder.nearest(range, metric, 0.0);
            break;
        }
        case PlanShape::too_many_operators: {
            DragProjection projection{};
            projection.maximum = 10.0;
            root = builder.project(projection);
            for (std::size_t index = 0U; index < maximum_drag_operators; ++index) {
                root = builder.suppress(root, Modifier::control);
            }
            break;
        }
        case PlanShape::bad_child:
            root = builder.fallback(no_drag_node, no_drag_node, 0.0);
            break;
        default:
            break;
        }
        if (container != nullptr) builder.group_container(DragGroup{}, *container);
        builder.drop_motion(settles);
        const DragPlanRefusal refusal = builder.finish(root);
        return refusal;
    }

    bool accepts_drag_candidate(const DragLanding& landing) override {
        const bool odd = landing.token.value % 2U == 1U;
        const bool accepted = !refuse_odd || !odd || landing.home;
        return accepted;
    }

    void preview_drag(const DragFrame& frame) override { previews.push_back(frame.selection.token.value); }

    void announce_drag(const DragFrame& frame, DragAnnouncement& announcement) override {
        std::array<char, 64> text{};
        const unsigned position = frame.selection.token.value + 1U;
        const unsigned long count = static_cast<unsigned long>(slots.size());
        const int written = std::snprintf(text.data(), text.size(), "Item %u of %lu", position, count);
        if (written <= 0) return;
        announcement.assign(std::string_view(text.data(), static_cast<std::size_t>(written)));
    }

    void commit_drag_chain(const DragOutcome& outcome) override {
        move(subject, outcome.selection.token.value);
        subject = outcome.selection.token.value;
        ++chains;
    }

    DragInteractionEvents* drag_interactions() noexcept override { return events; }

    void end_drag(const DragOutcome& outcome) override {
        ends.push_back(outcome);
        const bool listed = shape == PlanShape::list || shape == PlanShape::list_chained;
        if (outcome.end == DragEnd::dropped && listed) move(subject, outcome.selection.token.value);
    }

private:
    // Moves the item at `from` to slot `to` (within the reserved capacity).
    void move(std::size_t from, std::size_t to) {
        if (from >= order.size() || to >= order.size() || from == to) return;
        const int item = order[from];
        order.erase(order.begin() + static_cast<std::ptrdiff_t>(from));
        order.insert(order.begin() + static_cast<std::ptrdiff_t>(to), item);
    }
};

// A subject control; begin_keyboard_drag starts its client's keyboard drag.
class DragItem final : public Control {
public:
    explicit DragItem(StableId id) : Control(std::move(id)) {}

    bool begin_keyboard_drag() override {
        SurfaceRoot* const root = surface_root();
        if (root == nullptr || client == nullptr || owner == nullptr) return false;
        const bool started = (*root).drag_engine().begin_keyboard(*client, *owner, *this, 0U);
        return started;
    }

    TestClient* client{};
    Control* owner{};
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    // Holds the item: group 0's container.
    std::shared_ptr<Panel> container;
    std::shared_ptr<DragItem> item;
    // The client's observed owner, outside the container.
    std::shared_ptr<Panel> owner;
    std::unique_ptr<Window> window;
};

// A 400 by 400 window; the item at `item_bounds` inside a 200 by 200
// container at the origin.
Fixture make_fixture(Rect item_bounds) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId("drag.root"));
    fixture.container = make_control<Panel>(StableId("drag.container"));
    (*fixture.container).set_requested_bounds({0.0, 0.0, 200.0, 200.0});
    fixture.item = make_control<DragItem>(StableId("drag.item"));
    (*fixture.item).set_requested_bounds(item_bounds);
    fixture.owner = make_control<Panel>(StableId("drag.owner"));
    (*fixture.owner).set_requested_bounds({300.0, 0.0, 20.0, 20.0});
    (*fixture.container).add_child(fixture.item);
    (*fixture.root).add_child(fixture.container);
    (*fixture.root).add_child(fixture.owner);
    fixture.window = std::make_unique<Window>(fixture.root, Size{400.0, 400.0});
    (*fixture.window).perform_layout();
    return fixture;
}

bool press(Fixture& fixture, TestClient& client, Point at, double threshold) {
    DragEngine& engine = (*fixture.window).drag_engine();
    const Rect source = client.slots.empty() ? Rect{at.x - 5.0, at.y - 5.0, 10.0, 10.0} : client.slots[client.subject];
    const bool armed = engine.arm(client, *fixture.owner, *fixture.item, 0U, at, source, threshold);
    return armed;
}

void move_to(Fixture& fixture, Point at, Modifier modifiers = Modifier::none) {
    static_cast<void>((*fixture.window).drag_engine().pointer(pointer_event(PointerAction::move, at, modifiers)));
}

void release_at(Fixture& fixture, Point at) {
    static_cast<void>((*fixture.window).drag_engine().pointer(pointer_event(PointerAction::up, at)));
}

// A declared tile at (x, y), 30 units square, with its token and group.
DragLanding tile(double x, double y, std::uint32_t token, std::uint16_t group) {
    DragLanding landing{};
    landing.rect = Rect{x, y, 30.0, 30.0};
    landing.anchor = centre(landing.rect);
    landing.token.value = token;
    landing.group.value = group;
    return landing;
}

// -- reorder ----------------------------------------------------------------------------

void list_reorder_forward_and_back() {
    TestClient client;
    client.reset_list(5U, 1U);
    Fixture fixture = make_fixture(client.slots[1]);
    client.container = fixture.container.get();
    DragEngine& engine = (*fixture.window).drag_engine();

    const Point grab = centre(client.slots[1]);
    require(press(fixture, client, grab, 4.0) && engine.phase() == DragPhase::armed, "a press arms a drag");
    require((*fixture.item).has_pointer_capture(), "the engine takes the capture for the subject");
    move_to(fixture, {grab.x, grab.y + 2.0});
    require(engine.phase() == DragPhase::armed, "a move under the threshold keeps the drag armed");
    move_to(fixture, {grab.x, 81.0});
    require(engine.phase() == DragPhase::dragging && engine.epoch().value == 1U, "past the threshold the plan is made");
    require(engine.selection().kind == DragSelectionKind::candidate && engine.selection().token.value == 3U,
            "the nearest landing along the axis is selected (slot 3, anchor 80)");
    require(!client.previews.empty() && client.previews.back() == 3U, "the client previews the selection");
    require(engine.announcement() == "Item 4 of 5", "the client's announcement is kept in fixed storage");
    const std::span<const DragLanding> landings = engine.landings();
    require(landings.size() == 5U && landings[3].rect.y == 70.0 && landings[0].rect.y == 10.0 && landings[1].home,
            "a forward landing ends where its slot ends; a backward one starts where its slot starts");
    release_at(fixture, {grab.x, 81.0});
    require(engine.phase() == DragPhase::idle && client.ends.size() == 1U && client.ends.back().end == DragEnd::dropped,
            "the release drops on the selected candidate");
    const std::vector<int> forward{0, 2, 3, 1, 4};
    require(client.order == forward, "the client commits the move once");
    require(!(*fixture.item).has_pointer_capture(), "the end releases the capture");

    client.subject = 3U;
    const Point again = centre(client.slots[3]);
    require(press(fixture, client, again, 4.0), "a second press arms");
    move_to(fixture, {again.x, 20.0});
    release_at(fixture, {again.x, 20.0});
    const std::vector<int> back{1, 0, 2, 3, 4};
    require(client.order == back, "a drag back moves the item to the first slot");
    const DragStatistics& statistics = engine.statistics();
    require(statistics.ends[static_cast<std::size_t>(DragEnd::dropped)] == 2U && statistics.storage_growths == 0U,
            "two drops, and no storage growth after the first planning");

    // A release before the threshold is no drag: the caller keeps the click.
    require(press(fixture, client, again, 4.0), "a third press arms");
    const bool taken = engine.pointer(pointer_event(PointerAction::up, again));
    require(!taken && engine.phase() == DragPhase::idle && client.ends.size() == 2U,
            "a release before the threshold disarms without an end");
}

// -- cancellation -------------------------------------------------------------------------

enum class CancelBy : std::uint8_t { escape, capture, deactivation, model, subject_gone };

void cancel_once(CancelBy how, DragEnd expected) {
    TestClient client;
    client.reset_list(5U, 0U);
    Fixture fixture = make_fixture(client.slots[0]);
    Window& window = *fixture.window;
    DragEngine& engine = window.drag_engine();
    window.set_active(true);
    const std::vector<int> before = client.order;
    const Point grab = centre(client.slots[0]);
    require(press(fixture, client, grab, 0.0), "a press with no threshold plans at once");
    require(engine.phase() == DragPhase::dragging, "planning ran at the press");
    move_to(fixture, {grab.x, 95.0});
    require(engine.selection().token.value == 4U, "the drag reaches the last slot");
    switch (how) {
    case CancelBy::escape:
        require(window.dispatch_key(key_down(PhysicalKey::escape)), "the engine takes Escape first");
        break;
    case CancelBy::capture:
        (*fixture.item).set_pointer_capture(false);
        break;
    case CancelBy::deactivation:
        window.set_active(false);
        break;
    case CancelBy::model:
        engine.cancel(DragEnd::model_changed);
        break;
    case CancelBy::subject_gone:
        (*fixture.owner).dispose();
        move_to(fixture, {grab.x, 90.0});
        break;
    default:
        break;
    }
    require(engine.phase() == DragPhase::idle, "every cancel ends the session");
    require(client.order == before, "a cancelled drag leaves the model untouched");
    const std::size_t index = static_cast<std::size_t>(expected);
    require(engine.statistics().ends[index] == 1U, "the end is counted by kind");
    if (how == CancelBy::subject_gone) {
        require(client.ends.empty(), "a gone client is never called");
        return;
    }
    require(client.ends.size() == 1U && client.ends.back().end == expected &&
                client.ends.back().selection.kind == DragSelectionKind::none,
            "the client is asked to restore, with no candidate");
}

void cancels_leave_the_model_untouched() {
    cancel_once(CancelBy::escape, DragEnd::escaped);
    cancel_once(CancelBy::capture, DragEnd::capture_lost);
    cancel_once(CancelBy::deactivation, DragEnd::deactivated);
    cancel_once(CancelBy::model, DragEnd::model_changed);
    cancel_once(CancelBy::subject_gone, DragEnd::subject_gone);
}

// -- declared targets ------------------------------------------------------------------------

void release_outside_and_suppression() {
    TestClient client;
    client.shape = PlanShape::targets;
    client.declared = {tile(250.0, 250.0, 10U, 0U), tile(300.0, 250.0, 11U, 0U)};
    Fixture fixture = make_fixture({10.0, 10.0, 20.0, 20.0});
    DragEngine& engine = (*fixture.window).drag_engine();
    require(press(fixture, client, {20.0, 20.0}, 0.0), "a target drag arms");
    move_to(fixture, {260.0, 260.0});
    require(engine.selection().token.value == 10U, "the pointer inside a target selects it");
    move_to(fixture, {200.0, 200.0});
    require(engine.selection().kind == DragSelectionKind::none, "outside every target nothing is selected");
    release_at(fixture, {200.0, 200.0});
    require(client.ends.size() == 1U && client.ends.back().end == DragEnd::rejected,
            "a release where nothing accepts is rejected");

    client.shape = PlanShape::suppressed_targets;
    require(press(fixture, client, {20.0, 20.0}, 0.0), "a suppressed target drag arms");
    move_to(fixture, {310.0, 260.0}, Modifier::control);
    require(engine.selection().kind == DragSelectionKind::none, "Ctrl held suppresses the targets");
    move_to(fixture, {310.0, 260.0});
    require(engine.selection().token.value == 11U, "without Ctrl the target is selected again");
    release_at(fixture, {310.0, 260.0});
    require(client.ends.back().end == DragEnd::dropped && client.ends.back().selection.token.value == 11U,
            "the release drops on the target");
}

void ties_by_declaration_order() {
    TestClient client;
    client.shape = PlanShape::landings;
    client.metric = DragMetric::euclidean;
    client.declared = {tile(100.0, 100.0, 7U, 0U), tile(100.0, 100.0, 8U, 0U), tile(150.0, 100.0, 9U, 0U)};
    Fixture fixture = make_fixture({10.0, 10.0, 30.0, 30.0});
    DragEngine& engine = (*fixture.window).drag_engine();
    const Rect source{10.0, 10.0, 30.0, 30.0};
    require(engine.arm(client, *fixture.owner, *fixture.item, 0U, centre(source), source, 0.0), "a drag arms");
    const std::uint64_t ties_before = engine.statistics().ties;
    move_to(fixture, centre(client.declared[0].rect));
    require(engine.selection().token.value == 7U, "of two coinciding landings the earlier wins");
    require(engine.statistics().ties == ties_before + 1U, "the tie is counted");
    move_to(fixture, centre(client.declared[2].rect));
    require(engine.selection().token.value == 9U, "a distinct landing is selected without a tie");
    require(engine.statistics().ties == ties_before + 1U, "no tie is counted there");
    engine.cancel(DragEnd::escaped);
}

void chaining() {
    TestClient client;
    client.shape = PlanShape::list_chained;
    client.reset_list(5U, 0U);
    Fixture fixture = make_fixture(client.slots[0]);
    DragEngine& engine = (*fixture.window).drag_engine();
    const Point grab = centre(client.slots[0]);
    require(press(fixture, client, grab, 0.0), "a chained list drag arms");
    move_to(fixture, {grab.x, 40.0});
    require(client.chains == 1U && engine.epoch().value == 2U, "reaching slot 1 commits it and plans again");
    require(engine.selection().kind == DragSelectionKind::candidate && engine.landings()[engine.selection().landing].home,
            "after the chain point the subject's new place is home");
    move_to(fixture, {grab.x, 50.0});
    require(engine.selection().kind == DragSelectionKind::none, "between slots, outside the snap radius, nothing");
    move_to(fixture, {grab.x, 60.0});
    require(client.chains == 2U && engine.epoch().value == 3U, "slot 2 chains again");
    release_at(fixture, {grab.x, 60.0});
    const std::vector<int> expected{1, 2, 0, 3, 4};
    require(client.order == expected, "one gesture made two moves");
    require(client.ends.back().end == DragEnd::dropped && client.ends.back().epoch.value == 3U,
            "the release drops home in the latest epoch");
}

// -- the keyboard -----------------------------------------------------------------------------

void keyboard_path() {
    TestClient client;
    client.reset_list(5U, 2U);
    Fixture fixture = make_fixture(client.slots[2]);
    Window& window = *fixture.window;
    DragEngine& engine = window.drag_engine();
    require(engine.begin_keyboard(client, *fixture.owner, *fixture.item, 0U), "the keyboard mode starts");
    require(engine.mode() == DragInputMode::keyboard && engine.selection().token.value == 2U,
            "it starts on the subject's own place");
    require(engine.announcement() == "Item 3 of 5", "the start is announced");
    require(window.dispatch_key(key_down(PhysicalKey::down)) && engine.selection().token.value == 3U, "Down steps once");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::down)));
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::down)));
    require(engine.selection().token.value == 4U, "the last slot stops the stepping");
    for (int step = 0; step < 3; ++step) static_cast<void>(window.dispatch_key(key_down(PhysicalKey::up)));
    require(engine.selection().token.value == 1U && engine.announcement() == "Item 2 of 5", "Up steps back");
    require(!window.dispatch_key(key_down(PhysicalKey::f2)), "keys the engine does not use pass on");
    require(window.dispatch_key(key_down(PhysicalKey::enter)), "Enter commits");
    const std::vector<int> expected{0, 2, 1, 3, 4};
    require(client.order == expected && client.ends.back().end == DragEnd::dropped &&
                client.ends.back().mode == DragInputMode::keyboard,
            "Enter commits through the same end as a drop");
}

// The set of tokens the pointer selects (sampled densely, and at every
// anchor) equals the set the keyboard may step to.
void keyboard_reaches_what_the_pointer_reaches() {
    TestClient client;
    client.shape = PlanShape::targets;
    // A three by three grid of tiles, one group per row; the middle tile is
    // refused, so neither path may reach it.
    client.refuse_odd = false;
    for (std::uint32_t row = 0U; row < 3U; ++row) {
        for (std::uint32_t column = 0U; column < 3U; ++column) {
            const double x = 220.0 + static_cast<double>(column) * 40.0;
            const double y = 220.0 + static_cast<double>(row) * 40.0;
            client.declared.push_back(tile(x, y, row * 3U + column, static_cast<std::uint16_t>(row)));
        }
    }
    Fixture fixture = make_fixture({220.0, 220.0, 30.0, 30.0});
    Window& window = *fixture.window;
    DragEngine& engine = window.drag_engine();
    require(engine.begin_keyboard(client, *fixture.owner, *fixture.item, 0U), "a keyboard target drag starts");

    std::vector<std::uint32_t> pointer_set;
    for (double y = 200.0; y < 360.0; y += 2.0) {
        for (double x = 200.0; x < 360.0; x += 2.0) {
            const DragSelection selection = engine.probe({x, y}, Modifier::none);
            if (selection.kind != DragSelectionKind::candidate) continue;
            bool known = false;
            for (const std::uint32_t token : pointer_set) known = known || token == selection.token.value;
            if (!known) pointer_set.push_back(selection.token.value);
        }
    }
    std::vector<std::uint32_t> keyboard_set;
    const std::span<const DragLanding> landings = engine.landings();
    for (std::size_t index = 0U; index < landings.size(); ++index) {
        if (engine.keyboard_reachable(index)) keyboard_set.push_back(landings[index].token.value);
    }
    require(pointer_set.size() == 9U && keyboard_set.size() == 9U, "all nine tiles are reachable both ways");
    for (const std::uint32_t token : pointer_set) {
        bool found = false;
        for (const std::uint32_t reachable : keyboard_set) found = found || reachable == token;
        require(found, "every pointer-reachable tile is keyboard-reachable");
    }

    // Walking: Right along each row, Tab to the next row.
    std::vector<std::uint32_t> visited{engine.selection().token.value};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 2; ++column) {
            static_cast<void>(window.dispatch_key(key_down(PhysicalKey::right)));
            visited.push_back(engine.selection().token.value);
        }
        static_cast<void>(window.dispatch_key(key_down(PhysicalKey::tab)));
        visited.push_back(engine.selection().token.value);
    }
    for (std::uint32_t token = 0U; token < 9U; ++token) {
        bool seen = false;
        for (const std::uint32_t step : visited) seen = seen || step == token;
        require(seen, "arrows and Tab visit every reachable tile");
    }
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::tab, Modifier::shift)));
    require(engine.selection().group.value == 2U, "Shift+Tab steps to the previous group");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::escape)));
    require(engine.phase() == DragPhase::idle && client.ends.back().end == DragEnd::escaped, "Escape cancels");

    // Refused candidates are reachable neither way.
    client.refuse_odd = true;
    require(engine.begin_keyboard(client, *fixture.owner, *fixture.item, 0U), "the keyboard drag starts again");
    require(engine.landings().size() == 5U && engine.statistics().refused_candidates == 4U,
            "the client's refusals remove candidates at planning");
    const DragSelection refused = engine.probe(centre(client.declared[1].rect), Modifier::none);
    require(refused.kind == DragSelectionKind::none, "the pointer cannot select a refused candidate");
    engine.cancel(DragEnd::escaped);
}

// -- rate, motion, refusals ----------------------------------------------------------------------

// A path from slot 0's centre that wanders sideways and ends at y 87, sampled
// at `rate` events per second over one second.
std::uint32_t drop_after_path(int rate) {
    TestClient client;
    client.reset_list(5U, 0U);
    Fixture fixture = make_fixture(client.slots[0]);
    const Point grab = centre(client.slots[0]);
    require(press(fixture, client, grab, 4.0), "a drag arms");
    for (int sample = 1; sample <= rate; ++sample) {
        const double t = static_cast<double>(sample) / static_cast<double>(rate);
        const Point at{grab.x + 30.0 * std::sin(t * 6.0), grab.y + (87.0 - grab.y) * t};
        move_to(fixture, at);
    }
    release_at(fixture, {grab.x + 30.0 * std::sin(6.0), 87.0});
    require(client.ends.size() == 1U && client.ends.back().end == DragEnd::dropped, "the path ends in a drop");
    const std::uint32_t token = client.ends.back().selection.token.value;
    return token;
}

void event_rate_independence() {
    const std::uint32_t slow = drop_after_path(30);
    const std::uint32_t normal = drop_after_path(60);
    const std::uint32_t fast = drop_after_path(240);
    require(slow == 3U && normal == 3U && fast == 3U, "30, 60 and 240 events per second end in the same drop");
}

DragOutcome drag_with_motion(bool reduced) {
    TestClient client;
    client.settles = true;
    client.reset_list(5U, 0U);
    Fixture fixture = make_fixture(client.slots[0]);
    Window& window = *fixture.window;
    if (reduced) {
        PresentationSettings settings = window.presentation_settings();
        settings.reduced_motion = true;
        window.set_presentation_settings(settings);
    }
    DragEngine& engine = window.drag_engine();
    const Point grab = centre(client.slots[0]);
    require(press(fixture, client, grab, 0.0), "a settling drag arms");
    move_to(fixture, {grab.x, 61.0});
    release_at(fixture, {grab.x, 61.0});
    require(client.ends.size() == 1U, "the drag ended once");
    const DragOutcome outcome = client.ends.back();
    if (reduced) {
        require(engine.phase() == DragPhase::idle && !outcome.animate, "under reduced motion the drop jumps");
    } else {
        require(engine.phase() == DragPhase::settling && outcome.animate, "with motion the session settles");
        require(engine.drags(*fixture.item), "a settling session still drags its subject");
        engine.finish_settling();
        require(engine.phase() == DragPhase::idle, "the client's motion ends the settling");
    }
    return outcome;
}

void reduced_motion_equivalence() {
    const DragOutcome moving = drag_with_motion(false);
    const DragOutcome still = drag_with_motion(true);
    require(moving.end == still.end && moving.selection.token == still.selection.token &&
                moving.selection.token.value == 2U,
            "the outcome is the same with and without reduced motion");
}

void refused_plans() {
    const std::array<PlanShape, 3> shapes{PlanShape::too_many_candidates, PlanShape::too_many_operators,
                                          PlanShape::bad_child};
    for (const PlanShape shape : shapes) {
        TestClient client;
        client.shape = shape;
        Fixture fixture = make_fixture({10.0, 10.0, 20.0, 20.0});
        DragEngine& engine = (*fixture.window).drag_engine();
        const bool armed = press(fixture, client, {20.0, 20.0}, 0.0);
        require(!armed && engine.phase() == DragPhase::idle, "a refused plan starts no drag");
        require(engine.statistics().refused_plans == 1U, "the refusal is counted");
        require(client.ends.size() == 1U && client.ends.back().end == DragEnd::rejected,
                "the client is told, and restores nothing");
        require(!(*fixture.item).has_pointer_capture(), "a refused drag releases the capture");
    }
}

// -- producers and diagnostics ---------------------------------------------------------------------

std::vector<std::uint32_t> probe_column(TestClient& client) {
    Fixture fixture = make_fixture(client.slots.empty() ? Rect{10.0, 10.0, 100.0, 20.0} : client.slots[0]);
    DragEngine& engine = (*fixture.window).drag_engine();
    const Rect source = uniform_slots(12U)[4];
    require(engine.arm(client, *fixture.owner, *fixture.item, 0U, centre(source), source, 0.0), "a probe drag arms");
    std::vector<std::uint32_t> tokens;
    for (double y = -50.0; y < 320.0; y += 0.25) {
        const DragSelection selection = engine.probe({60.0, y}, Modifier::none);
        tokens.push_back(selection.token.value);
    }
    engine.cancel(DragEnd::escaped);
    return tokens;
}

void inverse_matches_scan() {
    TestClient indexed;
    indexed.reset_list(12U, 4U);
    const std::vector<std::uint32_t> by_inverse = probe_column(indexed);

    // The same landings declared one by one: no inverse, a full scan.
    TestClient scanned;
    scanned.shape = PlanShape::landings;
    scanned.reset_list(12U, 4U);
    const std::vector<Rect> slots = uniform_slots(12U);
    for (std::size_t slot = 0U; slot < slots.size(); ++slot) {
        const double start = slot <= 4U ? slots[slot].y : slots[slot].y + slots[slot].height - item_height;
        DragLanding landing{};
        landing.rect = Rect{item_x, start, item_width, item_height};
        landing.anchor = centre(landing.rect);
        landing.token.value = static_cast<std::uint32_t>(slot);
        landing.home = slot == 4U;
        scanned.declared.push_back(landing);
    }
    const std::vector<std::uint32_t> by_scan = probe_column(scanned);
    require(by_inverse == by_scan, "the list producer's O(1) inverse selects as a full scan does, ties included");
}

void drop_zone_map() {
    TestClient client;
    client.reset_list(5U, 0U);
    Fixture fixture = make_fixture(client.slots[0]);
    DragEngine& engine = (*fixture.window).drag_engine();
    require(press(fixture, client, centre(client.slots[0]), 0.0), "a map drag arms");
    std::vector<std::int32_t> cells(5U, -2);
    const bool sampled = engine.sample_drop_zones({10.0, 10.0, 100.0, 100.0}, {100.0, 20.0},
                                                  std::span<std::int32_t>(cells.data(), cells.size()));
    const std::vector<std::int32_t> expected{0, 1, 2, 3, 4};
    require(sampled && cells == expected, "the drop-zone map gives each row its slot");
    std::vector<std::int32_t> wrong(4U, -2);
    require(!engine.sample_drop_zones({10.0, 10.0, 100.0, 100.0}, {100.0, 20.0},
                                      std::span<std::int32_t>(wrong.data(), wrong.size())),
            "a map of the wrong size is refused");
    engine.cancel(DragEnd::escaped);
}

void foreign_targets() {
    TestClient client;
    client.shape = PlanShape::foreign;
    client.declared = {tile(250.0, 50.0, 40U, 3U)};
    Fixture fixture = make_fixture({10.0, 10.0, 20.0, 20.0});
    DragEngine& engine = (*fixture.window).drag_engine();
    require(press(fixture, client, {20.0, 20.0}, 0.0), "a drag with another surface's targets arms");
    require(engine.operators().size() == 1U && engine.operators()[0].foreign &&
                engine.operators()[0].kind == DragOperatorKind::target,
            "another surface's landings enter as declared targets only");
    move_to(fixture, {260.0, 60.0});
    require(engine.selection().token.value == 40U && engine.selection().group.value == 3U,
            "a foreign target is selected by containment");
    release_at(fixture, {260.0, 60.0});
    require(client.ends.back().end == DragEnd::dropped, "a foreign target takes the drop");
}

// -- rules ------------------------------------------------------------------------------------------

struct RuleJournal final {
    int dragging{};
    int candidate{};
};

void note_dragging(RuleJournal& journal, const RuleFiring&) { ++journal.dragging; }
void note_candidate(RuleJournal& journal, const RuleFiring&) { ++journal.candidate; }

void rules_conditions_and_action() {
    TestClient client;
    client.reset_list(5U, 1U);
    Fixture fixture = make_fixture(client.slots[1]);
    client.container = fixture.container.get();
    Window& window = *fixture.window;
    DragEngine& engine = window.drag_engine();
    RuleSet& rules = window.rules();
    RuleJournal journal;

    Rule while_dragging{};
    while_dragging.trigger = trigger::key_pressed(PhysicalKey::f2);
    while_dragging.conditions.push_back(condition::dragging(*fixture.container));
    while_dragging.actions.push_back(action(journal, &note_dragging));
    require(rules.add(while_dragging).refusal == RuleRefusal::none, "a dragging rule is armed");
    Rule over_candidate{};
    over_candidate.trigger = trigger::key_pressed(PhysicalKey::f2);
    over_candidate.conditions.push_back(condition::drop_candidate(*fixture.container));
    over_candidate.actions.push_back(action(journal, &note_candidate));
    require(rules.add(over_candidate).refusal == RuleRefusal::none, "a drop_candidate rule is armed");
    Rule move_key{};
    move_key.trigger = trigger::key_pressed(PhysicalKey::f3);
    move_key.actions.push_back(builtin::begin_keyboard_drag(*fixture.item));
    require(rules.add(move_key).refusal == RuleRefusal::none, "a begin_keyboard_drag rule is armed");
    require(rule_condition_kind_name(RuleConditionKind::dragging) == "dragging" &&
                rule_action_kind_name(RuleActionKind::begin_keyboard_drag) == "begin_keyboard_drag",
            "the new words have names");

    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::f2)));
    require(journal.dragging == 0 && journal.candidate == 0, "neither condition holds without a drag");
    const Point grab = centre(client.slots[1]);
    require(press(fixture, client, grab, 0.0), "a drag arms");
    move_to(fixture, {grab.x, 61.0});
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::f2)));
    require(journal.dragging == 1 && journal.candidate == 1, "both conditions hold while dragging over the container");
    release_at(fixture, {grab.x, 61.0});
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::f2)));
    require(journal.dragging == 1 && journal.candidate == 1, "after the drop neither holds");

    (*fixture.item).client = &client;
    (*fixture.item).owner = fixture.owner.get();
    client.subject = 2U;
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::f3)));
    require(engine.phase() == DragPhase::dragging && engine.mode() == DragInputMode::keyboard,
            "the begin_keyboard_drag action starts the keyboard mode");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::escape)));
    require(engine.phase() == DragPhase::idle, "Escape ends it");
}

// -- typed interactions (phase E4) ------------------------------------------------------------------

// Records each typed interaction as "<event>[ c<candidate>][ v<value>][ keyboard][ <end>];".
// With cancel_on_start, the started handler cancels the drag (reentrancy).
struct InteractionLog final : public Component {
    std::string text;
    DragEngine* cancel_on_start{};

    void on_started(const DragInteraction& interaction) {
        note("started", interaction);
        if (cancel_on_start != nullptr) (*cancel_on_start).cancel(DragEnd::escaped);
    }
    void on_target_changed(const DragInteraction& interaction) { note("target_changed", interaction); }
    void on_dropped(const DragInteraction& interaction) { note("dropped", interaction); }
    void on_ended(const DragInteraction& interaction) { note("ended", interaction); }

private:
    void note(const char* event, const DragInteraction& interaction) {
        text += event;
        if (interaction.has_candidate) text += " c" + std::to_string(interaction.candidate);
        if (interaction.has_value) text += " v" + std::to_string(std::lround(interaction.value));
        if (interaction.mode == DragInputMode::keyboard) text += " keyboard";
        if (interaction.end != DragEnd::none) {
            text += ' ';
            text += drag_end_name(interaction.end);
        }
        text += ';';
    }
};

void listen(DragInteractionEvents& events, InteractionLog& log) {
    on(events.started, log, &InteractionLog::on_started);
    on(events.target_changed, log, &InteractionLog::on_target_changed);
    on(events.dropped, log, &InteractionLog::on_dropped);
    on(events.ended, log, &InteractionLog::on_ended);
}

// The order of section 5.3: drag_started once at the planning, a
// drag_target_changed per change of candidate, and drag_dropped then
// drag_ended after the client has committed; a cancel raises drag_ended
// only, with no selection; a started handler that cancels ends the session
// and nothing further is raised; a keyboard drag says so.
void typed_interactions() {
    TestClient client;
    client.reset_list(5U, 1U);
    Fixture fixture = make_fixture(client.slots[1]);
    DragEngine& engine = (*fixture.window).drag_engine();
    DragInteractionEvents events;
    InteractionLog log;
    listen(events, log);
    client.events = &events;

    const Point grab = centre(client.slots[1]);
    require(press(fixture, client, grab, 4.0), "an observed drag arms");
    require(log.text.empty(), "arming raises nothing");
    move_to(fixture, {grab.x, 81.0});
    move_to(fixture, {grab.x, 82.0});
    move_to(fixture, {grab.x, 61.0});
    release_at(fixture, {grab.x, 61.0});
    if (log.text != "started c3;target_changed c3;target_changed c2;dropped c2 dropped;ended c2 dropped;") {
        std::cerr << log.text << '\n';
    }
    require(log.text == "started c3;target_changed c3;target_changed c2;dropped c2 dropped;ended c2 dropped;",
            "the interactions arrive in the study's order, once per change of candidate, after the commit");
    const std::vector<int> committed{0, 2, 1, 3, 4};
    require(client.order == committed, "drag_dropped follows the client's commit");

    log.text.clear();
    client.subject = 2U;
    const Point again = centre(client.slots[2]);
    require(press(fixture, client, again, 4.0), "a second drag arms");
    move_to(fixture, {again.x, 101.0});
    engine.cancel(DragEnd::escaped);
    require(log.text == "started c4;target_changed c4;ended escaped;", "a cancel raises drag_ended without a selection");

    log.text.clear();
    log.cancel_on_start = &engine;
    require(press(fixture, client, again, 4.0), "a third drag arms");
    move_to(fixture, {again.x, 101.0});
    require(log.text == "started c4;ended escaped;" && engine.phase() == DragPhase::idle,
            "a started handler that cancels ends the session; no target change follows");
    log.cancel_on_start = nullptr;

    log.text.clear();
    client.subject = 0U;
    require(engine.begin_keyboard(client, *fixture.owner, *fixture.item, 0U), "a keyboard drag starts");
    static_cast<void>(engine.key(key_down(PhysicalKey::escape)));
    require(log.text.rfind("started", 0U) == 0U && log.text.find("keyboard") != std::string::npos &&
                log.text.find("ended keyboard escaped;") != std::string::npos,
            "a keyboard drag's interactions say keyboard");
    client.events = nullptr;
}

// Depth-first search of a snapshot for a node by stable identity.
const SemanticNode* find_semantic_node(const std::vector<SemanticNode>& nodes, std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        const SemanticNode* const below = find_semantic_node(node.children, stable_id);
        if (below != nullptr) return below;
    }
    return nullptr;
}

// The announcements reach screen readers through the window's live region:
// absent until the window has an engine, then always present, named by the
// latest announcement; a changed announcement invalidates the semantics.
void announcements_live_region() {
    TestClient client;
    client.reset_list(5U, 1U);
    Fixture fixture = make_fixture(client.slots[1]);
    Window& window = *fixture.window;
    const SemanticSnapshot before = window.semantic_snapshot();
    require(find_semantic_node(before.roots, "drag.root.drag-announcements") == nullptr,
            "a window without a drag engine publishes no drag live region");

    DragEngine& engine = window.drag_engine();
    const Point grab = centre(client.slots[1]);
    require(press(fixture, client, grab, 4.0), "a drag arms");
    const std::uint64_t generation = window.semantic_generation();
    move_to(fixture, {grab.x, 81.0});
    require(window.semantic_generation() != generation, "a changed announcement invalidates the semantics");
    const SemanticSnapshot during = window.semantic_snapshot();
    const SemanticNode* const region = find_semantic_node(during.roots, "drag.root.drag-announcements");
    require(region != nullptr && (*region).role == SemanticRole::live_region && (*region).name == "Item 4 of 5" &&
                (*region).actions.empty(),
            "the live region is named by the current announcement and takes no action");
    require(semantic_announcement_due(nullptr, *region) == false && (*region).runtime_id != 0U,
            "it announces on a change of name, not on appearance");
    const std::uint64_t steady = window.semantic_generation();
    move_to(fixture, {grab.x, 82.0});
    require(window.semantic_generation() == steady, "an unchanged announcement leaves the semantics alone");
    move_to(fixture, {grab.x, 61.0});
    const SemanticSnapshot moved = window.semantic_snapshot();
    const SemanticNode* const renamed = find_semantic_node(moved.roots, "drag.root.drag-announcements");
    require(renamed != nullptr && (*renamed).name == "Item 3 of 5" && semantic_announcement_due(region, *renamed),
            "the next candidate's announcement is due to the reader");
    release_at(fixture, {grab.x, 61.0});
    require(engine.phase() == DragPhase::idle, "the drag ends");
    const SemanticSnapshot after = window.semantic_snapshot();
    require(find_semantic_node(after.roots, "drag.root.drag-announcements") != nullptr,
            "the live region stays once the window has an engine");
}

// -- the trace ---------------------------------------------------------------------------------------

void trace_text() {
    TestClient client;
    client.reset_list(5U, 1U);
    Fixture fixture = make_fixture(client.slots[1]);
    DragEngine& engine = (*fixture.window).drag_engine();
    engine.clear_trace();
    const Point grab = centre(client.slots[1]);
    require(press(fixture, client, grab, 4.0), "a traced drag arms");
    move_to(fixture, {grab.x, 61.0});
    release_at(fixture, {grab.x, 61.0});
    std::vector<DragTraceEntry> entries;
    engine.copy_trace(entries);
    std::string text;
    for (const DragTraceEntry& entry : entries) append_drag_trace_line(entry, text);
    const std::string expected =
        "arm armed pointer epoch=0 pointer=60,40 none ties=0\n"
        "plan dragging pointer epoch=1 pointer=60,61 candidate=2 landing=10,50,100,20 ties=0\n"
        "move dragging pointer epoch=1 pointer=60,61 candidate=2 landing=10,50,100,20 ties=0\n"
        "end idle pointer epoch=1 pointer=60,61 candidate=2 landing=10,50,100,20 end=dropped ties=0\n";
    if (text != expected) std::cerr << text;
    require(text == expected, "the trace reads as the golden text");
}

// -- seeded properties --------------------------------------------------------------------------------

std::uint32_t draw(std::mt19937& generator, std::uint32_t bound) {
    const std::uint32_t value = static_cast<std::uint32_t>(generator()) % bound;
    return value;
}

void seeded_properties() {
    std::mt19937 generator(20261108U);
    for (int round = 0; round < 200; ++round) {
        TestClient client;
        const std::size_t count = 2U + draw(generator, 11U);
        client.slots.clear();
        client.order.clear();
        double y = first_item_y;
        for (std::size_t index = 0U; index < count; ++index) {
            const double height = 8.0 + static_cast<double>(draw(generator, 33U));
            client.slots.push_back(Rect{item_x, y, item_width, height});
            client.order.push_back(static_cast<int>(index));
            y += height;
        }
        client.subject = draw(generator, static_cast<std::uint32_t>(count));
        Fixture fixture = make_fixture(client.slots[client.subject]);
        DragEngine& engine = (*fixture.window).drag_engine();
        const std::vector<int> before = client.order;
        const Point grab = centre(client.slots[client.subject]);
        require(press(fixture, client, grab, 0.0), "a seeded drag arms");

        // A pointer exactly at a landing's anchor (plus the grab) selects it.
        const std::span<const DragLanding> landings = engine.landings();
        for (std::size_t index = 0U; index < landings.size(); ++index) {
            const DragSelection at_anchor = engine.probe(landings[index].anchor, Modifier::none);
            require(at_anchor.kind == DragSelectionKind::candidate && at_anchor.landing == index,
                    "the pointer at a landing's anchor selects that landing");
        }
        for (int step = 0; step < 20; ++step) {
            const Point at{static_cast<double>(draw(generator, 300U)), static_cast<double>(draw(generator, 400U))};
            move_to(fixture, at);
        }
        const std::uint32_t ending = draw(generator, 3U);
        if (ending == 0U) {
            release_at(fixture, {grab.x, static_cast<double>(draw(generator, 400U))});
            require(client.ends.size() == 1U && client.ends.back().end == DragEnd::dropped &&
                        client.ends.back().selection.token.value < count,
                    "a release commits one of the declared candidates");
        } else if (ending == 1U) {
            static_cast<void>((*fixture.window).dispatch_key(key_down(PhysicalKey::escape)));
            require(client.order == before, "Escape leaves the model as it was");
        } else {
            (*fixture.item).set_pointer_capture(false);
            require(client.order == before, "a lost capture leaves the model as it was");
        }
        require(engine.phase() == DragPhase::idle && engine.statistics().storage_growths == 0U,
                "each seeded drag ends, with no storage growth");
    }
}

// -- allocation -----------------------------------------------------------------------------------------

void no_allocation_while_dragging() {
    TestClient client;
    client.reset_list(5U, 0U);
    Fixture fixture = make_fixture(client.slots[0]);
    DragEngine& engine = (*fixture.window).drag_engine();
    const Point grab = centre(client.slots[0]);
    require(press(fixture, client, grab, 0.0), "an allocation-counted drag arms");
    std::size_t attempts = 0U;
    {
        const drag_allocation_probe::CountScope scope;
        for (int step = 0; step < 100; ++step) {
            const double y = grab.y + static_cast<double>(step % 50) * 2.0;
            static_cast<void>(engine.pointer(pointer_event(PointerAction::move, {grab.x, y})));
        }
        attempts = drag_allocation_probe::attempts;
    }
    require(attempts == 0U, "pointer moves allocate nothing from the first dragging frame");
    engine.cancel(DragEnd::escaped);

    client.subject = 0U;
    require(engine.begin_keyboard(client, *fixture.owner, *fixture.item, 0U), "a keyboard drag starts");
    {
        const drag_allocation_probe::CountScope scope;
        for (int step = 0; step < 8; ++step) static_cast<void>(engine.key(key_down(PhysicalKey::down)));
        for (int step = 0; step < 8; ++step) static_cast<void>(engine.key(key_down(PhysicalKey::up)));
        static_cast<void>(engine.key(key_down(PhysicalKey::enter)));
        attempts = drag_allocation_probe::attempts;
    }
    require(attempts == 0U, "keyboard steps and the commit allocate nothing");
    require(engine.statistics().storage_growths == 0U, "the engine's storage never grew");
}

} // namespace

int main() {
    list_reorder_forward_and_back();
    cancels_leave_the_model_untouched();
    release_outside_and_suppression();
    ties_by_declaration_order();
    chaining();
    keyboard_path();
    keyboard_reaches_what_the_pointer_reaches();
    event_rate_independence();
    reduced_motion_equivalence();
    refused_plans();
    inverse_matches_scan();
    drop_zone_map();
    foreign_targets();
    rules_conditions_and_action();
    typed_interactions();
    announcements_live_region();
    trace_text();
    seeded_properties();
    no_allocation_while_dragging();
    std::cout << "Drag engine: reorder, cancels, targets, suppression, ties, chaining, keyboard path and "
                 "reachability, event-rate independence, reduced motion, refusals, inverse, drop zones, "
                 "foreign targets, rules, typed interactions, the live region, trace and seeded properties passed; no allocation while dragging\n";
    return 0;
}
