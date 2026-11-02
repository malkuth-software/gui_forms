#pragma once

#include "gui_forms/connected_controls/types/connected_control_types.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/image_list.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/theme.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms {

class Command;

enum class ContentAlignment : std::uint8_t {
    top_left,
    top_center,
    top_right,
    middle_left,
    middle_center,
    middle_right,
    bottom_left,
    bottom_center,
    bottom_right,
};

enum class TextImageRelation : std::uint8_t {
    overlay,
    image_before_text,
    text_before_image,
    image_above_text,
    text_above_image,
};

enum class ChoiceIndicatorStyle : std::uint8_t {
    classic,
    modern,
    toggle,
};

class ButtonBase : public Control {
public:
    explicit ButtonBase(StableId stable_id, std::string text = {});
    ~ButtonBase() override;
    virtual void bind(Command& command);
    void unbind_command() noexcept;
    [[nodiscard]] bool command_connected() const noexcept;

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    virtual void set_text(GUI_FORMS_LOCALIZABLE(text) std::string text);
    [[nodiscard]] FontSpec font() const noexcept { return font_; }
    void set_font(FontSpec font);
    [[nodiscard]] double text_line_spacing() const noexcept {
        return text_line_spacing_;
    }
    void set_text_line_spacing(double spacing);
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
    [[nodiscard]] ImageId image() const noexcept { return image_; }
    void set_image(ImageId image);
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
    [[nodiscard]] ContentAlignment image_alignment() const noexcept {
        return image_alignment_;
    }
    void set_image_alignment(ContentAlignment alignment);
    [[nodiscard]] ContentAlignment text_alignment() const noexcept {
        return text_alignment_;
    }
    void set_text_alignment(ContentAlignment alignment);
    [[nodiscard]] TextImageRelation text_image_relation() const noexcept {
        return text_image_relation_;
    }
    void set_text_image_relation(TextImageRelation relation);
    [[nodiscard]] double image_gap() const noexcept { return image_gap_; }
    void set_image_gap(double gap);
    [[nodiscard]] Insets content_padding() const noexcept {
        return content_padding_;
    }
    void set_content_padding(Insets padding);
    [[nodiscard]] bool use_mnemonic() const noexcept { return use_mnemonic_; }
    void set_use_mnemonic(bool value);
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
    // Optional disclosure state for buttons that own a popup or retained
    // disclosure region. Nullopt is an ordinary push button; false/true expose
    // collapsed/expanded semantics without conflating visual pressed state.
    [[nodiscard]] std::optional<bool> expanded_state() const noexcept {
        return expanded_state_;
    }
    void set_expanded_state(std::optional<bool> expanded);
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
    void on_focus_changed(bool focused) override;
    void on_activate() override;
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
        PartGeometry focus;
    };
    struct States final {
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
    struct ContentLayout final {
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
                                               FontSpec font, bool selected,
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
    void paint_button_frame(Painter& painter, Rect bounds, bool default_cue) const;
    void paint_button_text(Painter& painter, Rect bounds,
                           std::string_view text) const;
    void paint_button_content(Painter& painter, Rect bounds,
                              std::string_view text, Color foreground,
                              Point offset = {}, bool selected = false,
                              bool command_alignment = false) const;
    // The colours a style override gives a themed button's face and text
    // (owner decision T4-1; hot states, option 1).
    struct AuthoredButtonColours final {
        Color face{};
        Color text{};
    };
    // Whether the button paints its style override's own frame: only an
    // overridden button with per-control recipes or a connection, which the
    // engine's parts do not shape. Every other button paints themed, an
    // override supplying its authored colours.
    [[nodiscard]] bool paints_override_frame() const noexcept;
    // The face, cues and content of a themed push-style button. With a
    // style override, `authored` (by default the override's face and text)
    // colours the face and the enabled text.
    void paint_themed_button(Painter& painter, Rect bounds,
                             ControlVisualRole role, bool default_cue,
                             bool command_alignment = false,
                             bool selected = false,
                             std::optional<AuthoredButtonColours> authored = {}) const;
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

    std::string text_;
    FontSpec font_{FontRole::control, 12.0, 400, false, 0.24};
    double text_line_spacing_{1.25};
    std::optional<BasicControlStyle> style_override_;
    std::optional<ControlStateRecipes> visual_recipes_override_;
    std::optional<ConnectedControlTopology> connection_topology_;
    ImageId image_{};
    std::shared_ptr<ImageList> image_list_;
    SubscriptionToken image_list_changed_;
    std::string image_key_;
    int image_index_{-1};
    ContentAlignment image_alignment_{ContentAlignment::middle_center};
    ContentAlignment text_alignment_{ContentAlignment::middle_center};
    TextImageRelation text_image_relation_{TextImageRelation::overlay};
    double image_gap_{4.0};
    Insets content_padding_{6.0, 4.0, 6.0, 4.0};
    Event<ButtonBase&> clicked_;
    Event<const std::string&> text_changed_;
    std::uint32_t keyboard_key_{};
    bool pointer_engaged_{};
    bool pointer_pressed_{};
    bool keyboard_pressed_{};
    bool focused_{};
    bool hovered_{};
    bool use_mnemonic_{true};
    std::optional<bool> expanded_state_;
    Event<std::optional<bool>> expanded_changed_{};
};

} // namespace gui_forms
