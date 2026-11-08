#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/controls/info_banner/info_banner_values/info_banner_values.hpp"
#include "gui_forms/controls/info_banner/info_banner_capabilities/info_banner_capabilities.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// A message strip across a form or a page (W-4): an icon and a keyline in
// the severity's colour, a title and a message on a line each, an optional
// action control, and a close actuator while closable. Its face is the
// `notice` family. The severity is the banner's own custom state, exactly
// one of information, warning, success and error at a time, so a theme
// reads `:family(notice):state(warning)::part(keyline)`. Until T4's floor
// gives the status colours roles, the keyline and the icon are drawn in a
// built-in colour per severity as an authored foreground, which high
// contrast replaces with the role's own colour (§5.8).
//
// Closing (the actuator, Escape while the banner has focus, or the close
// semantic action) calls close(): it hides the banner and raises `closed`.
// A banner shown again may be closed again. The title and the message are
// single lines; text past a line's end is clipped.
//
// To assistive technology a banner is a live region named by its title (or
// its accessible name) and described by its message, so a host announces
// it when it appears or changes.
class InfoBanner final : public Control, public InfoBannerCapabilities<InfoBanner> {
public:
    explicit InfoBanner(StableId stable_id);

    // The action control (a button or a link), owned as an ordinary visual
    // child and placed at the end of the text row; null for none. The setter
    // returns the detached predecessor. A control that already has a parent
    // throws std::logic_error and changes nothing.
    [[nodiscard]] Control::Ptr action() const noexcept { return action_; }
    [[nodiscard]] Control::Ptr set_action(Control::Ptr control);

    // Hides the banner and raises `closed`; does nothing while it is hidden.
    // It closes whether or not the banner is closable: closable governs the
    // user's ways of closing, not the application's.
    void close();
    // Raised after close() hides the banner.
    [[nodiscard]] Event<>& closed() noexcept { return closed_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `face` is the
    // notice surface, `keyline` the severity's rule along the start edge,
    // `icon` the severity mark, `caption` the title, `label` the message,
    // `actuator` the close mark (absent unless closable; hot and pressed
    // as the pointer is) and `content` the action's slot (absent without
    // an action). An empty title or message publishes no caption or label.
    // The icon and the actuator take the glyph role, the role that draws a
    // mark.
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry keyline;
        PartGeometry icon;
        PartGeometry caption;
        PartGeometry label;
        PartGeometry actuator;
        PartGeometry content;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    // Where each part lies for a banner of `size`, and the size the banner
    // would choose for itself. Reads the last measured action size.
    struct Placement final {
        Rect face{};
        Rect keyline{};
        Rect icon{};
        Rect caption{};
        Rect label{};
        Rect actuator{};
        Rect content{};
        Size natural{};
    };
    [[nodiscard]] Placement placement(Size size) const;
    [[nodiscard]] FontSpec title_font() const noexcept;
    [[nodiscard]] FontSpec message_font() const noexcept;
    // The actuator's states: hot and pressed as the pointer is.
    [[nodiscard]] AnatomyStateSet actuator_states() const;

    Control::Ptr action_;
    // The action's desired size from the last measure; zero without one.
    Size action_size_{};
    Event<> closed_{};
    bool hot_close_{};
    bool pressed_close_{};
    // property_codemod begin InfoBanner (tools/property_families/info_banner.json; generated, do not edit)
public:
    // InfoBanner's property table: Control's entries, then its own.
    static constexpr std::size_t info_banner_property_count = Control::control_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, info_banner_property_count>& info_banner_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct InfoBannerSeverityCapability<InfoBanner>;
    friend struct InfoBannerTitleCapability<InfoBanner>;
    friend struct InfoBannerMessageCapability<InfoBanner>;
    friend struct InfoBannerClosableCapability<InfoBanner>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(severity)& cell, InfoBannerSeverity& incoming);
    void written(const decltype(severity)& cell);
    void prepare_write(const decltype(title)& cell, std::string& incoming);
    void written(const decltype(title)& cell);
    void prepare_write(const decltype(message)& cell, std::string& incoming);
    void written(const decltype(message)& cell);
    void prepare_write(const decltype(closable)& cell, bool& incoming);
    void written(const decltype(closable)& cell);
    // property_codemod end InfoBanner
};

} // namespace gui_forms
