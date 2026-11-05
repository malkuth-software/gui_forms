#include "gui_forms/gui_forms.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class RecordingPainter final : public Painter {
public:
    void save() override { ++commands; }
    void restore() override { ++commands; }
    void translate(Point) override { ++commands; }
    void clip_rect(Rect) override { ++commands; }
    void fill_rect(Rect, Color) override { ++fills; ++commands; }
    void stroke_rect(Rect, Color, double) override { ++strokes; ++commands; }
    void draw_line(Point, Point, Color, double) override { ++commands; }
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color) override {
        texts.emplace_back(text);
        ++commands;
    }
    void draw_image(ImageId image, Rect, double) override {
        images.push_back(image);
        ++commands;
    }

    std::uint64_t commands{};
    std::uint64_t fills{};
    std::uint64_t strokes{};
    std::vector<std::string> texts;
    std::vector<ImageId> images;
};

struct Fixture final {
    std::shared_ptr<TabControl> tabs;
    std::shared_ptr<TabPage> first;
    std::shared_ptr<TabPage> second;
    std::shared_ptr<TabPage> third;
    std::shared_ptr<Button> first_button;
    std::shared_ptr<Button> second_button;
};

class ObserveTabSelection final {
public:
    ObserveTabSelection(TabSelectionChange& observed, std::uint64_t& changes)
        : observed_(observed), changes_(changes) {}

    void operator()(const TabSelectionChange& change) const {
        observed_ = change;
        ++changes_;
    }

private:
    TabSelectionChange& observed_;
    std::uint64_t& changes_;
};

Fixture make_fixture() {
    Fixture value;
    value.tabs = make_control<TabControl>(StableId("tabs"));
    (*value.tabs).accessible_name = "Workspace tabs";
    (*value.tabs).set_requested_bounds({0.0, 0.0, 480.0, 260.0});
    value.first = make_control<TabPage>(StableId("tabs.first"), "Overview");
    value.second = make_control<TabPage>(StableId("tabs.second"), "Details");
    value.third = make_control<TabPage>(StableId("tabs.third"), "Diagnostics");
    value.first_button = make_control<Button>(StableId("tabs.first.button"),
                                               "First page action");
    value.second_button = make_control<Button>(StableId("tabs.second.button"),
                                                "Second page action");
    (*value.first_button).set_requested_bounds({20.0, 20.0, 160.0, 32.0});
    (*value.second_button).set_requested_bounds({20.0, 20.0, 170.0, 32.0});
    (*value.first).add_child(value.first_button);
    (*value.second).add_child(value.second_button);
    std::shared_ptr<gui_forms::Label> third_label = make_control<Label>(StableId("tabs.third.label"),
                                            "Third page content");
    (*third_label).set_requested_bounds({20.0, 20.0, 180.0, 30.0});
    (*value.third).add_child(third_label);
    (*value.tabs).add_page(value.first);
    (*value.tabs).add_page(value.second);
    (*value.tabs).add_page(value.third);
    return value;
}

void test_page_ownership_layout_and_visibility() {
    Fixture fixture = make_fixture();
    Window window(fixture.tabs, {480.0, 260.0});
    window.perform_layout();
    require((*fixture.tabs).page_count() == 3U &&
                (*fixture.tabs).selected_index() == 0U &&
                (*fixture.first).visible.read() && !(*fixture.second).visible.read() &&
                !(*fixture.third).visible.read(),
            "TabControl must own pages and expose exactly one selected page");
    require((*fixture.tabs).display_bounds() == Rect{0.0, 29.0, 480.0, 231.0} &&
                (*fixture.first).committed_arranged_bounds() ==
                    (*fixture.tabs).display_bounds(),
            "top-aligned TabControl must reserve a deterministic header strip");

    TabSelectionChange observed;
    std::uint64_t changes{};
    SubscriptionToken token =
        (*fixture.tabs).selected_index_changed().subscribe(
            ObserveTabSelection(observed, changes));
    (*fixture.tabs).set_selected_index(1U);
    window.perform_layout();
    require(changes == 1U && observed.old_index == 0U &&
                observed.new_index == 1U && !(*fixture.first).visible.read() &&
                (*fixture.second).visible.read(),
            "TabControl selection must mutate visibility before one ordered event");
    require(window.hit_test({30.0, 70.0}) != fixture.first_button,
            "a hidden TabPage subtree must not remain hit-testable");

    RecordingPainter painter;
    window.paint(painter, {0.0, 0.0, 480.0, 260.0});
    require(std::find(painter.texts.begin(), painter.texts.end(), "Overview") !=
                painter.texts.end() &&
                std::find(painter.texts.begin(), painter.texts.end(), "Details") !=
                painter.texts.end() &&
                std::find(painter.texts.begin(), painter.texts.end(),
                          "First page action") == painter.texts.end() &&
                std::find(painter.texts.begin(), painter.texts.end(),
                          "Second page action") != painter.texts.end(),
            "TabControl must paint every header and only the selected page content");
}

void test_keyboard_pointer_and_focus_restoration() {
    Fixture fixture = make_fixture();
    Window window(fixture.tabs, {480.0, 260.0});
    window.perform_layout();
    require(window.request_focus(fixture.first_button),
            "first page child must receive initial focus");
    KeyEvent next{KeyAction::down, PhysicalKey::tab, Modifier::control};
    require(window.dispatch_key(next) && (*fixture.tabs).selected_index() == 1U &&
                window.focused_control() == fixture.tabs,
            "Ctrl+Tab from page content must switch pages and retain safe focus");
    require(window.request_focus(fixture.second_button),
            "second page child must receive focus");
    KeyEvent previous{KeyAction::down, PhysicalKey::tab,
                      Modifier::control | Modifier::shift};
    require(window.dispatch_key(previous) && (*fixture.tabs).selected_index() == 0U &&
                window.focused_control() == fixture.first_button,
            "Ctrl+Shift+Tab must restore the destination page's remembered focus");

    const Rect third = (*fixture.tabs).tab_bounds(2U);
    const Point click{third.x + third.width * 0.5, third.y + third.height * 0.5};
    require(window.dispatch_pointer(
                {PointerAction::down, PointerButton::primary, click}) &&
                (*fixture.tabs).selected_index() == 2U &&
                window.focused_control() == fixture.tabs,
            "pointer header activation must select and focus the tab strip");
    KeyEvent left{KeyAction::down, PhysicalKey::left};
    require(window.dispatch_key(left) && (*fixture.tabs).selected_index() == 1U,
            "focused horizontal tabs must traverse with arrow keys");
    KeyEvent home{KeyAction::down, PhysicalKey::home};
    require(window.dispatch_key(home) && (*fixture.tabs).selected_index() == 0U,
            "focused tabs must support Home/End boundary traversal");
}

// Records each page event as "<action>:<index>"; cancels the event whose
// action and index match `cancel_action` and `cancel_index`.
class RecordPageEvents final {
public:
    RecordPageEvents(std::vector<std::string>& seen, std::string& cancel) : seen_(seen), cancel_(cancel) {}

    void operator()(TabControlCancelEvent& event) const {
        const std::string entry = name(event.action) + ":" + std::to_string(event.index);
        seen_.push_back(entry);
        if (entry == cancel_) event.cancel = true;
    }
    void operator()(const TabControlEvent& event) const {
        seen_.push_back(name(event.action) + ":" + std::to_string(event.index));
    }

private:
    static std::string name(TabControlAction action) {
        if (action == TabControlAction::deselecting) return "deselecting";
        if (action == TabControlAction::selecting) return "selecting";
        if (action == TabControlAction::deselected) return "deselected";
        return "selected";
    }

    std::vector<std::string>& seen_;
    std::string& cancel_;
};

class RecordIndexChange final {
public:
    explicit RecordIndexChange(std::vector<std::string>& seen) : seen_(seen) {}
    void operator()(const TabSelectionChange& change) const {
        seen_.push_back("changed:" + std::to_string(change.new_index.value_or(99U)));
    }

private:
    std::vector<std::string>& seen_;
};

// Windows Forms' Deselecting, Selecting, Deselected and Selected: the two
// cancellable events come before any change, by any route; the two after
// it, then selected_index_changed.
void test_selecting_events() {
    Fixture fixture = make_fixture();
    Window window(fixture.tabs, {480.0, 260.0});
    window.perform_layout();
    std::vector<std::string> seen;
    std::string cancel;
    const RecordPageEvents record(seen, cancel);
    SubscriptionToken deselecting = (*fixture.tabs).deselecting().subscribe(record);
    SubscriptionToken selecting = (*fixture.tabs).selecting().subscribe(record);
    SubscriptionToken deselected = (*fixture.tabs).deselected().subscribe(record);
    SubscriptionToken selected = (*fixture.tabs).selected().subscribe(record);
    SubscriptionToken changed = (*fixture.tabs).selected_index_changed().subscribe(RecordIndexChange(seen));

    (*fixture.tabs).set_selected_index(1U);
    const std::vector<std::string> order{"deselecting:0", "selecting:1", "deselected:0", "selected:1", "changed:1"};
    require(seen == order && (*fixture.tabs).selected_index() == 1U,
            "a change raises deselecting, selecting, deselected, selected, then selected_index_changed");

    seen.clear();
    cancel = "selecting:2";
    (*fixture.tabs).set_selected_index(2U);
    require(seen == std::vector<std::string>{"deselecting:1", "selecting:2"} &&
                (*fixture.tabs).selected_index() == 1U && (*fixture.second).visible.read(),
            "a cancelled selecting keeps the page");

    seen.clear();
    cancel = "deselecting:1";
    const Rect third = (*fixture.tabs).tab_bounds(2U);
    const Point click{third.x + third.width * 0.5, third.y + third.height * 0.5};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, click}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, click}));
    window.perform_layout();
    require(seen == std::vector<std::string>{"deselecting:1"} && (*fixture.tabs).selected_index() == 1U,
            "a cancelled deselecting refuses a click on another tab");

    seen.clear();
    cancel.clear();
    require(window.request_focus(fixture.tabs), "the control takes focus");
    KeyEvent next{KeyAction::down, PhysicalKey::tab, Modifier::control};
    require(window.dispatch_key(next) && (*fixture.tabs).selected_index() == 2U &&
                seen == std::vector<std::string>{"deselecting:1", "selecting:2", "deselected:1", "selected:2",
                                                 "changed:2"},
            "Ctrl+Tab raises the same events");

    seen.clear();
    KeyEvent home{KeyAction::down, PhysicalKey::home};
    require(window.dispatch_key(home) && (*fixture.tabs).selected_index() == 0U && seen.size() == 5U &&
                seen[1] == "selecting:0",
            "the strip's keys raise them too");

    seen.clear();
    std::shared_ptr<TabPage> removed = (*fixture.tabs).remove_page(*fixture.first);
    require(removed && seen == std::vector<std::string>{"changed:0"},
            "the page replacing a removed selected page raises only selected_index_changed");
}

void test_alignment_appearance_and_semantics() {
    Fixture fixture = make_fixture();
    Window window(fixture.tabs, {480.0, 260.0});
    window.perform_layout();
    const std::array<std::pair<TabAlignment, Rect>, 4> alignments{{
        {TabAlignment::top, {0.0, 29.0, 480.0, 231.0}},
        {TabAlignment::bottom, {0.0, 0.0, 480.0, 231.0}},
        {TabAlignment::left, {119.0, 0.0, 361.0, 260.0}},
        {TabAlignment::right, {0.0, 0.0, 361.0, 260.0}},
    }};
    for (const std::pair<TabAlignment, Rect>& alignment_spec : alignments) {
        (*fixture.tabs).set_alignment(alignment_spec.first);
        window.perform_layout();
        require((*fixture.tabs).display_bounds() == alignment_spec.second,
                "TabControl alignment must reserve the correct content edge");
    }
    for (const TabAppearance appearance :
         {TabAppearance::normal, TabAppearance::buttons,
          TabAppearance::flat_buttons}) {
        (*fixture.tabs).set_appearance(appearance);
        RecordingPainter painter;
        window.paint(painter, {0.0, 0.0, 480.0, 260.0});
        require(painter.fills >= 5U && painter.strokes >= 5U,
                "every TabControl appearance must produce a complete retained frame");
    }

    const SemanticSnapshot before = window.semantic_snapshot();
    const std::string json = before.to_json();
    require(json.find("\"role\":\"tab_group\"") != std::string::npos &&
                json.find("\"role\":\"tab\"") != std::string::npos &&
                json.find("tabs.third.tab") != std::string::npos,
            "TabControl must publish a tab group and stable virtual tab identities");
    require(window.perform_semantic_action("tabs.third.tab",
                                           SemanticAction::select) &&
                (*fixture.tabs).selected_index() == 2U,
            "semantic tab selection must execute the same public selection path");
}

void test_page_removal_and_disposal_reconciliation() {
    Fixture fixture = make_fixture();
    Window window(fixture.tabs, {480.0, 260.0});
    (*fixture.tabs).set_selected_index(1U);
    const std::shared_ptr<TabPage> removed = (*fixture.tabs).remove_page(*fixture.second);
    window.perform_layout();
    require(removed == fixture.second && !(*removed).attached() &&
                (*fixture.tabs).page_count() == 2U &&
                (*fixture.tabs).selected_tab() == fixture.third &&
                (*fixture.tabs).selected_index() == 1U,
            "removing a selected page must choose its surviving successor");

    (*fixture.third).dispose();
    window.perform_layout();
    require((*fixture.tabs).page_count() == 1U &&
                (*fixture.tabs).selected_tab() == fixture.first &&
                (*fixture.tabs).selected_index() == 0U && (*fixture.first).visible.read(),
            "direct selected-page disposal must reconcile without stale ownership");
}

template <typename Action>
bool throws(Action action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (const std::out_of_range&) {
        return true;
    }
    return false;
}

struct SetImageKey final {
    TabPage* page{};
    std::string key{};
    void operator()() const { (*page).set_image_key(key); }
};

struct SetImageIndex final {
    TabPage* page{};
    int index{};
    void operator()() const { (*page).set_image_index(index); }
};

struct SetImageList final {
    TabControl* tabs{};
    std::shared_ptr<ImageList> list{};
    void operator()() const { (*tabs).set_image_list(list); }
};

bool painted(const RecordingPainter& painter, ImageId image) {
    const bool found = std::find(painter.images.begin(), painter.images.end(), image) != painter.images.end();
    return found;
}

void test_tab_images() {
    Fixture fixture = make_fixture();
    Window window(fixture.tabs, {480.0, 260.0});
    const std::array<std::byte, 4> blue{std::byte{0xcc}, std::byte{0x77}, std::byte{0x33}, std::byte{0xff}};
    const std::array<std::byte, 4> green{std::byte{0x33}, std::byte{0xcc}, std::byte{0x33}, std::byte{0xff}};
    const ImageLoadResult folder = window.load_bgra32_premultiplied(1U, 1U, 4U, blue);
    const ImageLoadResult chart = window.load_bgra32_premultiplied(1U, 1U, 4U, green);
    require(static_cast<bool>(folder) && static_cast<bool>(chart), "tab image fixtures must load");

    require(throws(SetImageIndex{fixture.first.get(), -2}), "an image index below -1 is refused");
    require(throws(SetImageKey{fixture.first.get(), std::string("bad \xC3", 5U)}),
            "an image key that is not UTF-8 is refused");
    require(throws(SetImageKey{fixture.first.get(), std::string(257U, 'k')}), "an image key over 256 bytes is refused");
    (*fixture.first).set_image_key("folder");
    (*fixture.third).set_image_index(1);
    require((*fixture.first).image_index() == -1 && (*fixture.third).image_key().empty(),
            "a key and an index exclude each other");
    window.perform_layout();
    require(!(*fixture.tabs).realized_items().front().parts.tab_icon.present,
            "a tab shows no image before the control has an image list");

    std::shared_ptr<ImageList> images = std::make_shared<ImageList>(window, Size{16.0, 16.0});
    (*images).add_image("folder", folder.image);
    (*images).add_image("chart", chart.image);
    (*fixture.tabs).set_image_list(images);
    require((*fixture.tabs).image_list() == images, "the control keeps its image list");
    window.perform_layout();
    const std::vector<TabControl::Item> items = (*fixture.tabs).realized_items();
    require(items.size() == 3U && items[0].parts.tab_icon.present && !items[1].parts.tab_icon.present &&
                items[2].parts.tab_icon.present,
            "tabs with a key or an index the list has show an icon part; the others do not");
    require(items[0].parts.tab_icon.bounds.x + items[0].parts.tab_icon.bounds.width <=
                    items[0].parts.tab_label.bounds.x + 0.5 &&
                items[0].parts.tab_icon.bounds.x >= items[0].parts.tab.bounds.x,
            "the icon sits inside the tab, before its text");
    RecordingPainter painter;
    window.paint(painter, {0.0, 0.0, 480.0, 260.0});
    require(painted(painter, folder.image) && painted(painter, chart.image),
            "the tabs paint the images their key and index name");

    // The index names whatever the list holds there now.
    static_cast<void>((*images).remove_by_key("chart"));
    window.perform_layout();
    require(!(*fixture.tabs).realized_items()[2].parts.tab_icon.present,
            "an index past the list's end shows no image after the list shrinks");

    (*fixture.third).set_image_index(-1);
    (*fixture.first).set_image_key({});
    window.perform_layout();
    for (const TabControl::Item& item : (*fixture.tabs).realized_items()) {
        require(!item.parts.tab_icon.present, "clearing the key and index removes the image");
    }

    Window other_window(make_control<Panel>(StableId("tabs.other.root")), {200.0, 100.0});
    std::shared_ptr<ImageList> foreign = std::make_shared<ImageList>(other_window, Size{16.0, 16.0});
    require(throws(SetImageList{fixture.tabs.get(), foreign}) && (*fixture.tabs).image_list() == images,
            "a list of another window is refused and the old list kept");
    (*fixture.tabs).set_image_list(nullptr);
    require((*fixture.tabs).image_list() == nullptr, "the image list can be cleared");
}

} // namespace

bool painted(const RecordingPainter& painter, std::string_view text) {
    const bool found = std::find(painter.texts.begin(), painter.texts.end(), text) != painter.texts.end();
    return found;
}

// Records each owner-draw call and draws a marker into the tab's content.
class DrawTabContent final {
public:
    DrawTabContent(std::vector<std::size_t>& indices, std::vector<std::string>& identities) noexcept
        : indices_(indices), identities_(identities) {}

    void operator()(ItemContentPaint& item) const {
        indices_.push_back(item.index);
        identities_.emplace_back(item.stable_id);
        item.painter.draw_text_utf8({item.content.x, item.content.y}, "owner tab", item.font, item.text);
    }

private:
    std::vector<std::size_t>& indices_;
    std::vector<std::string>& identities_;
};

// Changes the strip's settings from inside its paint.
class ResizeTabsWhilePainting final {
public:
    ResizeTabsWhilePainting(TabControl& tabs, std::size_t& calls) noexcept : tabs_(tabs), calls_(calls) {}

    void operator()(ItemContentPaint&) const {
        ++calls_;
        tabs_.set_size_mode(TabSizeMode::fixed);
    }

private:
    TabControl& tabs_;
    std::size_t& calls_;
};

// Content-only owner drawing of the tabs (owner decision W-Q6): the
// application draws each tab's icon and label; the plate stays by role, and
// a handler that changes the strip ends that paint.
void test_owner_draw() {
    Fixture fixture = make_fixture();
    Window window(fixture.tabs, {480.0, 260.0});
    window.perform_layout();
    require((*fixture.tabs).draw_mode() == ItemDrawMode::normal, "tabs draw their own content by default");
    std::vector<std::size_t> indices;
    std::vector<std::string> identities;
    SubscriptionToken drawing =
        (*fixture.tabs).draw_item_content().subscribe(DrawTabContent(indices, identities));
    (*fixture.tabs).set_draw_mode(ItemDrawMode::owner_draw);
    window.perform_layout();
    RecordingPainter painter;
    window.paint(painter, {0.0, 0.0, 480.0, 260.0});
    require(indices == std::vector<std::size_t>{0U, 1U, 2U} && identities.size() == 3U &&
                identities[0] == "tabs.first",
            "each painted tab raises its content once, with its index and page ID");
    require(!painted(painter, "Overview") && !painted(painter, "Details") && painted(painter, "owner tab") &&
                painted(painter, "First page action"),
            "owner-drawn tabs show only what the handler drew; the page content is unaffected");
    drawing.disconnect();

    std::size_t calls = 0U;
    SubscriptionToken resizing =
        (*fixture.tabs).draw_item_content().subscribe(ResizeTabsWhilePainting(*fixture.tabs, calls));
    (*fixture.tabs).set_draw_mode(ItemDrawMode::normal);
    (*fixture.tabs).set_draw_mode(ItemDrawMode::owner_draw);
    window.perform_layout();
    RecordingPainter interrupted;
    window.paint(interrupted, {0.0, 0.0, 480.0, 260.0});
    require(calls == 1U && (*fixture.tabs).size_mode() == TabSizeMode::fixed,
            "a handler that changes the strip ends that paint at once");
    resizing.disconnect();
}

int main() {
    try {
        test_page_ownership_layout_and_visibility();
        test_keyboard_pointer_and_focus_restoration();
        test_alignment_appearance_and_semantics();
        test_page_removal_and_disposal_reconciliation();
        test_owner_draw();
        test_tab_images();
        test_selecting_events();
        std::cout << "gui_forms_tab_control_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_tab_control_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
