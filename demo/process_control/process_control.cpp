#include "process_control.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/indicator_lamp.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/radio_button.hpp"
#include "gui_forms/engine/light_planes/light_planes.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <stxlib/optional.hpp>
#include <utility>

namespace gui_forms::process_control {
namespace {

// -- the program's visual choices ---------------------------------------------------------
//
// The study's lamp colours (demo.html: green [0.16, 1, 0.25], amber [1,
// 0.5, 0.08] and red [1, 0.12, 0.04], engine_light::lamp_green, lamp_amber
// and lamp_red) encoded as sRGB and rounded, and a blue for the radio's
// third choice (engine_light::lamp_blue, likewise).
constexpr Color lamp_green_srgb = Color::rgba(111, 255, 137);
constexpr Color lamp_amber_srgb = Color::rgba(255, 188, 80);
constexpr Color lamp_red_srgb = Color::rgba(255, 97, 56);
constexpr Color lamp_blue_srgb = Color::rgba(89, 161, 255);
// The study's ivory type over the enamel, and the strip's ink.
constexpr Color enamel_ink = Color::rgba(204, 199, 172);
constexpr Color strip_ink = Color::rgba(32, 36, 38);
// The keys' lamp colours: CANCEL's red lamps, OK's green (the light-map
// rules the Gallery's page uses).
constexpr Color key_cancel_lamp = Color::rgba(255, 97, 56);
constexpr Color key_ok_lamp = Color::rgba(111, 255, 137);
// The strike (lamp_keys.html, powerAt): a lamp switched on shows 40% at
// once and rises linearly to full over engine_light::lamp_strike_seconds.
constexpr double key_strike_floor = 0.4;
// The press (lamp_keys.html: 70 ms, easing out).
constexpr FrameInterval key_press_duration = std::chrono::milliseconds(70);
// A motion value's extent for a share from 0 to 1, so that its settled
// condition (a fraction of a device pixel over the extent) is a fine share.
constexpr double share_extent = 256.0;

[[nodiscard]] FrameInterval seconds_interval(double seconds) noexcept {
    const FrameInterval interval =
        std::chrono::duration_cast<FrameInterval>(std::chrono::duration<double>(seconds));
    return interval;
}

} // namespace

LampKey::LampKey(StableId id, std::string_view stem, Color lamp_colour, LampKeyLighting lighting)
    : Button(std::move(id)), up_name_(std::string(stem) + "_up"), down_name_(std::string(stem) + "_down"),
      lamp_colour_(lamp_colour), lighting_(lighting),
      lamp_(*this, MotionCurve::timed(key_press_duration, MotionEasing::linear),
            lighting == LampKeyLighting::always ? key_lit_power : 0.0),
      press_(*this, MotionCurve::timed(key_press_duration, MotionEasing::exponential_approach)) {
    static_cast<void>(lamp_.set_extent(share_extent));
    static_cast<void>(press_.set_extent(share_extent));
    lamp_.set_invalidation(Dirty::paint);
    press_.set_invalidation(Dirty::paint);
}

void LampKey::set_lamps_enabled(bool enabled) {
    lamps_enabled_ = enabled;
    follow_press();
}

void LampKey::on_focus_changed(bool focused) {
    Button::on_focus_changed(focused);
    follow_press();
}

void LampKey::on_mouse_down(MouseEventArgs& arguments) {
    Button::on_mouse_down(arguments);
    follow_press();
}

void LampKey::on_mouse_up(MouseEventArgs& arguments) {
    Button::on_mouse_up(arguments);
    follow_press();
}

void LampKey::on_mouse_move(MouseEventArgs& arguments) {
    Button::on_mouse_move(arguments);
    follow_press();
}

void LampKey::on_mouse_leave() {
    Button::on_mouse_leave();
    follow_press();
}

void LampKey::on_key_down(KeyEventArgs& arguments) {
    Button::on_key_down(arguments);
    follow_press();
}

void LampKey::on_key_up(KeyEventArgs& arguments) {
    Button::on_key_up(arguments);
    follow_press();
}

void LampKey::on_detached_from_window() noexcept {
    Button::on_detached_from_window();
    lamp_.finish();
    press_.finish();
    raster_.release();
}

void LampKey::follow_press() {
    const bool held = pressed_visual() && effectively_enabled();
    const double press_target = held ? 1.0 : 0.0;
    if (press_.target() != press_target) static_cast<void>(press_.animate_to(press_target));
    bool lit = false;
    switch (lighting_) {
    case LampKeyLighting::always:
        lit = true;
        break;
    case LampKeyLighting::while_held:
    default:
        lit = held && lamps_enabled_;
        break;
    }
    const double lamp_target = lit ? key_lit_power : 0.0;
    if (lamp_.target() == lamp_target) return;
    if (lamp_target > 0.0) {
        // Struck: 40% at once, then full, linearly.
        const double floor = key_strike_floor * lamp_target;
        if (lamp_.current() < floor) lamp_.jump_to(floor);
        lamp_.set_curve(MotionCurve::timed(seconds_interval(engine_light::lamp_strike_seconds), MotionEasing::linear));
    } else {
        lamp_.set_curve(
            MotionCurve::timed(seconds_interval(engine_light::lamp_cool_seconds), MotionEasing::exponential_approach));
    }
    static_cast<void>(lamp_.animate_to(lamp_target));
    invalidate(Dirty::paint);
}

void LampKey::on_paint(Painter& painter, Rect local_damage) {
    const SurfaceRoot* const root = attached_surface_root();
    std::shared_ptr<const engine_light::LightPlanes> up{};
    std::shared_ptr<const engine_light::LightPlanes> down{};
    if (root != nullptr) {
        const std::shared_ptr<ApplicationResources> resources = (*root).resources();
        if (resources) {
            up = (*resources).lend_light_planes(up_name_);
            down = (*resources).lend_light_planes(down_name_);
        }
    }
    if (!up) {
        Button::on_paint(painter, local_damage);
        return;
    }
    const Rect bounds = local_bounds();
    const AnatomyStateSet states = anatomy_states();
    const engine_light::LampDrive drive{engine_light::lamp_colour_from_srgb(lamp_colour_), lamp_.current()};
    engine_light::LightComposite settings{};
    settings.press_mix = press_.current();
    PartPaint face;
    face.family = ControlVisualRole::button;
    face.role = PartRole::face;
    face.bounds = bounds;
    face.states = states;
    face.key.set(PartKey{"face", {}, {}});
    face.light_planes.set(PartLightPlanes{.planes = std::move(up),
                                          .down = std::move(down),
                                          .drive = drive,
                                          .settings = settings,
                                          .raster = &raster_});
    paint_part(painter, *this, face);
    PartPaint focus;
    focus.family = ControlVisualRole::button;
    focus.role = PartRole::focus;
    focus.bounds = bounds;
    focus.states = states;
    focus.key.set(PartKey{"focus", {}, {}});
    paint_part(painter, *this, focus);
}

namespace {

// The status lamps, in the dialog's order.
struct StatusLamp final {
    std::string_view id{};
    std::string_view legend{};
    std::string_view name{};
    Color colour{};
    IndicatorLampState initial{};
};

constexpr std::array<StatusLamp, 3> status_lamps{{
    {.id = "process.status.pump-ready", .legend = "PUMP READY", .name = "Pump ready", .colour = lamp_green_srgb,
     .initial = IndicatorLampState::on},
    {.id = "process.status.heating", .legend = "HEATING", .name = "Heating", .colour = lamp_amber_srgb,
     .initial = IndicatorLampState::on},
    {.id = "process.status.fault", .legend = "FAULT", .name = "Fault", .colour = lamp_red_srgb,
     .initial = IndicatorLampState::fault},
}};

// The radio lamps' colours, in the strip's order.
struct RadioColour final {
    std::string_view id{};
    std::string_view text{};
    Color colour{};
};

constexpr std::array<RadioColour, 3> radio_colours{{
    {.id = "process.radio.green", .text = "Green", .colour = lamp_green_srgb},
    {.id = "process.radio.amber", .text = "Amber", .colour = lamp_amber_srgb},
    {.id = "process.radio.blue", .text = "Blue", .colour = lamp_blue_srgb},
}};

// The state after `state` as the strip's buttons cycle it.
IndicatorLampState next_state(IndicatorLampState state) noexcept {
    switch (state) {
    case IndicatorLampState::off: return IndicatorLampState::on;
    case IndicatorLampState::on: return IndicatorLampState::error;
    case IndicatorLampState::error: return IndicatorLampState::fault;
    case IndicatorLampState::fault: return IndicatorLampState::off;
    }
    return IndicatorLampState::off;
}

template <typename Child>
void add_at(Control& parent, const std::shared_ptr<Child>& child, Rect bounds) {
    (*child).set_requested_bounds(bounds);
    parent.add_child(child);
}

// A label in the control face at `size`, `spacing` logical pixels between
// its clusters (the study's dialog type: 1 for the title and heading, 0.5
// for the subtitle).
std::shared_ptr<Label> make_label(std::string id, std::string text, double size, Color colour,
                                  double spacing = 0.0) {
    std::shared_ptr<Label> made = make_control<Label>(StableId(std::move(id)), std::move(text));
    (*made).font = stxlib::Optional<FontSpec>::of(FontSpec{FontRole::control, size, 400, false, spacing});
    (*made).fore_color = stxlib::Optional<Color>::of(colour);
    return made;
}

// The window's root: it holds the controls the program changes and their
// handlers, and sets the program's theme sheet.
class ProcessControlRoot final : public Panel {
public:
    explicit ProcessControlRoot(StableId id) : Panel(std::move(id)) {}

    void build();

private:
    void build_dialog();
    void build_strip();
    void apply_sheet();
    void show_state_button(std::size_t lamp);
    void on_state_clicked(std::size_t lamp);
    void on_paint_clicked();
    void on_seed_down();
    void on_seed_up();
    void on_radio_colour(std::size_t colour);
    void on_lamps_changed(bool on);
    void on_coolant_changed(bool on);
    void on_ok();
    void on_cancel();

    std::array<std::shared_ptr<IndicatorLamp>, 3> lamps_{};
    std::array<std::shared_ptr<Button>, 3> state_buttons_{};
    std::array<std::shared_ptr<RadioButton>, 2> radios_{};
    std::shared_ptr<LampKey> ok_{};
    std::shared_ptr<LampKey> cancel_{};
    std::shared_ptr<Button> paint_button_{};
    std::shared_ptr<Label> seed_label_{};
    std::shared_ptr<Label> message_{};
    std::shared_ptr<IndicatorLamp> flow_{};
    std::size_t paint_{};
    std::uint32_t seed_{study_seed};
};

void ProcessControlRoot::build() {
    build_dialog();
    build_strip();
    apply_sheet();
}

// The study's 500 x 310 body at the study's places: the title band, the
// type, the status lamps as jewels (centred 36 units in, 145, 174 and 203
// down, 14 units across) and the keys (120 x 52.5 at 228 and 360 across,
// 237.5 down), as the Gallery's industrial-surfaces page lays them out.
void ProcessControlRoot::build_dialog() {
    std::shared_ptr<Panel> dialog = make_control<Panel>(StableId("process.dialog"));
    static_cast<void>((*dialog).add_class("blasted-enamel"));
    add_at(*this, dialog, Rect{16.0, 16.0, 500.0, 310.0});
    std::shared_ptr<Panel> band = make_control<Panel>(StableId("process.dialog.title-band"));
    static_cast<void>((*band).add_class("blasted-enamel-heavy"));
    add_at(*dialog, band, Rect{0.0, 0.0, 500.0, 39.0});
    add_at(*dialog, make_label("process.dialog.title", "PROCESS CONTROL", 13.0, enamel_ink, 1.0),
           Rect{17.0, 8.0, 320.0, 24.0});
    add_at(*dialog, make_label("process.dialog.heading", "START TRANSFER", 18.0, enamel_ink, 1.0),
           Rect{24.0, 62.0, 440.0, 30.0});
    add_at(*dialog, make_label("process.dialog.subtitle", "PUMP TO RESERVE TANK", 10.0, enamel_ink, 0.5),
           Rect{24.0, 95.0, 440.0, 18.0});
    for (std::size_t index = 0U; index < status_lamps.size(); ++index) {
        const StatusLamp& choice = status_lamps[index];
        std::shared_ptr<IndicatorLamp> lamp =
            make_control<IndicatorLamp>(StableId(std::string(choice.id)), std::string(choice.legend));
        (*lamp).accessible_name = std::string(choice.name);
        (*lamp).fore_color = stxlib::Optional<Color>::of(enamel_ink);
        (*lamp).light_planes = std::string(engine_light::jewel_lamp_bake);
        (*lamp).lamp_color = stxlib::Optional<Color>::of(choice.colour);
        (*lamp).state = choice.initial;
        add_at(*dialog, lamp, Rect{29.0, 138.0 + 29.0 * static_cast<double>(index), 220.0, 14.0});
        lamps_[index] = lamp;
    }
    // The bakes carry the legends: the keys have no text, only their names.
    cancel_ = make_control<LampKey>(StableId("process.key.cancel"), "dialog_cancel", key_cancel_lamp,
                                       LampKeyLighting::always);
    (*cancel_).accessible_name = "Cancel";
    add_at(*dialog, cancel_, Rect{228.0, 237.0, 120.0, 53.0});
    ok_ = make_control<LampKey>(StableId("process.key.ok"), "dialog_ok", key_ok_lamp, LampKeyLighting::while_held);
    (*ok_).accessible_name = "OK";
    add_at(*dialog, ok_, Rect{360.0, 237.0, 120.0, 53.0});
    on((*ok_).clicked(), *this, &ProcessControlRoot::on_ok);
    on((*cancel_).clicked(), *this, &ProcessControlRoot::on_cancel);

    message_ = make_label("process.message", "Ready.", 11.0, strip_ink);
    add_at(*this, message_, Rect{16.0, 336.0, 500.0, 24.0});
}

// The strip beside the dialog: each status lamp's state, the paint and its
// seed, the lamp radio and its colour, the coolant pinpoint and the lamps.
void ProcessControlRoot::build_strip() {
    constexpr double left = 532.0;
    constexpr double width = 292.0;
    add_at(*this, make_label("process.strip.lamps", "Status lamps", 11.0, strip_ink), Rect{left, 16.0, width, 20.0});
    for (std::size_t index = 0U; index < status_lamps.size(); ++index) {
        std::shared_ptr<Button> button =
            make_control<Button>(StableId(std::string(status_lamps[index].id) + ".state"));
        add_at(*this, button, Rect{left, 40.0 + 32.0 * static_cast<double>(index), width, 28.0});
        on((*button).clicked(), *this, &ProcessControlRoot::on_state_clicked, index);
        state_buttons_[index] = button;
        show_state_button(index);
    }

    add_at(*this, make_label("process.strip.paint", "Paint", 11.0, strip_ink), Rect{left, 144.0, width, 20.0});
    paint_button_ = make_control<Button>(StableId("process.paint"), "Paint: " + std::string(enamel_paints[0].name));
    add_at(*this, paint_button_, Rect{left, 166.0, width, 28.0});
    on((*paint_button_).clicked(), *this, &ProcessControlRoot::on_paint_clicked);
    std::shared_ptr<Button> seed_down = make_control<Button>(StableId("process.seed.down"), "Seed -");
    add_at(*this, seed_down, Rect{left, 200.0, 88.0, 28.0});
    on((*seed_down).clicked(), *this, &ProcessControlRoot::on_seed_down);
    seed_label_ = make_label("process.seed", "Seed " + std::to_string(seed_), 11.0, strip_ink);
    add_at(*this, seed_label_, Rect{left + 100.0, 204.0, 92.0, 20.0});
    std::shared_ptr<Button> seed_up = make_control<Button>(StableId("process.seed.up"), "Seed +");
    add_at(*this, seed_up, Rect{left + width - 88.0, 200.0, 88.0, 28.0});
    on((*seed_up).clicked(), *this, &ProcessControlRoot::on_seed_up);

    add_at(*this, make_label("process.strip.radio", "Feed", 11.0, strip_ink), Rect{left, 240.0, width, 20.0});
    constexpr std::array<std::string_view, 2> feeds{"Manual feed", "Automatic feed"};
    for (std::size_t index = 0U; index < feeds.size(); ++index) {
        std::shared_ptr<RadioButton> radio = make_control<RadioButton>(
            index == 0U ? StableId("process.feed.manual") : StableId("process.feed.automatic"), std::string(feeds[index]));
        (*radio).group_name = "process-feed";
        (*radio).indicator_style = ChoiceIndicatorStyle::lamp;
        (*radio).checked = index == 0U;
        add_at(*this, radio, Rect{left, 262.0 + 28.0 * static_cast<double>(index), 180.0, 24.0});
        radios_[index] = radio;
    }
    for (std::size_t index = 0U; index < radio_colours.size(); ++index) {
        std::shared_ptr<Button> button =
            make_control<Button>(StableId(std::string(radio_colours[index].id)), std::string(radio_colours[index].text));
        add_at(*this, button, Rect{left + 100.0 * static_cast<double>(index), 322.0, 92.0, 28.0});
        on((*button).clicked(), *this, &ProcessControlRoot::on_radio_colour, index);
    }

    std::shared_ptr<CheckBox> coolant = make_control<CheckBox>(StableId("process.coolant"), "Coolant");
    (*coolant).checked = true;
    add_at(*this, coolant, Rect{left, 362.0, 140.0, 24.0});
    on((*coolant).checked_changed(), *this, &ProcessControlRoot::on_coolant_changed);
    flow_ = make_control<IndicatorLamp>(StableId("process.coolant-flow"), "Coolant flow");
    (*flow_).lamp_style = IndicatorLampStyle::pinpoint;
    (*flow_).state = IndicatorLampState::on;
    add_at(*this, flow_, Rect{left + 150.0, 362.0, 142.0, 24.0});

    std::shared_ptr<CheckBox> lamps = make_control<CheckBox>(StableId("process.lamps"), "Lamps on");
    (*lamps).indicator_style = ChoiceIndicatorStyle::toggle;
    (*lamps).checked = true;
    add_at(*this, lamps, Rect{left, 398.0, width, 24.0});
    on((*lamps).checked_changed(), *this, &ProcessControlRoot::on_lamps_changed);
}

void ProcessControlRoot::apply_sheet() {
    static_cast<void>(set_theme_sheet(process_control_sheet(paint_, seed_), "process-control"));
}

void ProcessControlRoot::show_state_button(std::size_t lamp) {
    const IndicatorLamp& shown = *lamps_[lamp];
    (*state_buttons_[lamp]).text =
        std::string(status_lamps[lamp].legend) + ": " + std::string(IndicatorLamp::state_name(shown.state.read()));
}

void ProcessControlRoot::on_state_clicked(std::size_t lamp) {
    IndicatorLamp& changed = *lamps_[lamp];
    changed.state = next_state(changed.state.read());
    show_state_button(lamp);
}

void ProcessControlRoot::on_paint_clicked() {
    paint_ = (paint_ + 1U) % enamel_paints.size();
    (*paint_button_).text = "Paint: " + std::string(enamel_paints[paint_].name);
    apply_sheet();
}

// The seed stays within the finish's range (0 to 2147483647), wrapping.
void ProcessControlRoot::on_seed_down() {
    seed_ = seed_ == 0U ? 2147483647U : seed_ - 1U;
    (*seed_label_).text = "Seed " + std::to_string(seed_);
    apply_sheet();
}

void ProcessControlRoot::on_seed_up() {
    seed_ = seed_ >= 2147483647U ? 0U : seed_ + 1U;
    (*seed_label_).text = "Seed " + std::to_string(seed_);
    apply_sheet();
}

void ProcessControlRoot::on_radio_colour(std::size_t colour) {
    for (const std::shared_ptr<RadioButton>& radio : radios_) {
        (*radio).lamp_color = stxlib::Optional<Color>::of(radio_colours[colour].colour);
    }
}

void ProcessControlRoot::on_lamps_changed(bool on) {
    for (std::size_t index = 0U; index < lamps_.size(); ++index) {
        (*lamps_[index]).state = on ? status_lamps[index].initial : IndicatorLampState::off;
        show_state_button(index);
    }
    (*ok_).set_lamps_enabled(on);
    (*cancel_).set_lamps_enabled(on);
}

void ProcessControlRoot::on_coolant_changed(bool on) {
    (*flow_).state = on ? IndicatorLampState::on : IndicatorLampState::off;
}

void ProcessControlRoot::on_ok() {
    (*message_).text = "Transfer started: pump to reserve tank.";
}

void ProcessControlRoot::on_cancel() {
    (*message_).text = "Transfer cancelled.";
}

} // namespace

std::string process_control_sheet(std::size_t paint, std::uint32_t seed) {
    const EnamelPaint& chosen = enamel_paints[paint % enamel_paints.size()];
    // The seed, then the grain (paint_grain, written as finish() reads it).
    const std::string seed_and_grain = std::to_string(seed) + ", 1.5";
    static_assert(paint_grain == 1.5, "the sheet writes paint_grain as 1.5");
    std::string sheet{};
    sheet += "/* The process-control demonstrator's theme (demo/process_control). */\n";
    sheet += ".blasted-enamel::part(face) { background: finish(blasted-enamel-glint, ";
    sheet += chosen.body;
    sheet += ", #a3a3a3, " + seed_and_grain + "); }\n";
    sheet += ".blasted-enamel-heavy::part(face) { background: finish(blasted-enamel-heavy-glint, ";
    sheet += chosen.band;
    sheet += ", #a3a3a3, " + seed_and_grain + "); }\n";
    // The keys draw their own bakes (LampKey): no rule here.
    return sheet;
}

std::unique_ptr<Window> make_process_control() {
    std::shared_ptr<ProcessControlRoot> root = make_control<ProcessControlRoot>(StableId("process.root"));
    (*root).build();
    std::unique_ptr<Window> window =
        std::make_unique<Window>(root, window_size, std::make_shared<ApplicationResources>());
    (*window).perform_layout();
    return window;
}

bool check_process_control_box(Window& window, std::string_view id) {
    const std::shared_ptr<CheckBox> box = std::dynamic_pointer_cast<CheckBox>(window.find(id));
    if (box == nullptr) return false;
    (*box).checked = true;
    return true;
}

} // namespace gui_forms::process_control
