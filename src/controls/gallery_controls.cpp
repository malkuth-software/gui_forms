#include "gallery_controls.hpp"

#include "gallery.hpp"
#include "gallery_catalog.hpp"
#include "gallery_page_specs.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <utility>

#ifndef GUI_FORMS_VERSION_STRING
// CMake defines this from project(GUIForms VERSION ...); a bare syntax check
// or an out-of-tree consumer still gets an honest label.
#define GUI_FORMS_VERSION_STRING "unversioned"
#endif

namespace gui_forms::gallery {
namespace {

// Where the shell puts each direct child of `gallery.root`, in document
// order: the command strip and the diagnostics overlay belong to no page.
struct MountPlacement final {
    std::string_view id{};
    std::string_view page{};
    Size size{};
};

constexpr std::array<MountPlacement, 6> mount_placements{{
    {"gallery.command-strip", "", {1280.0, 34.0}},
    {"gallery.backplane", "basics", {660.0, 200.0}},
    {"gallery.values-group", "values", {660.0, 116.0}},
    {"gallery.collection-group", "retained-rows", {660.0, 144.0}},
    {"gallery.instrument-group", "instrument", {660.0, 150.0}},
    {"gallery.diagnostics", "", {280.0, 742.0}},
}};

// The collection's rows, by item index: their stable IDs are the model's
// row identities.
constexpr std::array<std::string_view, 4> collection_row_ids{
    "gallery.collection.alpha", "gallery.collection.beta",
    "gallery.collection.gamma", "gallery.collection.delta"};

constexpr Color backplane_wash = Color::rgba(222, 235, 247);

[[nodiscard]] BasicControlStyle gallery_basic_style(bool quiet) noexcept
{
    BasicControlStyle style;
    if (quiet) {
        style.face = style.face_light;
        style.dark_border = style.border;
    }
    return style;
}

[[nodiscard]] std::string metric_text(std::string_view id, const MetricsSnapshot& m)
{
    char buffer[256]{};
    if (id == "gallery.diagnostics.about") {
        std::snprintf(buffer, sizeof(buffer),
                      "%s %s · %zu pages · %zu registry records · renderer: %s",
                      std::string(gallery_program_name).c_str(), GUI_FORMS_VERSION_STRING,
                      gallery_pages.size(), control_catalog.size(), m.renderer_name.c_str());
    } else if (id == "gallery.diagnostics.renderer") {
        std::snprintf(buffer, sizeof(buffer), "%s", m.renderer_name.c_str());
    } else if (id == "gallery.diagnostics.controls") {
        std::snprintf(buffer, sizeof(buffer), "controls %llu / IDs %llu / cache %llu",
                      static_cast<unsigned long long>(m.control_count),
                      static_cast<unsigned long long>(m.stable_id_count),
                      static_cast<unsigned long long>(m.display_cache_entries));
    } else if (id == "gallery.diagnostics.layout") {
        std::snprintf(buffer, sizeof(buffer), "measure %llu / arrange %llu",
                      static_cast<unsigned long long>(m.measure_passes),
                      static_cast<unsigned long long>(m.arrange_passes));
    } else if (id == "gallery.diagnostics.paint") {
        std::snprintf(buffer, sizeof(buffer), "paint %llu / chunks %llu new, %llu reused",
                      static_cast<unsigned long long>(m.paint_passes),
                      static_cast<unsigned long long>(m.display_chunks_rebuilt),
                      static_cast<unsigned long long>(m.display_chunks_reused));
    } else if (id == "gallery.diagnostics.input") {
        std::snprintf(buffer, sizeof(buffer), "input %llu / focus %llu / activate %llu",
                      static_cast<unsigned long long>(m.input_events),
                      static_cast<unsigned long long>(m.focus_transitions),
                      static_cast<unsigned long long>(m.activations));
    } else if (id == "gallery.diagnostics.flush") {
        std::snprintf(buffer, sizeof(buffer), "wake %llu / deadlines %llu / motion %llu",
                      static_cast<unsigned long long>(m.scheduler_wakes),
                      static_cast<unsigned long long>(m.frame_deadlines_fired),
                      static_cast<unsigned long long>(m.motion_clock_ticks));
    } else if (id == "gallery.diagnostics.present") {
        std::snprintf(buffer, sizeof(buffer), "present %llu / worst %.3f ms",
                      static_cast<unsigned long long>(m.frames_presented),
                      static_cast<double>(m.worst_present_duration_nanoseconds) / 1'000'000.0);
    } else {
        return {};
    }
    const std::string text(buffer);
    return text;
}

// Applies the relief style to every button and range control in the
// subtrees under `roots`. Walks with an explicit stack; the document is at
// most four levels deep.
void apply_relief_style(const std::vector<Control::Ptr>& roots, const BasicControlStyle& style)
{
    std::vector<Control::Ptr> pending(roots.begin(), roots.end());
    while (!pending.empty()) {
        const Control::Ptr control = std::move(pending.back());
        pending.pop_back();
        if (const std::shared_ptr<ButtonBase> button = std::dynamic_pointer_cast<ButtonBase>(control)) {
            (*button).set_style(style);
        } else if (const std::shared_ptr<RangeControl> range =
                       std::dynamic_pointer_cast<RangeControl>(control)) {
            (*range).set_style(style);
        }
        for (const Control::Ptr& child : (*control).children()) pending.push_back(child);
    }
}

// Invalidates the paint and semantics of every control under `roots`.
void invalidate_subtrees(const std::vector<Control::Ptr>& roots)
{
    std::vector<Control::Ptr> pending(roots.begin(), roots.end());
    while (!pending.empty()) {
        const Control::Ptr control = std::move(pending.back());
        pending.pop_back();
        (*control).invalidate(Dirty::paint | Dirty::semantics);
        for (const Control::Ptr& child : (*control).children()) pending.push_back(child);
    }
}

[[nodiscard]] std::optional<std::size_t> collection_row_index(std::string_view row_id) noexcept
{
    for (std::size_t index = 0; index < collection_row_ids.size(); ++index) {
        if (collection_row_ids[index] == row_id) return index;
    }
    return std::nullopt;
}

} // namespace

// ---- Synchronization ----------------------------------------------------------

void GalleryContext::synchronize(std::string_view cause)
{
    if (window == nullptr) {
        return;
    }
    UpdateScope scope = (*window).begin_update();
    const GalleryState& state = model.state();

    if (cause == "gallery.command.reset") {
        drop_status.clear();
        invalidate_subtrees(mount_controls());
    }
    (*document.gallery_diagnostics).visible = state.diagnostics_visible;

    const BasicControlStyle style = gallery_basic_style(state.style_mode == StyleMode::quiet_relief);
    apply_relief_style(mount_controls(), style);

    (*document.gallery_checkbox).set_checked(state.precise_updates);
    (*document.gallery_radio_classic).set_checked(state.style_mode == StyleMode::classic_relief);
    (*document.gallery_radio_quiet).set_checked(state.style_mode == StyleMode::quiet_relief);
    if (cause == "gallery.command.reset") {
        (*document.gallery_checkbox_indeterminate).set_check_state(CheckState::indeterminate);
        (*document.gallery_link).set_visited(false);
    }
    if ((*document.gallery_text_input).text.read() != state.text) {
        (*document.gallery_text_input).text = state.text;
    }
    (*document.gallery_slider).set_value(state.slider_value);
    (*document.gallery_progress).set_value(state.progress_value);
    (*document.gallery_instrument).set_value(state.instrument_value);
    const std::optional<std::size_t> row = collection_row_index(state.selected_collection_row);
    if (row && (*document.gallery_collection).selected_index() != row) {
        (*document.gallery_collection).select_index(*row);
    }

    (*document.gallery_command_status)
        .set_text(drop_status.empty() ? "Portable core · Host 0.4 · reusable controls" : drop_status);
    (*document.gallery_value_label).set_text(format_percent(state.progress_value));
    // Diagnostics are live instrumentation, not a snapshot captured when the
    // panel opens: every retained interaction is observable on the next frame.
    const MetricsSnapshot metrics = (*window).metrics_snapshot();
    for (const Control::Ptr& child : (*document.gallery_diagnostics).children()) {
        const std::shared_ptr<Label> label = std::dynamic_pointer_cast<Label>(child);
        if (!label) continue;
        const std::string metric = metric_text((*label).stable_id().value(), metrics);
        if (!metric.empty()) (*label).set_text(metric);
    }
}

std::vector<Control::Ptr> GalleryContext::mount_controls() const
{
    std::vector<Control::Ptr> mounts{
        document.gallery_command_strip,    document.gallery_backplane,
        document.gallery_values_group,     document.gallery_collection_group,
        document.gallery_instrument_group, document.gallery_diagnostics};
    return mounts;
}

// ---- Handlers -----------------------------------------------------------------

void GalleryContext::activate(std::string_view stable_id)
{
    if (model.activate(stable_id)) synchronize(stable_id);
}

void GalleryContext::reset_clicked(ButtonBase&)
{
    activate("gallery.command.reset");
}

void GalleryContext::diagnostics_clicked(ButtonBase&)
{
    activate("gallery.command.diagnostics");
}

void GalleryContext::apply_clicked(ButtonBase&)
{
    activate("gallery.default-button");
}

void GalleryContext::precise_updates_clicked(ButtonBase&)
{
    // The check box has already toggled; the model follows it.
    if ((*document.gallery_checkbox).checked() != model.state().precise_updates) {
        static_cast<void>(model.activate("gallery.checkbox"));
    }
    synchronize("gallery.checkbox");
}

void GalleryContext::classic_relief_clicked(ButtonBase&)
{
    static_cast<void>(model.activate("gallery.radio.classic"));
    synchronize("gallery.radio.classic");
}

void GalleryContext::quiet_relief_clicked(ButtonBase&)
{
    static_cast<void>(model.activate("gallery.radio.quiet"));
    synchronize("gallery.radio.quiet");
}

void GalleryContext::three_state_clicked(ButtonBase&)
{
    const CheckState check_state = (*document.gallery_checkbox_indeterminate).check_state();
    const char* state = "unchecked";
    if (check_state == CheckState::checked) {
        state = "checked";
    } else if (check_state == CheckState::indeterminate) {
        state = "indeterminate";
    }
    drop_status = std::string("Three-state option · ") + state;
    synchronize("gallery.checkbox.indeterminate");
}

void GalleryContext::contract_link_clicked(ButtonBase&)
{
    drop_status = "Reusable LinkLabel · retained activation · no external navigation";
    synchronize("gallery.link");
}

void GalleryContext::text_changed(const std::string& text)
{
    if (model.replace_text(text)) synchronize("gallery.text-input");
}

void GalleryContext::slider_changed(double value)
{
    if (model.set_slider_value(value)) synchronize("gallery.slider");
}

void GalleryContext::collection_selection_changed(const ListSelectionChange& change)
{
    if (!change.active_index || *change.active_index >= collection_row_ids.size()) return;
    const std::string_view row_id = collection_row_ids[*change.active_index];
    // synchronize() selecting the model's row is no activation.
    if (model.state().selected_collection_row == row_id) return;
    activate(row_id);
}

void GalleryContext::collection_dropped(const std::string& summary)
{
    drop_status = summary;
    synchronize("gallery.command.status");
}

// ---- Construction -------------------------------------------------------------

Control::Ptr GalleryTree::mount(std::string_view id) const
{
    for (const GalleryMount& candidate : mounts) {
        if (candidate.id == id) {
            return candidate.control;
        }
    }
    return nullptr;
}

GalleryTree build_gallery_tree()
{
    std::shared_ptr<GalleryContext> context = std::make_shared<GalleryContext>();
    GalleryDocument<GalleryContext>& document = (*context).document;
    document.initialize_component(*context);

    // What the form language does not state yet.
    (*document.gallery_default_button).set_default_button(true);
    (*document.gallery_checkbox_indeterminate).set_three_state(true);
    (*document.gallery_checkbox_indeterminate).set_check_state(CheckState::indeterminate);
    (*document.gallery_radio_classic).set_group_name("gallery.style-mode");
    (*document.gallery_radio_quiet).set_group_name("gallery.style-mode");
    (*document.gallery_slider).set_small_change(1.0);
    (*document.gallery_slider).set_large_change(10.0);
    (*document.gallery_text_input).placeholder_text = "Type here";
    (*document.gallery_diagnostics_about).set_text_wrapping(TextWrapping::word);
    (*document.gallery_diagnostics_about).set_vertical_alignment(VerticalAlignment::near);
    (*document.gallery_diagnostics_font).set_text_wrapping(TextWrapping::word);
    (*document.gallery_diagnostics_font).set_vertical_alignment(VerticalAlignment::near);
    (*document.gallery_diagnostics).set_paint_plane(PaintPlane::overlay);
    std::vector<std::string> row_ids;
    row_ids.reserve(collection_row_ids.size());
    for (const std::string_view row_id : collection_row_ids) row_ids.emplace_back(row_id);
    (*document.gallery_collection).set_item_stable_ids(std::move(row_ids));
    (*document.gallery_collection).select_index(0U);
    SurfaceMaterial backplane;
    backplane.fills = {MaterialFillLayer::solid(backplane_wash)};
    (*document.gallery_backplane).set_material(std::move(backplane));
    (*context).collection_drop_ = (*document.gallery_collection).dropped().subscribe(
        Delegate<const std::string&>::bind<GalleryContext, &GalleryContext::collection_dropped>(*context));

    // gallery.root is the declared boundary; the shell places its children.
    GalleryTree tree;
    for (const MountPlacement& placement : mount_placements) {
        Control::Ptr control;
        for (const Control::Ptr& child : (*document.gallery_root).children()) {
            if ((*child).stable_id().value() == placement.id) control = child;
        }
        if (!control) throw std::logic_error("gallery.form lacks a Gallery mount");
        const Control::Ptr detached = (*document.gallery_root).remove_child((*control).runtime_id());
        tree.mounts.push_back({placement.id, placement.page, placement.size, detached});
    }
    tree.context = std::move(context);
    return tree;
}

} // namespace gui_forms::gallery
