#include "gallery_controls.hpp"

#include "gallery.hpp"
#include "gallery_catalog.hpp"
#include "gallery_page_specs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#ifndef GUI_FORMS_VERSION_STRING
// CMake defines this from project(GUIForms VERSION ...); a bare syntax check
// or an out-of-tree consumer still gets an honest label.
#define GUI_FORMS_VERSION_STRING "unversioned"
#endif

namespace gui_forms::gallery {
namespace {

using dml::LayoutKind;
using dml::NodeKind;

// Demo-private candidate palette: Windows 7/10 professional structure, the
// Modern.Forms white work canvas, and the parent program's precise material
// grammar. These are not frozen M9 theme tokens.
constexpr Color face = Color::rgba(229, 234, 239);
constexpr Color face_light = Color::rgba(247, 249, 251);
constexpr Color paper = Color::rgba(255, 255, 255);
constexpr Color highlight = Color::rgba(255, 255, 255);
constexpr Color shadow = Color::rgba(148, 162, 175);
constexpr Color deep_shadow = Color::rgba(76, 94, 111);
constexpr Color ink = Color::rgba(27, 39, 51);
constexpr Color disabled_ink = Color::rgba(132, 143, 153);
constexpr Color accent = Color::rgba(38, 114, 185);
constexpr Color accent_deep = Color::rgba(25, 82, 139);
constexpr Color selection = Color::rgba(43, 120, 197);
constexpr Color instrument_face = Color::rgba(13, 24, 36);
constexpr Color cyan = Color::rgba(89, 216, 230);
constexpr Color amber = Color::rgba(255, 210, 122);
constexpr Color command_rule = Color::rgba(174, 193, 209);

constexpr double group_caption_height = 22.0;
constexpr double gap = 6.0;

[[nodiscard]] BasicControlStyle gallery_basic_style(bool quiet) noexcept
{
    BasicControlStyle style;
    if (quiet) {
        style.face = style.face_light;
        style.dark_border = style.border;
    }
    return style;
}

[[nodiscard]] bool same_rect(Rect left, Rect right) noexcept
{
    return left.x == right.x && left.y == right.y && left.width == right.width &&
           left.height == right.height;
}

void request_bounds(const Control::Ptr& child, Rect bounds)
{
    if (!same_rect((*child).requested_bounds(), bounds)) {
        (*child).set_requested_bounds(bounds);
    }
}

void sunken_frame(Painter& painter, Rect rect, Color fill)
{
    painter.fill_rect(rect, fill);
    painter.draw_line({rect.x, rect.y}, {rect.x + rect.width, rect.y}, deep_shadow, 1.0);
    painter.draw_line({rect.x, rect.y}, {rect.x, rect.y + rect.height}, deep_shadow, 1.0);
    painter.draw_line({rect.x, rect.y + rect.height - 1.0},
                      {rect.x + rect.width, rect.y + rect.height - 1.0}, highlight, 1.0);
    painter.draw_line({rect.x + rect.width - 1.0, rect.y},
                      {rect.x + rect.width - 1.0, rect.y + rect.height}, highlight, 1.0);
}

[[nodiscard]] bool is_container(NodeKind kind) noexcept
{
    return kind == NodeKind::form || kind == NodeKind::command_strip ||
           kind == NodeKind::panel || kind == NodeKind::user_control ||
           kind == NodeKind::backplane ||
           kind == NodeKind::group || kind == NodeKind::list ||
           kind == NodeKind::diagnostics;
}

[[nodiscard]] PaintPlane paint_plane_for(const dml::NodeSpec& specification) noexcept
{
    if (specification.id.starts_with("gallery.diagnostics")) {
        return PaintPlane::overlay;
    }
    switch (specification.kind) {
    case NodeKind::form:
    case NodeKind::command_strip:
    case NodeKind::panel:
    case NodeKind::user_control:
    case NodeKind::backplane:
    case NodeKind::group:
    case NodeKind::list:
        return PaintPlane::backplane;
    case NodeKind::label:
    case NodeKind::link_label:
    case NodeKind::button:
    case NodeKind::text_input:
    case NodeKind::check_box:
    case NodeKind::radio_button:
    case NodeKind::slider:
    case NodeKind::progress:
    case NodeKind::row:
    case NodeKind::instrument:
    case NodeKind::diagnostics:
        return PaintPlane::control;
    }
    // A kind outside the enumeration paints with the controls.
    return PaintPlane::control;
}

[[nodiscard]] Rect content_rect(NodeKind kind, Size size) noexcept
{
    if (kind == NodeKind::group || kind == NodeKind::diagnostics) {
        return {10.0, group_caption_height, std::max(0.0, size.width - 20.0),
                std::max(0.0, size.height - group_caption_height - 10.0)};
    }
    if (kind == NodeKind::command_strip) {
        return {6.0, 5.0, std::max(0.0, size.width - 12.0),
                std::max(0.0, size.height - 10.0)};
    }
    if (kind == NodeKind::backplane) {
        return {16.0, 14.0, std::max(0.0, size.width - 32.0),
                std::max(0.0, size.height - 28.0)};
    }
    if (kind == NodeKind::list) {
        return {2.0, 2.0, std::max(0.0, size.width - 4.0),
                std::max(0.0, size.height - 4.0)};
    }
    return {0.0, 0.0, size.width, size.height};
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
        std::snprintf(buffer, sizeof(buffer), "wake %llu / ticks %llu / active %llu",
                      static_cast<unsigned long long>(m.scheduler_wakes),
                      static_cast<unsigned long long>(m.active_surface_ticks),
                      static_cast<unsigned long long>(m.active_surface_count));
    } else if (id == "gallery.diagnostics.present") {
        std::snprintf(buffer, sizeof(buffer), "present %llu / worst %.3f ms",
                      static_cast<unsigned long long>(m.frames_presented),
                      static_cast<double>(m.worst_present_duration_nanoseconds) / 1'000'000.0);
    } else {
        return {};
    }
    return buffer;
}

void invalidate_gallery_control(Window& window, std::string_view id)
{
    if (const Control::Ptr control = window.find(id)) {
        (*control).invalidate(Dirty::paint | Dirty::semantics);
    }
}

void invalidate_gallery_controls(
    Window& window,
    std::initializer_list<std::string_view> identifiers)
{
    for (const std::string_view id : identifiers) {
        invalidate_gallery_control(window, id);
    }
}

void update_gallery_label(Window& window, std::string_view id, std::string text)
{
    if (std::shared_ptr<gui_forms::Label> label =
            std::dynamic_pointer_cast<Label>(window.find(id))) {
        (*label).set_text(std::move(text));
    }
}

class DropDataCounter final {
public:
    DropDataCounter(std::size_t& files,
                    std::size_t& text_items,
                    std::size_t& binary_items) noexcept
        : files_(&files), text_items_(&text_items), binary_items_(&binary_items) {}

    void operator()(const DragFileListData& data) const noexcept
    {
        *files_ += data.paths_utf8.size();
    }

    void operator()(const DragTextData&) const noexcept
    {
        ++*text_items_;
    }

    void operator()(const DragBinaryData&) const noexcept
    {
        ++*binary_items_;
    }

private:
    std::size_t* files_{};
    std::size_t* text_items_{};
    std::size_t* binary_items_{};
};

class GalleryButton final : public Button {
public:
    GalleryButton(StableId id, const dml::NodeSpec& specification,
                  std::shared_ptr<GalleryContext> context)
        : Button(std::move(id), std::string(specification.text)),
          id_(specification.id), context_(std::move(context))
    {
        set_enabled((specification.flags & dml::enabled) != 0U);
        set_visible((specification.flags & dml::visible) != 0U);
        set_default_button((specification.flags & dml::default_action) != 0U);
        click_ = clicked().subscribe(
            Delegate<ButtonBase&>::bind<GalleryButton,
                &GalleryButton::on_clicked>(*this));
    }

private:
    void on_clicked(ButtonBase&)
    {
        if ((*context_).model.activate(id_)) {
            (*context_).synchronize(id_);
        }
    }

    std::string id_;
    std::shared_ptr<GalleryContext> context_;
    SubscriptionToken click_;
};

class GalleryCheckBox final : public CheckBox {
public:
    GalleryCheckBox(StableId id, const dml::NodeSpec& specification,
                    std::shared_ptr<GalleryContext> context)
        : CheckBox(std::move(id), std::string(specification.text)),
          id_(specification.id), context_(std::move(context))
    {
        set_enabled((specification.flags & dml::enabled) != 0U);
        set_visible((specification.flags & dml::visible) != 0U);
        if (id_ == "gallery.checkbox") {
            set_checked((*context_).model.state().precise_updates);
        } else if (id_ == "gallery.checkbox.indeterminate") {
            set_three_state(true);
            set_check_state(CheckState::indeterminate);
        }
        click_ = clicked().subscribe(
            Delegate<ButtonBase&>::bind<GalleryCheckBox,
                &GalleryCheckBox::on_clicked>(*this));
    }

private:
    void on_clicked(ButtonBase&)
    {
        if (id_ == "gallery.checkbox") {
            static_cast<void>((*context_).model.activate(id_));
        } else {
            const char* state = check_state() == CheckState::checked
                ? "checked" : check_state() == CheckState::indeterminate
                    ? "indeterminate" : "unchecked";
            (*context_).drop_status = std::string("Three-state option · ") + state;
        }
        (*context_).synchronize(id_);
    }

    std::string id_;
    std::shared_ptr<GalleryContext> context_;
    SubscriptionToken click_;
};

class GalleryRadioButton final : public RadioButton {
public:
    GalleryRadioButton(StableId id, const dml::NodeSpec& specification,
                       std::shared_ptr<GalleryContext> context)
        : RadioButton(std::move(id), std::string(specification.text)),
          id_(specification.id), context_(std::move(context))
    {
        set_enabled((specification.flags & dml::enabled) != 0U);
        set_visible((specification.flags & dml::visible) != 0U);
        set_group_name("gallery.style-mode");
        set_checked(id_ == "gallery.radio.classic"
            ? (*context_).model.state().style_mode == StyleMode::classic_relief
            : (*context_).model.state().style_mode == StyleMode::quiet_relief);
        click_ = clicked().subscribe(
            Delegate<ButtonBase&>::bind<GalleryRadioButton,
                &GalleryRadioButton::on_clicked>(*this));
    }

private:
    void on_clicked(ButtonBase&)
    {
        static_cast<void>((*context_).model.activate(id_));
        (*context_).synchronize(id_);
    }

    std::string id_;
    std::shared_ptr<GalleryContext> context_;
    SubscriptionToken click_;
};

class GalleryLinkLabel final : public LinkLabel {
public:
    GalleryLinkLabel(StableId id, const dml::NodeSpec& specification,
                     std::shared_ptr<GalleryContext> context)
        : LinkLabel(std::move(id), std::string(specification.text)),
          context_(std::move(context))
    {
        click_ = clicked().subscribe(
            Delegate<ButtonBase&>::bind<GalleryLinkLabel,
                &GalleryLinkLabel::on_clicked>(*this));
    }

private:
    void on_clicked(ButtonBase&)
    {
        (*context_).drop_status =
            "Reusable LinkLabel · retained activation · no external navigation";
        (*context_).synchronize("gallery.link");
    }

    std::shared_ptr<GalleryContext> context_;
    SubscriptionToken click_;
};

class GalleryTrackBar final : public TrackBar {
public:
    GalleryTrackBar(StableId id, const dml::NodeSpec& specification,
                    std::shared_ptr<GalleryContext> context)
        : TrackBar(std::move(id)), id_(specification.id), context_(std::move(context))
    {
        set_enabled((specification.flags & dml::enabled) != 0U);
        set_visible((specification.flags & dml::visible) != 0U);
        set_range(specification.minimum, specification.maximum);
        set_value((*context_).model.state().slider_value);
        set_small_change(1.0);
        set_large_change(10.0);
        value_changed_ = value_changed().subscribe(
            Delegate<double>::bind<GalleryTrackBar,
                &GalleryTrackBar::on_value_changed>(*this));
    }

private:
    void on_value_changed(double next)
    {
        if ((*context_).model.set_slider_value(next)) {
            (*context_).synchronize(id_);
        }
    }

    std::string id_;
    std::shared_ptr<GalleryContext> context_;
    SubscriptionToken value_changed_;
};

class GalleryStatusLabel final : public Label {
public:
    GalleryStatusLabel(StableId id, std::string text,
                       std::shared_ptr<GalleryContext> context)
        : Label(std::move(id), std::move(text)), context_(std::move(context))
    {
        set_font({FontRole::control, 12.0, 400, false});
    }

    void on_paint(Painter& painter, Rect) override
    {
        painter.draw_image((*context_).status_badge, {2.0, 3.0, 16.0, 16.0}, 1.0);
        painter.draw_text_utf8({24.0, 15.0}, text(), font(),
                               enabled() ? ink : disabled_ink);
    }

private:
    std::shared_ptr<GalleryContext> context_;
};

class GalleryFontPolicyLabel final : public Label {
public:
    explicit GalleryFontPolicyLabel(StableId id)
        : Label(std::move(id)) {}

    void on_paint(Painter& painter, Rect) override
    {
        painter.draw_text_utf8({2.0, 14.0}, "Controls: Portsmouth Rapids 1.0",
                               {FontRole::content, 10.0, 400, false}, ink);
        painter.draw_text_utf8({2.0, 30.0}, "Fields: Carlito body specimen",
                               {FontRole::content, 10.0, 400, false}, disabled_ink);
    }
};

class GalleryLifecycleCard final : public UserControl {
public:
    explicit GalleryLifecycleCard(StableId id)
        : UserControl(std::move(id))
    {
        initialized_ = initialization_completed().subscribe(
            Delegate<Dirty, bool>::bind<GalleryLifecycleCard,
                &GalleryLifecycleCard::on_initialization_completed>(*this));
        load_ = loaded().subscribe(
            Delegate<>::bind<GalleryLifecycleCard,
                &GalleryLifecycleCard::on_loaded>(*this));
    }

    void arrange(Rect final_bounds) override
    {
        for (const Control::Ptr& child : children()) {
            if ((*child).stable_id().value() == "gallery.lifecycle-title") {
                request_bounds(child, {9.0, 2.0, 82.0, 20.0});
            } else if ((*child).stable_id().value() == "gallery.lifecycle-status") {
                request_bounds(child, {95.0, 2.0,
                                       std::max(0.0, final_bounds.width - 101.0), 20.0});
            }
        }
        UserControl::arrange(final_bounds);
    }

    void on_paint(Painter& painter, Rect) override
    {
        const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                          committed_arranged_bounds().height};
        painter.fill_rect(bounds, Color::rgba(247, 249, 251, 238));
        painter.fill_rect({0.0, 0.0, 4.0, bounds.height}, accent);
        painter.stroke_rect({0.5, 0.5, std::max(0.0, bounds.width - 1.0),
                             std::max(0.0, bounds.height - 1.0)}, shadow, 1.0);
    }

protected:
    void on_attached_to_window() override
    {
        UserControl::on_attached_to_window();
        for (const Control::Ptr& child : children()) {
            if ((*child).stable_id().value() == "gallery.lifecycle-status") {
                if (std::shared_ptr<gui_forms::Label> status = std::dynamic_pointer_cast<Label>(child)) {
                    char value[96]{};
                    std::snprintf(value, sizeof(value), "Load 1 · attach %llu · init %llu",
                                  static_cast<unsigned long long>(attachment_count() + 1),
                                  static_cast<unsigned long long>(initialization_batches_));
                    (*status).set_text(value);
                }
            }
        }
    }

private:
    void on_initialization_completed(Dirty, bool)
    {
        ++initialization_batches_;
    }

    void on_loaded()
    {
        begin_init();
        invalidate(Dirty::style | Dirty::paint);
        begin_init();
        invalidate(Dirty::semantics | Dirty::accessibility);
        end_init();
        end_init();
    }

    SubscriptionToken initialized_;
    SubscriptionToken load_;
    std::uint64_t initialization_batches_{};
};

}  // namespace

void GalleryContext::synchronize(std::string_view cause)
{
    if (window == nullptr) {
        return;
    }
    UpdateScope scope = (*window).begin_update();

    if (cause == "gallery.command.reset") {
        drop_status.clear();
        for (const dml::NodeSpec& node : dml::gallery_nodes) {
            invalidate_gallery_control(*window, node.id);
        }
        (*(*window).find("gallery.diagnostics")).set_visible(model.state().diagnostics_visible);
    } else if (cause == "gallery.command.diagnostics") {
        (*(*window).find("gallery.diagnostics")).set_visible(model.state().diagnostics_visible);
        invalidate_gallery_control(*window, cause);
    } else if (cause == "gallery.slider") {
        invalidate_gallery_controls(*window,
            {"gallery.slider", "gallery.progress", "gallery.value-label",
             "gallery.instrument"});
    } else if (cause == "gallery.radio.classic" || cause == "gallery.radio.quiet") {
        invalidate_gallery_controls(*window,
            {"gallery.radio.classic", "gallery.radio.quiet", "gallery.default-button",
             "gallery.slider"});
    } else if (cause.starts_with("gallery.collection.")) {
        invalidate_gallery_controls(*window,
            {"gallery.collection.alpha", "gallery.collection.beta",
             "gallery.collection.gamma", "gallery.collection.delta"});
    } else {
        invalidate_gallery_control(*window, cause);
    }

    // Diagnostics are live instrumentation, not a snapshot captured when the
    // panel opens. Keep their damage localized to the metric rows while
    // ensuring every retained interaction is observable on the next frame.
    if (model.state().diagnostics_visible) {
        invalidate_gallery_controls(*window,
            {"gallery.diagnostics.about",
             "gallery.diagnostics.renderer", "gallery.diagnostics.controls",
             "gallery.diagnostics.layout", "gallery.diagnostics.paint",
             "gallery.diagnostics.input", "gallery.diagnostics.flush",
             "gallery.diagnostics.present"});
    }

    const bool quiet = model.state().style_mode == StyleMode::quiet_relief;
    const BasicControlStyle style = gallery_basic_style(quiet);
    for (const dml::NodeSpec& node : dml::gallery_nodes) {
        const Control::Ptr control = (*window).find(node.id);
        if (std::shared_ptr<gui_forms::ButtonBase> button = std::dynamic_pointer_cast<ButtonBase>(control)) {
            (*button).set_style(style);
        } else if (std::shared_ptr<gui_forms::RangeControl> range = std::dynamic_pointer_cast<RangeControl>(control)) {
            (*range).set_style(style);
        }
    }
    if (std::shared_ptr<gui_forms::CheckBox> check = std::dynamic_pointer_cast<CheckBox>(
            (*window).find("gallery.checkbox"))) {
        (*check).set_checked(model.state().precise_updates);
    }
    if (std::shared_ptr<gui_forms::RadioButton> classic = std::dynamic_pointer_cast<RadioButton>(
            (*window).find("gallery.radio.classic"))) {
        (*classic).set_checked(model.state().style_mode == StyleMode::classic_relief);
    }
    if (std::shared_ptr<gui_forms::RadioButton> quiet_radio = std::dynamic_pointer_cast<RadioButton>(
            (*window).find("gallery.radio.quiet"))) {
        (*quiet_radio).set_checked(model.state().style_mode == StyleMode::quiet_relief);
    }
    if (cause == "gallery.command.reset") {
        if (std::shared_ptr<gui_forms::CheckBox> tri = std::dynamic_pointer_cast<CheckBox>(
                (*window).find("gallery.checkbox.indeterminate"))) {
            (*tri).set_check_state(CheckState::indeterminate);
        }
        if (std::shared_ptr<gui_forms::LinkLabel> link = std::dynamic_pointer_cast<LinkLabel>(
                (*window).find("gallery.link"))) {
            (*link).set_visited(false);
        }
    }
    if (std::shared_ptr<gui_forms::TrackBar> slider = std::dynamic_pointer_cast<TrackBar>(
            (*window).find("gallery.slider"))) {
        (*slider).set_value(model.state().slider_value);
    }
    if (std::shared_ptr<gui_forms::ProgressBar> progress = std::dynamic_pointer_cast<ProgressBar>(
            (*window).find("gallery.progress"))) {
        (*progress).set_value(model.state().progress_value);
    }
    update_gallery_label(*window, "gallery.command.status", drop_status.empty()
        ? "Portable core · Host 0.4 · reusable controls" : drop_status);
    update_gallery_label(*window, "gallery.value-label",
                         format_percent(model.state().progress_value));
    for (const dml::NodeSpec& node : dml::gallery_nodes) {
        if (node.id.starts_with("gallery.diagnostics.")) {
            const std::string metric = metric_text(node.id, (*window).metrics_snapshot());
            if (!metric.empty()) {
                update_gallery_label(*window, node.id, metric);
            }
        }
    }
}

GalleryControl::GalleryControl(StableId stable_id,
                               const dml::NodeSpec& specification,
                               std::shared_ptr<GalleryContext> context)
    : Control(std::move(stable_id)), specification_(&specification), context_(std::move(context))
{
    set_paint_plane(paint_plane_for(specification));
    set_enabled((specification.flags & dml::enabled) != 0U);
    set_visible((specification.flags & dml::visible) != 0U);
    set_focusable((specification.flags & dml::focusable) != 0U);
    set_allow_drop(specification.id == "gallery.collection");
    const bool interactive = (specification.flags & dml::enabled) != 0U;
    if (interactive && specification.kind == dml::NodeKind::text_input) {
        set_cursor(CursorKind::text);
    } else if (interactive &&
               (specification.kind == dml::NodeKind::button ||
                specification.kind == dml::NodeKind::check_box ||
                specification.kind == dml::NodeKind::radio_button ||
                specification.kind == dml::NodeKind::slider ||
                specification.kind == dml::NodeKind::row)) {
        set_cursor(CursorKind::hand);
    }
}

Size GalleryControl::measure(Size available)
{
    const double preferred_width = (*specification_).preferred_width > 0
                                       ? static_cast<double>((*specification_).preferred_width)
                                       : available.width;
    const double preferred_height = (*specification_).preferred_height > 0
                                        ? static_cast<double>((*specification_).preferred_height)
                                        : available.height;
    return {std::min(available.width, preferred_width),
            std::min(available.height, preferred_height)};
}

void GalleryControl::arrange(Rect final_bounds)
{
    arrange_children({final_bounds.width, final_bounds.height});
    Control::arrange(final_bounds);
}

void GalleryControl::arrange_children(Size size)
{
    if (!is_container((*specification_).kind) || children().empty()) {
        return;
    }

    const Rect content = content_rect((*specification_).kind, size);
    if ((*specification_).layout == LayoutKind::form_grid) {
        const double first_row_y = content.y;
        const double second_row_y = first_row_y + 28.0;
        const double third_row_y = second_row_y + 34.0;
        for (const Control::Ptr& child : children()) {
            const std::string_view id = (*child).stable_id().value();
            if (id == "gallery.intro") {
                request_bounds(child, {content.x, first_row_y, content.width, 22.0});
            } else if (id == "gallery.text-input") {
                request_bounds(child, {content.x, second_row_y, 250.0, 28.0});
            } else if (id == "gallery.default-button") {
                request_bounds(child, {content.x + 256.0, second_row_y, 92.0, 28.0});
            } else if (id == "gallery.disabled-button") {
                request_bounds(child, {content.x + 354.0, second_row_y, 108.0, 28.0});
            } else if (id == "gallery.checkbox") {
                request_bounds(child, {content.x, third_row_y, 220.0, 24.0});
            } else if (id == "gallery.radio.classic") {
                request_bounds(child, {content.x + 226.0, third_row_y, 180.0, 24.0});
            } else if (id == "gallery.radio.quiet") {
                request_bounds(child, {content.x + 412.0, third_row_y, 180.0, 24.0});
            } else if (id == "gallery.link") {
                request_bounds(child, {content.x + 230.0, third_row_y + 28.0, 170.0, 24.0});
            } else if (id == "gallery.checkbox.indeterminate") {
                request_bounds(child, {content.x, third_row_y + 28.0, 220.0, 24.0});
            } else if (id == "gallery.lifecycle-card") {
                request_bounds(child, {content.x + 406.0, third_row_y + 28.0,
                                       std::max(0.0, content.width - 406.0), 24.0});
            }
        }
        return;
    }
    if ((*specification_).layout == LayoutKind::none) {
        return;
    }

    const double child_gap = (*specification_).kind == NodeKind::list ? 0.0 : gap;
    double fixed = 0.0;
    std::size_t flexible = 0;
    for (const Control::Ptr& child : children()) {
        if (!(*child).visible()) {
            continue;
        }
        const dml::NodeSpec* gallery_child = dml::find((*child).stable_id().value());
        if (gallery_child == nullptr) {
            throw std::logic_error("Gallery child has no compiled DML specification");
        }
        const int preferred = (*specification_).layout == LayoutKind::flex_row
                                  ? (*gallery_child).preferred_width
                                  : (*gallery_child).preferred_height;
        if (preferred > 0) {
            fixed += static_cast<double>(preferred);
        } else {
            ++flexible;
        }
        fixed += child_gap;
    }
    if (fixed > 0.0) {
        fixed -= child_gap;
    }

    const double extent = (*specification_).layout == LayoutKind::flex_row ? content.width
                                                                          : content.height;
    const double flexible_extent = flexible == 0
                                       ? 0.0
                                       : std::max(0.0, extent - fixed) /
                                             static_cast<double>(flexible);
    double cursor = (*specification_).layout == LayoutKind::flex_row ? content.x : content.y;
    for (const Control::Ptr& child : children()) {
        if (!(*child).visible()) {
            continue;
        }
        const dml::NodeSpec* gallery_child = dml::find((*child).stable_id().value());
        if (gallery_child == nullptr) {
            throw std::logic_error("Gallery child has no compiled DML specification");
        }
        const int preferred = (*specification_).layout == LayoutKind::flex_row
                                  ? (*gallery_child).preferred_width
                                  : (*gallery_child).preferred_height;
        const double child_extent = preferred > 0 ? static_cast<double>(preferred) : flexible_extent;
        Rect bounds = content;
        if ((*specification_).layout == LayoutKind::flex_row) {
            bounds.x = cursor;
            bounds.width = child_extent;
        } else {
            bounds.y = cursor;
            bounds.height = child_extent;
        }
        request_bounds(child, bounds);
        cursor += child_extent + child_gap;
    }
}

void GalleryControl::on_paint(Painter& painter, Rect)
{
    const Rect bounds {0.0, 0.0, committed_arranged_bounds().width,
                       committed_arranged_bounds().height};
    const std::string text = display_text();

    switch ((*specification_).kind) {
    case NodeKind::form:
        painter.fill_rect(bounds, Color::rgba(213, 223, 232));
        return;
    case NodeKind::command_strip:
        painter.fill_rect(bounds, face_light);
        painter.fill_rect({0.0, 0.0, bounds.width, 3.0}, accent);
        painter.fill_rect({0.0, 3.0, bounds.width, 1.0}, Color::rgba(151, 201, 235));
        painter.draw_line({0.0, bounds.height - 1.0}, {bounds.width, bounds.height - 1.0},
                          command_rule, 1.0);
        return;
    case NodeKind::panel:
        painter.fill_rect(bounds, paper);
        return;
    case NodeKind::user_control:
        // A user control is drawn by the controls it holds.
        return;
    case NodeKind::backplane: {
        painter.fill_rect(bounds, Color::rgba(222, 235, 247));
        const double third = bounds.width / 3.0;
        painter.fill_rect({0.0, 0.0, third, bounds.height}, Color::rgba(207, 227, 244));
        painter.fill_rect({third, 0.0, third, bounds.height}, Color::rgba(226, 231, 246));
        painter.fill_rect({third * 2.0, 0.0, third, bounds.height},
                          Color::rgba(237, 228, 241));
        painter.fill_rect({0.0, bounds.height * 0.62, bounds.width, bounds.height * 0.38},
                          Color::rgba(245, 248, 251, 224));
        painter.fill_rect({0.0, 0.0, 5.0, bounds.height}, accent);
        return;
    }
    case NodeKind::group:
    case NodeKind::diagnostics:
        if (((*specification_).flags & dml::transparent) == 0U) {
            painter.fill_rect(bounds,
                              (*specification_).kind == NodeKind::diagnostics ? face_light : face);
        }
        painter.stroke_rect({0.5, 10.5, std::max(0.0, bounds.width - 1.0),
                             std::max(0.0, bounds.height - 11.0)}, shadow, 1.0);
        painter.fill_rect({9.0, 3.0, std::min(bounds.width - 18.0, 190.0), 16.0},
                          ((*specification_).flags & dml::transparent) != 0U
                              ? Color::rgba(230, 238, 247, 232)
                              : face);
        painter.draw_text_utf8({13.0, 15.0}, text, {FontRole::control, 12.0, 600, false}, ink);
        return;
    // These families are always instantiated as reusable public controls by
    // build_gallery_tree(). Keeping duplicate Gallery renderers here would
    // disguise an extraction regression.
    case NodeKind::label:
    case NodeKind::link_label:
    case NodeKind::button:
    case NodeKind::check_box:
    case NodeKind::radio_button:
    case NodeKind::slider:
    case NodeKind::progress:
        // Drawn by the public control (above).
        return;
    case NodeKind::text_input:
        sunken_frame(painter, bounds, paper);
        if (focused_) {
            painter.stroke_rect({1.5, 1.5, bounds.width - 3.0, bounds.height - 3.0},
                                accent, 1.0);
        }
        painter.draw_text_utf8({7.0, bounds.height * 0.5 + 5.0}, text,
                               {FontRole::content, 13.0, 400, false}, ink);
        if (focused_) {
            const double caret_x = std::min(bounds.width - 5.0, 7.0 + text.size() * 7.0);
            painter.draw_line({caret_x, 5.0}, {caret_x, bounds.height - 5.0}, ink, 1.0);
        }
        return;
    case NodeKind::list:
        sunken_frame(painter, bounds, paper);
        if (drag_hovered_) {
            painter.fill_rect({2.0, 2.0, std::max(0.0, bounds.width - 4.0),
                               std::max(0.0, bounds.height - 4.0)},
                              Color::rgba(216, 235, 249, 96));
            painter.stroke_rect({1.5, 1.5, std::max(0.0, bounds.width - 3.0),
                                 std::max(0.0, bounds.height - 3.0)}, accent, 2.0);
        }
        return;
    case NodeKind::row:
        painter.fill_rect(bounds, selected() ? selection : paper);
        if (selected()) {
            painter.fill_rect({0.0, 0.0, 4.0, bounds.height}, accent_deep);
            painter.draw_line({4.0, bounds.height - 1.0}, {bounds.width, bounds.height - 1.0},
                              Color::rgba(31, 91, 151), 1.0);
        }
        painter.draw_text_utf8({7.0, 18.0}, text,
                               {FontRole::content, 12.0, 400, false},
                               selected() ? highlight : ink);
        if (focused_) {
            painter.stroke_rect({1.5, 1.5, bounds.width - 3.0, bounds.height - 3.0},
                                selected() ? highlight : accent, 1.0);
        }
        return;
    case NodeKind::instrument: {
        sweep_timer_.note_painted();
        painter.fill_rect(bounds, instrument_face);
        painter.stroke_rect({0.5, 0.5, bounds.width - 1.0, bounds.height - 1.0}, deep_shadow, 1.0);
        for (int line = 1; line < 5; ++line) {
            const double y = bounds.height * static_cast<double>(line) / 5.0;
            painter.draw_line({8.0, y}, {bounds.width - 8.0, y}, Color::rgba(47, 70, 101), 1.0);
        }
        const double level = value();
        const double marker_x = 10.0 + level * std::max(0.0, bounds.width - 20.0);
        painter.draw_line({10.0, bounds.height - 14.0}, {marker_x, 18.0}, cyan, 2.0);
        painter.draw_line({marker_x, 18.0}, {bounds.width - 10.0, bounds.height - 25.0}, amber, 2.0);
        const Rect sweep = sweep_bounds(sweep_phase_);
        const double sweep_x = sweep.x + 1.0;
        painter.draw_line({sweep_x, 25.0}, {sweep_x, bounds.height - 8.0},
                          Color::rgba(128, 223, 239, 150), 1.0);
        char percent[32]{};
        std::snprintf(percent, sizeof(percent), "LEVEL %3.0f%%", level * 100.0);
        painter.draw_text_utf8({12.0, 17.0}, percent, {FontRole::monospace, 12.0, 600, false}, cyan);
        return;
    }
    }
    // A kind outside the enumeration paints nothing.
    return;
}

namespace {

// The instrument sweep crosses the trace once in this period (it moved 0.027
// of the trace per 30 Hz frame before it followed time).
constexpr FrameInterval instrument_sweep_period = std::chrono::microseconds(1'234'568);
// The instrument's authored tick rate, 30 Hz.
constexpr FrameInterval instrument_sweep_interval = std::chrono::nanoseconds(33'333'333);

} // namespace

void GalleryControl::on_attached_to_window()
{
    Control::on_attached_to_window();
    if ((*specification_).kind != NodeKind::instrument) return;
    sweep_last_tick_ = FrameClock::now();
    const std::shared_ptr<GalleryControl> self =
        std::static_pointer_cast<GalleryControl>(shared_from_this());
    sweep_timer_.start(*this, instrument_sweep_interval, SweepTick{self});
}

void GalleryControl::on_detached_from_window() noexcept
{
    sweep_timer_.stop();
    Control::on_detached_from_window();
}

AnimationTickResult GalleryControl::SweepTick::operator()(FrameTime now) const
{
    const std::shared_ptr<GalleryControl> live = control.lock();
    const AnimationTickResult result = live
        ? (*live).on_sweep_tick(now) : AnimationTickResult::unchanged;
    return result;
}

AnimationTickResult GalleryControl::on_sweep_tick(FrameTime now)
{
    // A timer that waited for a paint resumes from where it stopped.
    const FrameInterval elapsed = std::min(now - sweep_last_tick_, instrument_sweep_interval);
    sweep_last_tick_ = now;
    const double advance = std::chrono::duration<double>(std::max(elapsed, FrameInterval{})).count() /
        std::chrono::duration<double>(instrument_sweep_period).count();
    const Rect before = sweep_bounds(sweep_phase_);
    sweep_phase_ = std::fmod(sweep_phase_ + advance, 1.0);
    const Rect after = sweep_bounds(sweep_phase_);
    invalidate(Rect::united(before, after));
    return AnimationTickResult::damaged;
}

Rect GalleryControl::sweep_bounds(double phase) const noexcept
{
    // The sweep is a 1-pixel line from y 25 to 8 above the bottom; its
    // bounds take one pixel either side of its centre.
    const Rect bounds = committed_arranged_bounds();
    const double x = 10.0 + phase * std::max(0.0, bounds.width - 20.0);
    const Rect sweep{x - 1.0, 24.0, 2.0, std::max(0.0, bounds.height - 31.0)};
    return sweep;
}

bool GalleryControl::hit_test_local(Point local_point) const
{
    return focusable() && Control::hit_test_local(local_point);
}

void GalleryControl::on_pointer(PointerEvent& event)
{
    if (!enabled()) {
        return;
    }
    if (event.action == PointerAction::leave) {
        pressed_ = false;
        invalidate(Dirty::paint);
    }
    if (event.button == PointerButton::primary && event.action == PointerAction::down) {
        pressed_ = true;
        invalidate(Dirty::paint);
        event.handled = true;
    } else if (event.button == PointerButton::primary && event.action == PointerAction::up) {
        pressed_ = false;
        invalidate(Dirty::paint);
        event.handled = true;
    }
}

void GalleryControl::on_text_input(TextInputEvent& event)
{
    if ((*specification_).kind != NodeKind::text_input || !enabled() || event.composing) {
        return;
    }
    std::string next = (*context_).model.state().text;
    if (event.replacement_start >= 0) {
        const std::size_t start = std::min(next.size(), static_cast<std::size_t>(event.replacement_start));
        const std::size_t length = std::min(next.size() - start,
                                            static_cast<std::size_t>(std::max(0, event.replacement_length)));
        next.replace(start, length, event.text_utf8);
    } else {
        next.append(event.text_utf8);
    }
    if ((*context_).model.replace_text(std::move(next))) {
        (*context_).synchronize((*specification_).id);
    }
    event.handled = true;
}

void GalleryControl::on_drag(DragEvent& event)
{
    if ((*specification_).id != "gallery.collection") {
        return;
    }
    if (event.action == DragAction::leave) {
        drag_hovered_ = false;
        invalidate(Dirty::paint);
        event.handled = true;
        return;
    }
    drag_hovered_ = event.action != DragAction::drop;
    if (has_drag_effect(event.allowed_effects, DragEffect::copy)) {
        event.accepted_effect = DragEffect::copy;
    }
    if (event.action == DragAction::drop) {
        std::size_t files = 0;
        std::size_t text_items = 0;
        std::size_t binary_items = 0;
        for (const DragDataItem& item : event.items) {
            std::visit(DropDataCounter(files, text_items, binary_items), item);
        }
        char status[128]{};
        std::snprintf(status, sizeof(status),
                      "Drop received · %zu file%s · %zu text · %zu data",
                      files, files == 1 ? "" : "s", text_items, binary_items);
        (*context_).drop_status = status;
        (*context_).synchronize("gallery.command.status");
    }
    invalidate(Dirty::paint);
    event.handled = true;
}

void GalleryControl::on_focus_changed(bool focused)
{
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

void GalleryControl::on_activate()
{
    if ((*context_).model.activate((*specification_).id)) {
        (*context_).synchronize((*specification_).id);
    }
}

NodeKind GalleryControl::kind() const noexcept
{
    return (*specification_).kind;
}

bool GalleryControl::selected() const
{
    return (*context_).model.state().selected_collection_row == (*specification_).id;
}

double GalleryControl::value() const
{
    if ((*specification_).kind == NodeKind::instrument) {
        return (*context_).model.state().instrument_value;
    }
    return (*specification_).value;
}

std::string GalleryControl::display_text() const
{
    if ((*specification_).id == "gallery.command.status" &&
        !(*context_).drop_status.empty()) {
        return (*context_).drop_status;
    }
    if ((*specification_).id == "gallery.text-input") {
        return (*context_).model.state().text;
    }
    if ((*specification_).id == "gallery.value-label") {
        return format_percent((*context_).model.state().progress_value);
    }
    if ((*specification_).id.starts_with("gallery.diagnostics.") && (*context_).window != nullptr) {
        const std::string metrics = metric_text((*specification_).id,
                                                (*(*context_).window).metrics_snapshot());
        if (!metrics.empty()) {
            return metrics;
        }
    }
    return std::string((*specification_).text);
}

Control::Ptr GalleryTree::mount(std::string_view id) const
{
    for (const GalleryMount& candidate : mounts) {
        if ((*candidate.specification).id == id) {
            return candidate.control;
        }
    }
    return nullptr;
}

GalleryTree build_gallery_tree()
{
    std::shared_ptr<gui_forms::gallery::GalleryContext> context = std::make_shared<GalleryContext>();
    std::unordered_map<std::string_view, Control::Ptr> controls;
    controls.reserve(dml::gallery_nodes.size());

    for (const dml::NodeSpec& node : dml::gallery_nodes) {
        if (node.parent_id.empty()) {
            // gallery.root is the declared boundary; the shell is the real root.
            continue;
        }
        Control::Ptr control;
        if (node.kind == NodeKind::user_control) {
            control = make_control<GalleryLifecycleCard>(
                StableId(std::string(node.id)));
        } else if (node.kind == NodeKind::label && node.id == "gallery.command.status") {
            control = make_control<GalleryStatusLabel>(
                StableId(std::string(node.id)), std::string(node.text), context);
        } else if (node.kind == NodeKind::label &&
                   node.id == "gallery.diagnostics.font") {
            control = make_control<GalleryFontPolicyLabel>(
                StableId(std::string(node.id)));
        } else if (node.kind == NodeKind::label) {
            std::shared_ptr<gui_forms::Label> label = make_control<Label>(StableId(std::string(node.id)),
                                             std::string(node.text));
            (*label).set_enabled((node.flags & dml::enabled) != 0U);
            (*label).set_visible((node.flags & dml::visible) != 0U);
            if (node.id == "gallery.lifecycle-title") {
                (*label).set_font({FontRole::control, 10.0, 600, false});
            } else if (node.id == "gallery.lifecycle-status") {
                (*label).set_font({FontRole::content, 10.0, 400, false});
            } else if (node.id == "gallery.diagnostics.about") {
                (*label).set_font({FontRole::content, 10.0, 400, false});
                (*label).set_text_wrapping(TextWrapping::word);
                (*label).set_vertical_alignment(VerticalAlignment::near);
            } else {
                (*label).set_font({FontRole::content, 12.0, 400, false});
            }
            control = std::move(label);
        } else if (node.kind == NodeKind::button) {
            control = make_control<GalleryButton>(StableId(std::string(node.id)),
                                                  node, context);
        } else if (node.kind == NodeKind::check_box) {
            control = make_control<GalleryCheckBox>(StableId(std::string(node.id)),
                                                    node, context);
        } else if (node.kind == NodeKind::radio_button) {
            control = make_control<GalleryRadioButton>(StableId(std::string(node.id)),
                                                       node, context);
        } else if (node.kind == NodeKind::link_label) {
            control = make_control<GalleryLinkLabel>(StableId(std::string(node.id)),
                                                     node, context);
        } else if (node.kind == NodeKind::slider) {
            control = make_control<GalleryTrackBar>(StableId(std::string(node.id)),
                                                    node, context);
        } else if (node.kind == NodeKind::progress) {
            std::shared_ptr<gui_forms::ProgressBar> progress = make_control<ProgressBar>(StableId(std::string(node.id)));
            (*progress).set_enabled((node.flags & dml::enabled) != 0U);
            (*progress).set_visible((node.flags & dml::visible) != 0U);
            (*progress).set_range(node.minimum, node.maximum);
            (*progress).set_value((*context).model.state().progress_value);
            control = std::move(progress);
        } else {
            control = make_control<GalleryControl>(StableId(std::string(node.id)),
                                                   node, context);
        }
        controls.emplace(node.id, control);
    }
    GalleryTree tree;
    tree.context = std::move(context);
    for (const dml::NodeSpec& node : dml::gallery_nodes) {
        if (node.parent_id.empty()) {
            continue;
        }
        const std::unordered_map<std::string_view, Control::Ptr>::iterator
            child = controls.find(node.id);
        if (child == controls.end()) {
            throw std::logic_error("compiled gallery DML contains an uninstantiated node");
        }
        if (node.parent_id == "gallery.root") {
            tree.mounts.push_back({&node, (*child).second});
            continue;
        }
        const std::unordered_map<std::string_view, Control::Ptr>::iterator
            parent = controls.find(node.parent_id);
        if (parent == controls.end()) {
            throw std::logic_error("compiled gallery DML contains an unresolved parent");
        }
        (*(*parent).second).add_child((*child).second);
    }
    return tree;
}

}  // namespace gui_forms::gallery
