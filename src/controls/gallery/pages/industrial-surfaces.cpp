#include "pages.hpp"

#include "gost_paints_theme_package.hpp"
#include "industrial-surfaces_form.hpp"
#include "industrial_theme_package.hpp"

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/engine/light_planes/light_planes.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace gui_forms::gallery::pages {

namespace {

// -- the page's visual choices ------------------------------------------------------------
//
// The lamps the page shows, each a bake of the industrial-surfaces study's
// baker (lamp_keys.cpp: `lamps <dir> 240 dialog` and `lamps <dir> 112
// indicator`), named by its stem without `_up`, with its lamp's colour and
// its place in gallery.industrial.keys. These are the page's choices, not
// rulings; the owner's references are still open. A lamp is an indicator
// (owner, 2026-11-12): it shows a state and is never clicked on or off,
// so the page switches them from an ordinary check box.
struct LampChoice final {
    std::string_view stem{};
    std::string_view accessible_name{};
    engine_light::LampColour colour{};
    Rect bounds{};
};

constexpr std::array<LampChoice, 4> lamp_choices{{
    {.stem = "dialog_ok", .accessible_name = "OK lamp", .colour = engine_light::lamp_green,
     .bounds = {0.0, 0.0, 180.0, 120.0}},
    {.stem = "dialog_cancel", .accessible_name = "CANCEL lamp", .colour = engine_light::lamp_amber,
     .bounds = {196.0, 0.0, 180.0, 120.0}},
    {.stem = "indicator_backed", .accessible_name = "Backed indicator", .colour = engine_light::lamp_red,
     .bounds = {0.0, 132.0, 56.0, 56.0}},
    {.stem = "indicator_clear", .accessible_name = "Clear indicator", .colour = engine_light::lamp_amber,
     .bounds = {72.0, 132.0, 56.0, 56.0}},
}};

// Where the bakes are read from: GUI_FORMS_LIGHT_PLANES when it is set (a
// preview of the integrator's bakes), else the source tree's
// assets/light_planes, which the build names (GUI_FORMS_LIGHT_PLANE_DIR).
std::filesystem::path light_plane_directory() {
    const char* const chosen = std::getenv("GUI_FORMS_LIGHT_PLANES");
    if (chosen != nullptr && *chosen != '\0') return std::filesystem::path(chosen);
    return std::filesystem::path(GUI_FORMS_LIGHT_PLANE_DIR);
}

// -- one lamp -------------------------------------------------------------------------------

// A lamp shown from its baked light planes: the up bake, composited at the
// window's display scale with every lamp of the bake at this lamp's colour
// and power, and drawn device-aligned. The planes are stretched over the
// largest rectangle of their aspect at the control's top left. A lamp
// strikes over the study's 60 ms (lamp_power) and cools over 120 ms from
// the power it had (lamp_cooling_power), its frames ending once it is
// fully off; under reduced motion it is lit and put out at once. It lights its own lens and legend
// only: the composite is transparent where the bake's coverage is 0.
// Painting recomposites only when the power, the scale or the size moved.
// It takes no input. One thread: the UI thread.
class LampIndicator final : public Control {
public:
    LampIndicator(StableId stable_id, std::shared_ptr<const engine_light::LightPlanes> planes,
                  engine_light::LampColour colour)
        : Control(std::move(stable_id)), planes_(std::move(planes)), colour_(colour) {}

    void set_lit(bool lit);

protected:
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_detached_from_window() noexcept override;

private:
    // The strike's frames: observes the lamp, so a destroyed lamp's tick is
    // skipped.
    struct StrikeTick final {
        std::weak_ptr<LampIndicator> lamp;
        AnimationTickResult operator()(FrameTime now) const;
    };

    [[nodiscard]] bool reduced_motion() const noexcept;
    [[nodiscard]] Rect shown_rect() const noexcept;
    AnimationTickResult advance(FrameTime now);
    void composite(DeviceExtent extent, double scale);

    std::shared_ptr<const engine_light::LightPlanes> planes_;
    engine_light::LampColour colour_{};
    bool lit_{};
    double power_{};
    // The strike's or the cool-down's first frame, taken from the timer's
    // clock, and the power the cool-down starts from.
    std::optional<FrameTime> struck_at_{};
    double cooling_from_{};
    // The last composite and the power it was made at.
    std::shared_ptr<const PartRaster> raster_{};
    double raster_power_{};
    AnimationTimer strike_timer_;
};

void LampIndicator::set_lit(bool lit) {
    if (lit == lit_) return;
    lit_ = lit;
    struck_at_.reset();
    strike_timer_.stop();
    cooling_from_ = power_;
    const bool at_once = reduced_motion() || attached_window() == nullptr || (!lit && !(power_ > 0.0));
    if (at_once) {
        power_ = engine_light::lamp_power(lit, engine_light::lamp_strike_seconds, true);
        invalidate(Dirty::paint);
        return;
    }
    // The first frame of the strike, or the power the cool-down starts at.
    if (lit) power_ = engine_light::lamp_power(true, 0.0, false);
    invalidate(Dirty::paint);
    const std::shared_ptr<LampIndicator> self = std::dynamic_pointer_cast<LampIndicator>(shared_from_this());
    if (!self) return;
    strike_timer_.start(*this, display_frame_interval, StrikeTick{self});
}

bool LampIndicator::reduced_motion() const noexcept {
    const Window* const owner = attached_window();
    const bool reduced = owner != nullptr && (*owner).presentation_settings().reduced_motion;
    return reduced;
}

Rect LampIndicator::shown_rect() const noexcept {
    const Rect client = client_rectangle();
    if (!planes_ || (*planes_).width == 0U || client.width <= 0.0 || client.height <= 0.0) return Rect{};
    const double aspect = static_cast<double>((*planes_).height) / static_cast<double>((*planes_).width);
    double width = client.width;
    double height = width * aspect;
    if (height > client.height) {
        height = client.height;
        width = height / aspect;
    }
    return Rect{0.0, 0.0, width, height};
}

AnimationTickResult LampIndicator::advance(FrameTime now) {
    if (!struck_at_) struck_at_ = now;
    const double seconds = std::chrono::duration<double>(now - *struck_at_).count();
    const bool reduced = reduced_motion();
    power_ = lit_ ? engine_light::lamp_power(true, seconds, reduced)
                  : engine_light::lamp_cooling_power(cooling_from_, seconds, reduced);
    invalidate(Dirty::paint);
    const bool ended = lit_ ? engine_light::lamp_settled(seconds) : engine_light::lamp_cooled(seconds);
    if (ended || reduced) strike_timer_.stop();
    return AnimationTickResult::damaged;
}

AnimationTickResult LampIndicator::StrikeTick::operator()(FrameTime now) const {
    const std::shared_ptr<LampIndicator> live = lamp.lock();
    if (!live) return AnimationTickResult::unchanged;
    return (*live).advance(now);
}

void LampIndicator::on_detached_from_window() noexcept {
    Control::on_detached_from_window();
    strike_timer_.stop();
    // A lamp struck or cooling while away shows its end state when shown again.
    power_ = engine_light::lamp_power(lit_, engine_light::lamp_strike_seconds, true);
}

// Composites the planes over `extent` device pixels at `scale` into a new
// raster; on a refusal (an extent past the raster bound) the last raster
// stays.
void LampIndicator::composite(DeviceExtent extent, double scale) {
    const std::shared_ptr<PartRaster> made = PartRaster::create(extent, DeviceScale{scale});
    if (!made) return;
    std::array<engine_light::LampDrive, engine_light::maximum_light_plane_lamps> drives{};
    for (engine_light::LampDrive& drive : drives) {
        drive = engine_light::LampDrive{colour_, power_};
    }
    engine_light::LightComposite settings{};
    settings.room = engine_light::dialog_key_study_bake.room;
    const engine_light::LightPlaneRect rect{0, 0, static_cast<std::int64_t>(extent.width),
                                            static_cast<std::int64_t>(extent.height)};
    static_cast<void>(engine_light::composite_light_planes(*planes_, *planes_, drives, settings, rect, (*made).view()));
    (*made).record_opacity();
    raster_ = made;
    raster_power_ = power_;
}

void LampIndicator::on_paint(Painter& painter, Rect local_damage) {
    static_cast<void>(local_damage);
    if (!planes_) return;
    const SurfaceRoot* const owner = surface_root();
    const double scale = owner != nullptr ? (*owner).scale() : 1.0;
    const Rect shown = shown_rect();
    const double device_width = std::round(shown.width * scale);
    const double device_height = std::round(shown.height * scale);
    if (!(device_width >= 1.0 && device_height >= 1.0)) return;
    const DeviceExtent extent{static_cast<std::uint32_t>(device_width), static_cast<std::uint32_t>(device_height)};
    const bool current = raster_ && (*raster_).extent() == extent && (*raster_).scale() == DeviceScale{scale} &&
                         raster_power_ == power_;
    if (!current) composite(extent, scale);
    if (!raster_) return;
    painter.draw_part_raster(raster_, Point{shown.x, shown.y}, 1.0);
}

}  // namespace

// Owns the industrial-surfaces page (skeudoindustrial surfaces, phase 5):
// the controls demo/pages/industrial-surfaces.form describes, the lamps it
// places from the shipped bakes (lamp_choices), the Lamps check box that
// switches them, and the check box that puts the industrial theme
// (demo/themes/industrial, over the GOST paints) on the preview group.
class IndustrialSurfacesPage final : public gui_forms::Component {
public:
    explicit IndustrialSurfacesPage(const std::shared_ptr<Surface>& body);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void place_lamps();
    void on_lamps_changed(bool on);
    void on_theme_changed(bool on);
    [[nodiscard]] std::shared_ptr<const Theme> build_industrial_theme(Window& window);

    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    IndustrialSurfacesDocument<IndustrialSurfacesPage> document_{};
    engine_light::LightPlaneCache bakes_;
    std::vector<std::shared_ptr<LampIndicator>> lamps_{};
    // Compiled on first use and kept.
    std::shared_ptr<const Theme> industrial_{};
};

IndustrialSurfacesPage::IndustrialSurfacesPage(const std::shared_ptr<Surface>& body)
    : bakes_(light_plane_directory()) {
    document_.initialize_component(*this);
    mount_document(body);
    place_lamps();
    on((*document_.gallery_industrial_lamps).checked_changed(), *this, &IndustrialSurfacesPage::on_lamps_changed);
    on((*document_.gallery_industrial_theme).checked_changed(), *this, &IndustrialSurfacesPage::on_theme_changed);
}

void IndustrialSurfacesPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body, a design surface of the same 1000 x 660
    // size. The copy of the child list keeps the iteration valid while the
    // root shrinks.
    Control& root = *document_.gallery_industrial_surfaces_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
}

// One lamp for each choice whose bake is present; the status line says
// which are missing and how they are made.
void IndustrialSurfacesPage::place_lamps() {
    Control& keys = *document_.gallery_industrial_keys;
    std::size_t missing = 0;
    for (const LampChoice& choice : lamp_choices) {
        const std::string bake = std::string(choice.stem) + "_up";
        std::shared_ptr<const engine_light::LightPlanes> planes = bakes_.lend(bake);
        if (!planes) {
            ++missing;
            continue;
        }
        const std::string id = "gallery.industrial.lamp." + std::string(choice.stem);
        std::shared_ptr<LampIndicator> lamp = make_control<LampIndicator>(StableId(id), std::move(planes), choice.colour);
        (*lamp).accessible_name = std::string(choice.accessible_name);
        (*lamp).set_bounds(choice.bounds);
        keys.add_child(lamp);
        lamps_.push_back(std::move(lamp));
    }
    std::string text{};
    if (missing == lamp_choices.size()) {
        text = "No light-plane bakes in " + light_plane_directory().string() +
               ": the integrator bakes them with the study's baker (lamps <dir> 240 dialog; lamps <dir> 112 indicator).";
    } else if (missing != 0U) {
        text = std::to_string(missing) + " of the lamps' bakes are missing; the rest light their own lens and legend only.";
    } else {
        text = "Each lamp lights its own lens and legend only; the check box switches them, as a lamp is never clicked.";
    }
    (*document_.gallery_industrial_bakes).text = std::move(text);
}

void IndustrialSurfacesPage::on_lamps_changed(bool on) {
    for (const std::shared_ptr<LampIndicator>& lamp : lamps_) {
        (*lamp).set_lit(on);
    }
}

std::shared_ptr<const Theme> IndustrialSurfacesPage::build_industrial_theme(Window& window) {
    if (industrial_) return industrial_;
    ThemeLibrary library{};
    const std::vector<ThemeCompileDiagnostic> installed = library.install(gost_paints_theme_package());
    if (!installed.empty()) {
        (*document_.gallery_industrial_preview_state).text =
            "The GOST palette was refused: " + installed.front().code + " " + installed.front().name;
        return nullptr;
    }
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    options.effects = &built_in_effect_registry();
    options.base = ThemeBase::gui_forms;
    const std::shared_ptr<ApplicationResources> resources = window.resources();
    const ThemeCompileResult result = (*resources).compile_theme_package(industrial_theme_package(), library, options);
    if (!result.succeeded()) {
        std::string text = "The industrial theme was refused";
        if (!result.diagnostics().empty()) {
            const ThemeCompileDiagnostic& first = result.diagnostics().front();
            text += ": " + first.code + " " + first.name;
        }
        (*document_.gallery_industrial_preview_state).text = std::move(text);
        return nullptr;
    }
    industrial_ = result.theme();
    return industrial_;
}

void IndustrialSurfacesPage::on_theme_changed(bool on) {
    ScaledGroupBox& preview = *document_.gallery_industrial_preview;
    if (!on) {
        preview.clear_theme_override();
        (*document_.gallery_industrial_preview_state).text = "";
        return;
    }
    Window* const window = preview.attached_window();
    if (window == nullptr) return;
    std::shared_ptr<const Theme> theme = build_industrial_theme(*window);
    if (!theme) return;
    preview.set_theme_override(std::move(theme));
    (*document_.gallery_industrial_preview_state).text =
        "Pale mint keys in orange peel over grey-green, blue-green hammertone wells: GOST 319, 365 and 397.";
}

void add_industrial_surfaces(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<IndustrialSurfacesPage>(page));
}

}  // namespace gui_forms::gallery::pages
