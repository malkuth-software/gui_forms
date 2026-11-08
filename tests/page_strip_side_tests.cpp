// PageStrip on the left and right edges (stage DK, phase P5): tabs stacked
// from the top down and touching the content's edge, each part placed along
// the label's reading direction (bottom to top on the left, top to bottom on
// the right), the seam on the content's side, overflow buttons stacked at
// the end, hit-testing on the turned tabs, labels painted turned, vertical
// tab-list semantics with Up and Down, and PageView's areas for a side strip.

#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    const bool result = std::fabs(left - right) < 0.01;
    return result;
}

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

KeyEvent key(std::uint32_t physical, Modifier modifiers = Modifier::none) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = physical;
    event.modifiers = modifiers;
    return event;
}

void click(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
}

// Adds pages "p0", "p1", ... titled "Page 0", "Page 1", ..., closable.
void add_pages(PageSet& pages, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        PageRecord record{};
        record.id = PageId{"p" + std::to_string(index)};
        record.title = "Page " + std::to_string(index);
        record.closable = true;
        const PageEditStatus status = pages.insert(record, PagePlacement::end_of_group());
        require(status == PageEditStatus::applied, "the fixture's pages are inserted");
    }
}

std::string selected_id(const PageSet& pages) {
    if (!pages.selected_page()) return {};
    return (*pages.selected_page()).value;
}

const SemanticNode* find_node(const std::vector<SemanticNode>& nodes, std::string_view id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == id) return &node;
        const SemanticNode* const inner = find_node(node.children, id);
        if (inner != nullptr) return inner;
    }
    return nullptr;
}
// Borrows from `nodes`: the pointer is valid while that vector lives.
// semantic_snapshot() returns a new snapshot, so a pointer into the roots of
// a temporary would dangle after the statement; the deleted overload refuses
// one.
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view id) = delete;

struct TakeClose final {
    std::vector<std::string>* closes{};
    void operator()(PageCloseRequest& request) const {
        (*closes).push_back(request.page.value);
        request.handled = true;
    }
};

// A strip of fixed 80-unit tabs on `placement`, in a 26 by `length` window.
std::shared_ptr<PageStrip> side_strip(PageSet& pages, PageStripPlacement placement) {
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    (*strip).placement = placement;
    (*strip).item_sizing = PageItemSizing::fixed;
    PageItemWidths widths{};
    widths.fixed = 80.0;
    (*strip).item_widths = widths;
    (*strip).close_buttons = PageCloseButtons::selected_and_hot;
    return strip;
}

// Records the orientation of every text run painted.
class TextRecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { ++level; }
    void draw_turned_text_utf8(Point, std::string_view, FontSpec, Color, TextOrientation orientation) override {
        if (orientation == TextOrientation::bottom_to_top) ++upward;
        if (orientation == TextOrientation::top_to_bottom) ++downward;
        if (orientation == TextOrientation::horizontal) ++level;
    }
    void draw_image(ImageId, Rect, double) override {}

    std::size_t level{};
    std::size_t upward{};
    std::size_t downward{};
};

// -- tests ---------------------------------------------------------------------

void geometry_per_edge() {
    const std::array<PageStripPlacement, 2> edges{PageStripPlacement::left, PageStripPlacement::right};
    for (const PageStripPlacement edge : edges) {
        const bool left = edge == PageStripPlacement::left;
        PageSet pages{};
        add_pages(pages, 4U);
        std::shared_ptr<PageStrip> strip = side_strip(pages, edge);
        const Size wanted = (*strip).measure({500.0, 400.0});
        require(near(wanted.height, 400.0) && wanted.width >= 26.0 && wanted.width < 60.0,
                "a side strip measures its thickness across and takes the height along");
        Window window(strip, {26.0, 400.0});
        window.perform_layout();
        const std::vector<PageStrip::Item> items = (*strip).realized_items();
        require(items.size() == 4U && !(*strip).overflowing(), "four tabs fit down the edge");
        for (std::size_t index = 0; index < items.size(); ++index) {
            const Rect tab = items[index].parts.tab.bounds;
            require(near(tab.y, 2.0 + 80.0 * static_cast<double>(index)) && near(tab.height, 80.0),
                    "tabs stack from the top down, each its width long");
            require(near(tab.width, 24.0) && near(tab.x, left ? 2.0 : 0.0),
                    "tabs touch the content's edge, the gap on the far side");
            const Rect label = items[index].parts.tab_label.bounds;
            require(items[index].parts.tab_label.present && near(label.width, 24.0) && label.height > 10.0 &&
                        label.y >= tab.y && label.y + label.height <= tab.y + tab.height,
                    "a turned label lies along the tab's length, across its whole width");
            if (left) {
                require(near(label.y + label.height, tab.y + tab.height - 8.0),
                        "on the left edge the label starts at the tab's bottom (reading bottom to top)");
            } else {
                require(near(label.y, tab.y + 8.0), "on the right edge the label starts at the tab's top");
            }
        }
        const PageStrip::Item first = items[0];
        require(first.parts.tab_close.present, "the selected closable tab shows its close button");
        const Rect close = first.parts.tab_close.bounds;
        const Rect tab = first.parts.tab.bounds;
        if (left) {
            require(near(close.y, tab.y + 8.0), "on the left edge the close button ends the reading, at the top");
        } else {
            require(near(close.y + close.height, tab.y + tab.height - 8.0),
                    "on the right edge the close button ends the reading, at the bottom");
        }
        const Rect label = first.parts.tab_label.bounds;
        const bool clear = close.y + close.height <= label.y + 0.01 || close.y >= label.y + label.height - 0.01;
        require(near(close.x + close.width * 0.5, tab.x + tab.width * 0.5) && clear,
                "the close button is centred across the tab and clear of the label");
        const Rect seam = first.parts.tab_seam.bounds;
        require(first.parts.tab_seam.present && near(seam.width, 1.0) && near(seam.height, tab.height) &&
                    near(seam.x, left ? tab.x + tab.width - 1.0 : tab.x),
                "the seam lies along the content's edge of the selected tab");

        window.resize({26.0, 300.0});
        for (std::size_t index = 4; index < 10U; ++index) {
            PageRecord record{};
            record.id = PageId{"p" + std::to_string(index)};
            record.title = "Page";
            require(pages.insert(record, PagePlacement::end_of_group()) == PageEditStatus::applied, "more pages");
        }
        window.perform_layout();
        const PageStrip::Parts parts = (*strip).parts();
        require((*strip).overflowing() && parts.scroll_back.present && parts.list_button.present &&
                    near(parts.list_button.bounds.y + parts.list_button.bounds.height, 298.0) &&
                    near(parts.scroll_forward.bounds.y + 16.0, parts.list_button.bounds.y) &&
                    near(parts.scroll_back.bounds.y + 16.0, parts.scroll_forward.bounds.y) &&
                    near(parts.list_button.bounds.width, 24.0),
                "an overflowing side strip stacks its buttons at the bottom end");
        const std::vector<PageStrip::Item> overflowed = (*strip).realized_items();
        require(overflowed.back().parts.tab.bounds.y + overflowed.back().parts.tab.bounds.height <=
                    parts.scroll_back.bounds.y + 0.01,
                "the tabs end before the buttons");
    }
}

void hit_testing_on_turned_tabs() {
    const std::array<PageStripPlacement, 2> edges{PageStripPlacement::left, PageStripPlacement::right};
    for (const PageStripPlacement edge : edges) {
        PageSet pages{};
        add_pages(pages, 4U);
        std::shared_ptr<PageStrip> strip = side_strip(pages, edge);
        Window window(strip, {26.0, 400.0});
        window.perform_layout();
        const std::vector<PageStrip::Item> items = (*strip).realized_items();
        for (const PageStrip::Item& item : items) {
            const Rect tab = item.parts.tab.bounds;
            const std::optional<PageId> middle = (*strip).page_at(centre(tab));
            const std::optional<PageId> top = (*strip).page_at({tab.x + tab.width * 0.5, tab.y + 1.0});
            const std::optional<PageId> bottom = (*strip).page_at({tab.x + tab.width * 0.5, tab.y + tab.height - 1.0});
            require(middle && top && bottom && *middle == item.page && *top == item.page && *bottom == item.page,
                    "every point along a turned tab hits its page");
        }
        require(!(*strip).page_at({13.0, 390.0}), "the space after the last tab hits nothing");
        click(window, centre(items[2].parts.tab.bounds));
        require(selected_id(pages) == "p2", "a click on a turned tab selects its page");
        std::vector<std::string> closes;
        SubscriptionToken closing = (*strip).page_close_requested().subscribe(TakeClose{&closes});
        const PageStrip::Item chosen = (*strip).realized_items()[2];
        require(chosen.parts.tab_close.present, "the selected turned tab shows its close button");
        click(window, centre(chosen.parts.tab_close.bounds));
        require(closes.size() == 1U && closes[0] == "p2" && selected_id(pages) == "p2",
                "the close button of a turned tab is hit and asks to close");
    }
}

void turned_labels_and_semantics() {
    const std::array<PageStripPlacement, 3> edges{PageStripPlacement::left, PageStripPlacement::right,
                                                  PageStripPlacement::top};
    for (const PageStripPlacement edge : edges) {
        PageSet pages{};
        add_pages(pages, 3U);
        std::shared_ptr<PageStrip> strip = side_strip(pages, edge);
        const bool side = edge != PageStripPlacement::top;
        Window window(strip, side ? Size{26.0, 400.0} : Size{400.0, 26.0});
        window.perform_layout();
        TextRecordingPainter painter{};
        (*strip).on_paint(painter, (*strip).client_rectangle());
        if (edge == PageStripPlacement::left) {
            require(painter.upward == 3U && painter.downward == 0U && painter.level == 0U,
                    "labels on the left edge read from bottom to top");
        } else if (edge == PageStripPlacement::right) {
            require(painter.downward == 3U && painter.upward == 0U && painter.level == 0U,
                    "labels on the right edge read from top to bottom");
        } else {
            require(painter.level == 3U && painter.upward == 0U && painter.downward == 0U,
                    "labels on a top strip are not turned");
        }
        const SemanticSnapshot list_snapshot = window.semantic_snapshot();
        const SemanticNode* const list = find_node(list_snapshot.roots, "strip");
        require(list != nullptr && (*list).orientation &&
                    *(*list).orientation == (side ? SemanticOrientation::vertical : SemanticOrientation::horizontal),
                "the tab list's orientation follows the edge");
        require(window.request_focus(strip), "the strip takes focus");
        const std::uint32_t next = side ? PhysicalKey::down : PhysicalKey::right;
        const std::uint32_t across = side ? PhysicalKey::right : PhysicalKey::down;
        require(window.dispatch_key(key(next)) && selected_id(pages) == "p1", "the key along the row selects the next");
        static_cast<void>(window.dispatch_key(key(across)));
        require(selected_id(pages) == "p1", "the key across the row does not move");
        const std::uint32_t previous = side ? PhysicalKey::up : PhysicalKey::left;
        require(window.dispatch_key(key(previous)) && selected_id(pages) == "p0", "the key back selects the previous");
    }
}

void page_view_side_areas() {
    const std::array<PageStripPlacement, 2> edges{PageStripPlacement::left, PageStripPlacement::right};
    for (const PageStripPlacement edge : edges) {
        std::shared_ptr<PageView> view = make_control<PageView>(StableId("view"));
        std::shared_ptr<Label> body = make_control<Label>(StableId("view.body"), "Body");
        PageRecord record{};
        record.id = PageId{"only"};
        record.title = "Only";
        require((*view).add_page(record, body) == PageEditStatus::applied, "a page is added");
        (*(*view).strip()).placement = edge;
        Window window(view, {400.0, 200.0});
        window.perform_layout();
        const PageView::Parts parts = (*view).parts();
        const Rect strip = parts.strip.bounds;
        const Rect content = parts.content.bounds;
        require(near(strip.height, 200.0) && strip.width >= 26.0 && strip.width < 60.0 && near(content.height, 200.0) &&
                    near(strip.width + content.width, 400.0),
                "a side strip takes a column the height of the view, the content the rest");
        if (edge == PageStripPlacement::left) {
            require(near(strip.x, 0.0) && near(content.x, strip.width), "a left strip sits before the content");
        } else {
            require(near(content.x, 0.0) && near(strip.x, content.width), "a right strip sits after the content");
        }
        require((*(*view).strip()).absolute_bounds() == strip, "the strip control is arranged in its column");
    }
}

} // namespace

int main() {
    geometry_per_edge();
    hit_testing_on_turned_tabs();
    turned_labels_and_semantics();
    page_view_side_areas();
    std::cout << "PageStrip side strip tests passed\n";
    return 0;
}
