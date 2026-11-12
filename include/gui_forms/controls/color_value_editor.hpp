#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/inspection/inspection_types.hpp"
#include "gui_forms/controls/color_value_editor_capabilities.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms {

class GUI_FORMS_API ColorValueEditor final : public Panel, public ColorValueEditorCapabilities<ColorValueEditor> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit ColorValueEditor(StableId stable_id, Color initial_value = {});
    void initialize_control_tree();

    [[nodiscard]] std::shared_ptr<TextBox> editor() const noexcept {
        return editor_;
    }
    [[nodiscard]] Event<Color>& value_changed() noexcept {
        return value_changed_;
    }
    [[nodiscard]] Event<const PropertyEditorInputError&>& edit_failed() noexcept {
        return edit_failed_;
    }
    [[nodiscard]] static std::string format_value(Color colour);
    [[nodiscard]] static std::optional<Color> parse_value(std::string_view text);

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members.
    struct GUI_FORMS_API Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry swatch;
        PartGeometry field;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool invalid{};
        bool pending{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    static std::optional<std::uint8_t> parse_nibble(char digit) noexcept;
    void commit_text(const std::string& text);
    void commit(std::string_view text);
    void cancel();

    Color value_{};
    std::shared_ptr<TextBox> editor_;
    SubscriptionToken committed_;
    SubscriptionToken cancelled_;
    bool synchronizing_{};
    Event<Color> value_changed_;
    Event<const PropertyEditorInputError&> edit_failed_;
    // property_codemod begin ColorValueEditor (tools/property_families/color_value_editor.json; generated, do not edit)
private:
    friend struct ColorValueEditorValueCapability<ColorValueEditor>;
    friend struct ColorValueEditorSwatchWidthCapability<ColorValueEditor>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(value)& cell, Color incoming);
    [[nodiscard]] Color read_view(const decltype(value)& cell) const;
    void prepare_write(const decltype(swatch_width)& cell, double& incoming);
    void written(const decltype(swatch_width)& cell);
    // property_codemod end ColorValueEditor
};

} // namespace gui_forms
