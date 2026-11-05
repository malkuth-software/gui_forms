#pragma once

#include "gui_forms/inspection/inspection_types.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms {

class FlagsValueEditor final : public Panel {
public:
    FlagsValueEditor(StableId stable_id, PropertyEnumDescriptor descriptor,
                     PropertyEnumValue value);

    [[nodiscard]] const PropertyEnumDescriptor& descriptor() const noexcept {
        return descriptor_;
    }
    void set_descriptor(PropertyEnumDescriptor descriptor);
    [[nodiscard]] const PropertyEnumValue& value() const noexcept {
        return value_;
    }
    void set_value(PropertyEnumValue value);
    [[nodiscard]] bool dropped_down() const noexcept { return dropped_down_; }
    void set_dropped_down(bool dropped_down);
    [[nodiscard]] double overlay_width() const noexcept { return overlay_width_; }
    void set_overlay_width(double width);
    [[nodiscard]] Event<const PropertyEnumValue&>& value_changed() noexcept {
        return value_changed_;
    }
    [[nodiscard]] Event<bool>& drop_down_changed() noexcept {
        return drop_down_changed_;
    }

    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry content;
        PartGeometry divider;
        PartGeometry drop_down;
        PartGeometry disclosure;
    };
    struct States final {
        bool normal{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool open{};
        bool closed{};
        bool invalid{};
        bool pending{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    void open_drop_down();
    void close_drop_down();
    void on_overlay_revoked();
    void synchronize_overlay();
    void apply_overlay_choice(std::size_t overlay_index, CheckState state);

    PropertyEnumDescriptor descriptor_;
    PropertyEnumValue value_;
    std::shared_ptr<Panel> overlay_layer_;
    std::shared_ptr<CheckedListBox> overlay_list_;
    std::vector<std::size_t> overlay_choice_indices_;
    std::uint64_t overlay_scope_{};
    OverlayToken overlay_token_;
    SubscriptionToken overlay_check_;
    SubscriptionToken overlay_dismissal_;
    SubscriptionToken overlay_revocation_;
    double overlay_width_{240.0};
    bool dropped_down_{};
    bool focused_{};
    bool synchronizing_overlay_{};
    bool closing_overlay_{};
    Event<const PropertyEnumValue&> value_changed_;
    Event<bool> drop_down_changed_;
};

} // namespace gui_forms
