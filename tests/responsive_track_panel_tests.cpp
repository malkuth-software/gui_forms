#include "gui_forms/gui_forms.hpp"

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

void test_fixed_content_remaining_gap_minimum_and_maximum() {
    const std::vector<ResponsiveTrackSpec> specs{
        {ResponsiveTrackSizeMode::fixed, 50.0, 50.0, 50.0, 1.0},
        {ResponsiveTrackSizeMode::content, 20.0, 30.0, 80.0, 1.0},
        {ResponsiveTrackSizeMode::remaining, 40.0, 40.0, 180.0, 1.0},
    };
    const std::vector<double> content{90.0, 60.0, 30.0};
    const ResponsiveTrackResolution result = resolve_responsive_tracks(
        specs, content, 300.0, 5.0, 1.0);
    require(result.tracks.size() == 3U &&
                near(result.tracks[0].allocated, 50.0) &&
                near(result.tracks[1].minimum, 60.0) &&
                near(result.tracks[1].allocated, 60.0) &&
                near(result.tracks[2].allocated, 180.0) &&
                near(result.allocated_extent, 300.0) && !result.overflow,
            "fixed/content/remaining tracks must honor content minimums, gaps, and max caps");

    const std::vector<ResponsiveTrackSpec> weighted{
        {ResponsiveTrackSizeMode::fixed, 40.0, 40.0, 40.0, 1.0},
        {ResponsiveTrackSizeMode::remaining, 20.0, 20.0, 0.0, 1.0},
        {ResponsiveTrackSizeMode::remaining, 20.0, 20.0, 0.0, 2.0},
    };
    const ResponsiveTrackResolution weights = resolve_responsive_tracks(
        weighted, std::vector<double>(3U), 310.0, 5.0, 1.0);
    require(near(weights.tracks[0].allocated, 40.0) &&
                near(weights.tracks[1].allocated, 93.333333) &&
                near(weights.tracks[2].allocated, 166.666667),
            "multiple remaining tracks must distribute only free extent by authored weight");
}

void test_content_shrink_precedes_authored_collapse() {
    const std::vector<ResponsiveTrackSpec> specs{
        {ResponsiveTrackSizeMode::fixed, 40.0, 40.0, 40.0, 1.0},
        {ResponsiveTrackSizeMode::remaining, 20.0, 100.0, 0.0, 1.0},
        {ResponsiveTrackSizeMode::content, 30.0, 60.0, 100.0, 1.0,
         false, 10U},
    };
    const std::vector<double> measured{0.0, 0.0, 60.0};
    const ResponsiveTrackResolution shrink = resolve_responsive_tracks(
        specs, measured, 170.0, 5.0, 1.0);
    require(shrink.collapse_order.empty() &&
                near(shrink.tracks[1].allocated, 60.0) &&
                near(shrink.tracks[2].allocated, 60.0),
            "remaining whitespace must shrink to its minimum before a content track collapses");

    const ResponsiveTrackResolution collapse = resolve_responsive_tracks(
        specs, measured, 125.0, 5.0, 1.0);
    require(collapse.collapse_order == std::vector<std::size_t>{2U} &&
                collapse.tracks[2].collapse_reason ==
                    ResponsiveTrackCollapseReason::insufficient_extent &&
                near(collapse.tracks[2].collapse_threshold, 130.0),
            "content-aware minimum overflow must use the authored priority and expose its threshold");
}

void test_hidden_tracks_and_deterministic_priority_order() {
    const std::vector<ResponsiveTrackSpec> specs{
        {ResponsiveTrackSizeMode::fixed, 123.0, 221.0, 320.0, 1.0,
         false, 20U},
        {ResponsiveTrackSizeMode::remaining, 150.0, 300.0, 0.0, 1.0},
        {ResponsiveTrackSizeMode::fixed, 163.0, 291.0, 360.0, 1.0,
         false, 10U},
        {ResponsiveTrackSizeMode::fixed, 30.0, 30.0, 30.0, 1.0, true,
         5U},
    };
    const std::vector<double> measured(4U);
    const ResponsiveTrackResolution reference = resolve_responsive_tracks(
        specs, measured, 436.0, 0.0, 1.0);
    require(reference.collapse_order.empty() &&
                reference.tracks[3].collapse_reason ==
                    ResponsiveTrackCollapseReason::authored_hidden,
            "authored hidden tracks must remain distinct from automatic collapse");
    const ResponsiveTrackResolution narrow = resolve_responsive_tracks(
        specs, measured, 272.0, 0.0, 1.0);
    require(narrow.collapse_order ==
                std::vector<std::size_t>({2U, 0U}) &&
                near(narrow.tracks[2].collapse_threshold, 436.0) &&
                near(narrow.tracks[0].collapse_threshold, 273.0) &&
                near(narrow.tracks[1].allocated, 272.0),
            "authored priority order and successive exact collapse thresholds must be deterministic");
}

void test_cumulative_device_boundary_rounding() {
    const std::vector<ResponsiveTrackSpec> specs{
        {ResponsiveTrackSizeMode::fixed, 33.2, 33.2, 33.2, 1.0},
        {ResponsiveTrackSizeMode::fixed, 33.3, 33.3, 33.3, 1.0},
        {ResponsiveTrackSizeMode::remaining, 10.0, 10.0, 0.0, 1.0},
    };
    const std::vector<double> measured(3U);
    const ResponsiveTrackResolution one = resolve_responsive_tracks(
        specs, measured, 100.0, 0.5, 1.0);
    const ResponsiveTrackResolution two = resolve_responsive_tracks(
        specs, measured, 100.0, 0.5, 2.0);
    require(near(one.tracks[0].logical_end, two.tracks[0].logical_end) &&
                near(one.tracks[1].logical_end, two.tracks[1].logical_end) &&
                near(one.tracks[2].logical_end, 100.0) &&
                near(two.tracks[2].logical_end, 100.0) &&
                one.tracks[2].device_end == 100 &&
                two.tracks[2].device_end == 200,
            "device scale must change only cumulative snapped boundaries and never logical allocation");
    for (std::size_t index = 1U; index < specs.size(); ++index) {
        require(one.tracks[index].device_start >=
                    one.tracks[index - 1U].device_end &&
                    two.tracks[index].device_start >=
                    two.tracks[index - 1U].device_end,
                "snapping each cumulative boundary must not accumulate overlap drift");
    }
}

struct FocusFixture final {
    std::shared_ptr<ResponsiveTrackPanel> root;
    std::shared_ptr<Panel> content;
    std::shared_ptr<Panel> selection;
    std::shared_ptr<Button> fallback;
    std::shared_ptr<Button> selection_action;
    Window window;

    FocusFixture()
        : root(make_control<ResponsiveTrackPanel>(StableId("responsive.focus.root"))),
          content(make_control<Panel>(StableId("responsive.focus.content"))),
          selection(make_control<Panel>(StableId("responsive.focus.selection"))),
          fallback(make_control<Button>(StableId("responsive.focus.fallback"),
                                        "Content")),
          selection_action(make_control<Button>(
              StableId("responsive.focus.selection.action"), "Selection")),
          window(root, {240.0, 100.0}) {
        (*root).set_track_specs({
            {ResponsiveTrackSizeMode::remaining, 80.0, 120.0, 0.0, 1.0},
            {ResponsiveTrackSizeMode::fixed, 120.0, 120.0, 120.0, 1.0,
             false, 10U},
        });
        (*content).dock = DockStyle::fill;
        (*selection).dock = DockStyle::fill;
        (*fallback).set_requested_bounds({4.0, 4.0, 70.0, 24.0});
        (*selection_action).set_requested_bounds({4.0, 4.0, 80.0, 24.0});
        (*content).add_child(fallback);
        (*selection).add_child(selection_action);
        (*root).add_child(content);
        (*root).add_child(selection);
        (*root).set_child_track(*content, 0U);
        (*root).set_child_track(*selection, 1U);
        (*root).set_track_focus_fallback(1U, *fallback);
        window.perform_layout();
    }
};

void test_focus_preservation_fallback_and_explicit_reveal() {
    FocusFixture fixture;
    require(fixture.window.request_focus(fixture.selection_action),
            "focus specimen must focus the soon-to-collapse track");
    fixture.window.resize({100.0, 100.0});
    fixture.window.perform_layout();
    const ResponsiveLayoutSnapshot collapsed = (*fixture.root).layout_snapshot();
    require((*fixture.selection).visible.type() &&
                (*fixture.selection).layout_collapsed() &&
                !(*fixture.selection).effectively_visible() &&
                fixture.window.focused_control() == fixture.fallback &&
                collapsed.resolution.collapse_order ==
                    std::vector<std::size_t>{1U},
            "a protected focused track must move focus only to its declared fallback before effective collapse");
    const std::string semantics = fixture.window.semantic_snapshot().to_json();
    require(semantics.find("responsive.focus.selection.action") ==
                std::string::npos,
            "layout-collapsed descendants must leave the semantic and focus surface without changing authored visibility");
    const VisualInspectionSnapshot inspection =
        fixture.window.visual_inspection_snapshot();
    bool found_collapsed = false;
    for (const VisualControlInspection& control : inspection.controls) {
        if (control.stable_id == "responsive.focus.selection") {
            found_collapsed = control.state.visible &&
                control.state.layout_collapsed &&
                !control.state.effectively_visible;
        }
    }
    require(found_collapsed,
            "visual inspection must distinguish authored visibility from layout collapse");

    (*fixture.root).set_track_revealed(1U, true);
    fixture.window.perform_layout();
    const ResponsiveLayoutSnapshot revealed = (*fixture.root).layout_snapshot();
    require(!(*fixture.selection).layout_collapsed() &&
                (*fixture.selection).effectively_visible() &&
                revealed.resolution.overflow &&
                revealed.resolution.tracks[1].collapse_protected &&
                fixture.window.request_focus(fixture.selection_action),
            "explicit reveal must restore eligibility and report honest overflow when the protected track cannot fit");
}

void test_text_scale_reflow_transactions_and_resize_stability() {
    std::shared_ptr<ResponsiveTrackPanel> root =
        make_control<ResponsiveTrackPanel>(StableId("responsive.scale.root"));
    (*root).set_orientation(ResponsiveTrackOrientation::vertical);
    (*root).set_track_specs({
        {ResponsiveTrackSizeMode::content, 28.0, 40.0, 120.0, 1.0},
        {ResponsiveTrackSizeMode::remaining, 30.0, 100.0, 0.0, 1.0},
        {ResponsiveTrackSizeMode::content, 20.0, 24.0, 80.0, 1.0,
         false, 10U},
    });
    std::shared_ptr<Label> title = make_control<Label>(
        StableId("responsive.scale.title"), "Current location");
    (*title).set_font({FontRole::control, 15.0, 700, false});
    (*title).dock = DockStyle::fill;
    std::shared_ptr<Panel> content =
        make_control<Panel>(StableId("responsive.scale.content"));
    (*content).dock = DockStyle::fill;
    std::shared_ptr<Label> status = make_control<Label>(
        StableId("responsive.scale.status"), "Local authority remains available");
    (*status).set_font({FontRole::content, 10.0, 400, false});
    (*status).dock = DockStyle::fill;
    (*root).add_child(title);
    (*root).add_child(content);
    (*root).add_child(status);
    (*root).set_child_track(*title, 0U);
    (*root).set_child_track(*content, 1U);
    (*root).set_child_track(*status, 2U);
    Window window(root, {480.0, 300.0});

    double previous_title = 0.0;
    for (const double scale : {1.0, 1.25, 1.5, 2.0}) {
        window.set_text_scale(scale);
        window.perform_layout();
        const ResponsiveLayoutSnapshot snapshot = (*root).layout_snapshot();
        require(snapshot.resolution.tracks[0].allocated + 1e-9 >=
                    snapshot.resolution.tracks[0].measured_content &&
                    snapshot.resolution.tracks[0].allocated + 1e-9 >=
                    previous_title &&
                    !snapshot.resolution.tracks[0].collapsed(),
                "text scales 100/125/150/200 must grow content tracks before authored collapse and avoid line-box clipping");
        previous_title = snapshot.resolution.tracks[0].allocated;
    }

    const Rect committed_before = (*title).arranged_bounds();
    (*root).suspend_layout();
    (*root).set_track_gap(3.0);
    const LayoutTransactionState pending = (*root).layout_transaction_state();
    require(pending.suspend_depth == 1U && pending.deferred &&
                pending.requested_revision > pending.committed_revision &&
                (*title).committed_arranged_bounds() == committed_before,
            "suspended responsive mutations must expose pending revision while preserving committed geometry");
    (*root).resume_layout(true);
    const LayoutTransactionState committed = (*root).layout_transaction_state();
    require(!committed.deferred && committed.requested_revision ==
                committed.committed_revision,
            "responsive layout transaction must commit exactly after resume");

    for (std::size_t cycle = 0U; cycle < 20U; ++cycle) {
        window.resize(cycle % 2U == 0U ? Size{300.0, 180.0}
                                      : Size{480.0, 300.0});
        window.perform_layout();
    }
    const ResponsiveLayoutSnapshot stable = (*root).layout_snapshot();
    const std::uint64_t stable_revision = stable.committed_resolution_revision;
    window.perform_layout();
    require((*root).layout_snapshot().committed_resolution_revision ==
                stable_revision &&
                window.metrics_snapshot().bounded_pass_limit_hits == 0U,
            "resize cycles must settle without oscillation, reentrant runaway, or phantom committed revisions");
}

void test_invalid_specs_reject_atomically() {
    std::shared_ptr<ResponsiveTrackPanel> panel =
        make_control<ResponsiveTrackPanel>(StableId("responsive.invalid"));
    const std::vector<ResponsiveTrackSpec> accepted{
        {ResponsiveTrackSizeMode::fixed, 20.0, 20.0, 20.0, 1.0},
        {ResponsiveTrackSizeMode::remaining, 10.0, 20.0, 0.0, 1.0,
         false, 10U},
    };
    (*panel).set_track_specs(accepted);
    bool bounds_rejected = false;
    bool duplicate_priority_rejected = false;
    bool measurement_rejected = false;
    try {
        (*panel).set_track_spec(0U,
            {ResponsiveTrackSizeMode::fixed, 30.0, 20.0, 40.0, 1.0});
    } catch (const std::invalid_argument&) {
        bounds_rejected = true;
    }
    try {
        (*panel).set_track_specs({
            {ResponsiveTrackSizeMode::fixed, 10.0, 10.0, 10.0, 1.0,
             false, 7U},
            {ResponsiveTrackSizeMode::fixed, 10.0, 10.0, 10.0, 1.0,
             false, 7U},
        });
    } catch (const std::invalid_argument&) {
        duplicate_priority_rejected = true;
    }
    try {
        static_cast<void>(resolve_responsive_tracks(
            accepted, std::vector<double>{0.0}, 100.0, 0.0, 1.0));
    } catch (const std::invalid_argument&) {
        measurement_rejected = true;
    }
    require(bounds_rejected && duplicate_priority_rejected &&
                measurement_rejected &&
                std::vector<ResponsiveTrackSpec>((*panel).track_specs().begin(),
                                                  (*panel).track_specs().end()) ==
                    accepted,
            "invalid bounds, priorities, and corpus dimensions must reject before mutating retained specs");
}

} // namespace

int main() {
    test_fixed_content_remaining_gap_minimum_and_maximum();
    test_content_shrink_precedes_authored_collapse();
    test_hidden_tracks_and_deterministic_priority_order();
    test_cumulative_device_boundary_rounding();
    test_focus_preservation_fallback_and_explicit_reveal();
    test_text_scale_reflow_transactions_and_resize_stability();
    test_invalid_specs_reject_atomically();
    std::cout << "responsive-track-panel-tests: pass\n";
    return EXIT_SUCCESS;
}
