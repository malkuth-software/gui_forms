// T2's gate evidence (T2 plan, phase E11). Two halves of the gate are
// checked here for every registered primitive, without listing any by hand.
//
// - Reference images: every primitive in builtin_primitive_registry() is
//   declared by a theme rule on one part, drawn through the window at four
//   scales and three states, and hashed: the extent and bytes of every part
//   raster composited. The manifest also counts the window's other drawing
//   calls (the built-in recipe draws a part its primitive refused) and the
//   engine's refusals. With GUI_FORMS_ENGINE_GATE_MANIFEST=<file>
//   the hashes are written as a manifest. Every CI host writes one, and
//   tools/engine_gate.py compares them across hosts, failing on any
//   difference. A primitive registered later joins the manifest by
//   registration alone.
// - Idle frames: once inputs stop and every settling primitive has settled,
//   the window counters show no frame over a two-second window. This is
//   checked for each primitive's window and for the ribbon's rise. The
//   ribbon under reduced motion is also checked: hovering it runs no
//   motion frame.
//
// - Feature samples: most of the engine's features are layers and
//   parameters of a primitive, not primitives (fill kinds, shapes, borders,
//   filters, blends, finish, mask, procedural patterns, glows, glass's colour
//   filter, the sheen at the pointer, motion). Every sample in
//   engine_samples::builtin_feature_samples() is drawn the same way, at the
//   four scales; a motion sample is also drawn at fixed frames after it
//   turns hot. A sample's hash covers what the engine hands the painter: the
//   part rasters' extents and bytes and every drawing command with its
//   geometry, opacity and paint group, so that a scale, a translation, a
//   cross-fade or a blend changes it. Its window must settle and then run
//   no frame. The census reads the samples' themes and requires every
//   feature of the engine's vocabulary in at least one of them: a new fill
//   kind, easing or other enumerator fails the build here (-Werror=switch)
//   until its name is added, and fails the test until a sample draws it.
//
// The other two halves are proved by the primitives' own tests and listed
// in notes:results/2026-10-22-t2-engine-gate/README.md: cost fallback (a part
// whose cost cannot be bounded is drawn by the built-in recipe) and
// reduced motion.

#include "gui_forms/gui_forms.hpp"

#include "../src/core/engine/feature_samples/engine_feature_samples.hpp"
#include "../src/core/image/png_writer.hpp"
#include "../src/core/text/composite/text_composite.hpp"
#include "support/rgba_order.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using namespace gui_forms;
using std::chrono::milliseconds;

void require(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_gate_tests: " << message << '\n';
    std::exit(1);
}

// FNV-1a over bytes and words, in order.
struct GateHash final {
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
};

std::string hex(std::uint64_t value) {
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << value;
    return text.str();
}

class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// A painter that hashes the part rasters composited and counts the
// other drawing calls (fills, strokes, lines, box shadows); it draws nothing.
// With `placed` set (the feature samples) it also hashes every command it
// is given, in order: a tag, then its geometry and opacity in steps of
// 1 / placement_steps of a unit, its colour, its paint group.
class GatePainter final : public Painter {
public:
    void save() override { add_tag(command_save); }
    void restore() override { add_tag(command_restore); }
    void translate(Point offset) override {
        add_tag(command_translate);
        add_point(offset);
    }
    void clip_rect(Rect rect) override {
        add_tag(command_clip);
        add_rect(rect);
    }
    void begin_group(const PaintGroup& group) override {
        add_tag(command_begin_group);
        add_rect(group.bounds);
        add_amount(group.opacity);
        add_tag(static_cast<std::uint64_t>(group.composite));
        add_tag(group.isolated ? 1U : 0U);
    }
    void end_group() override { add_tag(command_end_group); }
    void fill_rect(Rect rect, Color color) override {
        painter_calls = painter_calls + 1U;
        add_tag(command_fill);
        add_rect(rect);
        add_colour(color);
    }
    void stroke_rect(Rect rect, Color color, double width) override {
        painter_calls = painter_calls + 1U;
        add_tag(command_stroke);
        add_rect(rect);
        add_colour(color);
        add_amount(width);
    }
    void draw_line(Point from, Point to, Color color, double width) override {
        painter_calls = painter_calls + 1U;
        add_tag(command_line);
        add_point(from);
        add_point(to);
        add_colour(color);
        add_amount(width);
    }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_box_shadow(Rect rect, double radius, Point offset, double blur, double spread, Color color) override {
        painter_calls = painter_calls + 1U;
        add_tag(command_box_shadow);
        add_rect(rect);
        add_amount(radius);
        add_point(offset);
        add_amount(blur);
        add_amount(spread);
        add_colour(color);
    }
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double opacity) override {
        add_tag(command_raster);
        add_point(origin);
        add_amount(opacity);
        add_raster(*raster);
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                 double opacity) override {
        add_tag(command_raster_scaled);
        add_rect(destination);
        add_amount(opacity);
        add_raster(*raster);
    }

    void reset() {
        hash = GateHash{};
        rasters = 0U;
        painter_calls = 0U;
    }

    GateHash hash{};
    std::size_t rasters{};
    std::size_t painter_calls{};
    bool placed{};

private:
    static constexpr std::uint64_t command_save = 0x100U;
    static constexpr std::uint64_t command_restore = 0x101U;
    static constexpr std::uint64_t command_translate = 0x102U;
    static constexpr std::uint64_t command_clip = 0x103U;
    static constexpr std::uint64_t command_begin_group = 0x104U;
    static constexpr std::uint64_t command_end_group = 0x105U;
    static constexpr std::uint64_t command_fill = 0x106U;
    static constexpr std::uint64_t command_stroke = 0x107U;
    static constexpr std::uint64_t command_line = 0x108U;
    static constexpr std::uint64_t command_box_shadow = 0x109U;
    static constexpr std::uint64_t command_raster = 0x10AU;
    static constexpr std::uint64_t command_raster_scaled = 0x10BU;
    // Geometry and opacity are hashed rounded to 1/4096: every host computes
    // them alike (no fused multiply-add), and the rounding keeps a last-bit
    // difference in a value no host shows from reading as a difference.
    static constexpr double placement_steps = 4096.0;

    void add_tag(std::uint64_t tag) {
        if (placed) hash.add_word(tag);
    }
    void add_amount(double value) {
        if (!placed) return;
        const long long steps = std::llround(value * placement_steps);
        hash.add_word(static_cast<std::uint64_t>(steps));
    }
    void add_point(Point point) {
        add_amount(point.x);
        add_amount(point.y);
    }
    void add_rect(Rect rect) {
        add_amount(rect.x);
        add_amount(rect.y);
        add_amount(rect.width);
        add_amount(rect.height);
    }
    void add_colour(Color color) {
        const std::uint64_t packed = (static_cast<std::uint64_t>(color.red) << 24U) |
                                     (static_cast<std::uint64_t>(color.green) << 16U) |
                                     (static_cast<std::uint64_t>(color.blue) << 8U) |
                                     static_cast<std::uint64_t>(color.alpha);
        add_tag(packed);
    }
    void add_raster(const PartRaster& raster) {
        const DeviceExtent extent = raster.extent();
        hash.add_word(extent.width);
        hash.add_word(extent.height);
        hash.add_bytes(gui_forms::test_support::rgba_order(raster.pixels()));
        rasters = rasters + 1U;
    }
};

// One part of the primitive's first role (or a sample's role), 96 by 28 at
// (20, 20) with radius 4, in the states the test sets, granted `overflow`.
class Subject final : public Control {
public:
    Subject(StableId stable_id, PartRole role) : Control(std::move(stable_id)), role_(role) {}
    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = role_;
        part.family = ControlVisualRole::button;
        part.bounds = engine_samples::feature_sample_bounds;
        part.corner_radius = engine_samples::feature_sample_corner_radius;
        part.key = PartKey{"subject", {}, {}};
        part.states = states;
        part.granted_overflow = overflow;
        paint_part(painter, *this, part);
    }
    AnatomyStateSet states{};
    Insets overflow{};

private:
    PartRole role_;
};

struct SubjectFixture final {
    ManualTime time{};
    std::shared_ptr<Panel> root;
    std::shared_ptr<Subject> subject;
    std::unique_ptr<Window> window;
    GatePainter painter{};
    std::size_t diagnostics{};
};

// The backplane a window is drawn on: the professional theme's (light), or
// for the specimen sheet a dark one (its window and panel recipes a dark
// solid), applied before a sample writes its theme.
enum class Backplane : std::uint8_t { light, dark };

void use_backplane(ThemeDefinition& definition, Backplane backplane) {
    if (backplane == Backplane::light) return;
    SurfaceMaterial dark{};
    dark.fills.clear();
    dark.fills.push_back(MaterialFillLayer::solid(Color::rgba(34, 38, 46)));
    const ControlVisualRole roles[] = {ControlVisualRole::window, ControlVisualRole::panel};
    for (const ControlVisualRole role : roles) {
        ControlRoleRecipes& recipes = definition.roles[static_cast<std::size_t>(role)];
        for (ControlVisualRecipe& recipe : recipes.ordinary) recipe.material = dark;
        for (ControlVisualRecipe& recipe : recipes.selected) recipe.material = dark;
    }
}

std::unique_ptr<SubjectFixture> make_subject(const EnginePrimitive& primitive, double scale,
                                             AnatomyStateSet states, Backplane backplane = Backplane::light) {
    const PrimitiveDescriptor& descriptor = primitive.descriptor();
    require(!descriptor.roles.empty(), std::string(descriptor.name) + " paints at least one role");
    const PartRole role = descriptor.roles[0];
    ThemeDefinition definition = windows_professional_theme_definition();
    definition.id = "engine-gate";
    use_backplane(definition, backplane);
    ThemeRule rule{};
    rule.label = "gate";
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = "subject";
    rule.selector.role = role;
    rule.declarations.push_back(RuleDeclaration::primitive(std::string(descriptor.name)));
    definition.rules.rules.push_back(rule);
    std::unique_ptr<SubjectFixture> fixture = std::make_unique<SubjectFixture>();
    SubjectFixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.subject = make_control<Subject>(StableId("subject"), role);
    (*made.subject).states = states;
    (*made.subject).set_requested_bounds(Rect{0.0, 0.0, 136.0, 68.0});
    (*made.root).add_child(made.subject);
    made.window = std::make_unique<Window>(made.root, Size{176.0, 108.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    window.set_scale(scale);
    window.set_theme(Theme::create(definition));
    made.diagnostics = window.theme().engine().rules().diagnostics().size();
    window.perform_layout();
    made.painter.reset();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

// One 16 ms frame of the test's time: poll, then paint what it damaged.
// Returns whether the poll ran anything or left damage.
bool run_frame(Window& window, ManualTime& time, Painter& painter) {
    time.time += milliseconds(16);
    const FramePollResult result = window.poll_frame_schedule(time.time);
    window.perform_layout();
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(painter, damage.bounds());
    const std::uint64_t work = result.deadlines_fired + result.ui_timer_ticks +
                               result.motion_clock_ticks;
    const bool ran = work != 0U || !damage.empty() || result.damage_pending;
    return ran;
}

// Moves the pointer to `point` in the window.
void move_to(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, point}));
    window.perform_layout();
}

// What the window counters showed over an idle stretch.
struct IdleWindow final {
    std::size_t frames_run{};
    std::uint64_t frame_requests{};
    std::uint64_t paint_passes{};
    std::uint64_t motion_clock_ticks{};
    std::uint64_t ambient_frames{};
};

constexpr std::size_t settle_limit = 240U;
constexpr std::size_t idle_polls = 125U; // two seconds of 16 ms frames

// Runs frames until one runs nothing and no idle keyframe bake waits.
void settle(Window& window, ManualTime& time, Painter& painter, const std::string& subject) {
    std::size_t step = 0U;
    while (step < settle_limit) {
        const bool ran = run_frame(window, time, painter);
        step = step + 1U;
        if (!ran && !window.engine_parts().idle_keyframes_waiting()) break;
    }
    require(step < settle_limit, subject + " settles within " + std::to_string(settle_limit) + " frames");
}

// Settles the window, then counts what it does over idle_polls more frames.
IdleWindow settle_then_watch(Window& window, ManualTime& time, Painter& painter, const std::string& subject) {
    settle(window, time, painter, subject);
    const MetricsSnapshot before = window.metrics_snapshot();
    IdleWindow idle{};
    for (std::size_t poll = 0U; poll < idle_polls; ++poll) {
        if (run_frame(window, time, painter)) idle.frames_run = idle.frames_run + 1U;
    }
    const MetricsSnapshot after = window.metrics_snapshot();
    idle.frame_requests = after.scheduled_frame_requests - before.scheduled_frame_requests;
    idle.paint_passes = after.paint_passes - before.paint_passes;
    idle.motion_clock_ticks = after.motion_clock_ticks - before.motion_clock_ticks;
    idle.ambient_frames = after.engine_ambient_frames - before.engine_ambient_frames;
    return idle;
}

bool silent(const IdleWindow& idle) {
    const bool quiet = idle.frames_run == 0U && idle.frame_requests == 0U && idle.paint_passes == 0U &&
                       idle.motion_clock_ticks == 0U && idle.ambient_frames == 0U;
    return quiet;
}

// The manifest's lines, written at the end when GUI_FORMS_ENGINE_GATE_MANIFEST
// names a file.
std::vector<std::string> manifest_lines{};

void add_counter(const std::string& name, std::uint64_t value) {
    manifest_lines.push_back("counter\t" + name + '\t' + std::to_string(value));
}

void add_idle(const std::string& subject, const IdleWindow& idle) {
    add_counter(subject + ".idle_frames_run", idle.frames_run);
    add_counter(subject + ".idle_frame_requests", idle.frame_requests);
    add_counter(subject + ".idle_paint_passes", idle.paint_passes);
    add_counter(subject + ".idle_motion_clock_ticks", idle.motion_clock_ticks);
    add_counter(subject + ".idle_ambient_frames", idle.ambient_frames);
}

struct StateCase final {
    const char* name;
    AnatomyState state;
    bool set;
};

constexpr std::array<StateCase, 3> state_cases{{
    {"rest", AnatomyState::hot, false},
    {"hot", AnatomyState::hot, true},
    {"pressed", AnatomyState::pressed, true},
}};

constexpr std::array<double, 4> gate_scales{1.0, 1.25, 1.5, 2.0};

void every_registered_primitive() {
    const std::span<const EnginePrimitive* const> primitives = builtin_primitive_registry().primitives();
    require(!primitives.empty(), "the built-in registry holds primitives");
    manifest_lines.push_back("registry\t" + std::to_string(primitives.size()));
    for (const EnginePrimitive* const primitive : primitives) {
        const std::string name((*primitive).descriptor().name);
        for (const StateCase& state_case : state_cases) {
            AnatomyStateSet states{};
            if (state_case.set) states.insert(state_case.state);
            for (const double scale : gate_scales) {
                std::unique_ptr<SubjectFixture> fixture = make_subject(*primitive, scale, states);
                SubjectFixture& made = *fixture;
                const std::string subject = name + ' ' + state_case.name + " at " + std::to_string(scale);
                require(made.diagnostics == 0U, subject + ": a rule declaring the primitive compiles cleanly");
                require(made.painter.rasters > 0U || made.painter.painter_calls > 0U,
                        subject + ": the part is drawn by the primitive or by the built-in recipe");
                const std::uint64_t first = made.painter.hash.value;
                const std::size_t rasters = made.painter.rasters;
                const std::size_t painter_calls = made.painter.painter_calls;
                const std::uint64_t refusals = (*made.window).metrics_snapshot().engine_primitive_refusals;
                // A second window draws the same bytes: nothing depends on
                // anything but the primitive, the part and the scale.
                std::unique_ptr<SubjectFixture> again = make_subject(*primitive, scale, states);
                require((*again).painter.hash.value == first && (*again).painter.rasters == rasters,
                        subject + ": drawing it again gives the same bytes");
                const IdleWindow idle = settle_then_watch(*made.window, made.time, made.painter, subject);
                require(silent(idle), subject + ": once settled, the window runs no frame");
                std::ostringstream scale_text;
                scale_text << std::fixed << std::setprecision(2) << scale;
                manifest_lines.push_back("primitive\t" + name + '\t' + state_case.name + '\t' + scale_text.str() +
                                         '\t' + std::to_string(rasters) + '\t' + std::to_string(painter_calls) +
                                         '\t' + std::to_string(refusals) + '\t' + hex(first));
                if (state_case.set || scale != 1.0) continue;
                add_idle("primitive." + name, idle);
            }
        }
    }
}

// -- the feature samples ---------------------------------------------------------

using engine_samples::EngineFeatureSample;
using engine_samples::FeatureSampleDrive;

// A sample's window: its client area.
constexpr Rect sample_window_area{0.0, 0.0, 176.0, 108.0};

struct SampleFixture final {
    ManualTime time{};
    std::shared_ptr<Panel> root;
    std::shared_ptr<Subject> subject;
    std::unique_ptr<Window> window;
    GatePainter painter{};
    std::size_t diagnostics{};
};

// The professional theme as the sample writes it, for the image `image`.
// Held on the heap, as a definition is never kept on a test's stack.
std::unique_ptr<ThemeDefinition> sample_definition(const EngineFeatureSample& sample, ImageId image,
                                                   Backplane backplane = Backplane::light) {
    std::unique_ptr<ThemeDefinition> held(new ThemeDefinition(windows_professional_theme_definition()));
    ThemeDefinition& definition = *held;
    definition.id = "engine-gate-sample";
    use_backplane(definition, backplane);
    engine_samples::FeatureSampleInputs inputs{};
    inputs.image = image;
    sample.write_theme(inputs, definition);
    return held;
}

// A sample's window at `scale` with its subject of `role`, before a theme.
std::unique_ptr<SampleFixture> shell_window(PartRole role, double scale) {
    std::unique_ptr<SampleFixture> fixture = std::make_unique<SampleFixture>();
    SampleFixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.subject = make_control<Subject>(StableId(std::string(engine_samples::feature_sample_subject)), role);
    (*made.subject).overflow = engine_samples::feature_sample_overflow;
    (*made.subject).set_requested_bounds(Rect{0.0, 0.0, 136.0, 68.0});
    (*made.root).add_child(made.subject);
    made.window = std::make_unique<Window>(made.root, Size{sample_window_area.width, sample_window_area.height});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    window.set_scale(scale);
    return fixture;
}

// Lays the window out and paints it once.
void first_paint(SampleFixture& made) {
    Window& window = *made.window;
    made.diagnostics = window.theme().engine().rules().diagnostics().size();
    window.perform_layout();
    made.painter.reset();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
}

// The sample's window at `scale`: its image loaded first, then its theme,
// laid out and painted once.
std::unique_ptr<SampleFixture> make_sample(const EngineFeatureSample& sample, double scale,
                                           Backplane backplane = Backplane::light) {
    std::unique_ptr<SampleFixture> fixture = shell_window(sample.role, scale);
    SampleFixture& made = *fixture;
    Window& window = *made.window;
    const std::vector<std::byte> pixels = engine_samples::feature_sample_image_pixels();
    const std::uint32_t side = engine_samples::feature_sample_image_side;
    const ImageLoadResult loaded = window.load_bgra32_premultiplied(side, side, side * 4U, pixels);
    require(static_cast<bool>(loaded), std::string(sample.name) + ": the sample image loads");
    const std::unique_ptr<ThemeDefinition> definition = sample_definition(sample, loaded.image, backplane);
    window.set_theme(Theme::create(*definition));
    first_paint(made);
    return fixture;
}

// One image of a sample: the frame it shows and what the painter was given.
struct SampleImage final {
    std::string frame{};
    std::size_t rasters{};
    std::size_t painter_calls{};
    std::uint64_t hash{};
};

struct SpecimenSink;
void write_tile(SpecimenSink& sink, Window& window, const std::string& group, const std::string& name,
                const std::string& frame, double scale, Backplane backplane, std::uint64_t hash);

// Where a sample's images also go as specimen tiles (null: nowhere).
struct SpecimenTarget final {
    SpecimenSink* sink{};
    double scale{};
    Backplane backplane{Backplane::light};
    std::string name{};
};

// The whole window painted once more (an empty request would paint only
// the damage), every command hashed; with a specimen target, also written
// as a tile.
SampleImage take_image(Window& window, const std::string& frame, const SpecimenTarget* specimen) {
    GatePainter painter{};
    painter.placed = true;
    static_cast<void>(window.paint(painter, sample_window_area));
    SampleImage image{};
    image.frame = frame;
    image.rasters = painter.rasters;
    image.painter_calls = painter.painter_calls;
    image.hash = painter.hash.value;
    if (specimen != nullptr) {
        write_tile(*(*specimen).sink, window, "sample", (*specimen).name, frame, (*specimen).scale,
                   (*specimen).backplane, image.hash);
    }
    return image;
}

// A motion frame's name: the time since the part turned hot, "t032" for
// 32 ms.
std::string motion_frame_name(std::uint32_t frames) {
    std::ostringstream name;
    name << 't' << std::setw(3) << std::setfill('0') << frames * 16U;
    return name.str();
}

bool is_motion_frame(std::uint32_t frame) {
    for (const std::uint32_t sampled : engine_samples::feature_sample_motion_frames) {
        if (sampled == frame) return true;
    }
    return false;
}

// Drives the sample as its drive says, and takes its images in order.
std::vector<SampleImage> drive_sample(SampleFixture& made, const EngineFeatureSample& sample,
                                      const std::string& subject, const SpecimenTarget* specimen = nullptr) {
    Window& window = *made.window;
    std::vector<SampleImage> images{};
    settle(window, made.time, made.painter, subject);
    images.push_back(take_image(window, "rest", specimen));
    if (sample.drive == FeatureSampleDrive::pointer) {
        // The engine reads the pointer only over a hot part (GIVEN B1-1):
        // the part turns hot as a control's does when the pointer enters it.
        (*made.subject).states.insert(AnatomyState::hot);
        (*made.subject).invalidate(Dirty::paint);
        move_to(window, engine_samples::feature_sample_pointer);
        settle(window, made.time, made.painter, subject + " at the pointer");
        images.push_back(take_image(window, "pointer", specimen));
    }
    if (sample.drive == FeatureSampleDrive::hot_motion) {
        (*made.subject).states.insert(AnatomyState::hot);
        (*made.subject).invalidate(Dirty::paint);
        const std::uint32_t last = engine_samples::feature_sample_motion_frames.back();
        for (std::uint32_t frame = 1U; frame <= last; ++frame) {
            static_cast<void>(run_frame(window, made.time, made.painter));
            if (is_motion_frame(frame)) images.push_back(take_image(window, motion_frame_name(frame), specimen));
        }
        settle(window, made.time, made.painter, subject + " hot");
        images.push_back(take_image(window, "hot", specimen));
    }
    return images;
}

bool same_images(const std::vector<SampleImage>& left, const std::vector<SampleImage>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const SampleImage& one = left[index];
        const SampleImage& other = right[index];
        if (one.frame != other.frame || one.hash != other.hash || one.rasters != other.rasters) return false;
    }
    return true;
}

// Whether a driven sample's images show its drive: the last differs from
// the first (at rest), and a motion sample shows some frame between them
// that is neither.
bool shows_drive(const std::vector<SampleImage>& images) {
    const std::uint64_t first = images.front().hash;
    const std::uint64_t last = images.back().hash;
    if (first == last) return false;
    if (images.size() == 2U) return true;
    for (std::size_t index = 1U; index + 1U < images.size(); ++index) {
        if (images[index].hash != first && images[index].hash != last) return true;
    }
    return false;
}

// Whether the sample's rules cross-fade the face.
bool fades_face(const EngineFeatureSample& sample) {
    const std::unique_ptr<ThemeDefinition> held = sample_definition(sample, ImageId{});
    for (const ThemeRule& rule : (*held).rules.rules) {
        for (const PropertyTransition& transition : rule.transitions) {
            if (transition.property == face_property) return true;
        }
    }
    return false;
}

std::size_t most_rasters(const std::vector<SampleImage>& images) {
    std::size_t most = 0U;
    for (const SampleImage& image : images) most = std::max(most, image.rasters);
    return most;
}

// One hash over a sample's images, in order.
std::uint64_t sequence_hash(const std::vector<SampleImage>& images) {
    GateHash hash{};
    for (const SampleImage& image : images) hash.add_word(image.hash);
    return hash.value;
}

struct DrawnSample final {
    std::string name{};
    double scale{};
    std::uint64_t signature{};
};

void every_feature_sample() {
    std::vector<DrawnSample> drawn_samples{};
    const std::span<const EngineFeatureSample> samples = engine_samples::builtin_feature_samples();
    require(!samples.empty(), "the feature samples are listed");
    manifest_lines.push_back("samples\t" + std::to_string(samples.size()));
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        const EngineFeatureSample& sample = samples[index];
        const std::string name(sample.name);
        require(builtin_primitive_registry().find(sample.primitive) != nullptr,
                name + ": its primitive is registered");
        for (std::size_t other = 0U; other < index; ++other) {
            require(samples[other].name != sample.name, name + ": the sample is listed once");
        }
        for (const double scale : gate_scales) {
            std::ostringstream scale_text;
            scale_text << std::fixed << std::setprecision(2) << scale;
            const std::string subject = name + " at " + scale_text.str();
            std::unique_ptr<SampleFixture> fixture = make_sample(sample, scale);
            SampleFixture& made = *fixture;
            require(made.diagnostics == 0U, subject + ": the sample's theme compiles cleanly");
            const std::vector<SampleImage> images = drive_sample(made, sample, subject);
            Window& window = *made.window;
            const MetricsSnapshot metrics = window.metrics_snapshot();
            const RasterCacheStatistics cache = window.engine_parts().cache_statistics();
            require(metrics.engine_primitive_refusals == 0U, subject + ": the primitive draws it (nothing refused)");
            require(cache.image_mask_fallbacks == 0U, subject + ": every image mask is drawn");
            require(cache.procedural_queued == 0U, subject + ": every procedural pattern is generated");
            for (const SampleImage& image : images) {
                require(image.rasters > 0U, subject + " " + image.frame + ": a part raster is composited");
            }
            if (sample.drive != FeatureSampleDrive::rest) {
                require(shows_drive(images), subject + ": its images show the drive");
            }
            if (fades_face(sample)) {
                require(most_rasters(images) >= 2U, subject + ": a frame shows both faces of the cross-fade");
            }
            // No two samples draw alike: each draws a feature the others do not.
            const std::uint64_t signature = sequence_hash(images);
            for (const DrawnSample& drawn : drawn_samples) {
                if (drawn.scale != scale || drawn.signature != signature) continue;
                require(false, subject + ": draws differently from " + drawn.name);
            }
            drawn_samples.push_back(DrawnSample{name, scale, signature});
            std::unique_ptr<SampleFixture> again = make_sample(sample, scale);
            require(same_images(drive_sample(*again, sample, subject), images),
                    subject + ": drawing it again gives the same images");
            const IdleWindow idle = settle_then_watch(window, made.time, made.painter, subject);
            require(silent(idle), subject + ": once settled, the window runs no frame");
            for (const SampleImage& image : images) {
                manifest_lines.push_back("sample\t" + name + '\t' + std::string(sample.primitive) + '\t' +
                                         image.frame + '\t' + scale_text.str() + '\t' +
                                         std::to_string(image.rasters) + '\t' +
                                         std::to_string(image.painter_calls) + '\t' +
                                         std::to_string(metrics.engine_primitive_refusals) + '\t' + hex(image.hash));
            }
            if (scale != 1.0) continue;
            add_idle("sample." + name, idle);
        }
    }
}

// -- the census: every feature of the vocabulary in some sample ------------------

// The names of each enumeration's values. Every switch lists every value
// (this file is built with -Werror=switch), and a value past the last
// returns an empty name, so that `values` finds them all.
std::string_view fill_kind_name(MaterialFillKind kind) {
    switch (kind) {
    case MaterialFillKind::solid: return "solid";
    case MaterialFillKind::linear_gradient: return "linear_gradient";
    case MaterialFillKind::radial_gradient: return "radial_gradient";
    case MaterialFillKind::image: return "image";
    case MaterialFillKind::conic_gradient: return "conic_gradient";
    case MaterialFillKind::procedural: return "procedural";
    }
    return {};
}

std::string_view spread_name(GradientSpreadMode spread) {
    switch (spread) {
    case GradientSpreadMode::pad: return "pad";
    case GradientSpreadMode::repeat: return "repeat";
    case GradientSpreadMode::reflect: return "reflect";
    }
    return {};
}

std::string_view image_mode_name(MaterialImageMode mode) {
    switch (mode) {
    case MaterialImageMode::stretch: return "stretch";
    case MaterialImageMode::tile: return "tile";
    case MaterialImageMode::nine_patch: return "nine_patch";
    case MaterialImageMode::single: return "single";
    }
    return {};
}

std::string_view coordinate_space_name(MaterialCoordinateSpace space) {
    switch (space) {
    case MaterialCoordinateSpace::normalized: return "normalized";
    case MaterialCoordinateSpace::logical: return "logical";
    }
    return {};
}

std::string_view linear_geometry_name(MaterialLinearGeometry geometry) {
    switch (geometry) {
    case MaterialLinearGeometry::endpoints: return "endpoints";
    case MaterialLinearGeometry::css_angle: return "css_angle";
    }
    return {};
}

std::string_view pattern_name(ProceduralPattern pattern) {
    switch (pattern) {
    case ProceduralPattern::felt: return "felt";
    case ProceduralPattern::wood_grain: return "wood_grain";
    case ProceduralPattern::paper_fibre: return "paper_fibre";
    case ProceduralPattern::pinstripe: return "pinstripe";
    case ProceduralPattern::brushed_metal: return "brushed_metal";
    case ProceduralPattern::fresco: return "fresco";
    case ProceduralPattern::checker: return "checker";
    }
    return {};
}

std::string_view fill_blend_name(MaterialBlendMode blend) {
    switch (blend) {
    case MaterialBlendMode::normal: return "normal";
    case MaterialBlendMode::multiply: return "multiply";
    case MaterialBlendMode::screen: return "screen";
    }
    return {};
}

std::string_view border_style_name(MaterialBorderStyle style) {
    switch (style) {
    case MaterialBorderStyle::solid: return "solid";
    case MaterialBorderStyle::dashed: return "dashed";
    case MaterialBorderStyle::dotted: return "dotted";
    }
    return {};
}

std::string_view glow_placement_name(MaterialGlowPlacement placement) {
    switch (placement) {
    case MaterialGlowPlacement::under: return "under";
    case MaterialGlowPlacement::over: return "over";
    }
    return {};
}

std::string_view edge_name(MaterialEdge edge) {
    switch (edge) {
    case MaterialEdge::top: return "top";
    case MaterialEdge::right: return "right";
    case MaterialEdge::bottom: return "bottom";
    case MaterialEdge::left: return "left";
    }
    return {};
}

std::string_view part_blend_name(PartBlend blend) {
    switch (blend) {
    case PartBlend::normal: return "normal";
    case PartBlend::multiply: return "multiply";
    case PartBlend::screen: return "screen";
    }
    return {};
}

std::string_view presentation_property_name(PresentationProperty property) {
    switch (property) {
    case PresentationProperty::parameter: return "parameter";
    case PresentationProperty::opacity: return "opacity";
    case PresentationProperty::scale: return "scale";
    case PresentationProperty::face: return "face";
    case PresentationProperty::translate_x: return "translate_x";
    case PresentationProperty::translate_y: return "translate_y";
    }
    return {};
}

// A fling's decay is no transition's curve: it answers a velocity
// (MotionValue::fling), and its own tests cover it. The census asks for
// the other kinds.
std::string_view motion_kind_name(MotionKind kind) {
    switch (kind) {
    case MotionKind::follow: return "follow";
    case MotionKind::spring: return "spring";
    case MotionKind::decay: return {};
    case MotionKind::timed: return "timed";
    }
    return {};
}

std::string_view easing_name(MotionEasing easing) {
    switch (easing) {
    case MotionEasing::smoothstep: return "smoothstep";
    case MotionEasing::exponential_approach: return "exponential_approach";
    case MotionEasing::linear: return "linear";
    case MotionEasing::ease: return "ease";
    case MotionEasing::overshoot: return "overshoot";
    case MotionEasing::cubic_bezier: return "cubic_bezier";
    case MotionEasing::steps: return "steps";
    }
    return {};
}

std::string_view step_position_name(MotionStepPosition position) {
    switch (position) {
    case MotionStepPosition::end: return "end";
    case MotionStepPosition::start: return "start";
    }
    return {};
}

// Every named value of an enumeration (contiguous from 0), as
// "<group>.<name>".
template <typename Enumeration>
void add_values(std::vector<std::string>& keys, std::string_view group, std::string_view (*name_of)(Enumeration)) {
    for (unsigned value = 0U; value < 256U; ++value) {
        const std::string_view name = name_of(static_cast<Enumeration>(value));
        if (name.empty()) continue;
        keys.push_back(std::string(group) + '.' + std::string(name));
    }
}

// What the census requires: every value of the vocabulary's enumerations,
// and the features that are no enumeration.
std::vector<std::string> required_features() {
    std::vector<std::string> keys{};
    add_values(keys, "fill", fill_kind_name);
    add_values(keys, "spread", spread_name);
    add_values(keys, "image", image_mode_name);
    add_values(keys, "space", coordinate_space_name);
    add_values(keys, "linear", linear_geometry_name);
    add_values(keys, "procedural", pattern_name);
    add_values(keys, "fill_blend", fill_blend_name);
    add_values(keys, "border", border_style_name);
    add_values(keys, "glow", glow_placement_name);
    add_values(keys, "keyline", edge_name);
    add_values(keys, "part_blend", part_blend_name);
    add_values(keys, "motion.property", presentation_property_name);
    add_values(keys, "motion.kind", motion_kind_name);
    add_values(keys, "motion.easing", easing_name);
    add_values(keys, "motion.steps", step_position_name);
    for (const std::string_view shape : part_shape_names) keys.push_back("shape." + std::string(shape));
    const char* const plain[] = {
        "fill.opacity",   "image.position",        "border.edges",   "corner_radii",       "shadow.outer",
        "shadow.inset",   "colour_filter",         "drop_shadow",    "filter_blur",        "finish",
        "mask.solid",     "mask.linear_gradient",  "mask.image",     "glass.colour_filter", "sheen.pointer",
        "motion.delay",   "motion.stops",
    };
    for (const char* const key : plain) keys.push_back(key);
    return keys;
}

void add_key(std::vector<std::string>& keys, std::string key) {
    for (const std::string& held : keys) {
        if (held == key) return;
    }
    keys.push_back(std::move(key));
}

void add_fill_features(std::vector<std::string>& keys, const MaterialFillLayer& fill) {
    add_key(keys, "fill." + std::string(fill_kind_name(fill.kind)));
    add_key(keys, "fill_blend." + std::string(fill_blend_name(fill.blend)));
    if (fill.opacity < 1.0) add_key(keys, "fill.opacity");
    const bool gradient = fill.kind == MaterialFillKind::linear_gradient ||
                          fill.kind == MaterialFillKind::radial_gradient ||
                          fill.kind == MaterialFillKind::conic_gradient;
    if (gradient) {
        add_key(keys, "spread." + std::string(spread_name(fill.spread)));
        add_key(keys, "space." + std::string(coordinate_space_name(fill.coordinate_space)));
    }
    if (fill.kind == MaterialFillKind::linear_gradient) {
        add_key(keys, "linear." + std::string(linear_geometry_name(fill.linear_geometry)));
    }
    if (fill.kind == MaterialFillKind::image) {
        add_key(keys, "image." + std::string(image_mode_name(fill.image_mode)));
        if (!(fill.image_anchor == Point{}) || !(fill.image_offset == Point{})) add_key(keys, "image.position");
    }
    if (fill.kind == MaterialFillKind::procedural) {
        add_key(keys, "procedural." + std::string(pattern_name(fill.procedural.pattern)));
    }
}

void add_material_features(std::vector<std::string>& keys, const SurfaceMaterial& material) {
    for (const MaterialFillLayer& fill : material.fills) add_fill_features(keys, fill);
    for (const MaterialShadow& shadow : material.shadows) add_key(keys, shadow.inset ? "shadow.inset" : "shadow.outer");
    for (const MaterialGlow& glow : material.glows) {
        add_key(keys, "glow." + std::string(glow_placement_name(glow.placement)));
    }
    if (material.border) add_key(keys, "border." + std::string(border_style_name((*material.border).style)));
    if (!material.border_edges.empty()) add_key(keys, "border.edges");
    for (const MaterialKeyline& keyline : material.keylines) {
        add_key(keys, "keyline." + std::string(edge_name(keyline.edge)));
    }
    if (material.corner_radii) add_key(keys, "corner_radii");
    if (material.colour_filter) add_key(keys, "colour_filter");
    if (material.drop_shadow) add_key(keys, "drop_shadow");
    if (material.filter_blur > 0.0) add_key(keys, "filter_blur");
    if (material.finish > 0.0) add_key(keys, "finish");
    if (material.mask) add_key(keys, "mask." + std::string(fill_kind_name((*material.mask).kind)));
}

PresentationProperty transition_property(const PropertyTransition& transition) {
    if (transition.property == opacity_property) return PresentationProperty::opacity;
    if (transition.property == scale_property) return PresentationProperty::scale;
    if (transition.property == face_property) return PresentationProperty::face;
    if (transition.property == translate_x_property) return PresentationProperty::translate_x;
    if (transition.property == translate_y_property) return PresentationProperty::translate_y;
    return PresentationProperty::parameter;
}

void add_transition_features(std::vector<std::string>& keys, const PropertyTransition& transition) {
    add_key(keys, "motion.property." + std::string(presentation_property_name(transition_property(transition))));
    const MotionCurve& curve = transition.curve;
    const std::string_view kind = motion_kind_name(curve.kind);
    if (!kind.empty()) add_key(keys, "motion.kind." + std::string(kind));
    if (curve.kind == MotionKind::timed) add_key(keys, "motion.easing." + std::string(easing_name(curve.easing)));
    if (curve.kind == MotionKind::timed && curve.easing == MotionEasing::steps) {
        add_key(keys, "motion.steps." + std::string(step_position_name(curve.step_position)));
    }
    if (curve.delay > FrameInterval{}) add_key(keys, "motion.delay");
    for (const PresentationState& state : transition.states) {
        if (!state.stops.empty()) add_key(keys, "motion.stops");
    }
}

// A declaration's choice, or `fallback` if the rule declares none.
std::string_view declared_choice(const ThemeRule& rule, std::string_view property, std::string_view fallback) {
    for (const RuleDeclaration& declaration : rule.declarations) {
        if (declaration.property == property && declaration.kind == RuleValueKind::name) return declaration.name;
    }
    return fallback;
}

bool declares_primitive(const ThemeRule& rule, std::string_view primitive) {
    for (const RuleDeclaration& declaration : rule.declarations) {
        if (declaration.property == primitive_property && declaration.name == primitive) return true;
    }
    return false;
}

// The features one sample draws, read from the theme it writes: the
// materials of the button recipe it draws (at rest and hot), and its rules.
std::vector<std::string> sample_features(const EngineFeatureSample& sample) {
    std::vector<std::string> keys{};
    const std::unique_ptr<ThemeDefinition> held = sample_definition(sample, ImageId{});
    const ThemeDefinition& definition = *held;
    const ControlRoleRecipes& button = definition.roles[static_cast<std::size_t>(ControlVisualRole::button)];
    if (sample.primitive == "material") {
        add_material_features(keys, button.ordinary[static_cast<std::size_t>(ControlSurfaceState::normal)].material);
        add_material_features(keys, button.ordinary[static_cast<std::size_t>(ControlSurfaceState::hot)].material);
    }
    for (const ThemeRule& rule : definition.rules.rules) {
        if (declares_primitive(rule, "material")) {
            add_key(keys, "shape." + std::string(declared_choice(rule, "shape", "rounded_rectangle")));
        }
        // A part no blend declaration reaches is drawn normal.
        add_key(keys, "part_blend." + std::string(declared_choice(rule, blend_property, "normal")));
        if (declares_primitive(rule, "glass") && declared_choice(rule, "recipe", "none") != "none") {
            add_key(keys, "glass.colour_filter");
        }
        if (declares_primitive(rule, "sheen") && sample.drive == FeatureSampleDrive::pointer) {
            add_key(keys, "sheen.pointer");
        }
        for (const PropertyTransition& transition : rule.transitions) add_transition_features(keys, transition);
    }
    return keys;
}

void census() {
    const std::span<const EngineFeatureSample> samples = engine_samples::builtin_feature_samples();
    std::vector<std::vector<std::string>> drawn{};
    for (const EngineFeatureSample& sample : samples) drawn.push_back(sample_features(sample));
    const std::vector<std::string> required = required_features();
    for (const std::string& feature : required) {
        std::string first{};
        for (std::size_t index = 0U; index < samples.size() && first.empty(); ++index) {
            for (const std::string& key : drawn[index]) {
                if (key == feature) first = std::string(samples[index].name);
            }
        }
        require(!first.empty(), "the feature " + feature + " is drawn by a feature sample");
        manifest_lines.push_back("feature\t" + feature + '\t' + first);
    }
}

// -- the ribbon's rise: inputs, then rest --------------------------------------

struct RibbonFixture final {
    ManualTime time{};
    std::shared_ptr<Command> paste = std::make_shared<Command>("paste", "Paste");
    std::shared_ptr<Ribbon> ribbon;
    std::unique_ptr<Window> window;
    GatePainter painter{};
};

std::unique_ptr<RibbonFixture> make_ribbon(bool reduced) {
    std::unique_ptr<RibbonFixture> fixture = std::make_unique<RibbonFixture>();
    RibbonFixture& made = *fixture;
    std::vector<RibbonTabSpec> tabs{};
    RibbonTabSpec home{.stable_id = "home", .text = "&Home"};
    RibbonGroupSpec clipboard{.stable_id = "clipboard", .caption = "Clipboard"};
    clipboard.commands.push_back({.stable_id = "paste", .command = made.paste});
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

Point paste_centre(const Ribbon& ribbon) {
    const std::vector<Ribbon::CommandItem> items = ribbon.realized_commands();
    for (const Ribbon::CommandItem& item : items) {
        if (item.stable_id != "paste") continue;
        const Rect bounds = item.parts.command_face.bounds;
        const Point centre{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
        return centre;
    }
    require(false, "the paste command is realized");
    return {};
}

void ribbon_rests_after_motion() {
    std::unique_ptr<RibbonFixture> fixture = make_ribbon(false);
    RibbonFixture& made = *fixture;
    Window& window = *made.window;
    const MetricsSnapshot start = window.metrics_snapshot();
    // Inputs: onto the paste face (it rises), away (it falls), on again and
    // away again before either settles.
    move_to(window, paste_centre(*made.ribbon));
    for (int frame = 0; frame < 10; ++frame) static_cast<void>(run_frame(window, made.time, made.painter));
    move_to(window, Point{590.0, 60.0});
    for (int frame = 0; frame < 5; ++frame) static_cast<void>(run_frame(window, made.time, made.painter));
    move_to(window, paste_centre(*made.ribbon));
    for (int frame = 0; frame < 5; ++frame) static_cast<void>(run_frame(window, made.time, made.painter));
    move_to(window, Point{590.0, 60.0});
    const MetricsSnapshot moved = window.metrics_snapshot();
    require(moved.motion_clock_ticks > start.motion_clock_ticks, "the ribbon's rise ran motion frames");
    const IdleWindow idle = settle_then_watch(window, made.time, made.painter, "the ribbon");
    require(window.metrics_snapshot().motion_values_moving == 0U && !window.motion_clock().armed(),
            "every motion value of the ribbon has settled");
    require(silent(idle), "once the ribbon has settled, the window runs no frame for two seconds");
    add_counter("ribbon.motion_clock_ticks_while_moving", moved.motion_clock_ticks - start.motion_clock_ticks);
    add_idle("ribbon", idle);
}

void ribbon_without_motion() {
    std::unique_ptr<RibbonFixture> fixture = make_ribbon(true);
    RibbonFixture& made = *fixture;
    Window& window = *made.window;
    const MetricsSnapshot start = window.metrics_snapshot();
    move_to(window, paste_centre(*made.ribbon));
    for (int frame = 0; frame < 10; ++frame) static_cast<void>(run_frame(window, made.time, made.painter));
    const MetricsSnapshot after = window.metrics_snapshot();
    require(after.motion_clock_ticks == start.motion_clock_ticks,
            "under reduced motion, hovering the ribbon runs no motion frame");
    require(after.motion_values_finished > start.motion_values_finished,
            "the rise jumps to its end instead");
    add_counter("reduced_motion.ribbon.motion_clock_ticks", after.motion_clock_ticks - start.motion_clock_ticks);
    add_counter("reduced_motion.ribbon.values_jumped", after.motion_values_finished - start.motion_values_finished);
}

void write_manifest() {
    const char* const path = std::getenv("GUI_FORMS_ENGINE_GATE_MANIFEST");
    if (path == nullptr || *path == '\0') return;
    std::FILE* const file = std::fopen(path, "wb");
    require(file != nullptr, std::string("the manifest can be written: ") + path);
    std::string text = "# gui_forms engine gate manifest v1\n";
    text += "# primitive\tname\tstate\tscale\trasters\tpainter_calls\trefusals\thash\n";
    text += "# sample\tname\tprimitive\tframe\tscale\trasters\tpainter_calls\trefusals\thash\n";
    text += "# feature\tname\tfirst sample drawing it\n";
    for (const std::string& line : manifest_lines) text += line + '\n';
    const std::size_t written = std::fwrite(text.data(), 1U, text.size(), file);
    const int closed = std::fclose(file);
    require(written == text.size() && closed == 0, std::string("the manifest is written whole: ") + path);
}

// -- the specimen sheet (GUI_FORMS_ENGINE_GATE_SPECIMEN=<directory>) -----------
//
// With the variable set, the test also writes the owner's specimen sheet's
// images: every primitive at rest at scales 1 and 2, every feature sample's
// images at scales 1 and 2, a finish sweep and the starting tokens, each on
// the light and the dark backplane, as PNGs, and specimen.js listing them
// with their hashes (the gate's own for the light primitive and sample
// tiles). It draws through the same windows as the gate, and composites
// with the engine's one blend (detail::composite_part_raster), as the
// hosts' painters do. Text, box shadows and lines are not drawn: no sample
// draws them, and the built-in recipe's are the hosts' own.

// A painter compositing into premultiplied BGRA pixels at the window's
// scale: part rasters and paint groups through the engine's blend, solid
// fills by premultiplied source-over, clips as rectangles (rounded where
// asked).
class SpecimenPainter final : public Painter {
public:
    SpecimenPainter(std::uint32_t width, std::uint32_t height, double scale) : width_(width), height_(height),
                                                                               scale_(scale) {
        Layer base{};
        base.pixels.assign(static_cast<std::size_t>(width) * height, 0U);
        layers_.push_back(std::move(base));
        State first{};
        first.clip = Rect{0.0, 0.0, static_cast<double>(width), static_cast<double>(height)};
        states_.push_back(first);
    }
    void save() override { states_.push_back(states_.back()); }
    void restore() override {
        if (states_.size() > 1U) states_.pop_back();
    }
    void translate(Point offset) override {
        State& state = states_.back();
        state.offset = Point{state.offset.x + offset.x, state.offset.y + offset.y};
    }
    void clip_rect(Rect rect) override { narrow(rect, false, 0.0); }
    void clip_rounded_rect(Rect rect, double radius) override { narrow(rect, true, radius); }
    void begin_group(const PaintGroup& group) override {
        save();
        clip_rect(group.bounds);
        Layer layer{};
        layer.pixels.assign(static_cast<std::size_t>(width_) * height_, 0U);
        layer.opacity = group.opacity;
        layer.composite = group.composite;
        layer.clip = states_.back().clip;
        layers_.push_back(std::move(layer));
    }
    void end_group() override {
        if (layers_.size() < 2U) return;
        Layer finished = std::move(layers_.back());
        layers_.pop_back();
        detail::PartRasterSource source{};
        source.pixels = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(finished.pixels.data()),
                                                      finished.pixels.size() * 4U);
        source.width = width_;
        source.height = height_;
        source.row_bytes = static_cast<std::size_t>(width_) * 4U;
        source.blend = detail::part_composite_blend(finished.composite);
        detail::TextCompositeClip clip{};
        clip.rect = finished.clip;
        static_cast<void>(detail::composite_part_raster(source, target(), clip,
                                                        detail::part_composite_opacity(finished.opacity)));
        restore();
    }
    void fill_rect(Rect rect, Color color) override {
        const Rect device = Rect::intersection(to_device(rect), states_.back().clip);
        if (device.empty()) return;
        const std::uint32_t alpha = color.alpha;
        const std::uint32_t red = (color.red * alpha + 127U) / 255U;
        const std::uint32_t green = (color.green * alpha + 127U) / 255U;
        const std::uint32_t blue = (color.blue * alpha + 127U) / 255U;
        const std::uint32_t left = static_cast<std::uint32_t>(pixel_edge(device.x));
        const std::uint32_t top = static_cast<std::uint32_t>(pixel_edge(device.y));
        const std::uint32_t right = static_cast<std::uint32_t>(pixel_edge(device.x + device.width));
        const std::uint32_t bottom = static_cast<std::uint32_t>(pixel_edge(device.y + device.height));
        std::vector<std::uint32_t>& pixels = layers_.back().pixels;
        for (std::uint32_t row = top; row < bottom && row < height_; ++row) {
            for (std::uint32_t column = left; column < right && column < width_; ++column) {
                std::uint32_t& pixel = pixels[static_cast<std::size_t>(row) * width_ + column];
                pixel = over(pixel, red, green, blue, alpha);
            }
        }
    }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_box_shadow(Rect, double, Point, double, double, Color) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double opacity) override {
        composite(*raster, Rect{origin.x, origin.y, 0.0, 0.0}, false, opacity);
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                 double opacity) override {
        composite(*raster, destination, true, opacity);
    }

    // The composited image: premultiplied BGRA bytes, row after row.
    [[nodiscard]] std::span<const std::byte> image_bytes() const& {
        const std::vector<std::uint32_t>& pixels = layers_.front().pixels;
        return std::as_bytes(std::span<const std::uint32_t>(pixels.data(), pixels.size()));
    }
    std::span<const std::byte> image_bytes() const&& = delete;

private:
    struct State final {
        Point offset{};
        // In device pixels.
        Rect clip{};
        bool rounded{};
        Rect rounded_rect{};
        double radius{};
    };
    struct Layer final {
        std::vector<std::uint32_t> pixels{};
        double opacity{1.0};
        PaintGroupComposite composite{PaintGroupComposite::source_over};
        Rect clip{};
    };

    // A fill's edge on the device pixel boundary nearest it, first snapped
    // to 1/256 of a pixel, so that two slices meeting at one logical edge
    // (a gradient drawn as slices) meet on one pixel boundary whatever
    // rounding each edge's arithmetic carried.
    static long pixel_edge(double position) {
        const double snapped = std::round(position * 256.0) / 256.0;
        return std::lround(snapped);
    }
    static std::uint32_t over(std::uint32_t pixel, std::uint32_t red, std::uint32_t green, std::uint32_t blue,
                              std::uint32_t alpha) {
        // BGRA in memory: blue is the word's low byte, red its third.
        const std::uint32_t keep = 255U - alpha;
        const std::uint32_t b = blue + ((pixel & 0xFFU) * keep + 127U) / 255U;
        const std::uint32_t g = green + (((pixel >> 8U) & 0xFFU) * keep + 127U) / 255U;
        const std::uint32_t r = red + (((pixel >> 16U) & 0xFFU) * keep + 127U) / 255U;
        const std::uint32_t a = alpha + ((pixel >> 24U) * keep + 127U) / 255U;
        const std::uint32_t joined = std::min(b, 255U) | (std::min(g, 255U) << 8U) | (std::min(r, 255U) << 16U) |
                                     (std::min(a, 255U) << 24U);
        return joined;
    }
    Rect to_device(Rect rect) const {
        const State& state = states_.back();
        const Rect device{(rect.x + state.offset.x) * scale_, (rect.y + state.offset.y) * scale_,
                          rect.width * scale_, rect.height * scale_};
        return device;
    }
    void narrow(Rect rect, bool rounded, double radius) {
        State& state = states_.back();
        const Rect device = to_device(rect);
        state.clip = Rect::intersection(state.clip, device);
        if (!rounded) return;
        state.rounded = true;
        state.rounded_rect = device;
        state.radius = radius * scale_;
    }
    detail::TextCompositeTarget target() {
        detail::TextCompositeTarget made{};
        std::vector<std::uint32_t>& pixels = layers_.back().pixels;
        made.pixels = std::span<std::uint32_t>(pixels.data(), pixels.size());
        made.width = width_;
        made.height = height_;
        made.stride = width_;
        return made;
    }
    void composite(const PartRaster& raster, Rect destination, bool scaled, double opacity) {
        // A raster made for another scale is not drawn, as on the hosts.
        if (raster.scale().value != scale_) return;
        const DeviceExtent extent = raster.extent();
        const State& state = states_.back();
        // Corners on the device pixels nearest them (SkiaRaster).
        const long long left = std::llround((destination.x + state.offset.x) * scale_);
        const long long top = std::llround((destination.y + state.offset.y) * scale_);
        const long long right = std::llround((destination.x + destination.width + state.offset.x) * scale_);
        const long long bottom = std::llround((destination.y + destination.height + state.offset.y) * scale_);
        if (scaled && (right <= left || bottom <= top)) return;
        detail::PartRasterSource source{};
        source.pixels = raster.pixels();
        source.width = extent.width;
        source.height = extent.height;
        source.row_bytes = raster.row_bytes();
        source.left = left;
        source.top = top;
        if (scaled) {
            source.shown_width = static_cast<std::uint32_t>(right - left);
            source.shown_height = static_cast<std::uint32_t>(bottom - top);
        }
        detail::TextCompositeClip clip{};
        clip.rect = state.clip;
        clip.rounded = state.rounded;
        clip.rounded_rect = state.rounded_rect;
        clip.radius = state.radius;
        static_cast<void>(detail::composite_part_raster(source, target(), clip,
                                                        detail::part_composite_opacity(opacity)));
    }

    std::uint32_t width_;
    std::uint32_t height_;
    double scale_;
    std::vector<Layer> layers_{};
    std::vector<State> states_{};
};

// Where the specimen is written, and its listing (specimen.js entries).
struct SpecimenSink final {
    std::string directory{};
    std::vector<std::string> entries{};
};

std::string json_text(std::string_view text) {
    std::string quoted = "\"";
    for (const char character : text) {
        if (character == '"' || character == '\\') quoted += '\\';
        quoted += character;
    }
    quoted += '"';
    return quoted;
}

std::string backplane_name(Backplane backplane) {
    return backplane == Backplane::light ? "light" : "dark";
}

// Paints the whole window through a SpecimenPainter, writes it as
// <group>-<name>-<frame>-<scale>-<backplane>.png, and lists it.
void write_tile(SpecimenSink& sink, Window& window, const std::string& group, const std::string& name,
                const std::string& frame, double scale, Backplane backplane, std::uint64_t hash) {
    const std::uint32_t width = static_cast<std::uint32_t>(std::lround(sample_window_area.width * scale));
    const std::uint32_t height = static_cast<std::uint32_t>(std::lround(sample_window_area.height * scale));
    SpecimenPainter painter(width, height, scale);
    static_cast<void>(window.paint(painter, sample_window_area));
    image::PngEncodeInput input{};
    input.width = width;
    input.height = height;
    input.row_bytes = static_cast<std::uint64_t>(width) * 4U;
    input.pixels = painter.image_bytes();
    input.order = image::PngChannelOrder::bgra;
    input.alpha = image::PngSourceAlpha::premultiplied;
    std::vector<std::byte> encoded{};
    require(image::encode_png(input, encoded), name + ": the specimen tile encodes");
    std::ostringstream scale_text;
    scale_text << std::fixed << std::setprecision(0) << scale;
    const std::string file = group + '-' + name + '-' + frame + '-' + scale_text.str() + '-' +
                             backplane_name(backplane) + ".png";
    const std::string path = sink.directory + '/' + file;
    std::FILE* const out = std::fopen(path.c_str(), "wb");
    require(out != nullptr, "the specimen tile can be written: " + path);
    const std::size_t written = std::fwrite(encoded.data(), 1U, encoded.size(), out);
    const int closed = std::fclose(out);
    require(written == encoded.size() && closed == 0, "the specimen tile is written whole: " + path);
    std::ostringstream entry;
    entry << "{\"group\":" << json_text(group) << ",\"name\":" << json_text(name) << ",\"frame\":"
          << json_text(frame) << ",\"scale\":" << scale_text.str() << ",\"backplane\":"
          << json_text(backplane_name(backplane)) << ",\"file\":" << json_text(file) << ",\"hash\":"
          << json_text(hex(hash)) << '}';
    sink.entries.push_back(entry.str());
}

constexpr std::array<double, 2> specimen_scales{1.0, 2.0};
constexpr std::array<Backplane, 2> specimen_backplanes{Backplane::light, Backplane::dark};

// Every primitive at rest, as the gate draws it; the label is the gate's
// hash (the part rasters composited).
void specimen_primitives(SpecimenSink& sink) {
    for (const EnginePrimitive* const primitive : builtin_primitive_registry().primitives()) {
        const std::string name((*primitive).descriptor().name);
        for (const double scale : specimen_scales) {
            for (const Backplane backplane : specimen_backplanes) {
                std::unique_ptr<SubjectFixture> fixture = make_subject(*primitive, scale, AnatomyStateSet{}, backplane);
                SubjectFixture& made = *fixture;
                const std::uint64_t hash = made.painter.hash.value;
                settle(*made.window, made.time, made.painter, name + " specimen");
                write_tile(sink, *made.window, "primitive", name, "rest", scale, backplane, hash);
            }
        }
    }
}

// Every feature sample's images, as the gate draws them.
void specimen_samples(SpecimenSink& sink) {
    for (const EngineFeatureSample& sample : engine_samples::builtin_feature_samples()) {
        for (const double scale : specimen_scales) {
            for (const Backplane backplane : specimen_backplanes) {
                std::unique_ptr<SampleFixture> fixture = make_sample(sample, scale, backplane);
                SpecimenTarget target{&sink, scale, backplane, std::string(sample.name)};
                static_cast<void>(drive_sample(*fixture, sample, std::string(sample.name) + " specimen", &target));
            }
        }
    }
}

// A window drawing `material` on the subject's face with the material
// primitive, at `scale`; settled.
std::unique_ptr<SampleFixture> material_window(const SurfaceMaterial& material, double scale, Backplane backplane,
                                               const std::vector<PropertyTransition>& transitions) {
    std::unique_ptr<ThemeDefinition> held(new ThemeDefinition(windows_professional_theme_definition()));
    ThemeDefinition& definition = *held;
    definition.id = "engine-gate-specimen";
    use_backplane(definition, backplane);
    ControlRoleRecipes& button = definition.roles[static_cast<std::size_t>(ControlVisualRole::button)];
    for (ControlVisualRecipe& recipe : button.ordinary) recipe.material = material;
    for (ControlVisualRecipe& recipe : button.selected) recipe.material = material;
    ThemeRule rule{};
    rule.label = "specimen";
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = std::string(engine_samples::feature_sample_subject);
    rule.selector.role = PartRole::face;
    rule.declarations.push_back(RuleDeclaration::primitive("material"));
    rule.declarations.push_back(RuleDeclaration::choice_value("recipe", "button"));
    rule.declarations.push_back(RuleDeclaration::choice_value("shape", "rounded_rectangle"));
    rule.transitions = transitions;
    definition.rules.rules.push_back(rule);
    std::unique_ptr<SampleFixture> fixture = shell_window(PartRole::face, scale);
    SampleFixture& made = *fixture;
    (*made.window).set_theme(Theme::create(definition));
    require((*made.window).theme().engine().rules().diagnostics().empty(), "the specimen theme compiles cleanly");
    first_paint(made);
    settle(*made.window, made.time, made.painter, "specimen");
    return fixture;
}

std::uint64_t placed_hash(Window& window) {
    const SampleImage image = take_image(window, "rest", nullptr);
    return image.hash;
}

std::string amount_name(double value) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << value;
    return text.str();
}

// The finish sweep (E9.6, owner option A1): f = 0, 0.25, 0.5, 0.75 and 1 on
// three faces; at f the top half lifts finish_lift f, the break darkens
// finish_break_darkening f, the shine's alpha is finish_shine_alpha f.
void specimen_finish(SpecimenSink& sink) {
    struct Base final {
        const char* name;
        Color top;
        Color bottom;
    };
    const Base bases[] = {
        {"sky", Color::rgba(196, 220, 244), Color::rgba(92, 156, 222)},
        {"leaf", Color::rgba(176, 222, 168), Color::rgba(72, 148, 84)},
        {"graphite", Color::rgba(132, 138, 150), Color::rgba(52, 56, 66)},
    };
    const double sweep[] = {0.0, 0.25, 0.5, 0.75, 1.0};
    for (const Base& base : bases) {
        for (const double finish : sweep) {
            SurfaceMaterial material{};
            material.fills.clear();
            const std::vector<GradientStop> stops{GradientStop{0.0, base.top}, GradientStop{1.0, base.bottom}};
            material.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0}, stops));
            material.border = MaterialBorder{Color::rgba(40, 52, 66), 1.0};
            material.corner_radius = engine_samples::feature_sample_corner_radius;
            material.finish = finish;
            for (const Backplane backplane : specimen_backplanes) {
                std::unique_ptr<SampleFixture> fixture = material_window(material, 2.0, backplane, {});
                Window& window = *(*fixture).window;
                write_tile(sink, window, "finish", base.name, amount_name(finish), 2.0, backplane, placed_hash(window));
            }
        }
    }
}

// A transition moving `property` to `target` while the part is hot, and
// back to 0 at rest.
PropertyTransition hot_move(std::string_view property, double target, MotionCurve curve) {
    PropertyTransition transition{};
    transition.property = std::string(property);
    PresentationState hot{};
    hot.when.push_back(AnatomyState::hot);
    hot.target = target;
    PresentationState rest{};
    transition.states.push_back(hot);
    transition.states.push_back(rest);
    transition.curve = curve;
    return transition;
}

// Turns the subject hot and writes a tile after each of `frames` 16 ms
// frames (counted from the change), then once settled.
void specimen_strip(SpecimenSink& sink, SampleFixture& made, const std::string& name, Backplane backplane,
                    std::span<const std::uint32_t> frames) {
    Window& window = *made.window;
    write_tile(sink, window, "token", name, "t000", 2.0, backplane, placed_hash(window));
    (*made.subject).states.insert(AnatomyState::hot);
    (*made.subject).invalidate(Dirty::paint);
    const std::uint32_t last = frames.back();
    std::size_t next = 0U;
    for (std::uint32_t frame = 1U; frame <= last; ++frame) {
        static_cast<void>(run_frame(window, made.time, made.painter));
        if (next < frames.size() && frames[next] == frame) {
            write_tile(sink, window, "token", name, motion_frame_name(frame), 2.0, backplane, placed_hash(window));
            next = next + 1U;
        }
    }
}

// The starting tokens as specimens: the blur cap (reach 8: filter_blur up
// to 8 / 1.5), the 400 ms ceiling (a 600 ms move stops at 400), and the
// overshoot cap (a spring at 8 %).
void specimen_tokens(SpecimenSink& sink) {
    const double blurs[] = {1.0, 2.0, 4.0, SurfaceMaterial::maximum_filter_reach / 1.5};
    for (const double blur : blurs) {
        SurfaceMaterial material{};
        material.fills.clear();
        const std::vector<GradientStop> stops{GradientStop{0.0, Color::rgba(214, 92, 118)},
                                              GradientStop{0.5, Color::rgba(236, 184, 64)},
                                              GradientStop{1.0, Color::rgba(92, 156, 222)}};
        material.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{1.0, 0.0}, stops));
        material.corner_radius = engine_samples::feature_sample_corner_radius;
        material.filter_blur = blur;
        for (const Backplane backplane : specimen_backplanes) {
            std::unique_ptr<SampleFixture> fixture = material_window(material, 2.0, backplane, {});
            Window& window = *(*fixture).window;
            write_tile(sink, window, "token", "blur", amount_name(blur), 2.0, backplane, placed_hash(window));
        }
    }
    SurfaceMaterial face{};
    face.fills.clear();
    face.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0},
                                                   std::vector<GradientStop>{
                                                       GradientStop{0.0, Color::rgba(232, 238, 246)},
                                                       GradientStop{1.0, Color::rgba(92, 156, 222)}}));
    face.border = MaterialBorder{Color::rgba(40, 52, 66), 1.0};
    face.corner_radius = engine_samples::feature_sample_corner_radius;
    // 600 ms asked, 400 ms given. The move starts on the first frame after
    // the change (16 ms): frames at 96 to 384 ms, at rest by 416, and still
    // at rest at 592, where a 600 ms move would be moving yet.
    const std::uint32_t ceiling_frames[] = {6U, 12U, 18U, 24U, 26U, 37U};
    const std::uint32_t spring_frames[] = {2U, 4U, 6U, 8U, 10U, 12U, 16U, 20U};
    for (const Backplane backplane : specimen_backplanes) {
        const std::vector<PropertyTransition> long_move{
            hot_move(translate_x_property, 8.0, MotionCurve::timed(milliseconds(600), MotionEasing::linear))};
        std::unique_ptr<SampleFixture> ceiling = material_window(face, 2.0, backplane, long_move);
        specimen_strip(sink, *ceiling, "ceiling", backplane, ceiling_frames);
        const std::vector<PropertyTransition> springy{
            hot_move(translate_y_property, 6.0, MotionCurve::spring(milliseconds(160), maximum_motion_overshoot))};
        std::unique_ptr<SampleFixture> spring = material_window(face, 2.0, backplane, springy);
        specimen_strip(sink, *spring, "overshoot", backplane, spring_frames);
    }
}

// specimen.js: the tiles and the engine's constants the sheet labels.
void write_specimen_listing(const SpecimenSink& sink) {
    std::string text = "// Written by gui_forms_engine_gate_tests (GUI_FORMS_ENGINE_GATE_SPECIMEN).\n";
    text += "window.SPECIMEN = {\n  constants: {";
    std::ostringstream constants;
    constants << "\"finish_lift\":" << finish_lift << ",\"finish_break_darkening\":" << finish_break_darkening
              << ",\"finish_shine_alpha\":" << finish_shine_alpha
              << ",\"maximum_motion_overshoot\":" << maximum_motion_overshoot
              << ",\"maximum_filter_reach\":" << SurfaceMaterial::maximum_filter_reach
              << ",\"maximum_glow_reach\":" << SurfaceMaterial::maximum_glow_reach
              << ",\"maximum_ambient_frames_per_second\":" << maximum_ambient_frames_per_second
              << ",\"maximum_ambient_window_fraction\":" << maximum_ambient_window_fraction
              << ",\"motion_duration_ceiling_ms\":"
              << std::chrono::duration_cast<milliseconds>(motion_duration_ceiling).count();
    text += constants.str() + "},\n  tiles: [\n";
    for (std::size_t index = 0U; index < sink.entries.size(); ++index) {
        text += "    " + sink.entries[index] + (index + 1U < sink.entries.size() ? ",\n" : "\n");
    }
    text += "  ]\n};\n";
    const std::string path = sink.directory + "/specimen.js";
    std::FILE* const out = std::fopen(path.c_str(), "wb");
    require(out != nullptr, "the specimen listing can be written: " + path);
    const std::size_t written = std::fwrite(text.data(), 1U, text.size(), out);
    const int closed = std::fclose(out);
    require(written == text.size() && closed == 0, "the specimen listing is written whole: " + path);
}

void write_specimen() {
    const char* const directory = std::getenv("GUI_FORMS_ENGINE_GATE_SPECIMEN");
    if (directory == nullptr || *directory == '\0') return;
    SpecimenSink sink{};
    sink.directory = directory;
    specimen_primitives(sink);
    specimen_samples(sink);
    specimen_finish(sink);
    specimen_tokens(sink);
    write_specimen_listing(sink);
}

} // namespace

int main() {
    every_registered_primitive();
    every_feature_sample();
    census();
    ribbon_rests_after_motion();
    ribbon_without_motion();
    write_manifest();
    write_specimen();
    return 0;
}
