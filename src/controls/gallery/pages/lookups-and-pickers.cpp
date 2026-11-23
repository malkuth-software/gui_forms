#include "demo_parts.hpp"
#include "gui_forms/controls/calendar_values.hpp"
#include "gui_forms/controls/data_grid_view.hpp"
#include "gui_forms/controls/data_grid_view_row_order.hpp"
#include "gui_forms/controls/data_grid_view_types.hpp"
#include "gui_forms/controls/date_time_value.hpp"
#include "gui_forms/controls/drawing_surface.hpp"
#include "gui_forms/controls/month_calendar.hpp"
#include "gui_forms/controls/numeric_up_down.hpp"
#include "gui_forms/controls/search_field.hpp"
#include "gui_forms/controls/track_bar.hpp"
#include "gui_forms/controls/tree_view.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "stxlib/math.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The lookups-and-pickers page: six DevExpress editor takes the survey
// rates "demo only", each composed from stock parts with no new API.
// - Zoom slider: a TrackBar between minus and plus Buttons that step
//   through the usual zoom stops (the status-bar zoom of an editor).
// - Duration box: hours, minutes and seconds NumericUpDowns whose spins
//   carry into the next part, as a time-span editor does.
// - Date range: one MonthCalendar showing two months with a selection
//   range and preset Buttons, in a popup under a Button.
// - Grid lookup: a SearchField over a sortable DataGridView of customers, in
//   a popup; a double click or Choose picks the row.
// - Tree lookup: a TreeView of folders in a popup; activating a folder picks
//   it.
// - Rating: five stars drawn on a DrawingSurface; hover previews, a click
//   sets.
// The popups are PopupPanel (demo_parts.hpp): an AnchoredOverlayLayer with a
// focus scope under a Button, which is the survey's PopupContainerEdit.
namespace gui_forms::gallery::pages {
namespace {

// -- dates ------------------------------------------------------------------
// Civil dates and day numbers (days since 1970-01-01, the proleptic
// Gregorian calendar), for the range presets.
std::int64_t day_number(const DateTimeValue& date) {
    const std::int64_t month = date.month;
    const std::int64_t year = static_cast<std::int64_t>(date.year) - (month <= 2 ? 1 : 0);
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const std::int64_t year_of_era = year - era * 400;
    const std::int64_t shifted_month = month > 2 ? month - 3 : month + 9;
    const std::int64_t day_of_year = (153 * shifted_month + 2) / 5 + static_cast<std::int64_t>(date.day) - 1;
    const std::int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const std::int64_t days = era * 146097 + day_of_era - 719468;
    return days;
}

DateTimeValue date_of_day(std::int64_t days) {
    const std::int64_t shifted = days + 719468;
    const std::int64_t era = (shifted >= 0 ? shifted : shifted - 146096) / 146097;
    const std::int64_t day_of_era = shifted - era * 146097;
    const std::int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const std::int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const std::int64_t shifted_month = (5 * day_of_year + 2) / 153;
    const std::int64_t day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
    const std::int64_t month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
    const std::int64_t year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
    DateTimeValue date{};
    date.year = static_cast<std::int32_t>(year);
    date.month = static_cast<std::uint8_t>(month);
    date.day = static_cast<std::uint8_t>(day);
    return date;
}

constexpr std::array<std::string_view, 12> month_abbreviations{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string date_text(const DateTimeValue& date) {
    std::string text = std::to_string(date.day);
    text += ' ';
    text += month_abbreviations[(static_cast<std::size_t>(date.month) + 11U) % 12U];
    text += ' ';
    text += std::to_string(date.year);
    return text;
}

// The page's "today", fixed so every run and capture shows the same month.
constexpr DateTimeValue fixed_today{2026, 11U, 22U, 0U, 0U, 0U, 0U};

// -- zoom -------------------------------------------------------------------
constexpr std::array<double, 11> zoom_stops{10.0, 25.0, 50.0, 67.0, 75.0, 100.0, 125.0, 150.0, 200.0, 300.0, 400.0};

// -- customers (the grid lookup) ---------------------------------------------
constexpr std::array<std::string_view, 10> customer_names{
    "Abbott Joinery", "Birch & Vale", "Cobalt Freight", "Dunmore Bakery", "Eastgate Optics",
    "Fennick Tools",  "Granite Labs", "Harbour Print",  "Iverson Farms",  "Juniper Studio"};
constexpr std::array<std::string_view, 4> customer_cities{"Leeds", "Porto", "Tallinn", "Ghent"};
constexpr std::array<std::string_view, 3> customer_segments{"Retail", "Trade", "Public"};

struct Customer final {
    std::string id{};
    std::string name{};
    std::string city{};
    std::string segment{};
    std::int64_t orders{};
};

// The lookup's rows on the stable-ID virtual model: the page owns every
// customer and the order shown, filtered by the search text and sorted by
// a header click.
class CustomerRows final : public DataGridViewCellValueProvider {
public:
    CustomerRows() {
        for (std::size_t city = 0; city < customer_cities.size(); ++city) {
            for (std::size_t name = 0; name < customer_names.size(); ++name) {
                Customer customer{};
                customer.id = "customer-" + std::to_string(customers_.size());
                customer.name = std::string(customer_names[name]);
                customer.city = std::string(customer_cities[city]);
                customer.segment = std::string(customer_segments[(name + city) % customer_segments.size()]);
                customer.orders = static_cast<std::int64_t>((name * 7U + city * 13U) % 40U + 1U);
                customers_.push_back(customer);
            }
        }
        filter("");
    }

    [[nodiscard]] BindingValue cell_value(const DataGridViewRowId& row, std::size_t column) const override {
        const Customer* const customer = find(row);
        if (customer == nullptr) return BindingValue{};
        if (column == 0U) return BindingValue{(*customer).name};
        if (column == 1U) return BindingValue{(*customer).city};
        if (column == 2U) return BindingValue{(*customer).segment};
        if (column == 3U) return BindingValue{(*customer).orders};
        return BindingValue{};
    }

    // The lookup is read only.
    bool push_cell_value(const DataGridViewRowId& row, std::size_t column, const BindingValue& value) override {
        static_cast<void>(row);
        static_cast<void>(column);
        static_cast<void>(value);
        return false;
    }

    // Keeps the customers whose name, city or segment holds `query`
    // (ASCII case ignored), in the current sort.
    void filter(std::string_view query) {
        const std::string wanted = ascii_lower(query);
        order_.clear();
        for (const Customer& customer : customers_) {
            const std::string haystack = ascii_lower(customer.name + " " + customer.city + " " + customer.segment);
            if (wanted.empty() || haystack.find(wanted) != std::string::npos) {
                order_.push_back(DataGridViewRowId{customer.id});
            }
        }
        sort_order();
    }

    void sort(std::size_t column, bool descending) {
        sort_column_ = column;
        descending_ = descending;
        sort_order();
    }

    [[nodiscard]] const std::vector<DataGridViewRowId>& order() const& noexcept { return order_; }
    const std::vector<DataGridViewRowId>& order() const&& = delete;

    [[nodiscard]] const Customer* find(const DataGridViewRowId& row) const {
        for (const Customer& customer : customers_) {
            if (customer.id == row.value) return &customer;
        }
        return nullptr;
    }

private:
    struct RowOrder final {
        const CustomerRows* rows{};
        std::size_t column{};
        bool descending{};

        bool operator()(const DataGridViewRowId& left, const DataGridViewRowId& right) const {
            const Customer& first = *(*rows).find(left);
            const Customer& second = *(*rows).find(right);
            bool less = false;
            if (column == 3U) {
                less = descending ? second.orders < first.orders : first.orders < second.orders;
                return less;
            }
            const std::string& first_text = column == 1U ? first.city : column == 2U ? first.segment : first.name;
            const std::string& second_text = column == 1U ? second.city : column == 2U ? second.segment : second.name;
            less = descending ? second_text < first_text : first_text < second_text;
            return less;
        }
    };

    void sort_order() {
        const RowOrder compare{this, sort_column_, descending_};
        std::stable_sort(order_.begin(), order_.end(), compare);
    }

    std::vector<Customer> customers_{};
    std::vector<DataGridViewRowId> order_{};
    std::size_t sort_column_{};
    bool descending_{};
};

// -- folders (the tree lookup) ----------------------------------------------
struct FolderNode final {
    std::string_view id{};
    std::string_view text{};
    std::size_t depth{};
    bool expandable{};
};

constexpr std::array<FolderNode, 14> folders{{
    {"folder.home", "Home", 0U, true},
    {"folder.documents", "Documents", 1U, true},
    {"folder.invoices", "Invoices", 2U, false},
    {"folder.letters", "Letters", 2U, false},
    {"folder.pictures", "Pictures", 1U, true},
    {"folder.holiday", "Holiday 2026", 2U, false},
    {"folder.scans", "Scans", 2U, false},
    {"folder.projects", "Projects", 1U, true},
    {"folder.gallery", "GUI.Forms Gallery", 2U, false},
    {"folder.paint", "Plan Paint", 2U, false},
    {"folder.music", "Music", 1U, false},
    {"folder.volumes", "Volumes", 0U, true},
    {"folder.backup", "Backup", 1U, false},
    {"folder.archive", "Archive", 1U, false},
}};

std::string folder_path(std::string_view id) {
    std::vector<std::string_view> parts{};
    std::size_t wanted_depth = static_cast<std::size_t>(-1);
    std::size_t index = folders.size();
    for (std::size_t at = 0; at < folders.size(); ++at) {
        if (folders[at].id == id) index = at;
    }
    if (index == folders.size()) return {};
    wanted_depth = folders[index].depth + 1U;
    for (std::size_t at = index + 1U; at-- > 0U;) {
        if (folders[at].depth + 1U == wanted_depth) {
            parts.push_back(folders[at].text);
            if (wanted_depth == 1U) break;
            --wanted_depth;
        }
    }
    std::string path{};
    for (std::size_t at = parts.size(); at-- > 0U;) {
        path += '/';
        path += parts[at];
    }
    return path;
}

// -- rating -----------------------------------------------------------------
constexpr int star_count = 5;
constexpr double star_pitch = 40.0;
constexpr double star_radius = 16.0;

// What the rating surface draws: the set value and the value under the
// pointer (zero for none). The page writes it; the paint functor reads it.
struct RatingState final {
    int value{3};
    int hover{};
};

class RatingPaint final {
public:
    explicit RatingPaint(std::shared_ptr<const RatingState> state) : state_(std::move(state)) {}

    void operator()(Painter& painter, Rect bounds, Rect damage) const {
        static_cast<void>(damage);
        const int shown = (*state_).hover > 0 ? (*state_).hover : (*state_).value;
        const Color lit = (*state_).hover > 0 ? Color::rgba(240, 170, 60) : Color::rgba(232, 150, 20);
        const Color unlit = Color::rgba(200, 205, 212);
        for (int star = 0; star < star_count; ++star) {
            const Point centre{bounds.x + star_pitch * (static_cast<double>(star) + 0.5),
                               bounds.y + bounds.height * 0.5};
            std::array<Point, 10> outline{};
            for (std::size_t corner = 0; corner < outline.size(); ++corner) {
                const double radius = corner % 2U == 0U ? star_radius : star_radius * 0.42;
                const double angle = -1.5707963267948966 + 0.6283185307179586 * static_cast<double>(corner);
                outline[corner] = Point{centre.x + radius * stxlib::math::cos(angle), centre.y + radius * stxlib::math::sin(angle)};
            }
            painter.fill_polygon(outline, star < shown ? lit : unlit, PolygonFillRule::nonzero);
        }
    }

private:
    std::shared_ptr<const RatingState> state_;
};

// -- zoom preview -------------------------------------------------------------
// A page outline drawn at the zoom, centred, clipped to the surface.
struct ZoomState final {
    double percent{100.0};
};

class ZoomPreviewPaint final {
public:
    explicit ZoomPreviewPaint(std::shared_ptr<const ZoomState> state) : state_(std::move(state)) {}

    void operator()(Painter& painter, Rect bounds, Rect damage) const {
        static_cast<void>(damage);
        painter.fill_rect(bounds, Color::rgba(226, 231, 237));
        const double scale = (*state_).percent / 100.0;
        const double width = 42.0 * scale;
        const double height = 56.0 * scale;
        const Rect sheet{bounds.x + (bounds.width - width) * 0.5, bounds.y + (bounds.height - height) * 0.5, width,
                         height};
        painter.save();
        painter.clip_rect(bounds);
        painter.fill_rect(sheet, Color::rgba(255, 255, 255));
        painter.stroke_rect(sheet, Color::rgba(120, 132, 145), 1.0);
        const double rule_width = width * 0.7;
        for (int line = 1; line <= 5; ++line) {
            const double y = sheet.y + height * 0.15 * static_cast<double>(line);
            painter.draw_line({sheet.x + width * 0.15, y}, {sheet.x + width * 0.15 + rule_width, y},
                              Color::rgba(170, 180, 190), std::max(1.0, scale));
        }
        painter.restore();
    }

private:
    std::shared_ptr<const ZoomState> state_;
};

class LookupsAndPickersPage final : public gui_forms::Component {
public:
    explicit LookupsAndPickersPage(const std::shared_ptr<Surface>& body) : body_(body) {}

    // Builds the page; the popups subscribe once the page is shared.
    void build(const std::shared_ptr<PageContext>& context) {
        Surface& body = *body_;
        add_page_heading(body, "lookups", "LOOKUPS AND PICKERS",
                         "Editors composed from stock parts: a zoom slider, a duration box, a date range, grid and "
                         "tree lookups in popups, and a star rating.");
        build_zoom(body);
        build_duration(body);
        build_rating(body);
        build_date_range(body, context);
        build_grid_lookup(body, context);
        build_tree_lookup(body, context);
        status_ = label("gallery.lookups.status", "Pick a value in any editor.", 11.0, 400, "muted");
        body.add_at(status_, {24.0, 640.0, 940.0, 22.0});
    }

private:
    // -- zoom slider --------------------------------------------------------
    void build_zoom(Surface& body) {
        const std::shared_ptr<LayoutGroup> zoom = group("gallery.lookups.zoom", "Zoom slider", {300.0, 170.0});
        body.add_at(zoom, {24.0, 92.0, 300.0, 170.0});
        zoom_out_ = make_control<Button>(StableId("gallery.lookups.zoom.out"), "-");
        (*zoom_out_).accessible_name = "Zoom out";
        (*zoom).add_at(zoom_out_, {12.0, 30.0, 28.0, 26.0});
        zoom_track_ = make_control<TrackBar>(StableId("gallery.lookups.zoom.track"));
        (*zoom_track_).accessible_name = "Zoom";
        (*zoom_track_).set_range(10.0, 400.0);
        (*zoom_track_).value = 100.0;
        (*zoom_track_).show_ticks = false;
        (*zoom).add_at(zoom_track_, {44.0, 30.0, 160.0, 26.0});
        zoom_in_ = make_control<Button>(StableId("gallery.lookups.zoom.in"), "+");
        (*zoom_in_).accessible_name = "Zoom in";
        (*zoom).add_at(zoom_in_, {208.0, 30.0, 28.0, 26.0});
        zoom_label_ = label("gallery.lookups.zoom.value", "100%", 11.0, 600, "ink");
        (*zoom).add_at(zoom_label_, {240.0, 32.0, 52.0, 22.0});
        zoom_state_ = gui_forms::detail::make_library_shared<ZoomState>();
        zoom_preview_ = make_control<DrawingSurface>(StableId("gallery.lookups.zoom.preview"));
        (*zoom_preview_).accessible_name = "Zoomed page";
        (*zoom_preview_).set_paint_callback(ZoomPreviewPaint(zoom_state_));
        (*zoom).add_at(zoom_preview_, {12.0, 64.0, 276.0, 94.0});
    }

    // The nearest stop past `percent` in `direction` (+1 up, -1 down), or
    // the end stop.
    [[nodiscard]] static double next_zoom_stop(double percent, int direction) {
        if (direction > 0) {
            for (const double stop : zoom_stops) {
                if (stop > percent + 0.5) return stop;
            }
            return zoom_stops.back();
        }
        for (std::size_t index = zoom_stops.size(); index-- > 0U;) {
            if (zoom_stops[index] < percent - 0.5) return zoom_stops[index];
        }
        return zoom_stops.front();
    }

    void on_zoom_out() { (*zoom_track_).value = next_zoom_stop((*zoom_track_).value.read(), -1); }
    void on_zoom_in() { (*zoom_track_).value = next_zoom_stop((*zoom_track_).value.read(), 1); }

    void on_zoom_changed(double value) {
        const double percent = std::round(value);
        (*zoom_label_).text = std::to_string(static_cast<int>(percent)) + "%";
        (*zoom_state_).percent = percent;
        (*zoom_preview_).invalidate(Dirty::paint);
    }

    // -- duration box -------------------------------------------------------
    void build_duration(Surface& body) {
        const std::shared_ptr<LayoutGroup> duration =
            group("gallery.lookups.duration", "Duration box", {300.0, 170.0});
        body.add_at(duration, {340.0, 92.0, 300.0, 170.0});
        constexpr std::array<std::string_view, 3> parts{"hours", "minutes", "seconds"};
        constexpr std::array<std::string_view, 3> units{"h", "min", "s"};
        // Minutes and seconds reach one past their range for a moment, so a
        // spin can carry; on_duration_part moves the carry on.
        constexpr std::array<double, 3> lowest{0.0, -1.0, -1.0};
        constexpr std::array<double, 3> highest{99.0, 60.0, 60.0};
        constexpr std::array<double, 3> initial{1.0, 2.0, 5.0};
        for (std::size_t part = 0; part < parts.size(); ++part) {
            const std::string id = "gallery.lookups.duration." + std::string(parts[part]);
            std::shared_ptr<NumericUpDown> spin = make_control<NumericUpDown>(StableId(id));
            (*spin).accessible_name = std::string(parts[part]);
            (*spin).set_range(lowest[part], highest[part]);
            (*spin).value = initial[part];
            const double left = 12.0 + 94.0 * static_cast<double>(part);
            (*duration).add_at(spin, {left, 32.0, 56.0, 26.0});
            (*duration).add_at(label(id + ".unit", std::string(units[part]), 11.0, 400, "muted"),
                               {left + 60.0, 34.0, 30.0, 22.0});
            duration_parts_[part] = spin;
        }
        duration_total_ = label("gallery.lookups.duration.total", "", 11.0, 400, "ink");
        (*duration).add_at(duration_total_, {12.0, 72.0, 276.0, 22.0});
        (*duration).add_at(wrapped_label("gallery.lookups.duration.note",
                                 "Spinning seconds past 59 carries into minutes, and minutes into hours.", 10.0,
                                 400, "muted"),
                           {12.0, 100.0, 276.0, 40.0});
        show_duration();
    }

    // A part reached one past its range: carry into the part above, or
    // borrow from it; the hours stop at their range.
    void on_duration_part(std::size_t part, double value) {
        if (part == 0U) {
            show_duration();
            return;
        }
        NumericUpDown& spin = *duration_parts_[part];
        NumericUpDown& above = *duration_parts_[part - 1U];
        if (value >= 60.0) {
            const double top = part == 1U ? 99.0 : 59.0;
            const bool room = above.value.read() < top;
            spin.value = room ? 0.0 : 59.0;
            if (room) above.value = above.value.read() + 1.0;
        } else if (value < 0.0) {
            const bool owed = above.value.read() > 0.0;
            spin.value = owed ? 59.0 : 0.0;
            if (owed) above.value = above.value.read() - 1.0;
        }
        show_duration();
    }

    void show_duration() {
        const std::int64_t hours = static_cast<std::int64_t>((*duration_parts_[0]).value.read());
        const std::int64_t minutes = std::clamp<std::int64_t>(
            static_cast<std::int64_t>((*duration_parts_[1]).value.read()), 0, 59);
        const std::int64_t seconds = std::clamp<std::int64_t>(
            static_cast<std::int64_t>((*duration_parts_[2]).value.read()), 0, 59);
        const std::int64_t total = hours * 3600 + minutes * 60 + seconds;
        (*duration_total_).text = std::to_string(hours) + " h " + std::to_string(minutes) + " min " +
                                  std::to_string(seconds) + " s  ·  " + std::to_string(total) + " s in all";
    }

    // -- rating -------------------------------------------------------------
    void build_rating(Surface& body) {
        const std::shared_ptr<LayoutGroup> rating = group("gallery.lookups.rating", "Rating", {320.0, 170.0});
        body.add_at(rating, {656.0, 92.0, 320.0, 170.0});
        rating_state_ = gui_forms::detail::make_library_shared<RatingState>();
        rating_ = make_control<DrawingSurface>(StableId("gallery.lookups.rating.stars"));
        (*rating_).hit_test_visible = true;
        (*rating_).semantic_role = SemanticRole::slider;
        (*rating_).set_paint_callback(RatingPaint(rating_state_));
        (*rating).add_at(rating_, {12.0, 32.0, star_pitch * star_count, 40.0});
        rating_label_ = label("gallery.lookups.rating.value", "", 11.0, 400, "ink");
        (*rating).add_at(rating_label_, {12.0, 80.0, 200.0, 22.0});
        rating_clear_ = make_control<Button>(StableId("gallery.lookups.rating.clear"), "Clear");
        (*rating).add_at(rating_clear_, {220.0, 78.0, 88.0, 26.0});
        show_rating();
    }

    [[nodiscard]] static int star_at(const MouseEventArgs& arguments) {
        const int star = static_cast<int>(std::floor(arguments.location.x / star_pitch)) + 1;
        return std::clamp(star, 1, star_count);
    }

    void on_rating_move(const MouseEventArgs& arguments) {
        const int star = star_at(arguments);
        if (star == (*rating_state_).hover) return;
        (*rating_state_).hover = star;
        (*rating_).invalidate(Dirty::paint);
    }

    void on_rating_leave() {
        (*rating_state_).hover = 0;
        (*rating_).invalidate(Dirty::paint);
    }

    void on_rating_down(const MouseEventArgs& arguments) {
        if (arguments.button != PointerButton::primary) return;
        (*rating_state_).value = star_at(arguments);
        show_rating();
        report("Rated " + std::to_string((*rating_state_).value) + " of 5");
    }

    void on_rating_clear() {
        (*rating_state_).value = 0;
        show_rating();
        report("Rating cleared");
    }

    void show_rating() {
        const std::string text = std::to_string((*rating_state_).value) + " of 5 stars";
        (*rating_label_).text = text;
        (*rating_).accessible_name = "Rating, " + text;
        (*rating_).invalidate(Dirty::paint);
    }

    // -- date range ---------------------------------------------------------
    void build_date_range(Surface& body, const std::shared_ptr<PageContext>& context) {
        const std::shared_ptr<LayoutGroup> range = group("gallery.lookups.range", "Date range", {470.0, 150.0});
        body.add_at(range, {24.0, 282.0, 470.0, 150.0});
        range_button_ = make_control<Button>(StableId("gallery.lookups.range.button"), "");
        (*range).add_at(range_button_, {12.0, 32.0, 300.0, 28.0});
        (*range).add_at(wrapped_label("gallery.lookups.range.note",
                              "Shift-click or drag across the two months, or take a preset.", 10.0, 400, "muted"),
                        {12.0, 70.0, 440.0, 22.0});

        std::shared_ptr<Panel> content = make_control<Panel>(StableId("gallery.lookups.range.popup"));
        static_cast<void>((*content).add_class("paper"));
        calendar_ = make_control<MonthCalendar>(StableId("gallery.lookups.range.calendar"));
        (*calendar_).calendar_dimensions = CalendarDimensions{2U, 1U};
        (*calendar_).max_selection_count = 62U;
        (*calendar_).set_today_date(fixed_today);
        (*calendar_).set_selection_range(date_of_day(day_number(fixed_today) - 6), fixed_today);
        (*calendar_).set_requested_bounds({8.0, 8.0, 440.0, 200.0});
        (*content).add_child(calendar_);
        constexpr std::array<std::string_view, 4> presets{"Today", "Last 7 days", "This month", "Last 30 days"};
        for (std::size_t preset = 0; preset < presets.size(); ++preset) {
            const std::string id = "gallery.lookups.range.preset." + std::to_string(preset);
            std::shared_ptr<Button> button = make_control<Button>(StableId(id), std::string(presets[preset]));
            (*button).set_requested_bounds({456.0, 8.0 + 34.0 * static_cast<double>(preset), 112.0, 28.0});
            (*content).add_child(button);
            range_presets_[preset] = button;
        }
        range_apply_ = make_control<Button>(StableId("gallery.lookups.range.apply"), "Apply");
        (*range_apply_).set_requested_bounds({456.0, 180.0, 112.0, 28.0});
        (*content).add_child(range_apply_);
        range_popup_ = gui_forms::detail::make_library_shared<PopupPanel>("gallery.lookups.range.layer", range_button_, content, calendar_,
                                                    Size{576.0, 216.0});
        (*context).components.add(range_popup_);
        chosen_range_ = (*calendar_).selection_range();
        show_range();
    }

    void on_range_preset(std::size_t preset) {
        const std::int64_t today = day_number(fixed_today);
        DateTimeValue first = fixed_today;
        if (preset == 1U) first = date_of_day(today - 6);
        if (preset == 2U) first = DateTimeValue{fixed_today.year, fixed_today.month, 1U, 0U, 0U, 0U, 0U};
        if (preset == 3U) first = date_of_day(today - 29);
        (*calendar_).set_selection_range(first, fixed_today);
    }

    void on_range_apply() {
        chosen_range_ = (*calendar_).selection_range();
        (*range_popup_).close();
        show_range();
        report("Range " + (*range_button_).text.read());
    }

    void show_range() {
        const std::int64_t days = day_number(chosen_range_.end) - day_number(chosen_range_.start) + 1;
        (*range_button_).text = date_text(chosen_range_.start) + "  to  " + date_text(chosen_range_.end) + "  (" +
                                std::to_string(days) + (days == 1 ? " day)" : " days)");
    }

    // -- grid lookup --------------------------------------------------------
    void build_grid_lookup(Surface& body, const std::shared_ptr<PageContext>& context) {
        const std::shared_ptr<LayoutGroup> lookup = group("gallery.lookups.grid", "Grid lookup", {466.0, 150.0});
        body.add_at(lookup, {510.0, 282.0, 466.0, 150.0});
        customer_button_ = make_control<Button>(StableId("gallery.lookups.grid.button"), "Customer: none");
        (*lookup).add_at(customer_button_, {12.0, 32.0, 300.0, 28.0});
        (*lookup).add_at(wrapped_label("gallery.lookups.grid.note",
                               "Type to filter, click a header to sort, double-click a row to choose.", 10.0, 400,
                               "muted"),
                         {12.0, 70.0, 440.0, 22.0});

        std::shared_ptr<Panel> content = make_control<Panel>(StableId("gallery.lookups.grid.popup"));
        static_cast<void>((*content).add_class("paper"));
        customer_search_ = make_control<SearchField>(StableId("gallery.lookups.grid.search"));
        (*customer_search_).accessible_name = "Find a customer";
        (*customer_search_).set_requested_bounds({8.0, 8.0, 504.0, 28.0});
        (*content).add_child(customer_search_);
        customer_grid_ = make_control<DataGridView>(StableId("gallery.lookups.grid.rows"));
        (*customer_grid_).accessible_name = "Customers";
        (*customer_grid_).read_only = true;
        (*customer_grid_).row_headers_visible = false;
        (*customer_grid_).selection_mode = DataGridViewSelectionMode::full_row_select;
        (*customer_grid_).multi_select = false;
        (*customer_grid_).row_order_policy = DataGridViewRowOrderPolicy::edit;
        (*customer_grid_).set_columns({
            {.name = "name", .header_text = "Customer", .width = 180.0},
            {.name = "city", .header_text = "City", .width = 110.0},
            {.name = "segment", .header_text = "Segment", .width = 100.0},
            {.name = "orders", .header_text = "Orders", .width = 80.0,
             .default_cell_style = {.alignment = stxlib::Optional<DataGridViewContentAlignment>::of(
                                        DataGridViewContentAlignment::middle_end)}},
        });
        customer_rows_ = gui_forms::detail::make_library_shared<CustomerRows>();
        (*customer_grid_).set_virtual_rows(customer_rows_, (*customer_rows_).order());
        (*customer_grid_).set_requested_bounds({8.0, 44.0, 504.0, 212.0});
        (*content).add_child(customer_grid_);
        customer_choose_ = make_control<Button>(StableId("gallery.lookups.grid.choose"), "Choose");
        (*customer_choose_).set_requested_bounds({412.0, 264.0, 100.0, 28.0});
        (*content).add_child(customer_choose_);
        customer_popup_ = gui_forms::detail::make_library_shared<PopupPanel>("gallery.lookups.grid.layer", customer_button_, content,
                                                       customer_search_, Size{520.0, 300.0});
        (*context).components.add(customer_popup_);
    }

    void on_customer_search(const std::string& query) {
        (*customer_rows_).filter(query);
        (*customer_grid_).set_virtual_row_order((*customer_rows_).order());
    }

    void on_customer_header(std::size_t column) {
        const DataGridViewSortDirection shown = (*customer_grid_).columns()[column].sort_glyph;
        const DataGridViewSortDirection next = shown == DataGridViewSortDirection::ascending
                                                   ? DataGridViewSortDirection::descending
                                                   : DataGridViewSortDirection::ascending;
        (*customer_rows_).sort(column, next == DataGridViewSortDirection::descending);
        (*customer_grid_).set_virtual_row_order((*customer_rows_).order());
        for (std::size_t index = 0; index < (*customer_grid_).columns().size(); ++index) {
            (*customer_grid_).set_column_sort_glyph(index, index == column ? next : DataGridViewSortDirection::none);
        }
    }

    void on_customer_double_click(const DataGridViewCellEvent& event) { choose_customer(event.row_id); }

    void on_customer_choose() {
        const std::optional<DataGridViewRowId> row = (*customer_grid_).current_row_id();
        if (!row.has_value()) return;
        choose_customer(*row);
    }

    void choose_customer(const DataGridViewRowId& row) {
        const Customer* const customer = (*customer_rows_).find(row);
        if (customer == nullptr) return;
        (*customer_button_).text = "Customer: " + (*customer).name + " (" + (*customer).city + ")";
        (*customer_popup_).close();
        report("Chose " + (*customer).name);
    }

    // -- tree lookup --------------------------------------------------------
    void build_tree_lookup(Surface& body, const std::shared_ptr<PageContext>& context) {
        const std::shared_ptr<LayoutGroup> lookup = group("gallery.lookups.tree", "Tree lookup", {470.0, 150.0});
        body.add_at(lookup, {24.0, 452.0, 470.0, 150.0});
        folder_button_ = make_control<Button>(StableId("gallery.lookups.tree.button"), "Folder: /Home");
        (*lookup).add_at(folder_button_, {12.0, 32.0, 300.0, 28.0});
        (*lookup).add_at(wrapped_label("gallery.lookups.tree.note",
                               "Expand the folders and activate one (double-click or Enter) to choose it.", 10.0,
                               400, "muted"),
                         {12.0, 70.0, 440.0, 22.0});

        std::shared_ptr<Panel> content = make_control<Panel>(StableId("gallery.lookups.tree.popup"));
        static_cast<void>((*content).add_class("paper"));
        folder_tree_ = make_control<TreeView>(StableId("gallery.lookups.tree.folders"));
        (*folder_tree_).accessible_name = "Folders";
        std::vector<TreeViewItem> items{};
        for (const FolderNode& node : folders) {
            TreeViewItem item{};
            item.stable_id = std::string(node.id);
            item.text = std::string(node.text);
            item.depth = node.depth;
            item.expandable = node.expandable;
            item.expanded = node.depth == 0U;
            items.push_back(std::move(item));
        }
        (*folder_tree_).set_items(std::move(items));
        (*folder_tree_).set_requested_bounds({8.0, 8.0, 304.0, 248.0});
        (*content).add_child(folder_tree_);
        folder_choose_ = make_control<Button>(StableId("gallery.lookups.tree.choose"), "Choose");
        (*folder_choose_).set_requested_bounds({212.0, 264.0, 100.0, 28.0});
        (*content).add_child(folder_choose_);
        folder_popup_ = gui_forms::detail::make_library_shared<PopupPanel>("gallery.lookups.tree.layer", folder_button_, content,
                                                     folder_tree_, Size{320.0, 300.0});
        (*context).components.add(folder_popup_);
    }

    void on_folder_activated(const std::string& id) { choose_folder(id); }

    void on_folder_choose() { choose_folder(std::string((*folder_tree_).active_id())); }

    void choose_folder(const std::string& id) {
        const std::string path = folder_path(id);
        if (path.empty()) return;
        (*folder_button_).text = "Folder: " + path;
        (*folder_popup_).close();
        report("Chose " + path);
    }

    void report(std::string text) { (*status_).text = std::move(text); }

public:
    // Subscribes every handler; the page is shared by now.
    void connect() {
        on((*zoom_out_).clicked(), *this, &LookupsAndPickersPage::on_zoom_out);
        on((*zoom_in_).clicked(), *this, &LookupsAndPickersPage::on_zoom_in);
        on((*zoom_track_).value_changed(), *this, &LookupsAndPickersPage::on_zoom_changed);
        for (std::size_t part = 0; part < duration_parts_.size(); ++part) {
            on((*duration_parts_[part]).value_changed(), *this, &LookupsAndPickersPage::on_duration_part, part);
        }
        on((*rating_).mouse_move(), *this, &LookupsAndPickersPage::on_rating_move);
        on((*rating_).mouse_leave(), *this, &LookupsAndPickersPage::on_rating_leave);
        on((*rating_).mouse_down(), *this, &LookupsAndPickersPage::on_rating_down);
        on((*rating_clear_).clicked(), *this, &LookupsAndPickersPage::on_rating_clear);
        for (std::size_t preset = 0; preset < range_presets_.size(); ++preset) {
            on((*range_presets_[preset]).clicked(), *this, &LookupsAndPickersPage::on_range_preset, preset);
        }
        on((*range_apply_).clicked(), *this, &LookupsAndPickersPage::on_range_apply);
        (*range_popup_).connect();
        on((*customer_search_).search_requested(), *this, &LookupsAndPickersPage::on_customer_search);
        on((*customer_grid_).column_header_click(), *this, &LookupsAndPickersPage::on_customer_header);
        on((*customer_grid_).cell_double_click(), *this, &LookupsAndPickersPage::on_customer_double_click);
        on((*customer_choose_).clicked(), *this, &LookupsAndPickersPage::on_customer_choose);
        (*customer_popup_).connect();
        on((*folder_tree_).item_activated(), *this, &LookupsAndPickersPage::on_folder_activated);
        on((*folder_choose_).clicked(), *this, &LookupsAndPickersPage::on_folder_choose);
        (*folder_popup_).connect();
    }

private:
    std::shared_ptr<Surface> body_;
    std::shared_ptr<Label> status_;

    std::shared_ptr<Button> zoom_out_;
    std::shared_ptr<Button> zoom_in_;
    std::shared_ptr<TrackBar> zoom_track_;
    std::shared_ptr<Label> zoom_label_;
    std::shared_ptr<ZoomState> zoom_state_;
    std::shared_ptr<DrawingSurface> zoom_preview_;

    std::array<std::shared_ptr<NumericUpDown>, 3> duration_parts_{};
    std::shared_ptr<Label> duration_total_;

    std::shared_ptr<RatingState> rating_state_;
    std::shared_ptr<DrawingSurface> rating_;
    std::shared_ptr<Label> rating_label_;
    std::shared_ptr<Button> rating_clear_;

    std::shared_ptr<Button> range_button_;
    std::shared_ptr<MonthCalendar> calendar_;
    std::array<std::shared_ptr<Button>, 4> range_presets_{};
    std::shared_ptr<Button> range_apply_;
    std::shared_ptr<PopupPanel> range_popup_;
    CalendarDateRange chosen_range_{};

    std::shared_ptr<Button> customer_button_;
    std::shared_ptr<SearchField> customer_search_;
    std::shared_ptr<DataGridView> customer_grid_;
    std::shared_ptr<CustomerRows> customer_rows_;
    std::shared_ptr<Button> customer_choose_;
    std::shared_ptr<PopupPanel> customer_popup_;

    std::shared_ptr<Button> folder_button_;
    std::shared_ptr<TreeView> folder_tree_;
    std::shared_ptr<Button> folder_choose_;
    std::shared_ptr<PopupPanel> folder_popup_;
};

} // namespace

void add_lookups_and_pickers(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    const std::shared_ptr<LookupsAndPickersPage> owner = gui_forms::detail::make_library_shared<LookupsAndPickersPage>(page);
    (*owner).build(context);
    (*context).components.add(owner);
    (*owner).connect();
}

} // namespace gui_forms::gallery::pages
