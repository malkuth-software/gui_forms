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
    GalleryWindow& gallery = *window;
    UpdateScope scope = gallery.begin_update();
    const GalleryState& state = model.state();

    if (cause == "gallery.command.reset") {
        drop_status.clear();
        invalidate_subtrees(styled_controls());
        (*gallery.gallery_checkbox_indeterminate).check_state = CheckState::indeterminate;
        (*gallery.gallery_link).visited = false;
    }
    // The check box, the radio buttons, the slider, the progress bar and the
    // diagnostics' visibility follow the model through their bindings.
    const BasicControlStyle style = gallery_basic_style(state.style_mode == StyleMode::quiet_relief);
    apply_relief_style(styled_controls(), style);
    // A text box binds a text only to a BindingSource field (FD-6), not to a
    // string the model keeps.
    if ((*gallery.gallery_text_input).text.read() != state.text) {
        (*gallery.gallery_text_input).text = state.text;
    }
    (*gallery.gallery_instrument).set_value(state.instrument_value);
    const std::optional<std::size_t> row = collection_row_index(state.selected_collection_row);
    if (row && (*gallery.gallery_collection).selected_index() != row) {
        (*gallery.gallery_collection).select_index(*row);
    }

    (*gallery.gallery_command_status).text =
        drop_status.empty() ? "Portable core · Host 0.4 · reusable controls" : drop_status;
    (*gallery.gallery_value_label).text = format_percent(state.progress_value);
    // Diagnostics are live instrumentation, not a snapshot captured when the
    // panel opens: every retained interaction is observable on the next frame.
    const MetricsSnapshot metrics = gallery.metrics_snapshot();
    for (const Control::Ptr& child : (*gallery.gallery_diagnostics).children()) {
        const std::shared_ptr<Label> label = std::dynamic_pointer_cast<Label>(child);
        if (!label) continue;
        const std::string metric = metric_text((*label).stable_id().value(), metrics);
        if (!metric.empty()) (*label).text = metric;
    }
}

void GalleryContext::show_window_theme(std::shared_ptr<const Theme> theme)
{
    if (window == nullptr) return;
    (*window).set_theme_override(std::move(theme));
}

std::vector<Control::Ptr> GalleryContext::styled_controls() const
{
    std::vector<Control::Ptr> controls;
    if (window == nullptr) return controls;
    const GalleryWindow& gallery = *window;
    controls = {gallery.gallery_command_strip,    gallery.gallery_backplane,
                gallery.gallery_values_group,     gallery.gallery_collection_group,
                gallery.gallery_instrument_group, gallery.gallery_diagnostics};
    return controls;
}

// ---- Rule actions -------------------------------------------------------------

void GalleryContext::activate(std::string_view stable_id)
{
    if (model.activate(stable_id)) synchronize(stable_id);
}

void GalleryContext::reset_clicked(GalleryContext& context, const RuleFiring&)
{
    context.activate("gallery.command.reset");
}

void GalleryContext::diagnostics_clicked(GalleryContext& context, const RuleFiring&)
{
    context.activate("gallery.command.diagnostics");
}

void GalleryContext::apply_clicked(GalleryContext& context, const RuleFiring&)
{
    context.activate("gallery.default-button");
}

// The check box and the radio buttons write the model's Values through
// their bindings. Each action writes the control's state again (an equal
// write does nothing), so the model holds it whichever runs first, then
// counts the activation and brings the rest up to date.
void GalleryContext::precise_updates_clicked(GalleryContext& context, const RuleFiring&)
{
    context.model.precise_updates.set((*(*context.window).gallery_checkbox).checked.read());
    context.model.note_activation();
    context.synchronize("gallery.checkbox");
}

void GalleryContext::classic_relief_clicked(GalleryContext& context, const RuleFiring&)
{
    context.model.classic_relief.set(true);
    context.model.note_activation();
    context.synchronize("gallery.radio.classic");
}

void GalleryContext::quiet_relief_clicked(GalleryContext& context, const RuleFiring&)
{
    context.model.quiet_relief.set(true);
    context.model.note_activation();
    context.synchronize("gallery.radio.quiet");
}

void GalleryContext::three_state_clicked(GalleryContext& context, const RuleFiring&)
{
    const CheckState check_state = (*(*context.window).gallery_checkbox_indeterminate).check_state.read();
    const char* state = "unchecked";
    if (check_state == CheckState::checked) {
        state = "checked";
    } else if (check_state == CheckState::indeterminate) {
        state = "indeterminate";
    }
    context.drop_status = std::string("Three-state option · ") + state;
    context.synchronize("gallery.checkbox.indeterminate");
}

void GalleryContext::contract_link_clicked(GalleryContext& context, const RuleFiring&)
{
    context.drop_status = "Reusable LinkLabel · retained activation · no external navigation";
    context.synchronize("gallery.link");
}

void GalleryContext::text_changed(GalleryContext& context, const RuleFiring&)
{
    const std::string text((*(*context.window).gallery_text_input).text.read());
    if (context.model.replace_text(text)) context.synchronize("gallery.text-input");
}

// The track bar writes model.slider through its binding; the progress bar
// follows the model. The label and the instrument are brought up to date.
void GalleryContext::slider_changed(GalleryContext& context, const RuleFiring&)
{
    context.synchronize("gallery.slider");
}

void GalleryContext::collection_selection_changed(GalleryContext& context, const RuleFiring&)
{
    // The collection selects one row at a time: its selected row is the
    // one the change made active.
    const std::optional<std::size_t> selected = (*(*context.window).gallery_collection).selected_index();
    if (!selected || *selected >= collection_row_ids.size()) return;
    const std::string_view row_id = collection_row_ids[*selected];
    // synchronize() selecting the model's row is no activation.
    if (context.model.state().selected_collection_row == row_id) return;
    context.activate(row_id);
}

void GalleryContext::collection_dropped(const std::string& summary)
{
    drop_status = summary;
    synchronize("gallery.command.status");
}

// ---- Attachment ---------------------------------------------------------------

namespace {

// Arms `rule_action` on `trigger` in `rules`; a refusal is a defect of the
// Gallery's own wiring.
void arm(RuleSet& rules, const RuleTrigger& trigger, GalleryContext& context,
         void (*rule_action)(GalleryContext&, const RuleFiring&))
{
    const RuleAddResult added = on(rules, trigger, context, rule_action);
    if (added.refusal != RuleRefusal::none) {
        throw std::logic_error("a Gallery rule was refused: " + std::string(rule_refusal_name(added.refusal)));
    }
}

} // namespace

void GalleryContext::attach(GalleryWindow& gallery_window)
{
    window = &gallery_window;
    GalleryWindow& gallery = gallery_window;

    // What the form language does not state yet.
    (*gallery.gallery_default_button).set_default_button(true);
    (*gallery.gallery_radio_classic).group_name = "gallery.style-mode";
    (*gallery.gallery_radio_quiet).group_name = "gallery.style-mode";
    (*gallery.gallery_slider).small_change = 1.0;
    (*gallery.gallery_slider).large_change = 10.0;
    (*gallery.gallery_text_input).placeholder_text = "Type here";
    (*gallery.gallery_diagnostics_about).text_wrapping = TextWrapping::word;
    (*gallery.gallery_diagnostics_about).vertical_alignment = VerticalAlignment::near;
    (*gallery.gallery_diagnostics_font).text_wrapping = TextWrapping::word;
    (*gallery.gallery_diagnostics_font).vertical_alignment = VerticalAlignment::near;
    (*gallery.gallery_diagnostics).set_paint_plane(PaintPlane::overlay);
    std::vector<std::string> row_ids;
    row_ids.reserve(collection_row_ids.size());
    for (const std::string_view row_id : collection_row_ids) row_ids.emplace_back(row_id);
    (*gallery.gallery_collection).set_item_stable_ids(std::move(row_ids));
    (*gallery.gallery_collection).select_index(0U);
    SurfaceMaterial backplane;
    backplane.fills = {MaterialFillLayer::solid(backplane_wash)};
    (*gallery.gallery_backplane).material = std::move(backplane);
    collection_drop_ = (*gallery.gallery_collection).dropped().subscribe(
        Delegate<const std::string&>::bind<GalleryContext, &GalleryContext::collection_dropped>(*this));

    RuleSet& rules = gallery.rules();
    Button& reset = *gallery.gallery_command_reset;
    Button& diagnostics = *gallery.gallery_command_diagnostics;
    Button& apply = *gallery.gallery_default_button;
    CheckBox& precise = *gallery.gallery_checkbox;
    RadioButton& classic = *gallery.gallery_radio_classic;
    RadioButton& quiet = *gallery.gallery_radio_quiet;
    CheckBox& three_state = *gallery.gallery_checkbox_indeterminate;
    LinkLabel& link = *gallery.gallery_link;
    TextBox& text_input = *gallery.gallery_text_input;
    CollectionList& collection = *gallery.gallery_collection;
    // ButtonBase::clicked covers the pointer and the keyboard (Space,
    // Enter, mnemonics), which Control's mouse Click does not.
    arm(rules, trigger::interaction(reset, reset.clicked()), *this, &GalleryContext::reset_clicked);
    arm(rules, trigger::interaction(diagnostics, diagnostics.clicked()), *this,
        &GalleryContext::diagnostics_clicked);
    arm(rules, trigger::interaction(text_input, text_input.text_changed()), *this, &GalleryContext::text_changed);
    arm(rules, trigger::interaction(apply, apply.clicked()), *this, &GalleryContext::apply_clicked);
    arm(rules, trigger::interaction(precise, precise.clicked()), *this, &GalleryContext::precise_updates_clicked);
    arm(rules, trigger::interaction(classic, classic.clicked()), *this, &GalleryContext::classic_relief_clicked);
    arm(rules, trigger::interaction(quiet, quiet.clicked()), *this, &GalleryContext::quiet_relief_clicked);
    arm(rules, trigger::interaction(three_state, three_state.clicked()), *this,
        &GalleryContext::three_state_clicked);
    arm(rules, trigger::interaction(link, link.clicked()), *this, &GalleryContext::contract_link_clicked);
    arm(rules, trigger::interaction(collection, collection.selection_changed()), *this,
        &GalleryContext::collection_selection_changed);
    // The track bar writes model.slider through its binding, and the
    // model's own writes (set_slider_value, Reset) reach the same Value: the
    // rule follows the Value, in its notification.
    arm(rules, trigger::value_changed(model.slider), *this, &GalleryContext::slider_changed);
}

} // namespace gui_forms::gallery
