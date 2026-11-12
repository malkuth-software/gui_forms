#include "gui_forms/controls/font_dialog.hpp"

#include "../panel_anatomy.hpp"

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

// The dialog's layout in logical units: the title, three columns (face,
// style, size) under their captions, the preview, and the buttons.
constexpr double dialog_width = 460.0;
constexpr double dialog_height = 332.0;
constexpr double dialog_margin = 16.0;
constexpr double title_height = 28.0;
constexpr double caption_top = 44.0;
constexpr double caption_height = 20.0;
constexpr double list_top = 66.0;
constexpr double list_height = 92.0;
constexpr double face_width = 172.0;
constexpr double style_left = 200.0;
constexpr double style_width = 118.0;
constexpr double size_left = 330.0;
constexpr double size_width = 114.0;
constexpr double size_height = 30.0;
constexpr double preview_top = 172.0;
constexpr double preview_height = 94.0;
constexpr double button_top = 284.0;
constexpr double button_width = 84.0;
constexpr double button_height = 32.0;
constexpr double button_gap = 8.0;

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

    void on_pointer(PointerEvent& event) override {
        if (event.action == PointerAction::down || event.action == PointerAction::up) {
            event.handled = true;
        }
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
    native_closed_.disconnect();
    native_token_.disconnect();
    overlay_token_.disconnect();
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
    if (synchronizing_ || !change.active_index) return;
    const std::size_t index = *change.active_index;
    if (index >= bundled_faces.size()) return;
    FontSpec next = selection_;
    next.role = bundled_faces[index].role;
    selection_ = normalized(next);
    show_selection();
}

void FontDialog::on_style_selected(const ListSelectionChange& change) {
    if (synchronizing_ || !change.active_index || *change.active_index >= styles_.size()) return;
    const FontStyle style = styles_[*change.active_index];
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
    if (open_) {
        close(DialogResult::ok);
        return;
    }
    // Placed in a layout, the dialog reports without closing anything.
    font_ = selection_;
    const FontDialogResult result{DialogResult::ok, font_};
    closed_.emit(result);
}

void FontDialog::on_cancel(ButtonBase&) {
    if (open_) {
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
    if (!open_) return;
    close(DialogResult::cancel);
    event.handled = true;
}

// -- the owned overlay -------------------------------------------------------------

bool FontDialog::show(const Control::Ptr& owner) {
    require_mutable();
    if (open_) return true;
    Window* const window = owner ? (*owner).attached_window() : nullptr;
    if (window == nullptr || parent()) return false;
    Window& host = *window;
    const Size client = host.client_size();
    const std::string prefix(stable_id().value());
    std::shared_ptr<FontDialogLayer> layer = make_control<FontDialogLayer>(StableId(prefix + ".layer"));
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    const double width = std::min(dialog_width, client.width);
    const double height = std::min(dialog_height, client.height);
    const Rect centred{std::floor((client.width - width) * 0.5), std::floor((client.height - height) * 0.5),
                       width, height};
    set_requested_bounds(centred);
    set_paint_plane(PaintPlane::overlay);
    selection_ = normalized(font_);
    show_selection();
    (*layer).add_child(shared_from_this());
    OverlayToken token = host.open_overlay(owner, layer);
    overlay_layer_ = layer;
    overlay_token_ = std::move(token);
    if (Event<>* closed = overlay_token_.closed_event()) {
        revocation_token_ = (*closed).subscribe(
            *this, Delegate<>::bind<FontDialog, &FontDialog::on_overlay_revoked>(*this));
    }
    if (surface_ == DialogSurface::native_popup && open_native(host, owner, centred)) {
        // The empty layer keeps the owner's keys from its controls.
        overlay_scope_ = host.begin_focus_scope(layer).value;
    } else {
        overlay_scope_ = host.begin_focus_scope(layer, face_list_).value;
    }
    open_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

bool FontDialog::open_native(Window& host, const Control::Ptr& owner, Rect bounds) {
    const Control::Ptr self = shared_from_this();
    static_cast<void>((*overlay_layer_).remove_child(runtime_id()));
    native_root_ = make_control<Control>(StableId(std::string(stable_id().value()) + ".native"));
    set_requested_bounds({0.0, 0.0, bounds.width, bounds.height});
    set_paint_plane(PaintPlane::control);
    (*native_root_).add_child(self);
    NativePopupOptions options{};
    options.focus = HostPopupFocus::takes_focus;
    options.fallback = NativePopupFallback::refuse;
    native_token_ = host.open_native_popup(owner, native_root_, bounds, options);
    if (native_token_.surface() != NativePopupSurface::native) {
        static_cast<void>((*native_root_).remove_child(runtime_id()));
        native_root_.reset();
        set_requested_bounds(bounds);
        set_paint_plane(PaintPlane::overlay);
        (*overlay_layer_).add_child(self);
        return false;
    }
    if (Event<>* closed = native_token_.closed_event()) {
        native_closed_ = (*closed).subscribe(
            *this, Delegate<>::bind<FontDialog, &FontDialog::on_overlay_revoked>(*this));
    }
    Window* const popup = native_token_.window();
    if (popup != nullptr) static_cast<void>((*popup).begin_focus_scope(native_root_, face_list_));
    return true;
}

NativePopupSurface FontDialog::presented_surface() const noexcept {
    if (!open_) return NativePopupSurface::none;
    if (native_root_) return NativePopupSurface::native;
    return NativePopupSurface::overlay;
}

void FontDialog::release_overlay(FocusScopeCloseReason reason) {
    revocation_token_.disconnect();
    native_closed_.disconnect();
    // The scope is the owner's window's, where the layer is.
    Window* const host = overlay_layer_ ? (*overlay_layer_).attached_window() : nullptr;
    if (host != nullptr && overlay_scope_ != 0U) {
        static_cast<void>((*host).end_focus_scope(FocusScopeId{overlay_scope_}, reason));
    }
    overlay_scope_ = 0U;
    // Keep this dialog alive past its layer: the application owns it.
    const Control::Ptr self = shared_from_this();
    if (overlay_layer_) static_cast<void>((*overlay_layer_).remove_child(runtime_id()));
    const Control::Ptr native_root = native_root_;
    native_root_.reset();
    if (native_root) static_cast<void>((*native_root).remove_child(runtime_id()));
    native_token_.disconnect();
    overlay_token_.disconnect();
    overlay_layer_.reset();
    set_paint_plane(PaintPlane::control);
    open_ = false;
}

void FontDialog::close(DialogResult result) {
    require_mutable();
    if (!open_) return;
    if (result == DialogResult::ok) font_ = selection_;
    release_overlay(FocusScopeCloseReason::explicit_close);
    const FontDialogResult closing{result, font_};
    closed_.emit(closing);
}

void FontDialog::on_overlay_revoked() {
    if (!open_) return;
    release_overlay(FocusScopeCloseReason::owner_unavailable);
    const FontDialogResult closing{DialogResult::cancel, font_};
    closed_.emit(closing);
}

// -- layout and paint ------------------------------------------------------------

void FontDialog::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    if (!face_list_) return;
    set_child_layout(face_caption_, {dialog_margin, caption_top, face_width, caption_height});
    set_child_layout(style_caption_, {style_left, caption_top, style_width, caption_height});
    set_child_layout(size_caption_, {size_left, caption_top, size_width, caption_height});
    set_child_layout(face_list_, {dialog_margin, list_top, face_width, list_height});
    set_child_layout(style_list_, {style_left, list_top, style_width, list_height});
    set_child_layout(size_box_, {size_left, list_top, size_width, size_height});
    // The buttons sit at the bottom right: Apply last, when shown.
    double right = final_bounds.width - dialog_margin;
    if (show_apply.read()) {
        set_child_layout(apply_button_, {right - button_width, button_top, button_width, button_height});
        right -= button_width + button_gap;
    }
    set_child_layout(cancel_button_, {right - button_width, button_top, button_width, button_height});
    right -= button_width + button_gap;
    set_child_layout(ok_button_, {right - button_width, button_top, button_width, button_height});
}

FontDialog::Parts FontDialog::dialog_parts() const {
    const Rect bounds = client_rectangle();
    const Panel::Parts panel = panel_parts();
    Parts record;
    record.scrollable = panel.scrollable;
    record.face = PartGeometry::from_bounds(bounds);
    const Rect title{dialog_margin, 8.0, std::max(0.0, bounds.width - dialog_margin * 2.0), title_height};
    record.title = PartGeometry::from_bounds(title);
    const Rect well{dialog_margin, preview_top, std::max(0.0, bounds.width - dialog_margin * 2.0), preview_height};
    record.preview_well = PartGeometry::from_bounds(well);
    const Rect text{well.x + 8.0, well.y + 4.0, std::max(0.0, well.width - 16.0),
                    std::max(0.0, well.height - 8.0)};
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
    descriptor.modal = open_;
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
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &FontDialog::Parts::face},
    {.declaration = {.name = "title", .role = PartRole::label, .composed_from = {}},
     .member = &FontDialog::Parts::title},
    {.declaration = {.name = "preview_well", .role = PartRole::well, .composed_from = {}},
     .member = &FontDialog::Parts::preview_well},
    {.declaration = {.name = "preview_text", .role = PartRole::content, .composed_from = {}},
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
