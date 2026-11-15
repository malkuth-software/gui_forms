// A consumer of every public C++ component, built against an installed SDK
// only (tests/sdk_consumer/CMakeLists.txt): the controls in a window, a
// composite made from a document at run time, an application font family,
// and GUI.Drawing's raster seam (execute_recorder); the text masks are
// text_masks_consumer.cpp's, a program linking GUIForms::TextMasks alone. Each
// part calls into the installed archives, so a symbol they lack fails the
// link, and a part that links but misbehaves fails the run.
//
//   sdk_consumer <face.ttf>

#include "gui_forms/composites/composite_control/composite_control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/drawing.hpp"
#include "gui_forms/drawing/recorder_raster/recorder_raster.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/typography/app_fonts/app_fonts.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        failures = failures + 1;
    }
}

// The controls and the window (Controls, Core, Threading, Application).
void controls_in_a_window() {
    const std::shared_ptr<gui_forms::Panel> root =
        gui_forms::make_control<gui_forms::Panel>(gui_forms::StableId(std::string("sdk-consumer")));
    const std::shared_ptr<gui_forms::Button> button =
        gui_forms::make_control<gui_forms::Button>(gui_forms::StableId(std::string("sdk-consumer.button")));
    (*button).set_requested_bounds({8.0, 8.0, 96.0, 28.0});
    (*root).add_child(button);
    gui_forms::Window window(root, {320.0, 200.0});
    window.perform_layout();
    require((*button).parent() != nullptr, "the button joins the panel");
}

// A composite made at run time: the control language's compiler and the
// composite control (Application).
constexpr std::string_view consumer_pair = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<control xmlns="urn:gui-forms:control:1" name="sdk-consumer-pair" version="1.0" base="user-control" family="panel">
  <part name="face" role="face" box="inset(region, 0)"/>
  <children>
    <button id="@action" text="Go"/>
  </children>
  <focus target="none"/>
</control>
)xml";

constexpr std::array<gui_forms::CompositeChildType, 1> consumer_child_types{
    gui_forms::composite_child_type<gui_forms::Button>("button"),
};

void composite_from_a_document() {
    const gui_forms::CompositeLoadResult loaded =
        gui_forms::load_composite_definition("sdk-consumer-pair.control", consumer_pair, consumer_child_types);
    for (const std::string& line : loaded.diagnostics) {
        std::cerr << "  " << line << '\n';
    }
    require(static_cast<bool>(loaded) && loaded.diagnostics.empty(), "the composite document compiles");
    if (!loaded) {
        return;
    }
    const std::shared_ptr<gui_forms::CompositeControl> pair = gui_forms::make_control<gui_forms::CompositeControl>(
        gui_forms::StableId(std::string("pair")), loaded.definition);
    (*pair).set_requested_bounds({0.0, 0.0, 200.0, 80.0});
    gui_forms::Window window(pair, {200.0, 80.0});
    window.perform_layout();
    require((*pair).child("action") != nullptr, "the composite makes its child");
}

std::shared_ptr<const std::vector<std::byte>> read_face(const char* const path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> characters{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(characters.size());
    for (std::size_t index = 0U; index < characters.size(); ++index) {
        bytes[index] = static_cast<std::byte>(characters[index]);
    }
    const std::shared_ptr<const std::vector<std::byte>> face =
        std::make_shared<const std::vector<std::byte>>(std::move(bytes));
    return face;
}

// An application font family: validation, load, admission and unload (Core).
void application_font_family(const char* const face_path) {
    const std::shared_ptr<const std::vector<std::byte>> bytes = read_face(face_path);
    require(!(*bytes).empty(), "the face file reads");
    const std::span<const std::byte> view((*bytes).data(), (*bytes).size());
    require(gui_forms::validate_app_font(view).status == gui_forms::AppFontStatus::loaded, "the face validates");
    gui_forms::AppFontFace face{};
    face.bytes = bytes;
    const std::array<gui_forms::AppFontFace, 1> faces{face};
    gui_forms::AppFontLoad loaded = gui_forms::load_app_font_family("SDK Consumer Face", faces);
    const gui_forms::FontFamilyId family = loaded.family.id();
    require(family.value != 0U, "the family loads");
    gui_forms::FontSpec font{};
    font.family = family;
    require(gui_forms::admit_font(font, gui_forms::FontContext::composite).family == family,
            "a composite admits the family");
    require(loaded.family.unload() == gui_forms::AppFontUnload::released, "the unused family unloads at once");
}

// GUI.Drawing's raster seam: a recording drawn into a Bitmap (Drawing, and
// the raster archive Application carries).
void drawing_raster_seam() {
    gui_drawing::Bitmap bitmap(4, 4);
    gui_drawing::GraphicsRecorder recorder;
    recorder.clear(gui_drawing::Color::from_argb(UINT32_C(0xff336699)));
    const gui_drawing::RecorderRasterResult result = gui_drawing::execute_recorder(recorder, bitmap, 0U);
    require(result.status == gui_drawing::RecorderRasterStatus::executed && result.commands_executed == 1U,
            "the raster runs the recording");
    require(bitmap.get_pixel(2, 2).argb() == UINT32_C(0xff336699), "the raster fills the bitmap");
}

} // namespace

int main(int argument_count, char** arguments) {
    if (argument_count != 2) {
        std::cerr << "usage: sdk_consumer <face.ttf>\n";
        return 2;
    }
    controls_in_a_window();
    composite_from_a_document();
    application_font_family(arguments[1]);
    drawing_raster_seam();
    if (failures != 0) {
        std::cerr << failures << " SDK consumer check(s) failed\n";
        return 1;
    }
    std::cout << "sdk_consumer: every component linked and ran\n";
    return 0;
}
