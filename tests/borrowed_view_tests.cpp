// The borrowed views (borrow sanitizer stages 2 and 3, owner rulings
// 2026-11-11). Mode `plain` reads every applied group the way callers do
// and prints "borrowed views: ok"; it runs in both builds. In the checked
// build (GUI_FORMS_CHECKED_BORROWS, the sanitizer tree) each other mode
// takes a view and misuses it: the borrow sanitizer must end the process
// with its report. Each prints "survived" only if the process was not
// ended; CTest passes a mode when its report line appears and "survived"
// does not (as gui_forms_fail_fast_tests).
//
// The death fixtures cover each fault (study section 2.7) on the four
// subjects it names: the child list, a collection cell's read, a back link
// and a call-scope payload.
//
//   fault                     children        collection         back link          payload
//   structurally_invalidated  children        collection         back_link          (a scope only ends)
//   owner_destroyed           owner_death     collection_death   back_link_death    payload_death
//   wrong_thread              children_thread collection_thread  back_link_thread   payload_thread
//   out_of_range              children_range  collection_range   back_link_null *   payload_range
//
// (* a pointer has no index: its own fault, null_dereference.) Stage 3 adds
// text_store, layout_workspace, write_lease and raster_death.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/list_view.hpp"
#include "gui_forms/controls/tool_strip.hpp"
#include "gui_forms/components/file_dialog_types/file_dialog_types.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/layout/block_flow_solver/block_flow_solver.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/live_surface/write_lease/live_surface_write_lease.hpp"
#include "gui_forms/text/text_store/text_store.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace gui_forms;

#if !GUI_FORMS_CHECKED_BORROWS
// Ruling 2: the ordinary build compiles to the plain views, member types
// and accessors alike.
static_assert(std::is_same_v<decltype(std::declval<const Control&>().children()), std::span<const Control::Ptr>>);
static_assert(std::is_same_v<decltype(std::declval<const Control&>().attached_window()), Window*>);
static_assert(std::is_same_v<decltype(std::declval<const Control&>().attached_surface_root()), SurfaceRoot*>);
static_assert(std::is_same_v<decltype(std::declval<const ListView&>().items()), std::span<const ListViewItem>>);
static_assert(std::is_same_v<decltype(std::declval<const SurfaceRoot&>().presenting_window()), Window*>);
static_assert(std::is_same_v<detail::LendingVector<Control::Ptr>, std::vector<Control::Ptr>>);
static_assert(sizeof(detail::LendingPointer<Window>) == sizeof(Window*));
static_assert(std::is_same_v<Control::ChildView, std::span<const Control::Ptr>>);
static_assert(std::is_same_v<decltype(std::declval<const TextStore&>().utf8()), std::string_view>);
static_assert(std::is_same_v<decltype(std::declval<const PartRaster&>().pixels()), std::span<const std::uint8_t>>);
static_assert(std::is_same_v<decltype(std::declval<LiveSurfaceWriteLease&>().pixels()), std::span<std::byte>>);
static_assert(std::is_same_v<decltype(FileDialogOk{}.file_names), std::span<const std::string>>);
// Stage 3 lenders are empty members: the ordinary layouts are unchanged.
static_assert(std::is_empty_v<detail::BorrowLender>);
#endif

// A death test must not open the system's crash dialog, which would hold
// the test until someone dismisses it.
void quiet_crash_reporting() {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#if defined(_MSC_VER) || defined(_UCRT)
    _set_abort_behavior(0U, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#endif
}

ListViewItem list_view_item(std::string stable_id, std::string text) {
    ListViewItem item{};
    item.stable_id = std::move(stable_id);
    item.text = std::move(text);
    return item;
}

int fail(const char* what) {
    std::fprintf(stderr, "borrowed views: %s\n", what);
    return EXIT_FAILURE;
}

// A thread's work: reads the first element of `view` (an any_thread view
// in plain mode; another thread's view in the wrong-thread modes).
template <typename View>
void read_first_on_thread(const View* view) {
    const std::size_t count = (*view).size();
    std::printf("read %zu elements\n", count);
}

template <typename View>
void read_on_other_thread(const View& view) {
    std::thread reader(&read_first_on_thread<View>, &view);
    reader.join();
}

// The back link read on another thread.
void read_pointer_on_thread(const BorrowedPointer<Window>* pointer) {
    Window* const window = *pointer;
    std::printf("read %s\n", window != nullptr ? "a window" : "nothing");
}

// A raise's payload, as DockWorkspace and the file dialogs make one: the
// scope owns the view, and `kept` is what a handler copied out of it.
void raise_file_ok(const std::vector<std::string>& names, FileDialogOk& kept) {
    const detail::BorrowScope raise;
    FileDialogOk ok{raise.lend_view(std::span<const std::string>(names.data(), names.size())), false};
    kept = ok;
}

// Every group read as callers read it, in either build: the views agree
// with their owners, convert to the plain views and survive reads.
int plain_mode() {
    std::shared_ptr<Control> root = make_control<Control>(StableId("borrow.root"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    std::shared_ptr<Control> first = make_control<Control>(StableId("borrow.first"));
    std::shared_ptr<Control> second = make_control<Control>(StableId("borrow.second"));
    (*root).add_child(first);
    (*root).add_child(second);
    Window window(root, {120.0, 80.0});

    const BorrowedSpan<Control::Ptr> children = (*root).children();
    if (children.size() != 2U || children[0] != first || children[1] != second) return fail("children differ");
    std::size_t walked = 0U;
    for (const Control::Ptr& child : children) {
        if (child) ++walked;
    }
    if (walked != 2U) return fail("children walk differs");
    const std::span<const Control::Ptr> plain_children = (*root).children();
    if (plain_children.size() != 2U) return fail("plain children differ");

    const BorrowedPointer<Window> attached = (*first).attached_window();
    Window* const attached_plain = attached;
    if (attached_plain != &window) return fail("window back link differs");
    SurfaceRoot* const root_plain = (*first).attached_surface_root();
    if (root_plain == nullptr) return fail("surface root back link missing");
    Window* const presented = window.presenting_window();
    if (presented != &window) return fail("presenting window differs");

    std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("borrow.list"));
    (*list).items = {"AM", "FM"};
    const BorrowedSpan<std::string> items = (*list).items.read();
    if (items.size() != 2U || items[1] != "FM") return fail("collection read differs");

    std::shared_ptr<ListView> view = make_control<ListView>(StableId("borrow.view"));
    std::vector<ListViewItem> rows;
    rows.push_back(list_view_item("borrow.row.a", "a"));
    (*view).set_items(std::move(rows));
    const BorrowedSpan<ListViewItem> view_items = (*view).items();
    if (view_items.size() != 1U || view_items[0].text != "a") return fail("list items differ");

    // Index walks and the named alias (study section 2.5).
    const Control::ChildView named = (*root).children();
    if ((*root).child_count() != named.size()) return fail("child_count differs");
    if ((*root).child_at(1U) != second || (*root).child_at(2U) != nullptr) return fail("child_at differs");
    const std::span<const Control::Ptr> unchecked = unchecked_view(named);
    if (unchecked.size() != 2U) return fail("unchecked_view differs");

    // The weak window handle: the window while it lives.
    const WindowRef handle = window.window_ref();
    if (handle.get() != &window) return fail("window_ref differs");

    // A call-scope payload, read inside the raise.
    const std::vector<std::string> names{"a.txt", "b.txt"};
    {
        const detail::BorrowScope raise;
        const FileDialogOk ok{raise.lend_view(std::span<const std::string>(names.data(), names.size())), false};
        if (ok.file_names.size() != 2U || ok.file_names[1] != "b.txt") return fail("payload differs");
    }

    // Stage 3: text, layout workspace, raster and lease views.
    TextStore store("borrowed");
    const BorrowedText text = store.utf8();
    if (text != std::string_view("borrowed") || text.size() != 8U) return fail("text store view differs");
    BlockFlowLayoutWorkspace workspace;
    workspace.reserve(4U);
    const BorrowedSpan<BlockFlowItemResult> results = workspace.items();
    if (!results.empty()) return fail("fresh workspace has results");
    const std::shared_ptr<PartRaster> raster = PartRaster::create(DeviceExtent{2U, 2U}, DeviceScale{1.0});
    if (!raster) return fail("raster not made");
    const BorrowedSpan<std::uint8_t> raster_pixels = (*raster).pixels();
    if (raster_pixels.size() != 16U) return fail("raster pixels differ");
    LiveSurfaceDescription description{};
    description.width = 2U;
    description.height = 2U;
    const std::shared_ptr<LiveSurface> surface = LiveSurface::create(description);
    if (!surface) return fail("live surface not made");
    LiveSurfaceWriteLease lease = (*surface).try_acquire_write();
    const BorrowedMutableSpan<std::byte> lease_pixels = lease.pixels();
    if (lease_pixels.size() != 16U) return fail("lease pixels differ");
    // A lease's views are any_thread: another thread may read them.
    read_on_other_thread(lease_pixels);
    static_cast<void>(lease.publish());

    // The const vector& siblings now lend views (study section 2.5, row 4).
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("borrow.strip"));
    std::vector<ToolStripItemSpec> specs;
    specs.push_back({.stable_id = "borrow.strip.new", .text = "New"});
    specs.push_back({.stable_id = "borrow.strip.open", .text = "Open"});
    (*strip).set_items(std::move(specs));
    const BorrowedSpan<ToolStripItemSpec> strip_items = (*strip).items();
    if (strip_items.size() != 2U || strip_items[1].stable_id != "borrow.strip.open") return fail("strip items differ");
    const std::vector<ToolStripItemSpec> copied(strip_items.begin(), strip_items.end());
    if (copied.size() != 2U) return fail("strip items copy differs");

    std::printf("borrowed views: ok\n");
    return EXIT_SUCCESS;
}

// A view of the child list, then an added child, then a read.
void children_mode() {
    std::shared_ptr<Control> root = make_control<Control>(StableId("borrow.root"));
    (*root).add_child(make_control<Control>(StableId("borrow.first")));
    const BorrowedSpan<Control::Ptr> children = (*root).children();
    (*root).add_child(make_control<Control>(StableId("borrow.second")));
    const Control::Ptr& stale = children[0];
    std::printf("read %s\n", stale ? "a child" : "nothing");
}

// A borrowed window back link, then the control leaves the window, then a
// read.
void back_link_mode() {
    std::shared_ptr<Control> root = make_control<Control>(StableId("borrow.root"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    std::shared_ptr<Control> child = make_control<Control>(StableId("borrow.child"));
    (*root).add_child(child);
    Window window(root, {120.0, 80.0});
    const BorrowedPointer<Window> attached = (*child).attached_window();
    static_cast<void>((*root).remove_child((*child).runtime_id()));
    Window* const stale = attached;
    std::printf("read %s\n", stale != nullptr ? "a window" : "nothing");
}

// A collection cell's read, then an appended element, then a read.
void collection_mode() {
    std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("borrow.list"));
    (*list).items = {"AM", "FM"};
    const BorrowedSpan<std::string> items = (*list).items.read();
    (*list).items.append("WFM");
    const std::string& stale = items[0];
    std::printf("read %zu bytes\n", stale.size());
}

// A list control's items, then new items, then a read.
void list_items_mode() {
    std::shared_ptr<ListView> view = make_control<ListView>(StableId("borrow.view"));
    std::vector<ListViewItem> rows;
    rows.push_back(list_view_item("borrow.row.a", "a"));
    (*view).set_items(std::move(rows));
    const BorrowedSpan<ListViewItem> items = (*view).items();
    std::vector<ListViewItem> replacement;
    replacement.push_back(list_view_item("borrow.row.b", "b"));
    (*view).set_items(std::move(replacement));
    const ListViewItem& stale = items[0];
    std::printf("read %zu bytes\n", stale.text.size());
}

// A view of the child list read on another thread.
void children_thread_mode() {
    std::shared_ptr<Control> root = make_control<Control>(StableId("borrow.root"));
    (*root).add_child(make_control<Control>(StableId("borrow.first")));
    const Control::ChildView children = (*root).children();
    read_on_other_thread(children);
}

// A view of the child list indexed past its end.
void children_range_mode() {
    std::shared_ptr<Control> root = make_control<Control>(StableId("borrow.root"));
    (*root).add_child(make_control<Control>(StableId("borrow.first")));
    const Control::ChildView children = (*root).children();
    const Control::Ptr& past = children[1];
    std::printf("read %s\n", past ? "a child" : "nothing");
}

// A collection cell's read outliving its control.
void collection_death_mode() {
    BorrowedSpan<std::string> items{};
    {
        std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("borrow.list"));
        (*list).items = {"AM", "FM"};
        items = (*list).items.read();
    }
    const std::string& stale = items[0];
    std::printf("read %zu bytes\n", stale.size());
}

// A collection cell's read used on another thread.
void collection_thread_mode() {
    std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("borrow.list"));
    (*list).items = {"AM", "FM"};
    const BorrowedSpan<std::string> items = (*list).items.read();
    read_on_other_thread(items);
}

// A collection cell's read indexed past its end.
void collection_range_mode() {
    std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("borrow.list"));
    (*list).items = {"AM", "FM"};
    const BorrowedSpan<std::string> items = (*list).items.read();
    const std::string& past = items[2];
    std::printf("read %zu bytes\n", past.size());
}

// A back link outliving the control that lent it (a detached control's
// link is null, but stamped all the same).
void back_link_death_mode() {
    BorrowedPointer<Window> attached{};
    {
        std::shared_ptr<Control> lone = make_control<Control>(StableId("borrow.lone"));
        attached = (*lone).attached_window();
    }
    Window* const stale = attached;
    std::printf("read %s\n", stale != nullptr ? "a window" : "nothing");
}

// A window back link read on another thread.
void back_link_thread_mode() {
    std::shared_ptr<Control> root = make_control<Control>(StableId("borrow.root"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    std::shared_ptr<Control> child = make_control<Control>(StableId("borrow.child"));
    (*root).add_child(child);
    Window window(root, {120.0, 80.0});
    const BorrowedPointer<Window> attached = (*child).attached_window();
    std::thread reader(&read_pointer_on_thread, &attached);
    reader.join();
}

// A detached control's (null) back link dereferenced.
void back_link_null_mode() {
    std::shared_ptr<Control> lone = make_control<Control>(StableId("borrow.lone"));
    const BorrowedPointer<Window> attached = (*lone).attached_window();
    const Window& stale = *attached;
    std::printf("read %p\n", static_cast<const void*>(&stale));
}

// A payload a handler kept after the raise.
void payload_death_mode() {
    const std::vector<std::string> names{"a.txt"};
    FileDialogOk kept{};
    raise_file_ok(names, kept);
    std::printf("read %zu names\n", kept.file_names.size());
}

// A payload read on another thread during the raise.
void payload_thread_mode() {
    const std::vector<std::string> names{"a.txt"};
    const detail::BorrowScope raise;
    const FileDialogOk ok{raise.lend_view(std::span<const std::string>(names.data(), names.size())), false};
    read_on_other_thread(ok.file_names);
}

// A payload indexed past its end during the raise.
void payload_range_mode() {
    const std::vector<std::string> names{"a.txt"};
    const detail::BorrowScope raise;
    const FileDialogOk ok{raise.lend_view(std::span<const std::string>(names.data(), names.size())), false};
    const std::string& past = ok.file_names[1];
    std::printf("read %zu bytes\n", past.size());
}

// A text store's view, then an edit, then a read.
void text_store_mode() {
    TextStore store("borrowed");
    const BorrowedText text = store.utf8();
    store.set_text("changed");
    std::printf("read %zu bytes\n", text.size());
}

// A tool strip's item view, then set_items, then a read.
void tool_strip_items_mode() {
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("borrow.strip"));
    std::vector<ToolStripItemSpec> first;
    first.push_back({.stable_id = "borrow.strip.new", .text = "New"});
    (*strip).set_items(std::move(first));
    const BorrowedSpan<ToolStripItemSpec> items = (*strip).items();
    std::vector<ToolStripItemSpec> second;
    second.push_back({.stable_id = "borrow.strip.open", .text = "Open"});
    (*strip).set_items(std::move(second));
    std::printf("read %zu items\n", items.size());
}

// A layout workspace's view, then the next solve's storage, then a read.
void layout_workspace_mode() {
    BlockFlowLayoutWorkspace workspace;
    const BorrowedSpan<BlockFlowItemResult> results = workspace.items();
    workspace.reserve(8U);
    std::printf("read %zu results\n", results.size());
}

// A write lease's pixels used after it published.
void write_lease_mode() {
    LiveSurfaceDescription description{};
    description.width = 2U;
    description.height = 2U;
    const std::shared_ptr<LiveSurface> surface = LiveSurface::create(description);
    LiveSurfaceWriteLease lease = (*surface).try_acquire_write();
    const BorrowedMutableSpan<std::byte> pixels = lease.pixels();
    static_cast<void>(lease.publish());
    std::printf("read %zu bytes\n", pixels.size());
}

// A part raster's pixels outliving the raster.
void raster_death_mode() {
    BorrowedSpan<std::uint8_t> pixels{};
    {
        const std::shared_ptr<PartRaster> raster = PartRaster::create(DeviceExtent{2U, 2U}, DeviceScale{1.0});
        pixels = (*raster).pixels();
    }
    std::printf("read %zu bytes\n", pixels.size());
}

// A view of the child list outliving its control.
void owner_death_mode() {
    BorrowedSpan<Control::Ptr> children{};
    {
        std::shared_ptr<Control> root = make_control<Control>(StableId("borrow.root"));
        (*root).add_child(make_control<Control>(StableId("borrow.first")));
        children = (*root).children();
    }
    const Control::Ptr& stale = children[0];
    std::printf("read %s\n", stale ? "a child" : "nothing");
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: gui_forms_borrowed_view_tests plain|<death mode> (see the file comment)\n");
        return EXIT_FAILURE;
    }
    quiet_crash_reporting();
    const std::string_view mode = argv[1];
    if (mode == "plain") return plain_mode();
    if (mode == "children") children_mode();
    else if (mode == "back_link") back_link_mode();
    else if (mode == "collection") collection_mode();
    else if (mode == "list_items") list_items_mode();
    else if (mode == "owner_death") owner_death_mode();
    else if (mode == "children_thread") children_thread_mode();
    else if (mode == "children_range") children_range_mode();
    else if (mode == "collection_death") collection_death_mode();
    else if (mode == "collection_thread") collection_thread_mode();
    else if (mode == "collection_range") collection_range_mode();
    else if (mode == "back_link_death") back_link_death_mode();
    else if (mode == "back_link_thread") back_link_thread_mode();
    else if (mode == "back_link_null") back_link_null_mode();
    else if (mode == "payload_death") payload_death_mode();
    else if (mode == "payload_thread") payload_thread_mode();
    else if (mode == "payload_range") payload_range_mode();
    else if (mode == "text_store") text_store_mode();
    else if (mode == "layout_workspace") layout_workspace_mode();
    else if (mode == "tool_strip_items") tool_strip_items_mode();
    else if (mode == "write_lease") write_lease_mode();
    else if (mode == "raster_death") raster_death_mode();
    else {
        std::fprintf(stderr, "unknown mode\n");
        return EXIT_FAILURE;
    }
    std::printf("survived\n");
    return EXIT_SUCCESS;
}
