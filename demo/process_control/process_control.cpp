#include "process_control.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/flex_panel.hpp"
#include "gui_forms/controls/indicator_lamp.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/radio_button.hpp"
#include "gui_forms/engine/light_planes/light_planes.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/control/control_layout_styles/control_layout_styles.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/layout/style/layout_style.hpp"
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

// The status lamps, in the dialog's order. The amber lamp sparkles
// (IndicatorLamp::lamp_sparkle; owner, 2026-11-22: lit amber looked dull).
struct StatusLamp final {
    std::string_view id{};
    std::string_view legend{};
    std::string_view name{};
    Color colour{};
    IndicatorLampState initial{};
    bool sparkle{};
};

constexpr std::array<StatusLamp, 3> status_lamps{{
    {.id = "process.status.pump-ready", .legend = "PUMP READY", .name = "Pump ready", .colour = lamp_green_srgb,
     .initial = IndicatorLampState::on},
    {.id = "process.status.heating", .legend = "HEATING", .name = "Heating", .colour = lamp_amber_srgb,
     .initial = IndicatorLampState::on, .sparkle = true},
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

// -- layout (owner, 2026-11-22: the controls follow the window) ---------------------------
//
// The window is laid out by flex panels: the dialog's column grows with
// the window and the strip keeps its width beside it; inside the dialog
// the title band spans its width, the type and the lamps keep their places
// from its upper left, and the keys keep their size at its lower right.

// A length for a flex item's size.
SizeValue length_of(double units) noexcept {
    const SizeValue value{SizeKind::length_percentage, LengthPercentage{LogicalLength{units}, 0.0}};
    return value;
}

// How a child sits in its flex panel: its width and height (0: from the
// panel, stretched across or grown along), how much it grows along the
// main axis (a growing item also shrinks), its least width and height,
// its margin, whether an `auto` margin before it pushes it to the far end,
// and its own cross alignment.
struct FlexPlace final {
    double width{};
    double height{};
    double grow{};
    double least_width{};
    double least_height{};
    Insets margin{};
    bool pushed{};
    LayoutAlignment align{LayoutAlignment::automatic};
};

template <typename Child>
void add_item(FlexPanel& panel, const std::shared_ptr<Child>& child, const FlexPlace& place) {
    (*child).margin = place.margin;
    panel.add_child(child);
    FlexItemLayout layout{};
    layout.flex.grow = FlexFactor{place.grow};
    layout.flex.shrink = FlexFactor{place.grow > 0.0 ? 1.0 : 0.0};
    if (place.width > 0.0) layout.width = length_of(place.width);
    if (place.height > 0.0) layout.height = length_of(place.height);
    if (place.least_width > 0.0) layout.min_width = length_of(place.least_width);
    if (place.least_height > 0.0) layout.min_height = length_of(place.least_height);
    layout.align_self = place.align;
    if (place.pushed) {
        const bool column = panel.flex_direction.read() == FlexDirection::column;
        if (column) {
            layout.auto_margin.top = true;
        } else {
            layout.auto_margin.left = true;
        }
    }
    panel.set_item_layout(*child, layout);
}

// A flex panel along `direction`, `gap` units between its items, inside
// `padding`, its items stretched across it unless `align` says otherwise.
std::shared_ptr<FlexPanel> make_flex(std::string id, FlexDirection direction, double gap, Insets padding = {},
                                     LayoutAlignment align = LayoutAlignment::stretch) {
    std::shared_ptr<FlexPanel> made = make_control<FlexPanel>(StableId(std::move(id)));
    (*made).flex_direction = direction;
    (*made).padding = padding;
    (*made).margin = Insets{};
    (*made).align_items = align;
    const LengthPercentage spacing{LogicalLength{gap}, 0.0};
    if (direction == FlexDirection::column) {
        (*made).row_gap = spacing;
    } else {
        (*made).column_gap = spacing;
    }
    return made;
}

// The dialog's places (the study's 500 x 310 body): the title band's
// height, the type's left edge and first line, and the space round the
// keys; the strip's width; the margin round everything.
constexpr double band_height = 39.0;
constexpr double dialog_inset = 24.0;
constexpr double heading_top = 23.0;
constexpr double key_inset = 20.0;
constexpr double strip_width = 292.0;
constexpr double window_margin = 16.0;

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
    // The dialog's column and the strip (build).
    std::shared_ptr<FlexPanel> main_{};
    std::shared_ptr<FlexPanel> strip_{};
    std::size_t paint_{};
    std::uint32_t seed_{study_seed};
};

// The window's two columns, the dialog's and the strip's, in one row: the
// dialog's grows with the window (never below the study's 500 x 310), the
// strip keeps its width.
void ProcessControlRoot::build() {
    std::shared_ptr<FlexPanel> layout =
        make_flex("process.layout", FlexDirection::row, window_margin,
                  Insets{window_margin, window_margin, window_margin, window_margin});
    (*layout).dock = DockStyle::fill;
    add_child(layout);
    main_ = make_flex("process.main", FlexDirection::column, 10.0);
    add_item(*layout, main_, FlexPlace{.grow = 1.0, .least_width = 500.0});
    strip_ = make_flex("process.strip", FlexDirection::column, 4.0);
    add_item(*layout, strip_, FlexPlace{.width = strip_width});
    build_dialog();
    build_strip();
    apply_sheet();
}

// The study's body, laid out from its upper left: the title band across
// it, the type and the status lamps as jewels (centred 36 units in, 145,
// 174 and 203 down, 14 units across) at the study's places, and the keys
// (120 x 53) at its lower right, 20 units in, as the Gallery's
// industrial-surfaces page has them at the study's size.
void ProcessControlRoot::build_dialog() {
    std::shared_ptr<Panel> dialog = make_control<Panel>(StableId("process.dialog"));
    static_cast<void>((*dialog).add_class("blasted-enamel"));
    add_item(*main_, dialog, FlexPlace{.grow = 1.0, .least_height = 310.0});
    std::shared_ptr<FlexPanel> body = make_flex("process.dialog.layout", FlexDirection::column, 0.0);
    (*body).dock = DockStyle::fill;
    (*dialog).add_child(body);
    std::shared_ptr<Panel> band = make_control<Panel>(StableId("process.dialog.title-band"));
    static_cast<void>((*band).add_class("blasted-enamel-heavy"));
    add_item(*body, band, FlexPlace{.height = band_height});
    add_at(*band, make_label("process.dialog.title", "PROCESS CONTROL", 13.0, enamel_ink, 1.0),
           Rect{17.0, 8.0, 320.0, 24.0});
    std::shared_ptr<FlexPanel> content =
        make_flex("process.dialog.content", FlexDirection::column, 0.0,
                  Insets{dialog_inset, heading_top, key_inset, key_inset}, LayoutAlignment::flex_start);
    add_item(*body, content, FlexPlace{.grow = 1.0});
    add_item(*content, make_label("process.dialog.heading", "START TRANSFER", 18.0, enamel_ink, 1.0),
             FlexPlace{.width = 440.0, .height = 30.0});
    add_item(*content, make_label("process.dialog.subtitle", "PUMP TO RESERVE TANK", 10.0, enamel_ink, 0.5),
             FlexPlace{.width = 440.0, .height = 18.0, .margin = Insets{0.0, 3.0, 0.0, 0.0}});
    for (std::size_t index = 0U; index < status_lamps.size(); ++index) {
        const StatusLamp& choice = status_lamps[index];
        std::shared_ptr<IndicatorLamp> lamp =
            make_control<IndicatorLamp>(StableId(std::string(choice.id)), std::string(choice.legend));
        (*lamp).accessible_name = std::string(choice.name);
        (*lamp).fore_color = stxlib::Optional<Color>::of(enamel_ink);
        (*lamp).light_planes = std::string(engine_light::jewel_lamp_bake);
        (*lamp).lamp_color = stxlib::Optional<Color>::of(choice.colour);
        (*lamp).lamp_sparkle = choice.sparkle;
        (*lamp).state = choice.initial;
        // 138 down from the body's top, then every 29.
        const double above = index == 0U ? 25.0 : 15.0;
        add_item(*content, lamp, FlexPlace{.width = 220.0, .height = 14.0, .margin = Insets{5.0, above, 0.0, 0.0}});
        lamps_[index] = lamp;
    }
    // The keys at the body's lower right, whatever its size. The bakes
    // carry the legends: the keys have no text, only their names.
    std::shared_ptr<FlexPanel> keys = make_flex("process.dialog.keys", FlexDirection::row, 12.0);
    add_item(*content, keys, FlexPlace{.height = 53.0, .pushed = true, .align = LayoutAlignment::flex_end});
    cancel_ = make_control<LampKey>(StableId("process.key.cancel"), "dialog_cancel", key_cancel_lamp,
                                       LampKeyLighting::always);
    (*cancel_).accessible_name = "Cancel";
    add_item(*keys, cancel_, FlexPlace{.width = 120.0, .height = 53.0});
    ok_ = make_control<LampKey>(StableId("process.key.ok"), "dialog_ok", key_ok_lamp, LampKeyLighting::while_held);
    (*ok_).accessible_name = "OK";
    add_item(*keys, ok_, FlexPlace{.width = 120.0, .height = 53.0});
    on((*ok_).clicked(), *this, &ProcessControlRoot::on_ok);
    on((*cancel_).clicked(), *this, &ProcessControlRoot::on_cancel);

    message_ = make_label("process.message", "Ready.", 11.0, strip_ink);
    add_item(*main_, message_, FlexPlace{.height = 24.0});
}

// The strip beside the dialog: each status lamp's state, the paint and its
// seed, the lamp radio and its colour, the coolant pinpoint and the lamps.
void ProcessControlRoot::build_strip() {
    // A section's heading, a gap above every one but the first.
    const FlexPlace first_heading{.height = 20.0};
    const FlexPlace heading{.height = 20.0, .margin = Insets{0.0, 8.0, 0.0, 0.0}};
    const FlexPlace row{.height = 28.0};
    add_item(*strip_, make_label("process.strip.lamps", "Status lamps", 11.0, strip_ink), first_heading);
    for (std::size_t index = 0U; index < status_lamps.size(); ++index) {
        std::shared_ptr<Button> button =
            make_control<Button>(StableId(std::string(status_lamps[index].id) + ".state"));
        add_item(*strip_, button, row);
        on((*button).clicked(), *this, &ProcessControlRoot::on_state_clicked, index);
        state_buttons_[index] = button;
        show_state_button(index);
    }

    add_item(*strip_, make_label("process.strip.paint", "Paint", 11.0, strip_ink), heading);
    paint_button_ = make_control<Button>(StableId("process.paint"), "Paint: " + std::string(enamel_paints[0].name));
    add_item(*strip_, paint_button_, row);
    on((*paint_button_).clicked(), *this, &ProcessControlRoot::on_paint_clicked);
    // The seed's row: its buttons at the ends, its value between them.
    std::shared_ptr<FlexPanel> seed_row =
        make_flex("process.strip.seed", FlexDirection::row, 12.0, Insets{}, LayoutAlignment::center);
    add_item(*strip_, seed_row, FlexPlace{.height = 28.0, .margin = Insets{0.0, 2.0, 0.0, 0.0}});
    std::shared_ptr<Button> seed_down = make_control<Button>(StableId("process.seed.down"), "Seed -");
    add_item(*seed_row, seed_down, FlexPlace{.width = 88.0, .height = 28.0});
    on((*seed_down).clicked(), *this, &ProcessControlRoot::on_seed_down);
    seed_label_ = make_label("process.seed", "Seed " + std::to_string(seed_), 11.0, strip_ink);
    add_item(*seed_row, seed_label_, FlexPlace{.height = 20.0, .grow = 1.0});
    std::shared_ptr<Button> seed_up = make_control<Button>(StableId("process.seed.up"), "Seed +");
    add_item(*seed_row, seed_up, FlexPlace{.width = 88.0, .height = 28.0});
    on((*seed_up).clicked(), *this, &ProcessControlRoot::on_seed_up);

    add_item(*strip_, make_label("process.strip.radio", "Feed", 11.0, strip_ink), heading);
    constexpr std::array<std::string_view, 2> feeds{"Manual feed", "Automatic feed"};
    for (std::size_t index = 0U; index < feeds.size(); ++index) {
        std::shared_ptr<RadioButton> radio = make_control<RadioButton>(
            index == 0U ? StableId("process.feed.manual") : StableId("process.feed.automatic"), std::string(feeds[index]));
        (*radio).group_name = "process-feed";
        (*radio).indicator_style = ChoiceIndicatorStyle::lamp;
        (*radio).checked = index == 0U;
        add_item(*strip_, radio, FlexPlace{.width = 180.0, .height = 24.0, .align = LayoutAlignment::flex_start});
        radios_[index] = radio;
    }
    std::shared_ptr<FlexPanel> colour_row = make_flex("process.strip.colours", FlexDirection::row, 8.0);
    add_item(*strip_, colour_row, FlexPlace{.height = 28.0, .margin = Insets{0.0, 4.0, 0.0, 0.0}});
    for (std::size_t index = 0U; index < radio_colours.size(); ++index) {
        std::shared_ptr<Button> button =
            make_control<Button>(StableId(std::string(radio_colours[index].id)), std::string(radio_colours[index].text));
        add_item(*colour_row, button, FlexPlace{.width = 92.0, .height = 28.0});
        on((*button).clicked(), *this, &ProcessControlRoot::on_radio_colour, index);
    }

    std::shared_ptr<FlexPanel> coolant_row = make_flex("process.strip.coolant", FlexDirection::row, 10.0);
    add_item(*strip_, coolant_row, FlexPlace{.height = 24.0, .margin = Insets{0.0, 4.0, 0.0, 0.0}});
    std::shared_ptr<CheckBox> coolant = make_control<CheckBox>(StableId("process.coolant"), "Coolant");
    (*coolant).checked = true;
    add_item(*coolant_row, coolant, FlexPlace{.width = 140.0, .height = 24.0});
    on((*coolant).checked_changed(), *this, &ProcessControlRoot::on_coolant_changed);
    flow_ = make_control<IndicatorLamp>(StableId("process.coolant-flow"), "Coolant flow");
    (*flow_).lamp_style = IndicatorLampStyle::pinpoint;
    (*flow_).state = IndicatorLampState::on;
    add_item(*coolant_row, flow_, FlexPlace{.width = 142.0, .height = 24.0});

    std::shared_ptr<CheckBox> lamps = make_control<CheckBox>(StableId("process.lamps"), "Lamps on");
    (*lamps).indicator_style = ChoiceIndicatorStyle::toggle;
    (*lamps).checked = true;
    add_item(*strip_, lamps, FlexPlace{.height = 24.0, .margin = Insets{0.0, 4.0, 0.0, 0.0}});
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
    // Smaller, the dialog and the strip would no longer fit side by side.
    (*window).minimum_size = window_size;
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
