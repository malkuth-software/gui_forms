#include "support.hpp"
#include "gui_forms/controls/block_panel.hpp"
#include "gui_forms/controls/flex_panel.hpp"
#include "gui_forms/controls/grid_panel.hpp"
#include "gui_forms/controls/text_block.hpp"

namespace gallery_pages_tests {

void test_css_layout_panels(gui_forms::Window& window) {
    select_page(window, "css-layout-panels");
    require(window.find("gallery.css-layout.heading") != nullptr, "css-layout-panels page shows its heading");

    // The flex row: equal bases, so the two-share item grows twice as much;
    // the auto-margin item ends flush with the content box's right edge.
    const std::shared_ptr<FlexPanel> flex_row =
        std::dynamic_pointer_cast<FlexPanel>(window.find("gallery.css-layout.flex.row"));
    const Control::Ptr one = window.find("gallery.css-layout.flex.row.item.0");
    const Control::Ptr two = window.find("gallery.css-layout.flex.row.item.2");
    const Control::Ptr pushed = window.find("gallery.css-layout.flex.row.pushed");
    const std::shared_ptr<FlexPanel> flex_column =
        std::dynamic_pointer_cast<FlexPanel>(window.find("gallery.css-layout.flex.column"));
    const Control::Ptr narrow = window.find("gallery.css-layout.flex.column.item.0");
    const Control::Ptr wide = window.find("gallery.css-layout.flex.column.item.2");
    window.perform_layout();
    require(flex_row && one && two && pushed && flex_column && narrow && wide,
            "the page must retain its two FlexPanel specimens");
    const double one_growth = (*one).arranged_bounds().width - (*one).requested_bounds().width;
    const double two_growth = (*two).arranged_bounds().width - (*two).requested_bounds().width;
    const Rect row_bounds = (*flex_row).arranged_bounds();
    const Rect pushed_bounds = (*pushed).arranged_bounds();
    require(one_growth > 0.0 && std::abs(two_growth - 2.0 * one_growth) < 1.0e-6,
            "a two-share flex item must grow twice as much as a one-share item");
    require(std::abs(pushed_bounds.x + pushed_bounds.width - (row_bounds.width - 6.0)) < 1.0e-6 &&
                pushed_bounds.y > (*one).arranged_bounds().y,
            "the auto-margin item must wrap to its own line and end at the content edge");
    const Rect narrow_bounds = (*narrow).arranged_bounds();
    const Rect wide_bounds = (*wide).arranged_bounds();
    require(std::abs((narrow_bounds.x + narrow_bounds.width / 2.0) - (wide_bounds.x + wide_bounds.width / 2.0)) <
                    1.0e-6 &&
                narrow_bounds.width < wide_bounds.width,
            "the centred column must centre items of different content widths");

    // Every specimen lies within the page body (660 px high), so none is
    // clipped.
    const Control::Ptr flex_group = window.find("gallery.css-layout.flex.group");
    require(flex_group && (*flex_group).arranged_bounds().y + (*flex_group).arranged_bounds().height <= 660.0,
            "the flex group must fit the page body");

    // The named areas: the head and foot span both columns, the main area
    // follows the fixed 90 px side column and its gap.
    const std::shared_ptr<GridPanel> areas =
        std::dynamic_pointer_cast<GridPanel>(window.find("gallery.css-layout.grid.areas"));
    const Control::Ptr head = window.find("gallery.css-layout.grid.areas.head");
    const Control::Ptr main = window.find("gallery.css-layout.grid.areas.main");
    const Control::Ptr foot = window.find("gallery.css-layout.grid.areas.foot");
    require(areas && head && main && foot, "the page must retain its named-area GridPanel");
    const Rect area_bounds = (*areas).arranged_bounds();
    const Rect foot_bounds = (*foot).arranged_bounds();
    require(std::abs((*head).arranged_bounds().width - area_bounds.width) < 1.0e-6 &&
                std::abs(foot_bounds.width - area_bounds.width) < 1.0e-6 &&
                std::abs((*main).arranged_bounds().x - 96.0) < 1.0e-6 &&
                std::abs(foot_bounds.y + foot_bounds.height - area_bounds.height) < 1.0e-6,
            "named areas must span their columns and the fraction row must fill the height");

    // The cards: five minmax(86px, 1fr) columns fit 476 px; the spanning
    // card covers two of them and the gap between.
    const std::shared_ptr<GridPanel> cards =
        std::dynamic_pointer_cast<GridPanel>(window.find("gallery.css-layout.grid.cards"));
    const Control::Ptr first_card = window.find("gallery.css-layout.grid.cards.item.0");
    const Control::Ptr spanning = window.find("gallery.css-layout.grid.cards.item.1");
    require(cards && first_card && spanning && (*cards).layout_snapshot().columns().size() == 5U,
            "five auto-fill card columns must fit the specimen");
    const double card_width = (*first_card).arranged_bounds().width;
    require(card_width >= 86.0 && std::abs((*spanning).arranged_bounds().width - (2.0 * card_width + 6.0)) < 1.0e-6,
            "the spanning card must cover two columns and their gap");

    const Control::Ptr grid_group = window.find("gallery.css-layout.grid.group");
    require(grid_group && (*grid_group).arranged_bounds().y + (*grid_group).arranged_bounds().height <= 660.0,
            "the grid group must fit the page body");

    // The stacked blocks: 4 px margins collapse to 4 between neighbours,
    // and the middle block is 200 px wide and centred.
    const std::shared_ptr<BlockPanel> stack =
        std::dynamic_pointer_cast<BlockPanel>(window.find("gallery.css-layout.block.stack"));
    const Control::Ptr first_block = window.find("gallery.css-layout.block.stack.item.0");
    const Control::Ptr middle_block = window.find("gallery.css-layout.block.stack.item.1");
    require(stack && first_block && middle_block, "the page must retain its stacked BlockPanel");
    const Rect stack_bounds = (*stack).arranged_bounds();
    const Rect first_bounds = (*first_block).arranged_bounds();
    const Rect middle_bounds = (*middle_block).arranged_bounds();
    require(std::abs(first_bounds.y - 4.0) < 1.0e-6 && std::abs(first_bounds.width - stack_bounds.width) < 1.0e-6 &&
                std::abs(middle_bounds.y - (first_bounds.y + first_bounds.height + 4.0)) < 1.0e-6 &&
                std::abs(middle_bounds.width - 200.0) < 1.0e-6 &&
                std::abs(middle_bounds.x - (stack_bounds.width - 200.0) / 2.0) < 1.0e-6,
            "stacked blocks must collapse their margins and centre the narrowed block");

    // The inline line: four 96 px margin boxes fit 476 px; the rest wrap,
    // and every line is centred.
    const std::shared_ptr<BlockPanel> line =
        std::dynamic_pointer_cast<BlockPanel>(window.find("gallery.css-layout.block.line"));
    require(line && (*line).layout_snapshot().lines().size() == 2U && (*line).layout_snapshot().lines()[0].count == 4U,
            "the inline line must hold four buttons and wrap the other two");
    const Control::Ptr first_inline = window.find("gallery.css-layout.block.line.item.0");
    require(first_inline && std::abs((*first_inline).arranged_bounds().x - (476.0 - 384.0) / 2.0) < 1.0e-6,
            "the first line must be centred");

    const Control::Ptr block_group = window.find("gallery.css-layout.block.group");
    require(block_group && (*block_group).arranged_bounds().y + (*block_group).arranged_bounds().height <= 660.0,
            "the block group must fit the page body");

    // The text row: the caption and the larger value share a baseline; the
    // paragraph wraps within its 476 px.
    const std::shared_ptr<TextBlock> caption =
        std::dynamic_pointer_cast<TextBlock>(window.find("gallery.css-layout.text.rate.caption"));
    const std::shared_ptr<TextBlock> value =
        std::dynamic_pointer_cast<TextBlock>(window.find("gallery.css-layout.text.rate.value"));
    const std::shared_ptr<TextBlock> paragraph =
        std::dynamic_pointer_cast<TextBlock>(window.find("gallery.css-layout.text.paragraph"));
    require(caption && value && paragraph, "the page must retain its TextBlock specimens");
    const Rect caption_bounds = (*caption).arranged_bounds();
    const Rect value_bounds = (*value).arranged_bounds();
    const double caption_baseline = caption_bounds.y + (*caption).first_baseline(caption_bounds.width).value();
    const double value_baseline = value_bounds.y + (*value).first_baseline(value_bounds.width).value();
    require(std::abs(caption_baseline - value_baseline) < 1.0e-6 && caption_bounds.y > value_bounds.y,
            "the caption must drop to the larger value's baseline");
    require((*paragraph).last_baseline(476.0).value() > (*paragraph).first_baseline(476.0).value(),
            "the paragraph must wrap to more than one line");
    const Control::Ptr text_group = window.find("gallery.css-layout.text.group");
    require(text_group && (*text_group).arranged_bounds().y + (*text_group).arranged_bounds().height <= 660.0,
            "the text group must fit the page body");

    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("Growing flex row") != std::string::npos &&
                semantics.find("Centred flex column") != std::string::npos &&
                semantics.find("Named grid areas") != std::string::npos &&
                semantics.find("Stacked blocks") != std::string::npos &&
                semantics.find("Centred inline line") != std::string::npos &&
                semantics.find("Auto-fill grid cards") != std::string::npos &&
                semantics.find("Baseline-aligned text") != std::string::npos &&
                semantics.find("Wrapped paragraph") != std::string::npos,
            "the flex and grid specimens must publish their named group semantics");
}

} // namespace gallery_pages_tests
