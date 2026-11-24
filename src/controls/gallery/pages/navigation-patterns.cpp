#include "demo_parts.hpp"
#include "gui_forms/controls/callout.hpp"
#include "gui_forms/controls/command_bar.hpp"
#include "gui_forms/controls/drawing_surface.hpp"
#include "gui_forms/controls/expandable_sections.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/search_field.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/timer/timer/timer.hpp"
#include "stxlib/math.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The navigation-patterns page: six DevExpress navigation takes the survey
// rates "demo only", composed from stock parts with no new API.
// - Accordion: ExpandableSections, one open at a time or many.
// - Drawer: a Panel that a Timer slides open beside the content.
// - Guided tour: a Callout walked from step to step with its action.
// - Mini toolbar: a CommandBar in an AnchoredOverlayLayer over a TextBox,
//   shown while text is selected (the Office floating format bar).
// - Command search: a SearchField over a ListBox of commands; activating one
//   runs it (the Ctrl+Q "Tell me" box).
// - Radial menu: six sectors drawn on a DrawingSurface; press, drag to a
//   sector and release to pick, or click the hub and then a sector.
namespace gui_forms::gallery::pages {
namespace {

// -- commands (the command search) -------------------------------------------
struct NamedCommand final {
    std::string_view name{};
    std::string_view shortcut{};
};

constexpr std::array<NamedCommand, 18> named_commands{{
    {"New document", "Ctrl+N"},     {"Open file", "Ctrl+O"},        {"Save", "Ctrl+S"},
    {"Save as", "Ctrl+Shift+S"},    {"Print", "Ctrl+P"},            {"Find", "Ctrl+F"},
    {"Replace", "Ctrl+H"},          {"Go to line", "Ctrl+G"},       {"Insert table", ""},
    {"Insert picture", ""},         {"Insert link", "Ctrl+K"},      {"Bold", "Ctrl+B"},
    {"Italic", "Ctrl+I"},           {"Underline", "Ctrl+U"},        {"Zoom in", "Ctrl++"},
    {"Zoom out", "Ctrl+-"},         {"Toggle full screen", "F11"},  {"Show keyboard shortcuts", "F1"},
}};

std::string command_line(const NamedCommand& command) {
    std::string line(command.name);
    if (!command.shortcut.empty()) {
        line += "   ";
        line += command.shortcut;
    }
    return line;
}

// -- radial menu --------------------------------------------------------------
constexpr std::array<std::string_view, 6> radial_items{"Cut", "Copy", "Paste", "Delete", "Rename", "Share"};
constexpr double radial_hub = 22.0;
constexpr double radial_outer = 78.0;
constexpr double pi = 3.141592653589793;

struct RadialState final {
    bool open{};
    Point centre{};
    // The sector under the pointer, or -1.
    int hot{-1};
};

// The sector at `offset` from the centre: -1 in the hub or outside the
// ring. Sector 0 is centred straight up, then clockwise.
[[nodiscard]] int radial_sector(Point offset) {
    const double distance = std::sqrt(offset.x * offset.x + offset.y * offset.y);
    if (distance < radial_hub || distance > radial_outer + 24.0) return -1;
    const double sector_angle = 2.0 * pi / static_cast<double>(radial_items.size());
    double angle = stxlib::math::atan2(offset.x, -offset.y) + sector_angle * 0.5;
    while (angle < 0.0) angle += 2.0 * pi;
    // The items' count as an int, the array's own size.
    constexpr int radial_count = 6;
    static_assert(radial_items.size() == static_cast<std::size_t>(radial_count));
    const int sector = static_cast<int>(angle / sector_angle) % radial_count;
    return sector;
}

class RadialPaint final {
public:
    explicit RadialPaint(std::shared_ptr<const RadialState> state) : state_(std::move(state)) {}

    void operator()(Painter& painter, Rect bounds, Rect damage) const {
        static_cast<void>(damage);
        painter.fill_rounded_rect(bounds, 6.0, Color::rgba(236, 241, 246));
        const FontSpec font{FontRole::control, 11.0, 400, false};
        if (!(*state_).open) {
            painter.draw_text_utf8({bounds.x + 14.0, bounds.y + bounds.height * 0.5 + 4.0},
                                   "Press anywhere here to open the menu", font, Color::rgba(91, 107, 121));
            return;
        }
        const Point centre{bounds.x + (*state_).centre.x, bounds.y + (*state_).centre.y};
        const std::size_t count = radial_items.size();
        const double sector_angle = 2.0 * pi / static_cast<double>(count);
        for (std::size_t sector = 0; sector < count; ++sector) {
            // A sector as a polygon: the outer arc, then the hub arc back.
            constexpr std::size_t steps = 10U;
            std::array<Point, (steps + 1U) * 2U> outline{};
            const double first = -pi * 0.5 + sector_angle * (static_cast<double>(sector) - 0.5) + 0.03;
            const double last = first + sector_angle - 0.06;
            for (std::size_t step = 0; step <= steps; ++step) {
                const double angle = first + (last - first) * static_cast<double>(step) / steps;
                outline[step] = Point{centre.x + radial_outer * stxlib::math::cos(angle), centre.y + radial_outer * stxlib::math::sin(angle)};
                const double back = last - (last - first) * static_cast<double>(step) / steps;
                outline[steps + 1U + step] =
                    Point{centre.x + radial_hub * stxlib::math::cos(back), centre.y + radial_hub * stxlib::math::sin(back)};
            }
            const bool hot = static_cast<int>(sector) == (*state_).hot;
            painter.fill_polygon(outline, hot ? Color::rgba(39, 116, 184) : Color::rgba(255, 255, 255),
                                 PolygonFillRule::nonzero);
            const double middle = -pi * 0.5 + sector_angle * static_cast<double>(sector);
            const double reach = (radial_hub + radial_outer) * 0.5;
            const std::string_view text = radial_items[sector];
            const Point origin{centre.x + reach * stxlib::math::cos(middle) - static_cast<double>(text.size()) * 3.0,
                               centre.y + reach * stxlib::math::sin(middle) + 4.0};
            painter.draw_text_utf8(origin, text, font, hot ? Color::rgba(255, 255, 255) : Color::rgba(28, 39, 50));
        }
        std::array<Point, 16> hub{};
        for (std::size_t corner = 0; corner < hub.size(); ++corner) {
            const double angle = 2.0 * pi * static_cast<double>(corner) / static_cast<double>(hub.size());
            hub[corner] = Point{centre.x + (radial_hub - 4.0) * stxlib::math::cos(angle),
                                centre.y + (radial_hub - 4.0) * stxlib::math::sin(angle)};
        }
        painter.fill_polygon(hub, Color::rgba(91, 107, 121), PolygonFillRule::nonzero);
    }

private:
    std::shared_ptr<const RadialState> state_;
};

// -- accordion ----------------------------------------------------------------
struct AccordionEntry final {
    std::string_view title{};
    std::string_view body{};
};

constexpr std::array<AccordionEntry, 4> accordion_entries{{
    {"Mail", "Inbox (12), Drafts (2), Sent, Archive"},
    {"Calendar", "Today: stand-up at 9:30, design review at 14:00"},
    {"Contacts", "142 people in 6 groups; 3 birthdays this week"},
    {"Tasks", "5 open, 2 due today, 1 overdue"},
}};

// -- drawer -------------------------------------------------------------------
constexpr double drawer_width = 150.0;
constexpr int drawer_frames = 12;
constexpr std::array<std::string_view, 4> drawer_items{"Home", "Reports", "People", "Settings"};

// -- tour ---------------------------------------------------------------------
struct TourStep final {
    std::string_view title{};
    std::string_view text{};
};

constexpr std::array<TourStep, 5> tour_steps{{
    {"Accordion", "One section open at a time; the toggle allows several."},
    {"Drawer", "The menu button slides the drawer in over the content."},
    {"Mini toolbar", "Select some words here and a toolbar appears above them."},
    {"Command search", "Type part of a command's name; Enter or a double click runs it."},
    {"Radial menu", "Press, drag to a sector and let go. That is the whole tour."},
}};

class NavigationPatternsPage final : public gui_forms::Component {
public:
    explicit NavigationPatternsPage(const std::shared_ptr<Surface>& body) : body_(body) {}

    void build() {
        Surface& body = *body_;
        add_page_heading(body, "navigation", "NAVIGATION PATTERNS",
                         "An accordion, a drawer, a guided tour, a floating mini toolbar, command search and a "
                         "radial menu, each composed from stock controls.");
        tour_start_ = make_control<Button>(StableId("gallery.navigation.tour.start"), "Take the tour");
        body.add_at(tour_start_, {836.0, 18.0, 140.0, 28.0});
        build_accordion(body);
        build_drawer(body);
        build_mini_toolbar(body);
        build_command_search(body);
        build_radial(body);
        build_tour_notes(body);
        status_ = label("gallery.navigation.status", "Pick something in any pattern.", 11.0, 400, "muted");
        body.add_at(status_, {24.0, 640.0, 940.0, 22.0});
    }

    void connect() {
        on((*accordion_).toggled(), *this, &NavigationPatternsPage::on_section_toggled);
        on((*accordion_mode_).clicked(), *this, &NavigationPatternsPage::on_accordion_mode);
        on((*drawer_toggle_).clicked(), *this, &NavigationPatternsPage::on_drawer_toggle);
        for (std::size_t item = 0; item < drawer_buttons_.size(); ++item) {
            on((*drawer_buttons_[item]).clicked(), *this, &NavigationPatternsPage::on_drawer_item, item);
        }
        on((*editor_).selection_changed(), *this, &NavigationPatternsPage::on_editor_selection);
        on((*mini_bar_).invoked(), *this, &NavigationPatternsPage::on_mini_command);
        on((*mini_layer_).dismiss_requested(), *this, &NavigationPatternsPage::on_mini_dismiss);
        on((*command_search_).search_requested(), *this, &NavigationPatternsPage::on_command_search);
        on((*command_list_).item_activated(), *this, &NavigationPatternsPage::on_command_activated);
        on((*radial_).mouse_down(), *this, &NavigationPatternsPage::on_radial_down);
        on((*radial_).mouse_move(), *this, &NavigationPatternsPage::on_radial_move);
        on((*radial_).mouse_up(), *this, &NavigationPatternsPage::on_radial_up);
        on((*tour_start_).clicked(), *this, &NavigationPatternsPage::on_tour_start);
        on((*tour_).action_clicked(), *this, &NavigationPatternsPage::on_tour_next);
        on((*tour_).closed(), *this, &NavigationPatternsPage::on_tour_closed);
    }

private:
    // -- accordion ------------------------------------------------------------
    void build_accordion(Surface& body) {
        const std::shared_ptr<LayoutGroup> box = group("gallery.navigation.accordion", "Accordion", {300.0, 260.0});
        body.add_at(box, {24.0, 92.0, 300.0, 260.0});
        accordion_ = make_control<ExpandableSections>(StableId("gallery.navigation.accordion.sections"));
        (*accordion_).single_open = true;
        std::vector<SectionItem> sections{};
        for (std::size_t entry = 0; entry < accordion_entries.size(); ++entry) {
            const std::string id = "gallery.navigation.accordion.body." + std::to_string(entry);
            std::shared_ptr<Label> content = wrapped_label(id, std::string(accordion_entries[entry].body), 11.0, 400, "ink");
            SectionItem section{};
            section.id = static_cast<int>(entry);
            section.label = std::string(accordion_entries[entry].title);
            section.content = content;
            sections.push_back(std::move(section));
        }
        (*accordion_).set_items(std::move(sections));
        (*accordion_).set_expanded(0, true);
        (*box).add_at(accordion_, {12.0, 30.0, 276.0, 186.0});
        accordion_mode_ = make_control<Button>(StableId("gallery.navigation.accordion.mode"), "One open at a time");
        (*box).add_at(accordion_mode_, {12.0, 222.0, 276.0, 26.0});
    }

    void on_section_toggled(int index, bool expanded) {
        if (index < 0 || static_cast<std::size_t>(index) >= accordion_entries.size()) return;
        report(std::string(accordion_entries[static_cast<std::size_t>(index)].title) +
               (expanded ? " opened" : " closed"));
    }

    void on_accordion_mode() {
        const bool single = !(*accordion_).single_open.read();
        (*accordion_).single_open = single;
        (*accordion_mode_).text = single ? "One open at a time" : "Several open";
    }

    // -- drawer ---------------------------------------------------------------
    void build_drawer(Surface& body) {
        const std::shared_ptr<LayoutGroup> box = group("gallery.navigation.drawer", "Drawer", {300.0, 260.0});
        body.add_at(box, {342.0, 92.0, 300.0, 260.0});
        drawer_group_ = box;
        drawer_toggle_ = make_control<Button>(StableId("gallery.navigation.drawer.toggle"), "Menu");
        (*drawer_toggle_).accessible_name = "Open the navigation drawer";
        (*drawer_toggle_).expanded_state = stxlib::Optional<bool>::of(false);
        (*box).add_at(drawer_toggle_, {12.0, 30.0, 70.0, 26.0});
        drawer_page_ = label("gallery.navigation.drawer.page", "Home", 16.0, 700, "heading");
        (*box).add_at(drawer_page_, {96.0, 30.0, 190.0, 28.0});
        (*box).add_at(wrapped_label("gallery.navigation.drawer.text",
                            "The drawer slides over this content from the left edge and closes when you pick a page.",
                            11.0, 400, "muted"),
                      {96.0, 66.0, 190.0, 120.0});
        drawer_ = make_control<Panel>(StableId("gallery.navigation.drawer.panel"));
        static_cast<void>((*drawer_).add_class("paper"));
        for (std::size_t item = 0; item < drawer_items.size(); ++item) {
            const std::string id = "gallery.navigation.drawer.item." + std::to_string(item);
            std::shared_ptr<Button> button = make_control<Button>(StableId(id), std::string(drawer_items[item]));
            (*button).set_requested_bounds({8.0, 8.0 + 34.0 * static_cast<double>(item), drawer_width - 16.0, 28.0});
            (*drawer_).add_child(button);
            drawer_buttons_[item] = button;
        }
        (*drawer_).visible = false;
        (*box).add_at(drawer_, {12.0, 62.0, 1.0, 186.0});
    }

    void on_drawer_toggle() { set_drawer_open(!drawer_open_); }

    void on_drawer_item(std::size_t item) {
        (*drawer_page_).text = std::string(drawer_items[item]);
        set_drawer_open(false);
        report("Drawer: " + std::string(drawer_items[item]));
    }

    void set_drawer_open(bool open) {
        drawer_open_ = open;
        (*drawer_toggle_).expanded_state = stxlib::Optional<bool>::of(open);
        (*drawer_toggle_).accessible_name = open ? "Close the navigation drawer" : "Open the navigation drawer";
        if (reduced_motion_for(*drawer_) || !ensure_timer()) {
            drawer_frame_ = open ? drawer_frames : 0;
            place_drawer();
            return;
        }
        (*timer_).start();
    }

    // The drawer's width follows its frame (0 closed, drawer_frames open).
    void place_drawer() {
        const double amount = static_cast<double>(drawer_frame_) / drawer_frames;
        const double eased = amount * amount * (3.0 - 2.0 * amount);
        const double width = std::max(1.0, drawer_width * eased);
        (*drawer_).visible = drawer_frame_ > 0;
        (*drawer_group_).set_design_bounds(*drawer_, {12.0, 62.0, width, 186.0});
    }

    bool ensure_timer() {
        if (timer_) return true;
        Window* const window = (*drawer_toggle_).attached_window();
        if (window == nullptr) return false;
        timer_ = std::make_unique<Timer>(*window, std::chrono::milliseconds(16));
        on((*timer_).tick(), *this, &NavigationPatternsPage::on_drawer_tick);
        return true;
    }

    void on_drawer_tick() {
        const int target = drawer_open_ ? drawer_frames : 0;
        if (drawer_frame_ < target) ++drawer_frame_;
        if (drawer_frame_ > target) --drawer_frame_;
        place_drawer();
        if (drawer_frame_ == target) (*timer_).stop();
    }

    // -- mini toolbar ---------------------------------------------------------
    void build_mini_toolbar(Surface& body) {
        const std::shared_ptr<LayoutGroup> box = group("gallery.navigation.mini", "Mini toolbar", {316.0, 260.0});
        body.add_at(box, {660.0, 92.0, 316.0, 260.0});
        editor_ = make_control<TextBox>(StableId("gallery.navigation.mini.editor"));
        (*editor_).accessible_name = "Note";
        (*editor_).multiline = true;
        (*editor_).word_wrap = true;
        (*editor_).text = "Select a few words of this note and a small toolbar floats above them: bold, italic, "
                          "upper case and lower case act on the selection.";
        (*box).add_at(editor_, {12.0, 70.0, 292.0, 176.0});
        mini_bar_ = make_control<CommandBar>(StableId("gallery.navigation.mini.bar"));
        mini_bar_items_ = {CommandItem{0, "Bold", "B", "Bold"}, CommandItem{1, "Italic", "I", "Italic"},
                           CommandItem{2, "Upper", "AA", "Upper case"}, CommandItem{3, "Lower", "aa", "Lower case"}};
        (*mini_bar_).set_items(mini_bar_items_);
        AnchoredOverlayPlacement placement;
        placement.preferred_size = Size{250.0, 36.0};
        placement.vertical_preference = OverlayVerticalPreference::above;
        placement.gap = 4.0;
        mini_layer_ = make_control<AnchoredOverlayLayer>(StableId("gallery.navigation.mini.layer"), editor_, placement);
        (*mini_layer_).set_content(mini_bar_);
        // A click back in the note keeps the toolbar while text stays
        // selected; the selection decides, not the click.
        (*mini_layer_).dismiss_on_click_away = false;
    }

    void on_editor_selection(const TextSelection& selection) {
        const bool selected = selection.anchor != selection.caret;
        if (selected && !mini_token_.connected()) {
            Window* const window = (*editor_).attached_window();
            if (window == nullptr) return;
            const Size client = (*window).client_size();
            (*mini_layer_).set_requested_bounds({0.0, 0.0, client.width, client.height});
            mini_token_ = (*window).open_overlay(editor_, mini_layer_);
        } else if (!selected && mini_token_.connected()) {
            mini_token_.disconnect();
        }
    }

    void on_mini_dismiss(const OverlayDismissReason reason) {
        static_cast<void>(reason);
        mini_token_.disconnect();
    }

    void on_mini_command(const CommandItem& command) {
        const TextSelection selection = (*editor_).selection();
        const std::size_t first = selection.start().value();
        const std::size_t last = std::max(selection.anchor.value(), selection.caret.value());
        const std::string text = (*editor_).text.read();
        if (first >= last || last > text.size()) return;
        std::string part = text.substr(first, last - first);
        if (command.id == 0) part = "**" + part + "**";
        if (command.id == 1) part = "_" + part + "_";
        if (command.id == 2 || command.id == 3) {
            for (char& letter : part) {
                if (command.id == 2 && letter >= 'a' && letter <= 'z') letter = static_cast<char>(letter - 'a' + 'A');
                if (command.id == 3 && letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter - 'A' + 'a');
            }
        }
        (*editor_).text = text.substr(0, first) + part + text.substr(last);
        (*editor_).select(Utf8Offset(first), Utf8Offset(first + part.size()));
        report(command.label + " applied to the selection");
    }

    // -- command search -------------------------------------------------------
    void build_command_search(Surface& body) {
        const std::shared_ptr<LayoutGroup> box =
            group("gallery.navigation.commands", "Command search", {300.0, 260.0});
        body.add_at(box, {24.0, 368.0, 300.0, 260.0});
        command_search_ = make_control<SearchField>(StableId("gallery.navigation.commands.search"));
        (*command_search_).accessible_name = "Search commands";
        (*box).add_at(command_search_, {12.0, 30.0, 276.0, 28.0});
        command_list_ = make_control<ListBox>(StableId("gallery.navigation.commands.list"));
        (*command_list_).accessible_name = "Commands";
        (*box).add_at(command_list_, {12.0, 64.0, 276.0, 184.0});
        on_command_search("");
    }

    void on_command_search(const std::string& query) {
        const std::string wanted = ascii_lower(query);
        shown_commands_.clear();
        std::vector<std::string> lines{};
        for (std::size_t index = 0; index < named_commands.size(); ++index) {
            if (wanted.empty() || ascii_lower(named_commands[index].name).find(wanted) != std::string::npos) {
                shown_commands_.push_back(index);
                lines.push_back(command_line(named_commands[index]));
            }
        }
        (*command_list_).items = std::move(lines);
        if (!shown_commands_.empty()) (*command_list_).select_index(0U);
    }

    void on_command_activated(std::size_t row) {
        if (row >= shown_commands_.size()) return;
        report("Ran \"" + std::string(named_commands[shown_commands_[row]].name) + "\"");
    }

    // -- radial menu ----------------------------------------------------------
    void build_radial(Surface& body) {
        const std::shared_ptr<LayoutGroup> box = group("gallery.navigation.radial", "Radial menu", {300.0, 260.0});
        body.add_at(box, {342.0, 368.0, 300.0, 260.0});
        radial_state_ = gui_forms::detail::make_library_shared<RadialState>();
        radial_ = make_control<DrawingSurface>(StableId("gallery.navigation.radial.surface"));
        (*radial_).accessible_name = "Radial menu area";
        (*radial_).hit_test_visible = true;
        (*radial_).set_paint_callback(RadialPaint(radial_state_));
        (*box).add_at(radial_, {12.0, 30.0, 276.0, 218.0});
    }

    void on_radial_down(const MouseEventArgs& arguments) {
        if (arguments.button != PointerButton::primary) return;
        RadialState& state = *radial_state_;
        if (state.open) {
            // The second click of click-click use: a sector picks, the hub
            // or outside closes.
            const int sector = radial_sector({arguments.location.x - state.centre.x,
                                              arguments.location.y - state.centre.y});
            close_radial(sector);
            return;
        }
        const Rect bounds = (*radial_).client_rectangle();
        state.open = true;
        state.hot = -1;
        state.centre = Point{std::clamp(arguments.location.x, radial_outer, std::max(radial_outer, bounds.width - radial_outer)),
                             std::clamp(arguments.location.y, radial_outer, std::max(radial_outer, bounds.height - radial_outer))};
        radial_pressed_ = true;
        (*radial_).set_pointer_capture(true);
        (*radial_).invalidate(Dirty::paint);
    }

    void on_radial_move(const MouseEventArgs& arguments) {
        RadialState& state = *radial_state_;
        if (!state.open) return;
        const int sector = radial_sector({arguments.location.x - state.centre.x, arguments.location.y - state.centre.y});
        if (sector == state.hot) return;
        state.hot = sector;
        (*radial_).invalidate(Dirty::paint);
    }

    void on_radial_up(const MouseEventArgs& arguments) {
        if (!radial_pressed_) return;
        radial_pressed_ = false;
        (*radial_).set_pointer_capture(false);
        const RadialState& state = *radial_state_;
        const int sector = radial_sector({arguments.location.x - state.centre.x, arguments.location.y - state.centre.y});
        // A release in the hub keeps the menu open for a second click.
        if (sector < 0) {
            report("Radial menu open: click a sector, or the hub to close");
            return;
        }
        close_radial(sector);
    }

    void close_radial(int sector) {
        RadialState& state = *radial_state_;
        state.open = false;
        state.hot = -1;
        (*radial_).invalidate(Dirty::paint);
        if (sector < 0) {
            report("Radial menu closed");
            return;
        }
        report("Radial menu: " + std::string(radial_items[static_cast<std::size_t>(sector)]));
    }

    // -- guided tour ----------------------------------------------------------
    void build_tour_notes(Surface& body) {
        const std::shared_ptr<LayoutGroup> box = group("gallery.navigation.tour", "Guided tour", {316.0, 260.0});
        body.add_at(box, {660.0, 368.0, 316.0, 260.0});
        (*box).add_at(wrapped_label("gallery.navigation.tour.text",
                            "\"Take the tour\" walks a Callout over the five other patterns. Next moves on; "
                            "Escape or the close mark ends the tour.",
                            11.0, 400, "ink"),
                      {12.0, 30.0, 292.0, 80.0});
        tour_progress_ = label("gallery.navigation.tour.progress", "Not started", 11.0, 600, "muted");
        (*box).add_at(tour_progress_, {12.0, 120.0, 292.0, 22.0});
        tour_ = make_control<Callout>(StableId("gallery.navigation.tour.callout"));
    }

    [[nodiscard]] Control::Ptr tour_target(std::size_t step) const {
        if (step == 0U) return accordion_;
        if (step == 1U) return drawer_toggle_;
        if (step == 2U) return editor_;
        if (step == 3U) return command_search_;
        return radial_;
    }

    void on_tour_start() {
        if ((*tour_).is_shown()) static_cast<void>((*tour_).close());
        tour_step_ = 0U;
        show_tour_step();
    }

    void show_tour_step() {
        const TourStep& step = tour_steps[tour_step_];
        (*tour_).title = std::string(step.title) + "  (" + std::to_string(tour_step_ + 1U) + " of " +
                         std::to_string(tour_steps.size()) + ")";
        (*tour_).text = std::string(step.text);
        (*tour_).action_text = tour_step_ + 1U < tour_steps.size() ? "Next" : "Done";
        tour_advancing_ = false;
        if (!(*tour_).show(tour_target(tour_step_))) return;
        (*tour_progress_).text = "Step " + std::to_string(tour_step_ + 1U) + ": " + std::string(step.title);
    }

    void on_tour_next() { tour_advancing_ = true; }

    // The callout has left the overlay: the next step shows from here.
    void on_tour_closed(const CalloutCloseReason reason) {
        static_cast<void>(reason);
        if (!tour_advancing_) {
            (*tour_progress_).text = "Tour ended at step " + std::to_string(tour_step_ + 1U);
            return;
        }
        tour_advancing_ = false;
        if (tour_step_ + 1U >= tour_steps.size()) {
            (*tour_progress_).text = "Tour finished";
            report("Tour finished");
            return;
        }
        ++tour_step_;
        show_tour_step();
    }

    void report(std::string text) { (*status_).text = std::move(text); }

    std::shared_ptr<Surface> body_;
    std::shared_ptr<Label> status_;
    std::unique_ptr<Timer> timer_;

    std::shared_ptr<ExpandableSections> accordion_;
    std::shared_ptr<Button> accordion_mode_;

    std::shared_ptr<LayoutGroup> drawer_group_;
    std::shared_ptr<Button> drawer_toggle_;
    std::shared_ptr<Label> drawer_page_;
    std::shared_ptr<Panel> drawer_;
    std::array<std::shared_ptr<Button>, drawer_items.size()> drawer_buttons_{};
    bool drawer_open_{};
    int drawer_frame_{};

    std::shared_ptr<TextBox> editor_;
    std::shared_ptr<CommandBar> mini_bar_;
    std::vector<CommandItem> mini_bar_items_{};
    std::shared_ptr<AnchoredOverlayLayer> mini_layer_;
    OverlayToken mini_token_;

    std::shared_ptr<SearchField> command_search_;
    std::shared_ptr<ListBox> command_list_;
    std::vector<std::size_t> shown_commands_{};

    std::shared_ptr<RadialState> radial_state_;
    std::shared_ptr<DrawingSurface> radial_;
    bool radial_pressed_{};

    std::shared_ptr<Button> tour_start_;
    std::shared_ptr<Callout> tour_;
    std::shared_ptr<Label> tour_progress_;
    std::size_t tour_step_{};
    bool tour_advancing_{};
};

} // namespace

void add_navigation_patterns(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    const std::shared_ptr<NavigationPatternsPage> owner = gui_forms::detail::make_library_shared<NavigationPatternsPage>(page);
    (*owner).build();
    (*context).components.add(owner);
    (*owner).connect();
}

} // namespace gui_forms::gallery::pages
