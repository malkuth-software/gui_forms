#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/inspection/inspection_types.hpp"
#include "gui_forms/controls/anchored_overlay_layer.hpp"
#include "gui_forms/controls/color_picker.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/controls/color_value_editor_capabilities.hpp"

#include <memory>
#include <optional>
#include <stxlib/optional.hpp>
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
    // The text form keeps alpha: format_value writes #RRGGBBAA, and
    // parse_value takes #RRGGBBAA or #RRGGBB (alpha 255), hex digits of
    // either case between optional surrounding white space. `value` holds
    // the alpha it was given or parsed.
    // The drop-down picker (the ImGui-class demonstrator's I2): a ColorPicker
    // in an anchored overlay below the editor, opened by a primary press on
    // the swatch, by Alt+Down or F4 while the editor has focus, or by
    // open_picker. While open, each pick writes the value (value_changed)
    // and the text; a click away, Escape or close_picker closes it.
    // open_picker returns false, changing nothing, when the picker is open
    // already or the editor is not in a window or not enabled.
    bool open_picker();
    void close_picker() noexcept;
    [[nodiscard]] bool picker_open() const noexcept { return picker_open_; }
    // The picker, made on first opening (null before).
    [[nodiscard]] std::shared_ptr<ColorPicker> picker() const noexcept { return picker_; }
    [[nodiscard]] static std::string format_value(Color colour);
    [[nodiscard]] static stxlib::Optional<Color> parse_value(std::string_view text);

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
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_key_down(KeyEventArgs& arguments) override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    static stxlib::Optional<std::uint8_t> parse_nibble(char digit) noexcept;
    void commit_text(const std::string& text);
    void commit(std::string_view text);
    void cancel();
    // The swatch's rectangle in local coordinates.
    [[nodiscard]] Rect swatch_bounds() const noexcept;
    // Writes `incoming` as a commit would: the value, the text and the
    // normal status, then value_changed when the colour moved.
    void accept_colour(Color incoming);
    void on_picker_changed(Color colour);
    void on_layer_dismissed(OverlayDismissReason reason);
    void on_overlay_revoked();

    Color value_{};
    std::shared_ptr<TextBox> editor_;
    SubscriptionToken committed_;
    SubscriptionToken cancelled_;
    bool synchronizing_{};
    Event<Color> value_changed_;
    Event<const PropertyEditorInputError&> edit_failed_;
    // The drop-down: its picker and layer (made on first opening), the
    // overlay lease and focus scope while open.
    std::shared_ptr<ColorPicker> picker_{};
    std::shared_ptr<AnchoredOverlayLayer> layer_{};
    OverlayToken overlay_{};
    FocusScopeId scope_{};
    SubscriptionToken picker_changed_{};
    SubscriptionToken layer_dismissed_{};
    SubscriptionToken overlay_revoked_{};
    bool picker_open_{};
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
