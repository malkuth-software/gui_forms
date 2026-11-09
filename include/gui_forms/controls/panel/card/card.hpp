#pragma once

#include "gui_forms/controls/panel/card/card_values/card_values.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/panel/card/card_capabilities/card_capabilities.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms {

// General retained header/body/footer composition. Card owns each installed
// section as an ordinary visual child and returns the detached predecessor on
// replacement; it never hides demo-only layout or renderer code.
class Card : public Panel, public CardCapabilities<Card> {
public:
    explicit Card(StableId stable_id);

    [[nodiscard]] Control::Ptr header() const noexcept { return header_; }
    [[nodiscard]] Control::Ptr body() const noexcept { return body_; }
    [[nodiscard]] Control::Ptr footer() const noexcept { return footer_; }
    [[nodiscard]] Control::Ptr set_header(Control::Ptr control);
    [[nodiscard]] Control::Ptr set_body(Control::Ptr control);
    [[nodiscard]] Control::Ptr set_footer(Control::Ptr control);

    [[nodiscard]] bool uses_theme_layout() const noexcept {
        return uses_theme_layout_;
    }
    [[nodiscard]] CardLayout effective_card_layout() const noexcept;
    void reset_card_layout_to_theme();
    [[nodiscard]] bool hovered_visual() const noexcept { return hovered_; }
    [[nodiscard]] bool pressed_visual() const noexcept { return pressed_; }
    [[nodiscard]] bool focused_visual() const noexcept { return focused_; }
    [[nodiscard]] Event<Card&>& activated() noexcept { return activated_; }
    [[nodiscard]] Event<bool>& selected_changed() noexcept {
        return selected_changed_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    void on_activate() override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 states, as dot members; Card's parts are Panel's.
    struct States final {
        bool normal{};
        bool hot{};
        bool pressed{};
        bool captured{};
        bool selected{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] States states() const;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    Control::Ptr replace_section(Control::Ptr& slot, Control::Ptr replacement);
    [[nodiscard]] ControlVisualContext current_context() const noexcept;

    Control::Ptr header_;
    Control::Ptr body_;
    Control::Ptr footer_;
    CardLayout layout_;
    Event<Card&> activated_;
    Event<bool> selected_changed_;
    std::uint32_t keyboard_key_{};
    bool hovered_{};
    bool pressed_{};
    bool focused_{};
    bool uses_theme_layout_{true};
    // property_codemod begin Card (tools/property_families/card.json; generated, do not edit)
private:
    friend struct CardCardLayoutCapability<Card>;
    friend struct CardInteractiveCapability<Card>;
    friend struct CardSelectedCapability<Card>;
    friend struct CardSelectionBehaviorPropertyCapability<Card>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(card_layout)& cell, CardLayout incoming);
    [[nodiscard]] CardLayout read_view(const decltype(card_layout)& cell) const;
    void prepare_write(const decltype(interactive)& cell, bool& incoming);
    void written(const decltype(interactive)& cell);
    void prepare_write(const decltype(selected)& cell, bool& incoming);
    void written(const decltype(selected)& cell);
    void prepare_write(const decltype(selection_behavior)& cell, CardSelectionBehavior& incoming);
    void written(const decltype(selection_behavior)& cell);
    // property_codemod end Card
};

} // namespace gui_forms
