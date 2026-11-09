// Presentational state machines, transitions and ambient effects (T2 plan,
// phase E7; GIVEN C1-1, C2-1, D2-1, D3-1, D4-1, B3-1 and G4-1): the
// machines and effects are declared in rule data and checked at load; the
// built-in rules draw the ribbon's dithered rise (flat at rest, rising to 1
// while hot by the exponential approach, falling back after) and the
// docking tiles' 100 ms fade, frame by frame against reference hashes; once
// they settle the window schedules no frames; under reduced motion and in
// an occluded window every transition jumps to its end; an ambient effect
// over its frame cap or over 10 % of the window is refused (a ceremonial
// surface lifts the area cap), runs at no more than 30 frames per second,
// and stops when the window is occluded, under reduced motion, with the
// application's motion switch off, or when its part is no longer painted.
// GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints the observed hashes instead of
// checking them.

#include "gui_forms/gui_forms.hpp"
#include "support/theme_definition_heap.hpp"
#include "support/rgba_order.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The hashes round lengths as x × 64 + 0.5: no fused multiply-add here,
// which clang would use on arm64 and not on x86-64.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using namespace gui_forms;
using std::chrono::milliseconds;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_motion_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode() {
    const char* const value = std::getenv("GUI_FORMS_ENGINE_IDENTITY_PRINT");
    const bool printing = value != nullptr && std::strcmp(value, "1") == 0;
    return printing;
}

// FNV-1a over bytes, doubles and words, in order.
struct BitHash final {
    std::uint64_t value{14695981039346656037ULL};

    void add_byte(std::uint8_t byte) {
        value = value ^ static_cast<std::uint64_t>(byte);
        value = value * 1099511628211ULL;
    }
    void add_bytes(std::span<const std::uint8_t> bytes) {
        for (const std::uint8_t byte : bytes) add_byte(byte);
    }
    void add_word(std::uint64_t word) {
        for (int shift = 0; shift < 64; shift += 8) add_byte(static_cast<std::uint8_t>((word >> shift) & 0xFFU));
    }
    void add_number(double number) {
        std::uint64_t bits = 0U;
        std::memcpy(&bits, &number, sizeof bits);
        add_word(bits);
    }
    // A length rounded to 1/64 logical px, so that a last-bit difference in
    // layout arithmetic between hosts does not change the hash.
    void add_length(double length) {
        const double scaled = std::floor(length * 64.0 + 0.5);
        add_word(static_cast<std::uint64_t>(static_cast<std::int64_t>(scaled)));
    }
};

class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

class DiscardingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

void move_to(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, point}));
    window.perform_layout();
}

void repaint(Window& window, Painter& painter) {
    window.perform_layout();
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(painter, damage.bounds());
}

// One frame of 16 ms of the test's time: poll, then paint what it damaged.
// Returns the motion clock's frames in the poll (0 or 1).
std::uint64_t run_frame(Window& window, ManualTime& time, Painter& painter) {
    time.time += milliseconds(16);
    const FramePollResult result = window.poll_frame_schedule(time.time);
    repaint(window, painter);
    return result.motion_clock_ticks;
}

const EnginePartRecord* record_of(const Window& window, std::string_view control, std::string_view part,
                                  std::string_view item) {
    const std::optional<PartRecordHandle> handle = window.engine_parts().find(control, PartKey{part, item, {}});
    if (!handle) return nullptr;
    const EnginePartRecord* const record = window.engine_parts().record(*handle);
    return record;
}

// The current value of a record's first transition channel, or NaN.
double channel_value(const EnginePartRecord* record) {
    if (record == nullptr || !(*record).motion) return std::nan("");
    for (const PartTransitionChannel& channel : (*(*record).motion).channels) {
        if (channel.value) return (*channel.value).current();
    }
    return std::nan("");
}

// -- the rules ---------------------------------------------------------------

CompiledThemeRules::PartFacts facts_of(std::string_view type, std::string_view part, PartRole role,
                                       AnatomyStateSet states = {}) {
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = type;
    facts.stable_id = "subject";
    facts.part = part;
    facts.role = role;
    facts.states = states;
    return facts;
}

bool has_diagnostic(const CompiledThemeRules& rules, RuleDiagnosticKind kind, std::string_view property) {
    for (const RuleDiagnostic& diagnostic : rules.diagnostics()) {
        if (diagnostic.kind == kind && diagnostic.property == property) return true;
    }
    return false;
}

PropertyTransition transition_on(std::string property, double target) {
    PropertyTransition transition{};
    transition.property = std::move(property);
    PresentationState state{};
    state.target = target;
    transition.states.push_back(state);
    transition.curve = MotionCurve::timed(milliseconds(100), MotionEasing::linear);
    return transition;
}

void machines_are_rule_data() {
    const CompiledThemeRules built_in =
        CompiledThemeRules::compile_with_built_in(ThemeRuleSheet{}, builtin_primitive_registry());
    require(built_in.diagnostics().empty() && built_in.built_in_rule_count() == 9U && built_in.rule_count() == 9U,
            "the engine's nine built-in rules compile cleanly");
    const CompiledThemeRules::Resolution face =
        built_in.resolve(facts_of("ribbon", "command_face", PartRole::face));
    require(face.outcome == CompiledThemeRules::Outcome::primitive &&
                (*face.primitive).descriptor().name == "dithered_bulge" && face.transition_count == 1U &&
                (*face.transitions[0]).property == PresentationProperty::parameter && (*face.transitions[0]).parameter == 0U &&
                face.level == CascadeLevel::built_in,
            "a ribbon command face takes the dithered bulge with its amplitude's rise, at the built-in level");
    AnatomyStateSet checked{};
    checked.insert(AnatomyState::checked);
    const CompiledThemeRules::Resolution toggle =
        built_in.resolve(facts_of("ribbon", "command_face", PartRole::face, checked));
    require(toggle.outcome == CompiledThemeRules::Outcome::built_in_selected && toggle.transition_count == 0U,
            "a checked toggle keeps today's plate");
    const CompiledThemeRules::Resolution tile = built_in.resolve(facts_of("dock_site", "tile", PartRole::actuator));
    require(tile.outcome == CompiledThemeRules::Outcome::primitive &&
                (*tile.primitive).descriptor().name == "pill_plate" && tile.transition_count == 1U &&
                (*tile.transitions[0]).property == PresentationProperty::opacity && (*tile.transitions[0]).states.size() == 2U,
            "a docking tile is the pill plate (E8) with its opacity's fade");
    AnatomyStateSet lit{};
    lit.insert(AnatomyState::selected);
    const CompiledThemeRules::Resolution gold =
        built_in.resolve(facts_of("dock_site", "tile", PartRole::actuator, lit));
    require(gold.outcome == CompiledThemeRules::Outcome::primitive &&
                (*gold.primitive).descriptor().name == "gold_plate" && gold.transition_count == 1U &&
                (*gold.transitions[0]).property == PresentationProperty::opacity,
            "a lit tile is the gold plate, and fades as any tile");
    const CompiledThemeRules::Resolution other = built_in.resolve(facts_of("button", "face", PartRole::face));
    require(other.outcome == CompiledThemeRules::Outcome::no_rule && other.transition_count == 0U,
            "every other part is as before");

    // A theme rule for the same part wins over the built-in one, and the
    // built-in rise, being the dithered bulge's, does not follow it.
    ThemeRuleSheet sheet{};
    ThemeRule smooth{};
    smooth.label = "smooth";
    smooth.selector.control_type = "ribbon";
    smooth.selector.part = "command_face";
    smooth.declarations.push_back(RuleDeclaration::primitive("bulge"));
    sheet.rules.push_back(smooth);
    const CompiledThemeRules themed = CompiledThemeRules::compile_with_built_in(sheet, builtin_primitive_registry());
    const CompiledThemeRules::Resolution themed_face =
        themed.resolve(facts_of("ribbon", "command_face", PartRole::face));
    require(themed_face.outcome == CompiledThemeRules::Outcome::primitive &&
                (*themed_face.primitive).descriptor().name == "bulge" && themed_face.transition_count == 0U &&
                themed.diagnostics().empty(),
            "a theme's rule takes precedence over the built-in rules");

    // Transitions and ambient effects are checked at load.
    ThemeRuleSheet faulty{};
    ThemeRule faults{};
    faults.label = "faults";
    faults.selector.role = PartRole::face;
    faults.declarations.push_back(RuleDeclaration::primitive("bulge"));
    faults.transitions.push_back(transition_on("sparkle", 1.0));
    faults.transitions.push_back(transition_on("exponent", 2.0));
    PropertyTransition stateless = transition_on("amplitude", 1.0);
    stateless.states.clear();
    faults.transitions.push_back(stateless);
    faults.transitions.push_back(transition_on("opacity", 2.0));
    faults.transitions.push_back(transition_on("amplitude", 0.5));
    faults.transitions.push_back(transition_on("amplitude", 0.25));
    AmbientEffect fast{};
    fast.period = milliseconds(1000);
    fast.frames_per_second = 31.0;
    faults.ambient = fast;
    faulty.rules.push_back(faults);
    ThemeRule loose{};
    loose.label = "loose";
    loose.selector.role = PartRole::glyph;
    loose.transitions.push_back(transition_on("amplitude", 1.0));
    PresentationState entering{};
    entering.target = 1.0;
    entering.enter_from = -0.5;
    PropertyTransition fade = transition_on("opacity", 1.0);
    fade.states.push_back(entering);
    loose.transitions.push_back(fade);
    AmbientEffect timeless{};
    timeless.frames_per_second = 30.0;
    loose.ambient = timeless;
    faulty.rules.push_back(loose);
    ThemeRule fading{};
    fading.label = "fading";
    fading.selector.role = PartRole::label;
    fading.transitions.push_back(transition_on("opacity", 0.5));
    faulty.rules.push_back(fading);
    const CompiledThemeRules checked_rules = CompiledThemeRules::compile(faulty, builtin_primitive_registry());
    require(has_diagnostic(checked_rules, RuleDiagnosticKind::transition_property_unknown, "sparkle") &&
                has_diagnostic(checked_rules, RuleDiagnosticKind::transition_property_not_animated, "exponent") &&
                has_diagnostic(checked_rules, RuleDiagnosticKind::transition_invalid, "amplitude") &&
                has_diagnostic(checked_rules, RuleDiagnosticKind::transition_invalid, "opacity") &&
                has_diagnostic(checked_rules, RuleDiagnosticKind::duplicate_declaration, "amplitude") &&
                has_diagnostic(checked_rules, RuleDiagnosticKind::ambient_over_frame_cap, "ambient") &&
                has_diagnostic(checked_rules, RuleDiagnosticKind::parameter_without_primitive, "amplitude") &&
                has_diagnostic(checked_rules, RuleDiagnosticKind::ambient_invalid, "ambient"),
            "each faulty transition and ambient effect is dropped with its own diagnostic");
    require(checked_rules.diagnostics().size() == 10U && checked_rules.rule_count() == 2U,
            "ten diagnostics; the rule left with nothing is dropped, the others stand");
    const CompiledThemeRules::Resolution label = checked_rules.resolve(facts_of("label", "text", PartRole::label));
    require(label.outcome == CompiledThemeRules::Outcome::no_rule && label.transition_count == 1U &&
                (*label.transitions[0]).property == PresentationProperty::opacity,
            "a rule declaring only an opacity transition stands, without a primitive");
}

// -- the ribbon's rise ---------------------------------------------------------

struct RibbonFixture final {
    ManualTime time{};
    std::shared_ptr<Command> paste = std::make_shared<Command>("paste", "Paste");
    std::shared_ptr<Command> cut = std::make_shared<Command>("cut", "Cut");
    std::shared_ptr<Ribbon> ribbon;
    std::unique_ptr<Window> window;
    DiscardingPainter painter{};
};

std::unique_ptr<RibbonFixture> make_ribbon(bool reduced = false) {
    std::unique_ptr<RibbonFixture> fixture = std::make_unique<RibbonFixture>();
    RibbonFixture& made = *fixture;
    std::vector<RibbonTabSpec> tabs{};
    RibbonTabSpec home{.stable_id = "home", .text = "&Home"};
    RibbonGroupSpec clipboard{.stable_id = "clipboard", .caption = "Clipboard"};
    clipboard.commands.push_back({.stable_id = "paste", .command = made.paste});
    clipboard.commands.push_back({.stable_id = "cut", .size = RibbonCommandSize::small, .command = made.cut});
    home.groups.push_back(std::move(clipboard));
    tabs.push_back(std::move(home));
    made.ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*made.ribbon).set_tabs(std::move(tabs));
    made.window = std::make_unique<Window>(made.ribbon, Size{600.0, 115.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    if (reduced) {
        PresentationSettings settings = window.presentation_settings();
        settings.reduced_motion = true;
        window.set_presentation_settings(settings);
    }
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

Rect paste_face(const Ribbon& ribbon) {
    const std::vector<Ribbon::CommandItem> items = ribbon.realized_commands();
    for (const Ribbon::CommandItem& item : items) {
        if (item.stable_id == "paste") return item.parts.command_face.bounds;
    }
    require(false, "the paste command is realized");
    return {};
}

// The paste face's amplitude and its raster's bytes as last drawn; no
// raster when the face is flat.
struct FaceFrame final {
    double amplitude{};
    std::vector<std::uint8_t> pixels{};
};

FaceFrame face_frame(const Window& window) {
    FaceFrame frame{};
    const EnginePartRecord* const record = record_of(window, "ribbon", "command_face", "paste");
    require(record != nullptr, "the paste face has a record");
    frame.amplitude = channel_value(record);
    if ((*record).cached && (*(*record).cached).raster) {
        const std::span<const std::uint8_t> pixels = (*(*(*record).cached).raster).pixels();
        frame.pixels.assign(pixels.begin(), pixels.end());
    }
    return frame;
}

// The dithered bulge at amplitude 1 for the paste face, drawn directly:
// the static rendering the settled rise must equal.
std::vector<std::uint8_t> static_face(Rect face, AnatomyStateSet states) {
    const EnginePrimitive* const dithered = builtin_primitive_registry().find("dithered_bulge");
    require(dithered != nullptr, "the dithered bulge is registered");
    const PrimitiveParameterBlock parameters = default_parameters((*dithered).descriptor());
    PrimitiveTarget target{};
    target.logical_size = Size{face.width, face.height};
    target.scale = DeviceScale{1.0};
    target.states = states;
    const DeviceExtent extent{static_cast<std::uint32_t>(std::ceil(face.width)),
                              static_cast<std::uint32_t>(std::ceil(face.height))};
    std::shared_ptr<PartRaster> raster = PartRaster::create(extent, DeviceScale{1.0});
    require(raster != nullptr, "a raster is made");
    PrimitivePreparation preparation{};
    require((*dithered).prepare(parameters, target, preparation) == PreparationOutcome::prepared &&
                (*dithered).rasterize(parameters, target, preparation, (*raster).view()) == RasterOutcome::painted,
            "the static face is drawn");
    const std::span<const std::uint8_t> pixels = (*raster).pixels();
    const std::vector<std::uint8_t> bytes(pixels.begin(), pixels.end());
    return bytes;
}

// Runs frames until the clock disarms; hashes each frame's amplitude and
// raster, and returns the frame count.
std::size_t run_until_settled(RibbonFixture& fixture, BitHash& hash, std::vector<double>& amplitudes) {
    Window& window = *fixture.window;
    std::size_t frames = 0U;
    for (int step = 0; step < 100 && window.motion_clock().armed(); ++step) {
        frames += run_frame(window, fixture.time, fixture.painter);
        const FaceFrame frame = face_frame(window);
        amplitudes.push_back(frame.amplitude);
        hash.add_number(frame.amplitude);
        // A flat face draws nothing: its raster is not redrawn.
        if (frame.amplitude > 0.0) hash.add_bytes(gui_forms::test_support::rgba_order(frame.pixels));
    }
    return frames;
}

// Runs idle frames until the window has baked every queued keyframe,
// requiring at most one bake per frame (owner A, 2026-10-22); returns the
// frames run.
std::size_t drain_idle_keyframes(RibbonFixture& fixture) {
    Window& window = *fixture.window;
    std::size_t frames = 0U;
    for (int step = 0; step < 40 && window.engine_parts().idle_keyframes_waiting(); ++step) {
        const std::uint64_t before = window.engine_parts().cache_statistics().bakes;
        static_cast<void>(run_frame(window, fixture.time, fixture.painter));
        require(window.engine_parts().cache_statistics().bakes <= before + 1U, "an idle frame bakes one keyframe");
        frames = frames + 1U;
    }
    require(!window.engine_parts().idle_keyframes_waiting(), "the idle keyframes are all baked");
    return frames;
}

// The expected hashes of the rise and the fall, recorded on Windows x64
// and checked on every CI platform.
constexpr std::uint64_t expected_rise_hash = 0x94265deb32d68de6ULL;
constexpr std::uint64_t expected_fall_hash = 0x14208adee63a9a21ULL;

void ribbon_rise() {
    std::unique_ptr<RibbonFixture> fixture = make_ribbon();
    Window& window = *(*fixture).window;
    const Rect face = paste_face(*(*fixture).ribbon);
    FaceFrame rest = face_frame(window);
    require(rest.pixels.empty() && !window.motion_clock().armed(),
            "a ribbon button is flat at rest: nothing is drawn and nothing moves");
    // E7r (owner A): the rise's keyframes, the nine levels above the flat
    // plane, are baked in idle frames after the first paint, one a frame.
    require(window.engine_parts().idle_keyframes_waiting(), "the first paint queues the keyframes");
    const std::size_t idle_frames = drain_idle_keyframes(*fixture);
    require(idle_frames >= 9U && window.engine_parts().cache_statistics().keyframe_idle_bakes >= 9U,
            "each keyframe takes an idle frame of its own");

    move_to(window, centre(face));
    repaint(window, (*fixture).painter);
    const FaceFrame start = face_frame(window);
    require(start.amplitude == 0.0 && start.pixels.empty() && window.motion_clock().armed(),
            "hot, the rise starts from the flat plane on the window's clock");
    // With the idle bakes done, nothing is baked while the rise runs.
    const RasterCacheStatistics started = window.engine_parts().cache_statistics();
    require(started.keyframe_demand_bakes == 0U, "no keyframe is baked on demand");
    BitHash rise{};
    std::vector<double> rising{};
    const std::size_t rise_frames = run_until_settled(*fixture, rise, rising);
    require(window.engine_parts().cache_statistics().bakes == started.bakes, "nothing is baked while the rise runs");
    require(rise_frames == 30U && rising.size() == 30U,
            "the rise settles within half an 8-bit step after 30 frames of 16 ms (480 ms)");
    const double first = 1.0 - std::exp(-16.0 / 75.0);
    require(std::fabs(rising[0] - first) < 1e-12, "the exponential approach: the gap shrinks by e each 75 ms");
    for (std::size_t index = 1; index < rising.size(); ++index) {
        require(rising[index] > rising[index - 1U], "the amplitude only rises");
    }
    require(rising.back() == 1.0, "and snaps exactly to 1 when it settles");
    AnatomyStateSet hot{};
    hot.insert(AnatomyState::hot);
    require(face_frame(window).pixels == static_face(face, hot),
            "the last frame is byte-identical to the static rendering");
    const MetricsSnapshot settled = window.metrics_snapshot();
    require(run_frame(window, (*fixture).time, (*fixture).painter) == 0U && !window.motion_clock().armed() &&
                settled.motion_values_moving == 0U,
            "once settled, the window schedules no frames");

    // Away from the button its bulge falls back to the plane.
    move_to(window, Point{590.0, 60.0});
    repaint(window, (*fixture).painter);
    BitHash fall{};
    std::vector<double> falling{};
    const std::size_t fall_frames = run_until_settled(*fixture, fall, falling);
    require(fall_frames == 30U && falling.back() == 0.0, "it falls back in 30 frames and rests flat");
    require(window.engine_parts().cache_statistics().bakes == started.bakes,
            "nor while it falls: it steps back through the same keyframes");
    for (std::size_t index = 1; index < falling.size(); ++index) {
        require(falling[index] < falling[index - 1U], "the amplitude only falls");
    }
    require(run_frame(window, (*fixture).time, (*fixture).painter) == 0U && !window.motion_clock().armed(),
            "and then nothing is scheduled");
    // Hovered again, the rise bakes nothing at all.
    move_to(window, centre(face));
    repaint(window, (*fixture).painter);
    BitHash again{};
    std::vector<double> rising_again{};
    static_cast<void>(run_until_settled(*fixture, again, rising_again));
    require(window.engine_parts().cache_statistics().bakes == started.bakes, "a second rise bakes nothing");
    require(again.value == rise.value, "and shows the same frames");
    if (print_mode()) {
        std::cout << "ribbon rise hash 0x" << std::hex << rise.value << ", fall hash 0x" << fall.value << std::dec
                  << '\n';
        return;
    }
    require(rise.value == expected_rise_hash, "the rise has the same bits on every host");
    require(fall.value == expected_fall_hash, "the fall has the same bits on every host");
}

// Before the idle frames have run, a rise bakes each keyframe the first
// time it reaches it, one at most in a frame (owner A, 2026-10-22).
void ribbon_rise_before_idle_bakes() {
    std::unique_ptr<RibbonFixture> fixture = make_ribbon();
    Window& window = *(*fixture).window;
    const Rect face = paste_face(*(*fixture).ribbon);
    move_to(window, centre(face));
    repaint(window, (*fixture).painter);
    require(window.motion_clock().armed(), "the rise starts at once");
    std::uint64_t previous = window.engine_parts().cache_statistics().bakes;
    for (int step = 0; step < 100 && window.motion_clock().armed(); ++step) {
        static_cast<void>(run_frame(window, (*fixture).time, (*fixture).painter));
        const std::uint64_t now = window.engine_parts().cache_statistics().bakes;
        require(now <= previous + 1U, "a frame bakes at most the one keyframe it reaches first");
        previous = now;
    }
    const RasterCacheStatistics ran = window.engine_parts().cache_statistics();
    require(ran.keyframe_demand_bakes <= 10U && ran.keyframe_idle_bakes == 0U,
            "while the rise runs no idle frame bakes, and each keyframe is baked once at most");
}

void ribbon_without_motion() {
    // Reduced motion (GIVEN D2-1): the transition jumps to its end.
    std::unique_ptr<RibbonFixture> reduced = make_ribbon(true);
    Window& calm = *(*reduced).window;
    const Rect face = paste_face(*(*reduced).ribbon);
    move_to(calm, centre(face));
    repaint(calm, (*reduced).painter);
    AnatomyStateSet hot{};
    hot.insert(AnatomyState::hot);
    require(face_frame(calm).amplitude == 1.0 && !calm.motion_clock().armed() &&
                face_frame(calm).pixels == static_face(face, hot),
            "under reduced motion the rise is at its end at once, and nothing is scheduled");

    // An occluded window (GIVEN D4-1): a rise under way jumps to its end.
    std::unique_ptr<RibbonFixture> fixture = make_ribbon();
    Window& window = *(*fixture).window;
    move_to(window, centre(face));
    repaint(window, (*fixture).painter);
    for (int step = 0; step < 3; ++step) static_cast<void>(run_frame(window, (*fixture).time, (*fixture).painter));
    require(face_frame(window).amplitude < 1.0 && window.motion_clock().armed(), "the rise is under way");
    window.set_occluded(true, (*fixture).time.time);
    require(!window.motion_clock().armed() && window.motion_clock().moving_count() == 0U,
            "occluded, the window schedules nothing");
    window.set_occluded(false, (*fixture).time.time);
    repaint(window, (*fixture).painter);
    require(face_frame(window).amplitude == 1.0 && face_frame(window).pixels == static_face(face, hot),
            "and shows the rise's end when it is seen again");
}

// -- the docking tiles' fade ---------------------------------------------------

class PanelFactory final {
public:
    void create(DockPanelCreation& creation) {
        creation.content = make_control<Panel>(StableId("content." + creation.panel.value));
    }
};

DockPanelRegistration tool(PanelFactory& factory, std::string id, std::string title, DockSide side) {
    DockPanelRegistration registration{};
    registration.descriptor.id = DockPanelId(std::move(id));
    registration.descriptor.kind = DockPanelKind::tool;
    registration.descriptor.minimum_size = {100.0, 60.0};
    registration.descriptor.default_placement.side = side;
    registration.descriptor.default_placement.ratio = 0.25;
    registration.title = std::move(title);
    registration.factory = Delegate<DockPanelCreation&>::bind<PanelFactory, &PanelFactory::create>(factory);
    return registration;
}

DockPanelRegistration document(PanelFactory& factory, std::string id, std::string title) {
    DockPanelRegistration registration{};
    registration.descriptor.id = DockPanelId(std::move(id));
    registration.descriptor.kind = DockPanelKind::document;
    registration.descriptor.minimum_size = {120.0, 80.0};
    registration.title = std::move(title);
    registration.factory = Delegate<DockPanelCreation&>::bind<PanelFactory, &PanelFactory::create>(factory);
    return registration;
}

void apply(DockWorkspace& workspace, DockEdit edit) {
    DockTransaction transaction{};
    transaction.edits.push_back(std::move(edit));
    require(workspace.apply(transaction).applied, "a fixture edit applies");
}

// Two documents, Solution on the left and Output at the bottom, in an 800
// by 500 window.
struct DockFixture final {
    ManualTime time{};
    PanelFactory factory{};
    std::unique_ptr<DockWorkspace> workspace;
    std::shared_ptr<DockSite> site;
    std::unique_ptr<Window> window;
    DiscardingPainter painter{};
};

std::unique_ptr<DockFixture> make_dock() {
    std::unique_ptr<DockFixture> fixture = std::make_unique<DockFixture>();
    DockFixture& made = *fixture;
    made.workspace = std::make_unique<DockWorkspace>("workspace");
    DockWorkspace& workspace = *made.workspace;
    require(workspace.register_panel(tool(made.factory, "solution", "Solution", DockSide::left)) &&
                workspace.register_panel(tool(made.factory, "output", "Output", DockSide::bottom)) &&
                workspace.register_panel(document(made.factory, "doc.first", "First.txt")) &&
                workspace.register_panel(document(made.factory, "doc.second", "Second.txt")),
            "the panels register");
    apply(workspace, DockEdit::show(DockPanelId("doc.first")));
    apply(workspace, DockEdit::show(DockPanelId("doc.second")));
    apply(workspace, DockEdit::show(DockPanelId("solution")));
    apply(workspace, DockEdit::show(DockPanelId("output")));
    made.site = make_control<DockSite>(StableId("site"), workspace);
    made.window = std::make_unique<Window>(made.site, Size{800.0, 500.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

Rect output_caption(const DockSite& site, const DockWorkspace& workspace) {
    const DockNodeId stack = workspace.layout().locate(DockPanelId("output")).stack;
    const std::vector<DockSite::Stack> stacks = site.realized_stack_items();
    for (const DockSite::Stack& record : stacks) {
        if (record.stack == stack) return record.parts.caption_label.bounds;
    }
    require(false, "the output stack is realized");
    return {};
}

// The opacity of each of the hovered pane's tiles and its glyph, in tile
// order, and a hash of what the drag layer draws inside them.
struct TileFrame final {
    std::vector<double> opacities{};
    std::vector<double> edge_opacities{};
    std::uint64_t drawn{};
    // The cross's parts composited at a partial opacity: each tile's plate
    // raster, and a paint group around each glyph.
    std::size_t fading_groups{};
};

bool inside(Rect outer, Rect inner) {
    const bool contained = inner.x >= outer.x - 0.001 && inner.y >= outer.y - 0.001 &&
                           inner.x + inner.width <= outer.x + outer.width + 0.001 &&
                           inner.y + inner.height <= outer.y + outer.height + 0.001;
    return contained;
}

void hash_operation(BitHash& hash, const VisualPaintOperationSnapshot& operation) {
    hash.add_word(static_cast<std::uint64_t>(operation.operation));
    hash.add_length(operation.rect.x);
    hash.add_length(operation.rect.y);
    hash.add_length(operation.rect.width);
    hash.add_length(operation.rect.height);
    hash.add_length(operation.first.x);
    hash.add_length(operation.first.y);
    hash.add_length(operation.second.x);
    hash.add_length(operation.second.y);
    // A paint group's opacity (the fade), exact.
    hash.add_number(operation.scalar);
    const std::array<std::uint8_t, 4> colour{operation.color.red, operation.color.green, operation.color.blue,
                                             operation.color.alpha};
    hash.add_bytes(colour);
    for (const GradientStop& stop : operation.gradient_stops) {
        const std::array<std::uint8_t, 4> stop_colour{stop.color.red, stop.color.green, stop.color.blue,
                                                      stop.color.alpha};
        hash.add_bytes(stop_colour);
    }
}

TileFrame tile_frame(Window& window, const DockSite& site) {
    TileFrame frame{};
    const std::vector<DockSite::Tile> tiles = site.realized_tiles();
    std::vector<Rect> crosses{};
    for (const DockSite::Tile& tile : tiles) {
        const EnginePartRecord* const plate = record_of(window, "site", "tile", tile.stable_id);
        const EnginePartRecord* const glyph = record_of(window, "site", "tile_glyph", tile.stable_id);
        require(plate != nullptr && glyph != nullptr, "every tile and glyph has a record");
        if (tile.kind == DockDropTargetKind::edge) {
            frame.edge_opacities.push_back(channel_value(plate));
            continue;
        }
        frame.opacities.push_back(channel_value(plate));
        frame.opacities.push_back(channel_value(glyph));
        crosses.push_back(tile.parts.tile.bounds);
    }
    VisualInspectionOptions options{};
    options.maximum_operations_per_control = VisualInspectionOptions::maximum_allowed_operations_per_control;
    const VisualInspectionSnapshot snapshot = window.visual_inspection_snapshot(options);
    const VisualControlInspection* const layer = snapshot.find("site.drag-layer");
    require(layer != nullptr && !(*layer).display_operations_truncated, "the drag layer's commands are inspected");
    BitHash hash{};
    for (const VisualPaintOperationSnapshot& operation : (*layer).display_operations) {
        bool within = false;
        for (const Rect cross : crosses) {
            const Rect points{std::min(operation.first.x, operation.second.x),
                              std::min(operation.first.y, operation.second.y),
                              std::fabs(operation.second.x - operation.first.x),
                              std::fabs(operation.second.y - operation.first.y)};
            const bool rect_inside = !operation.rect.empty() && inside(cross, operation.rect);
            const bool line_inside = operation.operation == VisualPaintOperation::draw_line && inside(cross, points);
            // A fading tile is one paint group around it (owner, E7-1).
            const bool group_around = operation.operation == VisualPaintOperation::begin_group &&
                                      inside(operation.rect, cross);
            const bool group_end = operation.operation == VisualPaintOperation::end_group;
            // A tile's plate is its primitive's raster (E8), placed at the
            // tile's origin.
            const bool raster_at = operation.operation == VisualPaintOperation::draw_part_raster &&
                                   inside(cross, Rect{operation.first.x, operation.first.y, 0.0, 0.0});
            if (rect_inside || line_inside || group_around || group_end || raster_at) within = true;
        }
        if (within) hash_operation(hash, operation);
        const bool composited = operation.operation == VisualPaintOperation::begin_group ||
                                operation.operation == VisualPaintOperation::draw_part_raster;
        const bool fading = composited && operation.scalar > 0.0 && operation.scalar < 1.0;
        if (fading) frame.fading_groups = frame.fading_groups + 1U;
    }
    frame.drawn = hash.value;
    return frame;
}

constexpr std::uint64_t expected_fade_hash = 0xca644bce5d05fa2eULL;

void tile_fade() {
    std::unique_ptr<DockFixture> fixture = make_dock();
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    const Rect caption = output_caption(site, *(*fixture).workspace);
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, centre(caption)}));
    move_to(window, Point{centre(caption).x + 10.0, centre(caption).y + 10.0});
    move_to(window, Point{400.0, 200.0});
    require(site.drag_phase() == DockDragPhase::dragging, "the drag starts");
    repaint(window, (*fixture).painter);
    TileFrame first = tile_frame(window, site);
    require(first.opacities.size() == 10U && first.edge_opacities.size() == 4U,
            "the hovered pane shows its cross of five tiles, and the four edge tiles show");
    for (const double opacity : first.opacities) require(opacity == 0.0, "the cross appears at opacity 0");
    for (const double opacity : first.edge_opacities) require(opacity == 1.0, "the edge tiles do not fade");
    BitHash fade{};
    fade.add_word(first.drawn);
    std::vector<double> steps{};
    for (int step = 1; step <= 7; ++step) {
        static_cast<void>(run_frame(window, (*fixture).time, (*fixture).painter));
        const TileFrame frame = tile_frame(window, site);
        steps.push_back(frame.opacities[0]);
        for (const double opacity : frame.opacities) {
            require(opacity == frame.opacities[0], "the cross's tiles and glyphs fade together");
        }
        fade.add_number(frame.opacities[0]);
        fade.add_word(frame.drawn);
        if (step < 7) {
            require(frame.fading_groups == 10U,
                    "mid-fade, each tile's plate and each glyph's group composite once at its opacity");
        }
    }
    for (int step = 0; step < 6; ++step) {
        require(std::fabs(steps[static_cast<std::size_t>(step)] - 0.16 * static_cast<double>(step + 1)) < 1e-12,
                "the fade is linear over 100 ms");
    }
    require(steps[6] == 1.0, "and ends exactly at 1 after 100 ms");
    std::size_t frames = 0U;
    for (int step = 0; step < 40 && window.motion_clock().armed(); ++step) {
        frames += run_frame(window, (*fixture).time, (*fixture).painter);
    }
    require(!window.motion_clock().armed() && run_frame(window, (*fixture).time, (*fixture).painter) == 0U,
            "once the fade and the stamp settle, nothing is scheduled");
    if (print_mode()) {
        std::cout << "tile fade hash 0x" << std::hex << fade.value << std::dec << '\n';
    } else {
        require(fade.value == expected_fade_hash, "the fade has the same bits on every host");
    }

    // Reduced motion: the cross is opaque at once.
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    move_to(window, Point{100.0, 100.0});
    repaint(window, (*fixture).painter);
    const TileFrame calm = tile_frame(window, site);
    for (const double opacity : calm.opacities) require(opacity == 1.0, "under reduced motion a new cross is opaque");
    require(!window.motion_clock().armed(), "and nothing is scheduled");
}

// -- ambient effects -----------------------------------------------------------

class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}

    void on_paint(Painter& painter, Rect) override {
        if (!shown) return;
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = face;
        part.key = PartKey{"face", {}, {}};
        paint_part(painter, *this, part);
    }

    Rect face{};
    bool shown{true};
};

// amplitude = 0.125 + time_in_state / 2: within one period of 1 s it runs
// from 0.125 to 0.625, and the pose at 250 ms shows 0.25.
ThemeRule ambient_rule(std::string stable_id, double frames_per_second) {
    ThemeRule rule{};
    rule.label = stable_id;
    rule.selector.stable_id = std::move(stable_id);
    rule.declarations.push_back(RuleDeclaration::primitive("bulge"));
    const std::array<ExpressionInstruction, 5> program{
        ExpressionInstruction::input(engine_input_position(EngineInput::time_in_state)),
        ExpressionInstruction::number(0.5), ExpressionInstruction::apply(ExpressionOperation::multiply),
        ExpressionInstruction::number(0.125), ExpressionInstruction::apply(ExpressionOperation::add)};
    rule.declarations.push_back(RuleDeclaration::expression_value("amplitude", program, 0.125));
    AmbientEffect effect{};
    effect.period = milliseconds(1000);
    effect.frames_per_second = frames_per_second;
    effect.pose = milliseconds(250);
    rule.ambient = effect;
    return rule;
}

struct AmbientFixture final {
    ManualTime time{};
    std::shared_ptr<Panel> root;
    std::shared_ptr<Pad> small;
    std::shared_ptr<Pad> large;
    std::unique_ptr<Window> window;
    DiscardingPainter painter{};
};

// A 200 by 150 window (30000 square units; 10 % is 3000): a small pad
// whose face is 40 by 25 (1000) and a large one whose face is 100 by 50
// (5000, still within the animated part's per-frame budget).
std::unique_ptr<AmbientFixture> make_ambient() {
    std::unique_ptr<AmbientFixture> fixture = std::make_unique<AmbientFixture>();
    AmbientFixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.small = make_control<Pad>(StableId("small"));
    (*made.small).set_requested_bounds({0.0, 0.0, 100.0, 75.0});
    (*made.small).face = Rect{10.0, 10.0, 40.0, 25.0};
    made.large = make_control<Pad>(StableId("large"));
    (*made.large).set_requested_bounds({100.0, 0.0, 100.0, 150.0});
    (*made.large).face = Rect{0.0, 10.0, 100.0, 50.0};
    (*made.root).add_child(made.small);
    (*made.root).add_child(made.large);
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "ambient-test";
    definition.rules.rules.push_back(ambient_rule("small", 30.0));
    definition.rules.rules.push_back(ambient_rule("large", 30.0));
    made.window = std::make_unique<Window>(made.root, Size{200.0, 150.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    window.set_theme(Theme::create(definition));
    require(window.theme().engine().rules().diagnostics().empty(), "the ambient theme compiles cleanly");
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

double drawn_amplitude(const Window& window, std::string_view control) {
    const EnginePartRecord* const record = record_of(window, control, "face", {});
    require(record != nullptr && (*record).cached && (*(*record).cached).raster, "the face was drawn");
    const double amplitude = (*(*record).cached).parameters.values[0].number;
    return amplitude;
}

// Polls every millisecond for `milliseconds_run`, painting after each poll;
// returns the ambient frames counted.
std::uint64_t run_ambient(AmbientFixture& fixture, int milliseconds_run) {
    Window& window = *fixture.window;
    const std::uint64_t before = window.metrics_snapshot().engine_ambient_frames;
    for (int step = 0; step < milliseconds_run; ++step) {
        fixture.time.time += milliseconds(1);
        static_cast<void>(window.poll_frame_schedule(fixture.time.time));
        repaint(window, fixture.painter);
    }
    const std::uint64_t frames = window.metrics_snapshot().engine_ambient_frames - before;
    return frames;
}

void ambient_effects() {
    std::unique_ptr<AmbientFixture> fixture = make_ambient();
    Window& window = *(*fixture).window;
    require(window.motion_clock().ambient_count() == 1U && window.metrics_snapshot().engine_ambient_refusals == 1U,
            "the small effect runs; the large one, over 10 % of the window, is refused");
    require(drawn_amplitude(window, "large") == 0.25, "a refused effect rests at its pose");
    const std::uint64_t second = run_ambient(*fixture, 1000);
    require(second >= 25U && second <= 30U, "an effect runs at no more than its 30 frames per second");
    require(drawn_amplitude(window, "small") > 0.125, "and its part shows the time within its period");

    // The application marks the large pad's surface ceremonial: its cap is
    // lifted, for that surface only.
    (*(*fixture).large).ceremonial = true;
    repaint(window, (*fixture).painter);
    require(window.motion_clock().ambient_count() == 2U, "on a ceremonial surface the area cap is lifted");
    (*(*fixture).large).ceremonial = false;
    repaint(window, (*fixture).painter);
    require(window.motion_clock().ambient_count() == 1U && window.metrics_snapshot().engine_ambient_refusals == 2U,
            "and off it the everyday cap holds again");

    // Occluded or minimized (GIVEN D4-1): every effect stops.
    window.set_occluded(true, (*fixture).time.time);
    require(window.motion_clock().ambient_count() == 0U && !window.motion_clock().armed(),
            "an occluded window runs no ambient effect and schedules nothing");
    require(run_ambient(*fixture, 200) == 0U, "and paints no ambient frames");
    window.set_occluded(false, (*fixture).time.time);
    repaint(window, (*fixture).painter);
    require(window.motion_clock().ambient_count() == 1U, "seen again, the effect resumes");

    // Reduced motion (GIVEN D2-1): effects stop and the part holds its pose.
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    repaint(window, (*fixture).painter);
    require(window.motion_clock().ambient_count() == 0U && !window.motion_clock().armed() &&
                drawn_amplitude(window, "small") == 0.25,
            "under reduced motion the effect stops and its part holds its static pose");
    require(run_ambient(*fixture, 200) == 0U, "and nothing is painted for it");
    settings.reduced_motion = false;
    window.set_presentation_settings(settings);
    repaint(window, (*fixture).painter);
    require(window.motion_clock().ambient_count() == 1U, "after reduced motion it resumes");

    // The application's motion switch off: effects stop.
    settings.ambient_motion = false;
    window.set_presentation_settings(settings);
    repaint(window, (*fixture).painter);
    require(window.motion_clock().ambient_count() == 0U && !window.motion_clock().armed() &&
                drawn_amplitude(window, "small") == 0.25,
            "with the motion switch off the effect stops at its pose");
    settings.ambient_motion = true;
    window.set_presentation_settings(settings);
    repaint(window, (*fixture).painter);
    require(window.motion_clock().ambient_count() == 1U, "and the switch on starts it again");

    // A part no longer painted ends its effect: it cannot keep the window
    // painting by itself.
    (*(*fixture).small).shown = false;
    (*(*fixture).small).invalidate(Dirty::paint);
    repaint(window, (*fixture).painter);
    static_cast<void>(run_ambient(*fixture, 200));
    require(window.motion_clock().ambient_count() == 0U && !window.motion_clock().armed(),
            "an effect whose part is no longer painted ends");
}

} // namespace

int main() {
    machines_are_rule_data();
    ribbon_rise();
    ribbon_rise_before_idle_bakes();
    ribbon_without_motion();
    tile_fade();
    ambient_effects();
    std::cout << "engine motion: state machines, the ribbon's rise, the tiles' fade and ambient effects passed\n";
    return EXIT_SUCCESS;
}
