// Stage S7: a DataGridView's cell region on a threaded sub-surface
// (threaded sub-surfaces design note section 2, coordinator ruling
// 2026-11-05, option 1), against synthetic snapshots: the snapshot record
// is checked whole, the shared cell painter draws a resolved cell's parts in
// the grid's order, and SubSurfaceHost::redraw_all carries each snapshot
// with its content epoch, so the frame tagged with an epoch shows that
// epoch's snapshot (newest wins; a resize keeps a waiting update); and a
// region text provider fills virtual rows' cells on the region's thread.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/controls/data_grid_view.hpp"
#include "gui_forms/controls/data_grid_view_cells.hpp"
#include "../src/controls/data_grid_view/data_grid_view_cells_control.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "headless_host.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;

// Throws on failure, so the check runs in every build type.
void require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::chrono::seconds wait_limit{10};

// The texts a painter drew, in order. Shared by the framebuffers a window
// makes for its sub-surfaces: written on their thread, read by the test,
// under the mutex. `frames` grows at each framebuffer paint (begin).
struct TextLog final {
    std::mutex mutex;
    std::vector<std::vector<std::string>> frames;
};

// Records draw_text_utf8; draws nothing else. With a log, each text goes to
// the log's newest frame; without one, to `texts`.
class TextPainter final : public Painter {
public:
    explicit TextPainter(std::shared_ptr<TextLog> log) : log_(std::move(log)) {}
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color) override {
        if (!log_) {
            texts.emplace_back(text);
            return;
        }
        const std::lock_guard<std::mutex> lock((*log_).mutex);
        if ((*log_).frames.empty()) (*log_).frames.emplace_back();
        (*log_).frames.back().emplace_back(text);
    }
    void draw_image(ImageId, Rect, double) override {}

    std::vector<std::string> texts;

private:
    std::shared_ptr<TextLog> log_;
};

// A framebuffer with real (blank) pixels whose painter logs texts.
class TextFramebuffer final : public PaintFramebuffer {
public:
    TextFramebuffer(std::uint32_t width, std::uint32_t height, std::shared_ptr<TextLog> log)
        : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * height * 4U), log_(log),
          painter_(std::move(log)) {}
    bool begin(const ImageRegistry&, Rect) override {
        const std::lock_guard<std::mutex> lock((*log_).mutex);
        (*log_).frames.emplace_back();
        return true;
    }
    void end() override {}
    Painter& painter() noexcept override { return painter_; }
    std::span<std::byte> pixels() noexcept override { return pixels_; }
    std::size_t row_bytes() const noexcept override { return static_cast<std::size_t>(width_) * 4U; }
    std::uint32_t width() const noexcept override { return width_; }
    std::uint32_t height() const noexcept override { return height_; }

private:
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<std::byte> pixels_;
    std::shared_ptr<TextLog> log_;
    TextPainter painter_;
};

// The window's framebuffer painter: makes TextFramebuffers sharing one log.
class TextRaster final : public Painter {
public:
    explicit TextRaster(std::shared_ptr<TextLog> log) : log_(std::move(log)) {}
    std::unique_ptr<PaintFramebuffer> create_framebuffer(Size logical_size, double scale) override {
        const std::uint32_t width = static_cast<std::uint32_t>(std::ceil(logical_size.width * scale));
        const std::uint32_t height = static_cast<std::uint32_t>(std::ceil(logical_size.height * scale));
        if (width == 0U || height == 0U) return {};
        std::unique_ptr<PaintFramebuffer> result = std::make_unique<TextFramebuffer>(width, height, log_);
        return result;
    }
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

private:
    std::shared_ptr<TextLog> log_;
};

// Appends `piece` to the snapshot's text: its range there.
DataGridViewCellsTextRange append_text(DataGridViewCellsSnapshot& snapshot, std::string_view piece) {
    DataGridViewCellsTextRange range{};
    range.begin_byte = snapshot.text.size();
    snapshot.text.append(piece);
    range.end_byte = snapshot.text.size();
    return range;
}

// A plain text cell at `bounds` showing `text`.
DataGridViewCellsCell text_cell(DataGridViewCellsSnapshot& snapshot, Rect bounds, std::string_view text) {
    DataGridViewCellsCell cell{};
    cell.parts.cell = PartGeometry::from_bounds(bounds);
    cell.parts.selection = PartGeometry::from_bounds(bounds);
    cell.parts.content = PartGeometry::from_bounds(bounds);
    cell.parts.focus = PartGeometry::from_bounds(bounds);
    cell.text = append_text(snapshot, text);
    return cell;
}

// One cell covering a 60 by 30 region, showing `text`.
DataGridViewCellsSnapshot one_cell(std::string_view text) {
    DataGridViewCellsSnapshot snapshot{};
    snapshot.well = PartGeometry::from_bounds(Rect{-4.0, -4.0, 68.0, 38.0});
    snapshot.cells.push_back(text_cell(snapshot, Rect{0.0, 0.0, 60.0, 30.0}, text));
    return snapshot;
}

std::unique_ptr<SubSurfaceContentUpdate> update_showing(std::string_view text) {
    std::unique_ptr<SubSurfaceContentUpdate> update = std::make_unique<DataGridViewCellsUpdate>(one_cell(text));
    return update;
}

bool contains(const std::vector<std::string>& texts, std::string_view text) {
    const std::vector<std::string>::const_iterator found = std::find(texts.begin(), texts.end(), text);
    return found != texts.end();
}

std::size_t position_of(const std::vector<std::string>& texts, std::string_view text) {
    const std::vector<std::string>::const_iterator found = std::find(texts.begin(), texts.end(), text);
    const std::size_t position = static_cast<std::size_t>(found - texts.begin());
    return position;
}

void snapshots_are_checked_whole() {
    DataGridViewCellsSnapshot good = one_cell("alpha");
    require(data_grid_view_cells_snapshot_valid(good), "a snapshot whose ranges lie in its buffers is valid");
    DataGridViewCellsSnapshot text_past = one_cell("alpha");
    text_past.cells[0].text.end_byte = text_past.text.size() + 1U;
    require(!data_grid_view_cells_snapshot_valid(text_past), "text past the buffer is refused");
    DataGridViewCellsSnapshot reversed = one_cell("alpha");
    reversed.cells[0].text.begin_byte = 3U;
    reversed.cells[0].text.end_byte = 2U;
    require(!data_grid_view_cells_snapshot_valid(reversed), "a reversed range is refused");
    DataGridViewCellsSnapshot lines_past = one_cell("alpha beta");
    lines_past.lines.push_back(DataGridViewCellsTextRange{0U, 5U});
    lines_past.cells[0].lines = DataGridViewCellsLineRange{0U, 2U};
    require(!data_grid_view_cells_snapshot_valid(lines_past), "lines past the line buffer are refused");
    DataGridViewCellsSnapshot line_outside = one_cell("alpha");
    static_cast<void>(append_text(line_outside, "other"));
    line_outside.lines.push_back(DataGridViewCellsTextRange{5U, 10U});
    line_outside.cells[0].lines = DataGridViewCellsLineRange{0U, 1U};
    require(!data_grid_view_cells_snapshot_valid(line_outside), "a line outside its cell's text is refused");
    DataGridViewCellsSnapshot provided_without_row = one_cell("alpha");
    provided_without_row.cells[0].provided = DataGridViewCellsProvided{.provided = true, .row = 0U, .column = 0U};
    require(!data_grid_view_cells_snapshot_valid(provided_without_row), "a provided cell naming no row is refused");
    provided_without_row.provided_rows.push_back("v0");
    require(data_grid_view_cells_snapshot_valid(provided_without_row), "a provided cell naming its row is valid");

    DataGridViewCells cells(StableId("grid"));
    require(cells.set_snapshot(good), "a valid snapshot is taken");
    require(!cells.set_snapshot(line_outside), "an invalid snapshot is refused");
    require(cells.snapshot().text == "alpha", "a refused snapshot leaves the previous one");
    require(cells.snapshots_taken() == 1U && cells.snapshots_refused() == 1U, "taken and refused are counted");
}

void cells_paint_resolved_parts() {
    std::shared_ptr<DataGridViewCells> cells = std::make_shared<DataGridViewCells>(StableId("grid"));
    Window window(cells, Size{200.0, 100.0});
    window.flush();
    DataGridViewCellsSnapshot snapshot{};
    snapshot.well = PartGeometry::from_bounds(Rect{-4.0, -4.0, 208.0, 108.0});
    snapshot.cells.push_back(text_cell(snapshot, Rect{0.0, 0.0, 100.0, 20.0}, "alpha"));
    // Wrapped: two lines of the cell's own text.
    DataGridViewCellsCell wrapped = text_cell(snapshot, Rect{100.0, 0.0, 100.0, 40.0}, "one two");
    snapshot.lines.push_back(DataGridViewCellsTextRange{wrapped.text.begin_byte, wrapped.text.begin_byte + 3U});
    snapshot.lines.push_back(DataGridViewCellsTextRange{wrapped.text.begin_byte + 4U, wrapped.text.end_byte});
    wrapped.lines = DataGridViewCellsLineRange{0U, 2U};
    wrapped.lines_box = Rect{102.0, 2.0, 96.0, 36.0};
    wrapped.line_height = 18.0;
    wrapped.content_box = Rect{100.0, 0.0, 100.0, 40.0};
    snapshot.cells.push_back(wrapped);
    // A link: its text through the link part, not the content part.
    DataGridViewCellsCell link = text_cell(snapshot, Rect{0.0, 20.0, 100.0, 20.0}, "link");
    link.parts.content = PartGeometry{};
    link.parts.link = PartGeometry::from_bounds(Rect{2.0, 22.0, 30.0, 16.0});
    snapshot.cells.push_back(link);
    // Under the editor: the content is not drawn.
    DataGridViewCellsCell editing = text_cell(snapshot, Rect{0.0, 40.0, 100.0, 20.0}, "under the editor");
    editing.editing_text = true;
    snapshot.cells.push_back(editing);
    // A button's label is drawn from the cell's text.
    DataGridViewCellsCell button = text_cell(snapshot, Rect{100.0, 40.0, 100.0, 20.0}, "go");
    button.parts.content = PartGeometry{};
    button.parts.button_face = PartGeometry::from_bounds(Rect{102.0, 42.0, 96.0, 16.0});
    button.parts.button_label = PartGeometry::from_bounds(Rect{102.0, 42.0, 96.0, 16.0});
    snapshot.cells.push_back(button);
    // An absent cell draws nothing.
    DataGridViewCellsCell absent = text_cell(snapshot, Rect{0.0, 60.0, 100.0, 20.0}, "absent");
    absent.parts.cell = PartGeometry{};
    snapshot.cells.push_back(absent);
    require((*cells).set_snapshot(std::move(snapshot)), "the synthetic snapshot is valid");

    TextPainter painter({});
    (*cells).on_paint(painter, (*cells).client_rectangle());
    const std::vector<std::string>& texts = painter.texts;
    require(contains(texts, "alpha") && contains(texts, "one") && contains(texts, "two"),
            "text and wrapped lines are drawn");
    require(!contains(texts, "one two"), "a wrapped cell draws its lines, not its whole text");
    require(contains(texts, "link") && contains(texts, "go"), "the link and the button label are drawn");
    require(!contains(texts, "under the editor"), "a cell under the editor leaves its text to the editor");
    require(!contains(texts, "absent"), "an absent cell draws nothing");
    require(position_of(texts, "alpha") < position_of(texts, "one") &&
                position_of(texts, "one") < position_of(texts, "two") &&
                position_of(texts, "two") < position_of(texts, "link"),
            "cells are drawn in the snapshot's order");
    const SemanticDescriptor descriptor = (*cells).semantic_descriptor();
    require(!descriptor.exposed, "the cells are not exposed (the grid publishes them)");
    require((*cells).anatomy().control_id == "data_grid_view", "the cells select the grid's theme rules");
}

[[nodiscard]] bool wait_for_content_tag(const LiveSurface& surface, std::uint64_t tag) {
    const std::chrono::steady_clock::time_point limit = std::chrono::steady_clock::now() + wait_limit;
    while (std::chrono::steady_clock::now() < limit) {
        const LiveSurfaceSnapshot snapshot = surface.snapshot();
        if (snapshot.has_frame && !snapshot.carries_previous_frame && snapshot.content_tag >= tag) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// The texts of the newest painted frame.
std::vector<std::string> newest_frame(TextLog& log) {
    const std::lock_guard<std::mutex> lock(log.mutex);
    if (log.frames.empty()) return {};
    std::vector<std::string> texts = log.frames.back();
    return texts;
}

void region_shows_each_epochs_snapshot() {
    std::shared_ptr<TextLog> log = std::make_shared<TextLog>();
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("grid.window"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("grid.cells"));
    (*host).set_requested_bounds(Rect{10.0, 10.0, 60.0, 30.0});
    (*host).set_content(std::make_unique<DataGridViewCellsContent>(StableId("grid")), true);
    require((*host).redraw_all(update_showing("first")) == 0U,
            "before the window the update waits for the first configuration");
    (*root).add_child(host);
    TextRaster raster(log);
    Window window(root, Size{200.0, 100.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    require((*host).content_epoch() == 1U, "the waiting update rides the first configuration");
    const std::shared_ptr<LiveSurface> surface = (*host).live_surface();
    require(wait_for_content_tag(*surface, 1U), "the cells publish at epoch 1");
    require(newest_frame(*log) == std::vector<std::string>{"first"}, "epoch 1 shows the first snapshot");

    const std::uint64_t second = (*host).redraw_all(update_showing("second"));
    require(second == 2U, "a redraw-all opens a new content epoch");
    require(wait_for_content_tag(*surface, second), "the cells publish at the redraw-all's epoch");
    require(newest_frame(*log) == std::vector<std::string>{"second"}, "the epoch's frame shows its snapshot");

    // Two in one turn: the newest wins, and every update is applied or
    // replaced, once.
    static_cast<void>((*host).redraw_all(update_showing("third")));
    const std::uint64_t fourth = (*host).redraw_all(update_showing("fourth"));
    require(wait_for_content_tag(*surface, fourth), "the cells publish at the newest epoch");
    require(newest_frame(*log) == std::vector<std::string>{"fourth"}, "the newest snapshot wins");
    const SubSurfaceStatistics counted = (*host).statistics();
    require(counted.updates_applied + counted.updates_coalesced == 4U, "each update is applied or replaced once");

    // A redraw-all without an update: a new epoch, the same cells.
    const std::uint64_t bare = (*host).redraw_all({});
    require(wait_for_content_tag(*surface, bare), "a bare redraw-all publishes at its epoch");

    // A resize in the same turn as an update keeps the update: the resize's
    // configuration takes it over if the first is still waiting.
    static_cast<void>((*host).redraw_all(update_showing("fifth")));
    (*host).set_requested_bounds(Rect{10.0, 10.0, 80.0, 40.0});
    window.flush();
    const std::uint64_t resized = (*host).content_epoch();
    require(wait_for_content_tag(*surface, resized), "the cells publish at the resize's epoch");
    require(newest_frame(*log) == std::vector<std::string>{"fifth"}, "a resize keeps the waiting update");
    const LiveSurfaceFrame frame = (*surface).acquire_latest();
    require(frame.width() == 80U && frame.height() == 40U, "the region follows the resize");

    static_cast<void>((*root).remove_child((*host).runtime_id()));
    require(!(*host).started(), "leaving the window stops the cells' thread");
}

// An owner-draw handler that counts its calls and asks for the default
// content.
struct AskDefaultContent final {
    std::size_t* calls{};
    void operator()(ItemContentPaint& item) const {
        ++*calls;
        item.draw_default = true;
    }
};

// Whether a node of `nodes` or below has a stable identity starting with
// `prefix`.
bool has_node_under(const std::vector<SemanticNode>& nodes, std::string_view prefix) {
    for (const SemanticNode& node : nodes) {
        if (std::string_view(node.stable_id).substr(0U, prefix.size()) == prefix) return true;
        if (has_node_under(node.children, prefix)) return true;
    }
    return false;
}

// Part 2: a DataGridView with cells_on_sub_surface paints its headers on the
// UI thread and sends its visible cells to its region, which shows each
// paint's cells at that paint's epoch and publishes no semantics of its own; scrolling sends the new rows; owner
// drawing and turning the property off bring the cells back to the UI
// thread.
void grid_sends_its_cells_to_its_region() {
    std::shared_ptr<TextLog> log = std::make_shared<TextLog>();
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.s7"));
    std::vector<DataGridViewColumn> columns;
    columns.push_back({.name = "a", .header_text = "A", .width = 80.0});
    columns.push_back({.name = "b", .header_text = "B", .width = 80.0});
    columns.push_back({.name = "c", .header_text = "C", .width = 80.0});
    (*grid).set_columns(std::move(columns));
    for (std::size_t index = 0U; index < 200U; ++index) {
        const std::string row = "r" + std::to_string(index);
        DataGridViewRow added{};
        added.stable_id = DataGridViewRowId{"row" + std::to_string(index)};
        added.cells = {BindingValue{row + "c0"}, BindingValue{row + "c1"}, BindingValue{row + "c2"}};
        static_cast<void>((*grid).rows.append(std::move(added)));
    }
    (*grid).allow_user_to_add_rows = false;
    (*grid).cells_on_sub_surface = true;
    TextRaster raster(log);
    Window window(grid, Size{300.0, 200.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    std::shared_ptr<SubSurfaceHost> host{};
    const std::span<const Control::Ptr> children = (*grid).children();
    if (!children.empty()) host = std::dynamic_pointer_cast<SubSurfaceHost>(children[0]);
    require(static_cast<bool>(host), "the cell region is the grid's first child");
    require((*host).hit_test_transparent.read() && !(*host).focusable.read(),
            "the region leaves hit testing and focus to the grid");
    require((*host).started(), "the region starts once the grid is arranged in a window");
    // The grid publishes its cells; the region adds no "Sub-surface" pane
    // and none of its sub-surface's nodes to the grid's semantic tree.
    require(!(*host).publishes_semantics(), "the region publishes no semantics");
    const SemanticSnapshot semantics = window.semantic_snapshot();
    require(has_node_under(semantics.roots, "grid.s7"), "the grid publishes its node");
    require(!has_node_under(semantics.roots, "grid.s7.cells-region"), "the region publishes no node");
    require((*host).semantic_virtual_children().empty(), "the region publishes no virtual children");

    TextPainter first({});
    static_cast<void>(window.paint(first));
    require(contains(first.texts, "A") && !contains(first.texts, "r0c0"),
            "the UI thread paints the headers and leaves the cells to the region");
    const std::shared_ptr<LiveSurface> surface = (*host).live_surface();
    require(wait_for_content_tag(*surface, (*host).content_epoch()), "the region publishes the paint's epoch");
    const std::vector<std::string> shown = newest_frame(*log);
    require(contains(shown, "r0c0") && contains(shown, "r0c2") && contains(shown, "r1c0"),
            "the region shows the visible cells");

    (*grid).set_first_displayed_row(DataGridViewRowPosition{10U});
    window.flush();
    TextPainter scrolled({});
    static_cast<void>(window.paint(scrolled));
    require(!contains(scrolled.texts, "r10c0"), "a scrolled grid still leaves the cells to the region");
    require(wait_for_content_tag(*surface, (*host).content_epoch()), "the region publishes the scroll's epoch");
    const std::vector<std::string> after_scroll = newest_frame(*log);
    require(contains(after_scroll, "r10c0") && !contains(after_scroll, "r0c0"),
            "the region shows the rows the scroll brought in");

    // Owner drawing: the handler runs on the UI thread and asks for the
    // default content, which the UI thread paints.
    std::size_t asked = 0U;
    SubscriptionToken drawing = (*grid).draw_item_content().subscribe(AskDefaultContent{&asked});
    (*grid).draw_mode = ItemDrawMode::owner_draw;
    window.flush();
    TextPainter owner_drawn({});
    static_cast<void>(window.paint(owner_drawn));
    require(asked > 0U && contains(owner_drawn.texts, "r10c0"), "owner drawing paints the cells on the UI thread");
    (*grid).draw_mode = ItemDrawMode::normal;
    drawing.disconnect();

    (*grid).cells_on_sub_surface = false;
    window.flush();
    require(!(*host).started(), "turning the region off stops its thread");
    TextPainter plain({});
    static_cast<void>(window.paint(plain));
    require(contains(plain.texts, "r10c0"), "without the region the UI thread paints the cells");
}

// The UI thread's virtual-mode provider: counts what it is asked.
class CountingValues final : public DataGridViewCellValueProvider {
public:
    [[nodiscard]] BindingValue cell_value(const DataGridViewRowId& row, std::size_t column) const override {
        ++asked;
        return BindingValue{"ui " + row.value + ":" + std::to_string(column)};
    }
    bool push_cell_value(const DataGridViewRowId&, std::size_t, const BindingValue&) override { return false; }

    mutable std::size_t asked{};
};

// What the region's text provider saw: written on the region's thread, read
// by the test, under the mutex.
struct ProvidedLog final {
    std::mutex mutex;
    std::size_t calls{};
    std::size_t cells{};
    bool off_test_thread{true};
    bool released{};
};

// The region's provider: each cell's text is "p <row id>:<column>".
class RegionTexts final : public DataGridViewCellsTextProvider {
public:
    RegionTexts(std::shared_ptr<ProvidedLog> log, std::thread::id test_thread)
        : log_(std::move(log)), test_thread_(test_thread) {}
    ~RegionTexts() override {
        const std::lock_guard<std::mutex> lock((*log_).mutex);
        (*log_).released = true;
    }
    void fill_cell_texts(std::span<const DataGridViewCellsTextRequest> requests, std::string& text,
                         std::span<DataGridViewCellsTextRange> ranges) noexcept override {
        for (std::size_t index = 0U; index < requests.size(); ++index) {
            const std::size_t begin = text.size();
            text += "p ";
            text += requests[index].row_id;
            text += ':';
            text += std::to_string(requests[index].column);
            ranges[index] = DataGridViewCellsTextRange{begin, text.size()};
        }
        const std::lock_guard<std::mutex> lock((*log_).mutex);
        ++(*log_).calls;
        (*log_).cells += requests.size();
        if (std::this_thread::get_id() == test_thread_) (*log_).off_test_thread = false;
    }

private:
    std::shared_ptr<ProvidedLog> log_;
    std::thread::id test_thread_{};
};

// Decision 3(a), the virtual-mode half: with virtual rows and a cells text
// provider, the region's thread fills the text cells; the UI thread neither
// asks its value provider nor formats them; the provider goes with the
// region.
void region_provider_fills_virtual_cells() {
    std::shared_ptr<TextLog> log = std::make_shared<TextLog>();
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.virtual"));
    std::vector<DataGridViewColumn> columns;
    columns.push_back({.name = "a", .header_text = "A", .width = 80.0});
    columns.push_back({.name = "b", .header_text = "B", .width = 80.0});
    (*grid).set_columns(std::move(columns));
    std::shared_ptr<CountingValues> values = std::make_shared<CountingValues>();
    std::vector<DataGridViewRowId> order;
    for (std::size_t index = 0U; index < 100U; ++index) order.push_back(DataGridViewRowId{"v" + std::to_string(index)});
    (*grid).set_virtual_rows(values, std::move(order));
    std::shared_ptr<ProvidedLog> provided = std::make_shared<ProvidedLog>();
    (*grid).set_cells_text_provider(std::make_unique<RegionTexts>(provided, std::this_thread::get_id()));
    require((*grid).cells_text_provided(), "the grid holds the provider");
    (*grid).cells_on_sub_surface = true;
    TextRaster raster(log);
    Window window(grid, Size{300.0, 200.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    std::shared_ptr<SubSurfaceHost> host{};
    const std::span<const Control::Ptr> children = (*grid).children();
    if (!children.empty()) host = std::dynamic_pointer_cast<SubSurfaceHost>(children[0]);
    require(static_cast<bool>(host) && (*host).started(), "the region runs");

    const std::size_t asked_before = (*values).asked;
    TextPainter first({});
    static_cast<void>(window.paint(first));
    require((*values).asked == asked_before, "the paint asks the UI provider for no provided cell");
    const std::shared_ptr<LiveSurface> surface = (*host).live_surface();
    require(wait_for_content_tag(*surface, (*host).content_epoch()), "the region publishes the paint's epoch");
    const std::vector<std::string> shown = newest_frame(*log);
    require(contains(shown, "p v0:0") && contains(shown, "p v0:1") && contains(shown, "p v1:0"),
            "the region's provider fills the visible cells");
    require(!contains(shown, "ui v0:0"), "no UI-thread value reaches the region");
    {
        const std::lock_guard<std::mutex> lock((*provided).mutex);
        require((*provided).calls >= 1U && (*provided).cells >= 4U, "one call fills a snapshot's cells");
        require((*provided).off_test_thread, "the provider runs on the region's thread");
    }

    // The values the UI thread reads through the grid still come from its
    // own provider.
    require((*grid).cell_value(DataGridViewRowPosition{0U}, 0U) == BindingValue{std::string("ui v0:0")},
            "the grid's own reads use the UI provider");

    (*grid).cells_on_sub_surface = false;
    window.flush();
    require(!(*grid).cells_text_provided(), "the provider goes with the region");
    const std::lock_guard<std::mutex> lock((*provided).mutex);
    require((*provided).released, "the provider is released when the region stops");
}

int run_all() {
    snapshots_are_checked_whole();
    cells_paint_resolved_parts();
    region_shows_each_epochs_snapshot();
    grid_sends_its_cells_to_its_region();
    region_provider_fills_virtual_cells();
    return 0;
}

} // namespace

int main() {
    try {
        const int result = run_all();
        std::puts("data grid view cells tests passed");
        return result;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "data grid view cells tests failed: %s\n", error.what());
        return 1;
    }
}
