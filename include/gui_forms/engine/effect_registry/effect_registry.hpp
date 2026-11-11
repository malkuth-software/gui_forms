#pragma once

// The effect registry and an effect's instance on a part (design note §1,
// §6, §7). Registration is explicit and checked, beside the primitive
// registry: one generated entry per effect type (make_effect_entry), which
// themes and applications address by name. The only indirect calls are the
// entry's thunks, made by the engine; effect code makes none.
//
// An EffectInstance owns one effect's state on one part: storage of the
// effect's size and alignment from the toolkit's allocator, value-
// initialized when the effect is attached and released when it is detached
// or the instance is destroyed. It counts fixed ticks from the engine clock
// it is given (never the wall clock), at most
// maximum_effect_ticks_per_frame per frame, skipping and counting the rest;
// it runs setup before the first frame and, when declared, after a geometry
// change; and it builds each frame's canvas with the effect's budget.
//
// Execution: UI thread. An instance is owned by the control (or engine part)
// that hosts it; it holds no reference to anything else.

#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/engine/effect_canvas/effect_canvas.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace gui_forms {

class Control;
class Painter;
class PartRaster;

namespace engine_primitives {
struct BoxBlurWork;
} // namespace engine_primitives

// One registered effect type: its declaration, its parent's name, its state
// layout and the thunks the engine calls. `setup` is null for an effect
// without one. `verified` is set at registration from the plugin's record.
struct EffectEntry final {
    const EffectDeclaration* declaration{};
    std::string_view parent{};
    std::size_t size{};
    std::size_t alignment{};
    void* (*construct)(void* storage) noexcept {};
    void (*setup)(void* state, const void* view, const EffectSetup& setup) noexcept {};
    void (*run)(void* state, const void* view, const EffectFrame& frame, EffectCanvas& canvas) noexcept {};
    bool verified{};
};

namespace effect_detail {

template <typename Effect>
void* construct(void* storage) noexcept {
    Effect* const made = ::new (storage) Effect{};
    return made;
}

template <typename Effect>
void setup(void* state, const void* view, const EffectSetup& setup) noexcept {
    using View = EffectView<typename Effect::parent_type>;
    Effect& effect = *static_cast<Effect*>(state);
    effect.setup(*static_cast<const View*>(view), setup);
}

template <typename Effect>
void run(void* state, const void* view, const EffectFrame& frame, EffectCanvas& canvas) noexcept {
    using View = EffectView<typename Effect::parent_type>;
    Effect& effect = *static_cast<Effect*>(state);
    effect.run(*static_cast<const View*>(view), frame, canvas);
}

// Releases an EffectInstance's state storage, allocated at `alignment`.
// Declared at namespace scope, complete before EffectInstance uses it: a
// nested class with a default member initializer is not default
// constructible until its enclosing class is complete, which libstdc++'s
// unique_ptr default constructor requires.
struct StorageRelease final {
    std::size_t alignment{};
    void operator()(std::byte* storage) const noexcept;
};

// Releases an instance's blur storage (EffectInstance's backdrop), whose
// type is private to the engine.
struct BlurWorkRelease final {
    void operator()(engine_primitives::BoxBlurWork* work) const noexcept;
};

} // namespace effect_detail

// The generated table entry of one effect type. Its state must fit the
// budget it declares, and that budget the toolkit's maximum.
template <typename Effect>
    requires EffectProgram<Effect>
[[nodiscard]] constexpr EffectEntry make_effect_entry() noexcept {
    static_assert(sizeof(Effect) <= Effect::declaration.state_budget,
                  "an effect's state (its sizeof) must fit its declared state_budget");
    static_assert(Effect::declaration.state_budget <= maximum_effect_state_bytes,
                  "an effect's state budget must not exceed maximum_effect_state_bytes");
    EffectEntry entry{};
    entry.declaration = &Effect::declaration;
    entry.parent = EffectView<typename Effect::parent_type>::parent_name;
    entry.size = sizeof(Effect);
    entry.alignment = alignof(Effect);
    entry.construct = &effect_detail::construct<Effect>;
    if constexpr (EffectWithSetup<Effect>) entry.setup = &effect_detail::setup<Effect>;
    entry.run = &effect_detail::run<Effect>;
    return entry;
}

// Whether `name` may name an effect: snake_case (lowercase ASCII letters,
// digits and single '_'s, the first a letter, the last not '_'), at most
// maximum_effect_name_length characters, and not effect_none_name's
// `none`, which themes write for no effect.
inline constexpr std::size_t maximum_effect_name_length = 64U;
[[nodiscard]] bool effect_name_valid(std::string_view name) noexcept;

enum class EffectRegistrationOutcome : std::uint8_t {
    registered,
    registry_full,
    // The name is not one effect_name_valid admits.
    name_not_snake_case,
    duplicate_name,
    no_parent,
    setup_without_budget,
    frame_rate_out_of_range,
    outsets_out_of_range,
    // A backdrop margin that is not finite, negative or past
    // maximum_effect_backdrop_margin.
    backdrop_out_of_range,
    // An engine_version of 0 or past current_effect_vocabulary_version.
    vocabulary_version_unknown,
};

// The most effect types one registry holds.
inline constexpr std::size_t maximum_registered_effects = 64U;
// The farthest an effect may draw outside its part, in logical units.
inline constexpr double maximum_effect_outset = 64.0;
// The farthest an effect's backdrop blur reaches (EffectDeclaration::
// backdrop_margin), in logical units: glass's own bound.
inline constexpr double maximum_effect_backdrop_margin = 8.0;
// What sampling the backdrop costs a frame, per device pixel of the grown
// backplane image (EP-7): its copy into the blur's storage, six box passes
// of an add and a subtract per lane, and the copy out. Added to the
// effect's frame estimate when it is selected (check_effect_selection).
inline constexpr std::uint32_t effect_backdrop_operations_per_pixel = 16U;
// The half width of the box passes for a backdrop blur reaching `margin`
// logical units at display `scale`: floor(margin * scale / 3) device pixels.
[[nodiscard]] std::uint32_t effect_backdrop_half(double margin, double scale) noexcept;

class EffectRegistry final {
public:
    // Checks `entry` and adds it; anything but `registered` leaves the
    // registry unchanged. Looks up the plugin's verification record for it.
    EffectRegistrationOutcome add(const EffectEntry& entry) noexcept;
    // The entry registered under `name`, or null.
    [[nodiscard]] const EffectEntry* find(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] const EffectEntry& at(std::size_t index) const noexcept { return entries_[index]; }
    // How many registered effects carry no verification record.
    [[nodiscard]] std::size_t unverified_count() const noexcept;

private:
    std::array<EffectEntry, maximum_registered_effects> entries_{};
    std::size_t count_{};
};

// The registry of the toolkit's own effects, filled on first use (defined
// with the controls that host them).
[[nodiscard]] const EffectRegistry& built_in_effect_registry();

// The process's installed effect registry: the one a theme compiled without
// a registry of its own checks `effect` names against (E417), such as the
// user theme a process is started with (--theme, GUI_FORMS_THEME_SHEET),
// settled at its first window. gui_forms_core holds only the address and
// never names a registry: the installer owns the registry, which must stay
// alive until it is uninstalled (null) or the process ends, and the core
// reads it when a held request is settled, not when it is held. None is
// installed until a program installs one; with none, `effect` names are
// checked for their form only and E417 is skipped, which the request's
// report says. Replacing or uninstalling does not reach a compile already
// done. Any thread; settling reads it on the UI thread.
void install_effect_registry(const EffectRegistry* registry) noexcept;
[[nodiscard]] const EffectRegistry* installed_effect_registry() noexcept;

// Installs built_in_effect_registry() as the process's registry
// (install_effect_registry), which lives until the process ends (defined
// with the controls, so only a program linking them can call it). A program
// using the stock controls calls it once while it sets up, before its first
// window. Repeating it changes nothing.
void install_built_in_effect_registry();

// Whether the effect pass of the gui_forms Clang plugin verified the effect
// named `name`, of `state_size` bytes, whose run thunk is `run_thunk`: a
// record naming it with that state size is in the binary holding that code
// (design note §2). A record of another size (another build's effect of the
// same name) does not count.
[[nodiscard]] bool effect_verification_recorded(std::string_view name, std::size_t state_size,
                                                const void* run_thunk) noexcept;

// What effect registration found in this process, for diagnostics and the
// windows' metrics (MetricsSnapshot::effects_unverified): effects
// registered, and those registered unverified. The first unverified
// registration also writes one line to stderr naming it and saying why; the
// rest are only counted. Any thread.
struct EffectLoadDiagnostics final {
    std::uint64_t registered{};
    std::uint64_t unverified{};
    // Selections the load checks refused (note_effect_refused).
    std::uint64_t refused{};
};
[[nodiscard]] EffectLoadDiagnostics effect_load_diagnostics() noexcept;

// Who asked for the effect a control attaches to a part, in the order the
// control consults them (design note section 7): the application, by name
// (the control's `effect` property); the control's own visual style; the
// theme, by the part's `effect` declaration (effect_property).
enum class EffectSelectionSource : std::uint8_t { none, application, style, theme };

// The load checks' verdict on one selection (design note section 7). Any
// outcome but `selected` and `none` leaves the part to its primitive alone.
enum class EffectSelectionOutcome : std::uint8_t {
    selected,
    // Nothing asked for an effect, or the theme declared `none`.
    none,
    // No effect is registered under the name.
    unknown_effect,
    // The effect is registered for another parent type.
    parent_mismatch,
    // Its frame cost, its fixed operations plus its per-pixel operations
    // over the part and its outsets at the display scale, passes
    // maximum_effect_operations_per_frame.
    over_frame_budget,
    // A theme named an effect the plugin's effect pass did not verify. An
    // application may attach an unverified effect (and is warned at
    // registration); a theme may not.
    unverified,
};

[[nodiscard]] std::string_view effect_selection_source_name(EffectSelectionSource source) noexcept;
[[nodiscard]] std::string_view effect_selection_outcome_name(EffectSelectionOutcome outcome) noexcept;

// The most operations one effect frame may cost on its part: the bound an
// animated primitive's part has (maximum_animated_operations_per_frame,
// GIVEN F1-1).
inline constexpr std::uint64_t maximum_effect_operations_per_frame = 4'000'000U;

// What a control's effect selection came to, kept by the control for
// diagnostics: the name asked for (a copy, empty for none), who asked and
// the verdict.
struct EffectSelection final {
    std::string name{};
    EffectSelectionSource source{EffectSelectionSource::none};
    EffectSelectionOutcome outcome{EffectSelectionOutcome::none};

    friend bool operator==(const EffectSelection& left, const EffectSelection& right) noexcept {
        return left.name == right.name && left.source == right.source && left.outcome == right.outcome;
    }
};

// The load checks for the effect `name`, asked for by `source`, on a part
// of a control whose effect parent is `parent` (EffectView<P>::parent_name),
// `part` logical units in size at display `scale`. Sets `entry` to the
// registered entry when the outcome is `selected`, else to null. An empty
// name is `none`. Never fails.
[[nodiscard]] EffectSelectionOutcome check_effect_selection(const EffectRegistry& registry, std::string_view name,
                                                            EffectSelectionSource source, std::string_view parent,
                                                            Size part, double scale,
                                                            const EffectEntry*& entry) noexcept;

// Counts one refused selection (an outcome other than `selected` and
// `none`) in effect_load_diagnostics; the first in the process also writes
// one line to stderr naming the effect, who asked and why. A control calls
// it once per change of its selection, not once per paint. Any thread.
void note_effect_refused(const EffectSelection& selection) noexcept;

// What advancing an instance's time did.
struct EffectAdvance final {
    // Ticks now due for the next frame.
    std::uint32_t pending{};
    // Ticks skipped by this advance, past maximum_effect_ticks_per_frame.
    std::uint32_t skipped{};
};

// What an instance has counted over its life.
struct EffectInstanceCounters final {
    std::uint64_t ticks{};
    std::uint64_t skipped_ticks{};
    std::uint64_t frames{};
    std::uint64_t setups{};
    std::uint64_t refused_calls{};
    // Input events dropped: past maximum_effect_input_events before a frame.
    std::uint64_t dropped_input{};
};

class EffectInstance final {
public:
    EffectInstance() noexcept = default;
    ~EffectInstance();
    EffectInstance(const EffectInstance&) = delete;
    EffectInstance& operator=(const EffectInstance&) = delete;

    // Releases any effect held, then makes `entry`'s state, value-
    // initialized, with `seed`, its time starting at `now`. On an allocation
    // failure the instance is left empty and the exception propagates.
    void attach(const EffectEntry& entry, std::uint64_t seed, FrameTime now);
    // Releases the effect's state; the instance is empty.
    void detach() noexcept;
    [[nodiscard]] const EffectEntry* entry() const noexcept { return entry_; }

    // The engine clock reads `now` on an ambient frame: the ticks due since
    // the last advance are added to the pending ones, at most
    // maximum_effect_ticks_per_frame; the rest are skipped and counted.
    EffectAdvance advance(FrameTime now) noexcept;
    // Motion resumed after a pause: time restarts from `now`, with no
    // catch-up.
    void rebase(FrameTime now) noexcept;
    // Queues one input event for the next frame, if `granted` and the
    // effect's declaration both admit its kind (pointer events for
    // `pointer`, keys too for `pointer_and_keys`); otherwise it is ignored.
    // Past maximum_effect_input_events queued, it is counted and dropped.
    // Returns whether it was queued. Never fails.
    bool queue_input(const EffectInputEvent& event, EffectInputKind granted) noexcept;

    // The input the hosting control grants the effect (design note section
    // 6: none by default, set by the application through the control's
    // effect_input property). Kept across attach and detach.
    void set_input_grant(EffectInputKind grant) noexcept;
    [[nodiscard]] EffectInputKind input_grant() const noexcept { return input_grant_; }
    // Takes one pointer event the hosting control received, at `position`
    // in the control's coordinates, for a part whose origin there is
    // `part_origin`: converts it to part-local units, queues a move, press
    // or release under the grant (queue_input), and updates the standing
    // pointer state (EffectFrame::pointer) for every action, enter and
    // leave setting `inside`. Does nothing without an effect, a grant and
    // an effect declaring pointer input. Returns whether the effect's
    // frame changed (an event queued or the state moved), when the host
    // repaints the effect's extent. Never fails.
    bool note_pointer(const PointerEvent& event, Point part_origin) noexcept;
    // The standing pointer state the next frame is given.
    [[nodiscard]] EffectPointer pointer() const noexcept { return pointer_; }

    // Runs the effect's frame over `painter`, whose origin is the parent's,
    // for the part at `part` (logical, in the painter's coordinates) at
    // display `scale`. Runs setup first when due. `still` draws the still
    // pose and consumes no ticks. `view` must be the effect's parent's view.
    // `shape` is the part's shape, which EffectCanvas::clip_part_shape
    // clips to; by default a plain rectangle.
    // `host` is the control that paints the effect, through which a
    // backdrop is sampled from its window (EP-7); null for a paint with no
    // control, which lends no backdrop.
    template <typename Parent>
    void paint(Painter& painter, Rect part, double scale, bool still, const EffectView<Parent>& view,
               PartShape shape = PartShape{}, const Control* host = nullptr) {
        if (entry_ == nullptr || (*entry_).parent != EffectView<Parent>::parent_name) return;
        paint_view(painter, part, scale, still, &view, shape, host);
    }

    // The colours the next frames are given (EffectFrame::colours); the
    // host sets them before each paint. Kept across attach and detach.
    void set_colours(EffectColours colours) noexcept { colours_ = colours; }
    [[nodiscard]] EffectColours colours() const noexcept { return colours_; }

    // The state's bytes, hashed (FNV-1a): the motion checksum.
    [[nodiscard]] std::uint64_t state_checksum() const noexcept;
    [[nodiscard]] const EffectInstanceCounters& counters() const noexcept { return counters_; }
    [[nodiscard]] const EffectCanvasMeter& last_meter() const noexcept { return last_meter_; }

private:
    void paint_view(Painter& painter, Rect part, double scale, bool still, const void* view, PartShape shape,
                    const Control* host);
    // The blurred backplane under `layer` (the part and its outsets, in the
    // painter's coordinates) for an effect declaring a backdrop, sampled
    // through `host`'s window; null when none can be lent. Puts a
    // non-ambient effect on the window's backplane damage list.
    std::shared_ptr<const PartRaster> sample_backdrop(Painter& painter, const Control& host, Rect layer,
                                                      double scale);

    // What a kept backdrop was made from: the backplane's generation, the
    // raster's place in window units, its extent and scale and the blur.
    struct BackdropKey final {
        std::uint64_t generation{};
        Point origin{};
        DeviceExtent extent{};
        double scale{};
        std::uint32_t half{};

        friend constexpr bool operator==(const BackdropKey& left, const BackdropKey& right) noexcept {
            return left.generation == right.generation && left.origin == right.origin &&
                   left.extent == right.extent && left.scale == right.scale && left.half == right.half;
        }
    };

    const EffectEntry* entry_{};
    std::unique_ptr<std::byte, effect_detail::StorageRelease> storage_{};
    void* state_{};
    std::uint64_t seed_{};
    // Time: ticks are counted from `origin_`; `counted_` of them have been
    // made due (pending or run), `next_tick_` is the next one's index.
    FrameTime origin_{};
    std::uint64_t counted_{};
    std::uint64_t next_tick_{};
    std::uint32_t pending_{};
    // Input queued for the next frame, which receives and clears it.
    std::array<EffectInputEvent, maximum_effect_input_events> input_{};
    std::uint32_t input_count_{};
    // Setup: whether it ran, and the geometry it ran for.
    bool set_up_{};
    Size setup_part_{};
    double setup_scale_{};
    EffectInstanceCounters counters_{};
    EffectCanvasMeter last_meter_{};
    EffectColours colours_{};
    EffectInputKind input_grant_{EffectInputKind::none};
    EffectPointer pointer_{};
    // The backdrop (EP-7): the last blurred backplane, kept while its key
    // stands and refilled in place when no recorded command still holds
    // it; the blur's storage, reused from one blur to the next. Released
    // with the effect.
    std::shared_ptr<PartRaster> backdrop_{};
    std::optional<BackdropKey> backdrop_key_{};
    std::unique_ptr<engine_primitives::BoxBlurWork, effect_detail::BlurWorkRelease> blur_work_{};
};

} // namespace gui_forms
