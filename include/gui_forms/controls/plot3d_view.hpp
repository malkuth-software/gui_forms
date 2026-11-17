#pragma once

// Plot3DView: the plots package's 3D plot (D5; notes
// D5_PLOTTING_CONVENTIONS.md, owner decisions of 2026-11-13). It shows a
// plots::Plot3DScene (surfaces, lines and scatters in a box with back panes,
// grid and ticks), drawn on the CPU by stx_raster3d into one part raster,
// with its tick labels, axis titles and the hovered point's readout as
// themed text over it. The picture is kept: it is rendered again only when
// the view, the size, the scale, the items or the theme's colours change,
// once at one sample per pixel while a gesture moves it and again with
// 2 x 2 samples when the gesture ends.
//
// Interaction (matplotlib's mapping with the owner's choices: turntable
// rotation, wheel zoom on): left drag turns the box (azimuth across,
// elevation up and down), right drag zooms, middle drag or Shift + left
// drag pans, the wheel zooms about the pointer (`wheel_zoom`), a double
// click resets the view. Keyboard (a tab stop with a focus ring): the
// arrows turn the box 5 degrees as a drag that way does, Shift + arrows
// pan, + and - zoom, Home resets, P switches between perspective and
// orthographic projection.
//
// Picking: the pointer's point is looked up per pixel in the kept picture
// (exact on CONV surfaces, refined onto the interpolant) and shown at the
// plot's lower left as matplotlib's coordinate readout; while a drag turns
// the box the readout shows the angles. Data items have no tooltips (F9).
//
// Events, raised on the UI thread after the change they report and
// declared for rules (append_interactions): hovered when the point under
// the pointer changes (a none hit when it leaves the data); picked on a
// click that did not drag, with what lies under it; camera_changed once
// per change of the camera by a gesture, a key, set_camera or reset_view.
//
// Accessibility: one figure node (SemanticRole::figure), named by the
// accessible name, described by its items and view, valued by the readout.
//
// Labels: screen-facing by default; `labels_along_axis` lays each axis
// title along its edge as projected, turned at whatever angle that is
// (kept reading left to right). The picture, the labels and the readout are
// parts painted by the theme (paint_part): the picture as the image role's
// raster, the labels and readout as text roles.
//
// One UI thread.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/plots/plot3d_scene/plot3d_scene.hpp"
#include "gui_forms/controls/plot3d_view_capabilities.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class PartRaster;

class GUI_FORMS_API Plot3DView final : public Control, public Plot3DViewCapabilities<Plot3DView> {
public:
    explicit Plot3DView(StableId stable_id);
    ~Plot3DView() override;
    Plot3DView(const Plot3DView&) = delete;
    Plot3DView& operator=(const Plot3DView&) = delete;

    [[nodiscard]] Event<const plots::Plot3DHit&>& hovered() noexcept { return hovered_; }
    [[nodiscard]] Event<const plots::Plot3DHit&>& picked() noexcept { return picked_; }
    [[nodiscard]] Event<const plots::Plot3DCamera&>& camera_changed() noexcept { return camera_changed_; }

    // -- items: copied into the scene (Plot3DScene's rules and failures) -------
    plots::Plot3DItemId add_surface(const plots::Plot3DSurfaceSpec& spec);
    plots::Plot3DItemId add_line(const plots::Plot3DLineSpec& spec);
    plots::Plot3DItemId add_scatter(const plots::Plot3DScatterSpec& spec);
    bool remove_item(plots::Plot3DItemId item);
    void clear_items();
    // The scene as drawn last (items, limits, labels and statistics).
    [[nodiscard]] const plots::Plot3DScene& scene() const noexcept { return scene_; }

    // -- view ------------------------------------------------------------------
    [[nodiscard]] const plots::Plot3DCamera& camera() const noexcept { return scene_.camera(); }
    // Takes the camera (Plot3DScene::set_camera); raises camera_changed
    // when it changed.
    bool set_camera(const plots::Plot3DCamera& camera);
    // Back to the default angles, zoom and target (the projection stays).
    bool reset_view();
    // An axis's title, drawn beside its ticks; empty draws none. An axis
    // outside Plot3DAxis is refused with invalid_argument, and reads as empty.
    void set_axis_title(plots::Plot3DAxis axis, std::string_view title);
    [[nodiscard]] std::string_view axis_title(plots::Plot3DAxis axis) const noexcept;
    // The box's side ratios (positive and finite; std::invalid_argument
    // otherwise); 4:4:3 by default.
    void set_box_aspect(const std::array<double, 3>& aspect);

    // -- picking -----------------------------------------------------------------
    // What lies under a local point (from the control's top left) in the
    // kept picture.
    [[nodiscard]] plots::Plot3DHit hit_at(Point local_point) const;
    // The point under the pointer as last hovered, and its readout.
    [[nodiscard]] const plots::Plot3DHit& hover() const noexcept { return hover_; }
    [[nodiscard]] const std::string& readout() const& noexcept { return readout_; }
    const std::string& readout() const&& = delete;

    // The plot rectangle, in local coordinates.
    [[nodiscard]] Rect plot_bounds() const noexcept;
    // Pictures rendered (diagnostics and tests: a repaint that changes
    // nothing renders none).
    [[nodiscard]] std::uint64_t picture_renders() const noexcept { return scene_.render_count(); }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry plot;
        PartGeometry picture;
        PartGeometry x_axis;
        PartGeometry y_axis;
        PartGeometry z_axis;
        PartGeometry readout;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_wheel(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_mouse_capture_changed() override;
    void on_key_down(KeyEventArgs& arguments) override;
    void on_focus_changed(bool focused) override;

private:
    // What a drag does.
    enum class Drag : std::uint8_t { none, rotate, zoom, pan };

    // The routed forms of the pointer and key events.
    void handle_pointer(PointerEvent& event);
    void handle_key(KeyEvent& event);
    // After a gesture or key moved the camera: repaints and raises
    // camera_changed.
    void camera_moved();
    void end_drag();
    // The point under `plot_point` (from the plot's top left) becomes the
    // hover: raises hovered when it differs.
    void update_hover(Point plot_point);
    void clear_hover();
    // The scene's options from the properties, the titles and the aspect
    // (`titles` and `aspect` as given); the picture is stale when they
    // changed.
    void apply_options(const std::array<std::string, 3>& titles, const std::array<double, 3>& aspect);
    // The scene's colours from the theme.
    void sync_colors(AnatomyStateSet states);
    // Renders the picture into the kept raster (a new one while a recording
    // still holds the old one); false, the old picture kept, when it could
    // not.
    bool render_picture(const plots::Plot3DFrame& frame);
    void paint_labels(Painter& painter, AnatomyStateSet states);
    [[nodiscard]] double device_scale() const noexcept;
    [[nodiscard]] std::string angle_readout() const;
    [[nodiscard]] Rect readout_bounds() const noexcept;
    [[nodiscard]] Rect axis_bounds(plots::Plot3DAxis axis) const noexcept;

    plots::Plot3DScene scene_{};
    // The kept picture and the frame it was rendered for; stale when
    // anything it shows changed since.
    std::shared_ptr<PartRaster> picture_{};
    plots::Plot3DFrame drawn_frame_{};
    bool picture_stale_{true};
    Drag drag_{Drag::none};
    bool dragged_{};
    Point last_pointer_{};
    plots::Plot3DHit hover_{};
    std::string readout_{};
    Event<const plots::Plot3DHit&> hovered_{};
    Event<const plots::Plot3DHit&> picked_{};
    Event<const plots::Plot3DCamera&> camera_changed_{};
    // property_codemod begin Plot3DView (tools/property_families/plot3d_view.json; generated, do not edit)
public:
    // Plot3DView's property table: Control's entries, then its own.
    static constexpr std::size_t plot3d_view_property_count = Control::control_property_count + 5U;
    [[nodiscard]] static const std::array<PropertyEntry, plot3d_view_property_count>& plot3d_view_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct Plot3DViewShowPanesCapability<Plot3DView>;
    friend struct Plot3DViewShowGridCapability<Plot3DView>;
    friend struct Plot3DViewShowAxesCapability<Plot3DView>;
    friend struct Plot3DViewWheelZoomCapability<Plot3DView>;
    friend struct Plot3DViewLabelsAlongAxisCapability<Plot3DView>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(show_panes)& cell, bool& incoming);
    void written(const decltype(show_panes)& cell);
    void prepare_write(const decltype(show_grid)& cell, bool& incoming);
    void written(const decltype(show_grid)& cell);
    void prepare_write(const decltype(show_axes)& cell, bool& incoming);
    void written(const decltype(show_axes)& cell);
    void prepare_write(const decltype(wheel_zoom)& cell, bool& incoming);
    void written(const decltype(wheel_zoom)& cell);
    void prepare_write(const decltype(labels_along_axis)& cell, bool& incoming);
    void written(const decltype(labels_along_axis)& cell);
    // property_codemod end Plot3DView
};

} // namespace gui_forms
