// FontDialog (stage W): the bundled faces it offers, styles per face, size
// range, preview, Apply, OK and Cancel, the owned overlay with its focus scope
// and revocation, Escape, semantics (focus goes to the font list) and the
// published anatomy.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/font_dialog.hpp"
#include "gui_forms/controls/list_box.hpp"
#include "gui_forms/controls/numeric_up_down.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

struct RecordClosed final {
    std::vector<FontDialogResult>* seen{};
    void operator()(const FontDialogResult& result) const { (*seen).push_back(result); }
};

struct RecordApply final {
    std::vector<FontSpec>* seen{};
    void operator()(const FontSpec& font) const { (*seen).push_back(font); }
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Button> owner;
    std::shared_ptr<FontDialog> dialog;
    std::unique_ptr<Window> window;
};

Fixture make_fixture() {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId("root"));
    fixture.owner = make_control<Button>(StableId("owner"), "Font...");
    (*fixture.owner).set_requested_bounds({10.0, 10.0, 100.0, 30.0});
    (*fixture.root).add_child(fixture.owner);
    fixture.dialog = make_control<FontDialog>(StableId("font"));
    fixture.window = std::make_unique<Window>(fixture.root, Size{800.0, 600.0});
    (*fixture.window).perform_layout();
    return fixture;
}

void faces_and_styles() {
    const std::span<const BundledFontFace> faces = bundled_font_faces();
    require(faces.size() == 3U && faces[0].role == FontRole::control && !faces[0].italic &&
                faces[1].family == "Carlito" && faces[1].italic && faces[2].role == FontRole::monospace,
            "the dialog offers the three bundled role faces");
    Fixture fixture = make_fixture();
    FontDialog& dialog = *fixture.dialog;
    require(dialog.font.read() == FontSpec{FontRole::content, 9.0, 400, false} &&
                (*dialog.face_list()).items.read().size() == 3U && (*dialog.style_list()).items.read().size() == 4U,
            "the default is 9-unit Carlito, with four styles");
    dialog.font = FontSpec{FontRole::monospace, 14.0, 600, true};
    require(dialog.selection() == FontSpec{FontRole::monospace, 14.0, 700, true} &&
                (*dialog.face_list()).selected_index() == 2U && (*dialog.style_list()).selected_index() == 3U &&
                (*dialog.size_box()).value.read() == 14.0,
            "a font is shown at the nearest weight the faces have");
    (*dialog.face_list()).select_index(0U);
    require(dialog.selection().role == FontRole::control && !dialog.selection().italic &&
                dialog.selection().weight == 700U && (*dialog.style_list()).items.read().size() == 2U &&
                (*dialog.style_list()).selected_index() == 1U,
            "Portsmouth Rapids has no italics: the style list shrinks and italic is dropped");
    (*dialog.style_list()).select_index(0U);
    (*dialog.size_box()).value = 20.0;
    require(dialog.selection() == FontSpec{FontRole::control, 20.0, 400, false},
            "style and size controls change the selection");
    dialog.set_size_range(10.0, 16.0);
    require(dialog.selection().size == 16.0 && (*dialog.size_box()).maximum.read() == 16.0,
            "the size range clamps the selection");
    bool threw = false;
    try {
        dialog.set_size_range(12.0, 8.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a reversed size range is refused");
    require(dialog.font.read() == FontSpec{FontRole::monospace, 14.0, 600, true},
            "changing the selection leaves font() until OK or Apply");
}

void overlay_ok_cancel_apply() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    FontDialog& dialog = *fixture.dialog;
    std::vector<FontDialogResult> closed;
    std::vector<FontSpec> applied;
    SubscriptionToken closing = dialog.closed().subscribe(RecordClosed{&closed});
    SubscriptionToken applying = dialog.apply().subscribe(RecordApply{&applied});
    require(!dialog.show(nullptr), "a dialog needs an owner");
    require(dialog.show(fixture.owner) && dialog.is_open() && window.focus_scope_depth() == 1U &&
                window.focused_control() == dialog.face_list() && window.find("font") != nullptr,
            "show opens the dialog over the window and holds the focus in it");
    const Rect bounds = dialog.absolute_bounds();
    require(bounds.x == 170.0 && bounds.y == 134.0, "the dialog is centred in the window");
    require(!window.dispatch_pointer({PointerAction::down, PointerButton::primary, {50.0, 25.0}}) ||
                dialog.is_open(),
            "a press outside the dialog does not reach the page or close it");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, {50.0, 25.0}}));

    (*dialog.size_box()).value = 12.0;
    require((*dialog.apply_button()).perform_click() && applied.size() == 1U && applied[0].size == 12.0 &&
                dialog.font.read().size == 12.0 && dialog.is_open(),
            "Apply takes the selection and keeps the dialog open");
    (*dialog.size_box()).value = 18.0;
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) && !dialog.is_open() &&
                closed.size() == 1U && closed[0].result == DialogResult::cancel &&
                dialog.font.read().size == 12.0 && window.focus_scope_depth() == 0U &&
                window.find("font") == nullptr,
            "Escape cancels: the applied font stays and the overlay and its focus scope go");

    require(dialog.show(fixture.owner), "the dialog opens again");
    require(dialog.selection().size == 12.0, "a reopened dialog starts from font()");
    (*dialog.face_list()).select_index(0U);
    require((*dialog.ok_button()).perform_click() && !dialog.is_open() && closed.size() == 2U &&
                closed[1].result == DialogResult::ok && closed[1].font.role == FontRole::control &&
                dialog.font.read().role == FontRole::control,
            "OK takes the selection and closes");

    require(dialog.show(fixture.owner), "the dialog opens a third time");
    (*fixture.owner).visible = false;
    require(!dialog.is_open() && closed.size() == 3U && closed[2].result == DialogResult::cancel &&
                window.focus_scope_depth() == 0U,
            "hiding the owner revokes the dialog as a cancel");
    (*fixture.owner).visible = true;
    require(dialog.show(fixture.owner) && dialog.is_open(), "the dialog survives its revocation");
    dialog.close(DialogResult::cancel);
}

void embedded_semantics_and_anatomy() {
    Fixture fixture = make_fixture();
    FontDialog& dialog = *fixture.dialog;
    std::vector<FontDialogResult> closed;
    SubscriptionToken closing = dialog.closed().subscribe(RecordClosed{&closed});
    dialog.set_requested_bounds({0.0, 60.0, 460.0, 332.0});
    (*fixture.root).add_child(fixture.dialog);
    (*fixture.window).perform_layout();
    require(!dialog.show(fixture.owner), "a dialog placed in a layout does not open as an overlay");
    (*dialog.style_list()).select_index(1U);
    require((*dialog.ok_button()).perform_click() && closed.size() == 1U && closed[0].result == DialogResult::ok &&
                dialog.font.read().weight == 700U,
            "placed in a layout, OK reports and takes the selection");
    require((*fixture.window).request_focus(fixture.owner), "focus starts on the owner button");
    require((*fixture.window).perform_semantic_action("font", SemanticAction::focus) &&
                (*fixture.window).focused_control() == dialog.face_list(),
            "semantic focus on the dialog moves focus to its font list");

    const ControlAnatomy& anatomy = dialog.anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none && anatomy.control_id == "font_dialog",
            "FontDialog publishes a valid anatomy");
    const FontDialog::Parts parts = dialog.parts();
    require(parts.face.present && parts.title.present && parts.preview_well.present &&
                parts.preview_text.present && parts.preview_well.bounds.contains(
                    Point{parts.preview_text.bounds.x, parts.preview_text.bounds.y}),
            "the face, title and preview parts are published, the text inside its well");
    const SemanticDescriptor descriptor = dialog.semantic_descriptor();
    require(descriptor.role == SemanticRole::dialog && descriptor.name == "Font" &&
                descriptor.value == "Carlito Bold 9" && descriptor.modal == dialog.is_open(),
            "the dialog is a named dialog valued with its selection, modal only while open");
    bool threw = false;
    try {
        dialog.preview_text = std::string("\xFF");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "invalid preview text is refused");
    dialog.show_apply = false;
    require(!(*dialog.apply_button()).visible.read(), "Apply can be hidden");
}

} // namespace

int main() {
    faces_and_styles();
    overlay_ok_cancel_apply();
    embedded_semantics_and_anatomy();
    std::cout << "FontDialog: faces, styles, sizes, overlay, OK, Cancel, Apply, revocation, semantics and anatomy passed\n";
    return 0;
}
