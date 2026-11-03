#pragma once

// Effect programs (owner contract, 2026-11-02; design note
// desks/coordinator/EFFECT_PROGRAMS_DESIGN.md). An effect is one class with
// one per-frame function, `run`, and an optional bounded `setup`, templated
// on the control it draws over (its parent). Its members are its persistent
// state, so its memory is sizeof, known when it is compiled. It reads the
// parent's published state through EffectView<Parent>, a snapshot of named
// values; time and input arrive as arguments; it draws through EffectCanvas
// alone, in part-local logical units.
//
// Boundedness is by construction, enforced by the gui_forms Clang plugin's
// effect pass: no while, do or goto; every loop over bounded_range<N>, an
// std::array or an EffectArray, or a counted for with constant bounds; no
// recursion, calls through pointers or virtual functions, allocation,
// globals other than constexpr ones, I/O, clocks or host randomness; and
// calls only to functions that obey the same rules or to the vocabulary
// (GUI_FORMS_EFFECT_VOCABULARY), whose declared costs the plugin adds up
// against the effect's budget. An effect built without the plugin still
// runs, labelled unverified (EffectEntry::verified).
//
// This header is everything an effect's own code needs besides the canvas
// (gui_forms/engine/effect_canvas/effect_canvas.hpp) and its parent's view.

#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

// Marks a vocabulary function and its declared cost: `fixed` operations per
// call, and `per_pixel` operations for each device pixel the call covers
// (the canvas meters the latter at run time). Read by the plugin only.
#if defined(__clang__)
#define GUI_FORMS_EFFECT_VOCABULARY(fixed, per_pixel) \
    [[clang::annotate("gui_forms.effect_vocabulary", fixed, per_pixel)]]
#else
#define GUI_FORMS_EFFECT_VOCABULARY(fixed, per_pixel)
#endif

namespace gui_forms {

// The fixed tick every effect's time advances by: 1/60 s (owner decision
// 4(a)). Ticks are counted from the engine clock, never the wall clock.
inline constexpr std::uint32_t effect_ticks_per_second = 60U;
// The most ticks one frame advances (owner decision 2(a)); a longer stall
// skips the rest, and the skip is counted.
inline constexpr std::uint32_t maximum_effect_ticks_per_frame = 4U;
// The most input events one frame carries; the rest are counted, not kept.
inline constexpr std::size_t maximum_effect_input_events = 8U;
// The most stops an effect's gradient holds.
inline constexpr std::size_t maximum_effect_gradient_stops = 6U;
// The largest state any effect may declare, in bytes.
inline constexpr std::size_t maximum_effect_state_bytes = 16384U;

// At most Capacity passes: `count` clamped to Capacity. The only loop shape,
// with std::array and EffectArray, whose bound the plugin knows.
template <std::size_t Capacity>
class BoundedRange final {
public:
    class Iterator final {
    public:
        explicit constexpr Iterator(std::size_t index) noexcept : index_(index) {}
        [[nodiscard]] constexpr std::size_t operator*() const noexcept { return index_; }
        constexpr Iterator& operator++() noexcept {
            index_ = index_ + 1U;
            return *this;
        }
        friend constexpr bool operator==(const Iterator& left, const Iterator& right) noexcept {
            return left.index_ == right.index_;
        }

    private:
        std::size_t index_;
    };

    explicit constexpr BoundedRange(std::size_t count) noexcept : count_(count < Capacity ? count : Capacity) {}
    [[nodiscard]] constexpr Iterator begin() const noexcept { return Iterator(0U); }
    [[nodiscard]] constexpr Iterator end() const noexcept { return Iterator(count_); }

private:
    std::size_t count_;
};

template <std::size_t Capacity>
[[nodiscard]] constexpr BoundedRange<Capacity> bounded_range(std::size_t count) noexcept {
    const BoundedRange<Capacity> range(count);
    return range;
}

// A fixed-capacity array with a live count: `items` holds Capacity values,
// the first `count` of them live. State an effect keeps between frames.
template <typename Item, std::size_t Capacity>
struct EffectArray final {
    static_assert(std::is_trivially_copyable_v<Item>, "effect state must be trivially copyable");
    static constexpr std::size_t capacity = Capacity;

    std::array<Item, Capacity> items{};
    std::uint32_t count{};

    friend constexpr bool operator==(const EffectArray& left, const EffectArray& right) noexcept {
        return left.count == right.count && left.items == right.items;
    }
};

// The engine's deterministic generator (owner amendment, 2026-11-02): a
// counter-based hash of (seed, stream, counter), SplitMix64's finalizer
// over their mix, with no host randomness and no hidden state beyond the
// counter in this value. Bit-exact on every host; known-answer vectors are
// in the tests.
class EffectRandom final {
public:
    constexpr EffectRandom(std::uint64_t seed, std::uint64_t stream) noexcept : seed_(seed), stream_(stream) {}

    // The stream setup draws from.
    static constexpr std::uint64_t setup_stream = 0xffffffffffffffffULL;

    // The hash itself: the same three inputs give the same bits.
    [[nodiscard]] static constexpr std::uint64_t hash(std::uint64_t seed, std::uint64_t stream,
                                                      std::uint64_t counter) noexcept {
        std::uint64_t mixed = seed ^ (stream * 0x9e3779b97f4a7c15ULL) ^ (counter * 0xd1b54a32d192ed03ULL);
        mixed = mixed + 0x9e3779b97f4a7c15ULL;
        mixed = (mixed ^ (mixed >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        mixed = (mixed ^ (mixed >> 27U)) * 0x94d049bb133111ebULL;
        mixed = mixed ^ (mixed >> 31U);
        return mixed;
    }

    // The next 64 bits.
    GUI_FORMS_EFFECT_VOCABULARY(8, 0)
    constexpr std::uint64_t next() noexcept {
        const std::uint64_t value = hash(seed_, stream_, counter_);
        counter_ = counter_ + 1U;
        return value;
    }
    // In [0, 1): the top 53 bits, exactly representable.
    GUI_FORMS_EFFECT_VOCABULARY(10, 0)
    constexpr double next_unit() noexcept {
        const std::uint64_t bits = next() >> 11U;
        const double unit = static_cast<double>(bits) * (1.0 / 9007199254740992.0);
        return unit;
    }
    // In [0, bound): the high half of a 32 × 32-bit product; 0 when bound
    // is 0.
    GUI_FORMS_EFFECT_VOCABULARY(10, 0)
    constexpr std::uint32_t next_below(std::uint32_t bound) noexcept {
        const std::uint64_t bits = next() >> 32U;
        const std::uint64_t scaled = bits * static_cast<std::uint64_t>(bound);
        const std::uint32_t value = static_cast<std::uint32_t>(scaled >> 32U);
        return value;
    }
    // In [low, high).
    GUI_FORMS_EFFECT_VOCABULARY(12, 0)
    constexpr double next_between(double low, double high) noexcept {
        const double unit = next_unit();
        const double value = low + (high - low) * unit;
        return value;
    }

private:
    std::uint64_t seed_;
    std::uint64_t stream_;
    std::uint64_t counter_{};
};

// The default seed of an effect on a control: FNV-1a over the control's
// stable ID, a separator and the effect's name, so runs, tests and motion
// checksums reproduce. An application may set its own (effect_seed).
[[nodiscard]] constexpr std::uint64_t effect_default_seed(std::string_view stable_id,
                                                          std::string_view effect_name) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char character : stable_id) {
        hash = (hash ^ static_cast<std::uint8_t>(character)) * 0x100000001b3ULL;
    }
    hash = (hash ^ 0x1fU) * 0x100000001b3ULL;
    for (const char character : effect_name) {
        hash = (hash ^ static_cast<std::uint8_t>(character)) * 0x100000001b3ULL;
    }
    return hash;
}

// Effect arithmetic: the parts of <cmath> an effect may use, without the
// host's libm or fused multiply-add (the plugin rejects <cmath>).

// x minus the largest integer not above it, for x strictly between -2^52 and
// 2^52; 0 otherwise.
GUI_FORMS_EFFECT_VOCABULARY(6, 0)
[[nodiscard]] constexpr double effect_fraction(double x) noexcept {
    if (!(x > -4503599627370496.0 && x < 4503599627370496.0)) return 0.0;
    const double truncated = static_cast<double>(static_cast<std::int64_t>(x));
    const double floor = truncated > x ? truncated - 1.0 : truncated;
    const double fraction = x - floor;
    return fraction;
}

GUI_FORMS_EFFECT_VOCABULARY(2, 0)
[[nodiscard]] constexpr double effect_clamp(double x, double low, double high) noexcept {
    const double raised = x < low ? low : x;
    const double clamped = raised > high ? high : raised;
    return clamped;
}

GUI_FORMS_EFFECT_VOCABULARY(3, 0)
[[nodiscard]] constexpr double effect_mix(double a, double b, double t) noexcept {
    const double mixed = a + (b - a) * t;
    return mixed;
}

// 0 below edge0, 1 above edge1, u²(3 − 2u) between; equal edges give 0.
GUI_FORMS_EFFECT_VOCABULARY(10, 0)
[[nodiscard]] constexpr double effect_smoothstep(double edge0, double edge1, double x) noexcept {
    if (!(edge1 != edge0)) return 0.0;
    const double u = effect_clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    const double eased = u * u * (3.0 - 2.0 * u);
    return eased;
}

// The sine of x radians (engine_sine_cosine: the same bits on every host).
GUI_FORMS_EFFECT_VOCABULARY(40, 0)
[[nodiscard]] inline double effect_sine(double x) noexcept {
    const EngineSineCosine both = engine_sine_cosine(x);
    return both.sine;
}

// A colour with its alpha scaled by `factor`, clamped to [0, 1].
GUI_FORMS_EFFECT_VOCABULARY(6, 0)
[[nodiscard]] constexpr Color effect_faded(Color color, double factor) noexcept {
    const double alpha = effect_clamp(static_cast<double>(color.alpha) * factor, 0.0, 255.0);
    const Color faded = Color::rgba(color.red, color.green, color.blue, static_cast<std::uint8_t>(alpha));
    return faded;
}

// When an effect runs.
enum class EffectTimeModel : std::uint8_t {
    // On a geometry or state change only; never ticked.
    static_pose,
    // Driven by the part's transition: EffectFrame::transition in [0, 1].
    transition,
    // Ticked on the motion clock's ambient path, within the ambient caps.
    ambient,
};

// The input an effect may receive, and only when its parent grants it (the
// parent control's effect_input property): the lesser of the grant and the
// effect's declaration. Tab, Shift+Tab, Escape, access keys, Help and
// system keys never reach an effect, and a parent never takes focus for
// one, so an effect can never trap focus.
enum class EffectInputKind : std::uint8_t {
    none,
    pointer,
    pointer_and_keys,
};

// The names the control schema and the effect_input property spell these
// with.
constexpr std::string_view effect_time_model_name(EffectTimeModel model) noexcept {
    switch (model) {
    case EffectTimeModel::static_pose:
        return "static_pose";
    case EffectTimeModel::transition:
        return "transition";
    case EffectTimeModel::ambient:
        return "ambient";
    }
    return "ambient";
}

constexpr std::string_view effect_input_name(EffectInputKind input) noexcept {
    switch (input) {
    case EffectInputKind::none:
        return "none";
    case EffectInputKind::pointer:
        return "pointer";
    case EffectInputKind::pointer_and_keys:
        return "pointer_and_keys";
    }
    return "none";
}

enum class EffectInputEventKind : std::uint8_t {
    pointer_moved,
    pointer_pressed,
    pointer_released,
    key_pressed,
};

// One granted input event: the pointer in part-local logical units, or a
// key's code (KeyCode's value).
struct EffectInputEvent final {
    EffectInputEventKind kind{};
    Point position{};
    std::uint32_t key{};

    friend constexpr bool operator==(const EffectInputEvent& left, const EffectInputEvent& right) noexcept {
        return left.kind == right.kind && left.position == right.position && left.key == right.key;
    }
};

// What one frame of `run` is given. `ticks` fixed ticks are due, the first
// of them numbered `first_tick` since the effect was set up; zero when the
// part repaints for another reason, so state advances once per due tick
// however often the part repaints. `still` is true while motion is paused
// (reduced motion, an occluded window, the motion switch): the effect draws
// its still pose and advances nothing. A random draw for tick t uses
// EffectRandom(seed, t), so draws do not depend on how ticks were grouped
// into frames.
struct EffectFrame final {
    std::uint32_t ticks{};
    std::uint64_t first_tick{};
    double transition{};
    bool still{};
    std::uint64_t seed{};
    // The part's logical size and the display scale (device pixels per
    // logical unit).
    Size part{};
    double scale{1.0};
    std::array<EffectInputEvent, maximum_effect_input_events> input{};
    std::uint32_t input_count{};
};

// What `setup` is given: once when the effect is instantiated on a part,
// and again on a geometry change if the effect declares setup_on_geometry.
// Its draws come from EffectRandom(seed, EffectRandom::setup_stream).
struct EffectSetup final {
    std::uint64_t seed{};
    Size part{};
    double scale{1.0};
};

// An effect's per-frame budget: `fixed` operations (proved by the plugin's
// static bound) plus `per_device_pixel` operations for each device pixel of
// the part and its outsets (metered by the canvas, which refuses drawing
// past it; owner decision 1(a)).
struct EffectCost final {
    std::uint32_t fixed{};
    std::uint32_t per_device_pixel{};

    friend constexpr bool operator==(const EffectCost& left, const EffectCost& right) noexcept {
        return left.fixed == right.fixed && left.per_device_pixel == right.per_device_pixel;
    }
};

// What an effect is, as its class declares it in `static constexpr
// EffectDeclaration declaration`.
struct EffectDeclaration final {
    // Registered (snake_case) name, as themes and applications write it.
    std::string_view name{};
    // The engine version that introduced the effect.
    std::uint32_t engine_version{1U};
    EffectTimeModel time_model{EffectTimeModel::ambient};
    EffectCost cost{};
    // setup's own budget, in operations; a one-off, never charged to a
    // frame. Zero for an effect without setup.
    std::uint32_t setup_cost{};
    bool setup_on_geometry{};
    // The most bytes the effect's state may take; sizeof must not exceed it.
    std::size_t state_budget{};
    // How far the effect draws outside its part, in logical units. The
    // engine clips to them, damages them and composites them source-over in
    // the parent's paint order.
    Insets outsets{};
    EffectInputKind input{EffectInputKind::none};
    // Its ambient frame rate, at most the ambient cap (30).
    double frames_per_second{30.0};
};

// The read-only view an effect has of its parent: specialized once per
// parent type, as a struct of named values captured when the frame runs.
template <typename Parent>
struct EffectView;

class EffectCanvas;

// What an effect class must provide. Its state must be trivially copyable
// (no pointers, references or containers, so no borrow outlives its owner,
// and a test can checksum it), within its declared budget, and default
// constructible (the engine value-initializes it in storage it owns).
template <typename Effect>
concept EffectProgram =
    std::is_trivially_copyable_v<Effect> && std::is_default_constructible_v<Effect> &&
    std::same_as<std::remove_cv_t<decltype(Effect::declaration)>, EffectDeclaration> &&
    requires(Effect effect, const EffectView<typename Effect::parent_type>& view, const EffectFrame& frame,
             EffectCanvas& canvas) {
        effect.run(view, frame, canvas);
    };

template <typename Effect>
concept EffectWithSetup = EffectProgram<Effect> &&
    requires(Effect effect, const EffectView<typename Effect::parent_type>& view, const EffectSetup& setup) {
        effect.setup(view, setup);
    };

} // namespace gui_forms
