#include "gui_forms/controls/font_dialog.hpp"

#include "../panel_anatomy.hpp"
#include "../../dialog_presentation/provisional_child.hpp"

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/numeric_up_down.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// The faces every host registers for the three roles (the face tables of
// the Windows, macOS and X11 hosts). Portsmouth Rapids has no italic face
// registered; Carlito and Cousine have all four styles.
constexpr std::array<BundledFontFace, 3> bundled_faces{{
    {.role = FontRole::control, .family = "Portsmouth Rapids", .use = "controls", .italic = false},
    {.role = FontRole::content, .family = "Carlito", .use = "content", .italic = true},
    {.role = FontRole::monospace, .family = "Cousine", .use = "monospace", .italic = true},
}};

// The dialog's default layout in logical units at text scale 1: the title,
// three columns (face, style, size) under their captions, the preview, and
// the buttons, 460 x 332 in all. Every length scales with the window's text
// scale, and the columns, caption rows and buttons grow to the text they
// show (FontDialog::dialog_layout), so a longer name, a larger scale or
// another theme's font widens the dialog rather than clipping.
constexpr double dialog_width = 460.0;
constexpr double dialog_margin = 16.0;
constexpr double title_top = 8.0;
constexpr double title_height = 28.0;
// Below the title, the caption row, then the lists.
constexpr double title_caption_gap = 8.0;
constexpr double caption_height = 20.0;
constexpr double caption_list_gap = 2.0;
constexpr double list_height = 92.0;
constexpr double face_width = 172.0;
constexpr double style_width = 118.0;
constexpr double size_width = 114.0;
constexpr double size_height = 30.0;
constexpr double column_gap = 12.0;
constexpr double list_preview_gap = 14.0;
constexpr double preview_height = 94.0;
constexpr double preview_button_gap = 18.0;
constexpr double button_width = 84.0;
constexpr double button_height = 32.0;
constexpr double button_gap = 8.0;
// What a list row needs beside its text: the well's insets, the row's text
// inset on both sides, and room for a vertical scroll bar.
constexpr double list_text_allowance = 40.0;
// What a caption needs beside its text (the label's insets).
constexpr double caption_text_allowance = 4.0;
// The extent offered to a child measured for its one-line text.
constexpr double measure_limit = 1.0e6;

// The layer under an open dialog: it covers the window and takes presses
// outside the dialog, so the page below does not react while the dialog is
// open. It paints nothing.
class FontDialogLayer final : public Panel {
public:
    explicit FontDialogLayer(StableId stable_id) : Panel(std::move(stable_id)) {
        set_paint_plane(PaintPlane::overlay);
        border_style = BorderStyle::none;
        // Fully transparent: the layer has no appearance of its own.
        set_background(Color::rgba(0, 0, 0, 0));
    }

    void on_mouse_down(MouseEventArgs& arguments) override {
        arguments.handled = true;
        Control::on_mouse_down(arguments);
    }

    void on_mouse_up(MouseEventArgs& arguments) override {
        arguments.handled = true;
        Control::on_mouse_up(arguments);
    }
};

} // namespace

std::string_view FontDialog::style_name(FontStyle style) noexcept {
    switch (style) {
    case FontStyle::regular: return "Regular";
    case FontStyle::bold: return "Bold";
    case FontStyle::italic: return "Italic";
    case FontStyle::bold_italic: return "Bold Italic";
    }
    return "Bold Italic";
}

std::span<const BundledFontFace> bundled_font_faces() noexcept {
    return bundled_faces;
}

FontDialog::FontDialog(StableId stable_id) : Panel(std::move(stable_id)) {
    border_style = BorderStyle::none;
    focusable = false;
}

FontDialog::~FontDialog() {
    // Close without raising closed: there is no one left to tell.
    presentation_.abandon();
}

void FontDialog::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    face_caption_ = make_control<Label>(StableId(prefix + ".face-caption"), "Font:");
    style_caption_ = make_control<Label>(StableId(prefix + ".style-caption"), "Font style:");
    size_caption_ = make_control<Label>(StableId(prefix + ".size-caption"), "Size:");
    face_list_ = make_control<ListBox>(StableId(prefix + ".face-list"));
    style_list_ = make_control<ListBox>(StableId(prefix + ".style-list"));
    size_box_ = make_control<NumericUpDown>(StableId(prefix + ".size"));
    ok_button_ = make_control<Button>(StableId(prefix + ".ok"), "OK");
    cancel_button_ = make_control<Button>(StableId(prefix + ".cancel"), "Cancel");
    apply_button_ = make_control<Button>(StableId(prefix + ".apply"), "Apply");
    (*face_list_).accessible_name = "Font";
    (*style_list_).accessible_name = "Font style";
    (*size_box_).accessible_name = "Size";
    std::vector<std::string> faces;
    for (const BundledFontFace& face : bundled_faces) {
        faces.push_back(std::string(face.family) + " (" + std::string(face.use) + ")");
    }
    (*face_list_).items = std::move(faces);
    (*size_box_).decimal_places = 1U;
    (*size_box_).increment = 1.0;
    (*size_box_).set_range(minimum_size_, maximum_size_);
    (*ok_button_).default_button = true;
    (*ok_button_).dialog_result = DialogResult::ok;
    (*cancel_button_).dialog_result = DialogResult::cancel;
    add_child(face_caption_);
    add_child(style_caption_);
    add_child(size_caption_);
    add_child(face_list_);
    add_child(style_list_);
    add_child(size_box_);
    add_child(ok_button_);
    add_child(cancel_button_);
    add_child(apply_button_);
    face_token_ = (*face_list_).selection_changed().subscribe(
        *this, Delegate<const ListSelectionChange&>::bind<FontDialog, &FontDialog::on_face_selected>(*this));
    style_token_ = (*style_list_).selection_changed().subscribe(
        *this, Delegate<const ListSelectionChange&>::bind<FontDialog, &FontDialog::on_style_selected>(*this));
    size_token_ = (*size_box_).value_changed().subscribe(
        *this, Delegate<double>::bind<FontDialog, &FontDialog::on_size_changed>(*this));
    ok_token_ = (*ok_button_).clicked().subscribe(
        *this, Delegate<ButtonBase&>::bind<FontDialog, &FontDialog::on_ok>(*this));
    cancel_token_ = (*cancel_button_).clicked().subscribe(
        *this, Delegate<ButtonBase&>::bind<FontDialog, &FontDialog::on_cancel>(*this));
    apply_token_ = (*apply_button_).clicked().subscribe(
        *this, Delegate<ButtonBase&>::bind<FontDialog, &FontDialog::on_apply>(*this));
    show_selection();
}

// -- selection -------------------------------------------------------------------

std::size_t FontDialog::face_index(FontRole role) const noexcept {
    for (std::size_t index = 0U; index < bundled_faces.size(); ++index) {
        if (bundled_faces[index].role == role) return index;
    }
    return 0U;
}

// `font` as the bundled faces can show it: weight 400 or 700 (the nearer),
// italic only where the face has italics, size within the range.
FontSpec FontDialog::normalized(FontSpec requested) const noexcept {
    FontSpec result = requested;
    const BundledFontFace& face = bundled_faces[face_index(requested.role)];
    result.role = face.role;
    result.weight = static_cast<std::uint16_t>(requested.weight >= 550U ? 700U : 400U);
    result.italic = requested.italic && face.italic;
    result.size = std::clamp(requested.size, minimum_size_, maximum_size_);
    return result;
}

void FontDialog::fill_styles() {
    const BundledFontFace& face = bundled_faces[face_index(selection_.role)];
    styles_.clear();
    styles_.push_back(FontStyle::regular);
    styles_.push_back(FontStyle::bold);
    if (face.italic) {
        styles_.push_back(FontStyle::italic);
        styles_.push_back(FontStyle::bold_italic);
    }
    std::vector<std::string> names;
    for (const FontStyle style : styles_) {
        names.push_back(std::string(style_name(style)));
    }
    (*style_list_).items = std::move(names);
}

void FontDialog::show_selection() {
    if (!face_list_) return;
    synchronizing_ = true;
    (*face_list_).select_index(face_index(selection_.role));
    fill_styles();
    const bool bold = selection_.weight >= 700U;
    FontStyle style = bold ? FontStyle::bold : FontStyle::regular;
    if (selection_.italic) style = bold ? FontStyle::bold_italic : FontStyle::italic;
    for (std::size_t index = 0U; index < styles_.size(); ++index) {
        if (styles_[index] == style) (*style_list_).select_index(index);
    }
    (*size_box_).value = selection_.size;
    synchronizing_ = false;
    invalidate(Dirty::paint | Dirty::semantics);
}

void FontDialog::on_face_selected(const ListSelectionChange& change) {
    if (synchronizing_ || change.active_index.absent()) return;
    const std::size_t index = *change.active_index.value();
    if (index >= bundled_faces.size()) return;
    FontSpec next = selection_;
    next.role = bundled_faces[index].role;
    selection_ = normalized(next);
    show_selection();
}

void FontDialog::on_style_selected(const ListSelectionChange& change) {
    if (synchronizing_ || change.active_index.absent() || *change.active_index.value() >= styles_.size()) return;
    const FontStyle style = styles_[*change.active_index.value()];
    const bool bold = style == FontStyle::bold || style == FontStyle::bold_italic;
    selection_.weight = static_cast<std::uint16_t>(bold ? 700U : 400U);
    selection_.italic = style == FontStyle::italic || style == FontStyle::bold_italic;
    invalidate(Dirty::paint | Dirty::semantics);
}

void FontDialog::on_size_changed(double size) {
    if (synchronizing_) return;
    selection_.size = std::clamp(size, minimum_size_, maximum_size_);
    invalidate(Dirty::paint | Dirty::semantics);
}

FontSpec FontDialog::read_view(const decltype(font)&) const {
    return font_;
}

// Resets the selection to the written font even when it is unchanged.
void FontDialog::write_view(const decltype(font)&, FontSpec incoming) {
    require_mutable();
    if (!valid_font_spec(incoming)) throw std::invalid_argument("FontDialog font specification is invalid");
    set_font_from_code(true);
    font_ = incoming;
    selection_ = normalized(incoming);
    show_selection();
}

void FontDialog::set_size_range(double minimum, double maximum) {
    require_mutable();
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum <= 0.0 || maximum < minimum) {
        throw std::invalid_argument("FontDialog size range must be positive and ordered");
    }
    minimum_size_ = minimum;
    maximum_size_ = maximum;
    if (size_box_) (*size_box_).set_range(minimum_size_, maximum_size_);
    selection_ = normalized(selection_);
    show_selection();
}

void FontDialog::prepare_write(const decltype(show_apply)&, bool&) {
    require_mutable();
}

void FontDialog::written(const decltype(show_apply)&) {
    if (apply_button_) (*apply_button_).visible = show_apply.read();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
}

// -- buttons ---------------------------------------------------------------------

void FontDialog::on_ok(ButtonBase&) {
    if (presentation_.shown()) {
        close(DialogResult::ok);
        return;
    }
    // Placed in a layout, the dialog reports without closing anything.
    font_ = selection_;
    const FontDialogResult result{DialogResult::ok, font_};
    closed_.emit(result);
}

void FontDialog::on_cancel(ButtonBase&) {
    if (presentation_.shown()) {
        close(DialogResult::cancel);
        return;
    }
    selection_ = normalized(font_);
    show_selection();
    const FontDialogResult result{DialogResult::cancel, font_};
    closed_.emit(result);
}

void FontDialog::on_apply(ButtonBase&) {
    font_ = selection_;
    apply_.emit(font_);
}

void FontDialog::on_key_preview(KeyEvent& event) {
    if (event.action != KeyAction::down || event.physical_key != PhysicalKey::escape) return;
    if (!presentation_.shown()) return;
    close(DialogResult::cancel);
    event.handled = true;
}

// -- the owned overlay -------------------------------------------------------------

bool FontDialog::show(const Control::Ptr& owner) {
    require_mutable();
    if (presentation_.shown()) return true;
    Window* const window = owner ? (*owner).attached_window() : nullptr;
    if (window == nullptr || parent()) return false;
    Window& host = *window;
    const Size client = host.client_size();
    const std::string prefix(stable_id().value());
    // Holds the dialog across the focus handlers begin_focus_scope runs.
    const Control::Ptr self = shared_from_this();
    std::shared_ptr<FontDialogLayer> layer = make_control<FontDialogLayer>(StableId(prefix + ".layer"));
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    set_paint_plane(PaintPlane::overlay);
    selection_ = normalized(font_);
    show_selection();
    // Until the presentation is installed, every step is undone on an
    // exception: the guard takes the dialog out of the layer, and the
    // owner half's lease and subscription end with it.
    ProvisionalChild in_layer(layer, self);
    PresentationLayer owner_half{};
    owner_half.overlay = host.open_overlay(owner, layer);
    owner_half.layer = layer;
    if (Event<>* closed = owner_half.overlay.closed_event()) {
        owner_half.overlay_closed = (*closed).subscribe(
            *this, Delegate<>::bind<FontDialog, &FontDialog::on_overlay_revoked>(*this));
    }
    // Measured now that the dialog is in the owner's window, at its text
    // scale and with its fonts, then centred.
    const Size wanted = dialog_layout(0.0).preferred;
    const double width = std::min(wanted.width, client.width);
    const double height = std::min(wanted.height, client.height);
    const Rect centred{std::floor((client.width - width) * 0.5), std::floor((client.height - height) * 0.5),
                       width, height};
    set_requested_bounds(centred);
    DialogPresentation shown = surface_ == DialogSurface::native_popup
        ? present_natively(host, owner, centred, std::move(owner_half))
        : DialogPresentation::overlay(std::move(owner_half));
    in_layer.commit();
    presentation_ = std::move(shown);
    // Beginning a focus scope moves the focus, running the previous
    // control's focus and validation handlers; one may close the dialog
    // (and even open it again). A scope this show() began after that is
    // ended here, since the close found none to end (as TaskDialog,
    // A-gf-controls-13-F4).
    // A refused scope (a containing scope, the nesting limit) takes the
    // dialog down again before the refusal leaves, with nothing raised.
    Control::Ptr preferred = face_list_;
    if (preferred && !((*preferred).eligible_for_input() && (*preferred).focusable.read())) preferred = {};
    FocusScopeId started{};
    try {
        if (Window* const popup = presentation_.popup_window()) {
            static_cast<void>((*popup).begin_focus_scope(presentation_.popup_root(), preferred));
        }
        if (!presentation_.shown_with(*layer)) return true;
        if (presentation_.surface() == NativePopupSurface::native) {
            // The empty layer keeps the owner's keys from its controls.
            started = host.begin_focus_scope(layer);
        } else {
            started = host.begin_focus_scope(layer, preferred);
        }
    } catch (...) {
        if (is_alive() && presentation_.shown_with(*layer)) abandon_show();
        throw;
    }
    if (!presentation_.adopt_scope(*layer, started)) {
        if (started) static_cast<void>(host.end_focus_scope(started));
        return true;
    }
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

DialogPresentation FontDialog::present_natively(Window& host, const Control::Ptr& owner, Rect bounds,
                                                PresentationLayer owner_half) {
    const Control::Ptr self = shared_from_this();
    static_cast<void>((*owner_half.layer).remove_child(runtime_id()));
    PresentationPopup popup_half{};
    popup_half.root = make_control<Control>(StableId(std::string(stable_id().value()) + ".native"));
    set_requested_bounds({0.0, 0.0, bounds.width, bounds.height});
    set_paint_plane(PaintPlane::control);
    // Taken out of the root again if the popup does not open, or throws.
    ProvisionalChild in_root(popup_half.root, self);
    NativePopupOptions options{};
    options.focus = HostPopupFocus::takes_focus;
    options.fallback = NativePopupFallback::refuse;
    popup_half.popup = host.open_native_popup(owner, popup_half.root, bounds, options);
    if (popup_half.popup.surface() != NativePopupSurface::native) {
        in_root.undo();
        set_requested_bounds(bounds);
        set_paint_plane(PaintPlane::overlay);
        (*owner_half.layer).add_child(self);
        DialogPresentation fallback = DialogPresentation::overlay(std::move(owner_half));
        return fallback;
    }
    if (Event<>* closed = popup_half.popup.closed_event()) {
        popup_half.popup_closed = (*closed).subscribe(
            *this, Delegate<>::bind<FontDialog, &FontDialog::on_overlay_revoked>(*this));
    }
    in_root.commit();
    DialogPresentation shown = DialogPresentation::native(std::move(owner_half), std::move(popup_half));
    return shown;
}

NativePopupSurface FontDialog::presented_surface() const noexcept {
    const NativePopupSurface result = presentation_.surface();
    return result;
}

void FontDialog::release_overlay(FocusScopeCloseReason reason) {
    // Keep this dialog alive past its layer: the application owns it.
    const Control::Ptr self = shared_from_this();
    // Closed from here on, for every handler the steps below run.
    DialogPresentation closing = presentation_.take();
    closing.dismantle(*this, reason);
    set_paint_plane(PaintPlane::control);
}

// A show whose scope was refused: the dialog leaves its layer and popup and
// both close, with closed not raised (no show was reported either).
void FontDialog::abandon_show() noexcept {
    const Control::Ptr retained = weak_from_this().lock();
    DialogPresentation closing = presentation_.take();
    try {
        closing.dismantle(*this, FocusScopeCloseReason::explicit_close);
        set_paint_plane(PaintPlane::control);
    } catch (...) {
        closing.abandon();
    }
}

void FontDialog::close(DialogResult result) {
    require_mutable();
    if (!presentation_.shown()) return;
    if (result == DialogResult::ok) font_ = selection_;
    release_overlay(FocusScopeCloseReason::explicit_close);
    const FontDialogResult closing{result, font_};
    closed_.emit(closing);
}

void FontDialog::on_overlay_revoked() {
    if (!presentation_.shown()) return;
    release_overlay(FocusScopeCloseReason::owner_unavailable);
    const FontDialogResult closing{DialogResult::cancel, font_};
    closed_.emit(closing);
}

// -- layout and paint ------------------------------------------------------------

FontDialog::DialogLayout FontDialog::dialog_layout(double width) const {
    const double scale = effective_text_scale();
    const double margin = dialog_margin * scale;
    const double gap = column_gap * scale;
    const Size unbounded{measure_limit, measure_limit};
    // What each column, row and button needs: the default scaled, or the
    // measured text with its allowance, whichever is larger.
    double face_need = face_width * scale;
    double style_need = style_width * scale;
    double size_need = size_width * scale;
    double caption_row = caption_height * scale;
    double button_row = button_height * scale;
    double ok_width = button_width * scale;
    double cancel_width = button_width * scale;
    double apply_width = button_width * scale;
    const FontSpec title_font = effective_font(FontSpec{FontRole::control, 13.0, 700, false});
    const TextExtent title_extent = measure_text_extent_utf8("Font", title_font);
    const double title_row = std::max(title_height * scale, std::ceil(title_extent.logical_size.height));
    const double title_need = std::ceil(title_extent.logical_size.width);
    if (face_list_) {
        const double caption_allowance = caption_text_allowance * scale;
        const Size face_caption = (*face_caption_).measure(unbounded);
        const Size style_caption = (*style_caption_).measure(unbounded);
        const Size size_caption = (*size_caption_).measure(unbounded);
        face_need = std::max(face_need, std::ceil(face_caption.width) + caption_allowance);
        style_need = std::max(style_need, std::ceil(style_caption.width) + caption_allowance);
        size_need = std::max(size_need, std::ceil(size_caption.width) + caption_allowance);
        caption_row = std::max({caption_row, std::ceil(face_caption.height), std::ceil(style_caption.height),
                                std::ceil(size_caption.height)});
        const double list_allowance = list_text_allowance * scale;
        const FontSpec face_font = (*face_list_).text_font((*face_list_).font.read());
        const BorrowedSpan<std::string> faces = (*face_list_).items.read();
        for (const std::string& face : faces) {
            const TextExtent extent = (*face_list_).measure_text_extent_utf8(face, face_font);
            face_need = std::max(face_need, std::ceil(extent.logical_size.width) + list_allowance);
        }
        // Every style any face offers, so the column keeps its width when
        // another face changes the list.
        const FontSpec style_font = (*style_list_).text_font((*style_list_).font.read());
        constexpr std::array<FontStyle, 4> all_styles{FontStyle::regular, FontStyle::bold, FontStyle::italic,
                                                      FontStyle::bold_italic};
        for (const FontStyle style : all_styles) {
            const TextExtent extent = (*style_list_).measure_text_extent_utf8(style_name(style), style_font);
            style_need = std::max(style_need, std::ceil(extent.logical_size.width) + list_allowance);
        }
        const Size ok_wanted = (*ok_button_).measure(unbounded);
        const Size cancel_wanted = (*cancel_button_).measure(unbounded);
        const Size apply_wanted = (*apply_button_).measure(unbounded);
        ok_width = std::max(ok_width, std::ceil(ok_wanted.width));
        cancel_width = std::max(cancel_width, std::ceil(cancel_wanted.width));
        apply_width = std::max(apply_width, std::ceil(apply_wanted.width));
        button_row = std::max({button_row, std::ceil(ok_wanted.height), std::ceil(cancel_wanted.height),
                               std::ceil(apply_wanted.height)});
    }
    const bool apply_shown = show_apply.read();
    const double button_gap_scaled = button_gap * scale;
    double buttons_need = ok_width + button_gap_scaled + cancel_width;
    if (apply_shown) buttons_need += button_gap_scaled + apply_width;
    const double columns_need = face_need + gap + style_need + gap + size_need;
    const double preferred_width = std::max({dialog_width * scale, margin * 2.0 + columns_need,
                                             margin * 2.0 + buttons_need, margin * 2.0 + title_need});

    // The columns from the left; the font column takes what the others leave.
    const double inner = std::max(0.0, width - margin * 2.0);
    const double face_column = std::max(0.0, inner - gap * 2.0 - style_need - size_need);
    const double style_left = margin + face_column + gap;
    const double size_left = style_left + style_need + gap;
    DialogLayout placed{};
    placed.title = {margin, title_top * scale, inner, title_row};
    const double caption_top = placed.title.y + title_row + title_caption_gap * scale;
    placed.face_caption = {margin, caption_top, face_column, caption_row};
    placed.style_caption = {style_left, caption_top, style_need, caption_row};
    placed.size_caption = {size_left, caption_top, size_need, caption_row};
    const double list_top = caption_top + caption_row + caption_list_gap * scale;
    const double list_row = list_height * scale;
    const double size_row = size_height * scale;
    placed.face_list = {margin, list_top, face_column, list_row};
    placed.style_list = {style_left, list_top, style_need, list_row};
    placed.size_box = {size_left, list_top, size_need, size_row};
    const double preview_top = list_top + std::max(list_row, size_row) + list_preview_gap * scale;
    placed.preview_well = {margin, preview_top, inner, preview_height * scale};
    const double button_top = preview_top + preview_height * scale + preview_button_gap * scale;
    // The buttons sit at the bottom right: Apply last, when shown.
    double right = width - margin;
    if (apply_shown) {
        placed.apply = {right - apply_width, button_top, apply_width, button_row};
        right -= apply_width + button_gap_scaled;
    }
    placed.cancel = {right - cancel_width, button_top, cancel_width, button_row};
    right -= cancel_width + button_gap_scaled;
    placed.ok = {right - ok_width, button_top, ok_width, button_row};
    placed.preferred = {preferred_width, button_top + button_row + margin};
    return placed;
}

void FontDialog::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    if (!face_list_) return;
    const DialogLayout placed = dialog_layout(final_bounds.width);
    arranged_ = placed;
    set_child_layout(face_caption_, placed.face_caption);
    set_child_layout(style_caption_, placed.style_caption);
    set_child_layout(size_caption_, placed.size_caption);
    set_child_layout(face_list_, placed.face_list);
    set_child_layout(style_list_, placed.style_list);
    set_child_layout(size_box_, placed.size_box);
    if (show_apply.read()) set_child_layout(apply_button_, placed.apply);
    set_child_layout(cancel_button_, placed.cancel);
    set_child_layout(ok_button_, placed.ok);
}

FontDialog::Parts FontDialog::dialog_parts() const {
    const Rect bounds = client_rectangle();
    const Panel::Parts panel = panel_parts();
    // The last arrangement's places: painting measures nothing.
    const DialogLayout& placed = arranged_;
    const double scale = effective_text_scale();
    Parts record;
    record.scrollable = panel.scrollable;
    record.face = PartGeometry::from_bounds(bounds);
    record.title = PartGeometry::from_bounds(placed.title);
    const Rect well = placed.preview_well;
    record.preview_well = PartGeometry::from_bounds(well);
    const Rect text{well.x + 8.0 * scale, well.y + 4.0 * scale, std::max(0.0, well.width - 16.0 * scale),
                    std::max(0.0, well.height - 8.0 * scale)};
    record.preview_text = PartGeometry::from_bounds(text);
    return record;
}

void FontDialog::on_paint(Painter& painter, Rect) {
    const Parts parts = dialog_parts();
    PartPaint part;
    part.family = ControlVisualRole::card;
    part.role = PartRole::face;
    part.bounds = parts.face.bounds;
    paint_part(painter, *this, part);
    part.role = PartRole::label;
    part.bounds = parts.title.bounds;
    FontSpec title_font{FontRole::control, 13.0, 700, false};
    part.font = effective_font(title_font);
    part.text = lend_paint_text("Font");
    paint_part(painter, *this, part);
    part.text = {};
    part.family = ControlVisualRole::editor;
    part.role = PartRole::well;
    part.bounds = parts.preview_well.bounds;
    paint_part(painter, *this, part);
    part.role = PartRole::content;
    part.bounds = parts.preview_text.bounds;
    part.font = effective_font(selection_);
    part.alignment = PartTextAlignment::center;
    part.text = lend_paint_text(preview_text.read());
    paint_part(painter, *this, part);
}

SemanticDescriptor FontDialog::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    // A dialog (12.13); while open over its owner's window, its layer holds
    // the focus and the pointer, so it blocks the rest of the window
    // (modal). Placed in a layout directly it is not modal.
    descriptor.role = SemanticRole::dialog;
    descriptor.modal = presentation_.shown();
    descriptor.name = accessible_name.read().empty() ? "Font" : accessible_name.read();
    const BundledFontFace& face = bundled_faces[face_index(selection_.role)];
    std::string value(face.family);
    value += selection_.weight >= 700U ? " Bold" : "";
    value += selection_.italic ? " Italic" : "";
    value += " " + std::to_string(static_cast<int>(std::lround(selection_.size)));
    descriptor.value = value;
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

bool FontDialog::on_semantic_action(SemanticAction action, std::string_view value) {
    Window* const host = window();
    if (action == SemanticAction::focus && face_list_ && host != nullptr) {
        const bool focused = (*host).request_focus(face_list_);
        return focused;
    }
    const bool handled = Panel::on_semantic_action(action, value);
    return handled;
}

// -- anatomy ---------------------------------------------------------------------

namespace {

constexpr std::array<PartField<FontDialog::Parts>, 4> font_dialog_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &FontDialog::Parts::face},
    {.declaration = {.name = "title", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &FontDialog::Parts::title},
    {.declaration = {.name = "preview_well", .role = PartRole::well, .composed_from = {}, .states = {}},
     .member = &FontDialog::Parts::preview_well},
    {.declaration = {.name = "preview_text", .role = PartRole::content, .composed_from = {}, .states = {}},
     .member = &FontDialog::Parts::preview_text},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 4> font_dialog_parts =
    joined_declarations(scrollable_detail::parts, part_declarations(font_dialog_extra_part_fields));
constexpr std::array<AnatomyState, 5> font_dialog_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ControlAnatomy font_dialog_anatomy{
    .control_id = "font_dialog",
    .parts = font_dialog_parts,
    .states = font_dialog_states,
    .custom_states = {},
    .item_systems = {},
};

// The events FontDialog adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 2> font_dialog_interactions{{
    {.name = "apply", .payload = "FontSpec", .mutable_payload = false},
    {.name = "closed", .payload = "FontDialogResult", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& FontDialog::anatomy() const noexcept {
    return font_dialog_anatomy;
}

FontDialog::Parts FontDialog::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(), record.scrollable);
    read_parts(anatomy(), font_dialog_extra_part_fields, published.parts(), record);
    return record;
}

void FontDialog::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Parts record = dialog_parts();
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), font_dialog_extra_part_fields, record, geometry);
}

void FontDialog::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), font_dialog_interactions.begin(), font_dialog_interactions.end());
}

// property_codemod begin FontDialog (tools/property_families/font_dialog.json; generated, do not edit)

void FontDialog::prepare_write(const decltype(preview_text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("FontDialog preview text must be valid UTF-8");
}

void FontDialog::written(const decltype(preview_text)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

// property_codemod end FontDialog

} // namespace gui_forms
