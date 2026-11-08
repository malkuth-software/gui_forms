#include "support.hpp"

namespace gallery_pages_tests {

void test_retained_layout_panels(gui_forms::Window& window) {
    select_page(window, "retained-layout-panels");
    constexpr std::array<FlowDirection, 4> directions{
        FlowDirection::left_to_right, FlowDirection::right_to_left,
        FlowDirection::top_down, FlowDirection::bottom_up};
    for (std::size_t index = 0U; index < directions.size(); ++index) {
        const std::shared_ptr<FlowLayoutPanelCompat> flow =
            std::dynamic_pointer_cast<FlowLayoutPanelCompat>(window.find(
                "gallery.layout.flow." + std::to_string(index)));
        require(flow && (*flow).flow_direction() == directions[index] &&
                    (*flow).children().size() == 5U,
                "layout page must retain one public FlowLayoutPanelCompat per direction");
    }
    const std::shared_ptr<FlowLayoutPanelCompat> breaking =
        std::dynamic_pointer_cast<FlowLayoutPanelCompat>(window.find("gallery.layout.flow.0"));
    const Control::Ptr second_item = window.find("gallery.layout.flow.0.item.1");
    const Control::Ptr third_item = window.find("gallery.layout.flow.0.item.2");
    window.perform_layout();
    require(breaking && second_item && third_item &&
                (*breaking).flow_break[*second_item].read() &&
                (*third_item).arranged_bounds().y > (*second_item).arranged_bounds().y &&
                (*third_item).requested_bounds().x == 0.0,
            "a FlowBreak must start a new flow line without rewriting authored bounds");

    const std::shared_ptr<TableLayoutPanelCompat> table =
        std::dynamic_pointer_cast<TableLayoutPanelCompat>(window.find("gallery.layout.table"));
    const Control::Ptr header = window.find("gallery.layout.table.header");
    const Control::Ptr span_note = window.find("gallery.layout.table.span");
    const Control::Ptr auto_placed = window.find("gallery.layout.table.auto.placed");
    const Control::Ptr table_progress = window.find("gallery.layout.table.progress");
    // cell_position reports the resolved cell: the two children added without
    // an authored position flow row-major into the first cells left free after
    // the explicit row-2 span, so they land at columns 2 and 3 of row 2.
    require(table && header && span_note && auto_placed && table_progress &&
                (*table).column_count() == 4U && (*table).row_count() == 3U &&
                (*table).column_span[*header].read() == 4U &&
                (*table).column_span[*span_note].read() == 2U &&
                (*table).column_span[*auto_placed].read() == 1U &&
                (*table).cell_position(*span_note) ==
                    std::optional<TableLayoutCellPosition>({0U, 2U}) &&
                (*table).cell_position(*auto_placed) ==
                    std::optional<TableLayoutCellPosition>({2U, 2U}) &&
                (*table).cell_position(*table_progress) ==
                    std::optional<TableLayoutCellPosition>({3U, 2U}) &&
                (*table).control_from_position(2U, 2U) == auto_placed &&
                (*header).arranged_bounds().width > (*span_note).arranged_bounds().width,
            "table page must retain explicit spans beside automatic placement");

    // One Value<double> drives the slider and the progress bar; a state
    // change through either control reaches the other without input events.
    const std::shared_ptr<TrackBar> slider =
        std::dynamic_pointer_cast<TrackBar>(window.find("gallery.layout.table.percent.two"));
    const std::shared_ptr<ProgressBar> progress =
        std::dynamic_pointer_cast<ProgressBar>(window.find("gallery.layout.table.progress"));
    const std::shared_ptr<Label> note =
        std::dynamic_pointer_cast<Label>(window.find("gallery.layout.note"));
    require(slider && progress && note && (*slider).value() == 68.0 &&
                (*progress).value() == 68.0,
            "the table slider and progress bar must start from the shared model value");
    (*slider).set_value(40.0);
    require((*progress).value() == 40.0 &&
                (*note).text.read().find("Shared table value 40") != std::string::npos,
            "a bound Value must project the slider's state change into the progress bar");

    const std::shared_ptr<CheckBox> percent_one =
        std::dynamic_pointer_cast<CheckBox>(window.find("gallery.layout.table.percent.one"));
    require(percent_one && (*percent_one).checked(),
            "the table check box must start from its bound Value<bool>");
    click(window, percent_one);
    require(!(*percent_one).checked() &&
                (*note).text.read().find("Percent · 1 share cleared") != std::string::npos,
            "a user click must reach the page through the bound Value, not the control");

    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("Mixed-track table layout specimen") != std::string::npos &&
                semantics.find("Left to right + break flow layout") != std::string::npos,
            "layout panels must publish their named group semantics");
}

} // namespace gallery_pages_tests
