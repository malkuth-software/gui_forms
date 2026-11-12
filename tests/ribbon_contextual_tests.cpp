// Ribbon contextual tab sets and application modes (stage RB, phase 5;
// owner decisions 14, 22, 25): availability changes and the built-in
// selection rules, accents on the sets' tabs and plane, mode switches showing
// the right core tabs and groups, semantics of sets, refusals, and scaling
// with contextual tabs present.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/ribbon.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/window.hpp"
#include "support/theme_definition_heap.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

RibbonGroupSpec group(const std::string& id, std::size_t commands) {
    RibbonGroupSpec spec{.stable_id = id, .caption = "Tools"};
    for (std::size_t index = 0; index < commands; ++index) {
        const std::string command_id = id + "." + std::to_string(index);
        spec.commands.push_back({.stable_id = command_id,
                                 .command = std::make_shared<Command>(command_id, "Command"),
                                 .image = ImageId{1U}});
    }
    return spec;
}

RibbonTabSpec tab(const std::string& id, const std::string& text, const std::string& set = {},
                  std::uint32_t modes = ribbon_all_modes) {
    RibbonTabSpec spec{.stable_id = id, .text = text};
    spec.groups.push_back(group(id + ".group", 3U));
    spec.tab_set = set;
    spec.modes = modes;
    return spec;
}

// Home, Insert and View; Picture Tools (Format, Arrange); Table Tools
// (Design); Chart Tools (Chart).
std::vector<RibbonTabSpec> sample_tabs() {
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(tab("home", "&Home"));
    tabs.push_back(tab("insert", "&Insert"));
    tabs.push_back(tab("view", "&View"));
    tabs.push_back(tab("format", "Format", "picture"));
    tabs.push_back(tab("arrange", "Arrange", "picture"));
    tabs.push_back(tab("design", "Design", "table"));
    tabs.push_back(tab("chart-tab", "Chart", "chart"));
    return tabs;
}

std::vector<RibbonTabSetSpec> sample_sets() {
    std::vector<RibbonTabSetSpec> sets{{"picture", "Picture Tools"}, {"table", "Table Tools"},
                                       {"chart", "Chart Tools"}};
    return sets;
}

std::size_t index_of(const Ribbon& ribbon, std::string_view id) {
    const std::span<const RibbonTabSpec> tabs = ribbon.tabs();
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        if (tabs[index].stable_id == id) return index;
    }
    require(false, "no such tab");
    return 0U;
}

std::string selected(const Ribbon& ribbon) {
    if (!ribbon.selected_tab.read()) return "none";
    return ribbon.tabs()[*ribbon.selected_tab.read()].stable_id;
}

std::vector<std::string> shown_tabs(const Ribbon& ribbon) {
    std::vector<std::string> ids;
    for (const Ribbon::Tab& item : ribbon.realized_tabs()) ids.push_back(item.stable_id);
    return ids;
}

// A borrow into `tabs`, which must outlive it: temporaries are refused.
const Ribbon::Tab* realized(const std::vector<Ribbon::Tab>& tabs, std::string_view id) {
    for (const Ribbon::Tab& item : tabs) {
        if (item.stable_id == id) return &item;
    }
    return nullptr;
}
const Ribbon::Tab* realized(std::vector<Ribbon::Tab>&& tabs, std::string_view id) = delete;

// The item `id` names, which must be there: a missing one ends the test
// rather than dereferencing null. A borrow into `items`, as realized is.
const Ribbon::Tab& tab_named(const std::vector<Ribbon::Tab>& items, std::string_view id) {
    const Ribbon::Tab* const found = realized(items, id);
    require(found != nullptr, "the item the test names is realized");
    return *found;
}
const Ribbon::Tab& tab_named(std::vector<Ribbon::Tab>&& items, std::string_view id) = delete;

struct Fixture final {
    std::shared_ptr<Ribbon> ribbon;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(Size size = {1200.0, 300.0}) {
    Fixture fixture;
    fixture.ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*fixture.ribbon).set_tabs(sample_tabs(), sample_sets());
    fixture.window = std::make_unique<Window>(fixture.ribbon, size);
    (*fixture.window).perform_layout();
    return fixture;
}

void click_tab(Fixture& fixture, std::string_view id) {
    const std::vector<Ribbon::Tab> tabs = (*fixture.ribbon).realized_tabs();
    const Ribbon::Tab* item = realized(tabs, id);
    require(item != nullptr, "the tab to click is shown");
    const Rect bounds = (*item).parts.tab.bounds;
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    static_cast<void>((*fixture.window).dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>((*fixture.window).dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
    (*fixture.window).perform_layout();
}

void availability_and_selection_rules() {
    Fixture fixture = make_fixture();
    Ribbon& ribbon = *fixture.ribbon;
    require(shown_tabs(ribbon) == std::vector<std::string>{"home", "insert", "view"} && selected(ribbon) == "home",
            "sets start not available: only the core tabs show, Home selected");
    click_tab(fixture, "insert");
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::available);
    (*fixture.window).perform_layout();
    require(shown_tabs(ribbon) == std::vector<std::string>{"home", "insert", "view", "format", "arrange"} &&
                selected(ribbon) == "insert",
            "an available set shows its tabs after the core tabs and changes no selection");
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::active);
    (*fixture.window).perform_layout();
    require(selected(ribbon) == "format", "a set becoming active selects its first tab");
    click_tab(fixture, "arrange");
    require(selected(ribbon) == "arrange", "the user picks the set's second tab");
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::not_available);
    (*fixture.window).perform_layout();
    require(shown_tabs(ribbon) == std::vector<std::string>{"home", "insert", "view"} && selected(ribbon) == "insert",
            "a set leaving while selected returns to the core tab chosen last");
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::active);
    (*fixture.window).perform_layout();
    require(selected(ribbon) == "arrange", "a returning set reselects the tab last chosen in it");
    click_tab(fixture, "view");
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::not_available);
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::active);
    (*fixture.window).perform_layout();
    require(selected(ribbon) == "format", "choosing a core tab forgets the set's tab: the first tab again");
    ribbon.set_tab_set_availability("table", RibbonTabSetAvailability::active);
    (*fixture.window).perform_layout();
    require(selected(ribbon) == "design", "another set's activation selects its tab");
    ribbon.set_tab_set_availability("table", RibbonTabSetAvailability::not_available);
    (*fixture.window).perform_layout();
    require(selected(ribbon) == "view", "its leaving returns to the core tab chosen last, not to the other set");
    bool threw = false;
    try {
        ribbon.set_tab_set_availability("drawing", RibbonTabSetAvailability::active);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "an unknown set is refused");
    threw = false;
    try {
        ribbon.selected_tab = index_of(ribbon, "design");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && selected(ribbon) == "view", "a hidden tab cannot be selected");

    std::vector<RibbonTabSpec> again = sample_tabs();
    again[0].text = "&Start";
    ribbon.set_tabs(std::move(again), sample_sets());
    (*fixture.window).perform_layout();
    require(ribbon.tab_set_availability("picture") == RibbonTabSetAvailability::active &&
                shown_tabs(ribbon).size() == 5U && selected(ribbon) == "view",
            "sets keep their availability across set_tabs, and the selection its tab");
}

void accents() {
    Fixture fixture = make_fixture();
    Ribbon& ribbon = *fixture.ribbon;
    require(!ribbon.tab_set_accent("picture"), "a hidden set has no accent");
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::active);
    ribbon.set_tab_set_availability("table", RibbonTabSetAvailability::available);
    (*fixture.window).perform_layout();
    const std::optional<AccentIndex> picture = ribbon.tab_set_accent("picture");
    const std::optional<AccentIndex> table = ribbon.tab_set_accent("table");
    require(picture && table && !(*picture == *table), "shown sets take different accents");
    const std::vector<Ribbon::Tab> tabs = ribbon.realized_tabs();
    require(tab_named(tabs, "format").parts.tab.accent == picture &&
                tab_named(tabs, "arrange").parts.tab.accent == picture &&
                tab_named(tabs, "design").parts.tab.accent == table &&
                !tab_named(tabs, "home").parts.tab.accent,
            "a set's tint is on its tabs, and core tabs have none");
    require(ribbon.parts().command_plane.accent == picture, "the selected contextual tab's plane carries its tint");
    click_tab(fixture, "home");
    require(!ribbon.parts().command_plane.accent, "a core tab's plane carries no tint");
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::not_available);
    ribbon.set_tab_set_availability("chart", RibbonTabSetAvailability::available);
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::available);
    const std::optional<AccentIndex> chart = ribbon.tab_set_accent("chart");
    const std::optional<AccentIndex> returned = ribbon.tab_set_accent("picture");
    require(chart && returned && !(*chart == *table) && !(*returned == *table) && !(*returned == *chart),
            "three shown sets have three accents");
    require(*chart == *picture,
            "a newly shown set may take an accent a hidden set gave up, and the returning one takes another");

    Fixture second = make_fixture();
    (*second.ribbon).set_tab_set_availability("table", RibbonTabSetAvailability::available);
    const AccentIndex first_table = *(*second.ribbon).tab_set_accent("table");
    (*second.ribbon).set_tab_set_availability("table", RibbonTabSetAvailability::not_available);
    (*second.ribbon).set_tab_set_availability("table", RibbonTabSetAvailability::available);
    require(*(*second.ribbon).tab_set_accent("table") == first_table, "a returning set keeps its accent when free");
}

// The sets' accents follow the window's theme: a one-colour ramp gives every
// shown set that colour, and a wider ramp gives them different ones again.
void accents_follow_theme() {
    Fixture fixture = make_fixture();
    Ribbon& ribbon = *fixture.ribbon;
    Window& window = *fixture.window;
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::available);
    ribbon.set_tab_set_availability("table", RibbonTabSetAvailability::available);
    window.perform_layout();
    require(!(*ribbon.tab_set_accent("picture") == *ribbon.tab_set_accent("table")),
            "shown sets take different accents");
    const std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    (*held).id = "one-accent";
    (*held).accent_ramp = {Color::rgba(200, 40, 40)};
    window.set_theme(Theme::create(*held));
    window.perform_layout();
    require((*ribbon.tab_set_accent("picture")).value == 0U && (*ribbon.tab_set_accent("table")).value == 0U,
            "on a one-colour ramp every shown set takes that colour");
    window.set_theme(default_theme());
    window.perform_layout();
    require(!(*ribbon.tab_set_accent("picture") == *ribbon.tab_set_accent("table")),
            "a wider ramp gives the shown sets different accents again");
}

void modes() {
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(tab("home", "&Home", {}, 0x1U));
    tabs.push_back(tab("edit", "&Edit", {}, 0x1U));
    tabs.push_back(tab("print", "&Print preview", {}, 0x2U));
    tabs.push_back(tab("view", "&View"));
    RibbonGroupSpec extra = group("view.extra", 3U);
    extra.modes = 0x2U;
    tabs[3].groups.push_back(std::move(extra));
    tabs.push_back(tab("format", "Format", "picture", 0x1U));
    std::vector<RibbonTabSetSpec> sets{{"picture", "Picture Tools"}};
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*ribbon).set_tabs(std::move(tabs), std::move(sets));
    Window window(ribbon, {1200.0, 300.0});
    window.perform_layout();
    require((*ribbon).application_modes.read() == 0x1U &&
                shown_tabs(*ribbon) == std::vector<std::string>{"home", "edit", "view"},
            "mode 0 shows the core tabs that belong to it, and those in every mode");
    (*ribbon).selected_tab = index_of(*ribbon, "edit");
    (*ribbon).set_tab_set_availability("picture", RibbonTabSetAvailability::active);
    window.perform_layout();
    require(selected(*ribbon) == "format", "a set shows in its modes");
    (*ribbon).application_modes = 0x2U;
    window.perform_layout();
    require(shown_tabs(*ribbon) == std::vector<std::string>{"print", "view"} && selected(*ribbon) == "print",
            "switching modes replaces the core tabs; a hidden selection falls back to the first core tab shown");
    (*ribbon).selected_tab = index_of(*ribbon, "view");
    window.perform_layout();
    require((*ribbon).realized_groups().size() == 2U, "a group in the new mode shows on its tab");
    const std::vector<RibbonLadderEntry> both = (*ribbon).reduction_ladder(index_of(*ribbon, "view"));
    (*ribbon).application_modes = 0x1U;
    window.perform_layout();
    const std::vector<RibbonLadderEntry> one = (*ribbon).reduction_ladder(index_of(*ribbon, "view"));
    require((*ribbon).realized_groups().size() == 1U && one[0].width < both[0].width && one.size() < both.size(),
            "a group outside the modes takes no room and no ladder steps");
    require(shown_tabs(*ribbon) == std::vector<std::string>{"home", "edit", "view", "format"} &&
                selected(*ribbon) == "view",
            "returning to mode 0 brings back its tabs, and the active set");
    (*ribbon).application_modes = 0x3U;
    window.perform_layout();
    require(shown_tabs(*ribbon).size() == 5U, "several modes show the union");
    bool threw = false;
    try {
        (*ribbon).application_modes = 0U;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*ribbon).application_modes.read() == 0x3U, "an empty mode set is refused");
}

void semantics_and_keyboard() {
    Fixture fixture = make_fixture();
    Ribbon& ribbon = *fixture.ribbon;
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::available);
    ribbon.set_tab_set_availability("chart", RibbonTabSetAvailability::available);
    (*fixture.window).perform_layout();
    const std::vector<SemanticNode> nodes = ribbon.semantic_virtual_children();
    const SemanticNode& row = nodes[1];
    require(row.children.size() == 6U, "the tab row lists the shown tabs only");
    require(row.children[2].name == "View" && row.children[2].position_in_set == std::size_t{3U} &&
                row.children[2].set_size == std::size_t{3U} && row.children[2].description.empty(),
            "core tabs count among the core tabs");
    require(row.children[4].name == "Arrange" && row.children[4].position_in_set == std::size_t{2U} &&
                row.children[4].set_size == std::size_t{2U} && row.children[4].description == "Picture Tools",
            "a contextual tab counts among its set and is described by the set's caption");
    require(row.children[5].name == "Chart" && row.children[5].position_in_set == std::size_t{1U} &&
                row.children[5].set_size == std::size_t{1U},
            "each set counts its own tabs");
    require(!ribbon.on_semantic_child_action("ribbon.tab.design", SemanticAction::select, {}),
            "a hidden tab is not selectable through accessibility");
    require(ribbon.on_semantic_child_action("ribbon.tab.format", SemanticAction::select, {}) &&
                selected(ribbon) == "format",
            "a shown contextual tab is");
    require((*fixture.window).request_focus(fixture.ribbon), "the ribbon takes focus");
    for (int step = 0; step < 3; ++step) {
        KeyEvent right{KeyAction::down, PhysicalKey::right};
        static_cast<void>((*fixture.window).dispatch_key(right));
    }
    require(selected(ribbon) == "chart-tab", "Right moves along the shown tabs, skipping hidden ones");
    KeyEvent right{KeyAction::down, PhysicalKey::right};
    static_cast<void>((*fixture.window).dispatch_key(right));
    require(selected(ribbon) == "chart-tab", "and stops at the last shown tab");
}

// Owner decision 46: the command plane's expand and collapse actions
// minimize and restore it, as a click on the selected tab does.
void plane_expand_collapse() {
    Fixture fixture = make_fixture();
    Ribbon& ribbon = *fixture.ribbon;
    std::vector<SemanticNode> nodes = ribbon.semantic_virtual_children();
    require(nodes.size() == 3U && nodes[2].stable_id == "ribbon.command-plane" &&
                has_semantic_state(nodes[2].states, SemanticState::expanded) &&
                nodes[2].actions == std::vector<SemanticAction>{SemanticAction::collapse},
            "the shown plane is expanded and offers collapse");
    require(ribbon.on_semantic_child_action("ribbon.command-plane", SemanticAction::collapse, {}) &&
                ribbon.minimized.read(),
            "collapse minimizes the plane");
    (*fixture.window).perform_layout();
    nodes = ribbon.semantic_virtual_children();
    require(nodes.size() == 3U && !has_semantic_state(nodes[2].states, SemanticState::expanded) &&
                nodes[2].actions == std::vector<SemanticAction>{SemanticAction::expand} && nodes[2].children.empty(),
            "the minimized plane stays published, collapsed, offering expand");
    require(ribbon.on_semantic_child_action("ribbon.command-plane", SemanticAction::expand, {}) && !ribbon.minimized.read(),
            "expand restores it");
    (*fixture.window).perform_layout();
    click_tab(fixture, "home");
    require(ribbon.minimized.read(), "the pointer path still minimizes");
    nodes = ribbon.semantic_virtual_children();
    require(nodes[2].actions == std::vector<SemanticAction>{SemanticAction::expand},
            "and the plane's state follows it");
    click_tab(fixture, "home");
    require(!ribbon.minimized.read(), "and restores");
}

void refusals() {
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    struct Case final {
        const char* what;
        std::vector<RibbonTabSpec> tabs;
        std::vector<RibbonTabSetSpec> sets;
    };
    std::vector<Case> cases;
    {
        Case undeclared{"a tab naming an undeclared set", sample_tabs(), {{"picture", "P"}, {"table", "T"}}};
        cases.push_back(std::move(undeclared));
    }
    {
        Case core_after{"a core tab after a contextual one", sample_tabs(), sample_sets()};
        core_after.tabs.push_back(tab("late", "Late"));
        cases.push_back(std::move(core_after));
    }
    {
        Case interleaved{"a set's tabs apart", sample_tabs(), sample_sets()};
        std::swap(interleaved.tabs[4], interleaved.tabs[5]);
        cases.push_back(std::move(interleaved));
    }
    {
        Case out_of_order{"sets out of their order", sample_tabs(), {{"table", "T"}, {"picture", "P"}, {"chart", "C"}}};
        cases.push_back(std::move(out_of_order));
    }
    {
        Case empty_set{"a set without tabs", sample_tabs(), sample_sets()};
        empty_set.sets.push_back({"drawing", "Drawing Tools"});
        cases.push_back(std::move(empty_set));
    }
    {
        Case shared_id{"a set ID shared with a tab", sample_tabs(), sample_sets()};
        shared_id.sets[0].stable_id = "home";
        for (RibbonTabSpec& item : shared_id.tabs) {
            if (item.tab_set == "picture") item.tab_set = "home";
        }
        cases.push_back(std::move(shared_id));
    }
    {
        Case no_modes{"a tab in no mode", sample_tabs(), sample_sets()};
        no_modes.tabs[1].modes = 0U;
        cases.push_back(std::move(no_modes));
    }
    {
        Case group_modes{"a group in no mode", sample_tabs(), sample_sets()};
        group_modes.tabs[1].groups[0].modes = 0U;
        cases.push_back(std::move(group_modes));
    }
    for (Case& item : cases) {
        bool threw = false;
        try {
            (*ribbon).set_tabs(std::move(item.tabs), std::move(item.sets));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        if (!threw) std::cerr << item.what << '\n';
        require(threw && (*ribbon).tabs().empty(), "invalid sets and modes are refused, keeping the previous tabs");
    }
}

void scaling_with_contextual_tabs() {
    Fixture fixture = make_fixture({1200.0, 300.0});
    Ribbon& ribbon = *fixture.ribbon;
    const std::vector<RibbonLadderEntry> home = ribbon.reduction_ladder(0U);
    ribbon.set_tab_set_availability("picture", RibbonTabSetAvailability::active);
    ribbon.set_tab_set_availability("table", RibbonTabSetAvailability::available);
    (*fixture.window).perform_layout();
    const std::size_t format = index_of(ribbon, "format");
    const std::vector<RibbonLadderEntry> ladder = ribbon.reduction_ladder(format);
    require(ladder.size() == 3U && ladder.back().groups[0].size == RibbonGroupSize::collapsed,
            "a contextual tab has its own ladder, built from its groups");
    (*fixture.window).resize({ladder.back().width, 300.0});
    (*fixture.window).perform_layout();
    require(ribbon.ladder_step() == std::size_t{2U} && ribbon.realized_groups()[0].collapsed &&
                ribbon.parts().command_plane.accent == ribbon.tab_set_accent("picture"),
            "a contextual tab's plane scales and keeps its tint");
    (*fixture.window).resize({ladder.back().width - 10.0, 300.0});
    (*fixture.window).perform_layout();
    require(ribbon.scrolling() && !ribbon.plane_hidden(), "and scrolls below its narrowest entry");
    ribbon.selected_tab = 0U;
    (*fixture.window).resize({home[0].width, 300.0});
    (*fixture.window).perform_layout();
    const std::vector<RibbonLadderEntry> home_again = ribbon.reduction_ladder(0U);
    require(home_again.size() == home.size() && home_again[0].width == home[0].width &&
                ribbon.ladder_step() == std::size_t{0U},
            "showing sets on the tab row leaves the core tab's ladder unchanged");
}

} // namespace

int main() {
    availability_and_selection_rules();
    accents();
    accents_follow_theme();
    modes();
    semantics_and_keyboard();
    plane_expand_collapse();
    refusals();
    scaling_with_contextual_tabs();
    std::cout << "Ribbon contextual tabs: availability and selection rules, accents, modes, semantics and keyboard, "
                 "refusals and scaling passed\n";
    return 0;
}
