#include "demo_parts.hpp"
#include "gui_forms/controls/card.hpp"
#include "gui_forms/controls/choice_group.hpp"
#include "gui_forms/controls/drawing_surface.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/track_bar.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/xml/types/xml_types.hpp"
#include "gui_forms/xml/xml_reader/xml_reader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The dashboard-pieces page: five DevExpress dashboard takes the survey
// rates "demo only", composed from stock parts with no new API.
// - Widget view: Cards in three layouts (table, rows, columns) picked by a
//   ChoiceGroup; activating a card maximizes it and again restores.
// - Treemap: a squarified treemap of a disk-usage fixture on a
//   DrawingSurface; the pointer names the block under it.
// - Bar gauge: concentric arcs drawn as polygons, one per TrackBar.
// - Card view: records shown as Cards, sorted and paged by the page.
// - HTML-template cards: the card view's layout comes from a small markup
//   template, read with read_xml and edited live; {field} names a record's
//   field. Only the elements title, line, note and badge are known, so a
//   template cannot reach past Labels.
namespace gui_forms::gallery::pages {
namespace {

constexpr double pi = 3.141592653589793;

// -- treemap ----------------------------------------------------------------
struct UsageEntry final {
    std::string_view folder{};
    std::string_view name{};
    double gigabytes{};
};

constexpr std::array<UsageEntry, 16> usage{{
    {"Photos", "2026", 18.2}, {"Photos", "2025", 14.6}, {"Photos", "Scans", 4.1},
    {"Video", "Holiday", 22.4}, {"Video", "Talks", 9.8},
    {"Projects", "Gallery", 6.3}, {"Projects", "Plan Paint", 4.9}, {"Projects", "STXlib", 2.2},
    {"Projects", "Notes", 0.9}, {"Music", "Albums", 11.5}, {"Music", "Podcasts", 3.4},
    {"System", "Caches", 7.7}, {"System", "Logs", 1.6}, {"Mail", "Archive", 5.2},
    {"Mail", "Inbox", 1.1}, {"Mail", "Attachments", 3.0},
}};

constexpr std::array<std::string_view, 6> usage_folders{"Photos", "Video", "Projects", "Music", "System", "Mail"};
constexpr std::array<Color, 6> folder_colours{Color::rgba(39, 116, 184), Color::rgba(116, 82, 153),
                                              Color::rgba(49, 139, 94),  Color::rgba(211, 121, 42),
                                              Color::rgba(91, 107, 121), Color::rgba(180, 70, 90)};

// The worst aspect ratio of `row` laid along a side of length `side`.
[[nodiscard]] double worst_ratio(const std::vector<double>& row, double side) {
    double sum = 0.0;
    double largest = 0.0;
    double smallest = 0.0;
    for (const double value : row) {
        sum += value;
        largest = std::max(largest, value);
        smallest = smallest == 0.0 ? value : std::min(smallest, value);
    }
    if (sum <= 0.0 || smallest <= 0.0) return 1e300;
    const double side_squared = side * side;
    const double sum_squared = sum * sum;
    const double worst = std::max(side_squared * largest / sum_squared, sum_squared / (side_squared * smallest));
    return worst;
}

// Squarified treemap (Bruls, Huizing and van Wijk): `values`, largest
// first, as areas, laid into `area` whose area equals their sum. One
// rectangle per value, in the same order.
[[nodiscard]] std::vector<Rect> squarify(const std::vector<double>& values, Rect area) {
    std::vector<Rect> result{};
    std::vector<double> row{};
    std::size_t next = 0;
    Rect free = area;
    while (next < values.size()) {
        const double side = std::min(free.width, free.height);
        std::vector<double> extended = row;
        extended.push_back(values[next]);
        if (row.empty() || worst_ratio(extended, side) <= worst_ratio(row, side)) {
            row = std::move(extended);
            ++next;
            if (next < values.size()) continue;
        }
        // Lay the row along the shorter side of what is free.
        double sum = 0.0;
        for (const double value : row) sum += value;
        const bool along_width = free.width >= free.height;
        const double thickness = side > 0.0 ? sum / side : 0.0;
        double cursor = along_width ? free.y : free.x;
        for (const double value : row) {
            const double length = thickness > 0.0 ? value / thickness : 0.0;
            if (along_width) {
                result.push_back(Rect{free.x, cursor, thickness, length});
            } else {
                result.push_back(Rect{cursor, free.y, length, thickness});
            }
            cursor += length;
        }
        if (along_width) {
            free = Rect{free.x + thickness, free.y, std::max(0.0, free.width - thickness), free.height};
        } else {
            free = Rect{free.x, free.y + thickness, free.width, std::max(0.0, free.height - thickness)};
        }
        row.clear();
    }
    return result;
}

struct UsageEntryLarger final {
    bool operator()(std::size_t left, std::size_t right) const { return usage[right].gigabytes < usage[left].gigabytes; }
};

struct FolderTotal final {
    std::size_t folder{};
    double gigabytes{};
};

struct FolderTotalLarger final {
    bool operator()(const FolderTotal& left, const FolderTotal& right) const { return right.gigabytes < left.gigabytes; }
};

// The treemap in unit coordinates of a `aspect`-wide, 1-high area: each
// usage entry's block (indexed like `usage`), and each folder's.
struct TreemapLayout final {
    std::array<Rect, usage.size()> blocks{};
    std::array<Rect, usage_folders.size()> folders{};
    int hot{-1};
};

[[nodiscard]] TreemapLayout layout_treemap(double aspect) {
    TreemapLayout layout{};
    std::vector<FolderTotal> totals{};
    double whole = 0.0;
    for (std::size_t folder = 0; folder < usage_folders.size(); ++folder) {
        FolderTotal total{folder, 0.0};
        for (const UsageEntry& entry : usage) {
            if (entry.folder == usage_folders[folder]) total.gigabytes += entry.gigabytes;
        }
        whole += total.gigabytes;
        totals.push_back(total);
    }
    std::stable_sort(totals.begin(), totals.end(), FolderTotalLarger{});
    const double scale = aspect / whole;
    std::vector<double> folder_areas{};
    for (const FolderTotal& total : totals) folder_areas.push_back(total.gigabytes * scale);
    const std::vector<Rect> folder_rects = squarify(folder_areas, Rect{0.0, 0.0, aspect, 1.0});
    for (std::size_t at = 0; at < totals.size(); ++at) {
        const Rect folder_rect = folder_rects[at];
        layout.folders[totals[at].folder] = folder_rect;
        std::vector<std::size_t> members{};
        for (std::size_t entry = 0; entry < usage.size(); ++entry) {
            if (usage[entry].folder == usage_folders[totals[at].folder]) members.push_back(entry);
        }
        std::stable_sort(members.begin(), members.end(), UsageEntryLarger{});
        const double folder_scale =
            totals[at].gigabytes > 0.0 ? folder_rect.width * folder_rect.height / totals[at].gigabytes : 0.0;
        std::vector<double> areas{};
        for (const std::size_t member : members) areas.push_back(usage[member].gigabytes * folder_scale);
        const std::vector<Rect> member_rects = squarify(areas, folder_rect);
        for (std::size_t member = 0; member < members.size(); ++member) {
            layout.blocks[members[member]] = member_rects[member];
        }
    }
    return layout;
}

[[nodiscard]] Rect scaled(Rect unit, Rect bounds, double aspect) {
    const double x_scale = bounds.width / aspect;
    const Rect result{bounds.x + unit.x * x_scale, bounds.y + unit.y * bounds.height, unit.width * x_scale,
                      unit.height * bounds.height};
    return result;
}

constexpr double treemap_aspect = 296.0 / 186.0;

class TreemapPaint final {
public:
    explicit TreemapPaint(std::shared_ptr<const TreemapLayout> layout) : layout_(std::move(layout)) {}

    void operator()(Painter& painter, Rect bounds, Rect damage) const {
        static_cast<void>(damage);
        const FontSpec font{FontRole::content, 9.0, 400, false};
        for (std::size_t entry = 0; entry < usage.size(); ++entry) {
            std::size_t folder = 0;
            for (std::size_t at = 0; at < usage_folders.size(); ++at) {
                if (usage_folders[at] == usage[entry].folder) folder = at;
            }
            const Rect block = scaled((*layout_).blocks[entry], bounds, treemap_aspect);
            const bool hot = static_cast<int>(entry) == (*layout_).hot;
            const Color base = folder_colours[folder];
            const Color fill = hot ? Color::rgba(250, 196, 80)
                                   : Color::rgba(static_cast<std::uint8_t>(base.red + (255 - base.red) * (entry % 3U) / 6U),
                                                 static_cast<std::uint8_t>(base.green + (255 - base.green) * (entry % 3U) / 6U),
                                                 static_cast<std::uint8_t>(base.blue + (255 - base.blue) * (entry % 3U) / 6U));
            painter.fill_rect(block, fill);
            painter.stroke_rect(block, Color::rgba(255, 255, 255), 1.0);
            if (block.width > 44.0 && block.height > 16.0) {
                painter.save();
                painter.clip_rect(block);
                painter.draw_text_utf8({block.x + 4.0, block.y + 12.0}, usage[entry].name, font,
                                       hot ? Color::rgba(28, 39, 50) : Color::rgba(255, 255, 255));
                painter.restore();
            }
        }
        for (const Rect& folder : (*layout_).folders) {
            painter.stroke_rect(scaled(folder, bounds, treemap_aspect), Color::rgba(28, 39, 50), 1.5);
        }
    }

private:
    std::shared_ptr<const TreemapLayout> layout_;
};

// -- bar gauge ----------------------------------------------------------------
constexpr std::array<std::string_view, 3> gauge_names{"CPU", "Memory", "Disk"};
constexpr std::array<Color, 3> gauge_colours{Color::rgba(39, 116, 184), Color::rgba(49, 139, 94),
                                             Color::rgba(211, 121, 42)};

struct GaugeState final {
    std::array<double, 3> values{62.0, 41.0, 78.0};
};

// An arc band from `start` sweeping `sweep` radians, between two radii.
void fill_arc(Painter& painter, Point centre, double inner, double outer, double start, double sweep, Color colour) {
    if (sweep <= 0.0) return;
    constexpr std::size_t steps = 32U;
    std::array<Point, (steps + 1U) * 2U> outline{};
    for (std::size_t step = 0; step <= steps; ++step) {
        const double angle = start + sweep * static_cast<double>(step) / steps;
        outline[step] = Point{centre.x + outer * std::cos(angle), centre.y + outer * std::sin(angle)};
        const double back = start + sweep - sweep * static_cast<double>(step) / steps;
        outline[steps + 1U + step] = Point{centre.x + inner * std::cos(back), centre.y + inner * std::sin(back)};
    }
    painter.fill_polygon(outline, colour, PolygonFillRule::nonzero);
}

class GaugePaint final {
public:
    explicit GaugePaint(std::shared_ptr<const GaugeState> state) : state_(std::move(state)) {}

    void operator()(Painter& painter, Rect bounds, Rect damage) const {
        static_cast<void>(damage);
        const Point centre{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
        const double outer = std::min(bounds.width, bounds.height) * 0.5 - 4.0;
        const double band = outer * 0.2;
        // A 270-degree sweep from lower left, clockwise.
        const double start = pi * 0.75;
        const double full = pi * 1.5;
        for (std::size_t ring = 0; ring < gauge_names.size(); ++ring) {
            const double ring_outer = outer - static_cast<double>(ring) * (band + 4.0);
            const double ring_inner = ring_outer - band;
            fill_arc(painter, centre, ring_inner, ring_outer, start, full, Color::rgba(226, 231, 237));
            const double amount = std::clamp((*state_).values[ring] / 100.0, 0.0, 1.0);
            fill_arc(painter, centre, ring_inner, ring_outer, start, full * amount, gauge_colours[ring]);
        }
    }

private:
    std::shared_ptr<const GaugeState> state_;
};

// -- records (the card view) --------------------------------------------------
struct Person final {
    std::string_view name{};
    std::string_view role{};
    std::string_view city{};
    std::string_view since{};
    std::string_view status{};
};

constexpr std::array<Person, 12> people{{
    {"Ada Brennan", "Engineer", "Leeds", "2019", "Available"},
    {"Bao Tran", "Designer", "Porto", "2021", "In a meeting"},
    {"Carla Mendes", "Engineer", "Porto", "2017", "Away"},
    {"Dmitri Vass", "Support", "Tallinn", "2022", "Available"},
    {"Elin Strand", "Manager", "Ghent", "2015", "Busy"},
    {"Farid Haddad", "Engineer", "Ghent", "2020", "Available"},
    {"Greta Holm", "Analyst", "Tallinn", "2018", "On leave"},
    {"Hugo Laurent", "Designer", "Leeds", "2023", "Available"},
    {"Ines Duarte", "Support", "Porto", "2016", "Busy"},
    {"Jonas Berg", "Engineer", "Tallinn", "2024", "Away"},
    {"Kofi Mensah", "Manager", "Leeds", "2014", "Available"},
    {"Lena Fischer", "Analyst", "Ghent", "2021", "In a meeting"},
}};

[[nodiscard]] std::string person_field(const Person& person, std::string_view field) {
    if (field == "name") return std::string(person.name);
    if (field == "role") return std::string(person.role);
    if (field == "city") return std::string(person.city);
    if (field == "since") return std::string(person.since);
    if (field == "status") return std::string(person.status);
    return "{" + std::string(field) + "}";
}

// `text` with each {field} replaced by the person's field; an unknown
// field stays as written, so a typo shows on the card.
[[nodiscard]] std::string fill_template(std::string_view text, const Person& person) {
    std::string result{};
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t open = text.find('{', at);
        if (open == std::string_view::npos) break;
        const std::size_t close = text.find('}', open + 1U);
        if (close == std::string_view::npos) break;
        result += text.substr(at, open - at);
        result += person_field(person, text.substr(open + 1U, close - open - 1U));
        at = close + 1U;
    }
    result += text.substr(at);
    return result;
}

struct PersonOrder final {
    bool by_city{};

    bool operator()(std::size_t left, std::size_t right) const {
        if (by_city && people[left].city != people[right].city) return people[left].city < people[right].city;
        return people[left].name < people[right].name;
    }
};

constexpr std::string_view default_template =
    "<card>\n"
    "  <title>{name}</title>\n"
    "  <line>{role}</line>\n"
    "  <note>{city} since {since}</note>\n"
    "  <badge>{status}</badge>\n"
    "</card>";

// One element of a parsed template: what it shows and how.
enum class TemplatePart : std::uint8_t { title, line, note, badge };

struct TemplateLine final {
    TemplatePart part{TemplatePart::line};
    std::string text{};
};

constexpr std::size_t cards_per_page = 6U;
constexpr double card_width = 184.0;
constexpr double card_height = 92.0;

class DashboardPiecesPage final : public gui_forms::Component {
public:
    explicit DashboardPiecesPage(const std::shared_ptr<Surface>& body) : body_(body) {}

    void build() {
        Surface& body = *body_;
        add_page_heading(body, "dashboard", "DASHBOARD PIECES",
                         "A widget view, a treemap, a bar gauge and a card view whose cards come from an editable "
                         "markup template, all composed from stock parts.");
        build_widgets(body);
        build_treemap(body);
        build_gauge(body);
        build_card_view(body);
        build_template(body);
        status_ = label("gallery.dashboard.status", "Point at the treemap or activate a widget.", 11.0, 400, "muted");
        body.add_at(status_, {24.0, 640.0, 940.0, 22.0});
        static_cast<void>(apply_template());
        show_cards();
    }

    void connect() {
        on((*widget_layout_).changed(), *this, &DashboardPiecesPage::on_widget_layout);
        for (std::size_t widget = 0; widget < widgets_.size(); ++widget) {
            on((*widgets_[widget]).activated(), *this, &DashboardPiecesPage::on_widget_activated, widget);
        }
        on((*treemap_).mouse_move(), *this, &DashboardPiecesPage::on_treemap_move);
        on((*treemap_).mouse_leave(), *this, &DashboardPiecesPage::on_treemap_leave);
        for (std::size_t ring = 0; ring < gauge_tracks_.size(); ++ring) {
            on((*gauge_tracks_[ring]).value_changed(), *this, &DashboardPiecesPage::on_gauge_value, ring);
        }
        on((*card_sort_).changed(), *this, &DashboardPiecesPage::on_card_sort);
        on((*card_previous_).clicked(), *this, &DashboardPiecesPage::on_card_previous);
        on((*card_next_).clicked(), *this, &DashboardPiecesPage::on_card_next);
        on((*template_apply_).clicked(), *this, &DashboardPiecesPage::on_template_apply);
        on((*template_reset_).clicked(), *this, &DashboardPiecesPage::on_template_reset);
    }

private:
    // -- widget view ----------------------------------------------------------
    void build_widgets(Surface& body) {
        widget_group_ = group("gallery.dashboard.widgets", "Widget view", {320.0, 260.0});
        body.add_at(widget_group_, {24.0, 92.0, 320.0, 260.0});
        widget_layout_ = make_control<ChoiceGroup>(StableId("gallery.dashboard.widgets.layout"));
        (*widget_layout_).accessible_name = "Widget layout";
        (*widget_layout_).set_items({ChoiceItem{0, "Table"}, ChoiceItem{1, "Rows"}, ChoiceItem{2, "Columns"}});
        (*widget_layout_).selected_index = 0;
        (*widget_group_).add_at(widget_layout_, {12.0, 28.0, 296.0, 26.0});
        constexpr std::array<std::string_view, 4> titles{"Revenue", "Orders", "Visitors", "Conversion"};
        constexpr std::array<std::string_view, 4> values{"€48.2k", "1,284", "19,450", "3.4%"};
        for (std::size_t widget = 0; widget < widgets_.size(); ++widget) {
            const std::string id = "gallery.dashboard.widgets.card." + std::to_string(widget);
            std::shared_ptr<Card> card = make_control<Card>(StableId(id));
            (*card).interactive = true;
            (*card).accessible_name = std::string(titles[widget]);
            std::shared_ptr<Label> title = label(id + ".title", std::string(titles[widget]), 10.0, 600, "muted");
            (*title).set_requested_bounds({8.0, 6.0, 120.0, 16.0});
            (*card).add_child(title);
            std::shared_ptr<Label> value = label(id + ".value", std::string(values[widget]), 18.0, 700, "ink");
            (*value).set_requested_bounds({8.0, 24.0, 130.0, 28.0});
            (*card).add_child(value);
            (*widget_group_).add_at(card, {0.0, 0.0, 1.0, 1.0});
            widgets_[widget] = card;
        }
        place_widgets();
    }

    // Lays the cards out for the chosen layout, or one card over the whole
    // area while it is maximized.
    void place_widgets() {
        const Rect area{12.0, 62.0, 296.0, 186.0};
        const int layout = (*widget_layout_).selected_index.read();
        for (std::size_t widget = 0; widget < widgets_.size(); ++widget) {
            Card& card = *widgets_[widget];
            if (maximized_ >= 0) {
                const bool shown = static_cast<int>(widget) == maximized_;
                card.visible = shown;
                if (shown) (*widget_group_).set_design_bounds(card, area);
                continue;
            }
            card.visible = true;
            const double index = static_cast<double>(widget);
            Rect slot{};
            if (layout == 1) {
                slot = Rect{area.x, area.y + index * (area.height / 4.0), area.width, area.height / 4.0 - 6.0};
            } else if (layout == 2) {
                slot = Rect{area.x + index * (area.width / 4.0), area.y, area.width / 4.0 - 6.0, area.height};
            } else {
                const double column = static_cast<double>(widget % 2U);
                const double row = static_cast<double>(widget / 2U);
                slot = Rect{area.x + column * (area.width / 2.0), area.y + row * (area.height / 2.0),
                            area.width / 2.0 - 6.0, area.height / 2.0 - 6.0};
            }
            (*widget_group_).set_design_bounds(card, slot);
        }
    }

    void on_widget_layout(int index) {
        static_cast<void>(index);
        maximized_ = -1;
        place_widgets();
    }

    void on_widget_activated(std::size_t widget) {
        maximized_ = maximized_ == static_cast<int>(widget) ? -1 : static_cast<int>(widget);
        place_widgets();
        report(maximized_ >= 0 ? (*widgets_[widget]).accessible_name.read() + " maximized; activate again to restore"
                               : std::string("Widgets restored"));
    }

    // -- treemap --------------------------------------------------------------
    void build_treemap(Surface& body) {
        const std::shared_ptr<LayoutGroup> box = group("gallery.dashboard.treemap", "Treemap", {320.0, 260.0});
        body.add_at(box, {356.0, 92.0, 320.0, 260.0});
        treemap_layout_ = std::make_shared<TreemapLayout>(layout_treemap(treemap_aspect));
        treemap_ = make_control<DrawingSurface>(StableId("gallery.dashboard.treemap.view"));
        (*treemap_).accessible_name = "Disk usage treemap";
        (*treemap_).hit_test_visible = true;
        (*treemap_).set_paint_callback(TreemapPaint(treemap_layout_));
        (*box).add_at(treemap_, {12.0, 30.0, 296.0, 186.0});
        treemap_label_ = label("gallery.dashboard.treemap.value", "Point at a block", 11.0, 400, "ink");
        (*box).add_at(treemap_label_, {12.0, 224.0, 296.0, 22.0});
    }

    void on_treemap_move(const MouseEventArgs& arguments) {
        const Rect client = (*treemap_).client_rectangle();
        const Rect bounds{0.0, 0.0, client.width, client.height};
        int hot = -1;
        for (std::size_t entry = 0; entry < usage.size(); ++entry) {
            const Rect block = scaled((*treemap_layout_).blocks[entry], bounds, treemap_aspect);
            const bool inside = arguments.location.x >= block.x && arguments.location.x < block.x + block.width &&
                                arguments.location.y >= block.y && arguments.location.y < block.y + block.height;
            if (inside) hot = static_cast<int>(entry);
        }
        if (hot == (*treemap_layout_).hot) return;
        (*treemap_layout_).hot = hot;
        (*treemap_).invalidate(Dirty::paint);
        if (hot < 0) return;
        const UsageEntry& entry = usage[static_cast<std::size_t>(hot)];
        const long tenths = std::lround(entry.gigabytes * 10.0);
        (*treemap_label_).text = std::string(entry.folder) + " / " + std::string(entry.name) + "   " +
                                 std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " GB";
    }

    void on_treemap_leave() {
        (*treemap_layout_).hot = -1;
        (*treemap_).invalidate(Dirty::paint);
    }

    // -- bar gauge ------------------------------------------------------------
    void build_gauge(Surface& body) {
        const std::shared_ptr<LayoutGroup> box = group("gallery.dashboard.gauge", "Bar gauge", {288.0, 260.0});
        body.add_at(box, {688.0, 92.0, 288.0, 260.0});
        gauge_state_ = std::make_shared<GaugeState>();
        gauge_ = make_control<DrawingSurface>(StableId("gallery.dashboard.gauge.view"));
        (*gauge_).semantic_role = SemanticRole::meter;
        (*gauge_).set_paint_callback(GaugePaint(gauge_state_));
        (*box).add_at(gauge_, {12.0, 30.0, 130.0, 130.0});
        for (std::size_t ring = 0; ring < gauge_tracks_.size(); ++ring) {
            const std::string id = "gallery.dashboard.gauge." + ascii_lower(gauge_names[ring]);
            const double top = 30.0 + 72.0 * static_cast<double>(ring);
            gauge_labels_[ring] = label(id + ".label", "", 10.0, 600, "ink");
            (*box).add_at(gauge_labels_[ring], {150.0, top, 126.0, 18.0});
            std::shared_ptr<TrackBar> track = make_control<TrackBar>(StableId(id));
            (*track).accessible_name = std::string(gauge_names[ring]);
            (*track).set_range(0.0, 100.0);
            (*track).value = (*gauge_state_).values[ring];
            (*track).show_ticks = false;
            (*box).add_at(track, {150.0, top + 20.0, 126.0, 26.0});
            gauge_tracks_[ring] = track;
        }
        show_gauge();
    }

    void on_gauge_value(std::size_t ring, double value) {
        (*gauge_state_).values[ring] = value;
        show_gauge();
    }

    void show_gauge() {
        std::string name = "Bar gauge:";
        for (std::size_t ring = 0; ring < gauge_names.size(); ++ring) {
            const std::string percent = std::to_string(static_cast<int>(std::lround((*gauge_state_).values[ring])));
            (*gauge_labels_[ring]).text = std::string(gauge_names[ring]) + "  " + percent + "%";
            name += " " + std::string(gauge_names[ring]) + " " + percent + "%";
        }
        (*gauge_).accessible_name = name;
        (*gauge_).invalidate(Dirty::paint);
    }

    // -- card view ------------------------------------------------------------
    void build_card_view(Surface& body) {
        cards_group_ = group("gallery.dashboard.cards", "Card view", {600.0, 260.0});
        body.add_at(cards_group_, {24.0, 368.0, 600.0, 260.0});
        card_sort_ = make_control<ChoiceGroup>(StableId("gallery.dashboard.cards.sort"));
        (*card_sort_).accessible_name = "Sort cards by";
        (*card_sort_).set_items({ChoiceItem{0, "By name"}, ChoiceItem{1, "By city"}});
        (*card_sort_).selected_index = 0;
        (*cards_group_).add_at(card_sort_, {12.0, 28.0, 200.0, 26.0});
        card_previous_ = make_control<Button>(StableId("gallery.dashboard.cards.previous"), "Previous");
        (*cards_group_).add_at(card_previous_, {372.0, 28.0, 100.0, 26.0});
        card_next_ = make_control<Button>(StableId("gallery.dashboard.cards.next"), "Next");
        (*cards_group_).add_at(card_next_, {480.0, 28.0, 100.0, 26.0});
        card_page_label_ = label("gallery.dashboard.cards.page", "", 10.0, 400, "muted");
        (*cards_group_).add_at(card_page_label_, {224.0, 30.0, 140.0, 22.0});
        for (std::size_t person = 0; person < people.size(); ++person) card_order_.push_back(person);
    }

    void on_card_sort(int index) {
        const PersonOrder order{index == 1};
        std::stable_sort(card_order_.begin(), card_order_.end(), order);
        card_page_ = 0U;
        show_cards();
    }

    void on_card_previous() {
        if (card_page_ == 0U) return;
        --card_page_;
        show_cards();
    }

    void on_card_next() {
        if ((card_page_ + 1U) * cards_per_page >= people.size()) return;
        ++card_page_;
        show_cards();
    }

    // Rebuilds the shown page's cards from the template.
    void show_cards() {
        for (const std::shared_ptr<Card>& card : shown_cards_) {
            static_cast<void>((*cards_group_).remove_child((*card).runtime_id()));
        }
        shown_cards_.clear();
        const std::size_t first = card_page_ * cards_per_page;
        for (std::size_t slot = 0; slot < cards_per_page && first + slot < card_order_.size(); ++slot) {
            const Person& person = people[card_order_[first + slot]];
            std::shared_ptr<Card> card = make_card(person, first + slot);
            const double column = static_cast<double>(slot % 3U);
            const double row = static_cast<double>(slot / 3U);
            (*cards_group_).add_at(card, {12.0 + column * (card_width + 8.0), 62.0 + row * (card_height + 8.0),
                                          card_width, card_height});
            shown_cards_.push_back(card);
        }
        const std::size_t pages = (people.size() + cards_per_page - 1U) / cards_per_page;
        (*card_page_label_).text = "Page " + std::to_string(card_page_ + 1U) + " of " + std::to_string(pages);
        (*card_previous_).enabled = card_page_ > 0U;
        (*card_next_).enabled = card_page_ + 1U < pages;
    }

    [[nodiscard]] std::shared_ptr<Card> make_card(const Person& person, std::size_t index) const {
        const std::string id = "gallery.dashboard.cards.card." + std::to_string(index);
        std::shared_ptr<Card> card = make_control<Card>(StableId(id));
        (*card).accessible_name = std::string(person.name);
        double top = 6.0;
        std::size_t line = 0;
        for (const TemplateLine& part : template_lines_) {
            const std::string text = fill_template(part.text, person);
            const std::string line_id = id + "." + std::to_string(line);
            ++line;
            std::shared_ptr<Label> shown;
            double height = 16.0;
            if (part.part == TemplatePart::title) {
                shown = label(line_id, text, 12.0, 700, "heading");
                height = 18.0;
            } else if (part.part == TemplatePart::note) {
                shown = label(line_id, text, 9.0, 400, "muted");
            } else if (part.part == TemplatePart::badge) {
                shown = label(line_id, "● " + text, 9.0, 600, "ink");
            } else {
                shown = label(line_id, text, 10.0, 400, "ink");
            }
            (*shown).set_requested_bounds({8.0, top, card_width - 16.0, height});
            (*card).add_child(shown);
            top += height + 2.0;
        }
        return card;
    }

    // -- template -------------------------------------------------------------
    void build_template(Surface& body) {
        const std::shared_ptr<LayoutGroup> box =
            group("gallery.dashboard.template", "HTML-template cards", {336.0, 260.0});
        body.add_at(box, {640.0, 368.0, 336.0, 260.0});
        template_editor_ = make_control<TextBox>(StableId("gallery.dashboard.template.markup"));
        (*template_editor_).accessible_name = "Card template";
        (*template_editor_).multiline = true;
        (*template_editor_).accepts_enter = true;
        (*template_editor_).text = std::string(default_template);
        (*box).add_at(template_editor_, {12.0, 28.0, 312.0, 150.0});
        template_apply_ = make_control<Button>(StableId("gallery.dashboard.template.apply"), "Apply template");
        (*box).add_at(template_apply_, {12.0, 184.0, 150.0, 26.0});
        template_reset_ = make_control<Button>(StableId("gallery.dashboard.template.reset"), "Reset");
        (*box).add_at(template_reset_, {174.0, 184.0, 150.0, 26.0});
        template_status_ = wrapped_label("gallery.dashboard.template.status",
                                 "Elements: title, line, note, badge; {name} {role} {city} {since} {status}.", 9.0,
                                 400, "muted");
        (*box).add_at(template_status_, {12.0, 214.0, 312.0, 34.0});
    }

    void on_template_apply() {
        if (!apply_template()) return;
        show_cards();
        report("Template applied to the card view");
    }

    void on_template_reset() {
        (*template_editor_).text = std::string(default_template);
        on_template_apply();
    }

    // Reads the editor's template into template_lines_; false, keeping the
    // last good template, with the reason in the template status.
    bool apply_template() {
        const XmlReadResult read = read_xml((*template_editor_).text.read());
        if (!read) {
            (*template_status_).text = "Not applied: " + std::string(xml_error_name(read.error)) + " at line " +
                                       std::to_string(read.position.line) + ", column " +
                                       std::to_string(read.position.column);
            return false;
        }
        if (read.root.name != "card") {
            (*template_status_).text = "Not applied: the root must be <card>, not <" + read.root.name + ">";
            return false;
        }
        std::vector<TemplateLine> lines{};
        for (const XmlElement& child : read.root.children) {
            TemplateLine line{};
            if (child.name == "title") {
                line.part = TemplatePart::title;
            } else if (child.name == "line") {
                line.part = TemplatePart::line;
            } else if (child.name == "note") {
                line.part = TemplatePart::note;
            } else if (child.name == "badge") {
                line.part = TemplatePart::badge;
            } else {
                (*template_status_).text = "Not applied: unknown element <" + child.name + ">";
                return false;
            }
            line.text = child.text;
            lines.push_back(std::move(line));
        }
        if (lines.empty() || lines.size() > 5U) {
            (*template_status_).text = "Not applied: a card holds one to five elements";
            return false;
        }
        template_lines_ = std::move(lines);
        (*template_status_).text = "Applied: " + std::to_string(template_lines_.size()) + " elements per card.";
        return true;
    }

    void report(std::string text) { (*status_).text = std::move(text); }

    std::shared_ptr<Surface> body_;
    std::shared_ptr<Label> status_;

    std::shared_ptr<LayoutGroup> widget_group_;
    std::shared_ptr<ChoiceGroup> widget_layout_;
    std::array<std::shared_ptr<Card>, 4> widgets_{};
    int maximized_{-1};

    std::shared_ptr<TreemapLayout> treemap_layout_;
    std::shared_ptr<DrawingSurface> treemap_;
    std::shared_ptr<Label> treemap_label_;

    std::shared_ptr<GaugeState> gauge_state_;
    std::shared_ptr<DrawingSurface> gauge_;
    std::array<std::shared_ptr<TrackBar>, 3> gauge_tracks_{};
    std::array<std::shared_ptr<Label>, 3> gauge_labels_{};

    std::shared_ptr<LayoutGroup> cards_group_;
    std::shared_ptr<ChoiceGroup> card_sort_;
    std::shared_ptr<Button> card_previous_;
    std::shared_ptr<Button> card_next_;
    std::shared_ptr<Label> card_page_label_;
    std::vector<std::size_t> card_order_{};
    std::size_t card_page_{};
    std::vector<std::shared_ptr<Card>> shown_cards_{};

    std::shared_ptr<TextBox> template_editor_;
    std::shared_ptr<Button> template_apply_;
    std::shared_ptr<Button> template_reset_;
    std::shared_ptr<Label> template_status_;
    std::vector<TemplateLine> template_lines_{};
};

} // namespace

void add_dashboard_pieces(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    const std::shared_ptr<DashboardPiecesPage> owner = std::make_shared<DashboardPiecesPage>(page);
    (*owner).build();
    (*context).components.add(owner);
    (*owner).connect();
}

} // namespace gui_forms::gallery::pages
