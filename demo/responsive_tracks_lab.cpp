#include "responsive_tracks_lab.hpp"

#include "gui_forms/gui_forms.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::responsive_tracks_lab {
namespace {

constexpr Color graphite = Color::rgba(70, 80, 86);
constexpr Color graphite_dark = Color::rgba(43, 53, 60);
constexpr Color pearl = Color::rgba(231, 238, 242);
constexpr Color paper = Color::rgba(251, 252, 250);
constexpr Color sapphire = Color::rgba(50, 107, 145);
constexpr Color coral = Color::rgba(205, 105, 83);

std::shared_ptr<Panel> band(std::string id, std::string text,
                            Color background, Color foreground,
                            FontSpec font, TextWrapping wrapping) {
    const std::string label_id = id + ".text";
    std::shared_ptr<Panel> value = make_control<Panel>(StableId(std::move(id)));
    (*value).set_background(background);
    (*value).auto_size = true;
    (*value).auto_size_mode = AutoSizeMode::grow_and_shrink;
    (*value).margin = Insets{};
    (*value).dock = DockStyle::fill;
    std::shared_ptr<Label> label = make_control<Label>(
        StableId(label_id), std::move(text));
    (*label).fore_color = foreground;
    (*label).font = font;
    (*label).text_wrapping = wrapping;
    (*label).vertical_alignment = VerticalAlignment::center;
    (*label).margin = Insets{};
    (*label).padding = {7.0, 3.0, 7.0, 3.0};
    (*label).dock = DockStyle::fill;
    (*value).add_child(label);
    return value;
}

class SidePane : public Panel {
public:
    SidePane(StableId id, bool seam_at_right)
        : Panel(std::move(id)), seam_at_right_(seam_at_right) {}

    void on_paint(Painter& painter, Rect damage) override {
        Panel::on_paint(painter, damage);
        const Rect bounds = client_rectangle();
        const double x = seam_at_right_ ? std::max(0.0, bounds.width - 1.5)
                                        : 1.5;
        painter.draw_line({x, 0.0}, {x, bounds.height}, graphite_dark, 3.0);
    }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        const std::vector<Control::Ptr> retained(children().begin(),
                                                 children().end());
        for (const Control::Ptr& child : retained) {
            if (!child || !(*child).visible.read()) continue;
            set_child_layout(child, seam_at_right_
                ? Rect{0.0, 0.0, std::max(0.0, final_bounds.width - 3.0),
                       final_bounds.height}
                : Rect{3.0, 0.0, std::max(0.0, final_bounds.width - 3.0),
                       final_bounds.height});
        }
    }

private:
    bool seam_at_right_{};
};

class SelectionPane final : public SidePane {
public:
    explicit SelectionPane(StableId id)
        : SidePane(std::move(id), false) {}

    void set_action(std::shared_ptr<Button> value) {
        action_ = std::move(value);
    }
    void set_inspector(std::shared_ptr<VisualInspectorView> value) {
        inspector_ = std::move(value);
    }

    void arrange(Rect final_bounds) override {
        SidePane::arrange(final_bounds);
        if (action_) set_child_layout(action_, {10.0, 7.0,
            std::max(0.0, final_bounds.width - 18.0), 27.0});
        if (inspector_) set_child_layout(inspector_, {6.0, 40.0,
            std::max(0.0, final_bounds.width - 12.0),
            std::max(0.0, final_bounds.height - 46.0)});
    }

private:
    std::shared_ptr<Button> action_;
    std::shared_ptr<VisualInspectorView> inspector_;
};

class LayoutDiagnosticsView final : public Control {
public:
    explicit LayoutDiagnosticsView(StableId id) : Control(std::move(id)) {
        hit_test_transparent = true;
    }

    void set_sources(std::shared_ptr<ResponsiveTrackPanel> rows,
                     std::shared_ptr<ResponsiveTrackPanel> columns) {
        rows_ = std::move(rows);
        columns_ = std::move(columns);
    }

    [[nodiscard]] std::string summary() const {
        const std::shared_ptr<ResponsiveTrackPanel> rows = rows_.lock();
        const std::shared_ptr<ResponsiveTrackPanel> columns = columns_.lock();
        if (!rows || !columns) return "layout diagnostics unavailable";
        const ResponsiveLayoutSnapshot vertical = (*rows).layout_snapshot();
        const ResponsiveLayoutSnapshot horizontal = (*columns).layout_snapshot();
        std::ostringstream text;
        text << "rows ";
        for (std::size_t index = 0U;
             index < vertical.resolution.tracks.size(); ++index) {
            if (index != 0U) text << '/';
            const ResponsiveTrackResult& track =
                vertical.resolution.tracks[index];
            if (track.collapsed()) text << 'x';
            else text << static_cast<int>(std::lround(track.allocated));
        }
        text << "  columns ";
        for (std::size_t index = 0U;
             index < horizontal.resolution.tracks.size(); ++index) {
            if (index != 0U) text << '/';
            const ResponsiveTrackResult& track =
                horizontal.resolution.tracks[index];
            if (track.collapsed()) text << 'x';
            else text << static_cast<int>(std::lround(track.allocated));
        }
        text << "  thresholds selection≤436 tree≤273"
             << "  rev " << vertical.committed_resolution_revision << '/'
             << horizontal.committed_resolution_revision;
        return text.str();
    }

    void on_paint(Painter& painter, Rect) override {
        const Rect bounds = client_rectangle();
        painter.fill_rect(bounds, Color::rgba(255, 255, 255, 220));
        painter.stroke_rect(bounds, sapphire, 1.0);
        painter.draw_text_utf8({7.0, 15.0}, summary(),
            effective_font({FontRole::monospace, 8.0, 400, false}),
            graphite_dark);
    }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor;
        descriptor.role = SemanticRole::group;
        descriptor.name = "Responsive layout diagnostics";
        descriptor.value = summary();
        descriptor.exposed = true;
        return descriptor;
    }

private:
    std::weak_ptr<ResponsiveTrackPanel> rows_;
    std::weak_ptr<ResponsiveTrackPanel> columns_;
};

class ContentPane final : public Panel {
public:
    explicit ContentPane(StableId id) : Panel(std::move(id)) {}

    void set_primary(std::shared_ptr<Button> value) {
        primary_ = std::move(value);
    }
    void set_diagnostics(std::shared_ptr<LayoutDiagnosticsView> value) {
        diagnostics_ = std::move(value);
    }
    void set_overlay(std::shared_ptr<VisualInspectorOverlay> value) {
        overlay_ = std::move(value);
    }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        if (primary_) set_child_layout(primary_, {10.0, 10.0,
            std::min(310.0, std::max(0.0, final_bounds.width - 20.0)), 31.0});
        if (diagnostics_) set_child_layout(diagnostics_, {10.0, 49.0,
            std::max(0.0, final_bounds.width - 20.0), 27.0});
        if (overlay_) set_child_layout(overlay_,
            {0.0, 0.0, final_bounds.width, final_bounds.height});
    }

private:
    std::shared_ptr<Button> primary_;
    std::shared_ptr<LayoutDiagnosticsView> diagnostics_;
    std::shared_ptr<VisualInspectorOverlay> overlay_;
};

class LabRoot final : public Panel {
public:
    explicit LabRoot(StableId id) : Panel(std::move(id)) {}

    void set_surface(std::shared_ptr<ResponsiveTrackPanel> value) {
        surface_ = std::move(value);
    }
    void set_surface_size(Size value) {
        if (surface_size_ == value) return;
        surface_size_ = value;
        invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test |
                   Dirty::semantics | Dirty::accessibility);
    }
    void add_controller(std::shared_ptr<Button> value) {
        controllers_.push_back(std::move(value));
    }
    void retain(SubscriptionToken token) {
        subscriptions_.push_back(std::move(token));
    }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        double x = 7.0;
        for (const std::shared_ptr<Button>& button : controllers_) {
            const double width = (*button).text.read().find('%') != std::string::npos
                ? 54.0 : 82.0;
            set_child_layout(button, {x, 8.0, width, 27.0});
            x += width + 5.0;
        }
        if (surface_) set_child_layout(surface_, {0.0, controller_height,
            std::min(surface_size_.width, final_bounds.width),
            std::min(surface_size_.height,
                     std::max(0.0, final_bounds.height - controller_height))});
    }

private:
    std::shared_ptr<ResponsiveTrackPanel> surface_;
    Size surface_size_{1450.0, 850.0};
    std::vector<std::shared_ptr<Button>> controllers_;
    std::vector<SubscriptionToken> subscriptions_;
};

struct SelectSize final {
    std::weak_ptr<LabRoot> root;
    Size surface_size;
    void operator()(ButtonBase&) const {
        if (const std::shared_ptr<LabRoot> owner = root.lock()) {
            (*owner).set_surface_size(surface_size);
        }
    }
};

struct SelectTextScale final {
    double scale{};
    void operator()(ButtonBase& source) const {
        if (Window* owner = source.attached_window()) {
            (*owner).set_text_scale(scale);
        }
    }
};

struct SelectReveal final {
    std::weak_ptr<ResponsiveTrackPanel> workspace;
    bool revealed{};
    void operator()(ButtonBase&) const {
        if (const std::shared_ptr<ResponsiveTrackPanel> value = workspace.lock()) {
            (*value).set_track_revealed(2U, revealed);
        }
    }
};

} // namespace

std::unique_ptr<Window> make_responsive_tracks_lab() {
    std::shared_ptr<LabRoot> root =
        make_control<LabRoot>(StableId("responsive.lab.root"));
    (*root).set_background(Color::rgba(205, 214, 220));

    std::shared_ptr<ResponsiveTrackPanel> surface =
        make_control<ResponsiveTrackPanel>(StableId("responsive.surface"));
    (*surface).orientation = ResponsiveTrackOrientation::vertical;
    (*surface).set_track_specs({
        {ResponsiveTrackSizeMode::content, 40.0, 40.0, 100.0, 1.0,
         false, 50U},
        {ResponsiveTrackSizeMode::content, 23.0, 23.0, 90.0, 1.0},
        {ResponsiveTrackSizeMode::content, 45.0, 66.0, 160.0, 1.0,
         false, 10U},
        {ResponsiveTrackSizeMode::content, 40.0, 40.0, 140.0, 1.0},
        {ResponsiveTrackSizeMode::remaining, 24.0, 200.0, 0.0, 1.0},
        {ResponsiveTrackSizeMode::content, 24.0, 24.0, 80.0, 1.0,
         false, 20U},
    });
    (*surface).dock = DockStyle::fill;

    std::vector<std::shared_ptr<Panel>> bands{
        band("responsive.band.title", "Projects — File Manager",
             sapphire, Color::rgba(255, 255, 255),
             {FontRole::control, 17.0, 700, false}, TextWrapping::word),
        band("responsive.band.menu", "File · Home · Edit · View · Go · Commands · Help",
             pearl, graphite_dark,
             {FontRole::control, 9.0, 700, false}, TextWrapping::word),
        band("responsive.band.shelf",
             "Open   Copy   Rename   Search   Inspect   More commands…",
             Color::rgba(239, 244, 247), graphite_dark,
             {FontRole::control, 10.0, 700, false}, TextWrapping::word),
        band("responsive.band.navigation",
             "Back  ·  Up  ·  ./Projects/Facade Study  ·  Search",
             graphite, Color::rgba(247, 249, 250),
             {FontRole::content, 10.0, 400, false}, TextWrapping::word),
        {},
        band("responsive.band.status",
             "12 objects · local authority · generation 42 · view mode",
             graphite_dark, Color::rgba(242, 246, 247),
             {FontRole::content, 9.0, 400, false}, TextWrapping::word),
    };

    std::shared_ptr<ResponsiveTrackPanel> workspace =
        make_control<ResponsiveTrackPanel>(StableId("responsive.workspace"));
    (*workspace).set_track_specs({
        {ResponsiveTrackSizeMode::fixed, 123.0, 221.0, 320.0, 1.0,
         false, 20U},
        {ResponsiveTrackSizeMode::remaining, 150.0, 300.0, 0.0, 1.0},
        {ResponsiveTrackSizeMode::fixed, 163.0, 291.0, 360.0, 1.0,
         false, 10U},
    });
    (*workspace).dock = DockStyle::fill;
    (*workspace).margin = Insets{};
    bands[4] = nullptr;

    std::shared_ptr<SidePane> tree =
        make_control<SidePane>(StableId("responsive.pane.tree"), true);
    (*tree).set_background(Color::rgba(232, 237, 239));
    (*tree).dock = DockStyle::fill;
    (*tree).margin = Insets{};
    std::shared_ptr<Panel> tree_text = band(
        "responsive.pane.tree.content",
        "FOLDERS\nProjects\n  Facade Study\nDocuments\nDownloads",
        Color::rgba(232, 237, 239), graphite_dark,
        {FontRole::content, 10.0, 400, false}, TextWrapping::word);
    (*tree).add_child(tree_text);

    std::shared_ptr<ContentPane> content =
        make_control<ContentPane>(StableId("responsive.pane.content"));
    (*content).set_background(paper);
    (*content).dock = DockStyle::fill;
    (*content).margin = Insets{};
    std::shared_ptr<Button> primary = make_control<Button>(
        StableId("responsive.content.primary"),
        "Facade Study");
    (*primary).font = {FontRole::content, 10.0, 400, false};
    std::shared_ptr<LayoutDiagnosticsView> diagnostics =
        make_control<LayoutDiagnosticsView>(
            StableId("responsive.layout.diagnostics"));
    std::shared_ptr<VisualInspectorOverlay> overlay =
        make_control<VisualInspectorOverlay>(
            StableId("responsive.layout.overlay"),
            "responsive.content.primary");
    (*content).add_child(primary);
    (*content).add_child(diagnostics);
    (*content).add_child(overlay);
    (*content).set_primary(primary);
    (*content).set_diagnostics(diagnostics);
    (*content).set_overlay(overlay);

    std::shared_ptr<SelectionPane> selection =
        make_control<SelectionPane>(StableId("responsive.pane.selection"));
    (*selection).set_background(Color::rgba(245, 243, 235));
    (*selection).dock = DockStyle::fill;
    (*selection).margin = Insets{};
    std::shared_ptr<Button> selection_action = make_control<Button>(
        StableId("responsive.selection.action"), "Selection inspector");
    std::shared_ptr<VisualInspectorView> inspector =
        make_control<VisualInspectorView>(
            StableId("responsive.selection.inspector"),
            "responsive.content.primary");
    (*selection).add_child(selection_action);
    (*selection).add_child(inspector);
    (*selection).set_action(selection_action);
    (*selection).set_inspector(inspector);

    (*workspace).add_child(tree);
    (*workspace).add_child(content);
    (*workspace).add_child(selection);
    (*workspace).child_track[*tree] = 0U;
    (*workspace).child_track[*content] = 1U;
    (*workspace).child_track[*selection] = 2U;
    (*workspace).set_track_focus_fallback(2U, *primary);
    (*workspace).set_track_focus_fallback(0U, *primary);
    (*diagnostics).set_sources(surface, workspace);

    for (std::size_t index = 0U; index < bands.size(); ++index) {
        Control::Ptr child = index == 4U
            ? std::static_pointer_cast<Control>(workspace)
            : std::static_pointer_cast<Control>(bands[index]);
        (*surface).add_child(child);
        (*surface).child_track[*child] = static_cast<std::uint32_t>(index);
    }
    (*root).add_child(surface);
    (*root).set_surface(surface);

    const std::vector<Size> presets{
        {1450.0, 850.0}, {1200.0, 760.0}, {960.0, 680.0},
        {720.0, 520.0}, {480.0, 360.0}, {300.0, 240.0},
        {150.0, 150.0},
    };
    for (const Size size : presets) {
        const std::string label = std::to_string(static_cast<int>(size.width)) +
            "×" + std::to_string(static_cast<int>(size.height));
        std::shared_ptr<Button> button = make_control<Button>(
            StableId("responsive.preset." +
                     std::to_string(static_cast<int>(size.width))), label);
        (*button).font = {FontRole::control, 8.0, 700, false};
        (*root).add_child(button);
        (*root).add_controller(button);
        (*root).retain((*button).clicked().subscribe(
            *root, SelectSize{root, size}));
    }
    for (const double scale : {1.0, 1.25, 1.5, 2.0}) {
        const std::string label =
            std::to_string(static_cast<int>(scale * 100.0)) + "%";
        std::shared_ptr<Button> button = make_control<Button>(
            StableId("responsive.scale." + std::to_string(static_cast<int>(scale * 100.0))), label);
        (*button).font = {FontRole::control, 8.0, 700, false};
        (*root).add_child(button);
        (*root).add_controller(button);
        (*root).retain((*button).clicked().subscribe(
            *root, SelectTextScale{scale}));
    }
    for (const std::pair<std::string, bool>& choice :
         std::vector<std::pair<std::string, bool>>{
             {"Reveal", true}, {"Auto", false}}) {
        std::shared_ptr<Button> button = make_control<Button>(
            StableId("responsive.reveal." + std::to_string(choice.second)),
            choice.first);
        (*button).font = {FontRole::control, 8.0, 700, false};
        (*root).add_child(button);
        (*root).add_controller(button);
        (*root).retain((*button).clicked().subscribe(
            *root, SelectReveal{workspace, choice.second}));
    }

    std::unique_ptr<Window> window = std::make_unique<Window>(
        root, Size{1450.0, 850.0 + controller_height});
    (*window).perform_layout();
    return window;
}

} // namespace gui_forms::responsive_tracks_lab
