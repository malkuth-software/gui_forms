#pragma once

#include "gui_forms/controls/button_base/button/button.hpp"

#include <cstdint>

namespace gui_forms {

enum class DropDownButtonMode : std::uint8_t {
    menu,
    split,
};

enum class DropDownButtonEdge : std::uint8_t { right, bottom };

// Retained desktop command button with an explicit disclosure actuator.
// Menu mode routes the whole button to the owned overlay. Split mode retains a
// primary Click region and a bounded disclosure region at the trailing edge.
class DropDownButton final : public Button {
public:
    explicit DropDownButton(StableId stable_id, std::string text = {},
                            DropDownButtonMode mode = DropDownButtonMode::menu);

    [[nodiscard]] DropDownButtonMode drop_down_mode() const noexcept {
        return mode_;
    }
    void set_drop_down_mode(DropDownButtonMode mode);
    // Disclosure extent on the selected edge; setters move the reserved
    // content inset with it. Author primary-content padding after these setters.
    [[nodiscard]] double drop_down_width() const noexcept {
        return drop_down_width_;
    }
    void set_drop_down_width(double width);
    [[nodiscard]] DropDownButtonEdge drop_down_edge() const noexcept { return edge_; }
    void set_drop_down_edge(DropDownButtonEdge edge);
    [[nodiscard]] bool drop_down_open() const noexcept {
        return drop_down_open_;
    }
    void set_drop_down_open(bool open);
    [[nodiscard]] Event<DropDownButton&>& drop_down_requested() noexcept {
        return drop_down_requested_;
    }
    [[nodiscard]] Event<DropDownButton&>& drop_down_close_requested() noexcept {
        return drop_down_close_requested_;
    }
    bool perform_drop_down();

    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry icon;
        PartGeometry label;
        PartGeometry drop_down;
        PartGeometry disclosure;
        PartGeometry preselect;
        PartGeometry focus;
    };
    struct States final {
        bool normal{};
        bool hot{};
        bool pressed{};
        bool preselect{};
        bool selected{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool open{};
        bool closed{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // The disclosure segment at the selected edge, in local coordinates.
    [[nodiscard]] Rect drop_down_bounds() const noexcept;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_activate() override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    [[nodiscard]] bool point_in_drop_down(Point window_point) const noexcept;

    DropDownButtonMode mode_{DropDownButtonMode::menu};
    DropDownButtonEdge edge_{DropDownButtonEdge::right};
    double drop_down_width_{16.0};
    bool drop_down_open_{};
    bool pointer_drop_down_{};
    Event<DropDownButton&> drop_down_requested_;
    Event<DropDownButton&> drop_down_close_requested_;
};

} // namespace gui_forms
