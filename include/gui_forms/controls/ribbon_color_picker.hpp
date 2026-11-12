#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/commands/color_command/color_command.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/anchored_overlay_layer.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/window/native_popup/popup_lift.hpp"
#include "gui_forms/controls/ribbon_color_picker_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

class RibbonColorPalette;

// What a palette entry is: Automatic (no colour, the program's default), a
// palette colour, or a recent colour.
enum class RibbonPaletteEntryKind : std::uint8_t { automatic, palette, recent };

// The ribbon's drop-down colour picker (stage RB, phase 6; RIBBON_STUDY.md
// §1.1): a small split button whose face shows an icon over a bar of the
// current colour and applies that colour again, and whose arrow opens a
// palette overlay below it: Automatic, a grid of palette colours, and the
// command's recent colours. With a ColorCommand the picker shows and commits
// the command's value (the command's value_changed, then its Command runs);
// without one it keeps its own value. Hosted in a Ribbon as
// RibbonCommandKind::color_picker.
//
// Not yet: theme colour shades, a "More colours" dialog and live preview.
class GUI_FORMS_API RibbonColorPicker final : public Control, public RibbonColorPickerCapabilities<RibbonColorPicker> {
public:
    // Development bounds, not measured capacity.
    static constexpr std::size_t maximum_palette = 60U;
    static constexpr std::size_t palette_columns = 10U;

    explicit RibbonColorPicker(StableId stable_id);
    ~RibbonColorPicker() override;

    // The palette colours, ten to a row; the ten standard colours by
    // default. Throws std::invalid_argument for an empty palette, a repeated
    // colour, or more than maximum_palette.
    [[nodiscard]] std::span<const Color> palette() const& noexcept { return palette_; }
    std::span<const Color> palette() const&& = delete;
    void set_palette(std::vector<Color> palette);
    // The size the picker measures, in logical units at the current text
    // scale.
    [[nodiscard]] Size natural_size() const noexcept;

    // Opens the palette below the picker; false while disabled, already
    // open or not attached to a window. With MenuOpenMode::keyboard the
    // keyboard starts on the current colour.
    bool open_palette(MenuOpenMode mode = MenuOpenMode::keyboard);
    void close_palette() noexcept;
    [[nodiscard]] bool palette_open() const noexcept { return palette_open_; }
    // The palette's content control (its parts are in its local
    // coordinates), null before the first opening.
    [[nodiscard]] Control::Ptr palette_overlay() const noexcept;

    // Raised after the value shown changes, by the user or the program.
    [[nodiscard]] Event<const std::optional<Color>&>& value_changed() noexcept { return value_changed_; }
    [[nodiscard]] Event<bool>& palette_open_changed() noexcept { return palette_open_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

    // Stage T1 anatomy, as dot members; its states are Control's.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry icon;
        PartGeometry color_bar;
        PartGeometry color_sample;
        PartGeometry arrow;
        PartGeometry arrow_glyph;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

    // The palette's entries, as its "palette_entry" item system: Automatic,
    // then the palette, then the recent colours.
    struct GUI_FORMS_API PaletteEntryParts final {
        PartGeometry entry_face;
        PartGeometry entry_well;
        PartGeometry entry_sample;
        PartGeometry entry_label;
        PartGeometry entry_focus;
    };
    struct GUI_FORMS_API PaletteEntryStates final {
        bool hot{};
        bool selected{};
        bool focus_visible{};
    };
    struct GUI_FORMS_API PaletteEntry final {
        std::size_t index{};
        std::string stable_id;
        RibbonPaletteEntryKind kind{RibbonPaletteEntryKind::palette};
        std::optional<Color> color{};
        PaletteEntryParts parts;
        PaletteEntryStates states;
    };
    // The open palette's entries in its local coordinates; empty while
    // closed.
    [[nodiscard]] std::vector<PaletteEntry> realized_entries() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    friend class RibbonColorPalette;

    enum class Zone : std::uint8_t { none, face, arrow };
    struct Geometry final {
        Rect face{};
        Rect icon{};
        Rect bar{};
        Rect sample{};
        Rect arrow{};
        Rect glyph{};
    };
    // One palette entry's colour and its cell in the palette's local
    // coordinates.
    struct Cell final {
        RibbonPaletteEntryKind kind{RibbonPaletteEntryKind::palette};
        std::optional<Color> color{};
        Rect bounds{};
    };
    struct CommandValueChanged final {
        RibbonColorPicker* picker{};
        void operator()(const std::optional<Color>& value) const;
    };
    struct CommandStateChanged final {
        RibbonColorPicker* picker{};
        void operator()(const CommandState& state) const;
    };

    [[nodiscard]] Geometry geometry() const;
    [[nodiscard]] Parts own_parts() const;
    [[nodiscard]] Zone zone_at(Point local) const;
    [[nodiscard]] bool enabled_to_use() const;
    // The palette's cells, from its top-left corner.
    [[nodiscard]] std::vector<Cell> cells() const;
    [[nodiscard]] Size palette_size() const;
    [[nodiscard]] std::optional<std::size_t> current_entry(const std::vector<Cell>& all) const;
    [[nodiscard]] PaletteEntryParts entry_parts(const std::vector<Cell>& all, std::size_t entry) const;
    [[nodiscard]] AnatomyStateSet entry_states(const std::vector<Cell>& all, std::size_t entry) const;
    void commit(std::optional<Color> color);
    void palette_pointer(PointerEvent& event);
    void palette_key(KeyEvent& event);
    void palette_paint(Painter& painter) const;
    [[nodiscard]] std::vector<SemanticNode> palette_semantic_children() const;
    bool palette_semantic_action(std::string_view stable_id, SemanticAction action);
    void on_layer_dismissed(OverlayDismissReason reason);
    void on_overlay_revoked();
    // The host closed a Popup window: the picker's closes the palette.
    void on_popup_window_closed(std::size_t slot);

    SubscriptionToken command_value_{};
    SubscriptionToken command_state_{};
    std::optional<Color> own_value_{};
    std::vector<Color> palette_{};
    Zone hot_{Zone::none};
    Zone pressed_{Zone::none};
    bool focused_{};

    // The palette overlay: its content and layer (made on first opening), the
    // lease and focus scope while open, and the entry under the pointer and
    // the keyboard's entry in it.
    std::shared_ptr<RibbonColorPalette> palette_overlay_{};
    std::shared_ptr<AnchoredOverlayLayer> layer_{};
    OverlayToken overlay_{};
    FocusScopeId scope_{};
    SubscriptionToken layer_dismissed_{};
    SubscriptionToken overlay_revoked_{};
    // The palette's Popup while it shows outside the owner's layer (owner,
    // 2026-11-08: pickers are Popups before 0.5); the layer then has no
    // content and takes the owner's outside clicks.
    PopupLift lift_;
    SubscriptionToken lift_closed_{};
    bool palette_open_{};
    bool palette_focused_{};
    std::optional<std::size_t> hot_entry_{};
    std::optional<std::size_t> focus_entry_{};

    Event<const std::optional<Color>&> value_changed_{};
    Event<bool> palette_open_changed_{};
    // property_codemod begin RibbonColorPicker (tools/property_families/ribbon_color_picker.json; generated, do not edit)
private:
    friend struct RibbonColorPickerCommandCapability<RibbonColorPicker>;
    friend struct RibbonColorPickerValueCapability<RibbonColorPicker>;
    friend struct RibbonColorPickerImageCapability<RibbonColorPicker>;
    friend struct TextCapability<RibbonColorPicker>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(command)& cell, std::shared_ptr<ColorCommand>& incoming);
    void written(const decltype(command)& cell);
    void write_view(const decltype(value)& cell, std::optional<Color> incoming);
    [[nodiscard]] std::optional<Color> read_view(const decltype(value)& cell) const;
    void prepare_write(const decltype(image)& cell, ImageId& incoming);
    void written(const decltype(image)& cell);
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    // property_codemod end RibbonColorPicker
};

} // namespace gui_forms
