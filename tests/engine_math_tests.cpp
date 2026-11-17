// The engine's deterministic elementary functions (T2 plan, phase E1): their
// error against the host library stays inside the stated bounds, their exact
// cases are exact, and fixed tables of results (the sine and cosine of E3
// among them) have the same bits on every CI platform.
// GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints the observed hashes instead of
// checking them.

#include "gui_forms/engine/engine_math/engine_math.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool print_mode() {
    const char* const value = std::getenv("GUI_FORMS_ENGINE_IDENTITY_PRINT");
    const bool printing = value != nullptr && std::strcmp(value, "1") == 0;
    return printing;
}

double relative_error(double observed, double expected) {
    const double difference = std::fabs(observed - expected);
    const double magnitude = std::fabs(expected);
    const double error = magnitude == 0.0 ? difference : difference / magnitude;
    return error;
}

// FNV-1a over the bytes of each double, in order.
struct BitHash final {
    std::uint64_t value{14695981039346656037ULL};

    void add(double number) {
        std::uint64_t bits = 0U;
        std::memcpy(&bits, &number, sizeof bits);
        for (int shift = 0; shift < 64; shift += 8) {
            const std::uint64_t byte = (bits >> shift) & 0xFFU;
            value = value ^ byte;
            value = value * 1099511628211ULL;
        }
    }
};

void test_logarithm() {
    for (int step = 1; step <= 4000; ++step) {
        const double x = static_cast<double>(step) * 0.00137;
        const double observed = engine_logarithm(x);
        const double expected = std::log(x);
        const double allowed = 1e-15 * (1.0 + std::fabs(expected));
        require(std::fabs(observed - expected) <= allowed, "logarithm within its bound on (0, 5.5]");
    }
    const double wide[] = {1e-300, 1e-100, 3.5e-20, 7.25e15, 1e100, 1e300};
    for (const double x : wide) {
        const double expected = std::log(x);
        const double allowed = 1e-15 * (1.0 + std::fabs(expected));
        require(std::fabs(engine_logarithm(x) - expected) <= allowed, "logarithm within its bound far from 1");
    }
    require(engine_logarithm(1.0) == 0.0, "ln 1 is exactly 0");
    require(std::isinf(engine_logarithm(0.0)) && engine_logarithm(0.0) < 0.0, "ln 0 is minus infinity");
    require(std::isnan(engine_logarithm(-1.0)), "ln of a negative number is NaN");
    require(std::isnan(engine_logarithm(std::numeric_limits<double>::quiet_NaN())), "ln NaN is NaN");
}

void test_exponential() {
    for (int step = -7000; step <= 7000; ++step) {
        const double x = static_cast<double>(step) * 0.1;
        const double observed = engine_exponential(x);
        const double expected = std::exp(x);
        require(relative_error(observed, expected) <= 1e-14, "exponential within its bound on [-700, 700]");
    }
    require(engine_exponential(0.0) == 1.0, "e^0 is exactly 1");
    require(std::isinf(engine_exponential(710.0)), "e^710 overflows to infinity");
    require(engine_exponential(-746.0) == 0.0, "e^-746 underflows to zero");
}

void test_power() {
    const double exponents[] = {0.5, 1.0, 2.2, 3.7, 8.0};
    for (const double exponent : exponents) {
        for (int step = 1; step <= 1000; ++step) {
            const double base = static_cast<double>(step) / 1000.0;
            const double observed = engine_power(base, exponent);
            const double expected = std::pow(base, exponent);
            require(relative_error(observed, expected) <= 1e-13, "power within its bound on (0, 1]");
        }
    }
    require(engine_power(1.0, 2.2) == 1.0, "1^q is exactly 1");
    require(engine_power(0.0, 2.2) == 0.0, "0^q is exactly 0 for q > 0");
    require(engine_power(0.37, 0.0) == 1.0, "b^0 is exactly 1");
    require(engine_power(2.0, 3.0) == 8.0 || relative_error(engine_power(2.0, 3.0), 8.0) <= 1e-15,
            "2^3 is 8 within one rounding");
    require(std::isnan(engine_power(-0.5, 2.0)), "a negative base is NaN");
}

void test_sine_cosine() {
    const EngineSineCosine zero = engine_sine_cosine(0.0);
    require(zero.sine == 0.0 && zero.cosine == 1.0, "sine and cosine of 0 are exact");
    for (int step = -200000; step <= 200000; ++step) {
        const double x = static_cast<double>(step) * 0.00517;
        const EngineSineCosine observed = engine_sine_cosine(x);
        require(std::fabs(observed.sine - std::sin(x)) <= 2e-15 && std::fabs(observed.cosine - std::cos(x)) <= 2e-15,
                "sine and cosine within their bound on [-1034, 1034]");
    }
    for (int step = 1; step <= 2000; ++step) {
        const double x = static_cast<double>(step) * 524.287;
        const EngineSineCosine observed = engine_sine_cosine(x);
        require(std::fabs(observed.sine - std::sin(x)) <= 1e-14 && std::fabs(observed.cosine - std::cos(x)) <= 1e-14,
                "sine and cosine within their bound up to 2^20");
    }
    // Above 2^20 the table reduction: against the exact values (computed
    // with 1600-bit integer arithmetic), to the whole finite range.
    struct LargeAngle final {
        double x{};
        double sine{};
        double cosine{};
    };
    const LargeAngle large[] = {
        {1048576.0, 0.33049314002173469, 0.94380839390131199},
        {1073741824.0, -0.61732641504604213, 0.78670712294118816},
        {1e300, -0.81788191211590855, -0.57538611195754907},
        {-1e300, 0.81788191211590855, -0.57538611195754907},
        {std::numeric_limits<double>::max(), 0.004961954789184062, -0.99998768942655991},
    };
    for (const LargeAngle& angle : large) {
        const EngineSineCosine observed = engine_sine_cosine(angle.x);
        require(std::fabs(observed.sine - angle.sine) <= 1e-15 && std::fabs(observed.cosine - angle.cosine) <= 1e-15,
                "2^20, 2^30, 1e300 and the largest double reduce exactly");
    }
    // The double nearest a multiple of π/2 (Kahan and McDonald): its cosine,
    // −4.687e-19, needs the reduction's full precision.
    const EngineSineCosine nearest = engine_sine_cosine(6381956970095103.0 * std::ldexp(1.0, 797));
    require(relative_error(nearest.cosine, -4.6871659242546277e-19) <= 1e-14 && nearest.sine == 1.0,
            "the worst case for reduction keeps its relative precision");
    for (int step = 1; step <= 2000; ++step) {
        const double x = std::ldexp(1.0 + static_cast<double>(step) / 2048.0, 21 + step % 1000);
        // 2x is exact, so the double-angle identities test the reduction at
        // two exponents without the host library.
        const EngineSineCosine single = engine_sine_cosine(x);
        const EngineSineCosine twice = engine_sine_cosine(2.0 * x);
        const double double_sine = 2.0 * single.sine * single.cosine;
        const double double_cosine = single.cosine * single.cosine - single.sine * single.sine;
        require(std::fabs(twice.sine - double_sine) <= 8e-15 && std::fabs(twice.cosine - double_cosine) <= 8e-15,
                "the table reduction keeps the double-angle identities up to 2^1021");
    }
    const EngineSineCosine infinite = engine_sine_cosine(std::numeric_limits<double>::infinity());
    const EngineSineCosine negative = engine_sine_cosine(-std::numeric_limits<double>::infinity());
    const EngineSineCosine missing = engine_sine_cosine(std::numeric_limits<double>::quiet_NaN());
    require(infinite.sine == 0.0 && infinite.cosine == 1.0 && negative.sine == 0.0 && negative.cosine == 1.0 &&
                missing.sine == 0.0 && missing.cosine == 1.0,
            "infinities and NaN give the stated sine 0 and cosine 1");
    require(engine_tangent(std::numeric_limits<double>::infinity()) == 0.0 &&
                engine_tangent(-std::numeric_limits<double>::infinity()) == 0.0 &&
                engine_tangent(std::numeric_limits<double>::quiet_NaN()) == 0.0,
            "the tangent of infinities and NaN is the stated 0");
    const double largest_tangent = engine_tangent(std::numeric_limits<double>::max());
    require(std::fabs(largest_tangent - 0.004961954789184062 / -0.99998768942655991) <= 4e-15,
            "the tangent of the largest double is finite and reduced exactly");
}

void test_arctangent() {
    require(engine_arctangent2(0.0, 0.0) == 0.0 && engine_arctangent2(0.0, -1.0) == std::atan2(0.0, -1.0) &&
                engine_arctangent2(1.0, 0.0) == std::atan2(1.0, 0.0) &&
                engine_arctangent2(-1.0, 0.0) == std::atan2(-1.0, 0.0),
            "the axes are exact");
    for (int step = 0; step < 100000; ++step) {
        const double angle = static_cast<double>(step) * 6.2831853 / 100000.0 - 3.14159265;
        const EngineSineCosine turned = engine_sine_cosine(angle);
        const double radius = 0.001 + static_cast<double>(step % 97) * 13.7;
        const double y = turned.sine * radius;
        const double x = turned.cosine * radius;
        require(std::fabs(engine_arctangent2(y, x) - std::atan2(y, x)) <= 4e-15,
                "the arctangent within its bound all round");
    }
    const double infinite = std::numeric_limits<double>::infinity();
    require(std::fabs(engine_arctangent2(infinite, infinite) - std::atan2(infinite, infinite)) <= 4e-15 &&
                std::fabs(engine_arctangent2(1.0, -infinite) - std::atan2(1.0, -infinite)) <= 4e-15,
            "infinite arguments follow atan2");
    require(std::isnan(engine_arctangent2(std::numeric_limits<double>::quiet_NaN(), 1.0)), "NaN gives NaN");
}

// The degree form: every quarter turn exact, other angles within 2^-49 of the
// host's radian functions; and the tangent at the arc handle constant.
void test_degrees_and_tangent() {
    struct QuarterTurn final {
        double degrees{};
        double sine{};
        double cosine{};
    };
    const QuarterTurn turns[] = {
        {0.0, 0.0, 1.0},    {90.0, 1.0, 0.0},   {180.0, 0.0, -1.0},  {270.0, -1.0, 0.0},
        {360.0, 0.0, 1.0},  {-90.0, -1.0, 0.0}, {-180.0, 0.0, -1.0}, {-270.0, 1.0, 0.0},
        {450.0, 1.0, 0.0},  {-0.0, 0.0, 1.0},   {7290.0, 1.0, 0.0},  {9.0e14, 0.0, 1.0},
    };
    for (const QuarterTurn& turn : turns) {
        const EngineSineCosine observed = engine_sine_cosine_degrees(turn.degrees);
        std::uint64_t sine_bits = 0U;
        std::uint64_t expected_sine_bits = 0U;
        std::memcpy(&sine_bits, &observed.sine, sizeof sine_bits);
        std::memcpy(&expected_sine_bits, &turn.sine, sizeof expected_sine_bits);
        std::uint64_t cosine_bits = 0U;
        std::uint64_t expected_cosine_bits = 0U;
        std::memcpy(&cosine_bits, &observed.cosine, sizeof cosine_bits);
        std::memcpy(&expected_cosine_bits, &turn.cosine, sizeof expected_cosine_bits);
        require(sine_bits == expected_sine_bits && cosine_bits == expected_cosine_bits,
                "a quarter turn in degrees is exact, zeros positive");
    }
    const double bound = 2e-15;
    for (int step = -40000; step <= 40000; ++step) {
        const double degrees = static_cast<double>(step) * 0.0917;
        // The host's reference is taken within one turn: fmod is exact, and
        // rounding degrees · π/180 once below 2π moves the radian argument
        // by at most 2^-51. Converted whole, 3668 degrees (64 radians) would
        // carry up to 2^-48 of argument rounding into the host's value,
        // more than the bound being checked.
        const double radians = std::fmod(degrees, 360.0) * 0.017453292519943295769;
        const EngineSineCosine observed = engine_sine_cosine_degrees(degrees);
        require(std::fabs(observed.sine - std::sin(radians)) <= bound &&
                    std::fabs(observed.cosine - std::cos(radians)) <= bound,
                "the degree form within its bound on [-3668, 3668]");
    }
    const EngineSineCosine half = engine_sine_cosine_degrees(45.0);
    require(std::fabs(half.sine - std::sqrt(0.5)) <= bound && std::fabs(half.cosine - std::sqrt(0.5)) <= bound,
            "45 degrees is sqrt(1/2) within the bound");
    const EngineSineCosine infinite = engine_sine_cosine_degrees(std::numeric_limits<double>::infinity());
    const EngineSineCosine missing = engine_sine_cosine_degrees(std::numeric_limits<double>::quiet_NaN());
    require(infinite.sine == 0.0 && infinite.cosine == 1.0 && missing.sine == 0.0 && missing.cosine == 1.0,
            "infinities and NaN give the stated sine 0 and cosine 1 in degrees");

    require(engine_tangent(0.0) == 0.0, "the tangent of 0 is exact");
    // tan(π/8) = sqrt(2) − 1, and a quarter-turn arc piece's handle is
    // 4/3 tan(π/8) = 0.55228474983079339840 of the radius.
    const double eighth_turn = 0.39269908169872415481;
    const double tangent = engine_tangent(eighth_turn);
    require(std::fabs(tangent - (std::sqrt(2.0) - 1.0)) <= 4e-15, "tan(pi/8) is sqrt(2) - 1 within 2^-48");
    const double handle = 4.0 / 3.0 * tangent;
    require(std::fabs(handle - 0.55228474983079339840) <= 6e-15, "the quarter arc handle constant within its bound");
    for (int step = -1000; step <= 1000; ++step) {
        const double x = static_cast<double>(step) * 0.000785398;
        require(std::fabs(engine_tangent(x) - std::tan(x)) <= 4e-15, "the tangent within its bound on [-pi/4, pi/4]");
    }
}

// The hypotenuse: exact for Pythagorean triples, near the host elsewhere.
void test_hypotenuse() {
    require(engine_hypotenuse(3.0, 4.0) == 5.0 && engine_hypotenuse(-5.0, 12.0) == 13.0 &&
                engine_hypotenuse(0.0, -7.5) == 7.5 && engine_hypotenuse(0.0, 0.0) == 0.0,
            "Pythagorean triples and axes are exact");
    for (int step = 1; step <= 2000; ++step) {
        const double x = static_cast<double>(step) * 0.731 - 600.0;
        const double y = static_cast<double>(step % 37) * 19.3 - 300.0;
        require(relative_error(engine_hypotenuse(x, y), std::hypot(x, y)) <= 4.5e-16,
                "the hypotenuse within two roundings of the host");
    }
    require(std::isnan(engine_hypotenuse(std::numeric_limits<double>::quiet_NaN(), 1.0)), "NaN gives NaN");
}

// The bits of fixed tables, the same on every host. The expected values were
// produced on Windows x64 and are checked by every CI platform.
constexpr std::uint64_t expected_table_hash = 0xa2d6d4ebca4d01adULL;
constexpr std::uint64_t expected_trigonometry_hash = 0x04da5e7f92123dc0ULL;
constexpr std::uint64_t expected_arctangent_hash = 0xee893c02aeb8d0f6ULL;

void test_identity() {
    BitHash hash{};
    for (int step = 1; step <= 512; ++step) {
        const double t = static_cast<double>(step) / 512.0;
        hash.add(engine_power(t, 2.2));
        hash.add(engine_logarithm(t * 37.0));
        hash.add(engine_exponential(t * 40.0 - 20.0));
    }
    BitHash trigonometry{};
    for (int step = 1; step <= 512; ++step) {
        const double t = static_cast<double>(step) / 512.0;
        const EngineSineCosine angle = engine_sine_cosine(t * 300.0 - 150.0);
        trigonometry.add(angle.sine);
        trigonometry.add(angle.cosine);
    }
    BitHash arctangent{};
    for (int step = 1; step <= 512; ++step) {
        const double t = static_cast<double>(step) / 512.0;
        arctangent.add(engine_arctangent2(t * 7.0 - 3.5, 2.0 - t * 4.0));
    }
    if (print_mode()) {
        std::cout << "engine arctangent table hash 0x" << std::hex << arctangent.value << std::dec << '\n';
        std::cout << "engine math table hash 0x" << std::hex << hash.value << std::dec << '\n';
        std::cout << "engine sine and cosine table hash 0x" << std::hex << trigonometry.value << std::dec << '\n';
        return;
    }
    require(hash.value == expected_table_hash, "the math table has the same bits on every host");
    require(trigonometry.value == expected_trigonometry_hash,
            "the sine and cosine table has the same bits on every host");
    require(arctangent.value == expected_arctangent_hash, "the arctangent table has the same bits on every host");
}

} // namespace

int main() {
    test_logarithm();
    test_exponential();
    test_power();
    test_sine_cosine();
    test_arctangent();
    test_degrees_and_tangent();
    test_hypotenuse();
    test_identity();
    std::cout << "engine math tests passed\n";
    return 0;
}
