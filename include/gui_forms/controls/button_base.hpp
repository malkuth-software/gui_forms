#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/connected_controls/types/connected_control_types.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/image_list.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/controls/button_base_capabilities.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms {

class Command;

enum class ChoiceIndicatorStyle : std::uint8_t {
    classic,
    modern,
    toggle,
};

class GUI_FORMS_API ButtonBase : public Control, public ButtonBaseCapabilities<ButtonBase> {
public:
    explicit ButtonBase(StableId stable_id, std::string initial_text = {});
    ~ButtonBase() override;
    virtual void bind(Command& command);
    void unbind_command() noexcept;
    [[nodiscard]] bool command_connected() const noexcept;

    [[nodiscard]] const BasicControlStyle& style() const noexcept;
    [[nodiscard]] bool has_style_override() const noexcept {
        return style_override_.has_value();
    }
    void set_style(BasicControlStyle style);
    void clear_style();
    [[nodiscard]] const std::optional<ControlStateRecipes>&
    visual_recipes_override() const noexcept {
        return visual_recipes_override_;
    }
    void set_visual_recipes(ControlStateRecipes recipes);
    void clear_visual_recipes();
    [[nodiscard]] const std::optional<ConnectedControlTopology>&
    connection_topology() const noexcept {
        return connection_topology_;
    }
    // Nullopt is a standalone control. A non-null value is explicit authored
    // topology; layout coordinates are never consulted to infer connection.
    void set_connection_topology(
        std::optional<ConnectedControlTopology> topology);
    void clear_image();
    [[nodiscard]] std::shared_ptr<ImageList> image_list() const noexcept {
        return image_list_;
    }
    void set_image_list(std::shared_ptr<ImageList> image_list);
    [[nodiscard]] int image_index() const noexcept { return image_index_; }
    void set_image_index(int image_index);
    [[nodiscard]] const std::string& image_key() const noexcept {
        return image_key_;
    }
    void set_image_key(std::string image_key);
    // Executes the same validated command path used by mnemonics, semantic
    // press, and a Window accept/cancel button. Returns false when unavailable
    // or when validation prevents the command.
    bool perform_click();
    [[nodiscard]] bool pressed_visual() const noexcept {
        return pointer_pressed_ || keyboard_pressed_;
    }
    [[nodiscard]] bool focused_visual() const noexcept { return focused_; }
    [[nodiscard]] bool focus_cue_visible() const noexcept;
    [[nodiscard]] bool hovered_visual() const noexcept { return hovered_; }
    [[nodiscard]] Event<std::optional<bool>>& expanded_changed() noexcept {
        return expanded_changed_;
    }
    [[nodiscard]] Event<ButtonBase&>& clicked() noexcept { return clicked_; }
    [[nodiscard]] Event<const std::string&>& text_changed() noexcept {
        return text_changed_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    // As in WinForms, a button clicks for the primary button only.
    [[nodiscard]] bool clicks_primary_button_only() const noexcept override;
    void on_focus_changed(bool focused) override;
    void on_activate() override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry icon;
        PartGeometry label;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool hot{};
        bool pressed{};
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

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    // Text and image placement inside a button face (stage T1). Paint and
    // published geometry share it. Rectangles are at rest: the pressed
    // content offset and a theme's visual offset are presentation and are
    // applied only when painting.
    struct GUI_FORMS_API ContentLayout final {
        // None when the button shows no text, or no image.
        std::optional<Rect> text{};
        std::optional<Rect> image{};
        double line_height{};
    };

    [[nodiscard]] Rect local_bounds() const noexcept;
    // The face less its content padding, and the command leading when
    // `command_alignment` is set.
    [[nodiscard]] Rect content_box(Rect face, bool command_alignment) const noexcept;
    // Places the text block, one entry of `line_sizes` per display line, and
    // the resolved image inside `content`.
    [[nodiscard]] ContentLayout layout_content(Rect content,
                                               std::span<const Size> line_sizes,
                                               FontSpec line_font, bool selected,
                                               bool command_alignment) const;
    // layout_content for `face`, measuring the display text with the
    // window's text metrics.
    [[nodiscard]] ContentLayout published_content_layout(
        Rect face, bool selected, bool command_alignment) const;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    // The parts of a push-style button whose face is the whole control.
    [[nodiscard]] Parts push_button_parts(bool selected, bool command_alignment) const;
    // The interaction, availability, focus and disclosure states every
    // button shares.
    [[nodiscard]] AnatomyStateSet button_anatomy_states() const;
    [[nodiscard]] std::string display_text() const;
    // How a button's content is inked: the theme family and anatomy states
    // its label and icon parts are drawn in, and the authored text colour,
    // or none (owner decision T4-1: it stands while the button is enabled
    // and outside forced high contrast).
    struct GUI_FORMS_API ButtonContentInk final {
        ControlVisualRole family{ControlVisualRole::button};
        AnatomyStateSet states{};
        std::optional<Color> authored{};
    };
    // The image and the text lines laid out in `bounds`, drawn as the icon
    // part and one label part per line through the theme's engine,
    // displaced by `offset`.
    void paint_button_content(Painter& painter, Rect bounds,
                              std::string_view shown_text, const ButtonContentInk& ink,
                              Point offset = {}, bool selected = false,
                              bool command_alignment = false) const;
    // The colours a style override gives a themed button's face and text
    // (owner decision T4-1; hot states, option 1).
    struct GUI_FORMS_API AuthoredButtonColours final {
        Color face{};
        Color text{};
    };
    // The face, cues and content of a themed push-style button. With a
    // style override, `authored` (by default the override's face and text)
    // colours the face and the enabled text.
    void paint_themed_button(Painter& painter, Rect bounds,
                             ControlVisualRole role, bool default_cue,
                             bool command_alignment = false,
                             bool selected = false,
                             std::optional<AuthoredButtonColours> authored = {}) const;
    // A flat button (WinForms FlatStyle.Flat): the style override's face
    // (else none: the parent shows), a light veil while hot or pressed, a
    // border `border_width` logical pixels wide in the override's text
    // colour (else the theme's border; none at 0), the focus cue and the
    // content. No raised face and no default ring.
    void paint_flat_button(Painter& painter, Rect bounds, double border_width) const;
    [[nodiscard]] const ControlVisualRecipe& resolve_visual_recipe(
        ControlVisualRole role, ControlVisualContext context) const noexcept;
    [[nodiscard]] Insets resolved_visual_outsets(
        ControlVisualRole role, ControlVisualContext context) const noexcept;
    [[nodiscard]] ImageListResolution resolved_button_image(
        bool selected = false) const noexcept;
    void on_attached_to_window() override;
    bool perform_dialog_command() override;
    [[nodiscard]] bool supports_dialog_command() const noexcept override {
        return true;
    }
    [[nodiscard]] bool mnemonic_matches(
        char32_t character) const noexcept override;
    bool process_mnemonic_self(char32_t character) override;

private:
    class CommandConnection;
    std::shared_ptr<CommandConnection> command_connection_{};
    [[nodiscard]] bool should_serialize_image() const noexcept;
    void on_image_list_changed(const ImageListChange& change);

    std::optional<BasicControlStyle> style_override_;
    std::optional<ControlStateRecipes> visual_recipes_override_;
    std::optional<ConnectedControlTopology> connection_topology_;
    ImageId image_{};
    std::shared_ptr<ImageList> image_list_;
    SubscriptionToken image_list_changed_;
    std::string image_key_;
    int image_index_{-1};
    Event<ButtonBase&> clicked_;
    Event<const std::string&> text_changed_;
    std::uint32_t keyboard_key_{};
    bool pointer_engaged_{};
    bool pointer_pressed_{};
    bool keyboard_pressed_{};
    bool focused_{};
    bool hovered_{};
    Event<std::optional<bool>> expanded_changed_{};
    // property_codemod begin ButtonBase (tools/property_families/button_base.json; generated, do not edit)
public:
    // ButtonBase's property table: Control's entries, then its own.
    static constexpr std::size_t button_base_property_count = Control::control_property_count + 5U;
    [[nodiscard]] static const std::array<PropertyEntry, button_base_property_count>& button_base_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_text_changed();
    virtual void on_font_changed();

private:
    friend struct TextCapability<ButtonBase>;
    friend struct TextAlignmentCapability<ButtonBase>;
    friend struct FontCapability<ButtonBase>;
    friend struct ContentPaddingCapability<ButtonBase>;
    friend struct TextLineSpacingCapability<ButtonBase>;
    friend struct ImageAlignmentCapability<ButtonBase>;
    friend struct TextImageRelationCapability<ButtonBase>;
    friend struct ImageGapCapability<ButtonBase>;
    friend struct ButtonUseMnemonicCapability<ButtonBase>;
    friend struct ExpandedStateCapability<ButtonBase>;
    friend struct ButtonBaseImageCapability<ButtonBase>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    void prepare_write(const decltype(text_alignment)& cell, ContentAlignment& incoming);
    void written(const decltype(text_alignment)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(content_padding)& cell, Insets& incoming);
    void written(const decltype(content_padding)& cell);
    void prepare_write(const decltype(text_line_spacing)& cell, double& incoming);
    void written(const decltype(text_line_spacing)& cell);
    void prepare_write(const decltype(image_alignment)& cell, ContentAlignment& incoming);
    void written(const decltype(image_alignment)& cell);
    void prepare_write(const decltype(text_image_relation)& cell, TextImageRelation& incoming);
    void written(const decltype(text_image_relation)& cell);
    void prepare_write(const decltype(image_gap)& cell, double& incoming);
    void written(const decltype(image_gap)& cell);
    void prepare_write(const decltype(use_mnemonic)& cell, bool& incoming);
    void written(const decltype(use_mnemonic)& cell);
    void prepare_write(const decltype(expanded_state)& cell, std::optional<bool>& incoming);
    void written(const decltype(expanded_state)& cell);
    void write_view(const decltype(image)& cell, ImageId incoming);
    [[nodiscard]] ImageId read_view(const decltype(image)& cell) const;
    // property_codemod end ButtonBase
};

} // namespace gui_forms
