#pragma once

#include "gui_forms/binding/child_property_view/child_property_view.hpp"
#include "gui_forms/controls/scrollable_control/container_control/container_control.hpp"
#include "gui_forms/controls/scrollable_control/container_control/command_overflow_panel/command_overflow_panel_capabilities/command_overflow_panel_capabilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace gui_forms {

// One direct child command group. A missing priority makes the group
// non-collapsible. Smaller priorities collapse first; priorities are unique so
// the projection never depends on child coordinates or incidental DOM order.
struct CommandOverflowGroupSpec final {
    double minimum{};
    double preferred{};
    double maximum{};
    std::optional<std::uint16_t> collapse_priority;

    friend bool operator==(const CommandOverflowGroupSpec& left,
                           const CommandOverflowGroupSpec& right) {
        return left.minimum == right.minimum &&
               left.preferred == right.preferred &&
               left.maximum == right.maximum &&
               left.collapse_priority == right.collapse_priority;
    }
};

struct CommandOverflowGroupResult final {
    std::string stable_id;
    double measured{};
    double allocated{};
    bool collapsed{};
    std::optional<std::uint16_t> collapse_priority;

    friend bool operator==(const CommandOverflowGroupResult& left,
                           const CommandOverflowGroupResult& right) {
        return left.stable_id == right.stable_id &&
               left.measured == right.measured &&
               left.allocated == right.allocated &&
               left.collapsed == right.collapsed &&
               left.collapse_priority == right.collapse_priority;
    }
};

struct CommandOverflowSnapshot final {
    double available_extent{};
    double group_gap{};
    bool overflow_actuator_visible{};
    bool overflow{};
    std::uint64_t committed_revision{};
    std::vector<CommandOverflowGroupResult> groups;

    friend bool operator==(const CommandOverflowSnapshot& left,
                           const CommandOverflowSnapshot& right) {
        return left.available_extent == right.available_extent &&
               left.group_gap == right.group_gap &&
               left.overflow_actuator_visible == right.overflow_actuator_visible &&
               left.overflow == right.overflow &&
               left.committed_revision == right.committed_revision &&
               left.groups == right.groups;
    }
};

// The group spec an overflow panel provides to its children (a child
// property, study sections 10 and 15.4): a direct child's sizing and
// collapse priority, or none for a non-collapsible child at its desired
// width. `(*shelf).group_spec[*group] = CommandOverflowGroupSpec{176.0,
// 176.0, 176.0, 2U};` In a `.form` it is text: the three extents, then
// optionally `priority` and the priority, as in "176 176 176 priority 2".
template <typename Declaring>
struct GroupSpecCapability {
    ChildPropertyView<std::optional<CommandOverflowGroupSpec>, GroupSpecCapability<Declaring>> group_spec;

    static void assign(GroupSpecCapability& capability, const Control& child,
                       std::optional<CommandOverflowGroupSpec> value) {
        Declaring& container = static_cast<Declaring&>(capability);
        container.prepare_write(child, value);
        if (!container.replace_group_spec(child, std::move(value))) return;
        container.written(child);
    }

    static std::optional<CommandOverflowGroupSpec> read(const GroupSpecCapability& capability,
                                                        const Control& child) {
        const Declaring& container = static_cast<const Declaring&>(capability);
        std::optional<CommandOverflowGroupSpec> value = container.stored_group_spec(child);
        return value;
    }
};

// Horizontal retained command-group projection. It owns only layout collapse
// and the overflow actuator's effective availability. Command authority and
// overlay item composition remain with the consumer and can use the stable IDs
// published by layout_snapshot().
class CommandOverflowPanel final : public ContainerControl,
                                   public GroupSpecCapability<CommandOverflowPanel>, public CommandOverflowPanelCapabilities<CommandOverflowPanel> {
public:
    explicit CommandOverflowPanel(StableId stable_id);

    // The child properties it provides: group_spec.
    static constexpr std::size_t overflow_child_property_count = 1U;
    [[nodiscard]] static const std::array<ChildPropertyEntry, overflow_child_property_count>&
    overflow_child_property_entries() noexcept;
    [[nodiscard]] std::span<const ChildPropertyEntry> child_property_table() const noexcept override;

    void set_overflow_actuator(Control& control);
    void clear_overflow_actuator();
    [[nodiscard]] Control::Ptr overflow_actuator() const noexcept {
        return overflow_actuator_.lock();
    }

    [[nodiscard]] CommandOverflowSnapshot layout_snapshot() const;

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void child_leaving(const Control& child) noexcept override;

private:
    friend struct GroupSpecCapability<CommandOverflowPanel>;

    // The group spec's hooks: the container's thread, a live direct child
    // that is not the actuator, valid extents, a unique priority and the
    // group limit before the compare; relayout after a change.
    void prepare_write(const Control& child, const std::optional<CommandOverflowGroupSpec>& incoming) const;
    void written(const Control& child);
    // Stores `child`'s spec (none erases it); false when it was already `value`.
    bool replace_group_spec(const Control& child, std::optional<CommandOverflowGroupSpec> value);
    // `child`'s spec; refuses a child that is not a live direct child.
    [[nodiscard]] std::optional<CommandOverflowGroupSpec> stored_group_spec(const Control& child) const;

    struct LayoutResult;
    [[nodiscard]] LayoutResult layout_groups(Size available, bool assign);
    void reconcile_metadata();
    static void validate_spec(const CommandOverflowGroupSpec& spec);

    std::unordered_map<std::uint64_t, CommandOverflowGroupSpec> group_specs_;
    std::weak_ptr<Control> overflow_actuator_;
    CommandOverflowSnapshot committed_snapshot_;
    // property_codemod begin CommandOverflowPanel (tools/property_families/command_overflow_panel.json; generated, do not edit)
public:
    // CommandOverflowPanel's property table: Control's entries, then its own.
    static constexpr std::size_t command_overflow_panel_property_count = Control::control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, command_overflow_panel_property_count>& command_overflow_panel_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct CommandOverflowGroupGapCapability<CommandOverflowPanel>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(group_gap)& cell, double& incoming);
    void written(const decltype(group_gap)& cell);
    // property_codemod end CommandOverflowPanel
};

} // namespace gui_forms
