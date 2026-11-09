// TabControl on PageStrip (stage DK, phase P8, owner decision DK-14): tabs
// sized by their captions, fixed only under TabSizeMode::fixed, shared under
// fill_to_right; overflow into the strip's scroll and list buttons with the
// selected tab kept in view; the strip as an internal child that takes no
// focus and publishes no semantics; the keys (a focused TabControl keeps
// Ctrl+Tab, the most local TabControl wins, commands it does not offer pass
// to the controls around it, rebinding and a host's map); appearance; and no
// allocation per tab while painting. The Windows Forms surface's own tests
// (tab_control_tests.cpp) are unchanged.

#include "gui_forms/gui_forms.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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
namespace tab_control_allocation_probe {
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
} // namespace tab_control_allocation_probe

void* operator new(const std::size_t bytes) {
    void* const memory = tab_control_allocation_probe::allocate(bytes);
    return memory;
}
void* operator new[](const std::size_t bytes) {
    void* const memory = tab_control_allocation_probe::allocate(bytes);
    return memory;
}
void operator delete(void* const memory) noexcept { std::free(memory); }
void operator delete[](void* const memory) noexcept { std::free(memory); }
void operator delete(void* const memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* const memory, std::size_t) noexcept { std::free(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment) {
    void* const memory = tab_control_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment) {
    void* const memory = tab_control_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void operator delete(void* const memory, const std::align_val_t) noexcept {
    tab_control_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t) noexcept {
    tab_control_allocation_probe::release_aligned(memory);
}
void operator delete(void* const memory, std::size_t, const std::align_val_t) noexcept {
    tab_control_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, std::size_t, const std::align_val_t) noexcept {
    tab_control_allocation_probe::release_aligned(memory);
}
// The nothrow forms too: the standard library uses them (std::stable_sort's
// temporary buffer), and every form must come from the same allocator as its
// matching delete, or a sanitizer reports an allocation/deallocation mismatch.
void* operator new(const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = tab_control_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = tab_control_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = tab_control_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = tab_control_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    tab_control_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    tab_control_allocation_probe::release_aligned(memory);
}

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_tab_control_strip_tests: " << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    const bool result = std::fabs(left - right) < 0.001;
    return result;
}

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

void click(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
}

struct TakeClose final {
    std::size_t* closes{};
    void operator()(PageCloseRequest& request) const {
        ++(*closes);
        request.handled = true;
    }
};

KeyEvent key(std::uint32_t physical, Modifier modifiers = Modifier::none) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = physical;
    event.modifiers = modifiers;
    return event;
}

// A tab control named `id` with pages `<id>.<index>` captioned by `captions`.
std::shared_ptr<TabControl> tab_control(std::string_view id, const std::vector<std::string>& captions,
                                        Rect bounds) {
    std::shared_ptr<TabControl> tabs = make_control<TabControl>(StableId(std::string(id)));
    (*tabs).set_requested_bounds(bounds);
    for (std::size_t index = 0; index < captions.size(); ++index) {
        const std::string page_id = std::string(id) + "." + std::to_string(index);
        (*tabs).add_page(make_control<TabPage>(StableId(page_id), captions[index]));
    }
    return tabs;
}

std::vector<std::string> numbered(std::size_t count) {
    std::vector<std::string> captions;
    for (std::size_t index = 0; index < count; ++index) captions.push_back("Page " + std::to_string(index));
    return captions;
}

// Counts the parts drawn; draws and allocates nothing.
class CountingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override { ++fills; }
    void fill_rounded_rect(Rect, double, Color) override { ++fills; }
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { ++texts; }
    void draw_image(ImageId, Rect, double) override {}

    std::size_t fills{};
    std::size_t texts{};
};

void sizing_modes() {
    const std::vector<std::string> captions{"A", "A much longer caption", "Mid caption"};
    std::shared_ptr<TabControl> tabs = tab_control("tabs", captions, {0.0, 0.0, 600.0, 200.0});
    Window window(tabs, {600.0, 200.0});
    window.perform_layout();
    const Rect short_tab = (*tabs).tab_bounds(0U);
    const Rect long_tab = (*tabs).tab_bounds(1U);
    require(!short_tab.empty() && long_tab.width > short_tab.width && near(short_tab.y + short_tab.height, 30.0),
            "under the normal size mode a tab is as wide as its caption and reaches the row's content edge");
    require((*tabs).display_bounds() == Rect{0.0, 29.0, 600.0, 171.0},
            "the display keeps its place under the tabs");
    (*tabs).size_mode = TabSizeMode::fixed;
    window.perform_layout();
    require(near((*tabs).tab_bounds(0U).width, 120.0) && near((*tabs).tab_bounds(1U).width, 120.0) &&
                (*tabs).tab_bounds(1U).x >= (*tabs).tab_bounds(0U).right(),
            "under the fixed size mode every tab is item_size wide, side by side");
    (*tabs).item_size = Size{90.0, 26.0};
    window.perform_layout();
    require(near((*tabs).tab_bounds(2U).width, 90.0) &&
                near((*tabs).tab_bounds(2U).y + (*tabs).tab_bounds(2U).height, 26.0) &&
                (*tabs).display_bounds() == Rect{0.0, 25.0, 600.0, 175.0},
            "the fixed tab follows item_size, and so does the row");
    (*tabs).size_mode = TabSizeMode::fill_to_right;
    window.perform_layout();
    const double first = (*tabs).tab_bounds(0U).width;
    require(near(first, (*tabs).tab_bounds(1U).width) && near(first, (*tabs).tab_bounds(2U).width) &&
                first > 90.0,
            "under fill_to_right the tabs share the row equally");
    bool refused = false;
    try {
        (*tabs).item_size = Size{2000.0, 30.0};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*tabs).item_size.read() == Size{90.0, 26.0}, "an item size beyond 1024 is refused");

    (*tabs).size_mode = TabSizeMode::normal;
    (*tabs).alignment = TabAlignment::left;
    window.perform_layout();
    (*tabs).selected_index = 1U;
    window.perform_layout();
    const Rect side = (*tabs).tab_bounds(1U);
    require(near(side.x + side.width, 90.0) && side.height > side.width,
            "on the left the tabs stack down a column item_size wide, as long as their turned captions");
    const PageStrip* const strip = dynamic_cast<const PageStrip*>(window.find("tabs.strip").get());
    require(strip != nullptr && (*strip).placement.read() == PageStripPlacement::left,
            "the strip sits on the control's alignment edge");
}

void overflow_keeps_the_selection_in_view() {
    std::shared_ptr<TabControl> tabs = tab_control("tabs", numbered(30U), {0.0, 0.0, 300.0, 160.0});
    Window window(tabs, {300.0, 160.0});
    window.perform_layout();
    require((*tabs).overflowing() && (*tabs).tab_bounds(29U).empty() && !(*tabs).tab_bounds(0U).empty(),
            "thirty tabs in 300 units overflow; the last is out of view");
    (*tabs).selected_index = 29U;
    window.perform_layout();
    require(!(*tabs).tab_bounds(29U).empty() && (*tabs).tab_bounds(29U).right() <= 300.0,
            "selecting the last page brings its tab into view");
    const std::vector<SemanticNode> nodes = (*tabs).semantic_virtual_children();
    require(nodes.size() == 30U && !has_semantic_state(nodes[0].states, SemanticState::visible) &&
                has_semantic_state(nodes[29].states, SemanticState::visible),
            "every tab is published; one out of view is not visible");
    const std::vector<TabControl::Item> items = (*tabs).realized_items();
    require(items.size() == 30U && !items[0].parts.tab.present && items[29].parts.tab.present &&
                items[29].states.selected && items[29].parts.tab_seam.present,
            "every page is an item; a tab out of view publishes no parts");
}

void strip_is_internal() {
    std::shared_ptr<TabControl> tabs = tab_control("tabs", {"One", "Two", "Three"}, {0.0, 0.0, 400.0, 200.0});
    Window window(tabs, {400.0, 200.0});
    window.perform_layout();
    const Control::Ptr strip = window.find("tabs.strip");
    require(strip && !(*strip).focusable.read(), "the strip is a child that takes no focus");
    const std::string json = window.semantic_snapshot().to_json();
    require(json.find("tabs.strip") == std::string::npos && json.find("tabs.2.tab") != std::string::npos,
            "the strip publishes nothing; the control publishes the tabs");
    click(window, centre((*tabs).tab_bounds(2U)));
    require((*tabs).selected_index.read() == 2U && window.focused_control() == tabs,
            "a click on a tab selects it and focuses the control");
    require(window.request_focus(Control::Ptr{}), "focus can be cleared");
    click(window, centre((*tabs).tab_bounds(2U)));
    require(window.focused_control() == tabs, "a click on the selected tab focuses the control too");
    require(window.dispatch_key(key(PhysicalKey::left)) && (*tabs).selected_index.read() == 1U &&
                window.dispatch_key(key(PhysicalKey::end)) && (*tabs).selected_index.read() == 2U,
            "the focused control's arrows and End select through the strip");
}

void keys_and_commands() {
    std::shared_ptr<TabControl> tabs = tab_control("tabs", {"One", "Two", "Three"}, {0.0, 0.0, 400.0, 200.0});
    Window window(tabs, {400.0, 200.0});
    window.perform_layout();
    require(window.request_focus(tabs), "the control takes focus");
    require(!window.dispatch_key(key(PhysicalKey::f4, Modifier::control)) &&
                !window.dispatch_key(key(PhysicalKey::f10, Modifier::shift)) &&
                !window.dispatch_key(key(PhysicalKey::context_menu)) &&
                !window.dispatch_key(key(PhysicalKey::page_down, Modifier::control | Modifier::shift)),
            "close, a tab's context menu and moving a tab are left to the controls around it");
    require(window.dispatch_key(key(PhysicalKey::page_down, Modifier::control)) && (*tabs).selected_index.read() == 1U,
            "Ctrl+PageDown selects the next page");
    window.rebind_key_command(KeyCommand::cycle_forward, {{PhysicalKey::j, Modifier::control}, {}});
    require(window.dispatch_key(key(PhysicalKey::j, Modifier::control)) && (*tabs).selected_index.read() == 2U,
            "the control follows a rebound cycle_forward");
    static_cast<void>(window.dispatch_key(key(PhysicalKey::tab, Modifier::control)));
    require((*tabs).selected_index.read() == 2U, "Ctrl+Tab no longer cycles once cycle_forward is rebound");
}

// A TabControl inside a document page: Ctrl+Tab belongs to the most local
// TabControl (G1-2), Ctrl+F4 passes through it to the view.
void inside_a_document_view() {
    PageViewOptions options{};
    options.preset = PageViewPreset::document;
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("docs"), options);
    std::shared_ptr<TabControl> outer = tab_control("outer", {"Outer one", "Outer two"}, {0.0, 0.0, 500.0, 260.0});
    std::shared_ptr<TabControl> inner = tab_control("inner", {"Inner one", "Inner two", "Inner three"},
                                                    {10.0, 10.0, 300.0, 150.0});
    (*(*outer).page_at(0U)).add_child(inner);
    PageRecord record{};
    record.id = PageId{"d0"};
    record.title = "Document";
    record.closable = true;
    require((*view).add_page(record, outer) == PageEditStatus::applied, "the document is added");
    Window window(view, {520.0, 320.0});
    window.perform_layout();
    std::size_t closes = 0U;
    TakeClose take{&closes};
    SubscriptionToken closing = (*(*view).strip()).page_close_requested().subscribe(take);
    require(window.request_focus(inner), "the inner control takes focus");
    require(window.dispatch_key(key(PhysicalKey::tab, Modifier::control)) && (*inner).selected_index.read() == 1U &&
                (*outer).selected_index.read() == 0U,
            "Ctrl+Tab goes to the most local TabControl");
    require(window.dispatch_key(key(PhysicalKey::f4, Modifier::control)) && closes == 1U,
            "Ctrl+F4 passes through the tab controls to the document view");
    require(window.request_focus(outer), "the outer control takes focus");
    require(window.dispatch_key(key(PhysicalKey::tab, Modifier::control)) && (*outer).selected_index.read() == 1U &&
                (*inner).selected_index.read() == 1U,
            "with the focus outside the inner control, the outer one cycles");
}

void host_map_keeps_ctrl_tab() {
    std::shared_ptr<TabControl> tabs = tab_control("tabs", {"One", "Two"}, {0.0, 0.0, 400.0, 200.0});
    Window window(tabs, {400.0, 200.0});
    KeyMap map = pc_key_map();
    map.set_binding(KeyCommand::close_document, {{PhysicalKey::w, Modifier::meta}, {}});
    HostSession session(window, {HostCapabilities::current_protocol_version, "macos-style-fixture",
                                 HostCapability::keyboard_input, map});
    window.perform_layout();
    require(window.request_focus(tabs), "the control takes focus");
    require(window.dispatch_key(key(PhysicalKey::tab, Modifier::control)) && (*tabs).selected_index.read() == 1U,
            "under the macOS map Ctrl+Tab still cycles");
    require(!window.dispatch_key(key(PhysicalKey::w, Modifier::meta)),
            "Cmd+W is a document's close, not the control's");
    static_cast<void>(window.dispatch_key(key(PhysicalKey::tab, Modifier::meta)));
    require((*tabs).selected_index.read() == 1U, "Meta+Tab is not a tab key");
}

std::size_t paint_allocations(std::size_t page_count) {
    std::shared_ptr<TabControl> tabs = tab_control("tabs", numbered(page_count), {0.0, 0.0, 4000.0, 200.0});
    (*tabs).size_mode = TabSizeMode::fixed;
    (*tabs).item_size = Size{40.0, 30.0};
    Window window(tabs, {4000.0, 200.0});
    window.perform_layout();
    const Control::Ptr strip = window.find("tabs.strip");
    CountingPainter painter{};
    const Rect own = (*tabs).client_rectangle();
    const Rect band = (*strip).client_rectangle();
    // The first paint lays out the strip (and may allocate); the measured
    // one paints from the cached layout.
    (*tabs).on_paint(painter, own);
    (*strip).on_paint(painter, band);
    std::size_t attempts = 0U;
    {
        tab_control_allocation_probe::CountScope probe{};
        (*tabs).on_paint(painter, own);
        (*strip).on_paint(painter, band);
        attempts = tab_control_allocation_probe::attempts;
    }
    require(painter.texts >= page_count * 2U, "every tab's caption was painted, by role");
    return attempts;
}

void no_allocation_per_tab_while_painting() {
    const std::size_t few = paint_allocations(4U);
    const std::size_t many = paint_allocations(40U);
    std::cout << "TabControl paint allocations: 4 tabs " << few << ", 40 tabs " << many << '\n';
    require(few == many && many == 0U, "painting allocates nothing, per tab or otherwise");
}

} // namespace

int main() {
    sizing_modes();
    overflow_keeps_the_selection_in_view();
    strip_is_internal();
    keys_and_commands();
    inside_a_document_view();
    host_map_keeps_ctrl_tab();
    no_allocation_per_tab_while_painting();
    std::cout << "TabControl on PageStrip: sizing modes, overflow, the internal strip, keys and commands, "
                 "the most local TabControl, a host's map and paint allocation passed\n";
    return 0;
}
