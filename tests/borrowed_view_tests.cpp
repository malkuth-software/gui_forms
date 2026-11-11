// The borrowed views (borrow sanitizer stage 2, owner rulings 2026-11-11).
// Mode `plain` reads every applied group the way callers do and prints
// "borrowed views: ok"; it runs in both builds. In the checked build
// (GUI_FORMS_CHECKED_BORROWS, the sanitizer tree) each other mode takes a
// view, changes its owner (or ends it) and uses the view again: the borrow
// sanitizer must end the process with its report. Each prints "survived"
// only if the process was not ended; CTest passes a mode when its report
// line appears and "survived" does not (as gui_forms_fail_fast_tests).

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/controls/panel/list_box/list_box.hpp"
#include "gui_forms/controls/panel/list_view/list_view.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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
static_assert(std::is_same_v<decltype(std::declval<const Control&>().window()), Window*>);
static_assert(std::is_same_v<decltype(std::declval<const Control&>().surface_root()), SurfaceRoot*>);
static_assert(std::is_same_v<decltype(std::declval<const ListView&>().items()), std::span<const ListViewItem>>);
static_assert(std::is_same_v<decltype(std::declval<const SurfaceRoot&>().presenting_window()), Window*>);
static_assert(std::is_same_v<detail::LendingVector<Control::Ptr>, std::vector<Control::Ptr>>);
static_assert(sizeof(detail::LendingPointer<Window>) == sizeof(Window*));
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

    const BorrowedPointer<Window> attached = (*first).window();
    Window* const attached_plain = attached;
    if (attached_plain != &window) return fail("window back link differs");
    SurfaceRoot* const root_plain = (*first).surface_root();
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
    const BorrowedPointer<Window> attached = (*child).window();
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
        std::fprintf(stderr, "usage: gui_forms_borrowed_view_tests plain|children|back_link|collection|list_items|"
                             "owner_death\n");
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
    else {
        std::fprintf(stderr, "unknown mode\n");
        return EXIT_FAILURE;
    }
    std::printf("survived\n");
    return EXIT_SUCCESS;
}
