// PageStrip (stage DK, phase P2): the anatomy, geometry tables at several
// widths for content, fixed and fill sizing, overflow with scroll and list
// buttons, the selected tab kept visible, keyboard traces under automatic
// and manual activation, mnemonics, pointer and wheel, the page list, the
// semantic snapshot (tabs select and close, never press), refusals, release
// of the set, and no allocation per tab while painting.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/page_strip.hpp"
#include "gui_forms/controls/page_strip_values.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/pages/page_set/page_set.hpp"
#include "gui_forms/pages/types/page_set_types.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/typography/types/typography_types.hpp"
#include "gui_forms/window/presentation/colour_scheme.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

// Counts allocation attempts on this thread while enabled. Replacement
// allocation is confined to this executable.
namespace page_strip_allocation_probe {
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
} // namespace page_strip_allocation_probe

void* operator new(const std::size_t bytes) {
    void* const memory = page_strip_allocation_probe::allocate(bytes);
    return memory;
}
void* operator new[](const std::size_t bytes) {
    void* const memory = page_strip_allocation_probe::allocate(bytes);
    return memory;
}
void operator delete(void* const memory) noexcept { std::free(memory); }
void operator delete[](void* const memory) noexcept { std::free(memory); }
void operator delete(void* const memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* const memory, std::size_t) noexcept { std::free(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment) {
    void* const memory = page_strip_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment) {
    void* const memory = page_strip_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void operator delete(void* const memory, const std::align_val_t) noexcept {
    page_strip_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t) noexcept {
    page_strip_allocation_probe::release_aligned(memory);
}
void operator delete(void* const memory, std::size_t, const std::align_val_t) noexcept {
    page_strip_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, std::size_t, const std::align_val_t) noexcept {
    page_strip_allocation_probe::release_aligned(memory);
}
// The nothrow forms too: the standard library uses them (std::stable_sort's
// temporary buffer), and every form must come from the same allocator as its
// matching delete, or a sanitizer reports an allocation/deallocation mismatch.
void* operator new(const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = page_strip_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = page_strip_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = page_strip_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = page_strip_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    page_strip_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    page_strip_allocation_probe::release_aligned(memory);
}

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

bool near(double left, double right) {
    const bool result = std::fabs(left - right) < 0.001;
    return result;
}

// Adds pages "p0", "p1", ... titled "Page 0", "Page 1", ...
void add_pages(PageSet& pages, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        PageRecord record{};
        record.id = PageId{"p" + std::to_string(index)};
        record.title = "Page " + std::to_string(index);
        const PageEditStatus status = pages.insert(record, PagePlacement::end_of_group());
        require(status == PageEditStatus::applied, "the fixture's pages are inserted");
    }
}

void select(PageSet& pages, std::string_view id) {
    const PageEditStatus status = pages.select(PageId{std::string(id)}, PageSelectionCause::programmatic);
    require(page_edit_succeeded(status), "the fixture's selection applies");
}

std::string selected_id(const PageSet& pages) {
    if (!pages.selected_page()) return {};
    return (*pages.selected_page()).value;
}

KeyEvent key(std::uint32_t physical, Modifier modifiers = Modifier::none) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = physical;
    event.modifiers = modifiers;
    return event;
}

void click(Window& window, Point point) {
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}),
            "the strip takes the press");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
}

// Borrows from `nodes`: the pointer is valid while that vector lives. The
// deleted overload refuses a temporary (house style section 1).
const SemanticNode* find_node(const std::vector<SemanticNode>& nodes, std::string_view id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == id) return &node;
        const SemanticNode* const inner = find_node(node.children, id);
        if (inner != nullptr) return inner;
    }
    return nullptr;
}
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view id) = delete;

bool has_action(const SemanticNode& node, SemanticAction action) {
    for (const SemanticAction offered : node.actions) {
        if (offered == action) return true;
    }
    return false;
}

// Records the set's and the strip's events in order.
struct Trace final {
    std::vector<std::string> events;
};

struct RecordSelecting final {
    Trace* trace{};
    void operator()(PageSelectingEvent& event) const {
        (*trace).events.push_back("selecting " + event.new_page.value);
    }
};

struct CancelSelecting final {
    bool* active{};
    void operator()(PageSelectingEvent& event) const {
        if (*active) event.cancel = true;
    }
};

struct RecordSelected final {
    Trace* trace{};
    void operator()(const PageSelectionChange& change) const {
        (*trace).events.push_back("selected " + (change.new_page ? (*change.new_page).value : std::string("none")));
    }
};

struct RecordInvoked final {
    Trace* trace{};
    void operator()(const PageInvocation& invocation) const {
        (*trace).events.push_back("invoked " + invocation.page.value);
    }
};

struct TakeClose final {
    std::vector<std::string>* closes{};
    void operator()(PageCloseRequest& request) const {
        (*closes).push_back(request.page.value);
        request.handled = true;
    }
};

// A painter that draws nothing and allocates nothing: it counts the parts
// drawn, so painting can be measured for allocation alone.
class CountingPainter final : public Painter {
public:
    void save() override { ++saves; }
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override { ++fills; }
    void fill_rounded_rect(Rect, double, Color) override { ++fills; }
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void draw_line(Point, Point, Color, double) override { ++lines; }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { ++texts; }
    void draw_image(ImageId, Rect, double) override {}

    std::size_t saves{};
    std::size_t fills{};
    std::size_t lines{};
    std::size_t texts{};
};

// -- tests ---------------------------------------------------------------------

void anatomy_and_parts() {
    PageSet pages{};
    add_pages(pages, 3U);
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    Window window(strip, {640.0, 26.0});
    window.perform_layout();
    const ControlAnatomy& anatomy = (*strip).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none && anatomy.control_id == "page_strip",
            "PageStrip publishes a valid anatomy");
    require(anatomy.item_systems.size() == 1U && anatomy.item_systems.front().name == "page" &&
                anatomy.item_systems.front().custom_states.size() == 2U &&
                anatomy.item_systems.front().custom_states[0].name == "pinned" &&
                anatomy.item_systems.front().custom_states[1].name == "modified",
            "PageStrip's tabs are the page item system with the admitted pinned and modified states");
    const PageStrip::Parts parts = (*strip).parts();
    require(parts.region.present && parts.rail.present && !parts.scroll_back.present &&
                !parts.list_button.present,
            "a strip whose tabs fit shows no scroll or list buttons");
    const std::vector<PageStrip::Item> items = (*strip).realized_items();
    require(items.size() == 3U && items[0].page.value == "p0" && items[0].states.selected &&
                items[0].parts.tab_seam.present && !items[1].parts.tab_seam.present,
            "every tab is realized and only the selected one shows its seam");
    require(items[0].stable_id == "strip.tab.p0", "a tab's identity is the strip's and the page's");
    require(items[0].parts.tab_label.present && items[0].parts.tab_focus.present && !items[0].parts.tab_icon.present,
            "a tab without an image shows a label and a focus part, no icon");
    const Rect seam = items[0].parts.tab_seam.bounds;
    const Rect tab = items[0].parts.tab.bounds;
    require(near(seam.y + seam.height, tab.y + tab.height), "a top strip's seam lies on the tab's lower edge");
    (*strip).placement = PageStripPlacement::bottom;
    const PageStrip::Item moved = (*strip).realized_items()[0];
    require(near(moved.parts.tab_seam.bounds.y, moved.parts.tab.bounds.y) && near(moved.parts.tab.bounds.y, 0.0),
            "a bottom strip's tabs touch its top edge, the seam with them");
}

// One row of a geometry table: the strip's width and what must show there.
struct GeometryRow final {
    double width{};
    bool overflow{};
    std::size_t visible{};
};

void fixed_geometry_table() {
    PageSet pages{};
    add_pages(pages, 6U);
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    (*strip).item_sizing = PageItemSizing::fixed;
    PageItemWidths widths{};
    widths.fixed = 100.0;
    (*strip).item_widths = widths;
    Window window(strip, {640.0, 26.0});
    // Six tabs of 100 after a 2-unit inset at each end; overflow adds three
    // 16-unit buttons, so `width - 4 - 48` is the tab area.
    const std::vector<GeometryRow> table{
        {640.0, false, 6U}, {604.0, false, 6U}, {603.0, true, 5U}, {400.0, true, 3U},
        {300.0, true, 2U},  {250.0, true, 1U},  {60.0, true, 1U},
    };
    for (const GeometryRow& row : table) {
        window.resize({row.width, 26.0});
        window.perform_layout();
        const std::vector<PageStrip::Item> items = (*strip).realized_items();
        const PageStrip::Parts parts = (*strip).parts();
        require((*strip).overflowing() == row.overflow, "the fixed table's overflow matches");
        require(items.size() == row.visible, "the fixed table's visible count matches");
        require(parts.scroll_back.present == row.overflow && parts.scroll_forward.present == row.overflow &&
                    parts.list_button.present == row.overflow,
                "the scroll and list buttons show exactly while overflowing");
        require(near(items[0].parts.tab.bounds.x, 2.0), "the first tab starts after the inset");
        const double area_end = row.overflow ? parts.scroll_back.bounds.x : row.width - 2.0;
        for (std::size_t index = 0; index < items.size(); ++index) {
            const Rect bounds = items[index].parts.tab.bounds;
            require(bounds.x + bounds.width <= area_end + 0.001, "tabs stay before the buttons");
            if (index + 1U < items.size()) {
                require(near(bounds.width, 100.0), "a whole tab keeps its fixed width");
                require(near(bounds.x + bounds.width, items[index + 1U].parts.tab.bounds.x), "tabs are contiguous");
            }
        }
        if (row.overflow) {
            require(near(parts.list_button.bounds.x + parts.list_button.bounds.width, row.width - 2.0) &&
                        near(parts.scroll_forward.bounds.x + 16.0, parts.list_button.bounds.x) &&
                        near(parts.scroll_back.bounds.x + 16.0, parts.scroll_forward.bounds.x),
                    "the buttons sit at the strip's end: back, forward, list");
        }
    }
    window.resize({60.0, 26.0});
    window.perform_layout();
    require(near((*strip).realized_items()[0].parts.tab.bounds.width, 8.0),
            "a tab wider than the area still shows, cut to the area");
}

void content_and_fill_geometry() {
    PageSet pages{};
    const std::vector<std::string> titles{"A", "Settings", "A much longer title than any maximum allows at all",
                                          "Two words"};
    for (std::size_t index = 0; index < titles.size(); ++index) {
        PageRecord record{};
        record.id = PageId{"c" + std::to_string(index)};
        record.title = titles[index];
        require(pages.insert(record, PagePlacement::end_of_group()) == PageEditStatus::applied, "inserted");
    }
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    Window window(strip, {1600.0, 26.0});
    window.perform_layout();
    const PageItemWidths widths = (*strip).item_widths.read();
    const FontSpec font = (*strip).effective_font((*strip).font.read());
    std::vector<PageStrip::Item> items = (*strip).realized_items();
    require(items.size() == 4U && !(*strip).overflowing(), "a wide strip shows every content-sized tab");
    for (std::size_t index = 0; index < items.size(); ++index) {
        const double text = (*strip).resolve_text_layout_utf8(titles[index], font).logical_size.width;
        const double expected = std::clamp(text + 16.0, widths.minimum, widths.maximum);
        require(near(items[index].parts.tab.bounds.width, expected),
                "a content-sized tab is its label plus padding, within the minimum and maximum");
    }
    require(near(items[0].parts.tab.bounds.width, widths.minimum), "a one-letter tab takes the minimum width");
    require(near(items[2].parts.tab.bounds.width, widths.maximum), "a long title is held to the maximum width");

    (*strip).item_sizing = PageItemSizing::fill;
    // A 400-unit strip shares 396 units: 99 each, within 40 and 240.
    const std::vector<double> fill_widths{400.0, 900.0, 1600.0};
    for (const double width : fill_widths) {
        window.resize({width, 26.0});
        window.perform_layout();
        items = (*strip).realized_items();
        const double share = std::clamp((width - 4.0) / 4.0, widths.minimum, widths.maximum);
        require(items.size() == 4U, "fill shows every tab while the share is above the minimum");
        for (const PageStrip::Item& item : items) {
            require(near(item.parts.tab.bounds.width, share), "fill gives every tab the same share");
        }
    }
    window.resize({120.0, 26.0});
    window.perform_layout();
    require((*strip).overflowing(), "fill below the minimum overflows");
    bool refused = false;
    try {
        PageItemWidths inverted{};
        inverted.minimum = 300.0;
        inverted.maximum = 100.0;
        (*strip).item_widths = inverted;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && near((*strip).item_widths.read().minimum, widths.minimum),
            "a minimum above the maximum is refused and the old widths stay");
}

void selected_tab_always_visible() {
    PageSet pages{};
    add_pages(pages, 40U);
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    (*strip).item_sizing = PageItemSizing::fixed;
    PageItemWidths widths{};
    widths.fixed = 60.0;
    (*strip).item_widths = widths;
    Window window(strip, {300.0, 26.0});
    const std::vector<std::string_view> chosen{"p30", "p0", "p39", "p17"};
    for (const std::string_view id : chosen) {
        select(pages, id);
        for (double width = 70.0; width <= 2600.0; width += 137.0) {
            window.resize({width, 26.0});
            window.perform_layout();
            const std::vector<PageId> visible = (*strip).visible_pages();
            bool shown = false;
            for (const PageId& page : visible) shown = shown || page.value == id;
            require(shown, "the selected tab is visible at every width");
        }
    }
    window.resize({300.0, 26.0});
    window.perform_layout();
    select(pages, "p20");
    require((*strip).scroll_forward() && (*strip).scroll_forward() && (*strip).scroll_forward() &&
                (*strip).scroll_forward() && (*strip).scroll_forward(),
            "the scroll buttons move the row");
    std::vector<PageId> visible = (*strip).visible_pages();
    bool shown = false;
    for (const PageId& page : visible) shown = shown || page.value == "p20";
    require(!shown, "scrolling may move the selected tab out of view");
    window.resize({310.0, 26.0});
    window.perform_layout();
    visible = (*strip).visible_pages();
    for (const PageId& page : visible) shown = shown || page.value == "p20";
    require(shown, "the next resize brings the selected tab back");
    require((*strip).ensure_visible(PageId{"p2"}) && (*strip).visible_pages().front().value == "p2",
            "ensure_visible scrolls a page before the row to its start");
    require((*strip).ensure_visible(PageId{"p39"}) && (*strip).visible_pages().back().value == "p39" &&
                !(*strip).scroll_forward(),
            "ensure_visible scrolls the last page to the row's end, and no further");
    require(!(*strip).ensure_visible(PageId{"missing"}), "ensure_visible refuses a page not in the set");
    // The wheel scrolls: turned up towards the start.
    const Rect area = (*strip).realized_items().front().parts.tab.bounds;
    const std::string before = (*strip).visible_pages().front().value;
    PointerEvent wheel{};
    wheel.action = PointerAction::wheel;
    wheel.position = centre(area);
    wheel.wheel_delta = Point{0.0, 120.0};
    require(window.dispatch_pointer(wheel) && (*strip).visible_pages().front().value != before,
            "the wheel scrolls an overflowing strip");
}

void keyboard_traces() {
    PageSet pages{};
    add_pages(pages, 4U);
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    Window window(strip, {640.0, 26.0});
    window.perform_layout();
    Trace trace{};
    SubscriptionToken selecting = pages.selecting().subscribe(RecordSelecting{&trace});
    SubscriptionToken selected = pages.selected_page_changed().subscribe(RecordSelected{&trace});
    SubscriptionToken invoked = (*strip).page_invoked().subscribe(RecordInvoked{&trace});
    require(window.request_focus(strip) && (*strip).focused_page() && (*(*strip).focused_page()).value == "p0",
            "taking focus puts the keyboard on the selected tab");
    require(window.dispatch_key(key(PhysicalKey::right)), "Right is handled");
    const std::vector<std::string> expected{"selecting p1", "selected p1", "invoked p1"};
    require(trace.events == expected, "Right selects the next tab: selecting, selected, invoked");
    require(window.dispatch_key(key(PhysicalKey::end)) && selected_id(pages) == "p3", "End selects the last tab");
    require(window.dispatch_key(key(PhysicalKey::right)) && selected_id(pages) == "p0", "Right wraps to the first");
    require(window.dispatch_key(key(PhysicalKey::left)) && selected_id(pages) == "p3", "Left wraps to the last");
    require(window.dispatch_key(key(PhysicalKey::home)) && selected_id(pages) == "p0", "Home selects the first");
    require(window.dispatch_key(key(PhysicalKey::page_down, Modifier::control)) && selected_id(pages) == "p1",
            "Ctrl+PageDown selects the next page");
    require(window.dispatch_key(key(PhysicalKey::page_up, Modifier::control)) &&
                window.dispatch_key(key(PhysicalKey::page_up, Modifier::control)) && selected_id(pages) == "p3",
            "Ctrl+PageUp selects the previous page, wrapping");
    require(!window.dispatch_key(key(PhysicalKey::page_down)), "PageDown alone is left to others");

    // A cancelled selection keeps everything: the selection, the keyboard's
    // tab, and no invocation.
    bool cancelling = true;
    SubscriptionToken cancel = pages.selecting().subscribe(CancelSelecting{&cancelling});
    trace.events.clear();
    require(window.dispatch_key(key(PhysicalKey::left)), "Left is handled even when cancelled");
    const std::vector<std::string> cancelled{"selecting p2"};
    require(trace.events == cancelled && selected_id(pages) == "p3" && (*(*strip).focused_page()).value == "p3",
            "a cancelled selection keeps the selection and the keyboard's tab and invokes nothing");
    cancelling = false;

    // Manual activation: arrows move the keyboard only; Space selects.
    (*strip).activation = PageStripActivation::manual;
    trace.events.clear();
    require(window.dispatch_key(key(PhysicalKey::left)) && window.dispatch_key(key(PhysicalKey::left)),
            "Left is handled under manual activation");
    require(trace.events.empty() && selected_id(pages) == "p3" && (*(*strip).focused_page()).value == "p1",
            "manual activation moves the keyboard without selecting");
    const std::vector<PageStrip::Item> items = (*strip).realized_items();
    require(items[1].states.focus_visible && !items[1].states.selected && items[3].states.selected,
            "the keyboard's tab shows the focus cue apart from the selected tab");
    require(window.dispatch_key(key(PhysicalKey::space)) && selected_id(pages) == "p1",
            "Space selects the keyboard's tab");
    const std::vector<std::string> manual{"selecting p1", "selected p1", "invoked p1"};
    require(trace.events == manual, "a manual selection raises the same events");
    trace.events.clear();
    require(window.dispatch_key(key(PhysicalKey::enter)) && trace.events.size() == 1U &&
                trace.events[0] == "invoked p1",
            "Enter on the selected tab invokes it without selecting again");
}

void mnemonics() {
    PageSet pages{};
    const std::vector<std::string> titles{"&General", "&Advanced", "Ab&out"};
    for (std::size_t index = 0; index < titles.size(); ++index) {
        PageRecord record{};
        record.id = PageId{"m" + std::to_string(index)};
        record.title = titles[index];
        require(pages.insert(record, PagePlacement::end_of_group()) == PageEditStatus::applied, "inserted");
    }
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    (*strip).set_requested_bounds({0.0, 0.0, 600.0, 26.0});
    (*root).add_child(strip);
    std::shared_ptr<Button> other = make_control<Button>(StableId("other"), "Other");
    (*other).set_requested_bounds({0.0, 40.0, 80.0, 26.0});
    (*root).add_child(other);
    Window window(root, {600.0, 80.0});
    window.perform_layout();
    require(window.request_focus(other), "another control has focus");
    require(window.dispatch_key(key(PhysicalKey::a, Modifier::alt)) && selected_id(pages) == "m1" &&
                window.focused_control() == strip,
            "Alt+A selects the page whose mnemonic is A and focuses the strip");
    require(window.dispatch_key(key(PhysicalKey::o, Modifier::alt)) && selected_id(pages) == "m2",
            "a mnemonic inside the title selects too");
    require((*strip).realized_items()[0].page.value == "m0", "the realized tabs follow the set");
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* const general = find_node(snapshot.roots, "strip.tab.m0");
    require(general != nullptr && (*general).name == "General", "a tab's name drops the mnemonic marker");
    (*strip).mnemonics_enabled = false;
    require(window.request_focus(other), "focus moves away");
    static_cast<void>(window.dispatch_key(key(PhysicalKey::g, Modifier::alt)));
    require(selected_id(pages) == "m2", "with mnemonics off a marker selects nothing");
    const SemanticSnapshot literal = window.semantic_snapshot();
    const SemanticNode* const shown = find_node(literal.roots, "strip.tab.m0");
    require(shown != nullptr && (*shown).name == "&General", "with mnemonics off a title shows as written");
}

void pointer_and_list() {
    PageSet pages{};
    add_pages(pages, 12U);
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    (*strip).item_sizing = PageItemSizing::fixed;
    // Twelve tabs of 120: a 500-unit strip shows three before its buttons.
    Window window(strip, {500.0, 26.0});
    window.perform_layout();
    Trace trace{};
    SubscriptionToken selecting = pages.selecting().subscribe(RecordSelecting{&trace});
    SubscriptionToken selected = pages.selected_page_changed().subscribe(RecordSelected{&trace});
    SubscriptionToken invoked = (*strip).page_invoked().subscribe(RecordInvoked{&trace});
    std::vector<PageStrip::Item> items = (*strip).realized_items();
    static_cast<void>(
        window.dispatch_pointer({PointerAction::move, PointerButton::none, centre(items[1].parts.tab.bounds)}));
    require((*strip).realized_items()[1].states.hot && !(*strip).realized_items()[0].states.hot,
            "hovering a tab makes it hot");
    click(window, centre(items[1].parts.tab.bounds));
    const std::vector<std::string> expected{"selecting p1", "selected p1", "invoked p1"};
    require(trace.events == expected && window.focused_control() == strip,
            "a click selects, invokes, then focuses the strip");
    PointerEvent press{PointerAction::down, PointerButton::primary, centre(items[2].parts.tab.bounds)};
    require(window.dispatch_pointer(press) && (*strip).realized_items()[2].states.pressed,
            "a pressed tab shows pressed");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, press.position}));
    require(!(*strip).realized_items()[2].states.pressed, "the release clears pressed");

    PageStrip::Parts parts = (*strip).parts();
    require(parts.scroll_back.states.contains(AnatomyState::disabled) &&
                !parts.scroll_forward.states.contains(AnatomyState::disabled),
            "at the start, scroll back is disabled and scroll forward is not");
    click(window, centre(parts.scroll_forward.bounds));
    require((*strip).visible_pages().front().value == "p1", "scroll forward moves the row by one tab");
    click(window, centre(parts.scroll_back.bounds));
    require((*strip).visible_pages().front().value == "p0", "scroll back moves it back");

    click(window, centre(parts.list_button.bounds));
    require((*strip).page_list_open() && (*strip).parts().list_button.states.contains(AnatomyState::open),
            "the list button opens the page list");
    (*strip).close_page_list();
    require(!(*strip).page_list_open(), "the list closes");
    require((*strip).show_page_list(MenuOpenMode::keyboard), "the list opens from the keyboard");
    // The keyboard-opened list starts on its first row; Down moves.
    for (int step = 0; step < 10; ++step) {
        require(window.dispatch_key(key(PhysicalKey::down)), "Down moves in the list");
    }
    trace.events.clear();
    require(window.dispatch_key(key(PhysicalKey::enter)), "Enter chooses a row");
    require(!(*strip).page_list_open() && selected_id(pages) == "p10" && trace.events.back() == "invoked p10",
            "a page chosen from the list is selected and invoked");
    bool shown = false;
    for (const PageId& page : (*strip).visible_pages()) shown = shown || page.value == "p10";
    require(shown, "a page chosen from the list is scrolled into view");
    window.resize({1500.0, 26.0});
    window.perform_layout();
    require(!(*strip).show_page_list(MenuOpenMode::keyboard), "the list does not open while every tab fits");
}

void semantics() {
    PageSet pages{};
    add_pages(pages, 10U);
    require(pages.set_closable(PageId{"p1"}, true) == PageEditStatus::applied, "p1 is closable");
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    (*strip).item_sizing = PageItemSizing::fixed;
    (*strip).accessible_name = "Sections";
    Window window(strip, {400.0, 26.0});
    window.perform_layout();
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* const list = find_node(snapshot.roots, "strip");
    require(list != nullptr && (*list).role == SemanticRole::tab_group && (*list).name == "Sections" &&
                (*list).orientation && *(*list).orientation == SemanticOrientation::horizontal &&
                (*list).value == "Page 0",
            "the strip is a horizontal tab list named by the application, valued by the selected tab");
    std::size_t tabs = 0U;
    for (const SemanticNode& child : (*list).children) {
        if (child.role != SemanticRole::tab) continue;
        ++tabs;
        require(!has_action(child, SemanticAction::press) && has_action(child, SemanticAction::select),
                "tabs offer select and never press");
        require(child.set_size && *child.set_size == 10U && child.position_in_set, "tabs carry their place in the set");
        require(child.controlled_panel.empty(), "a strip alone links no panel");
    }
    require(tabs == 10U, "every page is a tab, laid out or not");
    const SemanticNode* const first = find_node(snapshot.roots, "strip.tab.p0");
    const SemanticNode* const closable = find_node(snapshot.roots, "strip.tab.p1");
    const SemanticNode* const hidden = find_node(snapshot.roots, "strip.tab.p9");
    require(first != nullptr && has_semantic_state((*first).states, SemanticState::selected) &&
                *(*first).position_in_set == 1U,
            "the selected tab is selected and first in the set");
    require(closable != nullptr && has_action(*closable, SemanticAction::close) &&
                !has_action(*first, SemanticAction::close),
            "only a closable tab offers close");
    require(hidden != nullptr && !has_semantic_state((*hidden).states, SemanticState::visible),
            "a tab scrolled out of view is not visible");
    const SemanticNode* const list_button = find_node(snapshot.roots, "strip.list-button");
    const SemanticNode* const back = find_node(snapshot.roots, "strip.scroll-back");
    require(list_button != nullptr && (*list_button).role == SemanticRole::menu_button && (*list_button).has_popup &&
                back != nullptr && (*back).role == SemanticRole::button,
            "an overflowing strip publishes its scroll buttons and its list button");
    require(window.perform_semantic_action("strip.tab.p9", SemanticAction::select) && selected_id(pages) == "p9",
            "the select action selects a tab out of view");
    bool shown = false;
    for (const PageId& page : (*strip).visible_pages()) shown = shown || page.value == "p9";
    require(shown, "a tab selected through accessibility is scrolled into view");
    std::vector<std::string> closes;
    SubscriptionToken closing = (*strip).page_close_requested().subscribe(TakeClose{&closes});
    require(window.perform_semantic_action("strip.tab.p1", SemanticAction::close) && closes.size() == 1U &&
                closes[0] == "p1" && pages.find(PageId{"p1"}),
            "close asks the owner and the strip removes nothing itself");
    require(!window.perform_semantic_action("strip.tab.p0", SemanticAction::close),
            "close is refused on a tab that is not closable");
}

void release_and_refusals() {
    std::shared_ptr<PageStrip> strip{};
    {
        PageSet pages{};
        add_pages(pages, 3U);
        strip = make_control<PageStrip>(StableId("strip"), pages);
        Window window(strip, {400.0, 26.0});
        window.perform_layout();
        require((*strip).realized_item_count() == 3U, "the bound strip shows its tabs");
        (*strip).release_page_set();
        window.perform_layout();
        require((*strip).page_set() == nullptr && (*strip).realized_item_count() == 0U &&
                    !(*strip).select_relative(1, PageSelectionCause::programmatic) &&
                    !(*strip).ensure_visible(PageId{"p0"}),
                "a released strip shows no tabs and its page operations do nothing");
    }
    require((*strip).semantic_virtual_children().empty(), "the strip outlives its released set safely");
    PageSet pages{};
    std::shared_ptr<PageStrip> bound = make_control<PageStrip>(StableId("bound"), pages);
    bool refused = false;
    try {
        (*bound).placement = static_cast<PageStripPlacement>(9);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*bound).placement.read() == PageStripPlacement::top, "an invalid placement is refused");
    require(!(*bound).overflowing() && (*bound).realized_items().empty() && !(*bound).select_relative(1,
                PageSelectionCause::programmatic),
            "an empty set shows nothing and selects nothing");
}

// With `dark`, the window shows the default theme's dark variant, whose
// materials are assembled for each paint.
std::size_t paint_allocations(std::size_t page_count, bool dark) {
    PageSet pages{};
    add_pages(pages, page_count);
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    (*strip).item_sizing = PageItemSizing::fixed;
    PageItemWidths widths{};
    widths.fixed = 20.0;
    (*strip).item_widths = widths;
    Window window(strip, {2000.0, 26.0});
    if (dark) {
        PresentationSettings settings = window.presentation_settings();
        settings.colour_scheme = ColourScheme::dark;
        window.set_presentation_settings(settings);
    }
    window.perform_layout();
    select(pages, "p1");
    CountingPainter painter{};
    const Rect surface = (*strip).client_rectangle();
    // The first paint after a change lays out (and may allocate); the
    // measured one paints from the cached layout.
    (*strip).on_paint(painter, surface);
    std::size_t attempts = 0U;
    {
        page_strip_allocation_probe::CountScope probe{};
        (*strip).on_paint(painter, surface);
        attempts = page_strip_allocation_probe::attempts;
    }
    require(painter.texts >= page_count * 2U, "every tab's label was painted");
    return attempts;
}

void no_allocation_per_tab_while_painting() {
    std::size_t probe_check = 0U;
    {
        page_strip_allocation_probe::CountScope probe{};
        const std::unique_ptr<std::string> allocated = std::make_unique<std::string>(64U, 'x');
        probe_check = page_strip_allocation_probe::attempts;
    }
    require(probe_check >= 1U, "the allocation probe counts this executable's allocations");
    const std::size_t few = paint_allocations(4U, false);
    const std::size_t many = paint_allocations(40U, false);
    const std::size_t many_dark = paint_allocations(40U, true);
    std::cout << "PageStrip paint allocations: 4 tabs " << few << ", 40 tabs " << many << ", 40 tabs dark "
              << many_dark << '\n';
    require(few == many, "painting allocates nothing per tab");
    require(many == 0U, "painting from the cached layout allocates nothing");
    require(many_dark == 0U, "painting the dark variant from the cached layout allocates nothing");
}

// Counts the tab labels a strip measures ("Page n").
class CountingLabelMetrics final : public TextMetricsProvider {
public:
    [[nodiscard]] ResolvedTextLayout resolve_text_layout_utf8(std::string_view text, FontSpec font) override {
        if (text.starts_with("Page ")) ++labels;
        ResolvedTextLayout result = estimate_text_layout_utf8(text, font);
        return result;
    }

    std::size_t labels{};
};

// Audit N112: a resize lays the tabs out again from their kept natural
// widths; new pages measure again.
void measured_widths_are_kept() {
    CountingLabelMetrics metrics{};
    PageSet pages{};
    add_pages(pages, 12U);
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("kept"), pages);
    Window window(strip, {900.0, 26.0});
    window.set_text_metrics_provider(&metrics);
    window.perform_layout();
    static_cast<void>((*strip).realized_items());
    const std::size_t first = metrics.labels;
    require(first >= 12U, "the first layout measures every label");
    window.resize({300.0, 26.0});
    window.perform_layout();
    static_cast<void>((*strip).realized_items());
    window.resize({700.0, 26.0});
    window.perform_layout();
    const std::vector<PageStrip::Item> items = (*strip).realized_items();
    require(metrics.labels == first && !items.empty(), "a resize measures no label again");
    PageRecord added{};
    added.id = PageId{"p-new"};
    added.title = "Page new";
    require(pages.insert(added, PagePlacement::end_of_group()) == PageEditStatus::applied, "a page is added");
    window.perform_layout();
    static_cast<void>((*strip).realized_items());
    require(metrics.labels > first, "a new page measures again");
}

} // namespace

int main() {
    anatomy_and_parts();
    fixed_geometry_table();
    content_and_fill_geometry();
    selected_tab_always_visible();
    keyboard_traces();
    mnemonics();
    pointer_and_list();
    semantics();
    release_and_refusals();
    no_allocation_per_tab_while_painting();
    measured_widths_are_kept();
    std::cout << "PageStrip: anatomy, geometry tables, overflow, the selected tab kept visible, keys, mnemonics, "
                 "pointer, list, semantics, release and paint allocation passed\n";
    return 0;
}
