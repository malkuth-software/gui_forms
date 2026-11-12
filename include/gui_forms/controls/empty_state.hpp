#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/controls/empty_state_capabilities.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>

namespace gui_forms {

// What a list, a page or a search shows when it has nothing to show (W-4):
// a picture, a title saying what is empty, a description saying why or
// what to do next, and an optional action control, in one column centred
// in the control. The parts use the panel family and draw no face, so the
// container's ground shows through. The title and the description are
// single lines, centred; text wider than the control is clipped.
//
// To assistive technology it is a group named by its title (or its
// accessible name) and described by its description; the action stays
// reachable inside it. It takes no focus or pointer input of its own.
class EmptyState final : public Control, public EmptyStateCapabilities<EmptyState> {
public:
    explicit EmptyState(StableId stable_id);

    // The action control (such as "Create a message"), owned as an ordinary
    // visual child and placed below the description; null for none. The
    // setter returns the detached predecessor. A control that already has a
    // parent throws std::logic_error and changes nothing.
    [[nodiscard]] Control::Ptr action() const noexcept { return action_; }
    [[nodiscard]] Control::Ptr set_action(Control::Ptr control);

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `icon` is the
    // picture, `caption` the title, `label` the description and `content`
    // the action's slot; each is absent while it has nothing to show.
    struct Parts final {
        PartGeometry region;
        PartGeometry icon;
        PartGeometry caption;
        PartGeometry label;
        PartGeometry content;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // Where each part lies in a control of `size`, and the size the column
    // needs. Reads the last measured action size.
    struct Placement final {
        Rect icon{};
        Rect caption{};
        Rect label{};
        Rect content{};
        Size natural{};
    };
    [[nodiscard]] Placement placement(Size size) const;
    [[nodiscard]] FontSpec title_font() const noexcept;
    [[nodiscard]] FontSpec description_font() const noexcept;

    Control::Ptr action_;
    // The action's desired size from the last measure or arrange; zero
    // without one.
    Size action_size_{};
    // property_codemod begin EmptyState (tools/property_families/empty_state.json; generated, do not edit)
public:
    // EmptyState's property table: Control's entries, then its own.
    static constexpr std::size_t empty_state_property_count = Control::control_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, empty_state_property_count>& empty_state_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct EmptyStateImageCapability<EmptyState>;
    friend struct EmptyStateTitleCapability<EmptyState>;
    friend struct EmptyStateDescriptionCapability<EmptyState>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(image)& cell, ImageId& incoming);
    void written(const decltype(image)& cell);
    void prepare_write(const decltype(title)& cell, std::string& incoming);
    void written(const decltype(title)& cell);
    void prepare_write(const decltype(description)& cell, std::string& incoming);
    void written(const decltype(description)& cell);
    // property_codemod end EmptyState
};

} // namespace gui_forms
