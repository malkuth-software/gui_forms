#pragma once

#include "gui_forms/controls/scrollable_control/container_control/container_control.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
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

// Horizontal retained command-group projection. It owns only layout collapse
// and the overflow actuator's effective availability. Command authority and
// overlay item composition remain with the consumer and can use the stable IDs
// published by layout_snapshot().
class CommandOverflowPanel final : public ContainerControl {
public:
    explicit CommandOverflowPanel(StableId stable_id);

    [[nodiscard]] double group_gap() const noexcept { return group_gap_; }
    void set_group_gap(double gap);
    void set_group_spec(const Control& group, CommandOverflowGroupSpec spec);
    [[nodiscard]] std::optional<CommandOverflowGroupSpec> group_spec(
        const Control& group) const;
    void clear_group_spec(const Control& group);

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

private:
    struct LayoutResult;
    [[nodiscard]] LayoutResult layout_groups(Size available, bool assign);
    void reconcile_metadata();
    static void validate_spec(const CommandOverflowGroupSpec& spec);

    std::unordered_map<std::uint64_t, CommandOverflowGroupSpec> group_specs_;
    std::weak_ptr<Control> overflow_actuator_;
    double group_gap_{};
    CommandOverflowSnapshot committed_snapshot_;
};

} // namespace gui_forms
