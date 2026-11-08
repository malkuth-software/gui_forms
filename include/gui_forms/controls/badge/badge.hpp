#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/controls/badge/badge_values/badge_values.hpp"
#include "gui_forms/controls/badge/badge_capabilities/badge_capabilities.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

// A small count or dot (W-4; owner decision D7: a standalone control, not
// an adornment attached to another control). In count mode the chip shows
// `count`, or `maximum` followed by "+" once the count passes it; a zero
// count hides the chip unless show_zero is set. In dot mode the chip is a
// small dot without text. A badge takes no input and no focus. It is
// static text to assistive technology, named by its accessible name or
// else by the text it shows; a hidden badge is not exposed.
class Badge final : public Control, public BadgeCapabilities<Badge> {
public:
    explicit Badge(StableId stable_id);

    // The text the chip shows now: the count, the maximum and "+" past it,
    // or nothing in dot mode or for a hidden zero.
    [[nodiscard]] std::string display_text() const;
    // Whether the chip is drawn: always in dot mode, and in count mode
    // unless the count is zero and show_zero is off.
    [[nodiscard]] bool showing() const noexcept;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `chip` is the
    // drawn pill or dot and `label` the text inside it; both are absent
    // while the badge is hidden, and `label` in dot mode.
    struct Parts final {
        PartGeometry region;
        PartGeometry chip;
        PartGeometry label;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The chip's size for the current mode and text, before placement: a
    // dot's fixed square, or a pill at least as wide as it is tall.
    [[nodiscard]] Size chip_size(const std::string& shown) const;
    // The chip placed at the start of the client area, centred across it.
    [[nodiscard]] Rect chip_bounds(const std::string& shown) const;
    [[nodiscard]] FontSpec chip_font() const noexcept;
    // property_codemod begin Badge (tools/property_families/badge.json; generated, do not edit)
public:
    // Badge's property table: Control's entries, then its own.
    static constexpr std::size_t badge_property_count = Control::control_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, badge_property_count>& badge_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct BadgeCountCapability<Badge>;
    friend struct BadgeMaximumCapability<Badge>;
    friend struct BadgeModeCapability<Badge>;
    friend struct BadgeShowZeroCapability<Badge>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(count)& cell, std::uint32_t& incoming);
    void written(const decltype(count)& cell);
    void prepare_write(const decltype(maximum)& cell, std::uint32_t& incoming);
    void written(const decltype(maximum)& cell);
    void prepare_write(const decltype(mode)& cell, BadgeMode& incoming);
    void written(const decltype(mode)& cell);
    void prepare_write(const decltype(show_zero)& cell, bool& incoming);
    void written(const decltype(show_zero)& cell);
    // property_codemod end Badge
};

} // namespace gui_forms
