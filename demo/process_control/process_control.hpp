#pragma once

#include "gui_forms/controls/button.hpp"
#include "gui_forms/engine/light_planes/light_planes.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// The process-control demonstrator (owner, 2026-11-22; skeudoindustrial
// surfaces, phase 3): the owner's process-control dialog
// (notes:experiments/industrial-surfaces/demo.html) as a program of its
// own, built from the library's general primitives and nothing special to
// it:
// - the dialog's blasted enamel and its heavier title band
//   (`finish(blasted-enamel-glint, ...)`), with a picker for the ГОСТ paint
//   under the coat and the seed of its noise;
// - its three status lamps, jewels lit from the shipped indicator bake,
//   each switched off, on, error or fault (failing: it flickers and
//   stutters, steady under reduced motion) from the side strip;
// - its CANCEL and OK keys (LampKey), drawn from the shipped key bakes,
//   each lit only while held, the lamp and the press moved by the engine's
//   motion;
// - the lamp radio row (RadioButton's lamp look) with its colour set at run
//   time, and the coolant flow pinpoint.
// The window is laid out by flex panels (owner, 2026-11-22: the controls
// follow the window): the dialog grows with the window, never below the
// study's 500 x 310, keeping its type and lamps at the study's places from
// its upper left and its keys at their size at its lower right; the strip
// keeps its width beside it.
// The program's theme is one sheet set on the root (Control::
// set_theme_sheet), rebuilt when the paint or the seed changes.
//
// Demo code over public GUI.Forms vocabulary: no host, renderer or
// platform type appears here. The hosts' mains are src/host/*/
// process_control_main.*; every one takes the demonstration programs'
// options (program_options.hpp), --headless --capture and --check
// included.
namespace gui_forms::process_control {

inline constexpr std::string_view program_name = "GUI.Forms Process Control";
// The window's first logical size, and its least: the study's 500 x 310
// dialog, the message under it and the strip beside it.
inline constexpr Size window_size{840.0, 440.0};

// One ГОСТ paint the enamel can be laid over (demo/themes/gost_paints,
// ГОСТ 22133-86's colorimetry).
struct EnamelPaint final {
    std::string_view name{};
    std::string_view body{};
    std::string_view band{};
};

// The paints, the study's first: ГОСТ 397 blue-green under ГОСТ 832's band.
inline constexpr std::array<EnamelPaint, 3> enamel_paints{{
    {.name = "ГОСТ 397 blue-green", .body = "#4d676d", .band = "#656f74"},
    {.name = "ГОСТ 365 grey-green", .body = "#80948a", .band = "#4d676d"},
    {.name = "ГОСТ 832 dark grey", .body = "#656f74", .band = "#4d676d"},
}};

// The study's seed.
inline constexpr std::uint32_t study_seed = 11U;

// The paint's grain (finish()'s fifth argument): the blast's specks and
// facets 1.5 times the preset's size. The study's dialog.html draws its
// body at two device pixels a logical unit and shows that canvas one pixel
// a CSS pixel, so on a 2x display its grain is twice the size a window at
// scale 2 gives the same preset; the owner found the paint's grain a
// little too fine beside it (2026-11-22), so the demonstrator goes half
// way there.
inline constexpr double paint_grain = 1.5;

// A held key's lamp power: the bakes' unit (demo.html drives both keys'
// lamps at 1).
inline constexpr double key_lit_power = 1.0;

// When a LampKey's lamp is lit, as the study lights its two keys
// (notes:experiments/industrial-surfaces: demo.html, dialog.html,
// dialog_demo.cpp, lampWhenHeld).
enum class LampKeyLighting {
    // Lit only while held, and only while the lamps are on: the study's OK.
    while_held,
    // Lit throughout, whatever the Lamps toggle says: the study's CANCEL
    // (owner, 2026-11-22: CANCEL is supposed to stay lit).
    always,
};

// A dialog key drawn from a shipped key bake (<stem>_up and <stem>_down,
// engine_light::LightPlanes), its lamp and its press moved by the engine's
// motion (owner, 2026-11-22: OK must not sit at constant full light).
//
// A while_held key rests with the room's light alone, its lamp off; held
// (pointer or Space/Enter), the lamp strikes (key_strike_floor, then full
// over lamp_strike_seconds) and the key goes down (key_press_duration);
// released, the key comes up and the lamp cools over lamp_cool_seconds.
// An always key keeps its lamp at key_lit_power and only goes down and up.
// Hover, focus and the default key leave the lamp as it is, as in the study
// (focus shows the theme's focus cue). Reduced motion and an occluded
// window jump to the end (the motion clock). With the lamps switched off
// (set_lamps_enabled) a while_held key presses but does not light.
//
// The face is composited by the key's own LightPlaneRaster through
// PartPaint::light_planes, as IndicatorLamp composites its bake: the
// light-map theme primitive cannot animate a key this size within the
// frame budget. Where the bake cannot be lent, or in forced high contrast,
// the theme's button is drawn instead.
class LampKey final : public Button {
public:
    // A key drawn from the shipped bakes <stem>_up and <stem>_down, its
    // lamps in `lamp_colour` (sRGB), lit as `lighting` says.
    LampKey(StableId id, std::string_view stem, Color lamp_colour, LampKeyLighting lighting);

    // Whether the lamp lights while held (the strip's Lamps toggle).
    void set_lamps_enabled(bool enabled);
    // The lamp's power now (key_lit_power when lit, else 0) and how far the
    // key is down (0 to 1), as the last frame left them.
    [[nodiscard]] double lamp_power() const noexcept { return lamp_.current(); }
    [[nodiscard]] double press_share() const noexcept { return press_.current(); }

    void on_paint(Painter& painter, Rect local_damage) override;
    void on_focus_changed(bool focused) override;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_key_down(KeyEventArgs& arguments) override;
    void on_key_up(KeyEventArgs& arguments) override;
    void on_detached_from_window() noexcept override;

private:
    // Moves the lamp and the press toward what the key's held state asks
    // for; a target already set leaves its move running.
    void follow_press();

    std::string up_name_;
    std::string down_name_;
    Color lamp_colour_;
    LampKeyLighting lighting_;
    bool lamps_enabled_{true};
    MotionValue lamp_;
    MotionValue press_;
    engine_light::LightPlaneRaster raster_{};
};

// The program's theme sheet for the paint at `paint` (an index into
// enamel_paints, wrapped) and `seed`: the enamel and title band classes.
[[nodiscard]] std::string process_control_sheet(std::size_t paint, std::uint32_t seed);

// The demonstrator's window, laid out and themed.
[[nodiscard]] std::unique_ptr<Window> make_process_control();

// Sets the check box with stable id `id` as a click would (capture
// staging, --check); false when the window holds no such check box.
[[nodiscard]] bool check_process_control_box(Window& window, std::string_view id);

} // namespace gui_forms::process_control
