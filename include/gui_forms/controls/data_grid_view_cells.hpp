#pragma once

// A DataGridView's cell region on a threaded sub-surface (stage S7, threaded
// sub-surfaces design note section 2, "split ownership, worked"; coordinator
// ruling 2026-11-05, option 1). The grid on the UI thread keeps its data, its
// events, its editor, its headers and its scroll bars; for each visible
// frame it resolves the visible row window into a DataGridViewCellsSnapshot
// (values already formatted, styles already inherited, every part already
// placed) and sends it with SubSurfaceHost::redraw_all, so the cells drawn at
// a content epoch agree with the headers drawn for it. The sub-surface's
// DataGridViewCells control only paints the snapshot it was given.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

class Painter;

// A byte range of DataGridViewCellsSnapshot::text: [begin_byte, end_byte).
struct GUI_FORMS_API DataGridViewCellsTextRange final {
    std::size_t begin_byte{};
    std::size_t end_byte{};

    friend bool operator==(const DataGridViewCellsTextRange& left,
                           const DataGridViewCellsTextRange& right) noexcept {
        return left.begin_byte == right.begin_byte && left.end_byte == right.end_byte;
    }
};

// A range of DataGridViewCellsSnapshot::lines: `count` lines from `first`.
struct GUI_FORMS_API DataGridViewCellsLineRange final {
    std::size_t first{};
    std::size_t count{};
};

// The parts of one cell, placed in the region's coordinates; a part the
// cell's kind does not show is absent (PartGeometry::present). They are the
// grid's "cell" item parts, resolved on the UI thread with the application's
// authored colours (PartGeometry::authored_background and
// authored_foreground).
struct GUI_FORMS_API DataGridViewCellsParts final {
    PartGeometry cell{};
    PartGeometry selection{};
    PartGeometry content{};
    PartGeometry image{};
    // A link cell's text extent and the one-unit rule under its baseline.
    PartGeometry link{};
    PartGeometry link_underline{};
    PartGeometry check_well{};
    PartGeometry check_indicator{};
    PartGeometry button_face{};
    PartGeometry button_label{};
    PartGeometry drop_down{};
    PartGeometry drop_down_glyph{};
    PartGeometry vertical_rule{};
    PartGeometry horizontal_rule{};
    PartGeometry focus{};
    // The error glyph of a cell with error text.
    PartGeometry error{};
};

// A cell whose text the region's own provider fills on the sub-surface's
// thread (DataGridViewCellsTextProvider; decision 3(a), the virtual-mode
// half): its row, an index of DataGridViewCellsSnapshot::provided_rows, and
// its column. A provided cell arrives with an empty text range and no lines.
struct GUI_FORMS_API DataGridViewCellsProvided final {
    bool provided{};
    std::size_t row{};
    std::size_t column{};
};

// One visible cell, resolved. Text and lines are ranges of the snapshot's
// shared buffers, so a snapshot of any size makes three allocations, not
// one per cell.
struct GUI_FORMS_API DataGridViewCellsCell final {
    DataGridViewCellsParts parts{};
    // The cell's active states (its anatomy's standard and custom states),
    // as the theme reads them; checked draws a check box's indicator.
    AnatomyStateSet states{};
    // A link cell whose link is visited: drawn in the visited_link family.
    bool visited{};
    // The grid's text editor covers this cell: its text content is not
    // drawn (the editor, on the UI thread, shows it).
    bool editing_text{};
    // The effective font (Control::effective_font) and the text alignment
    // of the cell's style.
    FontSpec font{};
    PartTextAlignment alignment{PartTextAlignment::start};
    // The formatted value as the cell shows it.
    DataGridViewCellsTextRange text{};
    // Wrapped text: the lines, each a byte range of `text` itself, the box
    // they take, their line height, and the content box they are cut to.
    // No lines: the text is not wrapped.
    DataGridViewCellsLineRange lines{};
    Rect lines_box{};
    double line_height{};
    Rect content_box{};
    // An image cell's image, drawn whole at parts.image and cut to
    // `image_clip` (the cell's inside).
    ImageId image{};
    Rect image_clip{};
    // Set for a cell whose text the region's provider fills.
    DataGridViewCellsProvided provided{};
};

// The grid's look, which the sub-surface's control takes over so that the
// theme selects and colours its parts as it does the grid's: the grid's
// style classes and inline style select rules (its stable ID is the
// control's own, DataGridViewCellsContent); its effective back and fore
// colours (Control::ambient_back_color, ambient_fore_color) stand for its
// authored ones.
struct GUI_FORMS_API DataGridViewCellsLook final {
    std::vector<std::string> classes{};
    std::string inline_style{};
    std::optional<Color> back_color{};
    std::optional<Color> fore_color{};

    friend bool operator==(const DataGridViewCellsLook& left, const DataGridViewCellsLook& right) {
        return left.classes == right.classes &&
               left.inline_style == right.inline_style && left.back_color == right.back_color &&
               left.fore_color == right.fore_color;
    }
};

// What the cell region shows in one content epoch: complete, so a newer
// snapshot replaces an older one whole. Coordinates are the region's (the
// grid's data area at the origin). Made and filled on the UI thread, then
// moved to the sub-surface's thread inside a DataGridViewCellsUpdate.
struct GUI_FORMS_API DataGridViewCellsSnapshot final {
    DataGridViewCellsLook look{};
    // The grid's well, placed so that only its ground lies in the region
    // (its border is the grid's chrome), with the grid's authored
    // background. With an authored background, the cells' own extent keeps
    // the theme's ground (`cells_extent`; empty for none), as Windows Forms'
    // cells keep their back colour over BackgroundColor.
    PartGeometry well{};
    Rect cells_extent{};
    // The visible cells, in paint order (rows, then display columns).
    std::vector<DataGridViewCellsCell> cells{};
    // The cells' formatted text and wrapped lines, indexed by the cells.
    std::string text{};
    std::vector<DataGridViewCellsTextRange> lines{};
    // The row identities (DataGridViewRowId values) the provided cells name,
    // one per visible row that has any; empty without a provider.
    std::vector<std::string> provided_rows{};
};

// Whether every range of `snapshot` lies in its buffers: each cell's text
// in `text`, each cell's lines in `lines`, each line in its cell's text,
// and each provided cell's row in `provided_rows`, with no lines. The sub-surface checks a snapshot whole
// before it takes it (house style 8: validate whole buffers up front).
[[nodiscard]] GUI_FORMS_API bool data_grid_view_cells_snapshot_valid(const DataGridViewCellsSnapshot& snapshot) noexcept;

// Paints one resolved cell of `snapshot`, its parts in the grid's order,
// cut to `data` (the region's data area), for `owner` (whose theme and
// look select the parts). It is the cell painter both sides share: the
// sub-surface's control and, once wired, the grid itself. The cell must be
// one `data_grid_view_cells_snapshot_valid` accepted.
GUI_FORMS_API void paint_data_grid_view_cell(Painter& painter, const Control& owner, const DataGridViewCellsSnapshot& snapshot,
                               const DataGridViewCellsCell& cell, Rect data);
// The same cell painted in two steps, for the grid's owner drawing, which
// draws between them: the selection plate, then the other parts. Without
// `default_content` the content parts (link, text, image, check box,
// button) are the owner's and are left out; the drop-down, the error
// glyph, the rules and the focus part are drawn either way.
GUI_FORMS_API void paint_data_grid_view_cell_selection(Painter& painter, const Control& owner, const DataGridViewCellsCell& cell);
GUI_FORMS_API void paint_data_grid_view_cell_parts(Painter& painter, const Control& owner, const DataGridViewCellsSnapshot& snapshot,
                                     const DataGridViewCellsCell& cell, Rect data, bool default_content);

// The sub-surface's root control, private to the library
// (src/controls/data_grid_view/data_grid_view_cells_control.hpp): it paints
// the newest snapshot DataGridViewCellsContent hands it.
class DataGridViewCells;

// One cell a DataGridViewCellsTextProvider is asked for: its row's
// identity, borrowed from the snapshot (valid through the call), and its
// column.
struct GUI_FORMS_API DataGridViewCellsTextRequest final {
    std::string_view row_id{};
    std::size_t column{};
};

// The sub-surface half of virtual mode on a cell region (threaded
// sub-surfaces decision 3(a): a model the sub-surface's thread owns): the
// application's model of the rows' text, moved to the region's thread with
// its content (DataGridView::set_cells_text_provider) and called only
// there, once per snapshot that has provided cells. The UI thread then
// neither asks DataGridViewCellValueProvider::cell_value nor raises
// cell_formatting for those cells; it still does for the cells semantics,
// the editor and the application read through the grid. The provider is
// released on the region's thread when its sub-surface stops.
class GUI_FORMS_API DataGridViewCellsTextProvider {
public:
    virtual ~DataGridViewCellsTextProvider();
    // Appends each requested cell's shown text to `text`, in request order,
    // and stores its byte range of `text` in the same slot of `ranges`
    // (as many as `requests`; each starts empty). It may not throw. A range
    // outside `text` makes the region refuse the snapshot (the previous one
    // stays).
    virtual void fill_cell_texts(std::span<const DataGridViewCellsTextRequest> requests, std::string& text,
                                 std::span<DataGridViewCellsTextRange> ranges) noexcept = 0;
};

// One snapshot on its way to the sub-surface (SubSurfaceHost::redraw_all).
class GUI_FORMS_API DataGridViewCellsUpdate final : public SubSurfaceContentUpdate {
public:
    explicit DataGridViewCellsUpdate(DataGridViewCellsSnapshot snapshot) : snapshot(std::move(snapshot)) {}
    DataGridViewCellsSnapshot snapshot;
};

// The cell region's content: builds a DataGridViewCells, under the grid's
// stable ID, on the sub-surface's thread and hands it each
// DataGridViewCellsUpdate, after its provider (when it has one) filled the
// provided cells' text. Other updates are ignored. A look whose classes or
// inline style the control refuses throws, and the sub-surface fails.
class GUI_FORMS_API DataGridViewCellsContent final : public SubSurfaceContent {
public:
    explicit DataGridViewCellsContent(StableId stable_id) : stable_id_(std::move(stable_id)) {}
    // With a text provider, owned from here on and moved with the content
    // to the sub-surface's thread; null for none (provided cells then show
    // no text).
    DataGridViewCellsContent(StableId stable_id, std::unique_ptr<DataGridViewCellsTextProvider> provider)
        : stable_id_(std::move(stable_id)), provider_(std::move(provider)) {}
    [[nodiscard]] Control::Ptr build_root() override;
    void apply_update(SubSurfaceContentUpdate& update) override;

private:
    // Asks the provider for the provided cells' text, in paint order, and
    // stores the ranges in the cells. Sub-surface thread.
    void fill_provided(DataGridViewCellsSnapshot& snapshot);

    StableId stable_id_;
    std::unique_ptr<DataGridViewCellsTextProvider> provider_{};
    // Reused by every fill_provided: the requests and the ranges answered,
    // and each request's cell index in the snapshot.
    std::vector<DataGridViewCellsTextRequest> requests_{};
    std::vector<DataGridViewCellsTextRange> ranges_{};
    std::vector<std::size_t> request_cells_{};
    // Made by build_root; released with the content on the sub-surface's
    // thread, after the sub-surface's tree.
    std::shared_ptr<DataGridViewCells> cells_{};
};

} // namespace gui_forms
