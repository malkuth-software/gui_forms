#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/engine/light_planes/light_planes.hpp"
#include "gui_forms/controls/indicator_lamp_values.hpp"
#include "gui_forms/controls/indicator_lamp_capabilities.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace gui_forms {

// An indicator lamp (skeudoindustrial surfaces, follow-up 4): a round lens
// in a bezel that shows a state, off, on or error, with an optional legend
// beside it. Every theme can show one: the default theme and the familiar
// themes style its parts (`indicator-lamp::part(lens):state(on)`), and a
// theme that does not draws it with its built-in recipes, the lit lens as a
// progress fill (on) or a notice (error).
//
// A lamp is never clickable and takes no focus: the pointer reaches what
// lies beneath it. It lights only its own lens: the lit lens is drawn
// clipped to the lens, and nothing of it reaches the bezel, the legend or
// a neighbour. A change of state warms the lens up (the strike envelope,
// engine_light::lamp_power) or cools it down (lamp_cooling_power) over a
// few frames, and at once under reduced motion or outside a window.
//
// To assistive technology a lamp is static text named by its accessible
// name or else its legend, with its state as the value ("on", "off" or
// "error"), so the state never rests on colour alone.
//
// A lamp may show baked light (skeudoindustrial surfaces): its lens is
// then composited from the bake its light_planes property names, read
// through its window's resources (ApplicationResources::
// set_light_plane_directory), with every lamp of the bake lit in
// lamp_color at the warm-up's power, so one bake serves every colour. The
// bake carries the lens's whole look, its body included: the lens takes the
// bake's aspect (the largest such rectangle in the client area without a
// legend, else in the bezel's square), and the bezel gives way. Without the
// bake, and in forced high contrast, the theme's lens is drawn.
class GUI_FORMS_API IndicatorLamp final : public Control, public IndicatorLampCapabilities<IndicatorLamp> {
public:
    explicit IndicatorLamp(StableId stable_id, std::string text = {});

    // How lit the lens is now, from 0 (dark) to 1 (fully lit): the warm-up
    // or cool-down's current power, or the state's settled one.
    [[nodiscard]] double power() const noexcept {
        return power_;
    }
    // Whether a warm-up or cool-down is running.
    [[nodiscard]] bool changing() const noexcept;
    // Whether the lens is composited from its bake now: light_planes names
    // one that the window's resources hold, outside forced high contrast.
    [[nodiscard]] bool light_planes_shown() const;
    // The value the lamp reports to assistive technology for `state`.
    [[nodiscard]] static std::string_view state_name(IndicatorLampState state) noexcept;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `bezel` is the
    // ring the lens sits in, `lens` the glass that lights, and `label` the
    // legend, absent while the text is empty.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry bezel;
        PartGeometry lens;
        PartGeometry label;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_detached_from_window() noexcept override;

private:
    // The warm-up's and cool-down's frames: observes the lamp, so a
    // destroyed lamp's tick is skipped.
    struct ChangeTick final {
        std::weak_ptr<IndicatorLamp> lamp;
        AnimationTickResult operator()(FrameTime now) const;
    };

    // Where the parts lie in the client area: the bezel at the start, the
    // lens inside it and the legend after it.
    struct Placement final {
        Rect bezel{};
        Rect lens{};
        Rect label{};
    };
    // `bake`, when not null, is the bake the lens is composited from.
    [[nodiscard]] Placement placement(const engine_light::LightPlanes* bake) const;
    // The bake light_planes names, from the window's resources; null when
    // none is named or held, outside a window, or in forced high contrast.
    [[nodiscard]] std::shared_ptr<const engine_light::LightPlanes> shown_bake() const;
    // The lit lamp's colour at unit power: lamp red for an error, else
    // lamp_color, else lamp amber.
    [[nodiscard]] engine_light::LampColour lit_colour() const noexcept;
    // The lens drawn as the theme's: the dark lens, then the lit lens over
    // it at the current power, clipped to the lens.
    void paint_theme_lens(Painter& painter, const Placement& placed, AnatomyStateSet states);
    // The lens composited from `bake` at the current power.
    void paint_baked_lens(Painter& painter, const Placement& placed, AnatomyStateSet states,
                          std::shared_ptr<const engine_light::LightPlanes> bake);
    [[nodiscard]] FontSpec legend_font() const noexcept;
    [[nodiscard]] bool reduced_motion() const noexcept;
    // Starts the change to the state just written: at once, or a warm-up or
    // cool-down timed by the timer's clock.
    void begin_change();
    AnimationTickResult advance(FrameTime now);

    double power_{};
    // The lit state the lens shows while it is lit or cooling (on or
    // error); off only before the lamp is first lit.
    IndicatorLampState lit_state_{IndicatorLampState::off};
    // The change's first frame, taken from the timer's clock, and the power
    // a cool-down starts from.
    std::optional<FrameTime> changed_at_{};
    double cooling_from_{};
    AnimationTimer change_timer_;
    // The baked lens's last composite, kept between paints; released when
    // the lamp leaves its window.
    engine_light::LightPlaneRaster baked_lens_{};
    // property_codemod begin IndicatorLamp (tools/property_families/indicator_lamp.json; generated, do not edit)
public:
    // IndicatorLamp's property table: Control's entries, then its own.
    static constexpr std::size_t indicator_lamp_property_count = Control::control_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, indicator_lamp_property_count>& indicator_lamp_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct IndicatorLampStateChoiceCapability<IndicatorLamp>;
    friend struct IndicatorLampTextCapability<IndicatorLamp>;
    friend struct IndicatorLampLightPlanesCapability<IndicatorLamp>;
    friend struct IndicatorLampLampColorCapability<IndicatorLamp>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(state)& cell, IndicatorLampState& incoming);
    void written(const decltype(state)& cell);
    void prepare_write(const decltype(text)& cell, std::string& incoming);
    void written(const decltype(text)& cell);
    void prepare_write(const decltype(light_planes)& cell, std::string& incoming);
    void written(const decltype(light_planes)& cell);
    void prepare_write(const decltype(lamp_color)& cell, std::optional<Color>& incoming);
    void written(const decltype(lamp_color)& cell);
    // property_codemod end IndicatorLamp
};

} // namespace gui_forms
