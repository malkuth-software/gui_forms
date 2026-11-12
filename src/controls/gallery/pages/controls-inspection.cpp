#include "pages.hpp"
#include "gui_forms/controls/combo_box.hpp"
#include "gui_forms/controls/domain_up_down.hpp"
#include "gui_forms/controls/h_scroll_bar.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/numeric_up_down.hpp"
#include "gui_forms/controls/progress_bar.hpp"
#include "gui_forms/controls/progress_bar_values.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/track_bar.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms::gallery::pages {
namespace {

// The controls-inspection page (stage T4, visual inspection): every stock
// control in each state that can be shown at rest, in a fixed labelled
// grid, drawn by the window's theme alone, so that one
// capture per theme compares each control with its reference. Hot, pressed
// and keyboard focus need pointer or keyboard input on one control at a
// time; the specimen sheet (tests/specimen/specimen_sheet.cpp) renders
// those from a fresh window per cell.
//
// Rows are controls, from the top; each cell is a caption naming the state
// above the control. All coordinates are design coordinates of the
// 1000 x 660 page body.
constexpr double row_label_width = 112.0;
constexpr double cell_width = 144.0;
constexpr double caption_height = 18.0;
// Between a caption and its control, and between rows.
constexpr double caption_gap = 3.0;
constexpr double row_gap = 6.0;
constexpr double grid_left = 8.0;
constexpr double grid_top = 34.0;

// One row of the grid: its top, its control height and its stable-id stem.
struct InspectionRow final {
    std::string_view stem;
    std::string_view title;
    double top{};
    double control_height{};
};

std::string inspection_id(const InspectionRow& row, std::string_view state, std::string_view part) {
    std::string id = "gallery.inspection.";
    id += row.stem;
    id += '.';
    // A caption's words ("default disabled") are dashed in the identity.
    for (const char letter : state) id += letter == ' ' ? '-' : letter;
    if (!part.empty()) {
        id += '.';
        id += part;
    }
    return id;
}

void add_row_title(Surface& page, const InspectionRow& row) {
    std::string id = "gallery.inspection.";
    id += row.stem;
    id += ".title";
    const std::shared_ptr<Label> title = make_control<Label>(StableId(id), std::string(row.title));
    page.add_at(title, {grid_left, row.top + caption_height + caption_gap, row_label_width - 8.0, row.control_height});
}

// Places `control` in column `column` of `row`, under a caption naming
// `state`.
void add_cell(Surface& page, const InspectionRow& row, std::size_t column, std::string_view state,
              const Control::Ptr& control) {
    const double left = grid_left + row_label_width + cell_width * static_cast<double>(column);
    const std::shared_ptr<Label> caption =
        make_control<Label>(StableId(inspection_id(row, state, "caption")), std::string(state));
    page.add_at(caption, {left, row.top, cell_width - 8.0, caption_height});
    page.add_at(control, {left, row.top + caption_height + caption_gap, cell_width - 12.0, row.control_height});
}

void add_buttons(Surface& page, const InspectionRow& row) {
    const std::shared_ptr<Button> normal = make_control<Button>(StableId(inspection_id(row, "normal", "")), "OK");
    add_cell(page, row, 0U, "normal", normal);
    const std::shared_ptr<Button> disabled = make_control<Button>(StableId(inspection_id(row, "disabled", "")), "OK");
    (*disabled).enabled = false;
    add_cell(page, row, 1U, "disabled", disabled);
    const std::shared_ptr<Button> standing = make_control<Button>(StableId(inspection_id(row, "default", "")), "OK");
    (*standing).default_button = true;
    add_cell(page, row, 2U, "default", standing);
    const std::shared_ptr<Button> standing_disabled =
        make_control<Button>(StableId(inspection_id(row, "default-disabled", "")), "OK");
    (*standing_disabled).default_button = true;
    (*standing_disabled).enabled = false;
    add_cell(page, row, 3U, "default disabled", standing_disabled);
}

std::shared_ptr<CheckBox> check_box(const InspectionRow& row, std::string_view state, CheckState value, bool enabled) {
    const std::shared_ptr<CheckBox> box = make_control<CheckBox>(StableId(inspection_id(row, state, "")), "Option");
    (*box).three_state = true;
    (*box).check_state = value;
    (*box).enabled = enabled;
    return box;
}

void add_check_boxes(Surface& page, const InspectionRow& row) {
    add_cell(page, row, 0U, "unchecked", check_box(row, "unchecked", CheckState::unchecked, true));
    add_cell(page, row, 1U, "checked", check_box(row, "checked", CheckState::checked, true));
    add_cell(page, row, 2U, "indeterminate", check_box(row, "indeterminate", CheckState::indeterminate, true));
    add_cell(page, row, 3U, "unchecked disabled", check_box(row, "unchecked-disabled", CheckState::unchecked, false));
    add_cell(page, row, 4U, "checked disabled", check_box(row, "checked-disabled", CheckState::checked, false));
    add_cell(page, row, 5U, "indeterminate disabled",
             check_box(row, "indeterminate-disabled", CheckState::indeterminate, false));
}

// Each radio button has a group of its own, so that checking one leaves the
// others as the grid shows them.
std::shared_ptr<RadioButton> radio_button(const InspectionRow& row, std::string_view state, bool checked, bool enabled) {
    const std::string id = inspection_id(row, state, "");
    const std::shared_ptr<RadioButton> radio = make_control<RadioButton>(StableId(id), "Choice");
    (*radio).group_name = id;
    (*radio).checked = checked;
    (*radio).enabled = enabled;
    return radio;
}

void add_radio_buttons(Surface& page, const InspectionRow& row) {
    add_cell(page, row, 0U, "unchecked", radio_button(row, "unchecked", false, true));
    add_cell(page, row, 1U, "checked", radio_button(row, "checked", true, true));
    add_cell(page, row, 3U, "unchecked disabled", radio_button(row, "unchecked-disabled", false, false));
    add_cell(page, row, 4U, "checked disabled", radio_button(row, "checked-disabled", true, false));
}

std::shared_ptr<TextBox> text_box(const InspectionRow& row, std::string_view state) {
    const std::shared_ptr<TextBox> field = make_control<TextBox>(StableId(inspection_id(row, state, "")));
    (*field).text = "Text";
    (*field).accessible_name = std::string(state);
    return field;
}

void add_text_boxes(Surface& page, const InspectionRow& row) {
    add_cell(page, row, 0U, "normal", text_box(row, "normal"));
    const std::shared_ptr<TextBox> disabled = text_box(row, "disabled");
    (*disabled).enabled = false;
    add_cell(page, row, 1U, "disabled", disabled);
    const std::shared_ptr<TextBox> read_only = text_box(row, "read-only");
    (*read_only).read_only = true;
    add_cell(page, row, 2U, "read-only", read_only);
}

std::shared_ptr<ComboBox> combo_box(const InspectionRow& row, std::string_view state) {
    const std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId(inspection_id(row, state, "")));
    (*combo).items = {std::string("First"), std::string("Second")};
    (*combo).selected_index = std::optional<std::size_t>(0U);
    (*combo).accessible_name = std::string(state);
    return combo;
}

void add_combo_boxes(Surface& page, const InspectionRow& row) {
    add_cell(page, row, 0U, "normal", combo_box(row, "normal"));
    const std::shared_ptr<ComboBox> disabled = combo_box(row, "disabled");
    (*disabled).enabled = false;
    add_cell(page, row, 1U, "disabled", disabled);
}

std::shared_ptr<ListBox> list_box(const InspectionRow& row, std::string_view state) {
    const std::shared_ptr<ListBox> list = make_control<ListBox>(StableId(inspection_id(row, state, "")));
    (*list).items = {std::string("First"), std::string("Selected"), std::string("Third")};
    (*list).select_index(1U);
    (*list).accessible_name = std::string(state);
    return list;
}

void add_list_boxes(Surface& page, const InspectionRow& row) {
    add_cell(page, row, 0U, "selected", list_box(row, "selected"));
    const std::shared_ptr<ListBox> disabled = list_box(row, "disabled");
    (*disabled).enabled = false;
    add_cell(page, row, 1U, "disabled", disabled);
}

std::shared_ptr<NumericUpDown> spinner(const InspectionRow& row, std::string_view state) {
    const std::shared_ptr<NumericUpDown> value = make_control<NumericUpDown>(StableId(inspection_id(row, state, "")));
    (*value).set_range(0.0, 100.0);
    (*value).value = 42.0;
    (*value).accessible_name = std::string(state);
    return value;
}

std::shared_ptr<DomainUpDown> domain(const InspectionRow& row, std::string_view state) {
    const std::shared_ptr<DomainUpDown> value = make_control<DomainUpDown>(StableId(inspection_id(row, state, "")));
    (*value).items = {"First", "Second", "Third"};
    (*value).selected_index = std::optional<std::size_t>{0U};
    (*value).accessible_name = std::string(state);
    return value;
}

// NumericUpDown in the first two cells, DomainUpDown in the next two.
void add_spinners(Surface& page, const InspectionRow& row) {
    add_cell(page, row, 0U, "normal", spinner(row, "normal"));
    const std::shared_ptr<NumericUpDown> disabled = spinner(row, "disabled");
    (*disabled).enabled = false;
    add_cell(page, row, 1U, "disabled", disabled);
    add_cell(page, row, 2U, "domain", domain(row, "domain"));
    const std::shared_ptr<DomainUpDown> domain_disabled = domain(row, "domain disabled");
    (*domain_disabled).enabled = false;
    add_cell(page, row, 3U, "domain disabled", domain_disabled);
}

// A range control at 40 of 0 to 100.
template <typename Range>
std::shared_ptr<Range> range(const InspectionRow& row, std::string_view state, bool enabled) {
    const std::shared_ptr<Range> control = make_control<Range>(StableId(inspection_id(row, state, "")));
    (*control).set_range(0.0, 100.0);
    (*control).value = 40.0;
    (*control).enabled = enabled;
    (*control).accessible_name = std::string(state);
    return control;
}

void add_ranges(Surface& page, const InspectionRow& track, const InspectionRow& progress, const InspectionRow& scroll) {
    add_cell(page, track, 0U, "normal", range<TrackBar>(track, "normal", true));
    add_cell(page, track, 1U, "disabled", range<TrackBar>(track, "disabled", false));
    // Windows Forms' default progress style is blocks; both are shown.
    const std::shared_ptr<ProgressBar> blocks = range<ProgressBar>(progress, "blocks", true);
    (*blocks).visual_style = ProgressBarVisualStyle::blocks;
    add_cell(page, progress, 0U, "blocks", blocks);
    const std::shared_ptr<ProgressBar> blocks_disabled = range<ProgressBar>(progress, "blocks disabled", false);
    (*blocks_disabled).visual_style = ProgressBarVisualStyle::blocks;
    add_cell(page, progress, 1U, "blocks disabled", blocks_disabled);
    add_cell(page, progress, 2U, "continuous", range<ProgressBar>(progress, "continuous", true));
    add_cell(page, scroll, 0U, "normal", range<HScrollBar>(scroll, "normal", true));
    add_cell(page, scroll, 1U, "disabled", range<HScrollBar>(scroll, "disabled", false));
}

void add_group_boxes(Surface& page, const InspectionRow& row) {
    const std::shared_ptr<GroupBox> normal =
        make_control<GroupBox>(StableId(inspection_id(row, "normal", "")), "Group");
    add_cell(page, row, 0U, "normal", normal);
    const std::shared_ptr<GroupBox> disabled =
        make_control<GroupBox>(StableId(inspection_id(row, "disabled", "")), "Group");
    (*disabled).enabled = false;
    add_cell(page, row, 1U, "disabled", disabled);
}

void add_labels(Surface& page, const InspectionRow& row) {
    const std::shared_ptr<Label> label = make_control<Label>(StableId(inspection_id(row, "label", "")), "Label");
    add_cell(page, row, 0U, "label", label);
    const std::shared_ptr<Label> label_disabled =
        make_control<Label>(StableId(inspection_id(row, "label-disabled", "")), "Label");
    (*label_disabled).enabled = false;
    add_cell(page, row, 1U, "label disabled", label_disabled);
    const std::shared_ptr<LinkLabel> link = make_control<LinkLabel>(StableId(inspection_id(row, "link", "")), "Link");
    add_cell(page, row, 2U, "link", link);
    const std::shared_ptr<LinkLabel> link_disabled =
        make_control<LinkLabel>(StableId(inspection_id(row, "link-disabled", "")), "Link");
    (*link_disabled).enabled = false;
    add_cell(page, row, 3U, "link disabled", link_disabled);
}

} // namespace

void add_controls_inspection(const std::shared_ptr<Surface>& page,
                             const std::shared_ptr<PageContext>& context) {
    static_cast<void>(context);
    Surface& body = *page;
    const std::shared_ptr<Label> heading = make_control<Label>(
        StableId("gallery.inspection.heading"),
        "Every stock control at rest in the window's theme. Hot, pressed and focus: the specimen sheet.");
    body.add_at(heading, {grid_left, 8.0, 980.0, 18.0});
    // Each row's top: the previous row's top, caption, gap, control and
    // the gap between rows.
    std::array<InspectionRow, 12> grid{{
        {"button", "Button", 0.0, 24.0},
        {"check-box", "CheckBox", 0.0, 20.0},
        {"radio-button", "RadioButton", 0.0, 20.0},
        {"text-box", "TextBox", 0.0, 24.0},
        {"combo-box", "ComboBox", 0.0, 24.0},
        {"list-box", "ListBox", 0.0, 64.0},
        {"numeric-up-down", "Up-downs", 0.0, 24.0},
        {"track-bar", "TrackBar", 0.0, 32.0},
        {"progress-bar", "ProgressBar", 0.0, 18.0},
        {"h-scroll-bar", "HScrollBar", 0.0, 17.0},
        {"group-box", "GroupBox", 0.0, 48.0},
        {"label", "Label, LinkLabel", 0.0, 18.0},
    }};
    double top = grid_top;
    for (InspectionRow& row : grid) {
        row.top = top;
        top = top + caption_height + caption_gap + row.control_height + row_gap;
    }
    // The theme's own ground under the grid: a borderless panel, so the
    // controls stand on the face their theme draws them for, not on the
    // Gallery's page ground.
    const std::shared_ptr<Panel> ground = make_control<Panel>(StableId("gallery.inspection.ground"));
    body.add_at(ground, {0.0, grid_top - 6.0, 1000.0, top - grid_top + 6.0});
    const InspectionRow& buttons = grid[0];
    const InspectionRow& checks = grid[1];
    const InspectionRow& radios = grid[2];
    const InspectionRow& texts = grid[3];
    const InspectionRow& combos = grid[4];
    const InspectionRow& lists = grid[5];
    const InspectionRow& spinners = grid[6];
    const InspectionRow& tracks = grid[7];
    const InspectionRow& progress = grid[8];
    const InspectionRow& scrolls = grid[9];
    const InspectionRow& groups = grid[10];
    const InspectionRow& labels = grid[11];
    const std::array<const InspectionRow*, 12> rows{&buttons, &checks, &radios, &texts, &combos, &lists,
                                                    &spinners, &tracks, &progress, &scrolls, &groups, &labels};
    for (const InspectionRow* row : rows) add_row_title(body, *row);
    add_buttons(body, buttons);
    add_check_boxes(body, checks);
    add_radio_buttons(body, radios);
    add_text_boxes(body, texts);
    add_combo_boxes(body, combos);
    add_list_boxes(body, lists);
    add_spinners(body, spinners);
    add_ranges(body, tracks, progress, scrolls);
    add_group_boxes(body, groups);
    add_labels(body, labels);
}

} // namespace gui_forms::gallery::pages
