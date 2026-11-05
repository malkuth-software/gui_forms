#include "gui_forms/window.hpp"

#include "../../display/chunk/display_chunk.hpp"
#include "../../display/command/display_command.hpp"
#include "../popup/popup_attachment.hpp"
#include "../../text/prepared/prepared_storage.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

VisualPaintOperation public_operation(detail::DisplayOperation operation) {
    using Private = detail::DisplayOperation;
    switch (operation) {
    case Private::save: return VisualPaintOperation::save;
    case Private::restore: return VisualPaintOperation::restore;
    case Private::translate: return VisualPaintOperation::translate;
    case Private::clip_rect: return VisualPaintOperation::clip_rect;
    case Private::clip_rounded_rect:
    case Private::clip_rounded_rect_corners:
        return VisualPaintOperation::clip_rounded_rect;
    case Private::fill_rect: return VisualPaintOperation::fill_rect;
    case Private::fill_rounded_rect:
        return VisualPaintOperation::fill_rounded_rect;
    case Private::stroke_rect: return VisualPaintOperation::stroke_rect;
    case Private::stroke_rounded_rect:
        return VisualPaintOperation::stroke_rounded_rect;
    case Private::fill_linear_gradient:
        return VisualPaintOperation::fill_linear_gradient;
    case Private::fill_linear_gradient_spread:
        return VisualPaintOperation::fill_linear_gradient_spread;
    case Private::fill_radial_gradient:
        return VisualPaintOperation::fill_radial_gradient;
    case Private::draw_box_shadow: return VisualPaintOperation::draw_box_shadow;
    case Private::draw_inset_box_shadow:
        return VisualPaintOperation::draw_inset_box_shadow;
    case Private::draw_line: return VisualPaintOperation::draw_line;
    case Private::draw_text_utf8: return VisualPaintOperation::draw_text;
    case Private::draw_prepared_text:
        return VisualPaintOperation::draw_prepared_text;
    case Private::draw_image: return VisualPaintOperation::draw_image;
    case Private::draw_image_region:
        return VisualPaintOperation::draw_image_region;
    case Private::draw_image_region_sampled:
        return VisualPaintOperation::draw_image_region_sampled;
    case Private::fill_image_pattern:
        return VisualPaintOperation::fill_image_pattern;
    case Private::draw_live_surface:
        return VisualPaintOperation::draw_live_surface;
    case Private::draw_part_raster:
    case Private::draw_part_raster_scaled:
        return VisualPaintOperation::draw_part_raster;
    case Private::begin_group: return VisualPaintOperation::begin_group;
    case Private::end_group: return VisualPaintOperation::end_group;
    }
    throw std::logic_error("GUI.Forms display operation has no inspection spelling");
}

VisualPaintOperationSnapshot inspect_operation(
    const detail::DisplayCommand& command, std::size_t index,
    bool include_text, bool include_resolved_text,
    TextMetricsProvider* provider) {
    VisualPaintOperationSnapshot result;
    result.command_index = index;
    result.operation = public_operation(command.operation);
    result.first = command.first;
    result.second = command.second;
    result.rect = command.rect;
    result.color = command.color;
    result.font = command.font;
    result.image = command.image;
    result.scalar = command.scalar;
    result.secondary_scalar = command.secondary_scalar;
    result.tertiary_scalar = command.tertiary_scalar;
    result.gradient_stops = command.gradient_stops;
    result.gradient_spread = command.gradient_spread;
    result.image_pattern_wrap = command.image_pattern_wrap;
    result.image_sampling = command.image_sampling;
    result.text_byte_count = command.text.size();
    result.text_orientation = command.text_orientation;
    if (include_text) result.text = command.text;
    if (command.operation == detail::DisplayOperation::draw_prepared_text &&
        command.prepared_text && (*command.prepared_text).input) {
        const detail::PreparedInputStorage& input = *(*command.prepared_text).input;
        result.font = input.key.font;
        result.text_byte_count = input.text_bytes;
        if (include_text && input.text_bytes != 0U) {
            result.text.assign(input.text.get(), input.text_bytes);
        }
        // Inspect the retained payload even if its paint authority is stale.
        // Do not reshape it through a potentially different ordinary provider.
    }
    if (include_resolved_text && provider != nullptr &&
        command.operation == detail::DisplayOperation::draw_text_utf8) {
        result.resolved_text = (*provider).resolve_text_layout_utf8(
            command.text, command.font);
    }
    return result;
}

struct PendingInspectionNode final {
    Control::Ptr control;
    std::optional<RuntimeId> parent_runtime_id;
    std::string parent_stable_id;
    std::size_t depth{};
    bool popup_tree{};
    Rect ancestor_clip{};
    std::size_t ancestor_clip_count{};
};

} // namespace

VisualInspectionSnapshot SurfaceRoot::visual_inspection_snapshot(
    VisualInspectionOptions options) {
    require_root_thread("visual inspection snapshot");
    settle_dirty();
    if (options.maximum_controls == 0U ||
        options.maximum_controls > VisualInspectionOptions::maximum_allowed_controls) {
        throw std::invalid_argument(
            "visual inspection maximum_controls is outside the bounded range");
    }
    if (options.include_display_operations &&
        (options.maximum_operations_per_control == 0U ||
         options.maximum_operations_per_control >
             VisualInspectionOptions::maximum_allowed_operations_per_control)) {
        throw std::invalid_argument(
            "visual inspection operation limit is outside the bounded range");
    }

    ensure_layout(true);

    VisualInspectionSnapshot snapshot;
    snapshot.content_revision = content_revision_;
    snapshot.display_generation = display_generation_;
    snapshot.client_size = client_size_;
    snapshot.device_scale = scale_;
    snapshot.text_scale = presentation_settings_.text_scale;
    snapshot.high_contrast = presentation_settings_.high_contrast;
    snapshot.reduced_motion = presentation_settings_.reduced_motion;
    snapshot.sound_enabled = presentation_settings_.sound_enabled;
    snapshot.theme_id = std::string((*theme_).id());
    snapshot.window_active = active_;
    snapshot.window_occluded = occluded_;
    snapshot.paint_lease = paint_lease_snapshot();
    for (std::size_t index = 0U; index < paint_plane_count; ++index) {
        const std::span<const Rect> rectangles = plane_damage_[index].rectangles();
        snapshot.pending_damage[index].assign(rectangles.begin(), rectangles.end());
    }
    snapshot.controls.reserve(std::min(options.maximum_controls,
                                       stable_ids_.size() + popups_.size()));

    const Rect window_clip{0.0, 0.0, client_size_.width, client_size_.height};
    std::vector<PendingInspectionNode> pending;
    if (root_) {
        pending.push_back({root_, std::nullopt, {}, 0U, false, window_clip, 0U});
    }
    for (PopupList::reverse_iterator iterator = popups_.rbegin();
         iterator != popups_.rend(); ++iterator) {
        if (!*iterator || !(*(*iterator)).connected()) continue;
        const Control::Ptr popup = (*(*iterator)).popup();
        if (popup) {
            pending.push_back({popup, std::nullopt, {}, 0U, true, window_clip, 0U});
        }
    }

    while (!pending.empty()) {
        PendingInspectionNode current = std::move(pending.back());
        pending.pop_back();
        const Control::Ptr& control = current.control;
        if (!control || !(*control).is_alive() || (*control).surface_root_ != this) {
            continue;
        }
        ++snapshot.total_controls;

        const Rect absolute = absolute_bounds_of(*control);
        const Insets outsets = (*control).effective_visual_outsets();
        const Rect visual = visual_bounds_of(*control, outsets);
        const Rect effective_clip = Rect::intersection(
            visual, current.ancestor_clip);
        const Rect window_visible = Rect::intersection(visual, window_clip);
        const Rect viewport = (*control).child_viewport_rectangle();
        const Rect absolute_viewport{absolute.x + viewport.x,
                                     absolute.y + viewport.y,
                                     viewport.width, viewport.height};
        const Rect child_clip = Rect::intersection(
            current.ancestor_clip, absolute_viewport);

        if (snapshot.controls.size() < options.maximum_controls) {
            VisualControlInspection inspected;
            inspected.runtime_id = (*control).runtime_id_;
            inspected.stable_id = (*control).stable_id_.value();
            inspected.parent_runtime_id = current.parent_runtime_id;
            inspected.parent_stable_id = current.parent_stable_id;
            inspected.depth = current.depth;
            inspected.popup_tree = current.popup_tree;
            inspected.layout.requested_bounds = (*control).requested_bounds_;
            inspected.layout.arranged_bounds = (*control).arranged_bounds_;
            inspected.layout.absolute_bounds = absolute;
            inspected.layout.client_rectangle = (*control).client_rectangle();
            inspected.layout.display_rectangle = (*control).display_rectangle();
            inspected.layout.child_viewport = viewport;
            inspected.layout.visual_bounds = visual;
            inspected.layout.effective_clip = effective_clip;
            inspected.layout.visual_outsets = outsets;
            inspected.layout.margin = (*control).margin.read();
            inspected.layout.padding = (*control).padding.read();
            inspected.layout.minimum_size = (*control).minimum_size.read();
            inspected.layout.maximum_size = (*control).maximum_size.read();
            inspected.layout.dock = (*control).dock.read();
            inspected.layout.anchor = (*control).anchor.read();
            inspected.layout.auto_size = (*control).auto_size.read();
            inspected.layout.clipped_by_ancestor =
                effective_clip != window_visible;
            inspected.layout.fully_clipped = effective_clip.empty();
            inspected.layout.ancestor_clip_count = current.ancestor_clip_count;
            inspected.layout.transaction = (*control).layout_transaction_state();

            inspected.state.visible = (*control).visible.read();
            inspected.state.layout_collapsed =
                (*control).layout_collapsed_;
            inspected.state.effectively_visible =
                (*control).effectively_visible();
            inspected.state.enabled = (*control).enabled.read();
            inspected.state.effectively_enabled =
                (*control).effectively_enabled();
            inspected.state.focusable = (*control).focusable_;
            inspected.state.focused = focused_.lock().get() == control.get();
            inspected.state.focus_cue_visible =
                inspected.state.focused && focus_cue_visible_;
            inspected.state.hovered = hovered_.lock().get() == control.get();
            inspected.state.pressed = pressed_.lock().get() == control.get();
            inspected.state.pointer_captured =
                captured_.lock().get() == control.get();
            inspected.state.hit_test_transparent =
                (*control).hit_test_transparent.read();
            inspected.state.window_active = active_;
            inspected.state.initializing = (*control).initializing();
            inspected.state.visual_status = (*control).visual_status_;
            inspected.dirty = (*control).dirty_;
            inspected.subtree_dirty = (*control).subtree_dirty_;
            inspected.paint_plane = (*control).paint_plane_;
            inspected.theme_id = std::string((*control).effective_theme().id());
            inspected.theme_override = static_cast<bool>((*control).theme_override_);
            inspected.authored_material = (*control).authored_surface_material_;

            const std::shared_ptr<const detail::DisplayChunk> chunk =
                (*control).display_chunk_;
            if (chunk) {
                inspected.display_chunk = (*chunk).info();
                inspected.total_display_operations = (*chunk).commands().size();
                inspected.display_chunk_current =
                    !has_dirty((*control).dirty_, Dirty::paint) &&
                    (*chunk).plane() == (*control).paint_plane_ &&
                    (*chunk).logical_bounds() == Rect{0.0, 0.0,
                        (*control).arranged_bounds_.width,
                        (*control).arranged_bounds_.height};
                if (options.include_display_operations) {
                    const std::size_t count = std::min(
                        options.maximum_operations_per_control,
                        (*chunk).commands().size());
                    inspected.display_operations.reserve(count);
                    for (std::size_t index = 0U; index < count; ++index) {
                        inspected.display_operations.push_back(inspect_operation(
                            (*chunk).commands()[index], index,
                            options.include_text, options.include_resolved_text,
                            text_metrics_provider_));
                    }
                    inspected.display_operations_truncated =
                        count != (*chunk).commands().size();
                }
            }
            snapshot.controls.push_back(std::move(inspected));
        } else {
            snapshot.controls_truncated = true;
        }

        const std::vector<Control::Ptr> children = (*control).children_;
        for (ControlList::const_reverse_iterator iterator = children.rbegin();
             iterator != children.rend(); ++iterator) {
            const Control::Ptr& child = *iterator;
            if (!child || !(*child).is_alive() || (*child).surface_root_ != this ||
                (*child).parent_.lock().get() != control.get()) {
                continue;
            }
            pending.push_back({child,
                               std::optional<RuntimeId>{(*control).runtime_id_},
                               std::string((*control).stable_id_.value()),
                               current.depth + 1U,
                               current.popup_tree, child_clip,
                               current.ancestor_clip_count + 1U});
        }
    }

    return snapshot;
}

} // namespace gui_forms
