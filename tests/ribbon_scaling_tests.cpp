// Ribbon scaling (stage RB, phase 2b; owner decisions 5 to 8, 27 to 36):
// group layouts and the reduction ladder (authored order, the default WPF
// cycle, refusals), gallery column steps, golden layouts at a width ladder at
// text scales 1 and 1.5, the WordPad ladders observed in phase 2a, scrolling
// at any width, the document-area hide rule with its hysteresis, collapsed
// group buttons and their overlays, and the measurement caches.
//
// Text is measured by FixedMetrics (half the font size per byte), so the
// golden numbers depend on no font engine.

#include "gui_forms/gui_forms.hpp"
#include "support/theme_definition_heap.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
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

// Deterministic text metrics: each byte is half the font size wide.
class FixedMetrics final : public TextMetricsProvider {
public:
    ResolvedTextLayout resolve_text_layout_utf8(std::string_view utf8, FontSpec font) override {
        ++calls;
        ResolvedTextLayout layout{};
        layout.effective_font = font;
        layout.logical_size = {static_cast<double>(utf8.size()) * font.size * 0.5, font.size * 1.3};
        layout.ascent = font.size;
        layout.descent = font.size * 0.3;
        return layout;
    }
    std::uint64_t calls{};
};

class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color) override { texts.emplace_back(text); }
    void draw_image(ImageId, Rect, double) override {}

    std::vector<std::string> texts;
};

bool painted(const NullPainter& painter, std::string_view text) {
    for (const std::string& seen : painter.texts) {
        if (seen == text) return true;
    }
    return false;
}

struct CountInvocation final {
    int* count{};
    void operator()(const CommandInvocation&) const { ++*count; }
};

std::shared_ptr<Command> command(const std::string& id, const std::string& text) {
    std::shared_ptr<Command> made = std::make_shared<Command>(id, text);
    return made;
}

RibbonCommandSpec large_button(const std::string& id, const std::string& text, std::uint32_t image = 1U) {
    RibbonCommandSpec spec{.stable_id = id, .command = command(id, text), .image = ImageId{image}};
    return spec;
}

RibbonCommandSpec small_button(const std::string& id, const std::string& text, std::uint32_t image = 1U) {
    RibbonCommandSpec spec{.stable_id = id, .size = RibbonCommandSize::small, .command = command(id, text),
                           .image = ImageId{image}};
    return spec;
}

RibbonCommandSpec icon_toggle(const std::string& id, const std::string& text, std::uint32_t image = 2U) {
    RibbonCommandSpec spec{.stable_id = id, .kind = RibbonCommandKind::toggle, .size = RibbonCommandSize::small,
                           .command = command(id, text), .image = ImageId{image}, .show_label = false};
    return spec;
}

RibbonCommandSpec icon_menu(const std::string& id, const std::string& text) {
    RibbonCommandSpec spec{.stable_id = id, .kind = RibbonCommandKind::drop_down, .size = RibbonCommandSize::small,
                           .text = text, .image = ImageId{3U}, .show_label = false,
                           .drop_down = {MenuItemSpec(id + ".item", MenuItemKind::command, command(id + ".c", "Item"),
                                                      "Item")}};
    return spec;
}

RibbonGroupLayout layout_of(RibbonGroupSize size, std::size_t rows = 3U) {
    RibbonGroupLayout layout{.size = size, .command_sizes = {}, .rows = rows};
    return layout;
}

// WordPad's Home tab (notes:results/2026-10-14-ribbon-scaling-observation/), with
// WordPad's authored policy as far as decision 6 allows: no label-drop
// layout; each group goes from its largest layout to its button, in the
// order observed (Insert, Editing, Paragraph, Clipboard, Font).
RibbonTabSpec wordpad_home(bool authored_order) {
    RibbonTabSpec home{.stable_id = "home", .text = "&Home"};
    RibbonGroupSpec clipboard{.stable_id = "clipboard", .caption = "Clipboard"};
    RibbonCommandSpec paste = large_button("paste", "Paste", 10U);
    paste.kind = RibbonCommandKind::split;
    paste.drop_down = {MenuItemSpec("paste.special", MenuItemKind::command, command("special", "Special"),
                                    "Paste special")};
    clipboard.commands.push_back(paste);
    clipboard.commands.push_back(small_button("cut", "Cut", 11U));
    clipboard.commands.push_back(small_button("copy", "Copy", 12U));
    RibbonGroupSpec font{.stable_id = "font", .caption = "Font", .dialog_launcher = true};
    font.commands.push_back({.stable_id = "font-family", .kind = RibbonCommandKind::combo_box, .text = "Font family",
                             .choices = {"Portsmouth", "Carlito"}, .width = 120.0});
    font.commands.push_back({.stable_id = "font-size", .kind = RibbonCommandKind::combo_box, .text = "Font size",
                             .choices = {"10", "12"}, .width = 40.0});
    font.commands.push_back(icon_toggle("bold", "Bold"));
    font.commands.push_back(icon_toggle("italic", "Italic"));
    font.commands.push_back(icon_toggle("underline", "Underline"));
    font.commands.push_back(icon_toggle("strike", "Strikethrough"));
    font.commands.push_back(icon_toggle("subscript", "Subscript"));
    font.commands.push_back(icon_toggle("superscript", "Superscript"));
    font.commands.push_back(icon_menu("text-color", "Text color"));
    font.commands.push_back(icon_menu("highlight", "Text highlight color"));
    RibbonGroupSpec paragraph{.stable_id = "paragraph", .caption = "Paragraph", .dialog_launcher = true};
    paragraph.commands.push_back(icon_toggle("outdent", "Decrease indent"));
    paragraph.commands.push_back(icon_toggle("indent", "Increase indent"));
    paragraph.commands.push_back(icon_menu("lists", "Lists"));
    paragraph.commands.push_back(icon_menu("spacing", "Line spacing"));
    paragraph.commands.push_back(icon_toggle("left", "Align left"));
    paragraph.commands.push_back(icon_toggle("center", "Center"));
    paragraph.commands.push_back(icon_toggle("right", "Align right"));
    paragraph.commands.push_back(icon_toggle("justify", "Justify"));
    RibbonGroupSpec insert{.stable_id = "insert", .caption = "Insert"};
    insert.commands.push_back(large_button("picture", "Picture", 20U));
    insert.commands.push_back(large_button("date-time", "Date and time", 21U));
    insert.commands.push_back(large_button("object", "Insert object", 22U));
    RibbonGroupSpec editing{.stable_id = "editing", .caption = "Editing"};
    editing.commands.push_back(small_button("find", "Find", 30U));
    editing.commands.push_back(small_button("replace", "Replace", 31U));
    editing.commands.push_back(small_button("select-all", "Select all", 32U));
    if (authored_order) {
        clipboard.layouts = {layout_of(RibbonGroupSize::large), layout_of(RibbonGroupSize::collapsed)};
        font.layouts = {layout_of(RibbonGroupSize::large, 2U), layout_of(RibbonGroupSize::collapsed)};
        paragraph.layouts = {layout_of(RibbonGroupSize::large, 2U), layout_of(RibbonGroupSize::collapsed)};
        insert.layouts = {layout_of(RibbonGroupSize::large), layout_of(RibbonGroupSize::collapsed)};
        editing.layouts = {layout_of(RibbonGroupSize::large), layout_of(RibbonGroupSize::collapsed)};
        home.reduction_order = {{"insert"}, {"editing"}, {"paragraph"}, {"clipboard"}, {"font"}};
    }
    home.groups.push_back(std::move(clipboard));
    home.groups.push_back(std::move(font));
    home.groups.push_back(std::move(paragraph));
    home.groups.push_back(std::move(insert));
    home.groups.push_back(std::move(editing));
    return home;
}

// WordPad's View tab: Zoom's three large buttons become small labelled rows,
// then each group collapses (Zoom, Show or hide, Settings).
RibbonTabSpec wordpad_view() {
    RibbonTabSpec view{.stable_id = "view", .text = "&View"};
    RibbonGroupSpec zoom{.stable_id = "zoom", .caption = "Zoom"};
    zoom.commands.push_back(large_button("zoom-in", "Zoom in", 40U));
    zoom.commands.push_back(large_button("zoom-out", "Zoom out", 41U));
    zoom.commands.push_back(large_button("zoom-100", "100%", 42U));
    RibbonGroupLayout medium = layout_of(RibbonGroupSize::medium);
    medium.command_sizes = {RibbonCommandSize::small, RibbonCommandSize::small, RibbonCommandSize::small};
    zoom.layouts = {layout_of(RibbonGroupSize::large), medium, layout_of(RibbonGroupSize::collapsed)};
    RibbonGroupSpec show{.stable_id = "show-hide", .caption = "Show or hide"};
    show.commands.push_back({.stable_id = "ruler", .kind = RibbonCommandKind::check_box, .text = "Ruler"});
    show.commands.push_back({.stable_id = "status-bar", .kind = RibbonCommandKind::check_box, .text = "Status bar"});
    show.layouts = {layout_of(RibbonGroupSize::large), layout_of(RibbonGroupSize::collapsed)};
    RibbonGroupSpec settings{.stable_id = "settings", .caption = "Settings"};
    RibbonCommandSpec wrap = icon_menu("word-wrap", "Word wrap");
    wrap.show_label = true;
    RibbonCommandSpec units = icon_menu("units", "Measurement units");
    units.show_label = true;
    settings.commands.push_back(wrap);
    settings.commands.push_back(units);
    settings.layouts = {layout_of(RibbonGroupSize::large), layout_of(RibbonGroupSize::collapsed)};
    view.reduction_order = {{"zoom"}, {"zoom"}, {"show-hide"}, {"settings"}};
    view.groups.push_back(std::move(zoom));
    view.groups.push_back(std::move(show));
    view.groups.push_back(std::move(settings));
    return view;
}

// Three groups of three large buttons each: every group has generated
// layouts large, medium (all small) and collapsed.
RibbonTabSpec three_groups(std::vector<RibbonReductionStep> order) {
    RibbonTabSpec tab{.stable_id = "tab", .text = "Tab"};
    const char* const names[3] = {"first", "second", "third"};
    for (std::size_t index = 0; index < 3U; ++index) {
        const std::string name = names[index];
        RibbonGroupSpec group{.stable_id = name, .caption = name};
        group.commands.push_back(large_button(name + ".a", "Alpha"));
        group.commands.push_back(large_button(name + ".b", "Beta"));
        group.commands.push_back(large_button(name + ".c", "Gamma"));
        tab.groups.push_back(std::move(group));
    }
    tab.reduction_order = std::move(order);
    return tab;
}

const char* size_name(RibbonGroupSize size) {
    if (size == RibbonGroupSize::large) return "large";
    if (size == RibbonGroupSize::medium) return "medium";
    if (size == RibbonGroupSize::small) return "small";
    return "collapsed";
}

// What changed between neighbouring ladder entries: "<group>:<size>" for a
// layout step, "<group>:<columns> columns" for a gallery step, several
// changes of one entry joined by "+".
std::vector<std::string> transitions(const Ribbon& ribbon, std::size_t tab) {
    const std::vector<RibbonLadderEntry> ladder = ribbon.reduction_ladder(tab);
    const std::vector<RibbonGroupSpec>& groups = ribbon.tabs()[tab].groups;
    std::vector<std::string> result;
    for (std::size_t entry = 1; entry < ladder.size(); ++entry) {
        std::string changes;
        for (std::size_t group = 0; group < groups.size(); ++group) {
            const RibbonLadderGroupState& before = ladder[entry - 1U].groups[group];
            const RibbonLadderGroupState& after = ladder[entry].groups[group];
            if (before.layout != after.layout) {
                if (!changes.empty()) changes += "+";
                changes += groups[group].stable_id + ":" + size_name(after.size);
            }
            if (before.gallery_columns != after.gallery_columns) {
                if (!changes.empty()) changes += "+";
                changes += groups[group].stable_id + ":" + std::to_string(after.gallery_columns) + " columns";
            }
        }
        result.push_back(changes);
    }
    return result;
}

// `value` rounded half away from zero to hundredths, so that no C library's
// tie rounding changes a golden line.
std::string number(double value) {
    const double rounded = std::round(value * 100.0) / 100.0;
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << rounded;
    return text.str();
}

// One line describing the plane: the ladder step, scrolling, the first group
// and every shown group as "<id> L<layout>|C x+width".
std::string describe(const Ribbon& ribbon) {
    std::string line = "step=";
    const std::optional<std::size_t> step = ribbon.ladder_step();
    line += step ? std::to_string(*step) : std::string("none");
    line += ribbon.scrolling() ? " scroll first=" + std::to_string(ribbon.first_visible_group()) : "";
    for (const Ribbon::Group& group : ribbon.realized_groups()) {
        line += " " + group.stable_id + " " + (group.collapsed ? std::string("C") : "L" + std::to_string(group.layout));
        line += " " + number(group.parts.group_region.bounds.x) + "+" + number(group.parts.group_region.bounds.width);
    }
    return line;
}

// Compares golden lines; on a difference prints the actual lines in source
// form and fails (with RIBBON_GOLDEN_PRINT set it only prints, to regenerate
// every golden table in one run).
void check_lines(const std::vector<std::string>& actual, const std::vector<std::string>& expected, const char* what) {
    if (actual == expected) return;
    std::cerr << what << ": expected lines differ; actual:\n";
    for (const std::string& line : actual) std::cerr << "        \"" << line << "\",\n";
    if (std::getenv("RIBBON_GOLDEN_PRINT") == nullptr) std::exit(1);
}

// A ribbon of `tabs` in a window of `size` measuring with `metrics`.
struct Fixture final {
    std::shared_ptr<Ribbon> ribbon;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(std::vector<RibbonTabSpec> tabs, Size size, FixedMetrics& metrics, double text_scale = 1.0) {
    Fixture fixture;
    fixture.ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*fixture.ribbon).set_tabs(std::move(tabs));
    fixture.window = std::make_unique<Window>(fixture.ribbon, size);
    (*fixture.window).set_text_metrics_provider(&metrics);
    if (text_scale != 1.0) (*fixture.window).set_text_scale(text_scale);
    (*fixture.window).perform_layout();
    return fixture;
}

void resize(Fixture& fixture, Size size) {
    (*fixture.window).resize(size);
    (*fixture.window).perform_layout();
}

bool refused(Ribbon& ribbon, std::vector<RibbonTabSpec> tabs) {
    try {
        ribbon.set_tabs(std::move(tabs));
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// -- tests ---------------------------------------------------------------------

void generated_layouts() {
    FixedMetrics metrics;
    std::vector<RibbonTabSpec> tabs;
    RibbonTabSpec tab = three_groups({});
    RibbonGroupSpec mixed{.stable_id = "mixed", .caption = "Mixed"};
    mixed.commands.push_back(large_button("m.a", "Alpha"));
    mixed.commands.push_back(small_button("m.b", "Beta"));
    mixed.commands.push_back(large_button("m.c", "Gamma"));
    mixed.commands.push_back(large_button("m.d", "Delta"));
    mixed.commands.push_back(large_button("m.e", "Epsilon"));
    mixed.commands.push_back(large_button("m.f", "Zeta"));
    tab.groups.push_back(std::move(mixed));
    RibbonGroupSpec single{.stable_id = "single", .caption = "Single"};
    single.commands.push_back(large_button("s.a", "Alone"));
    tab.groups.push_back(std::move(single));
    tabs.push_back(std::move(tab));
    Fixture fixture = make_fixture(std::move(tabs), {2000.0, 300.0}, metrics);
    const Ribbon& ribbon = *fixture.ribbon;
    const std::span<const RibbonGroupLayout> three = ribbon.group_layouts(0U, 0U);
    require(three.size() == 3U && three[0].size == RibbonGroupSize::large &&
                three[1].size == RibbonGroupSize::medium && three[1].command_sizes.size() == 3U &&
                three[1].command_sizes[0] == RibbonCommandSize::small &&
                three[2].size == RibbonGroupSize::collapsed,
            "three large commands generate large, medium (all small) and collapsed layouts");
    const std::span<const RibbonGroupLayout> mixed_layouts = ribbon.group_layouts(0U, 3U);
    // Alpha L, Beta S, Gamma to Zeta L: Delta to Zeta shrink first, then
    // Gamma (its run stops at the small Beta), then Alpha, then collapsed.
    require(mixed_layouts.size() == 5U && mixed_layouts[1].command_sizes[2] == RibbonCommandSize::large &&
                mixed_layouts[1].command_sizes[3] == RibbonCommandSize::small &&
                mixed_layouts[1].command_sizes[5] == RibbonCommandSize::small &&
                mixed_layouts[2].size == RibbonGroupSize::small &&
                mixed_layouts[2].command_sizes[0] == RibbonCommandSize::large &&
                mixed_layouts[2].command_sizes[2] == RibbonCommandSize::small &&
                mixed_layouts[3].command_sizes[0] == RibbonCommandSize::small &&
                mixed_layouts[4].size == RibbonGroupSize::collapsed,
            "the rightmost run of up to three large commands shrinks first, then the next run");
    const std::span<const RibbonGroupLayout> single_layouts = ribbon.group_layouts(0U, 4U);
    require(single_layouts.size() == 1U, "a group of one command never shrinks or collapses");
}

void default_cycle() {
    FixedMetrics metrics;
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(three_groups({}));
    Fixture fixture = make_fixture(std::move(tabs), {1200.0, 300.0}, metrics);
    check_lines(transitions(*fixture.ribbon, 0U),
                {"third:medium", "second:medium", "first:medium", "third:collapsed", "second:collapsed",
                 "first:collapsed"},
                "the default order is WPF's cycle: right to left, one step per group, repeating");
    const std::vector<RibbonLadderEntry> ladder = (*fixture.ribbon).reduction_ladder(0U);
    for (std::size_t entry = 1; entry < ladder.size(); ++entry) {
        require(ladder[entry].width < ladder[entry - 1U].width, "ladder widths strictly decrease");
    }
}

void authored_order() {
    FixedMetrics metrics;
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(three_groups({{"first"}, {"first"}}));
    Fixture fixture = make_fixture(std::move(tabs), {1200.0, 300.0}, metrics);
    check_lines(transitions(*fixture.ribbon, 0U),
                {"first:medium", "first:collapsed", "third:medium", "second:medium", "third:collapsed",
                 "second:collapsed"},
                "the authored order is followed step by step, then the cycle shrinks the rest");
}

void refusals() {
    FixedMetrics metrics;
    std::vector<RibbonTabSpec> good;
    good.push_back(three_groups({}));
    Fixture fixture = make_fixture(std::move(good), {1200.0, 300.0}, metrics);
    Ribbon& ribbon = *fixture.ribbon;
    std::vector<RibbonTabSpec> unknown;
    unknown.push_back(three_groups({{"fourth"}}));
    require(refused(ribbon, std::move(unknown)), "a reduction step naming an unknown group is refused");
    std::vector<RibbonTabSpec> past;
    past.push_back(three_groups({{"first"}, {"first"}, {"first"}}));
    require(refused(ribbon, std::move(past)), "shrinking a group past its last layout is refused");
    std::vector<RibbonTabSpec> no_gallery;
    no_gallery.push_back(three_groups({{"first", RibbonReductionKind::gallery_column}}));
    require(refused(ribbon, std::move(no_gallery)), "a gallery step on a group without a gallery is refused");
    std::vector<RibbonTabSpec> other_tab;
    other_tab.push_back(three_groups({}));
    RibbonTabSpec second{.stable_id = "second-tab", .text = "Second"};
    RibbonGroupSpec lone{.stable_id = "lone", .caption = "Lone"};
    lone.commands.push_back(large_button("lone.a", "Alpha"));
    lone.commands.push_back(large_button("lone.b", "Beta"));
    second.groups.push_back(std::move(lone));
    second.reduction_order = {{"first"}};
    other_tab.push_back(std::move(second));
    require(refused(ribbon, std::move(other_tab)), "a step naming a group on another tab is refused");

    std::vector<RibbonTabSpec> not_last;
    not_last.push_back(three_groups({}));
    not_last[0].groups[0].layouts = {layout_of(RibbonGroupSize::large), layout_of(RibbonGroupSize::collapsed),
                                     layout_of(RibbonGroupSize::collapsed)};
    require(refused(ribbon, std::move(not_last)), "a collapsed layout that is not last is refused");
    std::vector<RibbonTabSpec> growing;
    growing.push_back(three_groups({}));
    growing[0].groups[0].layouts = {layout_of(RibbonGroupSize::medium), layout_of(RibbonGroupSize::large)};
    require(refused(ribbon, std::move(growing)), "layouts that grow are refused");
    std::vector<RibbonTabSpec> count;
    count.push_back(three_groups({}));
    RibbonGroupLayout short_sizes = layout_of(RibbonGroupSize::medium);
    short_sizes.command_sizes = {RibbonCommandSize::small};
    count[0].groups[0].layouts = {layout_of(RibbonGroupSize::large), short_sizes};
    require(refused(ribbon, std::move(count)), "a layout without one size per command is refused");
    std::vector<RibbonTabSpec> rows;
    rows.push_back(three_groups({}));
    rows[0].groups[0].layouts = {layout_of(RibbonGroupSize::large, 4U)};
    require(refused(ribbon, std::move(rows)), "rows outside 1 to 3 are refused");
    std::vector<RibbonTabSpec> single;
    single.push_back(three_groups({}));
    single[0].groups[0].commands.resize(1U);
    single[0].groups[0].layouts = {layout_of(RibbonGroupSize::large), layout_of(RibbonGroupSize::collapsed)};
    require(refused(ribbon, std::move(single)), "a group of one command may not collapse");
    std::vector<RibbonTabSpec> icon_large;
    icon_large.push_back(three_groups({}));
    icon_large[0].groups[0].commands.push_back(icon_toggle("first.icon", "Icon"));
    RibbonGroupLayout makes_large = layout_of(RibbonGroupSize::large);
    makes_large.command_sizes.assign(4U, RibbonCommandSize::large);
    icon_large[0].groups[0].layouts = {makes_large};
    require(refused(ribbon, std::move(icon_large)), "an icon-only command may not be large in a layout");

    std::shared_ptr<RibbonGallery> gallery = make_control<RibbonGallery>(StableId("styles"));
    (*gallery).set_column_range(3U, 6U);
    std::vector<RibbonTabSpec> no_control;
    no_control.push_back(three_groups({}));
    no_control[0].groups[0].commands.push_back({.stable_id = "gallery", .kind = RibbonCommandKind::gallery});
    require(refused(ribbon, std::move(no_control)), "a gallery command without a gallery is refused");
    std::vector<RibbonTabSpec> twice;
    twice.push_back(three_groups({}));
    twice[0].groups[0].commands.push_back({.stable_id = "g1", .kind = RibbonCommandKind::gallery, .gallery = gallery});
    twice[0].groups[1].commands.push_back({.stable_id = "g2", .kind = RibbonCommandKind::gallery, .gallery = gallery});
    require(refused(ribbon, std::move(twice)), "a gallery used twice is refused");
    std::vector<RibbonTabSpec> past_minimum;
    past_minimum.push_back(three_groups({{"first", RibbonReductionKind::gallery_column},
                                         {"first", RibbonReductionKind::gallery_column},
                                         {"first", RibbonReductionKind::gallery_column},
                                         {"first", RibbonReductionKind::gallery_column}}));
    past_minimum[0].groups[0].commands.push_back(
        {.stable_id = "g1", .kind = RibbonCommandKind::gallery, .gallery = gallery});
    require(refused(ribbon, std::move(past_minimum)), "shrinking a gallery past its minimum columns is refused");
    std::shared_ptr<RibbonGallery> drop_down =
        make_control<RibbonGallery>(StableId("drop"), RibbonGalleryForm::drop_down);
    std::vector<RibbonTabSpec> wrong_form;
    wrong_form.push_back(three_groups({}));
    wrong_form[0].groups[0].commands.push_back(
        {.stable_id = "g1", .kind = RibbonCommandKind::gallery, .gallery = drop_down});
    require(refused(ribbon, std::move(wrong_form)), "a drop-down gallery cannot sit in the ribbon");
    require(ribbon.tabs().size() == 1U && ribbon.tabs()[0].reduction_order.empty() &&
                ribbon.group_layouts(0U, 0U).size() == 3U,
            "every refusal keeps the previous tabs");
}

void gallery_columns() {
    FixedMetrics metrics;
    std::shared_ptr<RibbonGallery> gallery = make_control<RibbonGallery>(StableId("styles"));
    (*gallery).item_size = Size{40.0, 20.0};
    (*gallery).set_column_range(3U, 6U);
    std::vector<RibbonGalleryItem> items;
    for (int index = 0; index < 12; ++index) {
        items.push_back({.stable_id = "style" + std::to_string(index), .label = "Style", .image = ImageId{5U}});
    }
    (*gallery).set_items({}, std::move(items));
    RibbonTabSpec tab{.stable_id = "tab", .text = "Tab"};
    RibbonGroupSpec styles{.stable_id = "styles", .caption = "Styles"};
    styles.commands.push_back({.stable_id = "gallery", .kind = RibbonCommandKind::gallery, .text = "Styles",
                               .gallery = gallery});
    styles.commands.push_back(large_button("change", "Change"));
    styles.commands.push_back(large_button("clear", "Clear"));
    RibbonGroupSpec other{.stable_id = "other", .caption = "Other"};
    other.commands.push_back(large_button("o.a", "Alpha"));
    other.commands.push_back(large_button("o.b", "Beta"));
    other.commands.push_back(large_button("o.c", "Gamma"));
    tab.groups.push_back(std::move(styles));
    tab.groups.push_back(std::move(other));
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(std::move(tab));
    Fixture fixture = make_fixture(std::move(tabs), {1200.0, 300.0}, metrics);
    Ribbon& ribbon = *fixture.ribbon;
    check_lines(transitions(ribbon, 0U),
                {"other:medium", "styles:5 columns", "other:collapsed", "styles:4 columns", "styles:3 columns",
                 "styles:medium", "styles:collapsed"},
                "the cycle takes one gallery column per turn, and the group's layout only at its minimum");
    const std::vector<RibbonLadderEntry> ladder = ribbon.reduction_ladder(0U);
    require(ladder[0].groups[0].gallery_columns == 6U, "a gallery starts at its widest");
    for (const RibbonLadderEntry& entry : ladder) {
        resize(fixture, {entry.width, 300.0});
        if (entry.groups[0].size == RibbonGroupSize::collapsed) continue;
        require((*gallery).columns.read() == entry.groups[0].gallery_columns,
                "the ribbon sets the gallery's columns to its ladder entry's");
        const Ribbon::CommandItem* placed = nullptr;
        const std::vector<Ribbon::CommandItem> commands = ribbon.realized_commands();
        for (const Ribbon::CommandItem& item : commands) {
            if (item.stable_id == "gallery") placed = &item;
        }
        require(placed != nullptr && (*placed).parts.command_gallery.present &&
                    (*placed).parts.command_gallery.bounds == (*gallery).committed_arranged_bounds(),
                "the gallery sits where the ribbon's layout puts it");
    }

    std::vector<RibbonTabSpec> authored = std::vector<RibbonTabSpec>(ribbon.tabs().begin(), ribbon.tabs().end());
    authored[0].reduction_order = {{"styles", RibbonReductionKind::gallery_column},
                                   {"styles", RibbonReductionKind::gallery_column},
                                   {"styles"}};
    ribbon.set_tabs(std::move(authored));
    const std::vector<std::string> steps = transitions(ribbon, 0U);
    require(steps.size() >= 3U && steps[0] == "styles:5 columns" && steps[1] == "styles:4 columns" &&
                steps[2] == "styles:medium",
            "authored gallery steps give up one column each, in order");
}

void wordpad_ladders() {
    FixedMetrics metrics;
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(wordpad_home(true));
    tabs.push_back(wordpad_view());
    Fixture fixture = make_fixture(std::move(tabs), {1400.0, 300.0}, metrics);
    // The observed WordPad Home ladder (notes:results/2026-10-14-ribbon-scaling-
    // observation/README.md): the label drop is excluded by decision 6 and is
    // the one expected difference.
    struct Observed final {
        const char* step;
        bool label_drop;
    };
    const Observed home[] = {
        {"clipboard: Cut and Copy lose their labels", true},
        {"insert:collapsed", false},
        {"editing:collapsed", false},
        {"paragraph:collapsed", false},
        {"clipboard:collapsed", false},
        {"font:collapsed", false},
    };
    std::vector<std::string> expected;
    for (const Observed& observed : home) {
        if (!observed.label_drop) expected.push_back(observed.step);
    }
    check_lines(transitions(*fixture.ribbon, 0U), expected,
                "the WordPad Home ladder repeats every observed step but the label drop");
    check_lines(transitions(*fixture.ribbon, 1U),
                {"zoom:medium", "zoom:collapsed", "show-hide:collapsed", "settings:collapsed"},
                "the WordPad View ladder repeats every observed step");
    std::vector<RibbonTabSpec> defaults;
    defaults.push_back(wordpad_home(false));
    (*fixture.ribbon).set_tabs(std::move(defaults));
    check_lines(transitions(*fixture.ribbon, 0U),
                {"editing:collapsed", "insert:medium", "paragraph:collapsed", "font:collapsed",
                 "clipboard:medium", "insert:collapsed", "clipboard:collapsed"},
                "without WordPad's order the default cycle runs from the right (the study's prediction)");
}

// Every command's rectangle, "<id> x,y wxh", at the plane's current entry.
std::vector<std::string> command_rectangles(const Ribbon& ribbon) {
    std::vector<std::string> lines;
    for (const Ribbon::CommandItem& item : ribbon.realized_commands()) {
        const Rect bounds = item.parts.command_focus.present ? item.parts.command_focus.bounds
                                                             : item.parts.command_combo_box.bounds;
        lines.push_back(item.stable_id + " " + number(bounds.x) + "," + number(bounds.y) + " " + number(bounds.width) +
                        "x" + number(bounds.height));
    }
    return lines;
}

void golden_ladder() {
    const double widths[] = {1000.0, 760.0, 700.0, 650.0, 600.0, 560.0, 520.0, 480.0, 440.0,
                             400.0,  350.0, 300.0, 250.0, 200.0, 150.0, 100.0, 60.0};
    const std::vector<std::string> commands_one{
        "paste 4.00,27.00 42.00x66.00",
        "cut 48.00,27.00 43.75x22.00",
        "copy 48.00,49.00 49.00x22.00",
        "font-family 106.00,32.50 120.00x22.00",
        "font-size 106.00,65.50 40.00x22.00",
        "bold 228.00,32.50 22.00x22.00",
        "italic 228.00,65.50 22.00x22.00",
        "underline 252.00,32.50 22.00x22.00",
        "strike 252.00,65.50 22.00x22.00",
        "subscript 276.00,32.50 22.00x22.00",
        "superscript 276.00,65.50 22.00x22.00",
        "text-color 300.00,32.50 34.00x22.00",
        "highlight 300.00,65.50 34.00x22.00",
        "outdent 343.00,32.50 22.00x22.00",
        "indent 343.00,65.50 22.00x22.00",
        "lists 367.00,32.50 34.00x22.00",
        "spacing 367.00,65.50 34.00x22.00",
        "left 403.00,32.50 22.00x22.00",
        "center 403.00,65.50 22.00x22.00",
        "right 427.00,32.50 22.00x22.00",
        "justify 427.00,65.50 22.00x22.00",
        "picture 458.00,27.00 46.75x66.00",
        "date-time 506.75,27.00 78.25x66.00",
        "object 587.00,27.00 78.25x66.00",
        "find 674.25,27.00 49.00x22.00",
        "replace 674.25,49.00 64.75x22.00",
        "select-all 674.25,71.00 80.50x22.00",
    };
    const std::vector<std::string> expected_one{
        "w=1000.00 step=0 clipboard L0 0.00+102.00 font L0 102.00+237.00 paragraph L0 339.00+115.00 insert L0 454.00+216.25 editing L0 670.25+89.50",
        "w=760.00 step=0 clipboard L0 0.00+102.00 font L0 102.00+237.00 paragraph L0 339.00+115.00 insert L0 454.00+216.25 editing L0 670.25+89.50",
        "w=700.00 step=1 clipboard L0 0.00+102.00 font L0 102.00+237.00 paragraph L0 339.00+115.00 insert C 454.00+51.00 editing L0 505.00+89.50",
        "w=650.00 step=1 clipboard L0 0.00+102.00 font L0 102.00+237.00 paragraph L0 339.00+115.00 insert C 454.00+51.00 editing L0 505.00+89.50",
        "w=600.00 step=1 clipboard L0 0.00+102.00 font L0 102.00+237.00 paragraph L0 339.00+115.00 insert C 454.00+51.00 editing L0 505.00+89.50",
        "w=560.00 step=3 clipboard L0 0.00+102.00 font L0 102.00+237.00 paragraph C 339.00+66.25 insert C 405.25+51.00 editing C 456.25+55.75",
        "w=520.00 step=3 clipboard L0 0.00+102.00 font L0 102.00+237.00 paragraph C 339.00+66.25 insert C 405.25+51.00 editing C 456.25+55.75",
        "w=480.00 step=4 clipboard C 0.00+66.25 font L0 66.25+237.00 paragraph C 303.25+66.25 insert C 369.50+51.00 editing C 420.50+55.75",
        "w=440.00 step=5 clipboard C 0.00+66.25 font C 66.25+51.00 paragraph C 117.25+66.25 insert C 183.50+51.00 editing C 234.50+55.75",
        "w=400.00 step=5 clipboard C 0.00+66.25 font C 66.25+51.00 paragraph C 117.25+66.25 insert C 183.50+51.00 editing C 234.50+55.75",
        "w=350.00 step=5 clipboard C 0.00+66.25 font C 66.25+51.00 paragraph C 117.25+66.25 insert C 183.50+51.00 editing C 234.50+55.75",
        "w=300.00 step=5 clipboard C 0.00+66.25 font C 66.25+51.00 paragraph C 117.25+66.25 insert C 183.50+51.00 editing C 234.50+55.75",
        "w=250.00 step=5 scroll first=0 clipboard C 0.00+66.25 font C 66.25+51.00 paragraph C 117.25+66.25 insert C 183.50+51.00 right@237.00",
        "w=200.00 step=5 scroll first=0 clipboard C 0.00+66.25 font C 66.25+51.00 paragraph C 117.25+66.25 right@187.00",
        "w=150.00 step=5 scroll first=0 clipboard C 0.00+66.25 font C 66.25+51.00 right@137.00",
        "w=100.00 step=5 scroll first=0 clipboard C 0.00+66.25 right@87.00",
        "w=60.00 step=5 scroll first=0 clipboard C 0.00+47.00 right@47.00",
    };
    const std::vector<std::string> commands_one_and_half{
        "paste 6.00,40.50 63.00x99.00",
        "cut 72.00,40.50 65.63x33.00",
        "copy 72.00,73.50 73.50x33.00",
        "font-family 158.50,48.75 180.00x33.00",
        "font-size 158.50,98.25 60.00x33.00",
        "bold 341.50,48.75 33.00x33.00",
        "italic 341.50,98.25 33.00x33.00",
        "underline 377.50,48.75 33.00x33.00",
        "strike 377.50,98.25 33.00x33.00",
        "subscript 413.50,48.75 33.00x33.00",
        "superscript 413.50,98.25 33.00x33.00",
        "text-color 449.50,48.75 51.00x33.00",
        "highlight 449.50,98.25 51.00x33.00",
        "outdent 513.50,48.75 33.00x33.00",
        "indent 513.50,98.25 33.00x33.00",
        "lists 549.50,48.75 51.00x33.00",
        "spacing 549.50,98.25 51.00x33.00",
        "left 603.50,48.75 33.00x33.00",
        "center 603.50,98.25 33.00x33.00",
        "right 639.50,48.75 33.00x33.00",
        "justify 639.50,98.25 33.00x33.00",
        "picture 685.50,40.50 70.13x99.00",
        "date-time 758.63,40.50 117.38x99.00",
        "object 879.00,40.50 117.38x99.00",
        "find 1009.38,40.50 73.50x33.00",
        "replace 1009.38,73.50 97.13x33.00",
        "select-all 1009.38,106.50 120.75x33.00",
    };
    const std::vector<std::string> expected_one_and_half{
        "w=1000.00 step=1 clipboard L0 0.00+152.50 font L0 152.50+355.00 paragraph L0 507.50+172.00 insert C 679.50+76.00 editing L0 755.50+133.75",
        "w=760.00 step=4 clipboard C 0.00+98.88 font L0 98.88+355.00 paragraph C 453.88+98.88 insert C 552.75+76.00 editing C 628.75+83.13",
        "w=700.00 step=5 clipboard C 0.00+98.88 font C 98.88+76.00 paragraph C 174.88+98.88 insert C 273.75+76.00 editing C 349.75+83.13",
        "w=650.00 step=5 clipboard C 0.00+98.88 font C 98.88+76.00 paragraph C 174.88+98.88 insert C 273.75+76.00 editing C 349.75+83.13",
        "w=600.00 step=5 clipboard C 0.00+98.88 font C 98.88+76.00 paragraph C 174.88+98.88 insert C 273.75+76.00 editing C 349.75+83.13",
        "w=560.00 step=5 clipboard C 0.00+98.88 font C 98.88+76.00 paragraph C 174.88+98.88 insert C 273.75+76.00 editing C 349.75+83.13",
        "w=520.00 step=5 clipboard C 0.00+98.88 font C 98.88+76.00 paragraph C 174.88+98.88 insert C 273.75+76.00 editing C 349.75+83.13",
        "w=480.00 step=5 clipboard C 0.00+98.88 font C 98.88+76.00 paragraph C 174.88+98.88 insert C 273.75+76.00 editing C 349.75+83.13",
        "w=440.00 step=5 clipboard C 0.00+98.88 font C 98.88+76.00 paragraph C 174.88+98.88 insert C 273.75+76.00 editing C 349.75+83.13",
        "w=400.00 step=5 scroll first=0 clipboard C 0.00+98.88 font C 98.88+76.00 paragraph C 174.88+98.88 insert C 273.75+76.00 right@380.50",
        "w=350.00 step=5 scroll first=0 clipboard C 0.00+98.88 font C 98.88+76.00 paragraph C 174.88+98.88 right@330.50",
        "w=300.00 step=5 scroll first=0 clipboard C 0.00+98.88 font C 98.88+76.00 paragraph C 174.88+98.88 right@280.50",
        "w=250.00 step=5 scroll first=0 clipboard C 0.00+98.88 font C 98.88+76.00 right@230.50",
        "w=200.00 step=5 scroll first=0 clipboard C 0.00+98.88 font C 98.88+76.00 right@180.50",
        "w=150.00 step=5 scroll first=0 clipboard C 0.00+98.88 right@130.50",
        "w=100.00 step=5 scroll first=0 clipboard C 0.00+80.50 right@80.50",
        "w=60.00 step=5 scroll first=0 clipboard C 0.00+40.50 right@40.50",
    };
    const double scales[] = {1.0, 1.5};
    for (const double scale : scales) {
        FixedMetrics metrics;
        std::vector<RibbonTabSpec> tabs;
        tabs.push_back(wordpad_home(true));
        Fixture fixture = make_fixture(std::move(tabs), {1400.0, 400.0}, metrics, scale);
        const bool one = scale == 1.0;
        check_lines(command_rectangles(*fixture.ribbon), one ? commands_one : commands_one_and_half,
                    "golden command rectangles at the widest entry");
        std::vector<std::string> lines;
        for (const double width : widths) {
            resize(fixture, {width, 400.0});
            std::string line = "w=" + number(width) + " " + describe(*fixture.ribbon);
            const Ribbon::Parts parts = (*fixture.ribbon).parts();
            if (parts.scroll_left.present) line += " left@" + number(parts.scroll_left.bounds.x);
            if (parts.scroll_right.present) line += " right@" + number(parts.scroll_right.bounds.x);
            lines.push_back(line);
        }
        check_lines(lines, one ? expected_one : expected_one_and_half, "golden layouts at the width ladder");
        // Every boundary: at an entry's width the plane shows that entry, one
        // unit narrower the next (or scrolling after the last).
        const std::vector<RibbonLadderEntry> ladder = (*fixture.ribbon).reduction_ladder(0U);
        for (std::size_t entry = 0; entry < ladder.size(); ++entry) {
            resize(fixture, {ladder[entry].width, 400.0});
            require((*fixture.ribbon).ladder_step() == entry && !(*fixture.ribbon).scrolling(),
                    "an entry shows at exactly its width");
            resize(fixture, {ladder[entry].width - 1.0, 400.0});
            const bool last = entry + 1U == ladder.size();
            require(last ? (*fixture.ribbon).scrolling() : (*fixture.ribbon).ladder_step() == entry + 1U,
                    "one unit narrower shows the next entry, or scrolls after the last");
        }
    }
}

void labels_never_drop() {
    FixedMetrics metrics;
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(wordpad_home(false));
    Fixture fixture = make_fixture(std::move(tabs), {1400.0, 300.0}, metrics);
    const std::vector<RibbonLadderEntry> ladder = (*fixture.ribbon).reduction_ladder(0U);
    for (const RibbonLadderEntry& entry : ladder) {
        resize(fixture, {entry.width, 300.0});
        for (const Ribbon::CommandItem& item : (*fixture.ribbon).realized_commands()) {
            const RibbonCommandSpec& spec = (*fixture.ribbon).tabs()[0].groups[item.group].commands[item.index];
            if (spec.kind == RibbonCommandKind::combo_box) continue;
            require(item.parts.command_label.present == spec.show_label,
                    "every command keeps its authored label at every ladder entry");
        }
        for (const Ribbon::Group& group : (*fixture.ribbon).realized_groups()) {
            require(group.collapsed ? group.parts.group_button_label.present && !group.parts.group_caption.present
                                    : group.parts.group_caption.present,
                    "groups keep their captions, on the collapsed button when collapsed");
        }
    }
}

void scrolling_never_hides() {
    FixedMetrics metrics;
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(wordpad_home(true));
    Fixture fixture = make_fixture(std::move(tabs), {1400.0, 400.0}, metrics);
    Ribbon& ribbon = *fixture.ribbon;
    const std::vector<RibbonLadderEntry> ladder = ribbon.reduction_ladder(0U);
    const double narrowest = ladder.back().width;
    const double widths[] = {narrowest - 1.0, 200.0, 120.0, 80.0, 40.0, 20.0};
    for (const double width : widths) {
        resize(fixture, {width, 400.0});
        require(!ribbon.plane_hidden() && ribbon.parts().command_plane.present && ribbon.scrolling() &&
                    ribbon.parts().scroll_right.present && !ribbon.realized_groups().empty(),
                "below the narrowest entry the plane scrolls, at any width, and never hides");
    }
    resize(fixture, {120.0, 400.0});
    require(!ribbon.parts().scroll_left.present && ribbon.first_visible_group() == 0U,
            "scrolling starts at the first group, with no left button");
    const Rect right = ribbon.parts().scroll_right.bounds;
    const Point press{right.x + right.width * 0.5, right.y + right.height * 0.5};
    static_cast<void>((*fixture.window).dispatch_pointer({PointerAction::down, PointerButton::primary, press}));
    static_cast<void>((*fixture.window).dispatch_pointer({PointerAction::up, PointerButton::primary, press}));
    (*fixture.window).perform_layout();
    require(ribbon.first_visible_group() == 1U && ribbon.parts().scroll_left.present &&
                ribbon.realized_groups()[0].stable_id == "font",
            "the right scroll button scrolls by one group and the left button appears");
    require(ribbon.on_semantic_child_action("ribbon.scroll-left", SemanticAction::press, {}) &&
                ribbon.first_visible_group() == 0U,
            "the scroll buttons are pressable through accessibility");
    (*fixture.window).perform_layout();
    require((*fixture.window).request_focus(fixture.ribbon), "the ribbon takes focus");
    // Down to the plane, then right to the last group's button.
    KeyEvent down{KeyAction::down, PhysicalKey::down};
    static_cast<void>((*fixture.window).dispatch_key(down));
    for (int step = 0; step < 4; ++step) {
        KeyEvent right_key{KeyAction::down, PhysicalKey::right};
        static_cast<void>((*fixture.window).dispatch_key(right_key));
        (*fixture.window).perform_layout();
    }
    // Each step scrolled the next group into view; Editing, the last, fits
    // beside Insert once nothing remains past the edge.
    const std::vector<Ribbon::Group> shown = ribbon.realized_groups();
    require(ribbon.first_visible_group() == 3U && shown.size() == 2U && shown[1].stable_id == "editing" &&
                !ribbon.parts().scroll_right.present,
            "moving the keyboard to a group out of view scrolls it into view");
    resize(fixture, {1400.0, 400.0});
    require(!ribbon.scrolling() && ribbon.first_visible_group() == 0U && !ribbon.parts().scroll_left.present,
            "widening ends scrolling");

    // Owner decision 45: scrolling starts below the narrowest entry and ends
    // only 8 units above it.
    struct Probe final {
        double offset;
        bool scrolls;
    };
    const Probe probes[] = {{0.0, false}, {-1.0, true}, {0.0, true}, {7.5, true},
                            {8.0, false}, {0.0, false}, {-0.5, true}, {20.0, false}};
    for (const Probe& probe : probes) {
        resize(fixture, {narrowest + probe.offset, 400.0});
        require(ribbon.scrolling() == probe.scrolls && ribbon.parts().scroll_right.present == probe.scrolls,
                "the scroll buttons appear below the narrowest entry and leave 8 units above it");
    }
    resize(fixture, {narrowest + 4.0, 400.0});
    require(!ribbon.scrolling(), "approached from wider, the band does not scroll");
    resize(fixture, {narrowest - 2.0, 400.0});
    resize(fixture, {narrowest + 4.0, 400.0});
    require(ribbon.scrolling() && ribbon.ladder_step() == std::size_t{ladder.size() - 1U} &&
                ribbon.realized_groups().size() < 5U,
            "approached from narrower, the band keeps scrolling with the right button over the last group");
}

void hide_rule() {
    FixedMetrics metrics;
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(three_groups({}));
    Fixture fixture = make_fixture(std::move(tabs), {900.0, 112.0}, metrics);
    Ribbon& ribbon = *fixture.ribbon;
    struct Probe final {
        double height;
        bool hidden;
    };
    // No declared minimum: hidden when no document area would be left
    // (below the ribbon's 112 units), shown again from 120 (8 more).
    const Probe none[] = {{112.0, false}, {111.0, true}, {112.0, true}, {119.0, true},
                          {120.0, false}, {112.0, false}, {111.5, true}, {300.0, false}};
    for (const Probe& probe : none) {
        resize(fixture, {900.0, probe.height});
        require(ribbon.plane_hidden() == probe.hidden, "the plane hides by the document area, with hysteresis");
        require(ribbon.parts().tab_rail.present && ribbon.realized_tabs().size() == 1U &&
                    ribbon.parts().command_plane.present == !probe.hidden,
                "only the command plane hides; the tab row stays");
        require(ribbon.measure({900.0, probe.height}).height == (probe.hidden ? 24.0 : 112.0),
                "a hidden plane gives its height to the document area");
    }
    ribbon.document_minimum_height = 100.0;
    const Probe declared[] = {{212.0, false}, {211.0, true}, {219.0, true}, {219.5, true},
                              {220.0, false}, {212.0, false}, {150.0, true}, {400.0, false}};
    for (const Probe& probe : declared) {
        resize(fixture, {900.0, probe.height});
        require(ribbon.plane_hidden() == probe.hidden,
                "with a declared minimum the plane hides below it and shows again 8 units above");
    }
    resize(fixture, {30.0, 400.0});
    require(!ribbon.plane_hidden() && ribbon.scrolling(), "the width never hides the plane");
    bool threw = false;
    try {
        ribbon.document_minimum_height = -1.0;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && ribbon.document_minimum_height.read() == 100.0, "a negative minimum is refused");

    // At text scale 1.5 the ribbon is 168 units tall, and the threshold moves.
    FixedMetrics larger_metrics;
    std::vector<RibbonTabSpec> larger_tabs;
    larger_tabs.push_back(three_groups({}));
    Fixture larger = make_fixture(std::move(larger_tabs), {900.0, 168.0}, larger_metrics, 1.5);
    require(!(*larger.ribbon).plane_hidden(), "at text scale 1.5 the plane fits at 168 units");
    resize(larger, {900.0, 167.0});
    require((*larger.ribbon).plane_hidden(), "and hides one unit below");
}

void group_overlay() {
    FixedMetrics metrics;
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(wordpad_home(true));
    Fixture fixture = make_fixture(std::move(tabs), {1400.0, 400.0}, metrics);
    Ribbon& ribbon = *fixture.ribbon;
    Window& window = *fixture.window;
    const std::vector<RibbonLadderEntry> ladder = ribbon.reduction_ladder(0U);
    resize(fixture, {ladder.back().width, 400.0});
    const std::vector<Ribbon::Group> groups = ribbon.realized_groups();
    require(groups.size() == 5U && groups[1].stable_id == "font" && groups[1].collapsed &&
                groups[1].parts.group_button.present && groups[1].parts.group_button_icon.present &&
                groups[1].parts.group_button_label.present && groups[1].parts.group_button_glyph.present &&
                !groups[1].parts.group_caption.present && !groups[1].parts.group_launcher.present,
            "a collapsed group is one button with its icon, caption and chevron");
    NullPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, ladder.back().width, 400.0}));
    require(painted(painter, "Font") && !painted(painter, "Bold"), "the collapsed button paints its caption");
    const std::shared_ptr<ComboBox> family = ribbon.combo_box("font-family");
    require(!(*family).effectively_visible(), "a collapsed group's hosted controls are not shown in the plane");

    const Rect button = groups[1].parts.group_button.bounds;
    const Point point{button.x + button.width * 0.5, button.y + button.height * 0.5};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
    window.perform_layout();
    require(ribbon.overlay_group() == std::string("font") &&
                ribbon.realized_groups()[1].parts.group_button.states.contains(AnatomyState::open),
            "a click on the button opens the group's overlay");
    const Control::Ptr overlay = ribbon.group_overlay();
    const std::vector<Ribbon::Group> overlay_groups = ribbon.realized_overlay_groups();
    require(overlay && overlay_groups.size() == 1U && !overlay_groups[0].collapsed && overlay_groups[0].layout == 0U &&
                overlay_groups[0].parts.group_caption.present && overlay_groups[0].parts.group_launcher.present,
            "the overlay shows the group at its largest layout with its caption and launcher");
    require((*family).parent() == overlay && (*family).effectively_visible(),
            "the group's hosted controls move into the overlay");
    const std::vector<Ribbon::CommandItem> overlay_commands = ribbon.realized_overlay_commands();
    const Ribbon::CommandItem* bold = nullptr;
    for (const Ribbon::CommandItem& item : overlay_commands) {
        if (item.stable_id == "bold") bold = &item;
        if (item.stable_id == "font-family") {
            require(item.parts.command_combo_box.bounds == (*family).committed_arranged_bounds(),
                    "a hosted control sits at its place in the overlay");
        }
    }
    require(bold != nullptr && (*bold).parts.command_icon.present, "the overlay realizes its commands");
    int bolds = 0;
    SubscriptionToken counted =
        (*ribbon.tabs()[0].groups[1].commands[2].command).invoked().subscribe(CountInvocation{&bolds});
    const Rect absolute = (*overlay).absolute_bounds();
    const Rect face = (*bold).parts.command_face.bounds;
    const Point bold_point{absolute.x + face.x + face.width * 0.5, absolute.y + face.y + face.height * 0.5};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, bold_point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, bold_point}));
    window.perform_layout();
    require(bolds == 1 && !ribbon.overlay_group() && (*family).parent() == fixture.ribbon,
            "a command in the overlay runs once and closes the overlay, returning the hosted controls");

    // Keyboard: the group's button opens the overlay with focus on its first
    // command; Escape closes it and returns to the button.
    require(window.request_focus(fixture.ribbon), "the ribbon takes focus");
    KeyEvent down{KeyAction::down, PhysicalKey::down};
    static_cast<void>(window.dispatch_key(down));
    KeyEvent right{KeyAction::down, PhysicalKey::right};
    static_cast<void>(window.dispatch_key(right));
    KeyEvent enter{KeyAction::down, PhysicalKey::enter};
    static_cast<void>(window.dispatch_key(enter));
    window.perform_layout();
    require(ribbon.overlay_group() == std::string("font") && window.focused_control() == overlay,
            "Enter on a collapsed group's button opens its overlay with focus in it");
    KeyEvent next{KeyAction::down, PhysicalKey::right};
    static_cast<void>(window.dispatch_key(next));
    const std::vector<Ribbon::CommandItem> focused = ribbon.realized_overlay_commands();
    bool italic_focus = false;
    for (const Ribbon::CommandItem& item : focused) {
        if (item.stable_id == "italic") italic_focus = item.states.focus_visible;
    }
    require(italic_focus, "the arrows move through the overlay's commands");
    KeyEvent escape{KeyAction::down, PhysicalKey::escape};
    static_cast<void>(window.dispatch_key(escape));
    window.perform_layout();
    require(!ribbon.overlay_group() && window.focused_control() == fixture.ribbon &&
                ribbon.realized_groups()[1].parts.group_button.states.contains(AnatomyState::focus_visible),
            "Escape closes the overlay and focus returns to the group's button");

    // Semantics: the collapsed group is a menu button with an overlay.
    const std::vector<SemanticNode> nodes = ribbon.semantic_virtual_children();
    const SemanticNode& plane = nodes[2];
    require(plane.children.size() == 5U && plane.children[1].role == SemanticRole::menu_button &&
                plane.children[1].has_popup && plane.children[1].name == "Font",
            "a collapsed group is a menu button to accessibility");
    require(ribbon.on_semantic_child_action("ribbon.group.font", SemanticAction::expand, {}) &&
                ribbon.overlay_group() == std::string("font"),
            "expanding the node opens the overlay");
    window.perform_layout();
    const Point away{ladder.back().width - 4.0, 396.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, away}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, away}));
    window.perform_layout();
    require(!ribbon.overlay_group() && (*family).parent() == fixture.ribbon, "a click away closes the overlay");
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
    window.perform_layout();
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
    window.perform_layout();
    require(!ribbon.overlay_group(), "a second click on the group's button closes its overlay");
    // The overlay is an overlay root, yet takes the ribbon's theme override.
    const std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    (*held).id = "ribbon-override";
    const std::shared_ptr<const Theme> override_theme = Theme::create(*held);
    ribbon.set_theme_override(override_theme);
    require(ribbon.show_group_overlay("font"), "the overlay opens by its group's ID");
    window.perform_layout();
    require((*family).parent() != fixture.ribbon && (*family).effective_theme().id() == "ribbon-override",
            "the group overlay's controls take the ribbon's theme override");
    // Widening until the group grows back closes the overlay.
    resize(fixture, {1400.0, 400.0});
    require(!ribbon.overlay_group() && (*family).parent() == fixture.ribbon && (*family).effectively_visible(),
            "an overlay whose group grows back closes");
    ribbon.clear_theme_override();
    require((*family).effective_theme().id() == window.theme().id(), "clearing the override restores the window's");
}

void caches() {
    FixedMetrics metrics;
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(wordpad_home(true));
    Fixture fixture = make_fixture(std::move(tabs), {1400.0, 400.0}, metrics);
    Ribbon& ribbon = *fixture.ribbon;
    const RibbonScalingCounters before = ribbon.scaling_counters();
    require(before.ladder_builds == 1U && before.ladder_entries == 6U && before.text_measurements > 0U,
            "the first layout builds the ladder once");
    for (int step = 0; step < 300; ++step) {
        resize(fixture, {1400.0 - static_cast<double>(step) * 4.5, 400.0});
    }
    const RibbonScalingCounters swept = ribbon.scaling_counters();
    require(swept.ladder_builds == 1U && swept.text_measurements == before.text_measurements &&
                swept.ladder_lookups > before.ladder_lookups,
            "a resize sweep measures no text and builds no ladder");
    (*ribbon.tabs()[0].groups[0].commands[1].command).set_enabled(false);
    (*fixture.window).perform_layout();
    require(ribbon.scaling_counters().ladder_builds == 1U, "an enabled change rebuilds nothing");
    (*ribbon.tabs()[0].groups[0].commands[1].command).set_text("Cut out");
    (*fixture.window).perform_layout();
    require(ribbon.scaling_counters().ladder_builds == 2U, "a text change rebuilds the ladder once");
    (*fixture.window).set_text_scale(1.5);
    (*fixture.window).perform_layout();
    require(ribbon.scaling_counters().ladder_builds == 3U, "a text-scale change rebuilds the ladder");
    // The ribbon follows the window's theme: a new theme rebuilds the ladder
    // once and repaints.
    const std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    (*held).id = "ribbon-follow";
    (*fixture.window).set_theme(Theme::create(*held));
    (*fixture.window).perform_layout();
    require(ribbon.scaling_counters().ladder_builds == 4U && ribbon.effective_theme().id() == "ribbon-follow",
            "a window theme change rebuilds the ladder once");
    (*fixture.window).perform_layout();
    require(ribbon.scaling_counters().ladder_builds == 4U, "nothing more is rebuilt after the theme change");
}

// Text and number fields show their text as a label part: at the field's
// left in a stacked layout, above it in a one-row layout; the field keeps its
// width and its control sits on the published control part; a press on the
// label focuses the field; an unlabelled field has no label part.
void field_labels() {
    FixedMetrics metrics;
    std::vector<RibbonTabSpec> tabs;
    RibbonTabSpec home{.stable_id = "home", .text = "&Home"};
    RibbonGroupSpec find{.stable_id = "find-group", .caption = "Find"};
    find.commands.push_back({.stable_id = "find", .kind = RibbonCommandKind::text_box, .text = "Find"});
    find.commands.push_back({.stable_id = "matches", .kind = RibbonCommandKind::numeric_up_down, .text = "&Matches"});
    find.commands.push_back(
        {.stable_id = "bare", .kind = RibbonCommandKind::text_box, .text = "Bare", .show_label = false});
    RibbonGroupSpec size{.stable_id = "size-group", .caption = "Size"};
    size.commands.push_back({.stable_id = "width", .kind = RibbonCommandKind::numeric_up_down, .text = "Width"});
    size.layouts = {layout_of(RibbonGroupSize::large, 1U)};
    home.groups.push_back(std::move(find));
    home.groups.push_back(std::move(size));
    tabs.push_back(std::move(home));
    Fixture fixture = make_fixture(std::move(tabs), {1000.0, 400.0}, metrics);
    Ribbon& ribbon = *fixture.ribbon;
    Window& window = *fixture.window;
    const std::vector<Ribbon::CommandItem> items = ribbon.realized_commands();
    const Ribbon::CommandItem* found = nullptr;
    for (const Ribbon::CommandItem& item : items) {
        if (item.stable_id == "matches") found = &item;
    }
    require(found != nullptr, "the labelled number field is realized");
    const Ribbon::CommandParts& matches = (*found).parts;
    const std::shared_ptr<NumericUpDown> matches_field = ribbon.numeric_up_down("matches");
    require(matches.command_label.present && matches.command_numeric_up_down.present &&
                matches.command_numeric_up_down.bounds.width == 64.0 &&
                matches.command_label.bounds.y == matches.command_numeric_up_down.bounds.y &&
                matches.command_label.bounds.x + matches.command_label.bounds.width <=
                    matches.command_numeric_up_down.bounds.x &&
                matches.command_numeric_up_down.bounds == (*matches_field).committed_arranged_bounds(),
            "a stacked field's label sits at its left and its control on its field part");
    for (const Ribbon::CommandItem& item : items) {
        if (item.stable_id == "bare") {
            require(!item.parts.command_label.present && item.parts.command_text_box.bounds.width == 100.0,
                    "an unlabelled field has no label part and keeps its width");
        }
        if (item.stable_id == "width") {
            const Ribbon::CommandParts& width = item.parts;
            require(width.command_label.present && width.command_numeric_up_down.present &&
                        width.command_label.bounds.x == width.command_numeric_up_down.bounds.x &&
                        width.command_label.bounds.y + width.command_label.bounds.height <=
                            width.command_numeric_up_down.bounds.y &&
                        width.command_numeric_up_down.bounds.height == 22.0 &&
                        width.command_numeric_up_down.bounds == (*ribbon.numeric_up_down("width")).committed_arranged_bounds(),
                    "a one-row layout puts the field's label above the field");
        }
    }
    NullPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 1000.0, 400.0}));
    require(painted(painter, "Matches") && painted(painter, "Width") && !painted(painter, "&Matches") &&
                !painted(painter, "Bare"),
            "labelled fields paint their labels without mnemonic markers; an unlabelled one paints none");
    const Rect label = matches.command_label.bounds;
    const Point point{label.x + label.width * 0.5, label.y + label.height * 0.5};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
    // A numeric up-down gives focus to its edit part (a composite field).
    require(window.focused_control() == SurfaceRoot::focus_target_of(matches_field),
            "a press on a field's label focuses its field");
    std::vector<RibbonTabSpec> icon_only;
    RibbonTabSpec refused_tab{.stable_id = "home", .text = "&Home"};
    RibbonGroupSpec refused_group{.stable_id = "group", .caption = "Group"};
    refused_group.commands.push_back({.stable_id = "family", .kind = RibbonCommandKind::combo_box,
                                      .text = "Family", .show_label = false});
    refused_tab.groups.push_back(std::move(refused_group));
    icon_only.push_back(std::move(refused_tab));
    require(refused(ribbon, std::move(icon_only)), "only text and number fields may leave their label out");
}

// A panel tab: the application's control is the tab's body, the ribbon's
// child sized to the command plane while the tab is selected, carried across
// set_tabs by identity and removed when no longer named; refusals.
void panel_tab() {
    FixedMetrics metrics;
    const std::shared_ptr<Panel> palette = make_control<Panel>(StableId("palette"));
    std::vector<RibbonTabSpec> tabs;
    tabs.push_back(wordpad_home(true));
    tabs.push_back(RibbonTabSpec{.stable_id = "palette-tab", .text = "&Palette", .panel = palette});
    Fixture fixture = make_fixture(tabs, {1400.0, 400.0}, metrics);
    Ribbon& ribbon = *fixture.ribbon;
    require((*palette).parent() == fixture.ribbon && !(*palette).effectively_visible(),
            "a panel tab's control is the ribbon's child, hidden while another tab is selected");
    ribbon.selected_tab = std::size_t{1U};
    (*fixture.window).perform_layout();
    const Ribbon::Parts parts = ribbon.parts();
    require((*palette).effectively_visible() && parts.command_plane.present &&
                (*palette).committed_arranged_bounds() == parts.command_plane.bounds &&
                ribbon.realized_groups().empty(),
            "the selected panel tab's control fills the command plane, with no groups");
    resize(fixture, {700.0, 400.0});
    require((*palette).committed_arranged_bounds() == ribbon.parts().command_plane.bounds,
            "the panel follows the command plane's width");
    ribbon.set_tabs(tabs);
    (*fixture.window).perform_layout();
    require((*palette).parent() == fixture.ribbon && ribbon.selected_tab.read() == std::size_t{1U} &&
                (*palette).effectively_visible(),
            "set_tabs carries the panel over by identity, with the selection");
    std::vector<RibbonTabSpec> with_groups = tabs;
    with_groups[1].groups.push_back(wordpad_home(true).groups[0]);
    with_groups[1].groups[0].stable_id = "panel-group";
    for (RibbonCommandSpec& item : with_groups[1].groups[0].commands) item.stable_id = "panel." + item.stable_id;
    require(refused(ribbon, with_groups), "a panel tab has no groups");
    std::vector<RibbonTabSpec> twice = tabs;
    twice.push_back(RibbonTabSpec{.stable_id = "again", .text = "Again", .panel = palette});
    require(refused(ribbon, twice), "a panel is used once");
    const std::shared_ptr<Panel> owned = make_control<Panel>(StableId("owned"));
    const std::shared_ptr<Panel> owner = make_control<Panel>(StableId("owner"));
    (*owner).add_child(owned);
    std::vector<RibbonTabSpec> elsewhere = tabs;
    elsewhere[1].panel = owned;
    require(refused(ribbon, elsewhere) && (*palette).parent() == fixture.ribbon,
            "a panel owned elsewhere is refused, and nothing changes");
    std::vector<RibbonTabSpec> without;
    without.push_back(wordpad_home(true));
    ribbon.set_tabs(std::move(without));
    require(!(*palette).parent(), "a panel no longer named leaves the ribbon");
}


} // namespace

int main() {
    generated_layouts();
    default_cycle();
    authored_order();
    refusals();
    gallery_columns();
    wordpad_ladders();
    golden_ladder();
    labels_never_drop();
    scrolling_never_hides();
    hide_rule();
    group_overlay();
    caches();
    field_labels();
    panel_tab();
    std::cout << "Ribbon scaling: generated layouts, the default cycle, the authored order, refusals, gallery "
                 "columns, the WordPad ladders, golden layouts at text scales 1 and 1.5, labels kept, scrolling at "
                 "any width, the hide rule with hysteresis, group overlays, caches, theme following, field labels "
                 "and panel tabs passed\n";
    return 0;
}
