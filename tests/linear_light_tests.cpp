// Linear-light gradient interpolation (owner, 2026-11-16;
// gui_forms/types/linear_light/linear_light.hpp): the integer sRGB tables,
// the interpolation of two colours, the stop expansion handed to Skia, and
// tritone's colour table. Every expected value was computed from the
// generated tables (tools/linear_light_tables.py) by an independent Python
// model of the same integer arithmetic.

#include "gui_forms/surface_material/colour_matrix/colour_matrix.hpp"
#include "gui_forms/types/linear_light/linear_light.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool same_colour(Color observed, Color expected) {
    const bool same = observed == expected;
    if (!same) {
        std::cerr << "observed " << static_cast<unsigned>(observed.red) << ',' << static_cast<unsigned>(observed.green)
                  << ',' << static_cast<unsigned>(observed.blue) << ',' << static_cast<unsigned>(observed.alpha)
                  << " expected " << static_cast<unsigned>(expected.red) << ','
                  << static_cast<unsigned>(expected.green) << ',' << static_cast<unsigned>(expected.blue) << ','
                  << static_cast<unsigned>(expected.alpha) << '\n';
    }
    return same;
}

// The conversions are constexpr: a table entry and its round trip compile.
static_assert(srgb_of_linear_light(linear_light_of_srgb(128U)) == 128U);
static_assert(linear_light_weight(0.5) == 32768U);

constexpr Color black{0U, 0U, 0U, 255U};
constexpr Color white{255U, 255U, 255U, 255U};
constexpr Color red{255U, 0U, 0U, 255U};
constexpr Color blue{0U, 0U, 255U, 255U};

void test_tables() {
    require(linear_light_of_srgb(0U).value == 0U, "black decodes to 0");
    require(linear_light_of_srgb(255U).value == 65535U, "white decodes to 65535");
    require(linear_light_of_srgb(1U).value == 20U, "byte 1 lies on the curve's linear segment");
    for (std::uint32_t value = 0U; value < 256U; ++value) {
        const std::uint8_t encoded = static_cast<std::uint8_t>(value);
        require(srgb_of_linear_light(linear_light_of_srgb(encoded)) == encoded, "every byte survives the round trip");
        if (value > 0U) {
            const std::uint8_t below = static_cast<std::uint8_t>(value - 1U);
            require(linear_light_of_srgb(below).value < linear_light_of_srgb(encoded).value,
                    "the decode table increases");
        }
    }
    require(srgb_of_linear_light(LinearLightLevel{65535U}) == 255U, "full intensity encodes to 255");
    require(srgb_of_linear_light(LinearLightLevel{9U}) == 0U, "below the first threshold encodes to 0");
    require(srgb_of_linear_light(LinearLightLevel{10U}) == 1U, "the first threshold encodes to 1");
}

void test_weights() {
    require(linear_light_weight(0.0) == 0U, "0 is weight 0");
    require(linear_light_weight(1.0) == linear_light_weight_one, "1 is the whole weight");
    require(linear_light_weight(-0.25) == 0U, "a negative fraction clamps to 0");
    require(linear_light_weight(2.0) == linear_light_weight_one, "a fraction past 1 clamps");
    require(linear_light_weight(std::numeric_limits<double>::quiet_NaN()) == 0U, "NaN is weight 0");
    require(linear_light_weight(0.25) == 16384U, "a quarter is 16384");
    require(linear_light_weight(0.5 / 65536.0) == 1U, "half a step rounds up");
}

void test_interpolation() {
    require(same_colour(interpolate_linear_light(black, white, 0U), black), "weight 0 is the first colour");
    require(same_colour(interpolate_linear_light(black, white, linear_light_weight_one), white),
            "the whole weight is the second colour");
    require(same_colour(interpolate_linear_light(red, red, 1234U), red), "equal colours give that colour");
    // The middle of black to white is 188 in linear light (128 in sRGB space).
    require(same_colour(interpolate_linear_light(black, white, 32768U), Color{188U, 188U, 188U, 255U}),
            "black to white is 188 at the middle");
    require(same_colour(interpolate_linear_light(black, white, 16384U), Color{137U, 137U, 137U, 255U}),
            "black to white is 137 at a quarter");
    require(same_colour(interpolate_linear_light(red, blue, 32768U), Color{188U, 0U, 188U, 255U}),
            "red to blue passes through a bright purple");
    // Premultiplied: fading to transparent keeps the colour.
    require(same_colour(interpolate_linear_light(red, Color{255U, 0U, 0U, 0U}, 32768U), Color{255U, 0U, 0U, 128U}),
            "fading to transparent keeps the colour");
    require(same_colour(interpolate_linear_light(red, Color{0U, 0U, 0U, 0U}, 32768U), Color{255U, 0U, 0U, 128U}),
            "a transparent stop's colour takes no part");
    require(same_colour(interpolate_linear_light(Color{10U, 20U, 30U, 0U}, Color{40U, 50U, 60U, 0U}, 32768U),
                        Color{0U, 0U, 0U, 0U}),
            "two transparent stops give transparent black");
    require(same_colour(interpolate_linear_light(black, Color{128U, 128U, 128U, 255U}, 32897U),
                        Color{93U, 93U, 93U, 255U}),
            "black to grey at 32897");
}

// Premultiplied sRGB at `t` from the expanded stops, as Skia interpolates
// them, in units of an 8-bit step.
double chord_channel(std::span<const GradientStop> stops, double t, std::size_t channel) {
    std::size_t next = 1U;
    while (next + 1U < stops.size() && t > stops[next].offset) next = next + 1U;
    const GradientStop& low = stops[next - 1U];
    const GradientStop& high = stops[next];
    const double f = (t - low.offset) / (high.offset - low.offset);
    const std::array<std::uint8_t, 3> low_channels{low.color.red, low.color.green, low.color.blue};
    const std::array<std::uint8_t, 3> high_channels{high.color.red, high.color.green, high.color.blue};
    const double from = static_cast<double>(low_channels[channel]) * static_cast<double>(low.color.alpha) / 255.0;
    const double to = static_cast<double>(high_channels[channel]) * static_cast<double>(high.color.alpha) / 255.0;
    const double value = from + (to - from) * f;
    return value;
}

void test_expansion_follows(Color from, Color to, std::size_t expected_count) {
    const std::array<GradientStop, 2> stops{{{0.0, from}, {1.0, to}}};
    std::array<GradientStop, maximum_linear_light_stops> expanded{};
    const std::size_t count = expand_linear_light_stops(stops, expanded);
    require(count == expected_count, "the expansion's stop count");
    require(expanded[0] == stops[0] && expanded[count - 1U] == stops[1], "the original stops are kept");
    for (std::size_t index = 1U; index < count; ++index) {
        require(expanded[index].offset > expanded[index - 1U].offset, "added stops increase strictly");
    }
    const std::span<const GradientStop> written(expanded.data(), count);
    double worst = 0.0;
    for (std::uint32_t sample = 0U; sample <= 1000U; ++sample) {
        const double t = static_cast<double>(sample) / 1000.0;
        const Color exact = interpolate_linear_light(from, to, linear_light_weight(t));
        const std::array<std::uint8_t, 3> exact_channels{exact.red, exact.green, exact.blue};
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
            const double reference =
                static_cast<double>(exact_channels[channel]) * static_cast<double>(exact.alpha) / 255.0;
            const double difference = std::fabs(chord_channel(written, t, channel) - reference);
            if (difference > worst) worst = difference;
        }
    }
    require(worst <= 1.3, "the expanded stops follow the linear-light gradient within 1.3 steps");
}

void test_expansion() {
    // Black to white: 17 stops, the dark end densest.
    const std::array<GradientStop, 2> grey{{{0.0, black}, {1.0, white}}};
    std::array<GradientStop, maximum_linear_light_stops> expanded{};
    const std::size_t count = expand_linear_light_stops(grey, expanded);
    require(count == 17U, "black to white expands to 17 stops");
    require(expanded[14].offset == 0.5 && same_colour(expanded[14].color, Color{188U, 188U, 188U, 255U}),
            "the middle stop is 188 grey at 0.5");
    require(expanded[1].offset == 1.0 / 256.0 && same_colour(expanded[1].color, Color{13U, 13U, 13U, 255U}),
            "the first added stop is 13 grey at 1/256");
    test_expansion_follows(black, white, 17U);
    test_expansion_follows(red, blue, 29U);
    test_expansion_follows(red, Color{255U, 0U, 0U, 0U}, 2U);
    test_expansion_follows(Color{128U, 128U, 128U, 255U}, Color{132U, 132U, 132U, 255U}, 2U);

    // Hard stops and solid spans add nothing.
    const std::array<GradientStop, 4> hard{{{0.0, red}, {0.5, red}, {0.5, blue}, {1.0, blue}}};
    require(expand_linear_light_stops(hard, expanded) == 4U, "hard stops and solid spans add no stops");
    require(expanded[1] == hard[1] && expanded[2] == hard[2], "the hard stop pair is kept");

    // Too little room for the stops themselves, or none: nothing written.
    std::array<GradientStop, 1> short_room{};
    require(expand_linear_light_stops(grey, short_room) == 0U, "too little room writes nothing");
    require(expand_linear_light_stops(std::span<const GradientStop>{}, expanded) == 0U, "no stops write nothing");
    // Exactly the stops' own room: they are copied, none added.
    std::array<GradientStop, 2> exact_room{};
    require(expand_linear_light_stops(grey, exact_room) == 2U, "the stops' own room copies them");

    // 32 alternating stops fill the capacity; every original stop is kept,
    // in order.
    std::array<GradientStop, maximum_gradient_stops> alternating{};
    for (std::size_t index = 0U; index < alternating.size(); ++index) {
        const double offset = static_cast<double>(index) / static_cast<double>(alternating.size() - 1U);
        alternating[index] = GradientStop{offset, index % 2U == 0U ? black : white};
    }
    const std::size_t filled = expand_linear_light_stops(alternating, expanded);
    require(filled == maximum_linear_light_stops, "alternating stops fill the capacity");
    std::size_t found = 0U;
    for (std::size_t index = 0U; index < filled; ++index) {
        if (found < alternating.size() && expanded[index] == alternating[found]) found = found + 1U;
        if (index > 0U) require(expanded[index].offset >= expanded[index - 1U].offset, "the stops stay in order");
    }
    require(found == alternating.size(), "every original stop is kept");
}

void test_duotone() {
    const MaterialColourTable table = MaterialColourTable::duotone(black, white);
    const std::size_t levels = MaterialColourTable::levels;
    require(table.entries[0] == 0U && table.entries[255] == 255U, "dark at 0, light at 255");
    require(table.entries[64] == 137U && table.entries[128] == 188U, "a quarter and a half way in linear light");
    for (std::size_t level = 0U; level < levels; ++level) {
        require(table.entries[3U * levels + level] == level, "duotone keeps alpha");
    }
}

void test_tritone() {
    const Color grey{128U, 128U, 128U, 255U};
    const MaterialColourTable table = MaterialColourTable::tritone(black, grey, white);
    const std::size_t levels = MaterialColourTable::levels;
    require(table.entries[0] == 0U && table.entries[255] == 255U, "the shadow at 0, the highlight at 255");
    require(table.entries[64] == 93U, "level 64 is 93, a quarter in linear light");
    require(table.entries[127] == 128U && table.entries[128] == 129U, "the midtone at the segments' join");
    require(table.entries[192] == 205U, "level 192 is 205");
    require(table.entries[levels + 64U] == 93U && table.entries[2U * levels + 64U] == 93U,
            "green and blue match red for a grey ramp");
    for (std::size_t level = 0U; level < levels; ++level) {
        require(table.entries[3U * levels + level] == level, "tritone keeps alpha");
    }
}

} // namespace

int main() {
    test_tables();
    test_weights();
    test_interpolation();
    test_expansion();
    test_duotone();
    test_tritone();
    std::cout << "linear light tests passed\n";
    return 0;
}
