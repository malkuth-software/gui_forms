#include "model_inspection.hpp"

#include "gui_forms/binding/value/binding_value.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gui_forms::model_inspection {

namespace {

[[nodiscard]] bool has_state(const SemanticNode& node, SemanticState state) noexcept {
    const bool set = (static_cast<std::uint32_t>(node.states) & static_cast<std::uint32_t>(state)) != 0U;
    return set;
}

// How a node is named in the summary: its accessible name, else its value,
// else its stable ID, after its role.
[[nodiscard]] std::string node_label(const SemanticNode& node) {
    std::string label = semantic_role_name(node.role);
    std::string name = node.name;
    if (name.empty() && !has_state(node, SemanticState::protected_content)) name = node.value;
    if (name.empty()) name = node.stable_id;
    if (name.size() > 48U) {
        name.resize(45U);
        name.append("...");
    }
    if (!name.empty()) label.append(" \"" + name + "\"");
    return label;
}

// Up to maximum_summary_items labels, joined, with "and N more".
class LabelList final {
public:
    void add(const SemanticNode& node) {
        if (labels_.size() < maximum_summary_items) {
            labels_.push_back(node_label(node));
        } else {
            more_ = more_ + 1U;
        }
    }
    [[nodiscard]] bool empty() const noexcept { return labels_.empty(); }
    [[nodiscard]] std::string joined() const {
        std::string text{};
        for (std::size_t index = 0U; index < labels_.size(); ++index) {
            if (index != 0U) text.append(index + 1U == labels_.size() && more_ == 0U ? " and " : ", ");
            text.append(labels_[index]);
        }
        if (more_ != 0U) text.append(" and " + std::to_string(more_) + " more");
        return text;
    }

private:
    std::vector<std::string> labels_{};
    std::size_t more_{};
};

// `text` cut at the last space before `maximum` bytes, with "...".
[[nodiscard]] std::string bounded(std::string text, std::size_t maximum) {
    if (text.size() <= maximum) return text;
    std::size_t cut = maximum - 3U;
    const std::size_t space = text.rfind(' ', cut);
    if (space != std::string::npos && space > maximum / 2U) cut = space;
    // Never inside a UTF-8 sequence.
    while (cut > 0U && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) cut = cut - 1U;
    text.resize(cut);
    text.append("...");
    return text;
}

[[nodiscard]] std::string rect_text(const Rect& rect) {
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), "%g,%g %gx%g", rect.x, rect.y, rect.width, rect.height);
    std::string text(buffer);
    return text;
}

[[nodiscard]] std::optional<Size> size_property(const Control& control, std::string_view name) {
    const std::optional<BindingValue> value = control.property_value(name);
    if (!value) return std::nullopt;
    const Size* const size = std::get_if<Size>(&*value);
    if (size == nullptr) return std::nullopt;
    return *size;
}

// The layout properties a control states itself, when set locally.
constexpr std::string_view own_layout_properties[] = {
    "dock", "anchor", "margin", "padding", "minimum_size", "maximum_size", "auto_size", "auto_size_mode",
};

} // namespace

std::string window_state_summary(const SemanticSnapshot& snapshot, std::size_t maximum_bytes) {
    const std::size_t maximum = maximum_bytes < 64U ? 64U : maximum_bytes;
    if (snapshot.roots.empty()) return std::string("The window has no semantic nodes yet.");
    LabelList focused{};
    LabelList selected{};
    LabelList checked{};
    LabelList expanded{};
    LabelList busy{};
    LabelList invalid{};
    LabelList disabled{};
    LabelList modal{};
    std::size_t visited = 0U;
    std::size_t protected_filled = 0U;
    std::size_t protected_empty = 0U;
    std::vector<const SemanticNode*> pending{};
    for (std::vector<SemanticNode>::const_reverse_iterator root = snapshot.roots.rbegin();
         root != snapshot.roots.rend(); ++root) {
        pending.push_back(&*root);
    }
    // The snapshot's own count bounds the walk; a malformed snapshot stops.
    const std::size_t limit = snapshot.node_count == 0U ? 100000U : snapshot.node_count;
    while (!pending.empty() && visited < limit) {
        const SemanticNode& node = *pending.back();
        pending.pop_back();
        visited = visited + 1U;
        if (has_state(node, SemanticState::visible) || node.states == SemanticState::none) {
            if (has_state(node, SemanticState::focused)) focused.add(node);
            if (has_state(node, SemanticState::selected)) selected.add(node);
            if (has_state(node, SemanticState::checked)) checked.add(node);
            if (has_state(node, SemanticState::expanded)) expanded.add(node);
            if (has_state(node, SemanticState::busy)) busy.add(node);
            if (has_state(node, SemanticState::invalid)) invalid.add(node);
            if (node.modal) modal.add(node);
            const bool control_like = node.role != SemanticRole::generic && !node.actions.empty();
            if (control_like && !has_state(node, SemanticState::enabled)) disabled.add(node);
            if (has_state(node, SemanticState::protected_content)) {
                if (node.value.empty()) {
                    protected_empty = protected_empty + 1U;
                } else {
                    protected_filled = protected_filled + 1U;
                }
            }
        }
        for (std::vector<SemanticNode>::const_reverse_iterator child = node.children.rbegin();
             child != node.children.rend(); ++child) {
            pending.push_back(&*child);
        }
    }
    const SemanticNode& first = snapshot.roots.front();
    std::string text = "A " + node_label(first) + " of " + std::to_string(visited) + " nodes.";
    if (!modal.empty()) text.append(" Waiting on the user: " + modal.joined() + ".");
    text.append(focused.empty() ? std::string(" Nothing has focus.") : " Focus: " + focused.joined() + ".");
    if (!selected.empty()) text.append(" Selected: " + selected.joined() + ".");
    if (!checked.empty()) text.append(" Checked: " + checked.joined() + ".");
    if (!expanded.empty()) text.append(" Expanded: " + expanded.joined() + ".");
    if (!busy.empty()) text.append(" Busy: " + busy.joined() + ".");
    if (!invalid.empty()) text.append(" Invalid: " + invalid.joined() + ".");
    if (!disabled.empty()) text.append(" Disabled: " + disabled.joined() + ".");
    if (protected_filled + protected_empty != 0U) {
        text.append(" Protected fields: " + std::to_string(protected_filled) + " filled, " +
                    std::to_string(protected_empty) + " empty.");
    }
    return bounded(std::move(text), maximum);
}

std::string layout_reasons(const Control& control) {
    std::string text{};
    const std::string_view id = control.stable_id().value();
    const Rect arranged = control.committed_arranged_bounds();
    const Rect requested = control.requested_bounds();
    text.append(std::string(id.empty() ? std::string_view("(no stable ID)") : id) + ": arranged " +
                rect_text(arranged));
    const Control::Ptr parent = control.parent();
    if (parent != nullptr) {
        const std::string_view parent_id = (*parent).stable_id().value();
        text.append(" in " + std::string(parent_id.empty() ? std::string_view("(no stable ID)") : parent_id));
    }
    text.push_back('\n');
    if (!control.effectively_visible()) text.append("not visible: takes no space\n");
    if (arranged.x != requested.x || arranged.y != requested.y || arranged.width != requested.width ||
        arranged.height != requested.height) {
        text.append("the parent's layout changed it from the requested " + rect_text(requested) + "\n");
    }
    for (const std::string_view name : own_layout_properties) {
        if (!control.should_serialize_property(name)) continue;
        const std::optional<BindingValue> value = control.property_value(name);
        if (!value) continue;
        text.append(std::string(name) + " = " + binding_value_to_string(*value) + "\n");
    }
    const std::optional<Size> minimum = size_property(control, "minimum_size");
    const std::optional<Size> maximum = size_property(control, "maximum_size");
    if (minimum && (*minimum).width > 0.0 && arranged.width <= (*minimum).width) {
        text.append("tight: the width is held at minimum_size\n");
    }
    if (minimum && (*minimum).height > 0.0 && arranged.height <= (*minimum).height) {
        text.append("tight: the height is held at minimum_size\n");
    }
    if (maximum && (*maximum).width > 0.0 && arranged.width >= (*maximum).width) {
        text.append("tight: the width is held at maximum_size\n");
    }
    if (maximum && (*maximum).height > 0.0 && arranged.height >= (*maximum).height) {
        text.append("tight: the height is held at maximum_size\n");
    }
    if (parent != nullptr) {
        for (const PropertyDescriptor& descriptor : (*parent).child_property_descriptors()) {
            if (!(*parent).should_serialize_child_property(control, descriptor.name)) continue;
            const std::optional<BindingValue> value = (*parent).child_property_value(control, descriptor.name);
            if (!value) continue;
            text.append("from the parent: " + descriptor.name + " = " + binding_value_to_string(*value) + "\n");
        }
        const Rect space = (*parent).committed_arranged_bounds();
        const double right = arranged.x + arranged.width - space.width;
        const double bottom = arranged.y + arranged.height - space.height;
        if (right > 0.5) text.append("overflows its parent on the right by " + std::to_string(std::lround(right)) + "\n");
        if (bottom > 0.5) {
            text.append("overflows its parent at the bottom by " + std::to_string(std::lround(bottom)) + "\n");
        }
        if (arranged.x < -0.5 || arranged.y < -0.5) text.append("starts outside its parent (negative position)\n");
    }
    return text;
}

std::string resource_summary(const ImageRegistrySnapshot& images, const AllocationStatistics& allocations) {
    std::string text = "images: " + std::to_string(images.resource_count) + " resources, " +
                       std::to_string(images.encoded_bytes) + " encoded bytes, " +
                       std::to_string(images.decoded_bytes) + " decoded bytes (revision " +
                       std::to_string(images.revision) + ")\n";
    for (std::size_t index = 0U; index < allocation_subsystem_count; ++index) {
        const AllocationSubsystem subsystem = static_cast<AllocationSubsystem>(index);
        const AllocationCounts counts = allocations.of(subsystem);
        const std::uint64_t live = counts.allocated_bytes >= counts.released_bytes
                                       ? counts.allocated_bytes - counts.released_bytes
                                       : 0U;
        text.append(std::string(allocation_subsystem_name(subsystem)) + ": " + std::to_string(live) +
                    " bytes live in " + std::to_string(counts.allocations - std::min(counts.allocations, counts.releases)) +
                    " allocations\n");
    }
    return text;
}

} // namespace gui_forms::model_inspection
