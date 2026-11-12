#include "responsive_tracks_lab.hpp"

#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/responsive_track_panel.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] bool near(double left, double right,
                        double epsilon = 0.000001) {
    return std::abs(left - right) <= epsilon;
}

template <typename ControlType>
std::shared_ptr<ControlType> find_as(Window& window, std::string_view id) {
    return std::dynamic_pointer_cast<ControlType>(window.find(id));
}

void resize_surface(Window& window, Size size) {
    window.resize({size.width,
                   size.height +
                       gui_forms::responsive_tracks_lab::controller_height});
    window.perform_layout();
}

void test_reference_geometry_and_house_relationships() {
    std::unique_ptr<Window> window =
        gui_forms::responsive_tracks_lab::make_responsive_tracks_lab();
    const std::shared_ptr<ResponsiveTrackPanel> rows =
        find_as<ResponsiveTrackPanel>(*window, "responsive.surface");
    const std::shared_ptr<ResponsiveTrackPanel> columns =
        find_as<ResponsiveTrackPanel>(*window, "responsive.workspace");
    require(rows && columns, "responsive lab must expose both nested track solvers");
    const ResponsiveLayoutSnapshot vertical = (*rows).layout_snapshot();
    const ResponsiveLayoutSnapshot horizontal = (*columns).layout_snapshot();
    require(vertical.resolution.tracks.size() == 6U &&
                near(vertical.resolution.tracks[0].allocated, 40.0) &&
                near(vertical.resolution.tracks[1].allocated, 23.0) &&
                near(vertical.resolution.tracks[2].allocated, 66.0) &&
                near(vertical.resolution.tracks[3].allocated, 40.0) &&
                near(vertical.resolution.tracks[4].allocated, 657.0) &&
                near(vertical.resolution.tracks[5].allocated, 24.0),
            "1450x850 surface must preserve 40/23/66/40/remaining/24 band geometry");
    require(horizontal.resolution.tracks.size() == 3U &&
                near(horizontal.resolution.tracks[0].allocated, 221.0) &&
                near(horizontal.resolution.tracks[1].allocated, 938.0) &&
                near(horizontal.resolution.tracks[2].allocated, 291.0),
            "grouped pane+seam tracks must preserve 218+3/fluid/3+288 reference geometry");
}

void test_size_matrix_collapse_thresholds_and_bounded_extreme() {
    std::unique_ptr<Window> window =
        gui_forms::responsive_tracks_lab::make_responsive_tracks_lab();
    const std::shared_ptr<ResponsiveTrackPanel> columns =
        find_as<ResponsiveTrackPanel>(*window, "responsive.workspace");
    const std::shared_ptr<Panel> tree =
        find_as<Panel>(*window, "responsive.pane.tree");
    const std::shared_ptr<Panel> selection =
        find_as<Panel>(*window, "responsive.pane.selection");
    const std::shared_ptr<Button> primary =
        find_as<Button>(*window, "responsive.content.primary");

    const std::vector<Size> ordinary{
        {1450.0, 850.0}, {1200.0, 760.0}, {960.0, 680.0},
        {720.0, 520.0}, {480.0, 360.0},
    };
    for (const Size size : ordinary) {
        resize_surface(*window, size);
        const ResponsiveLayoutSnapshot snapshot = (*columns).layout_snapshot();
        require(snapshot.resolution.collapse_order.empty() &&
                    !(*tree).layout_collapsed() &&
                    !(*selection).layout_collapsed(),
                "normal through severe widths above the content minimum must preserve both side panes");
    }

    resize_surface(*window, {300.0, 240.0});
    ResponsiveLayoutSnapshot snapshot = (*columns).layout_snapshot();
    require(snapshot.resolution.collapse_order ==
                std::vector<std::size_t>{2U} &&
                near(snapshot.resolution.tracks[2].collapse_threshold, 436.0) &&
                !(*tree).layout_collapsed() &&
                (*selection).layout_collapsed(),
            "300-wide surface must collapse selection first at the inspectable 436 threshold");

    resize_surface(*window, {240.0, 200.0});
    snapshot = (*columns).layout_snapshot();
    require(snapshot.resolution.collapse_order ==
                std::vector<std::size_t>({2U, 0U}) &&
                near(snapshot.resolution.tracks[0].collapse_threshold, 273.0) &&
                (*tree).layout_collapsed() &&
                (*selection).layout_collapsed(),
            "below 273 logical pixels the tree must follow selection in authored order");

    resize_surface(*window, {150.0, 150.0});
    snapshot = (*columns).layout_snapshot();
    require(!snapshot.resolution.overflow &&
                near(snapshot.resolution.tracks[1].allocated, 150.0) &&
                (*primary).effectively_visible() &&
                !(*primary).arranged_bounds().empty() &&
                (*primary).arranged_bounds().right() <= 150.0 + 1e-9 &&
                (*window).metrics_snapshot().bounded_pass_limit_hits == 0U,
            "150x150 surface must remain bounded around one usable current-location/content region");
    const std::string semantic = (*window).semantic_snapshot().to_json();
    require(semantic.find("responsive.content.primary") != std::string::npos &&
                semantic.find("responsive.selection.action") == std::string::npos,
            "extreme collapse must preserve the primary path and remove collapsed panes from task semantics");
}

void test_native_preset_preview_keeps_controller_window_stable() {
    std::unique_ptr<Window> window =
        gui_forms::responsive_tracks_lab::make_responsive_tracks_lab();
    const std::shared_ptr<Button> preset =
        find_as<Button>(*window, "responsive.preset.300");
    const std::shared_ptr<ResponsiveTrackPanel> surface =
        find_as<ResponsiveTrackPanel>(*window, "responsive.surface");
    require(preset && surface && (*preset).perform_click(),
            "the native width/height preset must remain invokable");
    (*window).perform_layout();
    require(near((*surface).arranged_bounds().width, 300.0) &&
                near((*surface).arranged_bounds().height, 240.0) &&
                near((*window).client_size().width, 1450.0) &&
                near((*window).client_size().height,
                     850.0 + gui_forms::responsive_tracks_lab::controller_height),
            "native presets must preview an exact product surface without losing the stable controller strip");
}

void test_text_scale_matrix_reflows_before_row_collapse() {
    std::unique_ptr<Window> window =
        gui_forms::responsive_tracks_lab::make_responsive_tracks_lab();
    const std::shared_ptr<ResponsiveTrackPanel> rows =
        find_as<ResponsiveTrackPanel>(*window, "responsive.surface");
    resize_surface(*window, {960.0, 680.0});
    std::vector<double> title_allocations;
    for (const double scale : {1.0, 1.25, 1.5, 2.0}) {
        (*window).set_text_scale(scale);
        (*window).perform_layout();
        const ResponsiveLayoutSnapshot snapshot = (*rows).layout_snapshot();
        require(snapshot.resolution.tracks.size() == 6U,
                "text-scale surface must keep a complete row diagnostic");
        for (const ResponsiveTrackResult& track : snapshot.resolution.tracks) {
            if (!track.collapsed()) {
                require(track.allocated + 1e-9 >= track.minimum,
                        "visible text-scale tracks must not allocate below their measured/content-aware minimum");
            }
        }
        title_allocations.push_back(snapshot.resolution.tracks[0].allocated);
    }
    require(std::is_sorted(title_allocations.begin(), title_allocations.end()),
            "100/125/150/200 text scale must never shrink the title line box");

    resize_surface(*window, {300.0, 150.0});
    const ResponsiveLayoutSnapshot extreme = (*rows).layout_snapshot();
    require(extreme.resolution.collapse_order.size() >= 2U &&
                extreme.resolution.collapse_order[0] == 2U &&
                extreme.resolution.collapse_order[1] == 5U &&
                (*find_as<Button>(*window, "responsive.content.primary"))
                    .effectively_visible(),
            "large text at short height must grow/reflow first, then collapse shelf and status before the primary field");
}

void test_resize_cycles_inspection_and_revision_stability() {
    std::unique_ptr<Window> window =
        gui_forms::responsive_tracks_lab::make_responsive_tracks_lab();
    const std::shared_ptr<ResponsiveTrackPanel> columns =
        find_as<ResponsiveTrackPanel>(*window, "responsive.workspace");
    for (std::size_t cycle = 0U; cycle < 16U; ++cycle) {
        resize_surface(*window, cycle % 2U == 0U
            ? Size{300.0, 240.0} : Size{1450.0, 850.0});
    }
    const std::uint64_t revision =
        (*columns).layout_snapshot().committed_resolution_revision;
    (*window).perform_layout();
    const ResponsiveLayoutSnapshot settled = (*columns).layout_snapshot();
    const std::string inspection =
        (*window).visual_inspection_snapshot().to_json();
    require(settled.committed_resolution_revision == revision &&
                settled.transaction.requested_revision ==
                    settled.transaction.committed_revision &&
                !settled.transaction.deferred &&
                inspection.find("\"layout_collapsed\":false") !=
                    std::string::npos &&
                inspection.find("responsive.layout.diagnostics") !=
                    std::string::npos &&
                (*window).metrics_snapshot().bounded_pass_limit_hits == 0U,
            "resize cycles must settle with committed revisions and public inspector-visible layout state");
}

} // namespace

int main() {
    test_reference_geometry_and_house_relationships();
    test_size_matrix_collapse_thresholds_and_bounded_extreme();
    test_native_preset_preview_keeps_controller_window_stable();
    test_text_scale_matrix_reflows_before_row_collapse();
    test_resize_cycles_inspection_and_revision_stability();
    std::cout << "responsive-tracks-lab-tests: pass\n";
    return EXIT_SUCCESS;
}
