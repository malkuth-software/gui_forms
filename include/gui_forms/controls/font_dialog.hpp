#pragma once

#include "gui_forms/controls/panel.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/window/native_popup/native_popup.hpp"
#include "gui_forms/controls/font_dialog_capabilities.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class Button;
class ButtonBase;
class Label;
class ListBox;
class NumericUpDown;
struct ListSelectionChange;

// One face GUI.Forms bundles for a FontRole, as every host registers it:
// its family name, what the role is for, and whether it has italic faces.
// Every face has regular (400) and bold (700) weights. The Noto faces are
// fallbacks for scripts and emoji, not choices, and are not listed.
struct BundledFontFace final {
    FontRole role{FontRole::control};
    std::string_view family{};
    std::string_view use{};
    bool italic{};
};

// The bundled faces a FontDialog offers, in its list order.
[[nodiscard]] std::span<const BundledFontFace> bundled_font_faces() noexcept;

// How a FontDialog closed, and the font it holds then.
struct FontDialogResult final {
    DialogResult result{DialogResult::none};
    FontSpec font{};
};

// A font dialog (Windows Forms' FontDialog) over GUI.Forms' bundled faces.
// GUI.Forms never uses arbitrary host fonts, so the dialog chooses a FontRole
// (Portsmouth Rapids for controls, Carlito for content, Cousine for
// monospace), a style the face has (bold; italic where the face has italics)
// and a size, shows a preview, and returns a FontSpec.
//
// It is an in-window dialog built from GUI.Forms controls: show() opens it
// as an owned overlay over its owner's window, centred, with the focus held
// inside it until it closes; it may also be placed in a layout directly.
// There is no modal loop: show() returns at once and closed reports the
// result. OK takes the selection as font() and closes; Cancel closes and
// keeps font(); Apply takes the selection as font() and raises apply.
class FontDialog final : public Panel, public FontDialogCapabilities<FontDialog> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit FontDialog(StableId stable_id);
    ~FontDialog() override;
    void initialize_control_tree();

    // The font the dialog's controls show now. Its weight is 400 or 700,
    // italic only for a face with italics, and its size within the range.
    [[nodiscard]] FontSpec selection() const noexcept { return selection_; }
    // The sizes offered, in logical units; a reversed or non-positive range
    // throws std::invalid_argument. The selection is clamped into it.
    [[nodiscard]] double minimum_size() const noexcept { return minimum_size_; }
    [[nodiscard]] double maximum_size() const noexcept { return maximum_size_; }
    void set_size_range(double minimum, double maximum);

    // Opens the dialog over `owner`'s window. Returns false when the owner is
    // not attached or the dialog already has a parent; true when it is open.
    bool show(const Control::Ptr& owner);
    // How show() presents the dialog (DialogSurface; an overlay by default).
    // With native_popup it opens in a native popup window owned by the
    // owner's window, centred over its client area, when the run can open
    // one; the transparent layer that holds the owner's input stays in the
    // owner's window. Takes effect at the next show().
    [[nodiscard]] DialogSurface surface() const noexcept { return surface_; }
    void set_surface(DialogSurface surface) noexcept { surface_ = surface; }
    // Where the open dialog went: native while it is in a popup window, else
    // overlay (or none when it is not open).
    [[nodiscard]] NativePopupSurface presented_surface() const noexcept;
    // Closes the dialog with `result` (OK takes the selection as font()) and
    // raises closed. Nothing happens when it is not open.
    void close(DialogResult result);
    [[nodiscard]] bool is_open() const noexcept { return open_; }

    // Raised by Apply, with the new font().
    [[nodiscard]] Event<const FontSpec&>& apply() noexcept { return apply_; }
    // Raised after the dialog closes: by OK, Cancel, Escape, close() or the
    // owner going away (cancel).
    [[nodiscard]] Event<const FontDialogResult&>& closed() noexcept { return closed_; }

    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_key_preview(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    // The focus action moves focus to the font list, the dialog's first
    // field; the dialog itself is not focusable.
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members; its states are Control's.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry title;
        PartGeometry preview_well;
        PartGeometry preview_text;
    };
    [[nodiscard]] Parts parts() const;

    // The dialog's controls, for reading and for tests.
    [[nodiscard]] std::shared_ptr<ListBox> face_list() const noexcept { return face_list_; }
    [[nodiscard]] std::shared_ptr<ListBox> style_list() const noexcept { return style_list_; }
    [[nodiscard]] std::shared_ptr<NumericUpDown> size_box() const noexcept { return size_box_; }
    [[nodiscard]] std::shared_ptr<Button> ok_button() const noexcept { return ok_button_; }
    [[nodiscard]] std::shared_ptr<Button> cancel_button() const noexcept { return cancel_button_; }
    [[nodiscard]] std::shared_ptr<Button> apply_button() const noexcept { return apply_button_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The styles of the style list, in its order.
    enum class FontStyle : std::uint8_t { regular, bold, italic, bold_italic };
    // The name the style list shows for `style`.
    [[nodiscard]] static std::string_view style_name(FontStyle style) noexcept;

    [[nodiscard]] Parts dialog_parts() const;
    // Shows the selection in the controls.
    void show_selection();
    void fill_styles();
    [[nodiscard]] std::size_t face_index(FontRole role) const noexcept;
    [[nodiscard]] FontSpec normalized(FontSpec requested) const noexcept;
    void on_face_selected(const ListSelectionChange& change);
    void on_style_selected(const ListSelectionChange& change);
    void on_size_changed(double size);
    void on_ok(ButtonBase& button);
    void on_cancel(ButtonBase& button);
    void on_apply(ButtonBase& button);
    void on_overlay_revoked();
    // Leaves the overlay: ends the focus scope, takes the dialog out of its
    // layer and releases the layer.
    void release_overlay(FocusScopeCloseReason reason);

    FontSpec font_{FontRole::content, 9.0, 400, false};
    FontSpec selection_{FontRole::content, 9.0, 400, false};
    double minimum_size_{8.0};
    double maximum_size_{72.0};
    bool open_{};
    bool synchronizing_{};
    std::shared_ptr<Label> face_caption_;
    std::shared_ptr<Label> style_caption_;
    std::shared_ptr<Label> size_caption_;
    std::shared_ptr<ListBox> face_list_;
    std::shared_ptr<ListBox> style_list_;
    std::shared_ptr<NumericUpDown> size_box_;
    std::shared_ptr<Button> ok_button_;
    std::shared_ptr<Button> cancel_button_;
    std::shared_ptr<Button> apply_button_;
    // The styles the style list shows for the selected face.
    std::vector<FontStyle> styles_;
    std::shared_ptr<Panel> overlay_layer_;
    OverlayToken overlay_token_;
    std::uint64_t overlay_scope_{};
    // The native popup window the dialog is in (DialogSurface::native_popup):
    // its root holds the dialog, so closing detaches the dialog at once even
    // though the window itself is released later.
    DialogSurface surface_{DialogSurface::overlay};
    Control::Ptr native_root_{};
    NativePopupToken native_token_{};
    SubscriptionToken native_closed_;
    // Moves the open dialog from the layer into a native popup window at
    // `bounds`; false (the dialog back in the layer) when the run cannot
    // open one.
    bool open_native(Window& host, const Control::Ptr& owner, Rect bounds);
    SubscriptionToken face_token_;
    SubscriptionToken style_token_;
    SubscriptionToken size_token_;
    SubscriptionToken ok_token_;
    SubscriptionToken cancel_token_;
    SubscriptionToken apply_token_;
    SubscriptionToken revocation_token_;
    Event<const FontSpec&> apply_;
    Event<const FontDialogResult&> closed_;
    // property_codemod begin FontDialog (tools/property_families/font_dialog.json; generated, do not edit)
private:
    friend struct FontDialogFontCapability<FontDialog>;
    friend struct FontDialogShowApplyCapability<FontDialog>;
    friend struct FontDialogPreviewTextCapability<FontDialog>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(font)& cell, FontSpec incoming);
    [[nodiscard]] FontSpec read_view(const decltype(font)& cell) const;
    void prepare_write(const decltype(show_apply)& cell, bool& incoming);
    void written(const decltype(show_apply)& cell);
    void prepare_write(const decltype(preview_text)& cell, std::string& incoming);
    void written(const decltype(preview_text)& cell);
    // property_codemod end FontDialog
};

} // namespace gui_forms
