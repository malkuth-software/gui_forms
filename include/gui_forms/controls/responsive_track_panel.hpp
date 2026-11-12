#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/child_property_view/child_property_view.hpp"
#include "gui_forms/controls/container_control.hpp"
#include "gui_forms/controls/responsive_track_panel_values.hpp"
#include "gui_forms/controls/responsive_track_panel_capabilities.hpp"

#include <array>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gui_forms {

enum class ResponsiveTrackSizeMode : std::uint8_t {
    fixed,
    content,
    remaining,
};

enum class ResponsiveTrackCollapseReason : std::uint8_t {
    none,
    authored_hidden,
    insufficient_extent,
};

// A zero maximum is unbounded, matching Control::maximum_size. Collapse is
// never inferred from content identity: an authored priority explicitly opts
// the track into automatic collapse. Smaller priority values collapse first.
struct GUI_FORMS_API ResponsiveTrackSpec final {
    ResponsiveTrackSizeMode size_mode{ResponsiveTrackSizeMode::content};
    double minimum{};
    double preferred{};
    double maximum{};
    double remaining_weight{1.0};
    bool hidden{};
    std::optional<std::uint16_t> collapse_priority;

    friend bool operator==(const ResponsiveTrackSpec& left,
                           const ResponsiveTrackSpec& right) {
        return left.size_mode == right.size_mode &&
               left.minimum == right.minimum &&
               left.preferred == right.preferred &&
               left.maximum == right.maximum &&
               left.remaining_weight == right.remaining_weight &&
               left.hidden == right.hidden &&
               left.collapse_priority == right.collapse_priority;
    }
};

struct GUI_FORMS_API ResponsiveTrackResult final {
    double measured_content{};
    double minimum{};
    double desired{};
    double allocated{};
    double logical_start{};
    double logical_end{};
    std::int64_t device_start{};
    std::int64_t device_end{};
    double collapse_threshold{};
    ResponsiveTrackCollapseReason collapse_reason{
        ResponsiveTrackCollapseReason::none};
    bool collapse_protected{};

    [[nodiscard]] bool collapsed() const noexcept {
        return collapse_reason != ResponsiveTrackCollapseReason::none;
    }
    friend bool operator==(const ResponsiveTrackResult& left,
                           const ResponsiveTrackResult& right) noexcept {
        return left.measured_content == right.measured_content &&
               left.minimum == right.minimum &&
               left.desired == right.desired &&
               left.allocated == right.allocated &&
               left.logical_start == right.logical_start &&
               left.logical_end == right.logical_end &&
               left.device_start == right.device_start &&
               left.device_end == right.device_end &&
               left.collapse_threshold == right.collapse_threshold &&
               left.collapse_reason == right.collapse_reason &&
               left.collapse_protected == right.collapse_protected;
    }
};

struct GUI_FORMS_API ResponsiveTrackResolution final {
    double available_extent{};
    double gap{};
    double device_scale{1.0};
    double minimum_extent{};
    double desired_extent{};
    double allocated_extent{};
    bool overflow{};
    std::vector<std::size_t> collapse_order;
    std::vector<ResponsiveTrackResult> tracks;

    friend bool operator==(const ResponsiveTrackResolution& left,
                           const ResponsiveTrackResolution& right) {
        return left.available_extent == right.available_extent &&
               left.gap == right.gap &&
               left.device_scale == right.device_scale &&
               left.minimum_extent == right.minimum_extent &&
               left.desired_extent == right.desired_extent &&
               left.allocated_extent == right.allocated_extent &&
               left.overflow == right.overflow &&
               left.collapse_order == right.collapse_order &&
               left.tracks == right.tracks;
    }
};

// Renderer-free one-dimensional solver. Content measurements and protected
// tracks are explicit inputs. Logical allocation is independent of display
// scale; only cumulative output boundaries are snapped to device pixels.
[[nodiscard]] GUI_FORMS_API ResponsiveTrackResolution resolve_responsive_tracks(
    std::span<const ResponsiveTrackSpec> specs,
    std::span<const double> measured_content,
    double available_extent,
    double gap,
    double device_scale,
    std::span<const std::size_t> collapse_protected_tracks = {});

struct GUI_FORMS_API ResponsiveLayoutSnapshot final {
    ResponsiveTrackOrientation orientation{
        ResponsiveTrackOrientation::horizontal};
    ResponsiveTrackResolution resolution;
    LayoutTransactionState transaction;
    std::uint64_t committed_resolution_revision{};
};

// The track a track panel provides to its children (a child property,
// study sections 10 and 15.4): the authored track index, or none for the
// next track no child names. `(*tracks).child_track[*child] = 2U;`
template <typename Declaring>
struct ChildTrackCapability {
    ChildPropertyView<std::optional<std::uint32_t>, ChildTrackCapability<Declaring>> child_track;

    static void assign(ChildTrackCapability& capability, const Control& child, std::optional<std::uint32_t> value) {
        Declaring& container = static_cast<Declaring&>(capability);
        container.prepare_write(child, value);
        if (!container.replace_child_track(child, value)) return;
        container.written(child);
    }

    static std::optional<std::uint32_t> read(const ChildTrackCapability& capability, const Control& child) {
        const Declaring& container = static_cast<const Declaring&>(capability);
        const std::optional<std::uint32_t> value = container.stored_child_track(child);
        return value;
    }
};

// Nest horizontal and vertical instances to form a retained grid. Each direct
// child occupies one authored track; multiple children may intentionally share
// a track. The panel never changes Control::visible(). Automatic collapse is a
// separate effective-layout state and therefore leaves author visibility
// intact while removing the subtree from paint, hit testing, focus, and
// semantics.
class GUI_FORMS_API ResponsiveTrackPanel final : public ContainerControl,
                                   public ChildTrackCapability<ResponsiveTrackPanel>, public ResponsiveTrackPanelCapabilities<ResponsiveTrackPanel> {
public:
    explicit ResponsiveTrackPanel(StableId stable_id);

    // The child properties it provides: child_track.
    static constexpr std::size_t track_child_property_count = 1U;
    [[nodiscard]] static const std::array<ChildPropertyEntry, track_child_property_count>&
    track_child_property_entries() noexcept;
    [[nodiscard]] std::span<const ChildPropertyEntry> child_property_table() const noexcept override;

    [[nodiscard]] std::span<const ResponsiveTrackSpec> track_specs() const& noexcept {
        return track_specs_;
    }
    std::span<const ResponsiveTrackSpec> track_specs() const&& = delete;
    void set_track_specs(std::vector<ResponsiveTrackSpec> specs);
    void set_track_spec(std::size_t track, ResponsiveTrackSpec spec);

    // A revealed track is protected from automatic collapse until the author
    // clears the request. This is the explicit focus-reveal seam; protection
    // may honestly produce overflow when no authored alternative can collapse.
    void set_track_revealed(std::size_t track, bool revealed);
    [[nodiscard]] bool track_revealed(std::size_t track) const noexcept;

    // If a currently focused track truly cannot fit after other authored
    // priorities collapse, this exact retained target receives focus before
    // the track becomes effectively unavailable. Without a live eligible
    // target the focused track remains protected and overflow is reported.
    void set_track_focus_fallback(std::size_t track, Control& target);
    void clear_track_focus_fallback(std::size_t track);

    [[nodiscard]] ResponsiveLayoutSnapshot layout_snapshot() const;

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void child_leaving(const Control& child) noexcept override;

private:
    friend struct ChildTrackCapability<ResponsiveTrackPanel>;

    [[nodiscard]] BindingValue track_specs_property() const;
    void set_track_specs_property(const BindingValue& value);

    // The child track's hooks: the container's thread, a live direct child
    // and a track inside the specs before the compare; relayout after.
    void prepare_write(const Control& child, const std::optional<std::uint32_t>& incoming) const;
    void written(const Control& child);
    // Stores `child`'s track (none erases it); false when it was already `value`.
    bool replace_child_track(const Control& child, std::optional<std::uint32_t> value);
    // `child`'s authored track; refuses a child that is not a live direct child.
    [[nodiscard]] std::optional<std::uint32_t> stored_child_track(const Control& child) const;

    using ChildTrackMap = std::unordered_map<std::uint64_t, std::size_t>;
    using FocusFallbackMap =
        std::unordered_map<std::size_t, std::weak_ptr<Control>>;

    [[nodiscard]] ResponsiveTrackResolution layout_children(
        Size available, bool assign);
    void reconcile_metadata();
    static void validate_specs(std::span<const ResponsiveTrackSpec> specs);

    std::vector<ResponsiveTrackSpec> track_specs_{ResponsiveTrackSpec{}};
    ChildTrackMap child_tracks_;
    FocusFallbackMap focus_fallbacks_;
    std::unordered_set<std::size_t> revealed_tracks_;
    ResponsiveTrackResolution committed_resolution_;
    std::uint64_t committed_resolution_revision_{};
    // property_codemod begin ResponsiveTrackPanel (tools/property_families/responsive_track_panel.json; generated, do not edit)
public:
    // ResponsiveTrackPanel's property table: Control's entries, then its own.
    static constexpr std::size_t responsive_track_panel_property_count = Control::control_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, responsive_track_panel_property_count>& responsive_track_panel_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct ResponsiveTrackAxisCapability<ResponsiveTrackPanel>;
    friend struct ResponsiveTrackGapCapability<ResponsiveTrackPanel>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(orientation)& cell, ResponsiveTrackOrientation& incoming);
    void written(const decltype(orientation)& cell);
    void prepare_write(const decltype(track_gap)& cell, double& incoming);
    void written(const decltype(track_gap)& cell);
    // property_codemod end ResponsiveTrackPanel
};

} // namespace gui_forms
