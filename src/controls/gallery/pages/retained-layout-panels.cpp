#include "pages.hpp"

namespace gui_forms::gallery::pages {

// Owner of the Retained Layout Panels page: four FlowLayoutPanelCompat directions
// and one mixed-track TableLayoutPanelCompat. The two range controls inside the
// table share one application-owned Value<double>, and the table's check box
// binds a Value<bool>; the page observes those values, never the controls.
class LayoutPanelsPage final : public gui_forms::Component {
public:
    LayoutPanelsPage(const std::shared_ptr<Surface>& page,
                     const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        (*page).add_at(label("gallery.layout.heading", "RETAINED LAYOUT PANELS",
                             22.0, 700, shell_blue_dark),
                       {24.0, 18.0, 620.0, 34.0});
        (*page).add_at(label("gallery.layout.lead",
                             "Preferred bounds remain authored data while flow and table containers assign independent retained slots.",
                             12.0, 400, muted),
                       {26.0, 52.0, 920.0, 24.0});
        build_flow_cards(page);
        build_table(page);
        note_ = label("gallery.layout.note",
                      "Resize proof is framework-owned: authored bounds never become the parent-assigned slot.",
                      11.0, 600, green);
        (*page).add_at(note_, {26.0, 624.0, 800.0, 22.0});
        on(table_share_.changed(), *this, &LayoutPanelsPage::on_share_changed);
        on(percent_one_.changed(), *this, &LayoutPanelsPage::on_percent_one_changed);
    }

private:
    void build_flow_cards(const std::shared_ptr<Surface>& page) {
        std::shared_ptr<LayoutGroup> flow_group = group(
            "gallery.layout.flow.group",
            "FlowLayoutPanelCompat · four directions · wrap · physical margins · FlowBreak",
            {930.0, 218.0});
        (*page).add_at(flow_group, {24.0, 92.0, 930.0, 218.0});
        constexpr std::array<std::pair<FlowDirection, std::string_view>, 4> directions{{
            {FlowDirection::left_to_right, "Left to right + break"},
            {FlowDirection::right_to_left, "Right to left"},
            {FlowDirection::top_down, "Top down"},
            {FlowDirection::bottom_up, "Bottom up"},
        }};
        for (std::size_t index = 0U; index < directions.size(); ++index) {
            std::shared_ptr<LayoutGroup> card = group(
                "gallery.layout.flow.card." + std::to_string(index),
                std::string(directions[index].second), {210.0, 156.0});
            (*flow_group).add_at(card, {18.0 + static_cast<double>(index) * 224.0,
                                        40.0, 210.0, 156.0});
            std::shared_ptr<FlowLayoutPanelCompat> flow = make_control<FlowLayoutPanelCompat>(
                StableId("gallery.layout.flow." + std::to_string(index)));
            (*flow).accessible_name = std::string(directions[index].second) +
                                        " flow layout";
            (*flow).set_flow_direction(directions[index].first);
            (*flow).padding = {5.0, 5.0, 5.0, 5.0};
            (*card).add_at(flow, {6.0, 26.0, 198.0, 122.0});
            std::shared_ptr<Button> break_item;
            for (std::size_t item_index = 0U; item_index < 5U; ++item_index) {
                std::shared_ptr<Button> item = make_control<Button>(
                    StableId("gallery.layout.flow." + std::to_string(index) +
                             ".item." + std::to_string(item_index)),
                    std::string(1U, static_cast<char>('A' + item_index)));
                (*item).set_visual_style(item_index % 2U == 0U
                                             ? ButtonVisualStyle::command
                                             : ButtonVisualStyle::standard);
                (*item).set_requested_bounds(
                    {0.0, 0.0,
                     index < 2U ? 48.0 + static_cast<double>(item_index % 2U) * 10.0
                                : 52.0,
                     index < 2U ? 26.0 : 19.0});
                (*item).margin = {3.0, 2.0, 3.0, 2.0};
                (*flow).add_child(item);
                if (item_index == 1U) break_item = item;
            }
            if (index == 0U && break_item) (*flow).set_flow_break(*break_item, true);
        }
    }

    void build_table(const std::shared_ptr<Surface>& page) {
        std::shared_ptr<LayoutGroup> table_group = group(
            "gallery.layout.table.group",
            "TableLayoutPanelCompat · absolute + auto + weighted percent tracks · spans + automatic placement",
            {930.0, 292.0});
        (*page).add_at(table_group, {24.0, 330.0, 930.0, 292.0});
        table_ = make_control<TableLayoutPanelCompat>(StableId("gallery.layout.table"));
        (*table_).accessible_name = "Mixed-track table layout specimen";
        (*table_).padding = {6.0, 6.0, 6.0, 6.0};
        (*table_).set_column_count(4U);
        (*table_).set_row_count(3U);
        (*table_).set_column_style(0U, {TableSizeMode::absolute, 140.0});
        (*table_).set_column_style(1U, {TableSizeMode::auto_size, 0.0});
        (*table_).set_column_style(2U, {TableSizeMode::percent, 1.0});
        (*table_).set_column_style(3U, {TableSizeMode::percent, 2.0});
        (*table_).set_row_style(0U, {TableSizeMode::absolute, 42.0});
        (*table_).set_row_style(1U, {TableSizeMode::auto_size, 0.0});
        (*table_).set_row_style(2U, {TableSizeMode::percent, 1.0});
        (*table_).set_cell_border_style(TableCellBorderStyle::inset);
        (*table_group).add_at(table_, {18.0, 38.0, 894.0, 218.0});

        std::shared_ptr<Label> header = label(
            "gallery.layout.table.header",
            "AUTHORED PREFERRED SIZE  →  RETAINED CELL SLOT  →  COMMITTED BOUNDS",
            12.0, 700, shell_blue_dark);
        (*header).set_requested_bounds({0.0, 0.0, 720.0, 26.0});
        (*header).margin = {8.0, 7.0, 8.0, 5.0};
        (*table_).add_child(header);
        (*table_).set_cell_position(*header, {0U, 0U});
        (*table_).set_column_span(*header, 4U);

        std::shared_ptr<Label> absolute = label("gallery.layout.table.absolute",
                                                "Absolute · 140 px", 11.0, 600, ink);
        (*absolute).set_requested_bounds({0.0, 0.0, 112.0, 24.0});
        (*table_).add_child(absolute);
        (*table_).set_cell_position(*absolute, {0U, 1U});

        std::shared_ptr<Button> automatic = make_control<Button>(
            StableId("gallery.layout.table.auto"), "Auto content");
        (*automatic).set_visual_style(ButtonVisualStyle::accent);
        (*automatic).set_requested_bounds({0.0, 0.0, 128.0, 30.0});
        (*table_).add_child(automatic);
        (*table_).set_cell_position(*automatic, {1U, 1U});

        std::shared_ptr<CheckBox> percent_one = make_control<CheckBox>(
            StableId("gallery.layout.table.percent.one"), "Percent · 1 share");
        (*percent_one).set_indicator_style(ChoiceIndicatorStyle::modern);
        (*percent_one).set_requested_bounds({0.0, 0.0, 140.0, 28.0});
        (*percent_one).bind(percent_one_);
        (*table_).add_child(percent_one);
        (*table_).set_cell_position(*percent_one, {2U, 1U});

        std::shared_ptr<TrackBar> percent_two = make_control<TrackBar>(
            StableId("gallery.layout.table.percent.two"));
        (*percent_two).accessible_name = "Percent two-share cell slider";
        (*percent_two).set_visual_style(TrackBarVisualStyle::filled);
        (*percent_two).set_requested_bounds({0.0, 0.0, 230.0, 30.0});
        (*percent_two).bind(table_share_);
        (*table_).add_child(percent_two);
        (*table_).set_cell_position(*percent_two, {3U, 1U});

        std::shared_ptr<Label> span_note = label(
            "gallery.layout.table.span",
            "ColumnSpan = 2 · lookup covers both cells", 11.0, 600, green);
        (*span_note).set_requested_bounds({0.0, 0.0, 260.0, 28.0});
        (*table_).add_child(span_note);
        (*table_).set_cell_position(*span_note, {0U, 2U});
        (*table_).set_column_span(*span_note, 2U);

        std::shared_ptr<Button> auto_placed = make_control<Button>(
            StableId("gallery.layout.table.auto.placed"), "Auto-placed cell");
        (*auto_placed).set_visual_style(ButtonVisualStyle::command);
        (*auto_placed).set_requested_bounds({0.0, 0.0, 136.0, 30.0});
        (*table_).add_child(auto_placed);

        // Same Value<double> as the slider: the bar follows the model, not
        // the slider's input events.
        std::shared_ptr<ProgressBar> progress = make_control<ProgressBar>(
            StableId("gallery.layout.table.progress"));
        (*progress).set_requested_bounds({0.0, 0.0, 230.0, 24.0});
        (*progress).bind(table_share_);
        (*table_).add_child(progress);
    }

    void on_share_changed(const double value) {
        (*note_).set_text("Shared table value " +
                          std::to_string(static_cast<int>(value)) +
                          " · slider and progress bar bind one Value<double>; authored bounds never become the parent-assigned slot.");
    }

    void on_percent_one_changed(const bool checked) {
        (*note_).set_text(checked
            ? "Percent · 1 share checked · the check box binds a Value<bool>; authored bounds never become the parent-assigned slot."
            : "Percent · 1 share cleared · the check box binds a Value<bool>; authored bounds never become the parent-assigned slot.");
    }

    std::shared_ptr<TableLayoutPanelCompat> table_;
    std::shared_ptr<Label> note_;
    Value<double> table_share_{68.0};
    Value<bool> percent_one_{true};
};

void add_layout_panels(const std::shared_ptr<Surface>& page,
                       const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<LayoutPanelsPage>(page, context));
}

} // namespace gui_forms::gallery::pages
