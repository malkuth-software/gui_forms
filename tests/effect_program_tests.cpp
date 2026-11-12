// Effect programs (design note desks/coordinator/EFFECT_PROGRAMS_DESIGN.md):
// the generator's known answers, bounded ranges, the registry's checks, the
// instance's fixed ticks (at most four a frame, the rest skipped and
// counted), setup once and again on a geometry change, still frames that
// advance nothing, the canvas's pixel meter and save depth, and the
// progress bar's five effects: what each draws, that the same seed and
// tick sequence give the same state bytes, and that the woven stripes'
// random arrangement follows its seed. Time comes from the test, never the
// wall clock. The verification records: with the gui_forms plugin's effect
// pass (GUI_FORMS_EFFECT_TEST_VERIFIED), each built-in effect is verified and
// its record's bounds fit its budgets; this file is compiled without the
// pass, so its own test effect is labelled unverified either way.

#include "gui_forms/controls/progress_bar_effects.hpp"
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/engine/effect_canvas/effect_canvas.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/gui_forms.hpp"
#include "support/professional_theme.hpp"
#include "support/theme_definition_heap.hpp"

#include "../src/core/text/localizable/gui_forms_notes.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_effect_program_tests: " << message << '\n';
    std::exit(1);
}

// Counts what an effect drew, and checks the painter's saves balance.
class RecordingPainter final : public Painter {
public:
    void save() override { saves = saves + 1; }
    void restore() override { restores = restores + 1; }
    void translate(Point offset) override { translations.push_back(offset); }
    void clip_rect(Rect bounds) override { clips.push_back(bounds); }
    void clip_rounded_rect_corners(Rect bounds, PaintCornerRadii radii) override {
        corner_boxes.push_back(bounds);
        corner_clips.push_back(radii);
    }
    void fill_rect(Rect, Color) override { rects = rects + 1; }
    void fill_rounded_rect(Rect, double, Color) override { rounded = rounded + 1; }
    void stroke_rect(Rect, Color, double) override { stroked = stroked + 1; }
    void stroke_rounded_rect(Rect, double, Color, double) override { circles = circles + 1; }
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop> stops) override {
        linear = linear + 1;
        last_stops = stops.size();
    }
    void fill_linear_gradient_spread(Rect, Point, Point, std::span<const GradientStop>, GradientSpreadMode) override {
        repeated = repeated + 1;
    }
    void fill_radial_gradient(Rect, Point center, Size, std::span<const GradientStop>) override {
        radial = radial + 1;
        radial_centers.push_back(center);
    }
    void draw_line(Point from, Point to, Color, double) override {
        lines = lines + 1;
        line_starts.push_back(from);
        static_cast<void>(to);
    }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double) override {
        part_rasters.push_back(raster);
        part_origins.push_back(origin);
    }
    void stroke_polyline(std::span<const Point> points, const StrokeStyle& style) override {
        polylines = polylines + 1;
        last_batch = points.size();
        last_dash_phase = style.dash_phase;
    }
    void fill_polygon(std::span<const Point> points, Color, PolygonFillRule) override {
        polygons = polygons + 1;
        last_batch = points.size();
        if (!points.empty()) polygon_starts.push_back(points[0]);
    }
    void fill_band(std::span<const Point> upper, std::span<const Point>, Color) override {
        bands = bands + 1;
        last_batch = upper.size();
    }
    void fill_rects(std::span<const Rect> batch, Color) override {
        rect_batches = rect_batches + 1;
        last_batch = batch.size();
    }
    void stroke_segments(std::span<const PaintSegment> segments, const StrokeStyle&) override {
        segment_batches = segment_batches + 1;
        last_batch = segments.size();
    }
    void draw_markers(std::span<const Point> centers, const MarkerStyle&) override {
        marker_batches = marker_batches + 1;
        last_batch = centers.size();
    }
    void fill_conic_gradient(Rect, Point, double, std::span<const GradientStop> stops) override {
        conics = conics + 1;
        last_stops = stops.size();
    }
    void fill_checker(Rect, const CheckerPattern&) override { checkers = checkers + 1; }

    int saves{};
    int restores{};
    int rects{};
    int rounded{};
    int stroked{};
    int circles{};
    int linear{};
    int repeated{};
    int radial{};
    int lines{};
    int polylines{};
    int polygons{};
    int bands{};
    int rect_batches{};
    int segment_batches{};
    int marker_batches{};
    int conics{};
    int checkers{};
    std::size_t last_batch{};
    double last_dash_phase{};
    std::size_t last_stops{};
    std::vector<Point> translations{};
    std::vector<Rect> clips{};
    std::vector<Point> radial_centers{};
    std::vector<Point> line_starts{};
    std::vector<Point> polygon_starts{};
    std::vector<Rect> corner_boxes{};
    std::vector<PaintCornerRadii> corner_clips{};
    std::vector<std::shared_ptr<const PartRaster>> part_rasters{};
    std::vector<Point> part_origins{};
};

const FrameTime start{std::chrono::seconds(1000)};

FrameTime at_milliseconds(std::int64_t milliseconds) {
    const FrameTime time = start + std::chrono::milliseconds(milliseconds);
    return time;
}

EffectView<ProgressBar> bar_view(double fraction) {
    EffectView<ProgressBar> view{};
    view.fraction = fraction;
    view.interior = Rect{0.0, 0.0, 196.0, 16.0};
    view.fill = Rect{0.0, 0.0, 196.0 * fraction, 16.0};
    view.stripe_width = 7.0;
    view.period_seconds = 1.4;
    view.still_phase = 0.5;
    // The marching stripes as the bar resolves them by default: inside the
    // fill's one-unit rim, the pitch the interior's thickness, the pale band
    // 0.4 of it, leaning 45 degrees back, 0.8 pitches a second forward.
    view.stripes.clip = Rect{1.0, 1.0, 196.0 * fraction - 2.0, 14.0};
    view.stripes.pitch = 16.0;
    view.stripes.band = 6.4;
    view.stripes.slant = 14.0;
    view.stripes.forward = true;
    view.stripes.direction = ProgressFillDirection::left_to_right;
    view.stripes.colour = Color::rgba(224, 241, 252, 116);
    view.stripes.period_seconds = 1.25;
    return view;
}

const EffectEntry& built_in(std::string_view name) {
    const EffectEntry* const entry = built_in_effect_registry().find(name);
    require(entry != nullptr, "a built-in effect is registered");
    return *entry;
}

// A test effect that draws one rectangle over its whole part.
class FillPart final {
public:
    using parent_type = ProgressBar;
    static constexpr EffectDeclaration declaration{
        .name = "test_fill_part",
        .cost = {.fixed = 100U, .per_device_pixel = 1U},
        .state_budget = 8U,
    };
    void run(const EffectView<ProgressBar>&, const EffectFrame& frame, EffectCanvas& canvas) {
        canvas.fill_rect({0.0, 0.0, frame.part.width, frame.part.height}, Color::rgba(1, 2, 3, 255));
        canvas.fill_rect({0.0, 0.0, frame.part.width, frame.part.height}, Color::rgba(1, 2, 3, 255));
        ticks_ = ticks_ + frame.ticks;
    }
    std::uint64_t ticks_{};
};

void generator_known_answers() {
    // Bit-exact on every host: these are the generator's definition.
    static_assert(EffectRandom::hash(0U, 0U, 0U) == EffectRandom::hash(0U, 0U, 0U));
    constexpr std::uint64_t first = EffectRandom::hash(1U, 2U, 3U);
    constexpr std::uint64_t second = EffectRandom::hash(1U, 2U, 4U);
    require(first != second, "the counter changes the hash");
    EffectRandom random(1U, 2U);
    const std::uint64_t drawn = random.next();
    require(drawn == EffectRandom::hash(1U, 2U, 0U), "next draws the hash of the counter, from 0");
    for (int index = 0; index < 1000; ++index) {
        const double unit = random.next_unit();
        require(unit >= 0.0 && unit < 1.0, "next_unit lies in [0, 1)");
        const std::uint32_t below = random.next_below(7U);
        require(below < 7U, "next_below stays below its bound");
    }
    require(random.next_below(0U) == 0U, "next_below(0) is 0");
    const char* const printing = std::getenv("GUI_FORMS_EFFECT_PRINT");
    if (printing != nullptr) std::cout << "hash(1,2,3) = " << first << '\n';
    require(first == 0x849572c09694a2e1ULL || printing != nullptr, "the hash's known answer");
    require(effect_default_seed("bar.one", "progress_bubbles") != effect_default_seed("bar.two", "progress_bubbles"),
            "the default seed follows the stable ID");
    require(effect_fraction(2.25) == 0.25 && effect_fraction(-0.25) == 0.75, "effect_fraction");
}

void bounded_ranges() {
    std::size_t passes = 0U;
    for (const std::size_t index : bounded_range<4U>(9U)) passes = passes + 1U + index * 0U;
    require(passes == 4U, "a bounded range clamps its count to its capacity");
    passes = 0U;
    for (const std::size_t index : bounded_range<4U>(2U)) passes = passes + 1U + index * 0U;
    require(passes == 2U, "a bounded range below capacity runs its count");
}

void registry_checks() {
    const EffectRegistry& registry = built_in_effect_registry();
    require(registry.count() == 6U, "six built-in effects: the progress bar's five and the sweep scope's");
    require(built_in("scope_sweep").parent == "sweep_scope", "the sweep scope's effect names its parent");
    const std::array<std::string_view, 5> names{"progress_stripes", "progress_pulse", "progress_laser",
                                                 "progress_woven_stripes", "progress_bubbles"};
    for (const std::string_view name : names) {
        const EffectEntry& entry = built_in(name);
        require(entry.parent == "progress_bar", "a progress bar effect names its parent");
    }
    require(built_in("progress_woven_stripes").setup != nullptr, "the woven stripes have setup");
    require(built_in("progress_stripes").setup == nullptr, "the stripes have none");
    EffectRegistry copy = registry;
    require(copy.add(built_in("progress_pulse")) == EffectRegistrationOutcome::duplicate_name,
            "a duplicate name is refused");
    EffectDeclaration bad = FillPart::declaration;
    bad.name = "Not_Snake";
    EffectEntry wrong = make_effect_entry<FillPart>();
    wrong.declaration = &bad;
    require(copy.add(wrong) == EffectRegistrationOutcome::name_not_snake_case, "a name not in snake_case");
    // This file is compiled without the effect pass: its effect has no
    // record and is labelled unverified, and counted.
    EffectRegistry local;
    require(local.add(make_effect_entry<FillPart>()) == EffectRegistrationOutcome::registered, "a test effect");
    require(!(*local.find("test_fill_part")).verified && local.unverified_count() == 1U,
            "an effect built without the effect pass is unverified");
}

// One effect's verification record, as the plugin's effect pass writes it
// (gui_forms_notes.hpp, effect_verification_note_type).
struct VerificationRecord final {
    std::string name;
    std::uint32_t bound{};
    std::uint32_t setup_bound{};
    std::uint32_t state_size{};
};

// Reads little-endian fields from one record table; `failed` once a read
// runs past its end.
class TableReader final {
public:
    explicit TableReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    std::uint32_t u32() {
        if (bytes_.size() - offset_ < 4U) {
            failed = true;
            return 0U;
        }
        std::uint32_t value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const std::uint32_t byte = std::to_integer<std::uint32_t>(bytes_[offset_ + index]);
            value = value | (byte << (8U * index));
        }
        offset_ = offset_ + 4U;
        return value;
    }

    std::string text() {
        const std::uint32_t size = u32();
        if (failed || bytes_.size() - offset_ < size) {
            failed = true;
            return std::string();
        }
        const std::string value(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
        offset_ = offset_ + size;
        return value;
    }

    bool failed{};

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

// Every verification record in the program.
std::vector<VerificationRecord> verification_records() {
    std::vector<VerificationRecord> records;
    const std::vector<std::vector<std::byte>> tables =
        gui_forms_note_descriptions(effect_verification_note_type, nullptr);
    for (const std::vector<std::byte>& table : tables) {
        TableReader reader(table);
        const std::uint32_t count = reader.u32();
        for (std::uint32_t index = 0U; index < count && !reader.failed; ++index) {
            VerificationRecord record;
            record.name = reader.text();
            record.bound = reader.u32();
            record.setup_bound = reader.u32();
            record.state_size = reader.u32();
            require(!reader.failed, "a verification record is well formed");
            records.push_back(record);
        }
    }
    return records;
}

void verification() {
    const EffectRegistry& registry = built_in_effect_registry();
    const std::vector<VerificationRecord> records = verification_records();
    const bool printing = std::getenv("GUI_FORMS_EFFECT_PRINT") != nullptr;
#if defined(GUI_FORMS_EFFECT_TEST_VERIFIED)
    require(registry.unverified_count() == 0U, "every built-in effect is verified");
    for (std::size_t index = 0U; index < registry.count(); ++index) {
        const EffectEntry& entry = registry.at(index);
        const EffectDeclaration& declaration = *entry.declaration;
        require(entry.verified, "a built-in effect carries its verification record");
        const VerificationRecord* found = nullptr;
        for (const VerificationRecord& record : records) {
            if (record.name == declaration.name) found = &record;
        }
        require(found != nullptr, "the record names the effect");
        const VerificationRecord& record = *found;
        require(record.bound > 0U && record.bound <= declaration.cost.fixed, "run's bound fits its fixed budget");
        require(record.setup_bound <= declaration.setup_cost, "setup's bound fits its setup budget");
        require((entry.setup != nullptr) == (record.setup_bound > 0U), "setup is bounded when there is one");
        require(record.state_size == entry.size, "the record's state size is sizeof the effect");
        if (printing) {
            std::cout << declaration.name << ": run bound " << record.bound << " of " << declaration.cost.fixed
                      << ", setup bound " << record.setup_bound << " of " << declaration.setup_cost << ", state "
                      << record.state_size << " of " << declaration.state_budget << " bytes\n";
        }
    }
#else
    require(registry.unverified_count() == registry.count(),
            "built without the effect pass, every effect is unverified");
    // The process counts every unverified registration (the built-in
    // registry's, and registry_checks' local ones), and windows report it.
    const EffectLoadDiagnostics diagnostics = effect_load_diagnostics();
    require(diagnostics.unverified >= registry.count() && diagnostics.registered >= diagnostics.unverified,
            "the load diagnostics count the unverified effects");
    require(records.empty(), "built without the effect pass, there are no records");
    static_cast<void>(printing);
#endif
}

void ticks_and_skips() {
    const EffectEntry entry = make_effect_entry<FillPart>();
    EffectInstance instance;
    instance.attach(entry, 7U, start);
    EffectAdvance advance = instance.advance(at_milliseconds(1000));
    require(advance.pending == 4U && advance.skipped == 56U, "a stall runs four ticks and skips the rest");
    RecordingPainter painter;
    const EffectView<ProgressBar> view = bar_view(0.5);
    instance.paint(painter, Rect{2.0, 2.0, 196.0, 16.0}, 1.0, false, view);
    require(instance.counters().ticks == 4U && instance.counters().skipped_ticks == 56U, "ticks run and skipped");
    advance = instance.advance(at_milliseconds(1050));
    require(advance.pending == 3U && advance.skipped == 0U, "ticks due since the last advance");
    instance.paint(painter, Rect{2.0, 2.0, 196.0, 16.0}, 1.0, true, view);
    require(instance.counters().ticks == 4U, "a still frame advances nothing");
    instance.paint(painter, Rect{2.0, 2.0, 196.0, 16.0}, 1.0, false, view);
    require(instance.counters().ticks == 7U, "pending ticks survive a still frame");
    instance.paint(painter, Rect{2.0, 2.0, 196.0, 16.0}, 1.0, false, view);
    require(instance.counters().ticks == 7U, "a repaint without an advance advances nothing");
    instance.rebase(at_milliseconds(9000));
    advance = instance.advance(at_milliseconds(9010));
    require(advance.pending == 0U && advance.skipped == 0U, "a rebase restarts time without catch-up");
    require(painter.saves == painter.restores, "the instance leaves the painter's saves balanced");
    require(!painter.translations.empty() && painter.translations.front() == Point{2.0, 2.0},
            "the canvas draws from the part's origin");
}

void meter_and_depth() {
    RecordingPainter painter;
    {
        EffectCanvas canvas(painter, Size{10.0, 10.0}, Insets{}, 2.0, 1U);
        canvas.fill_rect({0.0, 0.0, 10.0, 10.0}, Color::rgba(0, 0, 0, 255));
        canvas.fill_rect({0.0, 0.0, 10.0, 10.0}, Color::rgba(0, 0, 0, 255));
        require(canvas.meter().budget == 400.0 && canvas.meter().charged == 400.0, "the meter charges device pixels");
        require(canvas.meter().refused == 1U && painter.rects == 1, "a call past the budget is refused");
        for (int level = 0; level < 6; ++level) {
            canvas.save();
            canvas.clip_rect({1.0, 1.0, 8.0, 8.0});
        }
        require(canvas.meter().refused == 3U, "saves past the depth are refused");
        for (int level = 0; level < 6; ++level) canvas.restore();
    }
    require(painter.saves == painter.restores, "the canvas leaves the painter's saves balanced");
    RecordingPainter outside;
    {
        EffectCanvas canvas(outside, Size{10.0, 10.0}, Insets{}, 1.0, 4U);
        canvas.fill_rect({50.0, 50.0, 10.0, 10.0}, Color::rgba(0, 0, 0, 255));
        require(canvas.meter().charged == 0.0, "a call outside the clip charges nothing");
    }
}

void stripes_pulse_laser() {
    const EffectView<ProgressBar> view = bar_view(0.5);
    const Rect part{2.0, 2.0, 196.0, 16.0};
    RecordingPainter stripes;
    EffectInstance stripe_instance;
    stripe_instance.attach(built_in("progress_stripes"), 1U, start);
    stripe_instance.paint(stripes, part, 1.0, false, view);
    // Bands anchored to the track's start, every 16 from −32 (the first
    // whose far side, 6.4 + 14 on, could reach the clip at 1) to 96 (the
    // last starting before the clip's end at 97): nine four-point polygons.
    require(stripes.polygons == 9 && stripes.last_batch == 4U && stripes.lines == 0,
            "the stripes draw nine filled bands");
    require(stripe_instance.last_meter().refused == 0U, "the stripes stay within budget");
    // A quarter second at 0.8 pitches a second moves them 0.2 of 16 = 3.2
    // units forward, stepped to 3 whole device pixels. A frame runs at most
    // four ticks (the rest are skipped), so the quarter second goes by in
    // five 50 ms frames of three ticks each.
    RecordingPainter between;
    for (std::int64_t step = 1; step < 5; ++step) {
        static_cast<void>(stripe_instance.advance(at_milliseconds(step * 50)));
        stripe_instance.paint(between, part, 1.0, false, view);
    }
    static_cast<void>(stripe_instance.advance(at_milliseconds(250)));
    RecordingPainter moved;
    stripe_instance.paint(moved, part, 1.0, false, view);
    const bool travelled = !stripes.polygon_starts.empty() && !moved.polygon_starts.empty() &&
                           moved.polygon_starts[0].x - stripes.polygon_starts[0].x == 3.0;
    if (!travelled && !stripes.polygon_starts.empty() && !moved.polygon_starts.empty()) {
        std::cerr << "  stripes: first band at " << stripes.polygon_starts[0].x << ", then "
                  << moved.polygon_starts[0].x << " (" << stripes.polygons << " and " << moved.polygons
                  << " bands)\n";
    }
    require(travelled, "the stripes travel forward, toward the leading edge, in whole device pixels");
    // Mirrored for a right-to-left fill: the bands travel left.
    EffectView<ProgressBar> mirrored = view;
    mirrored.stripes.direction = ProgressFillDirection::right_to_left;
    mirrored.stripes.clip = Rect{99.0, 1.0, 96.0, 14.0};
    EffectInstance mirrored_instance;
    mirrored_instance.attach(built_in("progress_stripes"), 1U, start);
    RecordingPainter mirrored_first;
    mirrored_instance.paint(mirrored_first, part, 1.0, false, mirrored);
    for (std::int64_t step = 1; step < 5; ++step) {
        static_cast<void>(mirrored_instance.advance(at_milliseconds(step * 50)));
        mirrored_instance.paint(between, part, 1.0, false, mirrored);
    }
    static_cast<void>(mirrored_instance.advance(at_milliseconds(250)));
    RecordingPainter mirrored_moved;
    mirrored_instance.paint(mirrored_moved, part, 1.0, false, mirrored);
    const bool leftward = !mirrored_first.polygon_starts.empty() && !mirrored_moved.polygon_starts.empty() &&
                          mirrored_moved.polygon_starts[0].x - mirrored_first.polygon_starts[0].x == -3.0;
    require(leftward, "a right-to-left fill's stripes travel left");

    RecordingPainter pulse;
    EffectInstance pulse_instance;
    pulse_instance.attach(built_in("progress_pulse"), 1U, start);
    pulse_instance.paint(pulse, part, 1.0, false, view);
    require(pulse.linear == 1 && pulse.last_stops == 5U, "the pulse draws one five-stop band");

    RecordingPainter laser;
    EffectInstance laser_instance;
    laser_instance.attach(built_in("progress_laser"), 1U, start);
    laser_instance.paint(laser, part, 2.0, false, view);
    require(laser.repeated == 1 && laser.linear == 1, "the laser draws repeating bands and its edge");
    require(laser.radial == 5 && laser.lines == 8, "the laser draws its corona, four lobes, core and seven sparks");
    RecordingPainter still;
    laser_instance.paint(still, part, 2.0, true, view);
    require(still.radial == 3 && still.lines == 4, "a still laser draws the reduced front");
    require(laser_instance.last_meter().refused == 0U, "the laser stays within budget");
}

// Runs `name` over `frames` frames of 50 ms with `seed` at `fraction`, and
// returns its state checksum.
std::uint64_t run_frames(std::string_view name, std::uint64_t seed, double fraction, int frames) {
    EffectInstance instance;
    instance.attach(built_in(name), seed, start);
    const EffectView<ProgressBar> view = bar_view(fraction);
    RecordingPainter painter;
    for (int frame = 1; frame <= frames; ++frame) {
        static_cast<void>(instance.advance(at_milliseconds(frame * 50)));
        instance.paint(painter, Rect{2.0, 2.0, 196.0, 16.0}, 1.0, false, view);
    }
    return instance.state_checksum();
}

void woven_stripes() {
    const Rect part{2.0, 2.0, 196.0, 16.0};
    int previous = -1;
    for (int step = 0; step <= 10; ++step) {
        const double fraction = static_cast<double>(step) / 10.0;
        EffectInstance instance;
        instance.attach(built_in("progress_woven_stripes"), 11U, start);
        RecordingPainter painter;
        instance.paint(painter, part, 1.0, false, bar_view(fraction));
        if (step == 0) require(painter.lines == 0, "no stripe is woven in at 0");
        require(painter.lines >= previous, "stripes weave in as progress grows, and stay");
        previous = painter.lines;
    }
    // 196 ÷ (7 × 1.5) = 18 stripes, all in by the end.
    require(previous == 18, "every stripe is woven in at 1");
    require(run_frames("progress_woven_stripes", 11U, 0.5, 4) == run_frames("progress_woven_stripes", 11U, 0.5, 4),
            "the same seed gives the same arrangement");
    require(run_frames("progress_woven_stripes", 11U, 0.5, 4) != run_frames("progress_woven_stripes", 12U, 0.5, 4),
            "another seed gives another arrangement");
    EffectInstance resized;
    resized.attach(built_in("progress_woven_stripes"), 11U, start);
    RecordingPainter painter;
    resized.paint(painter, part, 1.0, false, bar_view(1.0));
    resized.paint(painter, part, 1.0, false, bar_view(1.0));
    require(resized.counters().setups == 1U, "setup runs once for one geometry");
    resized.paint(painter, Rect{2.0, 2.0, 300.0, 16.0}, 1.0, false, bar_view(1.0));
    require(resized.counters().setups == 2U, "setup runs again on a geometry change, as declared");
}

void bubbles_and_steam() {
    EffectInstance instance;
    instance.attach(built_in("progress_bubbles"), effect_default_seed("bar.bubbles", "progress_bubbles"), start);
    const EffectView<ProgressBar> view = bar_view(1.0);
    RecordingPainter painter;
    bool steam_above = false;
    for (int frame = 1; frame <= 120; ++frame) {
        static_cast<void>(instance.advance(at_milliseconds(frame * 33)));
        painter.radial_centers.clear();
        instance.paint(painter, Rect{2.0, 2.0, 196.0, 16.0}, 1.0, false, view);
        for (const Point center : painter.radial_centers) {
            if (center.y < 0.0) steam_above = true;
        }
        require(instance.last_meter().refused == 0U, "the bubbles stay within budget");
    }
    require(painter.circles > 0, "bubbles are drawn");
    require(steam_above, "steam rises into the top outset");
    require(painter.clips.front() == Rect{2.0, 2.0 - ProgressBubbles::steam_outset, 196.0,
                                          16.0 + ProgressBubbles::steam_outset},
            "the layer is clipped to the part and its outsets");
    const std::uint64_t first = run_frames("progress_bubbles", 5U, 0.8, 60);
    require(first == run_frames("progress_bubbles", 5U, 0.8, 60), "the same ticks give the same state bytes");
    require(first != run_frames("progress_bubbles", 6U, 0.8, 60), "another seed gives other bubbles");
    const char* const printing = std::getenv("GUI_FORMS_EFFECT_PRINT");
    if (printing != nullptr) std::cout << "bubbles checksum = " << first << '\n';
    // The motion checksum: the same bits on every host (no FMA, no host
    // libm, the engine's own sine).
    require(first == 0x629f307d0b039ac5ULL || printing != nullptr, "the bubbles' motion checksum");
}

} // namespace

// The bar hosts its effects: the style's own by default, another by name,
// none for a name no progress-bar effect has; the seed follows the stable
// ID unless the application sets one; steam widens the bar's outsets.
std::uint64_t hosted_checksum(const char* id, std::optional<std::uint64_t> seed) {
    std::shared_ptr<ProgressBar> bar = make_control<ProgressBar>(StableId(std::string_view(id)));
    (*bar).set_requested_bounds({0.0, 0.0, 200.0, 20.0});
    (*bar).value = 70.0;
    (*bar).effect_seed = seed;
    (*bar).effect = "progress_woven_stripes";
    Window window(bar, {200.0, 40.0});
    window.perform_layout();
    RecordingPainter painter;
    (*bar).on_paint(painter, {0.0, 0.0, 200.0, 20.0});
    const std::uint64_t checksum = (*bar).effect_instance().state_checksum();
    return checksum;
}

void progress_bar_hosting() {
    std::shared_ptr<ProgressBar> bar = make_control<ProgressBar>(StableId("effects.hosted"));
    (*bar).set_requested_bounds({0.0, 0.0, 200.0, 20.0});
    (*bar).value = 50.0;
    Window window(bar, {200.0, 40.0});
    window.perform_layout();
    require((*bar).effect_instance().entry() == nullptr && !window.next_wake().has_value(),
            "a continuous bar hosts no effect and asks for no frames");
    (*bar).visual_style = ProgressBarVisualStyle::laser_etch;
    require((*bar).effect_instance().entry() == &built_in("progress_laser") && !window.next_wake().has_value(),
            "the laser style hosts the laser effect, and asks for no frame before it is drawn");
    RecordingPainter drawn;
    (*bar).on_paint(drawn, {0.0, 0.0, 200.0, 20.0});
    require(window.next_wake().has_value(), "the drawn laser animates");
    (*bar).effect = "progress_bubbles";
    require((*bar).effect_instance().entry() == &built_in("progress_bubbles") &&
                (*bar).visual_outsets().top == ProgressBubbles::steam_outset,
            "a named effect replaces the style's, and its steam widens the outsets");
    const FrameTime origin = FrameClock::now();
    for (int frame = 1; frame <= 30; ++frame) {
        (*bar).advance_animation(origin + std::chrono::milliseconds(frame * 50));
        RecordingPainter painter;
        (*bar).on_paint(painter, {0.0, 0.0, 200.0, 20.0});
        require(painter.saves == painter.restores, "the bar's effect leaves the painter balanced");
    }
    require((*bar).effect_instance().counters().frames == 30U && (*bar).effect_instance().counters().ticks > 0U,
            "the bar advances and draws its effect");
    (*bar).motion_policy = MotionPolicy{true, false, true};
    const std::uint64_t ticks = (*bar).effect_instance().counters().ticks;
    (*bar).advance_animation(origin + std::chrono::milliseconds(1600));
    RecordingPainter reduced;
    (*bar).on_paint(reduced, {0.0, 0.0, 200.0, 20.0});
    require((*bar).effect_instance().counters().ticks == ticks, "reduced motion draws the still pose");
    (*bar).effect = "no_such_effect";
    require((*bar).effect_instance().entry() == nullptr && (*bar).visual_outsets() == Insets{},
            "an unknown name draws no effect");
    (*bar).effect = "";
    require((*bar).effect_instance().entry() == &built_in("progress_laser"), "an empty name restores the style's");

    const std::uint64_t first = hosted_checksum("effects.woven", std::nullopt);
    require(first == hosted_checksum("effects.woven", std::nullopt), "the default seed follows the stable ID");
    require(first != hosted_checksum("effects.other", std::nullopt), "another stable ID weaves another pattern");
    require(first != hosted_checksum("effects.woven", 7U) && hosted_checksum("effects.woven", 7U) ==
                                                               hosted_checksum("effects.other", 7U),
            "an application seed replaces the default");
}

// The bar's effects run on the window's motion clock (design note section
// 7): enlisted while they move, dropped when the bar is not painted between
// frames, enlisted again by its next paint, stopped under reduced motion
// and the ambient switch.
void progress_bar_on_the_motion_clock() {
    std::shared_ptr<ProgressBar> bar = make_control<ProgressBar>(StableId("effects.clocked"));
    (*bar).set_requested_bounds({0.0, 0.0, 200.0, 20.0});
    (*bar).value = 40.0;
    (*bar).visual_style = ProgressBarVisualStyle::laser_etch;
    Window window(bar, {200.0, 40.0});
    window.perform_layout();
    MotionClock& clock = window.motion_clock();
    require(!clock.control_ambient(*bar), "a laser bar not yet drawn is not on the motion clock");
    RecordingPainter first;
    (*bar).on_paint(first, {0.0, 0.0, 200.0, 20.0});
    require(clock.control_ambient(*bar) && clock.ambient_count() == 1U && window.next_wake().has_value(),
            "the drawn laser bar is enlisted on the motion clock");
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    RecordingPainter second;
    (*bar).on_paint(second, {0.0, 0.0, 200.0, 20.0});
    require((*bar).effect_instance().counters().ticks > 0U, "a clock frame makes ticks due, and the paint runs them");
    require(window.metrics_snapshot().effect_ambient_frames == 1U, "the window counts the effects' ambient frame");
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    require(!clock.control_ambient(*bar) && !window.next_wake().has_value(),
            "a bar not painted between frames is dropped, and the window rests");
    RecordingPainter again;
    (*bar).on_paint(again, {0.0, 0.0, 200.0, 20.0});
    require(clock.control_ambient(*bar), "its next paint enlists it again");

    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    require(!clock.control_ambient(*bar), "reduced motion stops the effects");
    const std::uint64_t ticks = (*bar).effect_instance().counters().ticks;
    RecordingPainter still;
    (*bar).on_paint(still, {0.0, 0.0, 200.0, 20.0});
    require(!clock.control_ambient(*bar) && (*bar).effect_instance().counters().ticks == ticks && still.radial == 3,
            "under reduced motion the bar draws the still pose and stays off the clock");
    settings.reduced_motion = false;
    window.set_presentation_settings(settings);
    RecordingPainter calm_over;
    (*bar).on_paint(calm_over, {0.0, 0.0, 200.0, 20.0});
    require(clock.control_ambient(*bar), "after reduced motion the next paint enlists the effects again");
    settings.ambient_motion = false;
    window.set_presentation_settings(settings);
    require(!clock.control_ambient(*bar) && clock.ambient_count() == 0U, "the ambient switch stops the effects");
    settings.ambient_motion = true;
    window.set_presentation_settings(settings);
    RecordingPainter switched_on;
    (*bar).on_paint(switched_on, {0.0, 0.0, 200.0, 20.0});
    require(clock.control_ambient(*bar), "with the ambient switch on the next paint enlists them again");
    (*bar).visual_style = ProgressBarVisualStyle::continuous;
    require(!clock.control_ambient(*bar) && !window.next_wake().has_value(), "a style without an effect withdraws");
}

// The effect properties set what the methods set, and a granted pointer
// press reaches the bubbles; ungranted, the pointer passes through.
void effect_properties_and_input() {
    std::shared_ptr<ProgressBar> bar = make_control<ProgressBar>(StableId("effects.properties"));
    (*bar).set_requested_bounds({0.0, 0.0, 200.0, 20.0});
    (*bar).value = 60.0;
    Window window(bar, {200.0, 40.0});
    window.perform_layout();
    (*bar).set_property_value("effect", BindingValue{std::string("progress_bubbles")});
    require((*bar).effect.read() == "progress_bubbles" && (*bar).effect_instance().entry() == &built_in("progress_bubbles"),
            "the effect property selects an effect by name");
    require(std::holds_alternative<std::monostate>(*(*bar).property_value("effect_seed")),
            "the effect seed is null by default");
    (*bar).set_property_value("effect_seed", BindingValue{std::uint64_t{42U}});
    require((*bar).effect_seed.read() == std::optional<std::uint64_t>{42U}, "the effect_seed property sets the seed");
    (*bar).set_property_value("effect_seed", BindingValue{});
    require(!(*bar).effect_seed.read().has_value(), "null restores the derived seed");
    require(!(*bar).hit_test_local({10.0, 10.0}), "without a grant the pointer passes through the bar");
    (*bar).set_property_value("effect_input", BindingValue{PropertyEnumValue{"effect_input_kind", "pointer", 1}});
    require((*bar).effect_input.read() == EffectInputKind::pointer && (*bar).hit_test_local({10.0, 10.0}),
            "with a pointer grant the bar takes the pointer for its effect");

    RecordingPainter before;
    (*bar).animation_enabled = false;
    (*bar).on_paint(before, {0.0, 0.0, 200.0, 20.0});
    PointerEvent press{};
    press.action = PointerAction::down;
    press.button = PointerButton::primary;
    press.position = Point{50.0, 10.0};
    (*bar).on_pointer(press);
    RecordingPainter after;
    (*bar).on_paint(after, {0.0, 0.0, 200.0, 20.0});
    require(after.circles == before.circles + 1, "a granted press releases one bubble under the pointer");
    (*bar).effect_input = EffectInputKind::none;
    (*bar).on_pointer(press);
    RecordingPainter ignored;
    (*bar).on_paint(ignored, {0.0, 0.0, 200.0, 20.0});
    require(ignored.circles == after.circles, "without a grant a press reaches no effect");
}

// A theme whose rules give a progress bar's fill the effect `fill` and its
// activity band the effect `activity` (effect_property), each skipped when
// empty; a class rule `calm` declares none for the fill.
std::unique_ptr<ThemeDefinition> effect_theme(std::string_view fill, std::string_view activity) {
    std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "effect-programs";
    if (!fill.empty()) {
        ThemeRule rule{};
        rule.label = "progress-bar::part(fill)";
        rule.selector.control_type = "progress_bar";
        rule.selector.part = "fill";
        rule.declarations.push_back(RuleDeclaration::choice_value(effect_property, fill));
        definition.rules.rules.push_back(rule);
    }
    if (!activity.empty()) {
        ThemeRule rule{};
        rule.label = "progress-bar::part(activity)";
        rule.selector.control_type = "progress_bar";
        rule.selector.part = "activity";
        rule.declarations.push_back(RuleDeclaration::choice_value(effect_property, activity));
        definition.rules.rules.push_back(rule);
    }
    ThemeRule calm{};
    calm.label = "progress-bar.calm::part(fill)";
    calm.selector.control_type = "progress_bar";
    calm.selector.classes.push_back("calm");
    calm.selector.part = "fill";
    calm.declarations.push_back(RuleDeclaration::choice_value(effect_property, effect_none_name));
    definition.rules.rules.push_back(calm);
    return held;
}

// Themes select effects by name (design note section 7): a rule's effect
// declaration gives a part its effect; the application's name and the
// visual style's own come first; `none` selects none; the load checks
// refuse an unknown name, another parent's effect, a frame over the bound
// and (for a theme) an unverified effect, leaving the part to its
// primitive with a counted, structured diagnostic; and a malformed name is
// the rules' own diagnostic.
void themes_select_effects() {
    std::shared_ptr<ProgressBar> bar = make_control<ProgressBar>(StableId("effects.themed"));
    (*bar).set_requested_bounds({0.0, 0.0, 200.0, 20.0});
    (*bar).value = 50.0;
    Window window(bar, {200.0, 40.0});
    window.set_theme(Theme::create(*effect_theme("progress_woven_stripes", "progress_pulse")));
    window.perform_layout();
    RecordingPainter first;
    (*bar).on_paint(first, {0.0, 0.0, 200.0, 20.0});
    const EffectEntry& woven = built_in("progress_woven_stripes");
    if (woven.verified) {
        require((*bar).effect_instance().entry() == &woven &&
                    (*bar).effect_selection().source == EffectSelectionSource::theme &&
                    (*bar).effect_selection().outcome == EffectSelectionOutcome::selected &&
                    (*bar).effect_selection().name == "progress_woven_stripes",
                "the theme's effect for the fill draws over a continuous bar");
    } else {
        require((*bar).effect_instance().entry() == nullptr &&
                    (*bar).effect_selection().outcome == EffectSelectionOutcome::unverified,
                "a theme may not select an unverified effect: the fill draws its primitive alone");
    }
    (*bar).visual_style = ProgressBarVisualStyle::laser_etch;
    require((*bar).effect_instance().entry() == &built_in("progress_laser") &&
                (*bar).effect_selection().source == EffectSelectionSource::style,
            "the visual style's own effect comes before the theme's");
    (*bar).effect = "progress_bubbles";
    require((*bar).effect_instance().entry() == &built_in("progress_bubbles") &&
                (*bar).effect_selection().source == EffectSelectionSource::application,
            "the application's effect comes first");
    (*bar).effect = "";
    (*bar).visual_style = ProgressBarVisualStyle::marquee;
    RecordingPainter marquee;
    (*bar).on_paint(marquee, {0.0, 0.0, 200.0, 20.0});
    require((*bar).effect_selection().source == EffectSelectionSource::theme &&
                (*bar).effect_selection().name == "progress_pulse",
            "while marquee the theme's effect for the activity band applies");
    (*bar).visual_style = ProgressBarVisualStyle::continuous;
    (*bar).add_class("calm");
    RecordingPainter calm;
    (*bar).on_paint(calm, {0.0, 0.0, 200.0, 20.0});
    require((*bar).effect_instance().entry() == nullptr &&
                (*bar).effect_selection().source == EffectSelectionSource::theme &&
                (*bar).effect_selection().outcome == EffectSelectionOutcome::none,
            "a more specific rule's none selects no effect");

    const std::uint64_t refused = effect_load_diagnostics().refused;
    std::shared_ptr<ProgressBar> unknown = make_control<ProgressBar>(StableId("effects.unknown"));
    (*unknown).set_requested_bounds({0.0, 0.0, 200.0, 20.0});
    Window unknown_window(unknown, {200.0, 40.0});
    unknown_window.set_theme(Theme::create(*effect_theme("no_such_effect", "scope_sweep")));
    unknown_window.perform_layout();
    RecordingPainter unknown_paint;
    (*unknown).on_paint(unknown_paint, {0.0, 0.0, 200.0, 20.0});
    require((*unknown).effect_instance().entry() == nullptr &&
                (*unknown).effect_selection().outcome == EffectSelectionOutcome::unknown_effect &&
                effect_load_diagnostics().refused == refused + 1U,
            "an unknown effect is refused and counted, and the fill draws its primitive alone");
    RecordingPainter unknown_again;
    (*unknown).on_paint(unknown_again, {0.0, 0.0, 200.0, 20.0});
    require(effect_load_diagnostics().refused == refused + 1U, "a refusal is counted once, not once per paint");
    (*unknown).visual_style = ProgressBarVisualStyle::marquee;
    RecordingPainter other_parent;
    (*unknown).on_paint(other_parent, {0.0, 0.0, 200.0, 20.0});
    require((*unknown).effect_selection().outcome == EffectSelectionOutcome::parent_mismatch &&
                (*unknown).effect_instance().entry() == nullptr,
            "another parent type's effect is refused");

    const EffectEntry* entry = &woven;
    require(check_effect_selection(built_in_effect_registry(), "progress_pulse", EffectSelectionSource::application,
                                   EffectView<ProgressBar>::parent_name, Size{200.0, 20.0}, 1.0,
                                   entry) == EffectSelectionOutcome::selected &&
                entry == &built_in("progress_pulse"),
            "the load checks select a fitting effect");
    // progress_pulse costs 400 + 4 per device pixel: a million pixels pass
    // the 4,000,000 bound.
    require(check_effect_selection(built_in_effect_registry(), "progress_pulse", EffectSelectionSource::application,
                                   EffectView<ProgressBar>::parent_name, Size{1000.0, 1000.0}, 1.0,
                                   entry) == EffectSelectionOutcome::over_frame_budget &&
                entry == nullptr,
            "an effect whose frame passes the operations bound at the part's size is refused");
    require(check_effect_selection(built_in_effect_registry(), "", EffectSelectionSource::theme,
                                   EffectView<ProgressBar>::parent_name, Size{200.0, 20.0}, 1.0,
                                   entry) == EffectSelectionOutcome::none,
            "an empty name is none");

    require(effect_name_valid("progress_pulse") && !effect_name_valid("none") && !effect_name_valid("Pulse") &&
                !effect_name_valid("pulse__band") && !effect_name_valid(std::string(65U, 'a')),
            "an effect's name is snake_case, at most 64 characters, and not none");
    ThemeRuleSheet sheet{};
    ThemeRule malformed{};
    malformed.label = "malformed";
    malformed.selector.control_type = "progress_bar";
    malformed.declarations.push_back(RuleDeclaration::choice_value(effect_property, "Not-An-Effect"));
    sheet.rules.push_back(malformed);
    ThemeRule twice{};
    twice.label = "twice";
    twice.selector.control_type = "progress_bar";
    twice.declarations.push_back(RuleDeclaration::choice_value(effect_property, "progress_pulse"));
    twice.declarations.push_back(RuleDeclaration::choice_value(effect_property, "progress_laser"));
    sheet.rules.push_back(twice);
    const CompiledThemeRules compiled = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    bool invalid = false;
    bool duplicate = false;
    for (const RuleDiagnostic& diagnostic : compiled.diagnostics()) {
        if (diagnostic.kind == RuleDiagnosticKind::effect_invalid && diagnostic.label == "malformed") invalid = true;
        if (diagnostic.kind == RuleDiagnosticKind::duplicate_declaration && diagnostic.label == "twice") {
            duplicate = true;
        }
    }
    require(invalid && duplicate && compiled.rule_count() == 1U,
            "a malformed name is effect_invalid and drops its rule; a second effect declaration is a duplicate");
}

// A theme giving a progress bar's fill the effect colour roles focus and
// chrome-highlight, with other colours for them in the dark scheme (as a
// palette's media variant lowers to a rule copy under the condition).
std::unique_ptr<ThemeDefinition> colour_role_theme() {
    std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "effect-colours";
    // The theme has a dark variant (T4 theme variants); without one, a dark
    // window shows GUI.Forms' own theme and these rules never apply.
    definition.colour_schemes = ColourSchemeSet{.light = true, .dark = true};
    ColourOrigin focus{};
    focus.source = ColourSource::role;
    focus.role = ColourRole::focus;
    ColourOrigin highlight{};
    highlight.source = ColourSource::role;
    highlight.role = ColourRole::chrome_highlight;
    ThemeRule light{};
    light.label = "progress-bar::part(fill)";
    light.selector.control_type = "progress_bar";
    light.selector.part = "fill";
    RuleDeclaration primary = RuleDeclaration::colour_value(effect_primary_property, Color::rgba(32, 96, 192));
    primary.origin = focus;
    RuleDeclaration secondary =
        RuleDeclaration::colour_value(effect_secondary_property, Color::rgba(240, 244, 248));
    secondary.origin = highlight;
    light.declarations.push_back(primary);
    light.declarations.push_back(secondary);
    definition.rules.rules.push_back(light);
    ThemeRule dark = light;
    dark.label = "progress-bar::part(fill) (dark)";
    dark.media.dark_scheme = true;
    dark.declarations[0].colour = Color::rgba(120, 170, 255);
    dark.declarations[1].colour = Color::rgba(40, 44, 52);
    definition.rules.rules.push_back(dark);
    return held;
}

// The barber pole follows Control::right_to_left (owner ruling of
// 2026-11-11). Left to right, a marching-stripes bar fills from the left
// and its bands are laid from the left edge; under a right-to-left parent
// the fill grows from the right and the bands are laid, leaning and
// travelling, from the right edge (the effect's own mirrored travel is
// stripes_pulse_laser's). Each band's first point is on its near side, so
// their mean lies in the filled half.
double mean_polygon_start(const RecordingPainter& painter) {
    double total = 0.0;
    for (const Point& start : painter.polygon_starts) total += start.x;
    const double count = static_cast<double>(painter.polygon_starts.size());
    const double mean = count > 0.0 ? total / count : 0.0;
    return mean;
}

void progress_bar_follows_right_to_left() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("effects.direction.root"));
    std::shared_ptr<ProgressBar> bar = make_control<ProgressBar>(StableId("effects.direction.bar"));
    (*bar).set_requested_bounds({0.0, 0.0, 200.0, 20.0});
    (*bar).margin = Insets{0.0, 0.0, 0.0, 0.0};
    (*bar).value = 50.0;
    (*bar).visual_style = ProgressBarVisualStyle::marching_stripes;
    (*root).add_child(bar);
    Window window(root, {200.0, 40.0});
    window.perform_layout();
    RecordingPainter forward;
    (*bar).on_paint(forward, {0.0, 0.0, 200.0, 20.0});
    const Rect forward_fill = (*bar).parts().fill.bounds;
    require(forward_fill.x == 2.0 && forward.polygons > 0 && mean_polygon_start(forward) < 100.0,
            "left to right, the barber pole fills and lays its bands from the left");

    (*root).layout_direction = LayoutDirection::right_to_left;
    window.perform_layout();
    RecordingPainter mirrored;
    (*bar).on_paint(mirrored, {0.0, 0.0, 200.0, 20.0});
    const Rect mirrored_fill = (*bar).parts().fill.bounds;
    require(mirrored_fill.right() == 198.0 && mirrored_fill.width == forward_fill.width,
            "right to left, the barber pole's fill grows from the right edge");
    require(mirrored.polygons > 0 && mean_polygon_start(mirrored) > 100.0,
            "right to left, the barber pole's bands are laid from the right edge");

    (*bar).visual_style = ProgressBarVisualStyle::blocks;
    const Rect blocks_fill = (*bar).parts().fill.bounds;
    require(blocks_fill.right() == 198.0, "right to left, the blocks fill from the right edge too");
}

// Effects take colour roles (EP-4): the theme's primary and secondary roles
// for the effect's part reach the frame (EffectFrame::colours), follow the
// colour scheme, are remapped by the forced high-contrast pass, and give
// the progress effects their colours at the effects' own alphas; the
// application's own appearance comes first; a literal is refused.
void effects_take_colour_roles() {
    std::shared_ptr<ProgressBar> bar = make_control<ProgressBar>(StableId("effects.coloured"));
    (*bar).set_requested_bounds({0.0, 0.0, 200.0, 20.0});
    (*bar).value = 50.0;
    (*bar).visual_style = ProgressBarVisualStyle::marching_stripes;
    Window window(bar, {200.0, 40.0});
    // A theme without colour roles: the windows-professional fixture (the
    // default, gui-forms, has its light sheet's).
    window.set_theme(test_support::professional_theme());
    RecordingPainter plain;
    window.perform_layout();
    (*bar).on_paint(plain, {0.0, 0.0, 200.0, 20.0});
    const ProgressBarAnimationAppearance appearance{};
    require((*bar).effect_colours().primary == appearance.stripe_color &&
                (*bar).effect_colours().secondary == appearance.luminance_color &&
                (*bar).effect_instance().colours() == (*bar).effect_colours(),
            "without theme roles the effects take the appearance's stripe and luminance colours");

    window.set_theme(Theme::create(*colour_role_theme()));
    RecordingPainter light;
    (*bar).on_paint(light, {0.0, 0.0, 200.0, 20.0});
    require((*bar).effect_colours().primary == Color::rgba(32, 96, 192) &&
                (*bar).effect_colours().secondary == Color::rgba(240, 244, 248) &&
                (*bar).effect_instance().colours() == (*bar).effect_colours(),
            "the theme's roles reach the effect's frame");

    PresentationSettings settings = window.presentation_settings();
    settings.colour_scheme = ColourScheme::dark;
    window.set_presentation_settings(settings);
    RecordingPainter dark;
    (*bar).on_paint(dark, {0.0, 0.0, 200.0, 20.0});
    require((*bar).effect_colours().primary == Color::rgba(120, 170, 255) &&
                (*bar).effect_colours().secondary == Color::rgba(40, 44, 52),
            "in the dark scheme the roles take the dark palette's colours");

    settings.high_contrast = true;
    window.set_presentation_settings(settings);
    RecordingPainter contrast;
    (*bar).on_paint(contrast, {0.0, 0.0, 200.0, 20.0});
    ColourOrigin focus{};
    focus.source = ColourSource::role;
    focus.role = ColourRole::focus;
    const Color remapped = high_contrast_colour(focus, ColourPosition::fill, ControlSurfaceState::normal, false);
    require((*bar).effect_colours().primary == remapped,
            "under forced high contrast the primary role takes the pass's colour for its role");
    settings.high_contrast = false;
    settings.colour_scheme = ColourScheme::light;
    window.set_presentation_settings(settings);

    ProgressBarAnimationAppearance own{};
    own.stripe_color = Color::rgba(200, 10, 10, 120);
    (*bar).animation_appearance = own;
    RecordingPainter application;
    (*bar).on_paint(application, {0.0, 0.0, 200.0, 20.0});
    require((*bar).effect_colours().primary == own.stripe_color,
            "the application's own appearance comes before the theme's roles");

    ThemeRuleSheet sheet{};
    ThemeRule literal{};
    literal.label = "literal";
    literal.selector.control_type = "progress_bar";
    literal.declarations.push_back(RuleDeclaration::colour_value(effect_primary_property, Color::rgba(1, 2, 3)));
    sheet.rules.push_back(literal);
    const CompiledThemeRules compiled = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    bool refused = false;
    for (const RuleDiagnostic& diagnostic : compiled.diagnostics()) {
        if (diagnostic.kind == RuleDiagnosticKind::effect_colour_invalid) refused = true;
    }
    require(refused && compiled.rule_count() == 0U, "an effect colour that is a literal, not a role, is refused");
}

// A test effect taking pointer input that draws one line from where its
// frame says the pointer stands, while it is inside, and a second while a
// button is held.
class PointerProbe final {
public:
    using parent_type = ProgressBar;
    static constexpr EffectDeclaration declaration{
        .name = "test_pointer_probe",
        .cost = {.fixed = 100U, .per_device_pixel = 1U},
        .state_budget = 8U,
        .input = EffectInputKind::pointer,
    };
    void run(const EffectView<ProgressBar>&, const EffectFrame& frame, EffectCanvas& canvas) {
        if (frame.pointer.inside) {
            const Point from = frame.pointer.position;
            canvas.stroke_line(from, Point{from.x + 1.0, from.y}, Color::rgba(1, 2, 3, 255), 1.0);
        }
        if (frame.pointer.pressed) {
            const Point from = frame.pointer.position;
            canvas.stroke_line(from, Point{from.x, from.y + 1.0}, Color::rgba(1, 2, 3, 255), 1.0);
        }
        frames_ = frames_ + 1U;
    }
    std::uint64_t frames_{};
};

const EffectEntry& pointer_probe_entry() {
    static const EffectEntry entry = make_effect_entry<PointerProbe>();
    return entry;
}

PointerEvent pointer_event(PointerAction action, Point position) {
    PointerEvent event{};
    event.action = action;
    event.position = position;
    if (action == PointerAction::down || action == PointerAction::up) event.button = PointerButton::primary;
    return event;
}

// The pointer record (design note section 8a(1)): the engine converts the
// host's pointer events to part-local units, queues them under the grant
// and keeps the standing state (inside, pressed, last position inside),
// which the frame carries only under a grant; with no grant every field is
// zero and the state checksum is the same as with no pointer at all.
void pointer_record() {
    const EffectView<ProgressBar> view = bar_view(0.5);
    const Point origin{10.0, 20.0};
    EffectInstance granted;
    granted.attach(pointer_probe_entry(), 1U, start);
    granted.set_input_grant(EffectInputKind::pointer);
    require(granted.note_pointer(pointer_event(PointerAction::enter, Point{15.0, 25.0}), origin) &&
                granted.pointer().inside && granted.pointer().position == Point{5.0, 5.0},
            "enter sets inside at the part-local position");
    require(granted.note_pointer(pointer_event(PointerAction::move, Point{40.0, 30.0}), origin) &&
                granted.pointer().position == Point{30.0, 10.0},
            "a move updates the position and asks for a repaint");
    RecordingPainter inside;
    granted.paint(inside, Rect{0.0, 0.0, 200.0, 20.0}, 1.0, true, view);
    require(inside.lines == 1 && inside.line_starts.size() == 1U && inside.line_starts[0] == Point{30.0, 10.0},
            "the frame carries the pointer inside the part at its last position");
    require(granted.note_pointer(pointer_event(PointerAction::down, Point{40.0, 30.0}), origin) &&
                granted.pointer().pressed,
            "a press sets pressed");
    RecordingPainter pressed;
    granted.paint(pressed, Rect{0.0, 0.0, 200.0, 20.0}, 1.0, true, view);
    require(pressed.lines == 2, "the frame carries the held button");
    require(granted.note_pointer(pointer_event(PointerAction::leave, Point{300.0, 300.0}), origin) &&
                !granted.pointer().inside && !granted.pointer().pressed &&
                granted.pointer().position == Point{30.0, 10.0},
            "leave clears inside and pressed and keeps the last position seen inside");
    RecordingPainter outside;
    granted.paint(outside, Rect{0.0, 0.0, 200.0, 20.0}, 1.0, true, view);
    require(outside.lines == 0, "after leave the effect sees the pointer outside");
    require(!granted.note_pointer(pointer_event(PointerAction::leave, Point{300.0, 300.0}), origin),
            "a second leave changes nothing and asks for no repaint");

    EffectInstance ungranted;
    ungranted.attach(pointer_probe_entry(), 1U, start);
    EffectInstance untouched;
    untouched.attach(pointer_probe_entry(), 1U, start);
    require(!ungranted.note_pointer(pointer_event(PointerAction::enter, Point{15.0, 25.0}), origin) &&
                ungranted.pointer() == EffectPointer{},
            "without a grant the pointer is not taken");
    RecordingPainter first;
    ungranted.paint(first, Rect{0.0, 0.0, 200.0, 20.0}, 1.0, true, view);
    RecordingPainter second;
    untouched.paint(second, Rect{0.0, 0.0, 200.0, 20.0}, 1.0, true, view);
    require(first.lines == 0 && ungranted.state_checksum() == untouched.state_checksum(),
            "with no grant the frame's pointer is zero: the checksum is the same as with no pointer");
    granted.set_input_grant(EffectInputKind::none);
    require(granted.pointer() == EffectPointer{}, "withdrawing the grant forgets the pointer");
}

// A test effect that moves its origin, clips to its part's shape and fills
// the part.
class ShapeProbe final {
public:
    using parent_type = ProgressBar;
    static constexpr EffectDeclaration declaration{
        .name = "test_shape_probe",
        .cost = {.fixed = 100U, .per_device_pixel = 1U},
        .state_budget = 8U,
    };
    void run(const EffectView<ProgressBar>&, const EffectFrame& frame, EffectCanvas& canvas) {
        canvas.save();
        canvas.translate(Point{5.0, 0.0});
        canvas.clip_part_shape();
        canvas.fill_rect(Rect{0.0, 0.0, frame.part.width, frame.part.height}, Color::rgba(1, 2, 3, 255));
        canvas.restore();
    }
};

const EffectEntry& shape_probe_entry() {
    static const EffectEntry entry = make_effect_entry<ShapeProbe>();
    return entry;
}

// clip_part_shape (EP-6): the canvas clips to the part's box with the
// corners its host gives it, each clamped to half the shorter side, in the
// painter's coordinates after the effect's own translation; with no shape
// the part is a plain rectangle.
void part_shape_clip() {
    const EffectView<ProgressBar> view = bar_view(0.5);
    EffectInstance instance;
    instance.attach(shape_probe_entry(), 1U, start);
    PartShape rounded{};
    rounded.corner_radius = 6.0;
    RecordingPainter even;
    instance.paint(even, Rect{0.0, 0.0, 200.0, 20.0}, 1.0, true, view, rounded);
    require(even.corner_clips.size() == 1U && even.corner_boxes[0] == Rect{-5.0, 0.0, 200.0, 20.0},
            "the shape clip is the part's box, moved by the effect's translation");
    const PaintCornerRadii sixes{6.0, 6.0, 6.0, 6.0};
    require(even.corner_clips[0] == sixes, "a rounded part gives every corner its radius");
    require(even.saves == even.restores, "the shape clip is undone by the effect's restore");

    PartShape differing{};
    differing.per_corner = true;
    differing.corners = PartCornerRadii{30.0, 0.0, 4.0, 2.0};
    RecordingPainter uneven;
    instance.paint(uneven, Rect{0.0, 0.0, 200.0, 20.0}, 1.0, true, view, differing);
    const PaintCornerRadii clamped{10.0, 0.0, 4.0, 2.0};
    require(uneven.corner_clips.size() == 1U && uneven.corner_clips[0] == clamped,
            "per-corner radii reach the clip, each clamped to half the shorter side");

    RecordingPainter plain;
    instance.paint(plain, Rect{0.0, 0.0, 200.0, 20.0}, 1.0, true, view);
    require(plain.corner_clips.size() == 1U && plain.corner_clips[0] == PaintCornerRadii{},
            "with no shape the part is a plain rectangle");
}

// A test effect that reads the backdrop under its part, blurred over three
// logical units, and draws it over the whole part; posed, never ticked.
class BackdropProbe final {
public:
    using parent_type = ProgressBar;
    static constexpr EffectDeclaration declaration{
        .name = "test_backdrop_probe",
        .time_model = EffectTimeModel::static_pose,
        .cost = {.fixed = 100U, .per_device_pixel = 4U},
        .state_budget = 8U,
        .backdrop = true,
        .backdrop_margin = 3.0,
    };
    void run(const EffectView<ProgressBar>&, const EffectFrame& frame, EffectCanvas& canvas) {
        canvas.draw_backdrop(Rect{0.0, 0.0, frame.part.width, frame.part.height}, 1.0);
    }
};

// One whose blur reaches past glass's bound.
class FarBackdrop final {
public:
    using parent_type = ProgressBar;
    static constexpr EffectDeclaration declaration{
        .name = "test_far_backdrop",
        .cost = {.fixed = 100U, .per_device_pixel = 4U},
        .state_budget = 8U,
        .backdrop = true,
        .backdrop_margin = 9.0,
    };
    void run(const EffectView<ProgressBar>&, const EffectFrame&, EffectCanvas&) {}
};

const EffectEntry& backdrop_probe_entry() {
    static const EffectEntry entry = make_effect_entry<BackdropProbe>();
    return entry;
}

// Vocabulary version 2: marching ants, a dashed outline whose phase
// follows the ticks.
class MarchingAnts final {
public:
    using parent_type = ProgressBar;
    static constexpr EffectDeclaration declaration{
        .name = "test_marching_ants",
        .engine_version = 2U,
        .cost = {.fixed = 200U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const EffectView<ProgressBar>&, const EffectFrame& frame, EffectCanvas& canvas) {
        phase_ = phase_ + static_cast<double>(frame.ticks) * 0.5;
        EffectPoints outline{};
        outline.items[0] = Point{1.0, 1.0};
        outline.items[1] = Point{frame.part.width - 1.0, 1.0};
        outline.items[2] = Point{frame.part.width - 1.0, frame.part.height - 1.0};
        outline.items[3] = Point{1.0, frame.part.height - 1.0};
        outline.items[4] = Point{1.0, 1.0};
        outline.count = 5U;
        StrokeStyle style{};
        style.color = Color::rgba(0, 0, 0, 255);
        style.width = 1.0;
        style.dashes[0] = 3.0;
        style.dashes[1] = 3.0;
        style.dash_count = 2U;
        style.dash_phase = phase_;
        canvas.stroke_polyline(outline, style);
    }
    double phase_{};
};

class FutureVocabulary final {
public:
    using parent_type = ProgressBar;
    static constexpr EffectDeclaration declaration{
        .name = "test_future_vocabulary",
        .engine_version = 3U,
        .cost = {.fixed = 100U, .per_device_pixel = 1U},
        .state_budget = 8U,
    };
    void run(const EffectView<ProgressBar>&, const EffectFrame&, EffectCanvas&) {}
};

const EffectEntry& marching_ants_entry() {
    static const EffectEntry entry = make_effect_entry<MarchingAnts>();
    return entry;
}

EffectPoints effect_points(std::span<const Point> points) {
    EffectPoints result{};
    for (std::size_t index = 0U; index < points.size(); ++index) result.items[index] = points[index];
    result.count = static_cast<std::uint32_t>(points.size());
    return result;
}

// Vocabulary version 2: each batched call draws once through the painter,
// is metered on its bounds, and refuses (and counts) arrays outside their
// bounds, points that are not finite and invalid styles; a registry admits
// version 2 and refuses a later one.
void vocabulary_version_two() {
    EffectRegistry local;
    require(local.add(marching_ants_entry()) == EffectRegistrationOutcome::registered,
            "a version 2 effect registers");
    require(local.add(make_effect_entry<FutureVocabulary>()) == EffectRegistrationOutcome::vocabulary_version_unknown,
            "an effect naming a later vocabulary is refused");
    EffectDeclaration unversioned = FillPart::declaration;
    unversioned.engine_version = 0U;
    EffectEntry zero = make_effect_entry<FillPart>();
    zero.declaration = &unversioned;
    require(local.add(zero) == EffectRegistrationOutcome::vocabulary_version_unknown, "version 0 is refused");

    RecordingPainter painter;
    StrokeStyle stroke{};
    stroke.color = Color::rgba(0, 0, 0, 255);
    stroke.width = 2.0;
    MarkerStyle marker{};
    marker.shape = MarkerShape::diamond;
    marker.size = 4.0;
    marker.fill = Color::rgba(255, 0, 0, 255);
    EffectGradient hue{};
    hue.stops[0] = GradientStop{0.0, Color::rgba(255, 0, 0, 255)};
    hue.stops[1] = GradientStop{1.0, Color::rgba(0, 0, 255, 255)};
    hue.count = 2U;
    CheckerPattern checker{};
    checker.cell = 4.0;
    checker.first = Color::rgba(255, 255, 255, 255);
    checker.second = Color::rgba(200, 200, 200, 255);
    const std::array<Point, 3> triangle{Point{0.0, 0.0}, Point{10.0, 0.0}, Point{5.0, 8.0}};
    const std::array<Point, 3> lower{Point{0.0, 10.0}, Point{10.0, 12.0}, Point{20.0, 10.0}};
    {
        EffectCanvas canvas(painter, Size{100.0, 20.0}, Insets{}, 1.0, 20U);
        canvas.stroke_polyline(effect_points(triangle), stroke);
        canvas.fill_polygon(effect_points(triangle), Color::rgba(0, 0, 0, 255), PolygonFillRule::nonzero);
        canvas.fill_band(effect_points(triangle), effect_points(lower), Color::rgba(0, 0, 0, 128));
        EffectRects bars{};
        bars.items[0] = Rect{0.0, 0.0, 4.0, 10.0};
        bars.items[1] = Rect{6.0, 2.0, 4.0, 8.0};
        bars.count = 2U;
        canvas.fill_rects(bars, Color::rgba(0, 0, 0, 255));
        EffectSegments ticks{};
        ticks.items[0] = PaintSegment{Point{0.0, 0.0}, Point{0.0, 5.0}};
        ticks.count = 1U;
        canvas.stroke_segments(ticks, stroke);
        canvas.draw_markers(effect_points(triangle), marker);
        canvas.fill_conic_gradient(Rect{0.0, 0.0, 20.0, 20.0}, Point{10.0, 10.0}, -90.0, hue);
        canvas.fill_checker(Rect{20.0, 0.0, 20.0, 20.0}, checker);
        require(canvas.meter().refused == 0U && canvas.meter().calls == 8U, "every version 2 call is admitted");
        require(canvas.meter().charged > 0.0, "the batched calls are metered");
    }
    require(painter.polylines == 1 && painter.polygons == 1 && painter.bands == 1 && painter.rect_batches == 1 &&
                painter.segment_batches == 1 && painter.marker_batches == 1 && painter.conics == 1 &&
                painter.checkers == 1,
            "each version 2 call draws once through the painter");
    require(painter.last_stops == 2U, "the conic passes its live stops");

    RecordingPainter refused;
    {
        EffectCanvas canvas(refused, Size{100.0, 20.0}, Insets{}, 1.0, 20U);
        EffectPoints single = effect_points(std::span<const Point>(triangle.data(), 1U));
        canvas.stroke_polyline(single, stroke);
        EffectPoints overfull = effect_points(triangle);
        overfull.count = static_cast<std::uint32_t>(maximum_effect_batch + 1U);
        canvas.fill_polygon(overfull, Color::rgba(0, 0, 0, 255), PolygonFillRule::nonzero);
        EffectPoints lost = effect_points(triangle);
        lost.items[1] = Point{std::numeric_limits<double>::quiet_NaN(), 0.0};
        canvas.fill_polygon(lost, Color::rgba(0, 0, 0, 255), PolygonFillRule::nonzero);
        StrokeStyle hairless = stroke;
        hairless.width = 0.0;
        canvas.stroke_polyline(effect_points(triangle), hairless);
        canvas.fill_band(effect_points(triangle), effect_points(std::span<const Point>(lower.data(), 2U)),
                         Color::rgba(0, 0, 0, 255));
        MarkerStyle dot = marker;
        dot.size = 0.0;
        canvas.draw_markers(effect_points(triangle), dot);
        EffectGradient one = hue;
        one.count = 1U;
        canvas.fill_conic_gradient(Rect{0.0, 0.0, 20.0, 20.0}, Point{10.0, 10.0}, 0.0, one);
        CheckerPattern flat = checker;
        flat.cell = 0.0;
        canvas.fill_checker(Rect{0.0, 0.0, 20.0, 20.0}, flat);
        canvas.fill_rects(EffectRects{}, Color::rgba(0, 0, 0, 255));
        require(canvas.meter().refused == 9U && canvas.meter().calls == 9U, "each refusal is counted");
    }
    require(refused.polylines == 0 && refused.polygons == 0 && refused.bands == 0 && refused.marker_batches == 0 &&
                refused.conics == 0 && refused.checkers == 0 && refused.rect_batches == 0,
            "a refused call draws nothing");

    RecordingPainter metered;
    {
        EffectCanvas canvas(metered, Size{10.0, 10.0}, Insets{}, 1.0, 1U);
        EffectRects whole{};
        whole.items[0] = Rect{0.0, 0.0, 10.0, 10.0};
        whole.count = 1U;
        canvas.fill_rects(whole, Color::rgba(0, 0, 0, 255));
        canvas.fill_rects(whole, Color::rgba(0, 0, 0, 255));
        require(canvas.meter().charged == 100.0 && canvas.meter().refused == 1U, "a batch past the budget is refused");
    }
    require(metered.rect_batches == 1, "only the batch within the budget is drawn");

    // Hosted: the ants' phase follows the ticks.
    const EffectView<ProgressBar> view = bar_view(0.5);
    EffectInstance ants;
    ants.attach(marching_ants_entry(), 1U, start);
    RecordingPainter first;
    ants.paint(first, Rect{0.0, 0.0, 40.0, 10.0}, 1.0, true, view);
    require(first.polylines == 1 && first.last_batch == 5U && ants.last_meter().refused == 0U,
            "a version 2 effect draws its outline as one polyline");
}

// Backdrop sampling (EP-7): an effect declaring a backdrop draws the
// window's backplane under its part, blurred, through one metered canvas
// call; the blurred raster is kept while the backplane stands; a posed
// effect is damaged when the backplane changes; a headless paint lends no
// backdrop and the call is refused.
void backdrop_sampling() {
    EffectRegistry local;
    require(local.add(make_effect_entry<FarBackdrop>()) == EffectRegistrationOutcome::backdrop_out_of_range,
            "a backdrop blur past its bound is refused at registration");
    require(effect_backdrop_half(3.0, 1.0) == 1U && effect_backdrop_half(3.0, 2.0) == 2U &&
                effect_backdrop_half(8.0, 1.0) == 2U && effect_backdrop_half(-1.0, 1.0) == 0U,
            "the box half width is floor(margin * scale / 3)");

    const EffectView<ProgressBar> view = bar_view(0.5);
    EffectInstance headless;
    headless.attach(backdrop_probe_entry(), 1U, start);
    RecordingPainter alone;
    headless.paint(alone, Rect{10.0, 20.0, 40.0, 10.0}, 1.0, true, view);
    require(alone.part_rasters.empty() && headless.last_meter().refused == 1U,
            "with no host there is no backdrop: the call is refused and counted");

    // The host is a control in the control plane: a panel paints in the
    // backplane plane, and would sample itself.
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("effects.backdrop"));
    std::shared_ptr<Control> host = make_control<Control>(StableId("effects.backdrop.host"));
    (*host).set_requested_bounds({0.0, 0.0, 100.0, 40.0});
    (*root).add_child(host);
    Window window(root, {200.0, 100.0});
    // The backplane is made from the theme's window material.
    window.set_theme(Theme::create(*test_support::professional_definition()));
    window.perform_layout();
    EffectInstance instance;
    instance.attach(backdrop_probe_entry(), 1U, start);
    RecordingPainter first;
    instance.paint(first, Rect{10.0, 20.0, 40.0, 10.0}, 1.0, true, view, PartShape{}, host.get());
    require(first.part_rasters.size() == 1U && instance.last_meter().refused == 0U,
            "a hosted effect draws its backdrop");
    const PartRaster& sampled = *first.part_rasters[0];
    require(sampled.extent() == DeviceExtent{40U, 10U} && first.part_origins[0] == Point{0.0, 0.0},
            "the backdrop covers the part, from its corner, one pixel per device pixel");
    require(sampled.pixels()[3] > 0U, "the backdrop holds the backplane's pixels");

    RecordingPainter again;
    instance.paint(again, Rect{10.0, 20.0, 40.0, 10.0}, 1.0, true, view, PartShape{}, host.get());
    require(again.part_rasters.size() == 1U && again.part_rasters[0].get() == first.part_rasters[0].get(),
            "while the backplane stands the blurred backdrop is kept, not blurred again");

    window.reset_activity_metrics();
    window.set_active(false);
    // Activation reaches the backplane's readers by more than one path;
    // each damage is the effect's extent.
    const MetricsSnapshot activated = window.metrics_snapshot();
    require(activated.engine_input_damaged_parts >= 1U &&
                activated.engine_input_damage_area ==
                    static_cast<double>(activated.engine_input_damaged_parts) * 40.0 * 10.0,
            "a posed effect reading the backdrop is damaged within its extent when the backplane changes");
    RecordingPainter changed;
    instance.paint(changed, Rect{10.0, 20.0, 40.0, 10.0}, 1.0, true, view, PartShape{}, host.get());
    require(changed.part_rasters.size() == 1U && changed.part_rasters[0].get() != first.part_rasters[0].get(),
            "a changed backplane is blurred again, into a new raster while the old one is held");
}

int main() {
    generator_known_answers();
    bounded_ranges();
    registry_checks();
    verification();
    ticks_and_skips();
    meter_and_depth();
    stripes_pulse_laser();
    woven_stripes();
    bubbles_and_steam();
    progress_bar_hosting();
    progress_bar_on_the_motion_clock();
    effect_properties_and_input();
    themes_select_effects();
    effects_take_colour_roles();
    progress_bar_follows_right_to_left();
    pointer_record();
    part_shape_clip();
    backdrop_sampling();
    vocabulary_version_two();
    std::cout << "effect programs: generator, bounded ranges, registry, verification, ticks and skips, meter, "
                 "stripes, pulse, laser, woven stripes, bubbles and steam, progress bar hosting, motion clock, "
                 "properties and input, themes selecting effects, colour roles, right to left, pointer record, "
                 "part shape clip, "
                 "backdrop, vocabulary version 2\n";
    return 0;
}
