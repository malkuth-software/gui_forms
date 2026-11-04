#include "compositor_lab.hpp"

#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gui_forms::compositor_lab {

namespace {

constexpr Color ink = Color::rgba(24, 32, 44);
// The window recipe over `opaque`, and the backplane bases over the others
// (straight alpha): a light tint over either material, clear over
// transparent.
constexpr Color paper = Color::rgba(236, 238, 242);
constexpr Color window_material_tint = Color::rgba(240, 244, 250, 72);
constexpr Color transient_material_tint = Color::rgba(250, 250, 252, 104);
constexpr Color cleared = Color::rgba(0, 0, 0, 0);

// The scenery's bands, left to right.
constexpr std::array<Color, 5> scenery_bands{
    Color::rgba(196, 64, 52), Color::rgba(232, 168, 48), Color::rgba(64, 152, 88),
    Color::rgba(48, 112, 196), Color::rgba(120, 72, 168)};
constexpr double scenery_width = 300.0;
constexpr double scenery_band_width = scenery_width / 5.0;
// The cross band, so that a reflection of the backplane shows rows as well
// as columns.
constexpr Rect scenery_cross_band{0.0, 236.0, scenery_width, 14.0};
constexpr Color scenery_cross_colour = Color::rgba(250, 250, 250);

// The parts, in window coordinates (the pane covers the window).
constexpr Rect glass_bounds{160.0, 36.0, 280.0, 128.0};
constexpr Rect backplane_mirror_bounds{40.0, 196.0, 200.0, 40.0};
constexpr Rect face_mirror_bounds{320.0, 196.0, 200.0, 40.0};
constexpr Rect status_bounds{20.0, 312.0, 520.0, 28.0};

SurfaceMaterial solid_material(Color colour) {
    SurfaceMaterial material{};
    material.fills.clear();
    material.fills.push_back(MaterialFillLayer::solid(colour));
    return material;
}

// A light raised face with a reflection below it, 2 units away and reaching
// 6 (gap plus reach is at most 8: the material outsets, owner decision
// T5-Q15), from half to no opacity.
SurfaceMaterial mirrored_material(MaterialReflectionSource source) {
    SurfaceMaterial material{};
    material.fills.clear();
    material.fills.push_back(MaterialFillLayer::linear({0.5, 0.0}, {0.5, 1.0}, {
        {0.0, Color::rgba(252, 252, 255, 236)},
        {1.0, Color::rgba(206, 218, 236, 236)},
    }));
    material.corner_radius = 6.0;
    MaterialReflection reflection{};
    reflection.edge = MaterialEdge::bottom;
    reflection.gap = 2.0;
    reflection.reach = 6.0;
    reflection.start_opacity = 0.5;
    reflection.end_opacity = 0.0;
    reflection.source = source;
    material.reflection = reflection;
    return material;
}

void set_ordinary_material(ThemeDefinition& definition, ControlVisualRole role, const SurfaceMaterial& material) {
    ControlRoleRecipes& recipes = definition.roles[static_cast<std::size_t>(role)];
    for (ControlVisualRecipe& recipe : recipes.ordinary) recipe.material = material;
}

// The backplane-plane scenery: coloured bands with a light cross band, over
// the left part of the window. The rest of the window shows its base.
class Scenery final : public Control {
public:
    explicit Scenery(StableId stable_id) : Control(std::move(stable_id)) { set_paint_plane(PaintPlane::backplane); }

    void on_paint(Painter& painter, Rect) override {
        for (std::size_t index = 0U; index < scenery_bands.size(); ++index) {
            const double left = scenery_band_width * static_cast<double>(index);
            const Rect band{left, 0.0, scenery_band_width, lab_size.height};
            painter.fill_rect(band, scenery_bands[index]);
        }
        painter.fill_rect(scenery_cross_band, scenery_cross_colour);
    }
};

// The control-plane pane: the glass region and the two mirrored faces.
class Pane final : public Control {
public:
    explicit Pane(StableId stable_id) : Control(std::move(stable_id)) {}

    void on_paint(Painter& painter, Rect) override {
        PartPaint glass{};
        glass.role = PartRole::region;
        glass.family = ControlVisualRole::card;
        glass.bounds = glass_bounds;
        glass.corner_radius = 10.0;
        glass.key = PartKey{"compositor-lab.glass", {}, {}};
        paint_part(painter, *this, glass);

        PartPaint backplane_mirror{};
        backplane_mirror.role = PartRole::face;
        backplane_mirror.family = ControlVisualRole::choice;
        backplane_mirror.bounds = backplane_mirror_bounds;
        backplane_mirror.key = PartKey{"compositor-lab.backplane-mirror", {}, {}};
        paint_part(painter, *this, backplane_mirror);

        PartPaint face_mirror{};
        face_mirror.role = PartRole::face;
        face_mirror.family = ControlVisualRole::button;
        face_mirror.bounds = face_mirror_bounds;
        face_mirror.key = PartKey{"compositor-lab.face-mirror", {}, {}};
        paint_part(painter, *this, face_mirror);
    }
};

std::string status_text(const WindowBackdropGrant& grant) {
    std::string text = "requested ";
    text += window_backdrop_name(grant.requested);
    text += ", granted ";
    text += window_backdrop_name(grant.granted);
    text += " (";
    text += window_backdrop_reason_name(grant.reason);
    text += ")";
    return text;
}

// The root draws nothing, so that the window's clear pixels show its base.
// It owns the status line's updates and the printed records.
class LabRoot final : public Control {
public:
    LabRoot(StableId stable_id, bool print_records)
        : Control(std::move(stable_id)), print_records_(print_records) {}

    void on_paint(Painter&, Rect) override {}

    void attach_status(std::shared_ptr<Label> status) { status_ = std::move(status); }

    // Shows `grant` on the status line.
    void show(const WindowBackdropGrant& grant) {
        if (status_ != nullptr) (*status_).set_text(status_text(grant));
    }

    // backdrop_changed's target, on the UI thread: shows the grant and
    // records it.
    void on_backdrop_changed(const WindowBackdropGrant& grant) {
        show(grant);
        record(grant);
    }

    // With print_records_, prints the record of `grant` unless it is the one
    // printed last (the host-ready record and a change raised at attach may
    // name the same grant).
    void record(const WindowBackdropGrant& grant) {
        const Window* const shown = window();
        if (!print_records_ || shown == nullptr) return;
        if (printed_any_ && same_grant(grant, last_printed_)) return;
        const std::string line = grant_record(*shown, grant);
        std::printf("%s\n", line.c_str());
        static_cast<void>(std::fflush(stdout));
        last_printed_ = grant;
        printed_any_ = true;
    }

private:
    static bool same_grant(const WindowBackdropGrant& left, const WindowBackdropGrant& right) noexcept {
        const bool same = left.requested == right.requested && left.granted == right.granted &&
                          left.reason == right.reason && left.generation.value == right.generation.value;
        return same;
    }

    std::shared_ptr<Label> status_;
    WindowBackdropGrant last_printed_{};
    bool printed_any_{};
    bool print_records_{};
};

std::shared_ptr<Label> caption(std::string id, std::string text, Rect bounds) {
    std::shared_ptr<Label> made = make_control<Label>(StableId(std::move(id)), std::move(text));
    (*made).set_foreground(ink);
    (*made).set_requested_bounds(bounds);
    return made;
}

void append_json_string(std::string& out, std::string_view key, std::string_view value) {
    out += '"';
    out += key;
    out += "\":\"";
    out += value;
    out += '"';
}

} // namespace

WindowBackdropRequest default_request() noexcept {
    const WindowBackdropRequest request =
        WindowBackdropRequest::with_fallback(WindowBackdrop::window_material, WindowBackdrop::transparent);
    return request;
}

std::optional<WindowBackdropRequest> parse_backdrop_request(std::string_view text) {
    if (text.empty()) return default_request();
    WindowBackdropRequest request{};
    request.length = 0U;
    std::size_t start = 0U;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view name = text.substr(start, end - start);
        std::optional<WindowBackdrop> found{};
        for (std::size_t index = 0U; index < window_backdrop_count; ++index) {
            const WindowBackdrop candidate = static_cast<WindowBackdrop>(index);
            if (name == window_backdrop_name(candidate)) found = candidate;
        }
        if (!found.has_value() || request.length == WindowBackdropRequest::maximum_entries) return std::nullopt;
        request.entries[request.length] = *found;
        request.length = request.length + 1U;
        if (comma == std::string_view::npos) break;
        start = comma + 1U;
    }
    if (!window_backdrop_request_valid(request)) return std::nullopt;
    return request;
}

std::shared_ptr<const Theme> compositor_lab_theme() {
    // ThemeDefinition is large; it is built on the heap.
    const std::unique_ptr<ThemeDefinition> held(new ThemeDefinition(windows_professional_theme_definition()));
    ThemeDefinition& definition = *held;
    definition.id = "compositor-lab";
    set_ordinary_material(definition, ControlVisualRole::window, solid_material(paper));
    set_ordinary_material(definition, ControlVisualRole::button,
                          mirrored_material(MaterialReflectionSource::face));
    set_ordinary_material(definition, ControlVisualRole::choice,
                          mirrored_material(MaterialReflectionSource::backplane));

    ThemeRule glass{};
    glass.label = "compositor-lab.glass";
    glass.selector.role = PartRole::region;
    glass.declarations.push_back(RuleDeclaration::primitive("glass"));
    definition.rules.rules.push_back(glass);
    ThemeRule face{};
    face.label = "compositor-lab.face";
    face.selector.role = PartRole::face;
    face.declarations.push_back(RuleDeclaration::primitive("material"));
    definition.rules.rules.push_back(face);

    definition.window_backdrop.bases.push_back(
        ThemeBackdropBase{WindowBackdrop::window_material, solid_material(window_material_tint)});
    definition.window_backdrop.bases.push_back(
        ThemeBackdropBase{WindowBackdrop::transient_material, solid_material(transient_material_tint)});
    definition.window_backdrop.bases.push_back(ThemeBackdropBase{WindowBackdrop::transparent, solid_material(cleared)});
    std::shared_ptr<const Theme> theme = Theme::create(definition);
    return theme;
}

std::string grant_record(const Window& window, const WindowBackdropGrant& grant) {
    std::string record = "{\"lab\":\"compositor\",";
    const HostServices* const services = window.host_services();
    if (services != nullptr) {
        record += "\"capabilities\":";
        record += (*services).snapshot().capabilities.to_json();
        record += ',';
    }
    record += "\"state\":";
    record += window.compositor_state().to_json();
    record += ",\"request\":[";
    const WindowBackdropRequest& request = window.backdrop_request();
    for (std::size_t index = 0U; index < request.length && index < WindowBackdropRequest::maximum_entries; ++index) {
        if (index > 0U) record += ',';
        record += '"';
        record += window_backdrop_name(request.entries[index]);
        record += '"';
    }
    record += "],";
    append_json_string(record, "granted", window_backdrop_name(grant.granted));
    record += ',';
    append_json_string(record, "reason", window_backdrop_reason_name(grant.reason));
    record += ",\"generation\":";
    record += std::to_string(grant.generation.value);
    record += '}';
    return record;
}

void record_grant_in_force(Window& window) {
    // The lab's root is a LabRoot; any other window records nothing.
    LabRoot* const root = dynamic_cast<LabRoot*>(window.root().get());
    if (root == nullptr) return;
    (*root).record(window.backdrop_grant());
}

std::unique_ptr<Window> make_compositor_lab(const WindowBackdropRequest& request, bool print_records) {
    const std::shared_ptr<LabRoot> root = make_control<LabRoot>(StableId("compositor-lab.root"), print_records);

    const std::shared_ptr<Scenery> scenery = make_control<Scenery>(StableId("compositor-lab.scenery"));
    (*scenery).set_requested_bounds(Rect{0.0, 0.0, scenery_width, lab_size.height});
    (*root).add_child(scenery);

    const std::shared_ptr<Pane> pane = make_control<Pane>(StableId("compositor-lab.pane"));
    (*pane).set_requested_bounds(Rect{0.0, 0.0, lab_size.width, lab_size.height});
    (*root).add_child(pane);

    (*pane).add_child(caption("compositor-lab.glass.caption", "Glass over the backplane",
                              Rect{glass_bounds.x + 16.0, glass_bounds.y + 12.0, 240.0, 22.0}));
    (*pane).add_child(caption("compositor-lab.backplane-mirror.caption", "Reflects the backplane",
                              Rect{backplane_mirror_bounds.x + 12.0, backplane_mirror_bounds.y + 9.0, 176.0, 22.0}));
    (*pane).add_child(caption("compositor-lab.face-mirror.caption", "Reflects its own face",
                              Rect{face_mirror_bounds.x + 12.0, face_mirror_bounds.y + 9.0, 176.0, 22.0}));
    const std::shared_ptr<Label> status = caption("compositor-lab.status", "requested, not yet granted", status_bounds);
    (*pane).add_child(status);
    (*root).attach_status(status);

    std::unique_ptr<Window> window = std::make_unique<Window>(root, lab_size);
    (*window).set_theme(compositor_lab_theme());
    const bool declared = (*window).declare_non_opaque();
    static_cast<void>(declared);
    // Before a host reports its compositor only `opaque` is supported, so
    // this grant is opaque until the host's report resolves the request
    // again. The status shows it without a record (no host is attached
    // yet); the subscription that follows records each later change.
    const WindowBackdropGrant grant = (*window).request_backdrop(request);
    (*root).show(grant);
    on((*window).backdrop_changed(), *root, &LabRoot::on_backdrop_changed);
    return window;
}

} // namespace gui_forms::compositor_lab
