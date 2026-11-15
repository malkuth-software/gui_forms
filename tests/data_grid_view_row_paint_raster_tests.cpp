// DataGridView's row paint hooks in pixels (Windows Forms' RowPrePaint and
// RowPostPaint, as part hooks): a row_pre_paint answer gives a row's cells
// an authored background, and a row_post_paint answer asks for an overlay
// part with an authored ground over another row's cells. Drawn through the
// offscreen raster and read back from the capture: the answered background
// fills the row's cell beside its text, the overlay covers the cell it was
// placed on, and with the hooks disconnected neither colour is shown.

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/data_grid_view.hpp"
#include "gui_forms/controls/data_grid_view_cells.hpp"
#include "gui_forms/controls/data_grid_view_rows.hpp"
#include "gui_forms/controls/data_grid_view_types.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

int failures = 0;

void require(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "gui_forms_data_grid_view_row_paint_raster_tests: " << message << '\n';
    ++failures;
}

struct Pixel final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
};

// The capture's pixel at logical (x, y), at scale 1: premultiplied BGRA8
// rows of pixel_width pixels; the window's backplane makes it opaque.
Pixel pixel_at(const WindowCapture& capture, double x, double y) {
    const std::size_t column = static_cast<std::size_t>(x);
    const std::size_t row = static_cast<std::size_t>(y);
    const std::size_t offset = (row * static_cast<std::size_t>(capture.pixel_width) + column) * 4U;
    Pixel pixel{};
    if (column >= capture.pixel_width || row >= capture.pixel_height || offset + 3U >= capture.bgra.size()) {
        return pixel;
    }
    pixel.blue = static_cast<std::uint8_t>(capture.bgra[offset]);
    pixel.green = static_cast<std::uint8_t>(capture.bgra[offset + 1U]);
    pixel.red = static_cast<std::uint8_t>(capture.bgra[offset + 2U]);
    return pixel;
}

// Whether a channel is within a few levels of the wanted one: the colours
// are opaque solid fills, so only rounding separates them.
bool near_channel(std::uint8_t channel, std::uint8_t wanted) {
    const int difference = static_cast<int>(channel) - static_cast<int>(wanted);
    const bool result = difference >= -6 && difference <= 6;
    return result;
}

bool shows(const Pixel& pixel, Color wanted) {
    const bool result = near_channel(pixel.red, wanted.red) && near_channel(pixel.green, wanted.green) &&
                        near_channel(pixel.blue, wanted.blue);
    return result;
}

std::optional<WindowCapture> capture(OffscreenAttachment& attached) {
    OffscreenCaptureOptions options{};
    options.scale = 1.0;
    std::optional<WindowCapture> picture = attached.capture_window(options);
    return picture;
}

// The rows the hooks answer: the pre-paint answer colours the third row's
// cells, the post-paint answer places one overlay on the second row.
constexpr std::size_t background_row = 2U;
constexpr std::size_t overlay_row = 1U;

// Answers row_pre_paint with an authored background for background_row.
struct AnswerBackground final {
    Color background{};
    void operator()(DataGridViewRowPrePaintEvent& event) const {
        if (event.row.value == background_row) event.background = background;
    }
};

// Answers row_post_paint with one plain glyph part (a ground, so its
// authored background is its only fill) at `bounds` over overlay_row.
struct AnswerOverlay final {
    Rect bounds{};
    Color background{};
    void operator()(DataGridViewRowPostPaintEvent& event) const {
        if (event.row.value != overlay_row) return;
        event.overlays.push_back(DataGridViewRowOverlayPart{bounds, PartRole::glyph, PartMark::none, background});
    }
};

const DataGridView::Cell* find_cell(const std::vector<DataGridView::Cell>& cells, std::size_t row,
                                    std::size_t column) {
    for (const DataGridView::Cell& cell : cells) {
        if (cell.address.row.value == row && cell.address.column == column) return &cell;
    }
    return nullptr;
}
// The result borrows from `cells`: a temporary would dangle.
const DataGridView::Cell* find_cell(std::vector<DataGridView::Cell>&& cells, std::size_t row,
                                    std::size_t column) = delete;

std::shared_ptr<DataGridView> make_grid() {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.row.paint.raster"));
    std::vector<DataGridViewColumn> columns;
    columns.push_back({.name = "name", .header_text = "Name", .width = 120.0});
    columns.push_back({.name = "note", .header_text = "Note", .width = 120.0});
    (*grid).set_columns(columns);
    (*grid).rows.append({.stable_id = {"alpha"}, .cells = {BindingValue{std::string("A")}, BindingValue{std::string("a")}}});
    (*grid).rows.append({.stable_id = {"bravo"}, .cells = {BindingValue{std::string("B")}, BindingValue{std::string("b")}}});
    (*grid).rows.append({.stable_id = {"charlie"}, .cells = {BindingValue{std::string("C")}, BindingValue{std::string("c")}}});
    (*grid).allow_user_to_add_rows = false;
    // The cells are painted on the UI thread, so the capture holds them.
    (*grid).cells_on_sub_surface = false;
    return grid;
}

void test_row_paint_hooks() {
    OffscreenRasterOptions raster_options{};
    raster_options.font_directory = GUI_FORMS_TEST_FONT_DIR;
    OffscreenRasterCreateResult made_raster = OffscreenRaster::create(raster_options);
    require(made_raster.succeeded(), "the bundled faces make a raster");
    if (!made_raster.succeeded()) return;
    const std::unique_ptr<OffscreenRaster> raster = made_raster.take_raster();

    const std::shared_ptr<DataGridView> grid = make_grid();
    Window window(grid, {360.0, 200.0});
    window.perform_layout();
    OffscreenAttachment attached(*raster, window);

    // The grid is the window's root, so its coordinates are the capture's.
    const std::vector<DataGridView::Cell> cells = (*grid).realized_cells();
    const DataGridView::Cell* coloured = find_cell(cells, background_row, 1U);
    const DataGridView::Cell* covered = find_cell(cells, overlay_row, 1U);
    require(coloured != nullptr && covered != nullptr, "the answered rows' cells are shown");
    if (coloured == nullptr || covered == nullptr) return;
    const Rect coloured_cell = (*coloured).parts.cell.bounds;
    const Rect covered_cell = (*covered).parts.cell.bounds;
    require(coloured_cell.width > 40.0 && coloured_cell.height > 10.0, "the cells are laid out");
    // The background is read beside the cell's one-letter text, inside its
    // grid lines; the overlay is placed in the middle of the other cell.
    const Point background_probe{coloured_cell.x + coloured_cell.width - 10.0,
                                 coloured_cell.y + coloured_cell.height * 0.5};
    const Rect overlay{covered_cell.x + 30.0, covered_cell.y + 3.0, 40.0, covered_cell.height - 6.0};
    const Point overlay_probe{overlay.x + overlay.width * 0.5, overlay.y + overlay.height * 0.5};

    const Color background = Color::rgba(30, 160, 90);
    const Color mark = Color::rgba(200, 40, 200);
    SubscriptionToken pre = (*grid).row_pre_paint().subscribe(AnswerBackground{background});
    SubscriptionToken post = (*grid).row_post_paint().subscribe(AnswerOverlay{overlay, mark});
    (*grid).invalidate(Dirty::paint);
    std::optional<WindowCapture> hooked = capture(attached);
    require(hooked.has_value(), "the grid is captured with its hooks");
    if (!hooked) return;
    require(shows(pixel_at(*hooked, background_probe.x, background_probe.y), background),
            "the pre-paint answer's background fills the row's cell");
    require(shows(pixel_at(*hooked, overlay_probe.x, overlay_probe.y), mark),
            "the post-paint answer's overlay covers the other row's cell");
    require(!shows(pixel_at(*hooked, covered_cell.x + covered_cell.width - 10.0, overlay_probe.y), mark),
            "the overlay stays inside its bounds");

    pre.disconnect();
    post.disconnect();
    (*grid).invalidate(Dirty::paint);
    std::optional<WindowCapture> plain = capture(attached);
    require(plain.has_value(), "the grid is captured without hooks");
    if (!plain) return;
    require(!shows(pixel_at(*plain, background_probe.x, background_probe.y), background),
            "without the hook the row keeps the theme's background");
    require(!shows(pixel_at(*plain, overlay_probe.x, overlay_probe.y), mark), "without the hook there is no overlay");
}

} // namespace

int main() {
    test_row_paint_hooks();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "gui_forms_data_grid_view_row_paint_raster_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
