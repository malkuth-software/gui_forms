#include "automation_calls.hpp"

#include "../../../core/host/session/session_directory.hpp"
#include "../marks/automation_marks_internal.hpp"

#include "gui_forms/automation/marks/automation_marks.hpp"
#include "gui_forms/host/session/host_session.hpp"
#include "gui_forms/visual_inspection/types/visual_inspection_types.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace gui_forms::automation::detail {
namespace {

// The most visual records and paint operations one inspect reads.
constexpr std::size_t inspect_control_limit = 4096U;
constexpr std::size_t inspect_operation_limit = 64U;
// The most bytes one inspect section may append before it is cut.
constexpr std::size_t inspect_section_limit = 64U * 1024U;

std::string number_text(double value) {
    std::string text = std::to_string(value);
    // to_string writes six decimals: trim the zeros it pads with.
    const std::size_t dot = text.find('.');
    if (dot != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    return text;
}

std::string rect_text(Rect rect) {
    std::string text = number_text(rect.x) + "," + number_text(rect.y) + " " + number_text(rect.width) + "x" +
                       number_text(rect.height);
    return text;
}

const char* yes_no(bool value) noexcept { return value ? "yes" : "no"; }

// Resolves the optional "target" of a read; on a failure fills `reply`.
bool resolve_optional_target(std::span<const FlatNode> nodes, const JsonValue& arguments, std::size_t& index,
                             ToolReply& reply) {
    index = no_parent;
    const std::optional<std::string> target = text_member(arguments, "target");
    if (!target.has_value()) return true;
    const std::optional<std::string> within = text_member(arguments, "within");
    const ResolvedTarget resolved = resolve_target(nodes, *target, within.has_value() ? *within : std::string{});
    if (resolved.resolution == Resolution::found) {
        index = resolved.index;
        return true;
    }
    if (resolved.resolution == Resolution::ambiguous) {
        reply = ToolReply::failure(ToolError::ambiguous, "[" + *target + "] names more than one node", resolved.hint);
        return false;
    }
    reply = ToolReply::failure(ToolError::not_found, "no node [" + *target + "]", resolved.hint);
    return false;
}

const VisualControlInspection* find_record(const VisualInspectionSnapshot&& snapshot, std::uint64_t runtime) = delete;

const VisualControlInspection* find_record(const VisualInspectionSnapshot& snapshot, std::uint64_t runtime) {
    for (const VisualControlInspection& record : snapshot.controls) {
        if (record.runtime_id.value == runtime) return &record;
    }
    return nullptr;
}

// ---- the core's inspect sections -------------------------------------------

void state_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    if (request.node == nullptr) {
        text += "window: active " + std::string(yes_no((*request.window).active())) + ", occluded " +
                yes_no((*request.window).occluded()) + "\n";
        return;
    }
    VisualInspectionOptions options{};
    options.maximum_controls = inspect_control_limit;
    options.include_display_operations = false;
    const VisualInspectionSnapshot snapshot = (*request.window).visual_inspection_snapshot(options);
    const VisualControlInspection* const record =
        request.control == nullptr ? nullptr : find_record(snapshot, (*request.control).runtime_id().value);
    if (record == nullptr) {
        text += "no visual record for the node's control (outside the first " + std::to_string(inspect_control_limit) +
                " controls)\n";
        return;
    }
    const VisualControlStateSnapshot& state = (*record).state;
    // The snapshot's node may take focus through a part the control hides
    // (a search field's inner editor): said so, so the two reads agree
    // (MCP round 1).
    const bool node_focusable = has_semantic_state((*request.node).states, SemanticState::focusable);
    std::string focusable = yes_no(state.focusable || node_focusable);
    if (node_focusable && !state.focusable) focusable += " (through an inner part; the control itself is not)";
    if (!node_focusable && state.focusable) focusable += " (the control; its node does not take focus)";
    text += "visible " + std::string(yes_no(state.effectively_visible)) + ", enabled " +
            yes_no(state.effectively_enabled) + ", focusable " + focusable + ", hovered " +
            yes_no(state.hovered) + ", pressed " + yes_no(state.pressed) + ", captured " +
            yes_no(state.pointer_captured) + ", status " + visual_status_name(state.visual_status) + "\n";
    text += "theme " + (*record).theme_id + ((*record).theme_override ? " (overridden)" : "") + ", plane " +
            paint_plane_name((*record).paint_plane) + "\n";
}

void layout_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    if (request.control == nullptr) {
        const Size size = (*request.window).client_size();
        text += "client " + number_text(size.width) + "x" + number_text(size.height) + " at scale " +
                number_text((*request.window).scale()) + "\n";
        return;
    }
    VisualInspectionOptions options{};
    options.maximum_controls = inspect_control_limit;
    options.include_display_operations = false;
    const VisualInspectionSnapshot snapshot = (*request.window).visual_inspection_snapshot(options);
    const VisualControlInspection* const record = find_record(snapshot, (*request.control).runtime_id().value);
    if (record == nullptr) {
        text += "no layout record for the node's control\n";
        return;
    }
    const VisualControlLayoutSnapshot& layout = (*record).layout;
    text += "requested " + rect_text(layout.requested_bounds) + "; arranged " + rect_text(layout.arranged_bounds) +
            "; absolute " + rect_text(layout.absolute_bounds) + "\n";
    text += "client " + rect_text(layout.client_rectangle) + "; visual " + rect_text(layout.visual_bounds) +
            "; clip " + rect_text(layout.effective_clip) + "\n";
    text += "dock " + std::string(dock_style_name(layout.dock)) + ", auto size " + yes_no(layout.auto_size) +
            ", clipped by ancestor " + yes_no(layout.clipped_by_ancestor) + ", fully clipped " +
            yes_no(layout.fully_clipped) + "\n";
}

void paint_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    if (request.control == nullptr) {
        text += "paint names a target\n";
        return;
    }
    VisualInspectionOptions options{};
    options.maximum_controls = inspect_control_limit;
    options.maximum_operations_per_control = inspect_operation_limit;
    options.include_display_operations = true;
    const VisualInspectionSnapshot snapshot = (*request.window).visual_inspection_snapshot(options);
    const VisualControlInspection* const record = find_record(snapshot, (*request.control).runtime_id().value);
    if (record == nullptr) {
        text += "no paint record for the node's control\n";
        return;
    }
    if (!(*record).display_chunk.present()) {
        // No recording at all is not "0 operations" (MCP round 1).
        text += "no paint recorded for this control: it draws nothing of its own (its look may come from its "
                "children or its parent), or it has not been painted yet\n";
        return;
    }
    text += std::to_string((*record).total_display_operations) + " paint operations" +
            ((*record).display_chunk_current ? "" : " (not current: the control changed since it was painted)") +
            "\n";
    for (const VisualPaintOperationSnapshot& operation : (*record).display_operations) {
        text += "  " + std::to_string(operation.command_index) + " " +
                visual_paint_operation_name(operation.operation) + " " + rect_text(operation.rect) + "\n";
    }
    if ((*record).display_operations_truncated) {
        text += "  ... cut at " + std::to_string(inspect_operation_limit) + "\n";
    }
}

void metrics_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    const Window& window = *request.window;
    text += window.metrics_snapshot().to_json();
    text += '\n';
}

void dispatcher_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    const DispatcherSnapshot snapshot = (*request.window).dispatcher_snapshot();
    text += "pending " + std::to_string(snapshot.pending) + " (most " + std::to_string(snapshot.maximum_pending) +
            "), posted " + std::to_string(snapshot.posted) + ", invoked " + std::to_string(snapshot.invoked) +
            ", cancelled " + std::to_string(snapshot.cancelled) + ", faulted " + std::to_string(snapshot.faulted) +
            ", accepting " + yes_no(snapshot.accepting) + "\n";
}

void interaction_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    const InteractionSnapshot snapshot = (*request.window).interaction_snapshot();
    text += "pointer " + number_text(snapshot.pointer_position.x) + "," + number_text(snapshot.pointer_position.y) +
            (snapshot.pointer_inside ? " inside" : " outside") + "; buttons primary " + yes_no(snapshot.primary_down) +
            ", secondary " + yes_no(snapshot.secondary_down) + "; held keys " +
            std::to_string(snapshot.held_keys.size()) + "\n";
    text += "hovered #r" + std::to_string(snapshot.hovered.value) + ", pressed #r" +
            std::to_string(snapshot.pressed.value) + ", captured #r" + std::to_string(snapshot.captured.value) +
            "; hover raised " + yes_no(snapshot.hover_raised) + ", pending " + yes_no(snapshot.hover_pending) + "\n";
}

void session_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    const HostSession* const session = gui_forms::detail::window_session(*request.window);
    if (session == nullptr) {
        text += "no host session\n";
        return;
    }
    text += (*session).snapshot().to_json();
    text += '\n';
}

void schema_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    if (request.node == nullptr) {
        text += "schema names a target\n";
        return;
    }
    const SemanticNode& node = *request.node;
    text += "role " + std::string(semantic_role_name(node.role));
    if (semantic_role_fallback(node.role) != node.role) {
        text += " (falls back to " + std::string(semantic_role_name(semantic_role_fallback(node.role))) + ")";
    }
    text += "; actions";
    if (node.actions.empty()) text += " none";
    for (const SemanticAction action : node.actions) {
        text += ' ';
        text += semantic_action_name(action);
        if (automation::detail::action_destructive_mark(node.stable_id, action)) text += "(destructive)";
    }
    text += '\n';
    if (!node.description.empty()) text += "description: " + node.description + "\n";
    if (!node.tool_tip.empty()) text += "tooltip: " + node.tool_tip + "\n";
}

void clipboard_section(void* context, const InspectRequest& request, std::string& text) {
    static_cast<void>(context);
    // Ruling 24: readable only when the program allows it.
    if (!clipboard_readable_by_model()) {
        text += "the program does not let a model read the clipboard\n";
        return;
    }
    HostServices* const services = (*request.window).host_services();
    if (services == nullptr) {
        text += "no host clipboard\n";
        return;
    }
    const HostClipboardTextResult read = (*services).read_clipboard_text();
    if (!read.status.accepted() || !read.has_text) {
        text += "no text on the clipboard\n";
        return;
    }
    text += "clipboard: " + read.text_utf8 + "\n";
}

// Paints protected nodes' bounds out of a capture (study 9.1): opaque grey
// over each, in the capture's pixels. `origin` is the capture's top left in
// window coordinates.
void paint_out_protected(std::span<const FlatNode> nodes, Point origin, WindowCapture& capture) {
    const double scale = capture.scale > 0.0 ? capture.scale : 1.0;
    for (const FlatNode& node : nodes) {
        if (!node.protected_content()) continue;
        const double left = std::floor((node.bounds.x - origin.x) * scale);
        const double top = std::floor((node.bounds.y - origin.y) * scale);
        const double right = std::ceil((node.bounds.x + node.bounds.width - origin.x) * scale);
        const double bottom = std::ceil((node.bounds.y + node.bounds.height - origin.y) * scale);
        const double width = static_cast<double>(capture.pixel_width);
        const double height = static_cast<double>(capture.pixel_height);
        const std::uint32_t x0 = static_cast<std::uint32_t>(std::clamp(left, 0.0, width));
        const std::uint32_t y0 = static_cast<std::uint32_t>(std::clamp(top, 0.0, height));
        const std::uint32_t x1 = static_cast<std::uint32_t>(std::clamp(right, 0.0, width));
        const std::uint32_t y1 = static_cast<std::uint32_t>(std::clamp(bottom, 0.0, height));
        for (std::uint32_t y = y0; y < y1; ++y) {
            std::byte* const row = capture.bgra.data() + static_cast<std::size_t>(y) * capture.pixel_width * 4U;
            for (std::uint32_t x = x0; x < x1; ++x) {
                std::byte* const pixel = row + static_cast<std::size_t>(x) * 4U;
                pixel[0] = std::byte{0x80};
                pixel[1] = std::byte{0x80};
                pixel[2] = std::byte{0x80};
                pixel[3] = std::byte{0xFF};
            }
        }
    }
}

// Premultiplied source over: `layer` onto `canvas` with its top left at
// pixel (left, top) of the canvas, clipped to it.
void compose_over(WindowCapture& canvas, const WindowCapture& layer, std::int64_t left, std::int64_t top) noexcept {
    const std::int64_t canvas_width = static_cast<std::int64_t>(canvas.pixel_width);
    const std::int64_t canvas_height = static_cast<std::int64_t>(canvas.pixel_height);
    const std::size_t needed = static_cast<std::size_t>(layer.pixel_width) * layer.pixel_height * 4U;
    if (layer.bgra.size() < needed) return;
    for (std::uint32_t row = 0U; row < layer.pixel_height; ++row) {
        const std::int64_t target_row = top + static_cast<std::int64_t>(row);
        if (target_row < 0 || target_row >= canvas_height) continue;
        for (std::uint32_t column = 0U; column < layer.pixel_width; ++column) {
            const std::int64_t target_column = left + static_cast<std::int64_t>(column);
            if (target_column < 0 || target_column >= canvas_width) continue;
            const std::byte* const source =
                layer.bgra.data() + (static_cast<std::size_t>(row) * layer.pixel_width + column) * 4U;
            std::byte* const target =
                canvas.bgra.data() +
                (static_cast<std::size_t>(target_row) * canvas.pixel_width + static_cast<std::size_t>(target_column)) *
                    4U;
            const std::uint32_t alpha = std::to_integer<std::uint32_t>(source[3]);
            const std::uint32_t keep = 255U - alpha;
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                const std::uint32_t over = std::to_integer<std::uint32_t>(source[channel]);
                const std::uint32_t under = std::to_integer<std::uint32_t>(target[channel]);
                const std::uint32_t mixed = std::min<std::uint32_t>(255U, over + (under * keep + 127U) / 255U);
                target[channel] = static_cast<std::byte>(mixed);
            }
        }
    }
}

// One Popup window's pixels and where its top left lies, in the owner's
// client coordinates (logical units).
struct PopupLayer final {
    WindowCapture capture{};
    Point origin{};
    std::size_t slot{};
};

// The largest composite side, in pixels.
constexpr std::int64_t maximum_composite_pixels = 16384;

// `capture` (the owner's) with its showing Popup windows and its tooltip's
// Splash window over it, in screen order (a menu's depths open in turn,
// the tooltip on top), as the user sees them: the canvas
// grows to take in each popup, clear where nothing draws, and each popup's
// password fields are painted out first. Appends the slots composited to
// `names`; returns how many were. The owner's pixels move right or down
// when a popup lies left of or above its client area.
std::size_t composite_popups(AutomationCore& core, Window& window, WindowCapture& capture, std::string& names) {
    const double scale = capture.scale > 0.0 ? capture.scale : 1.0;
    std::vector<PopupLayer> layers{};
    for (std::size_t slot = 0U; slot < Window::shown_popup_window_count; ++slot) {
        const ShownPopupWindow shown = window.shown_popup_window(slot);
        Window* const model = shown.model;
        if (model == nullptr) continue;
        const Rect* const placed = &shown.bounds;
        WindowCaptureOptions options{};
        options.include_overlays = true;
        options.scale = scale;
        std::optional<WindowCapture> popup = (*model).capture_window(options);
        if (!popup.has_value()) continue;
        std::vector<FlatNode> nodes{};
        core.flatten_window(*model, nodes);
        paint_out_protected(nodes, Point{}, *popup);
        PopupLayer layer{};
        layer.capture = std::move(*popup);
        layer.origin = Point{(*placed).x, (*placed).y};
        layer.slot = slot;
        layers.push_back(std::move(layer));
    }
    if (layers.empty()) return 0U;
    std::int64_t left = 0;
    std::int64_t top = 0;
    std::int64_t right = static_cast<std::int64_t>(capture.pixel_width);
    std::int64_t bottom = static_cast<std::int64_t>(capture.pixel_height);
    for (const PopupLayer& layer : layers) {
        const std::int64_t x = static_cast<std::int64_t>(std::llround(layer.origin.x * scale));
        const std::int64_t y = static_cast<std::int64_t>(std::llround(layer.origin.y * scale));
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x + static_cast<std::int64_t>(layer.capture.pixel_width));
        bottom = std::max(bottom, y + static_cast<std::int64_t>(layer.capture.pixel_height));
    }
    if (right - left > maximum_composite_pixels || bottom - top > maximum_composite_pixels) return 0U;
    WindowCapture canvas{};
    canvas.scale = capture.scale;
    canvas.pixel_width = static_cast<std::uint32_t>(right - left);
    canvas.pixel_height = static_cast<std::uint32_t>(bottom - top);
    canvas.logical_size = Size{static_cast<double>(canvas.pixel_width) / scale,
                               static_cast<double>(canvas.pixel_height) / scale};
    canvas.bgra.assign(static_cast<std::size_t>(canvas.pixel_width) * canvas.pixel_height * 4U, std::byte{0});
    compose_over(canvas, capture, -left, -top);
    for (const PopupLayer& layer : layers) {
        const std::int64_t x = static_cast<std::int64_t>(std::llround(layer.origin.x * scale));
        const std::int64_t y = static_cast<std::int64_t>(std::llround(layer.origin.y * scale));
        compose_over(canvas, layer.capture, x - left, y - top);
        names += names.empty() ? "" : ", ";
        names += layer.slot == Window::maximum_popup_windows ? std::string("tooltip") : "popup " + std::to_string(layer.slot);
        names += " at " + number_text(layer.origin.x) + "," +
                 number_text(layer.origin.y);
    }
    capture = std::move(canvas);
    return layers.size();
}

const SemanticNode* find_semantic_node(const SemanticSnapshot&& snapshot, std::uint64_t runtime) = delete;

// The SemanticNode a flat node came from, by runtime ID, in `snapshot`
// (an explicit work list).
const SemanticNode* find_semantic_node(const SemanticSnapshot& snapshot, std::uint64_t runtime) {
    std::vector<const SemanticNode*> work{};
    for (const SemanticNode& root : snapshot.roots) work.push_back(&root);
    while (!work.empty()) {
        const SemanticNode* const node = work.back();
        work.pop_back();
        if ((*node).runtime_id == runtime) return node;
        for (const SemanticNode& child : (*node).children) work.push_back(&child);
    }
    return nullptr;
}

} // namespace

bool window_idle(Window& window, FrameTime now, std::chrono::milliseconds horizon, std::string& busy) {
    const DispatcherSnapshot dispatcher = window.dispatcher_snapshot();
    if (dispatcher.pending != 0U) {
        busy = std::to_string(dispatcher.pending) + " posted callbacks waiting";
        return false;
    }
    // The band's motion (the in-use sign itself) and any ambient animation
    // never hold idle: neither their frames nor their wakes.
    if (window.needs_settling_frame()) {
        busy = "a frame is wanted (layout, paint or animation)";
        return false;
    }
    const stxlib::Optional<FrameTime> wake = window.next_settling_wake();
    if (const FrameTime* const due = wake.value()) {
        if (*due < now + horizon) {
            const std::int64_t milliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(*due - now).count();
            busy = "a timer is due in " + std::to_string(milliseconds < 0 ? 0 : milliseconds) + " ms";
            return false;
        }
    }
    return true;
}

std::uint64_t paint_passes_of(const Window& window) {
    const std::uint64_t passes = window.metrics_snapshot().paint_passes;
    return passes;
}

void append_window_line(AutomationCore& core, Window& window, std::string& text) {
    const Size size = window.client_size();
    // A window run modally reads as the dialog it is (round 2), its
    // controls under it.
    text += gui_forms::detail::shown_modally(window) ? "dialog (modal window) " : "window ";
    const std::string title = window.text.read();
    if (title.empty()) {
        // An empty title reads as one, not as a missing value (MCP round 1).
        text += "(untitled)";
    } else {
        append_json_string(title, text);
    }
    text += " [" + core.window_stable_id(window) + "]";
    if (window.active()) text += " active";
    text += " " + number_text(size.width) + "x" + number_text(size.height) + "\n";
}

ToolReply snapshot_tool(AutomationCore& core, Window& window, const JsonValue& arguments) {
    std::vector<FlatNode> nodes{};
    core.flatten_window(window, nodes);
    std::size_t root = no_parent;
    std::optional<std::string> target = text_member(arguments, "root");
    if (target.has_value()) {
        const ResolvedTarget resolved = resolve_target(nodes, *target, std::string_view{});
        if (resolved.resolution != Resolution::found) {
            const ToolError error =
                resolved.resolution == Resolution::ambiguous ? ToolError::ambiguous : ToolError::not_found;
            return ToolReply::failure(error, "no single node [" + *target + "]", resolved.hint);
        }
        root = resolved.index;
    }
    OutlineOptions options{};
    options.root = root;
    const std::optional<std::string> filter = text_member(arguments, "filter");
    if (filter.has_value()) options.filter = *filter;
    const std::optional<double> limit = number_member(arguments, "limit");
    const AutomationSettings& settings = core.settings();
    options.node_limit = settings.default_node_limit;
    if (limit.has_value() && *limit >= 1.0) {
        options.node_limit = std::min(static_cast<std::size_t>(*limit), settings.maximum_node_limit);
    }
    const std::optional<bool> bounds = boolean_member(arguments, "bounds");
    options.with_bounds = bounds.has_value() && *bounds;
    const std::optional<bool> actions = boolean_member(arguments, "actions");
    options.with_actions = !actions.has_value() || *actions;
    std::string text{};
    append_window_line(core, window, text);
    append_outline(nodes, options, text);
    ToolReply reply = ToolReply::success(std::move(text));
    return reply;
}

ToolReply text_read_tool(AutomationCore& core, Window& window, const JsonValue& arguments) {
    std::vector<FlatNode> nodes{};
    core.flatten_window(window, nodes);
    ToolReply refusal{};
    std::size_t index = no_parent;
    if (!resolve_optional_target(nodes, arguments, index, refusal)) return refusal;
    if (index == no_parent) return ToolReply::failure(ToolError::invalid_arguments, "text names a target");
    const FlatNode& node = nodes[index];
    if (node.protected_content()) {
        return ToolReply::failure(ToolError::protected_content, node_reference(node) + " is a password field",
                                  "its value is never read; type into it with keyboard if told to fill it");
    }
    SemanticTextLayout layout{};
    const SemanticTextResult result = node.is_virtual ? SemanticTextResult::no_text
                                                      : window.semantic_text_layout(RuntimeId{node.runtime_id}, layout);
    if (result == SemanticTextResult::stale_identity) {
        return ToolReply::failure(ToolError::stale_identity, node_reference(node) + " is gone", "take a new snapshot");
    }
    if (result != SemanticTextResult::done) {
        // Not editable text: the node's whole name, value and description
        // (MCP round 1: a label cut in the outline, static text).
        const SemanticSnapshot snapshot = window.semantic_snapshot();
        const SemanticNode* const source = find_semantic_node(snapshot, node.runtime_id);
        std::string text = node_reference(node) + " " + semantic_role_name(node.role) + "\nname ";
        append_json_string(node.name, text);
        text += "\nvalue ";
        append_json_string(node.value, text);
        if (source != nullptr && !(*source).description.empty()) {
            text += "\ndescription ";
            append_json_string((*source).description, text);
        }
        text += '\n';
        return ToolReply::success(std::move(text));
    }
    std::string text = node_reference(node) + " value ";
    append_json_string(node.value, text);
    text += '\n';
    // The selection is published on the snapshot's node.
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* const source = find_semantic_node(snapshot, node.runtime_id);
    if (source != nullptr) {
        if (const SemanticTextSelection* const selection = (*source).text_selection.value()) {
            text += "selection anchor " + std::to_string((*selection).anchor.value()) + ", caret " +
                    std::to_string((*selection).caret.value()) + " (UTF-8 bytes)\n";
        }
    }
    constexpr std::size_t maximum_lines = 200U;
    for (std::size_t line = 0U; line < layout.lines.size() && line < maximum_lines; ++line) {
        const TextRange<Utf8Offset> range = layout.lines[line].range;
        const std::size_t start = std::min(static_cast<std::size_t>(range.start.value()), node.value.size());
        const std::size_t end = std::min(static_cast<std::size_t>(range.end.value()), node.value.size());
        text += "line " + std::to_string(line + 1U) + " [" + std::to_string(start) + ".." + std::to_string(end) + "] ";
        append_json_string(std::string_view(node.value).substr(start, end - start), text);
        text += '\n';
    }
    if (layout.lines.size() > maximum_lines) {
        text += "... " + std::to_string(layout.lines.size() - maximum_lines) + " more lines\n";
    }
    ToolReply reply = ToolReply::success(std::move(text));
    return reply;
}

ToolReply capture_tool(AutomationCore& core, Window& window, const JsonValue& arguments) {
    const std::optional<std::string> mode = text_member(arguments, "mode");
    if (mode.has_value() && *mode == "presented") {
        return ToolReply::failure(ToolError::unsupported_action,
                                  "the presented pixels are not reachable through the portable host seam yet",
                                  "capture mode rendered draws the current state afresh");
    }
    std::vector<FlatNode> nodes{};
    core.flatten_window(window, nodes);
    ToolReply refusal{};
    std::size_t index = no_parent;
    if (!resolve_optional_target(nodes, arguments, index, refusal)) return refusal;
    const std::optional<double> scale = number_member(arguments, "scale");
    const std::optional<bool> overlays = boolean_member(arguments, "include_overlays");
    // The in-use sign (the band, AI mode's stripes) only when asked: by
    // default the model sees the program as the user does without a model
    // (round 2: captures were near-greyscale while a model was connected).
    const std::optional<bool> with_sign = boolean_member(arguments, "include_sign");
    const bool sign = with_sign.has_value() && *with_sign;
    std::optional<WindowCapture> capture{};
    Point origin{};
    std::string what{};
    if (index == no_parent) {
        WindowCaptureOptions options{};
        options.include_overlays = !overlays.has_value() || *overlays;
        options.include_model_sign = sign;
        if (scale.has_value() && *scale > 0.0 && *scale <= 8.0) options.scale = *scale;
        capture = window.capture_window(options);
        what = "window [" + core.window_stable_id(window) + "]";
    } else {
        // A virtual child is captured through the control that publishes it.
        const FlatNode& node = nodes[index];
        Control::Ptr control{};
        for (const FlatNode& candidate : nodes) {
            if (candidate.runtime_id == node.control_runtime_id && !candidate.stable_id.empty()) {
                control = window.find(candidate.stable_id);
                break;
            }
        }
        if (control == nullptr) {
            return ToolReply::failure(ToolError::unsupported_action,
                                      node_reference(node) + "'s control has no stable ID to capture it by",
                                      "capture the window instead");
        }
        ControlCaptureOptions options{};
        options.include_overlays = !overlays.has_value() || *overlays;
        options.include_model_sign = sign;
        if (scale.has_value() && *scale > 0.0 && *scale <= 8.0) options.scale = *scale;
        const std::optional<double> margin = number_member(arguments, "margin");
        if (margin.has_value() && *margin >= 0.0 && *margin <= 64.0) options.margin = *margin;
        capture = window.capture_control(control, options);
        origin = Point{node.bounds.x - options.margin, node.bounds.y - options.margin};
        origin.x = std::max(origin.x, 0.0);
        origin.y = std::max(origin.y, 0.0);
        what = node_reference(node);
    }
    if (!capture.has_value()) {
        return ToolReply::failure(ToolError::unsupported_action, "the window could not be captured now",
                                  "wait until idle and try again");
    }
    paint_out_protected(nodes, origin, *capture);
    // The owner alone would miss what the user sees in its Popup windows
    // (menus, drop-downs): a window capture composites them over it.
    std::string popups{};
    const std::optional<bool> with_popups = boolean_member(arguments, "popups");
    std::size_t composited = 0U;
    if (index == no_parent && (!with_popups.has_value() || *with_popups)) {
        composited = composite_popups(core, window, *capture, popups);
    }
    ToolImage image{};
    if (!encode_window_capture_png(*capture, image.bytes)) {
        return ToolReply::failure(ToolError::internal, "the capture could not be encoded");
    }
    ToolSuccess success{};
    success.text = "capture of " + what + ": " + std::to_string((*capture).pixel_width) + "x" +
                   std::to_string((*capture).pixel_height) + " px at scale " + number_text((*capture).scale) +
                   (sign ? " (rendered, with the in-use sign)" : " (rendered, without the in-use sign)");
    if (composited != 0U) success.text += " with its popup windows over it: " + popups;
    success.images.push_back(std::move(image));
    ToolReply reply = ToolReply::success(std::move(success));
    return reply;
}

ToolReply inspect_tool(AutomationCore& core, Window& window, const JsonValue& arguments,
                       std::string_view arguments_json) {
    std::vector<FlatNode> nodes{};
    core.flatten_window(window, nodes);
    ToolReply refusal{};
    std::size_t index = no_parent;
    if (!resolve_optional_target(nodes, arguments, index, refusal)) return refusal;
    std::vector<std::string> sections{};
    const JsonValue* const what = kind_member(arguments, "what", JsonKind::array);
    if (what != nullptr) {
        for (const JsonValue& item : (*what).items) {
            if (item.kind == JsonKind::string) sections.push_back(item.text);
        }
    }
    if (sections.empty()) {
        if (index == no_parent) {
            sections = {"metrics", "dispatcher", "interaction"};
        } else {
            sections = {"state", "layout", "schema"};
        }
    }
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    InspectRequest request{};
    request.window = &window;
    request.arguments_json = arguments_json.data();
    request.arguments_bytes = arguments_json.size();
    Control::Ptr control{};
    if (index != no_parent) {
        request.node = find_semantic_node(snapshot, nodes[index].runtime_id);
        for (const FlatNode& candidate : nodes) {
            if (candidate.runtime_id == nodes[index].control_runtime_id && !candidate.stable_id.empty()) {
                control = window.find(candidate.stable_id);
                break;
            }
        }
        request.control = control.get();
    }
    std::string text{};
    for (const std::string& name : sections) {
        InspectSectionEntry entry{};
        if (!core.tools().find_inspect_section(name, entry)) {
            std::vector<InspectSectionEntry> known{};
            core.tools().fill_inspect_sections(known);
            std::string hint = "sections:";
            for (const InspectSectionEntry& section : known) hint += " " + section.name;
            return ToolReply::failure(ToolError::invalid_arguments, "no inspect section " + name, hint);
        }
        text += "== " + name + "\n";
        std::string section = call_inspect_section(entry, request);
        if (section.size() > inspect_section_limit) {
            section.resize(inspect_section_limit);
            section += "\n... cut at " + std::to_string(inspect_section_limit) + " bytes\n";
        }
        text += section;
    }
    ToolReply reply = ToolReply::success(std::move(text));
    return reply;
}

void read_condition(const JsonValue& arguments, NodeCondition& condition) {
    const std::optional<std::string> target = text_member(arguments, "target");
    if (target.has_value()) condition.target = *target;
    const std::optional<std::string> within = text_member(arguments, "within");
    if (within.has_value()) condition.within = *within;
    const std::optional<std::string> state = text_member(arguments, "state");
    if (state.has_value()) condition.state = *state;
    condition.exists = boolean_member(arguments, "exists");
    condition.value = text_member(arguments, "value");
}

bool condition_holds(std::span<const FlatNode> nodes, const NodeCondition& condition, std::string& observed) {
    const ResolvedTarget resolved = resolve_target(nodes, condition.target, condition.within);
    const bool exists = resolved.resolution == Resolution::found;
    if (condition.exists.has_value() && *condition.exists != exists) {
        observed = exists ? "[" + condition.target + "] exists" : "[" + condition.target + "] does not exist";
        return false;
    }
    if (!exists) {
        observed = "[" + condition.target + "] does not exist";
        const bool absent_wanted = condition.exists.has_value() && !*condition.exists;
        return absent_wanted;
    }
    const FlatNode& node = nodes[resolved.index];
    if (!condition.state.empty()) {
        const bool clear = condition.state[0] == '-';
        const std::string name = clear ? condition.state.substr(1U) : condition.state;
        bool has = false;
        bool known = false;
        if (name == "hovered") {
            has = node.hovered;
            known = true;
        } else if (name == "pressed") {
            has = node.pressed;
            known = true;
        } else {
            for (const SemanticState state : semantic_states) {
                const std::string state_name = state == SemanticState::protected_content
                                                   ? std::string("protected")
                                                   : std::string(semantic_state_name(state));
                if (state_name != name) continue;
                known = true;
                // Focus on a password field is never exposed (study 9.1).
                has = !(state == SemanticState::focused && node.protected_content()) &&
                      has_semantic_state(node.states, state);
            }
        }
        if (!known || has == clear) {
            observed = node_reference(node) + (known ? (has ? " is " : " is not ") + name : " has no state " + name);
            return false;
        }
    }
    if (condition.value.has_value() && !node.protected_content() && node.value != *condition.value) {
        observed = node_reference(node) + " value \"" + node.value + "\"";
        return false;
    }
    observed = node_reference(node) + " as expected";
    return true;
}

void register_core_inspect_sections(ToolRegistry& tools) {
    static_cast<void>(tools.add_inspect_section("state", "the control's visual state: visible, enabled, hovered, pressed",
                                                &string_section<&state_section>, nullptr));
    static_cast<void>(tools.add_inspect_section("layout", "the control's requested, arranged and clipped bounds",
                                                &string_section<&layout_section>, nullptr));
    static_cast<void>(tools.add_inspect_section("paint", "the control's recorded paint operations", &string_section<&paint_section>,
                                                nullptr));
    static_cast<void>(tools.add_inspect_section("metrics", "the window's counters", &string_section<&metrics_section>, nullptr));
    static_cast<void>(tools.add_inspect_section("dispatcher", "the window's posted work", &string_section<&dispatcher_section>,
                                                nullptr));
    static_cast<void>(tools.add_inspect_section("interaction", "pointer, buttons, keys, hover and capture",
                                                &string_section<&interaction_section>, nullptr));
    static_cast<void>(tools.add_inspect_section("session", "the host session's counters", &string_section<&session_section>, nullptr));
    static_cast<void>(tools.add_inspect_section("schema", "the node's role, actions and descriptions",
                                                &string_section<&schema_section>, nullptr));
    static_cast<void>(tools.add_inspect_section("clipboard", "the clipboard's text, when the program allows it",
                                                &string_section<&clipboard_section>, nullptr));
}

} // namespace gui_forms::automation::detail
