#include "gui_forms/types/linear_light/linear_light.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// Integer arithmetic throughout, apart from linear_light_weight's exact
// scaling and the added stops' offsets: no fused multiply-add can change a
// colour, so no contraction pragma is needed.

namespace gui_forms {

namespace {

// Half an 8-bit step, on the scale the chord test compares: twice a
// premultiplied channel (channel × alpha, at most 255 × 255), so half of
// one step of 255 is 255 again.
constexpr std::int64_t chord_tolerance = 255;

// How many times a span between two stops is halved at most: 1/256 of the
// span, finer than any span's curvature needs.
constexpr std::uint32_t maximum_split_depth = 8U;

std::int64_t premultiplied_channel(std::uint8_t channel, std::uint8_t alpha) noexcept {
    const std::int64_t product = static_cast<std::int64_t>(channel) * static_cast<std::int64_t>(alpha);
    return product;
}

// Whether the middle of a span is within half a step of its ends'
// premultiplied sRGB chord in one channel.
bool channel_follows(std::int64_t low, std::int64_t middle, std::int64_t high) noexcept {
    const std::int64_t difference = 2 * middle - low - high;
    const bool follows = difference <= chord_tolerance && difference >= -chord_tolerance;
    return follows;
}

// Whether a renderer interpolating premultiplied sRGB from `low` to `high`
// reaches `middle` (the exact linear-light colour) at the span's middle,
// within half a step in every colour channel. Alpha interpolates linearly
// in both and is not compared.
bool chord_follows(Color low, Color middle, Color high) noexcept {
    const bool red = channel_follows(premultiplied_channel(low.red, low.alpha),
                                     premultiplied_channel(middle.red, middle.alpha),
                                     premultiplied_channel(high.red, high.alpha));
    const bool green = channel_follows(premultiplied_channel(low.green, low.alpha),
                                       premultiplied_channel(middle.green, middle.alpha),
                                       premultiplied_channel(high.green, high.alpha));
    const bool blue = channel_follows(premultiplied_channel(low.blue, low.alpha),
                                      premultiplied_channel(middle.blue, middle.alpha),
                                      premultiplied_channel(high.blue, high.alpha));
    const bool follows = red && green && blue;
    return follows;
}

// The expanded stops being written: `count` written so far into `out`, and
// `reserved` original stops still to come, for which room is always kept.
struct StopWriter final {
    std::span<GradientStop> out{};
    std::size_t count{};
    std::size_t reserved{};
};

bool room_to_add(const StopWriter& writer) noexcept {
    const bool room = writer.count + writer.reserved < writer.out.size();
    return room;
}

// One span between the original stops `low` and `high`: the part from
// weight `from` (colour `from_colour`) to weight `to` (`to_colour`), split
// at its middle when the chord strays, the left half's stops first, then
// the middle, then the right half's, so the stops stay in order.
struct SpanPart final {
    std::uint32_t from{};
    Color from_colour{};
    std::uint32_t to{};
    Color to_colour{};
    std::uint32_t depth{};
};

void split_span(const GradientStop& low, const GradientStop& high, SpanPart part, StopWriter& writer) noexcept {
    if (part.depth >= maximum_split_depth || part.to - part.from < 2U) return;
    if (!room_to_add(writer)) return;
    const std::uint32_t middle = part.from + (part.to - part.from) / 2U;
    const Color middle_colour = interpolate_linear_light(low.color, high.color, middle);
    if (chord_follows(part.from_colour, middle_colour, part.to_colour)) return;
    const SpanPart left{part.from, part.from_colour, middle, middle_colour, part.depth + 1U};
    split_span(low, high, left, writer);
    if (!room_to_add(writer)) return;
    const double span = high.offset - low.offset;
    const double fraction = static_cast<double>(middle) / static_cast<double>(linear_light_weight_one);
    const double along = span * fraction;
    const double offset = low.offset + along;
    writer.out[writer.count] = GradientStop{offset, middle_colour};
    writer.count = writer.count + 1U;
    const SpanPart right{middle, middle_colour, part.to, part.to_colour, part.depth + 1U};
    split_span(low, high, right, writer);
}

} // namespace

std::size_t expand_linear_light_stops(std::span<const GradientStop> stops,
                                      std::span<GradientStop> expanded) noexcept {
    if (stops.empty() || expanded.size() < stops.size()) return 0U;
    StopWriter writer{expanded, 0U, stops.size()};
    writer.out[0] = stops[0];
    writer.count = 1U;
    writer.reserved = stops.size() - 1U;
    for (std::size_t index = 1U; index < stops.size(); ++index) {
        const GradientStop& low = stops[index - 1U];
        const GradientStop& high = stops[index];
        const bool varies = high.offset > low.offset && !(low.color == high.color);
        if (varies) {
            const SpanPart whole{0U, low.color, linear_light_weight_one, high.color, 0U};
            split_span(low, high, whole, writer);
        }
        writer.out[writer.count] = high;
        writer.count = writer.count + 1U;
        writer.reserved = writer.reserved - 1U;
    }
    return writer.count;
}

} // namespace gui_forms
