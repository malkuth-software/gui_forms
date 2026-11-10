#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/controls/callout/callout_capabilities/callout_capabilities.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms {

class Button;
class Label;

// Why a callout closed.
enum class CalloutCloseReason : std::uint8_t {
    // The close mark, or the semantic close action.
    close_button,
    escape_key,
    // A press outside it, with light_dismiss set.
    click_away,
    // The action button: action_clicked was raised first.
    action,
    // close() from the application.
    programmatic,
    // The target left its window, or the callout was disposed while shown.
    owner_unavailable,
};

// A teaching callout (the superset's P1 rest; a teaching tip in Fluent's
// terms): a card pointing out a target control, with an optional bold title,
// wrapped explanatory text, a close mark and an optional action button. It
// opens below its target (above with prefers_above), aligned to the
// target's leading edge and moved to the other side when the preferred one
// has no room, in an overlay of the target's window; it follows the target
// when the window resizes.
//
// A callout is not modal. Without light_dismiss, presses outside it reach
// the window as usual and it stays until the close mark, Escape, its action
// or close(); with light_dismiss, a press outside closes it (and is not
// passed on). Focus moves into the callout when it opens (to the action
// button, else the callout) without being held there, and returns to where
// it was when it closes. Escape closes it while focus is inside it.
//
// The design language has no beak vocabulary yet (an owner decision), so
// the callout is a card without a pointer to its target. Theme: `face` is
// the card (tool_tip family), with the overlay `shadow` beneath; `glyph`
// the close mark; the title and text are Label children and the action a
// Button. To assistive technology it is a non-modal dialog named by its
// title, described by its text, with the close action.
class Callout final : public Control, public CalloutCapabilities<Callout> {
public:
    explicit Callout(StableId stable_id);
    ~Callout() override;

    // Opens the callout at `target`, which must be attached to a window;
    // false, changing nothing, when it is not or the callout is already
    // shown or has a parent. UI thread.
    bool show(const Control::Ptr& target);
    // Closes a shown callout as programmatic; false when it is not shown.
    bool close();
    [[nodiscard]] bool is_shown() const noexcept { return shown_; }
    // The target while shown, else empty.
    [[nodiscard]] Control::Ptr target() const noexcept { return target_.lock(); }
    // The action button while action_text is set, else empty.
    [[nodiscard]] std::shared_ptr<Button> action_button() const noexcept { return action_; }

    // Raised once per closing, after the callout has left the overlay. The
    // callout may be shown again from the handler.
    [[nodiscard]] Event<CalloutCloseReason>& closed() noexcept { return closed_; }
    // Raised when the action button is clicked, before the callout closes.
    [[nodiscard]] Event<>& action_clicked() noexcept { return action_clicked_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    void on_pointer(PointerEvent& event) override;
    void on_key_bubble(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry close;
        PartGeometry actuator;
    };
    [[nodiscard]] Parts parts() const;

    // The overlay layer a shown callout sits in (private to the callout's
    // source; named here so the callout can own it).
    class Layer;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_dispose() noexcept override;

private:
    // Where the parts lie in a card of `size`.
    struct Placement final {
        Rect face{};
        Rect close{};
        Rect title{};
        Rect text{};
        Rect action{};
    };
    // The measured heights of the title and text rows and the action
    // button's size, at the width last measured or arranged.
    struct Rows final {
        double title{0.0};
        double text{0.0};
        Size action{};
    };
    void measure_rows(double width);
    [[nodiscard]] Placement placement(Size size) const;
    [[nodiscard]] double card_width(Size available) const noexcept;
    void sync_children();
    void finish(CalloutCloseReason reason);
    void on_action(ButtonBase& button);
    void on_overlay_revoked();
    // Called by the layer for a press outside the card.
    void on_press_outside();
    friend class Layer;

    std::shared_ptr<Label> title_label_{};
    std::shared_ptr<Label> text_label_{};
    std::shared_ptr<Button> action_{};
    SubscriptionToken action_token_{};
    Control::WeakPtr target_{};
    Control::Ptr layer_{};
    OverlayToken overlay_{};
    SubscriptionToken overlay_revoked_{};
    FocusScopeId scope_{};
    Rows rows_{};
    bool shown_{false};
    bool hot_close_{false};
    bool pressed_close_{false};
    Event<CalloutCloseReason> closed_;
    Event<> action_clicked_;
    // property_codemod begin Callout (tools/property_families/callout.json; generated, do not edit)
public:
    // Callout's property table: Control's entries, then its own.
    static constexpr std::size_t callout_property_count = Control::control_property_count + 5U;
    [[nodiscard]] static const std::array<PropertyEntry, callout_property_count>& callout_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct CalloutTitleCapability<Callout>;
    friend struct CalloutTextCapability<Callout>;
    friend struct CalloutActionTextCapability<Callout>;
    friend struct CalloutLightDismissCapability<Callout>;
    friend struct CalloutPrefersAboveCapability<Callout>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(title)& cell, std::string& incoming);
    void written(const decltype(title)& cell);
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    void prepare_write(const decltype(action_text)& cell, std::string& incoming);
    void written(const decltype(action_text)& cell);
    void prepare_write(const decltype(light_dismiss)& cell, bool& incoming);
    void written(const decltype(light_dismiss)& cell);
    void prepare_write(const decltype(prefers_above)& cell, bool& incoming);
    void written(const decltype(prefers_above)& cell);
    // property_codemod end Callout
};

} // namespace gui_forms
