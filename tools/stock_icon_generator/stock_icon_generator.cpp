// The stock icon records' build-time generator. CMakeLists.txt builds it for
// the host and runs it to convert the committed Heroicons SVG files
// (assets/icons/heroicons/) into <build>/generated/stock_icons/
// stock_icon_data.inc, which stock_icons.cpp compiles in.
//
// It is a C++20 port of tools/stock_icon_codec.py, standard library only,
// and its output is byte-identical to `stock_icon_codec.py generate`: the
// Python codec is the test oracle (gui_forms_stock_icon_generator_oracle,
// built with the tests). The admitted SVG subset, the record format and
// every computation are the codec's; see its docstring. To stay
// byte-identical the port keeps the codec's order of floating-point
// operations, rounds with ties to even (std::nearbyint in the default
// rounding mode, as Python's round), parses numbers correctly rounded
// (std::strtod in the "C" locale this program never leaves), and is
// compiled with -ffp-contract=off so that no multiply and add are fused.
// The oracle runs on the same host, so both use that host's libm.
//
// Usage: gui_forms_stock_icon_generator --source DIR --output FILE
// Exit status 0 when FILE is written; 1 with a message naming the file when
// an SVG file is outside the subset; 2 for bad arguments. A failure leaves
// FILE as it was (the text is written whole, after every file is read).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view source_name = "Heroicons v2.2.0 (tailwindlabs/heroicons, commit ca7b62e)";
constexpr std::string_view svg_namespace = "http://www.w3.org/2000/svg";

constexpr std::uint8_t verb_move = 0U;
constexpr std::uint8_t verb_line = 1U;
constexpr std::uint8_t verb_quad = 2U;
constexpr std::uint8_t verb_cubic = 3U;
constexpr std::uint8_t verb_close = 5U;

constexpr int paint_nonzero = 0;
constexpr int paint_even_odd = 1;
constexpr int paint_stroke = 2;
constexpr int style_outline = 0;
constexpr int style_solid = 1;
constexpr double scale = 1000.0;
constexpr long long coordinate_limit = 32767;
// Python's math.radians multiplies by this constant (Py_MATH_PI / 180.0).
constexpr double degrees_to_radians = std::numbers::pi / 180.0;

// A file outside the admitted subset, or an unreadable tree.
class CodecError final : public std::runtime_error {
public:
    explicit CodecError(const std::string& message) : std::runtime_error(message) {}
};

struct GridPoint final {
    double x{};
    double y{};

    friend bool operator==(const GridPoint& left, const GridPoint& right) noexcept {
        return left.x == right.x && left.y == right.y;
    }
};

// One path element: verbs, points (grid units) and paint.
struct Layer final {
    std::vector<std::uint8_t> verbs{};
    std::vector<GridPoint> points{};
    int paint{paint_nonzero};
    double stroke_width{};
    int cap{};
};

struct Variant final {
    std::string name{};
    int style{};
    int grid{};
    std::vector<Layer> layers{};
};

// The directories read, each with its style and grid, in the codec's order.
struct VariantDirectory final {
    std::string_view path{};
    int style{};
    int grid{};
};
constexpr VariantDirectory variant_directories[] = {
    {"24/outline", style_outline, 24},
    {"24/solid", style_solid, 24},
    {"20/solid", style_solid, 20},
    {"16/solid", style_solid, 16},
};

// --- numbers ---------------------------------------------------------------

// Python's str.isspace over ASCII (it also counts 0x1c to 0x1f).
bool python_space(char character) noexcept {
    const unsigned char code = static_cast<unsigned char>(character);
    const bool space = code == 0x20U || (code >= 0x09U && code <= 0x0dU) || (code >= 0x1cU && code <= 0x1fU);
    return space;
}

bool ascii_digit(char character) noexcept {
    const bool digit = character >= '0' && character <= '9';
    return digit;
}

// Python's float() over the forms the codec meets: surrounding whitespace,
// an optional sign, decimal digits with an optional point and exponent,
// and inf or nan. Hexadecimal (which strtod would take) is refused.
double python_float(std::string_view text, std::string_view where) {
    std::size_t first = 0U;
    std::size_t last = text.size();
    while (first < last && python_space(text[first])) ++first;
    while (last > first && python_space(text[last - 1U])) --last;
    const std::string trimmed(text.substr(first, last - first));
    const bool hexadecimal = trimmed.find_first_of("xXpP") != std::string::npos;
    if (trimmed.empty() || hexadecimal) throw CodecError(std::string(where) + ": " + trimmed + " is not a number");
    const char* const begin = trimmed.c_str();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    const bool whole = end == begin + trimmed.size();
    if (!whole) throw CodecError(std::string(where) + ": " + trimmed + " is not a number");
    return value;
}

// The end of the codec's NUMBER pattern matched at `start`
// ([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?), or npos.
std::size_t number_end(std::string_view data, std::size_t start) noexcept {
    std::size_t index = start;
    if (index < data.size() && (data[index] == '-' || data[index] == '+')) ++index;
    if (index < data.size() && ascii_digit(data[index])) {
        while (index < data.size() && ascii_digit(data[index])) ++index;
        if (index < data.size() && data[index] == '.') ++index;
        while (index < data.size() && ascii_digit(data[index])) ++index;
    } else if (index + 1U < data.size() && data[index] == '.' && ascii_digit(data[index + 1U])) {
        ++index;
        while (index < data.size() && ascii_digit(data[index])) ++index;
    } else {
        return std::string_view::npos;
    }
    if (index < data.size() && (data[index] == 'e' || data[index] == 'E')) {
        std::size_t exponent = index + 1U;
        if (exponent < data.size() && (data[exponent] == '-' || data[exponent] == '+')) ++exponent;
        if (exponent < data.size() && ascii_digit(data[exponent])) {
            while (exponent < data.size() && ascii_digit(data[exponent])) ++exponent;
            index = exponent;
        }
    }
    return index;
}

bool path_command(char character) noexcept {
    const char upper = (character >= 'a' && character <= 'z') ? static_cast<char>(character - 'a' + 'A') : character;
    const std::string_view commands = "MLHVCSQTAZ";
    const bool command = commands.find(upper) != std::string_view::npos;
    return command;
}

bool ascii_letter(char character) noexcept {
    const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
    return letter;
}

char ascii_upper(char character) noexcept {
    const char upper = (character >= 'a' && character <= 'z') ? static_cast<char>(character - 'a' + 'A') : character;
    return upper;
}

// The path data's commands (one letter) and numbers, as text, in order
// (the codec's tokens_of).
std::vector<std::string> tokens_of(std::string_view data) {
    std::vector<std::string> result{};
    std::size_t index = 0U;
    while (index < data.size()) {
        const char character = data[index];
        if (python_space(character) || character == ',') {
            ++index;
            continue;
        }
        if (path_command(character)) {
            result.emplace_back(1U, character);
            ++index;
            continue;
        }
        const std::size_t end = number_end(data, index);
        if (end == std::string_view::npos) {
            throw CodecError("path data has an unknown character at " + std::to_string(index));
        }
        result.emplace_back(data.substr(index, end - index));
        index = end;
    }
    return result;
}

// --- paths -----------------------------------------------------------------

double python_modulo(double value, double divisor) noexcept {
    // Python's float %: the result takes the divisor's sign.
    double result = std::fmod(value, divisor);
    if (result != 0.0) {
        if ((divisor < 0.0) != (result < 0.0)) result += divisor;
    } else {
        result = std::copysign(0.0, divisor);
    }
    return result;
}

double vector_angle(double ux, double uy, double vx, double vy) noexcept {
    const double cross = ux * vy - uy * vx;
    const double dot = ux * vx + uy * vy;
    const double angle = std::atan2(cross, dot);
    return angle;
}

// Appends verbs and points to a layer, keeping SVG's current point,
// subpath start and reflected control points (the codec's PathBuilder).
class PathBuilder final {
public:
    explicit PathBuilder(Layer& layer) noexcept : layer_(layer) {}

    std::optional<GridPoint> current{};
    GridPoint start{};
    bool closed{};
    std::optional<GridPoint> last_cubic_control{};
    std::optional<GridPoint> last_quad_control{};

    GridPoint current_point() const {
        if (!current) throw CodecError("path data draws before its first move");
        return *current;
    }

    void move(GridPoint point) {
        layer_.verbs.push_back(verb_move);
        layer_.points.push_back(point);
        current = point;
        start = point;
        closed = false;
    }

    void line(GridPoint point) {
        ensure_open();
        layer_.verbs.push_back(verb_line);
        layer_.points.push_back(point);
        current = point;
    }

    void cubic(GridPoint first, GridPoint second, GridPoint end) {
        ensure_open();
        layer_.verbs.push_back(verb_cubic);
        layer_.points.push_back(first);
        layer_.points.push_back(second);
        layer_.points.push_back(end);
        current = end;
    }

    void quad(GridPoint control, GridPoint end) {
        ensure_open();
        layer_.verbs.push_back(verb_quad);
        layer_.points.push_back(control);
        layer_.points.push_back(end);
        current = end;
    }

    void close() {
        if (!current) throw CodecError("path data closes before its first move");
        if (closed) return;
        layer_.verbs.push_back(verb_close);
        current = start;
        closed = true;
    }

    // SVG's endpoint arc (SVG 1.1 appendix F.6) as cubic curves of at most
    // a quarter turn each, computed in the codec's order.
    void arc(double radius_x, double radius_y, double rotation, bool large, bool sweep, GridPoint end) {
        ensure_open();
        const GridPoint from = *current;
        if (from == end) return;
        radius_x = std::fabs(radius_x);
        radius_y = std::fabs(radius_y);
        if (radius_x == 0.0 || radius_y == 0.0) {
            line(end);
            return;
        }
        const double phi = python_modulo(rotation, 360.0) * degrees_to_radians;
        const double cos_phi = std::cos(phi);
        const double sin_phi = std::sin(phi);
        const double half_dx = (from.x - end.x) / 2.0;
        const double half_dy = (from.y - end.y) / 2.0;
        const double x1 = cos_phi * half_dx + sin_phi * half_dy;
        const double y1 = -sin_phi * half_dx + cos_phi * half_dy;
        // F.6.6: radii too small to reach are scaled up.
        const double lam = (x1 * x1) / (radius_x * radius_x) + (y1 * y1) / (radius_y * radius_y);
        if (lam > 1.0) {
            const double root_lam = std::sqrt(lam);
            radius_x *= root_lam;
            radius_y *= root_lam;
        }
        const double numerator = radius_x * radius_x * radius_y * radius_y - radius_x * radius_x * y1 * y1 -
                                 radius_y * radius_y * x1 * x1;
        const double denominator = radius_x * radius_x * y1 * y1 + radius_y * radius_y * x1 * x1;
        double factor = 0.0;
        if (denominator != 0.0) factor = std::sqrt(std::max(0.0, numerator / denominator));
        if (large == sweep) factor = -factor;
        const double centre_x1 = factor * radius_x * y1 / radius_y;
        const double centre_y1 = -factor * radius_y * x1 / radius_x;
        const double centre_x = cos_phi * centre_x1 - sin_phi * centre_y1 + (from.x + end.x) / 2.0;
        const double centre_y = sin_phi * centre_x1 + cos_phi * centre_y1 + (from.y + end.y) / 2.0;

        const double theta = vector_angle(1.0, 0.0, (x1 - centre_x1) / radius_x, (y1 - centre_y1) / radius_y);
        double delta = vector_angle((x1 - centre_x1) / radius_x, (y1 - centre_y1) / radius_y,
                                    (-x1 - centre_x1) / radius_x, (-y1 - centre_y1) / radius_y);
        if (!sweep && delta > 0.0) {
            delta -= 2.0 * std::numbers::pi;
        } else if (sweep && delta < 0.0) {
            delta += 2.0 * std::numbers::pi;
        }
        const double quarters = std::ceil(std::fabs(delta) / (std::numbers::pi / 2.0) - 1e-9);
        const long long segments = std::max(1LL, static_cast<long long>(quarters));
        const double step = delta / static_cast<double>(segments);
        // The tangent length of a circular arc of `step` radians, scaled by
        // each radius (the usual 4/3 tan(step/4) approximation).
        const double kappa = 4.0 / 3.0 * std::tan(step / 4.0);
        const ArcFrame frame{radius_x, radius_y, cos_phi, sin_phi, centre_x, centre_y};
        for (long long index = 0; index < segments; ++index) {
            const double t0 = theta + step * static_cast<double>(index);
            const double t1 = t0 + step;
            const GridPoint p0 = frame.point_at(t0);
            const GridPoint p3 = index == segments - 1 ? end : frame.point_at(t1);
            const GridPoint d0 = frame.derivative_at(t0);
            const GridPoint d1 = frame.derivative_at(t1);
            const GridPoint first{p0.x + kappa * d0.x, p0.y + kappa * d0.y};
            const GridPoint second{p3.x - kappa * d1.x, p3.y - kappa * d1.y};
            cubic(first, second, p3);
        }
    }

private:
    // The ellipse an arc lies on: radii, rotation and centre.
    struct ArcFrame final {
        double radius_x{};
        double radius_y{};
        double cos_phi{};
        double sin_phi{};
        double centre_x{};
        double centre_y{};

        GridPoint point_at(double t) const noexcept {
            const double x = radius_x * std::cos(t);
            const double y = radius_y * std::sin(t);
            const GridPoint point{cos_phi * x - sin_phi * y + centre_x, sin_phi * x + cos_phi * y + centre_y};
            return point;
        }

        GridPoint derivative_at(double t) const noexcept {
            const double x = -radius_x * std::sin(t);
            const double y = radius_y * std::cos(t);
            const GridPoint derivative{cos_phi * x - sin_phi * y, sin_phi * x + cos_phi * y};
            return derivative;
        }
    };

    // A drawing command after Z starts a new subpath at the closed one's
    // start; the record states it with an explicit move.
    void ensure_open() {
        if (!current) throw CodecError("path data draws before its first move");
        if (closed) move(*current);
    }

    Layer& layer_;
};

// Reads numbers and arc flags from path data tokens (the codec's number()
// and flag()).
class TokenReader final {
public:
    explicit TokenReader(std::vector<std::string> tokens) : tokens_(std::move(tokens)) {}

    [[nodiscard]] bool done() const noexcept { return index_ >= tokens_.size(); }
    [[nodiscard]] const std::string& peek() const& { return tokens_[index_]; }
    const std::string& peek() const&& = delete;
    void skip() noexcept { ++index_; }

    double number() {
        if (index_ >= tokens_.size() || ascii_letter(tokens_[index_][0])) {
            throw CodecError("path data needs a number at token " + std::to_string(index_));
        }
        const double value = python_float(tokens_[index_], "path data");
        ++index_;
        return value;
    }

    // A flag is one digit, possibly run together with what follows.
    bool flag() {
        if (index_ >= tokens_.size()) throw CodecError("path data ends inside an arc");
        std::string& text = tokens_[index_];
        const char first = text[0];
        if (first != '0' && first != '1') throw CodecError("arc flag " + text + " is not 0 or 1");
        if (text.size() == 1U) {
            ++index_;
        } else {
            text.erase(0U, 1U);
        }
        const bool set = first == '1';
        return set;
    }

private:
    std::vector<std::string> tokens_;
    std::size_t index_{};
};

// Two numbers read in order, each added to its base coordinate.
GridPoint read_point(TokenReader& reader, GridPoint base) {
    const double x = reader.number();
    const double y = reader.number();
    const GridPoint point{base.x + x, base.y + y};
    return point;
}

void clear_controls(PathBuilder& builder) noexcept {
    builder.last_cubic_control.reset();
    builder.last_quad_control.reset();
}

// Appends `data` (an SVG path's d attribute) to `layer` (parse_path).
void parse_path(std::string_view data, Layer& layer) {
    TokenReader reader(tokens_of(data));
    PathBuilder builder(layer);
    char command = '\0';
    while (!reader.done()) {
        const std::string& token = reader.peek();
        if (ascii_letter(token[0])) {
            command = token[0];
            reader.skip();
            if (ascii_upper(command) == 'Z') {
                builder.close();
                clear_controls(builder);
                continue;
            }
        } else if (command == '\0' || ascii_upper(command) == 'Z') {
            throw CodecError("path data has a number without a command");
        }
        const bool relative = command >= 'a' && command <= 'z';
        const char upper = ascii_upper(command);
        GridPoint base{0.0, 0.0};
        if (relative && builder.current) base = *builder.current;
        switch (upper) {
        case 'M': {
            builder.move(read_point(reader, base));
            // Further pairs after a move are lines.
            command = relative ? 'l' : 'L';
            clear_controls(builder);
            break;
        }
        case 'L': {
            builder.line(read_point(reader, base));
            clear_controls(builder);
            break;
        }
        case 'H': {
            const double x = reader.number() + (relative ? base.x : 0.0);
            const GridPoint current = builder.current_point();
            builder.line(GridPoint{x, current.y});
            clear_controls(builder);
            break;
        }
        case 'V': {
            const GridPoint current = builder.current_point();
            const double y = reader.number() + (relative ? base.y : 0.0);
            builder.line(GridPoint{current.x, y});
            clear_controls(builder);
            break;
        }
        case 'C': {
            const GridPoint first = read_point(reader, base);
            const GridPoint second = read_point(reader, base);
            const GridPoint end = read_point(reader, base);
            builder.cubic(first, second, end);
            builder.last_cubic_control = second;
            builder.last_quad_control.reset();
            break;
        }
        case 'S': {
            const GridPoint current = builder.current_point();
            GridPoint reflected = current;
            if (builder.last_cubic_control) {
                const GridPoint control = *builder.last_cubic_control;
                reflected = GridPoint{2.0 * current.x - control.x, 2.0 * current.y - control.y};
            }
            const GridPoint second = read_point(reader, base);
            const GridPoint end = read_point(reader, base);
            builder.cubic(reflected, second, end);
            builder.last_cubic_control = second;
            builder.last_quad_control.reset();
            break;
        }
        case 'Q': {
            const GridPoint control = read_point(reader, base);
            const GridPoint end = read_point(reader, base);
            builder.quad(control, end);
            builder.last_quad_control = control;
            builder.last_cubic_control.reset();
            break;
        }
        case 'T': {
            const GridPoint current = builder.current_point();
            GridPoint control = current;
            if (builder.last_quad_control) {
                const GridPoint previous = *builder.last_quad_control;
                control = GridPoint{2.0 * current.x - previous.x, 2.0 * current.y - previous.y};
            }
            const GridPoint end = read_point(reader, base);
            builder.quad(control, end);
            builder.last_quad_control = control;
            builder.last_cubic_control.reset();
            break;
        }
        case 'A': {
            const double radius_x = reader.number();
            const double radius_y = reader.number();
            const double rotation = reader.number();
            const bool large = reader.flag();
            const bool sweep = reader.flag();
            const GridPoint end = read_point(reader, base);
            builder.arc(radius_x, radius_y, rotation, large, sweep, end);
            clear_controls(builder);
            break;
        }
        default: throw CodecError(std::string("path command ") + command + " is not admitted");
        }
    }
}

// A coordinate as the codec's absolute path commands read it back: the base
// (0, 0) plus the number, which turns -0.0 into 0.0 (and so can change the
// side of an atan2 branch cut in an arc).
double read_back(double value) noexcept {
    const double read = 0.0 + value;
    return read;
}

// A rect element as its SVG 1.1 section 9.2 path, built with the absolute
// commands the codec writes as text and parses back. Its text is Python's
// shortest round-trip form, so the values are the same doubles; read_back
// applies the parse's one change.
void rect_path(double x, double y, double width, double height, double radius_x, double radius_y, Layer& layer) {
    if (radius_x <= 0.0 && radius_y > 0.0) radius_x = radius_y;
    if (radius_y <= 0.0 && radius_x > 0.0) radius_y = radius_x;
    radius_x = std::min(radius_x, width / 2.0);
    radius_y = std::min(radius_y, height / 2.0);
    PathBuilder builder(layer);
    if (radius_x <= 0.0) {
        // M{x} {y}H{x + width}V{y + height}H{x}Z
        builder.move(GridPoint{read_back(x), read_back(y)});
        builder.line(GridPoint{read_back(x + width), (*builder.current).y});
        builder.line(GridPoint{(*builder.current).x, read_back(y + height)});
        builder.line(GridPoint{read_back(x), (*builder.current).y});
        builder.close();
        return;
    }
    // M{x+rx} {y}H{x+w-rx}A{rx} {ry} 0 0 1 {x+w} {y+ry}V{y+h-ry}
    // A... {x+w-rx} {y+h}H{x+rx}A... {x} {y+h-ry}V{y+ry}A... {x+rx} {y}Z
    builder.move(GridPoint{read_back(x + radius_x), read_back(y)});
    builder.line(GridPoint{read_back(x + width - radius_x), (*builder.current).y});
    builder.arc(radius_x, radius_y, 0.0, false, true, GridPoint{read_back(x + width), read_back(y + radius_y)});
    builder.line(GridPoint{(*builder.current).x, read_back(y + height - radius_y)});
    builder.arc(radius_x, radius_y, 0.0, false, true,
                GridPoint{read_back(x + width - radius_x), read_back(y + height)});
    builder.line(GridPoint{read_back(x + radius_x), (*builder.current).y});
    builder.arc(radius_x, radius_y, 0.0, false, true, GridPoint{read_back(x), read_back(y + height - radius_y)});
    builder.line(GridPoint{(*builder.current).x, read_back(y + radius_y)});
    builder.arc(radius_x, radius_y, 0.0, false, true, GridPoint{read_back(x + radius_x), read_back(y)});
    builder.close();
}

// --- files -----------------------------------------------------------------

// One element of the admitted XML subset: a name and its attributes in
// document order.
struct XmlElement final {
    std::string name{};
    std::vector<std::pair<std::string, std::string>> attributes{};
};

// The svg root and its child elements, read from the subset ElementTree
// admits for these files: an optional <?xml?> prolog, no declarations, no
// entities, no comments; text between elements is ignored, as the codec
// ignores it. A child of the root that holds elements is refused.
class XmlReader final {
public:
    XmlReader(std::string_view text, std::string_view where) : text_(text), where_(where) {}

    void read(XmlElement& root, std::vector<XmlElement>& children) {
        if (text_.find("<!") != std::string_view::npos) fail("declarations and entities are not admitted");
        if (text_.find('&') != std::string_view::npos) fail("entities are not admitted");
        skip_space();
        if (text_.substr(index_, 5U) == "<?xml") {
            const std::size_t end = text_.find("?>", index_);
            if (end == std::string_view::npos) fail("the XML declaration is not closed");
            index_ = end + 2U;
        }
        skip_space();
        const bool closed = read_start(root);
        if (!closed) {
            while (true) {
                skip_text();
                if (index_ >= text_.size()) fail("svg is not closed");
                if (text_.substr(index_, 2U) == "</") {
                    read_end(root.name);
                    break;
                }
                XmlElement child{};
                const bool child_closed = read_start(child);
                if (!child_closed) {
                    skip_text();
                    if (text_.substr(index_, 2U) != "</") fail(child.name + " has children");
                    read_end(child.name);
                }
                children.push_back(std::move(child));
            }
        }
        skip_space();
        if (index_ != text_.size()) fail("text follows the root element");
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw CodecError(std::string(where_) + ": " + message);
    }

    void skip_space() noexcept {
        while (index_ < text_.size() && python_space(text_[index_])) ++index_;
    }

    void skip_text() noexcept {
        while (index_ < text_.size() && text_[index_] != '<') ++index_;
    }

    static bool name_character(char character) noexcept {
        const bool name = ascii_letter(character) || ascii_digit(character) || character == '-' ||
                          character == '_' || character == ':' || character == '.';
        return name;
    }

    std::string read_name() {
        const std::size_t first = index_;
        while (index_ < text_.size() && name_character(text_[index_])) ++index_;
        if (index_ == first) fail("an element or attribute has no name");
        return std::string(text_.substr(first, index_ - first));
    }

    // Reads `<name attributes` up to `/>` (true) or `>` (false).
    bool read_start(XmlElement& element) {
        if (index_ >= text_.size() || text_[index_] != '<') fail("an element was expected");
        ++index_;
        element.name = read_name();
        while (true) {
            skip_space();
            if (index_ >= text_.size()) fail("an element is not closed");
            if (text_.substr(index_, 2U) == "/>") {
                index_ += 2U;
                return true;
            }
            if (text_[index_] == '>') {
                ++index_;
                return false;
            }
            std::string key = read_name();
            skip_space();
            if (index_ >= text_.size() || text_[index_] != '=') fail("attribute " + key + " has no value");
            ++index_;
            skip_space();
            if (index_ >= text_.size() || (text_[index_] != '"' && text_[index_] != '\'')) {
                fail("attribute " + key + " is not quoted");
            }
            const char quote = text_[index_];
            ++index_;
            const std::size_t end = text_.find(quote, index_);
            if (end == std::string_view::npos) fail("attribute " + key + " is not closed");
            std::string value(text_.substr(index_, end - index_));
            index_ = end + 1U;
            // XML attribute-value normalization: whitespace becomes spaces.
            for (char& character : value) {
                if (character == '\t' || character == '\n' || character == '\r') character = ' ';
            }
            for (const std::pair<std::string, std::string>& existing : element.attributes) {
                if (existing.first == key) fail("attribute " + key + " is repeated");
            }
            element.attributes.emplace_back(std::move(key), std::move(value));
        }
    }

    void read_end(const std::string& name) {
        index_ += 2U;
        const std::string closing = read_name();
        skip_space();
        if (closing != name || index_ >= text_.size() || text_[index_] != '>') fail("</" + name + "> was expected");
        ++index_;
    }

    std::string_view text_;
    std::string_view where_;
    std::size_t index_{};
};

std::vector<double> split_numbers(std::string_view text, std::string_view where) {
    std::vector<double> numbers{};
    std::size_t index = 0U;
    while (index < text.size()) {
        while (index < text.size() && python_space(text[index])) ++index;
        const std::size_t first = index;
        while (index < text.size() && !python_space(text[index])) ++index;
        if (index > first) numbers.push_back(python_float(text.substr(first, index - first), where));
    }
    return numbers;
}

// The layers of one Heroicons file whose grid must be `grid` (parse_svg).
std::vector<Layer> parse_svg(std::string_view text, int grid, const std::string& where) {
    XmlElement root{};
    std::vector<XmlElement> elements{};
    XmlReader reader(text, where);
    reader.read(root, elements);
    if (root.name != "svg") throw CodecError(where + ": the root is not svg");
    std::string root_fill = "currentColor";
    std::optional<std::string> root_stroke{};
    double root_width = 1.0;
    std::optional<std::string> view_box{};
    for (const std::pair<std::string, std::string>& attribute : root.attributes) {
        const std::string& key = attribute.first;
        const std::string& value = attribute.second;
        if (key == "viewBox") {
            view_box = value;
        } else if (key == "fill") {
            root_fill = value;
        } else if (key == "stroke") {
            root_stroke = value;
        } else if (key == "stroke-width") {
            root_width = python_float(value, where);
        } else if (key == "xmlns") {
            if (value != svg_namespace) throw CodecError(where + ": the root is not svg");
        } else if (key.starts_with("xmlns:") || key == "aria-hidden" || key == "data-slot") {
            // Inert: a namespace declaration or an accessibility hint.
        } else {
            throw CodecError(where + ": svg attribute " + key + " is not admitted");
        }
    }
    const double side = static_cast<double>(grid);
    const std::vector<double> expected_box{0.0, 0.0, side, side};
    if (!view_box || split_numbers(*view_box, where) != expected_box) {
        throw CodecError(where + ": viewBox is not 0 0 " + std::to_string(grid) + " " + std::to_string(grid));
    }
    std::vector<Layer> layers{};
    for (const XmlElement& element : elements) {
        const std::string& tag = element.name;
        std::string fill = root_fill;
        std::optional<std::string> stroke = root_stroke;
        double width = root_width;
        std::string rule = "nonzero";
        std::string cap = "butt";
        std::string join = "miter";
        std::set<std::string> admitted{};
        std::map<std::string, double> rect{{"x", 0.0}, {"y", 0.0}, {"width", 0.0},
                                           {"height", 0.0}, {"rx", 0.0}, {"ry", 0.0}};
        std::string data{};
        if (tag == "path") {
            admitted = {"d", "fill", "stroke", "stroke-width", "fill-rule", "clip-rule", "stroke-linecap",
                        "stroke-linejoin"};
        } else if (tag == "rect") {
            admitted = {"x", "y", "width", "height", "rx", "ry", "fill", "stroke", "stroke-width",
                        "stroke-linecap", "stroke-linejoin"};
        } else {
            throw CodecError(where + ": element " + tag + " is not admitted");
        }
        for (const std::pair<std::string, std::string>& attribute : element.attributes) {
            const std::string& key = attribute.first;
            const std::string& value = attribute.second;
            if (admitted.find(key) == admitted.end()) {
                throw CodecError(where + ": " + tag + " attribute " + key + " is not admitted");
            }
            if (key == "d") {
                data = value;
            } else if (key == "fill") {
                fill = value;
            } else if (key == "stroke") {
                stroke = value;
            } else if (key == "stroke-width") {
                width = python_float(value, where);
            } else if (key == "fill-rule") {
                rule = value;
            } else if (key == "stroke-linecap") {
                cap = value;
            } else if (key == "stroke-linejoin") {
                join = value;
            } else if (tag == "rect" && rect.find(key) != rect.end()) {
                rect[key] = python_float(value, where);
            }
        }
        Layer layer{};
        const bool stroked = stroke && *stroke != "none";
        const bool filled = fill != "none";
        if (stroked == filled) {
            throw CodecError(where + ": a " + tag + " must be filled or stroked, not both or neither");
        }
        if (filled) {
            if (fill != "currentColor") throw CodecError(where + ": fill " + fill + " is not currentColor");
            if (rule != "nonzero" && rule != "evenodd") {
                throw CodecError(where + ": fill-rule " + rule + " is not admitted");
            }
            layer.paint = rule == "evenodd" ? paint_even_odd : paint_nonzero;
        } else {
            if (*stroke != "currentColor") throw CodecError(where + ": stroke " + *stroke + " is not currentColor");
            int cap_code = 0;
            if (cap == "butt") {
                cap_code = 0;
            } else if (cap == "round") {
                cap_code = 1;
            } else if (cap == "square") {
                cap_code = 2;
            } else {
                throw CodecError(where + ": stroke-linecap " + cap + " is not admitted");
            }
            if (join != "round") throw CodecError(where + ": stroke-linejoin " + join + " is not round");
            if (!(0.0 < width && width <= 8.0)) throw CodecError(where + ": stroke-width is out of range");
            layer.paint = paint_stroke;
            layer.stroke_width = width;
            layer.cap = cap_code;
        }
        if (tag == "rect") {
            // The codec writes these as path text, which refuses inf and nan.
            for (const std::pair<const std::string, double>& value : rect) {
                if (!std::isfinite(value.second)) throw CodecError(where + ": rect " + value.first + " is not finite");
            }
            rect_path(rect["x"], rect["y"], rect["width"], rect["height"], rect["rx"], rect["ry"], layer);
        } else {
            parse_path(data, layer);
        }
        if (layer.verbs.empty()) throw CodecError(where + ": a " + tag + " draws nothing");
        layers.push_back(std::move(layer));
    }
    if (layers.empty()) throw CodecError(where + ": the icon draws nothing");
    return layers;
}

bool icon_name(std::string_view name) noexcept {
    // [a-z0-9]+(?:-[a-z0-9]+)*
    if (name.empty() || name.front() == '-' || name.back() == '-') return false;
    char previous = '\0';
    for (const char character : name) {
        const bool word = (character >= 'a' && character <= 'z') || ascii_digit(character);
        if (!word && character != '-') return false;
        if (character == '-' && previous == '-') return false;
        previous = character;
    }
    return true;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw CodecError(path.string() + ": cannot be read");
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

bool variant_before(const Variant& left, const Variant& right) noexcept {
    if (left.name != right.name) return left.name < right.name;
    if (left.style != right.style) return left.style < right.style;
    return left.grid < right.grid;
}

std::vector<Variant> read_variants(const std::filesystem::path& source) {
    std::vector<Variant> variants{};
    for (const VariantDirectory& directory : variant_directories) {
        const std::filesystem::path folder = source / std::filesystem::path(directory.path);
        std::error_code error{};
        if (!std::filesystem::is_directory(folder, error)) throw CodecError(folder.string() + " is missing");
        // The codec sorts the paths, which in one folder is by file name.
        std::vector<std::string> file_names{};
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder)) {
            if (entry.path().extension().string() == ".svg") file_names.push_back(entry.path().filename().string());
        }
        std::sort(file_names.begin(), file_names.end());
        for (const std::string& file_name : file_names) {
            const std::filesystem::path file = folder / std::filesystem::path(file_name);
            const std::string name = file.stem().string();
            if (!icon_name(name)) {
                throw CodecError(file.string() + ": the name is not lowercase words joined by hyphens");
            }
            const std::string text = read_file(file);
            Variant variant{};
            variant.name = name;
            variant.style = directory.style;
            variant.grid = directory.grid;
            variant.layers = parse_svg(text, directory.grid, file.string());
            variants.push_back(std::move(variant));
        }
    }
    std::stable_sort(variants.begin(), variants.end(), variant_before);
    return variants;
}

// --- records ---------------------------------------------------------------

struct VariantRecord final {
    std::size_t name{};
    int style{};
    int grid{};
    std::size_t first_layer{};
    std::size_t layer_count{};
};

struct LayerRecord final {
    std::size_t first_verb{};
    std::size_t verb_count{};
    std::size_t first_coordinate{};
    int paint{};
    long long width{};
    int cap{};
};

struct Records final {
    std::vector<std::string> names{};
    std::vector<VariantRecord> variants{};
    std::vector<LayerRecord> layers{};
    std::vector<int> verbs{};
    std::vector<long long> coordinates{};
};

// round(value * 1000) with ties to even, as Python's round.
long long quantized(double value, const std::string& where) {
    const double scaled = std::nearbyint(value * scale);
    if (!std::isfinite(scaled) || std::fabs(scaled) > static_cast<double>(coordinate_limit)) {
        throw CodecError(where + ": a coordinate is outside the 16-bit record range");
    }
    return static_cast<long long>(scaled);
}

Records encode(const std::vector<Variant>& variants) {
    Records records{};
    std::set<std::string> unique{};
    for (const Variant& variant : variants) unique.insert(variant.name);
    records.names.assign(unique.begin(), unique.end());
    std::map<std::string, std::size_t> index_of{};
    for (std::size_t index = 0U; index < records.names.size(); ++index) index_of[records.names[index]] = index;
    for (const Variant& variant : variants) {
        const std::size_t first_layer = records.layers.size();
        const std::string where = variant.name + " " + std::to_string(variant.grid) + "/" +
                                  (variant.style != 0 ? "solid" : "outline");
        for (const Layer& layer : variant.layers) {
            LayerRecord record{};
            record.first_verb = records.verbs.size();
            record.verb_count = layer.verbs.size();
            record.first_coordinate = records.coordinates.size();
            for (const std::uint8_t verb : layer.verbs) records.verbs.push_back(verb);
            for (const GridPoint& point : layer.points) {
                records.coordinates.push_back(quantized(point.x, where));
                records.coordinates.push_back(quantized(point.y, where));
            }
            record.paint = layer.paint;
            record.width = 0;
            if (layer.paint == paint_stroke) {
                record.width = static_cast<long long>(std::nearbyint(layer.stroke_width * scale));
            }
            record.cap = layer.cap;
            records.layers.push_back(record);
        }
        VariantRecord record{};
        record.name = index_of[variant.name];
        record.style = variant.style;
        record.grid = variant.grid;
        record.first_layer = first_layer;
        record.layer_count = variant.layers.size();
        records.variants.push_back(record);
    }
    return records;
}

template <typename Value>
void append_wrapped(std::string& output, const std::vector<Value>& values, std::size_t per_line) {
    for (std::size_t start = 0U; start < values.size(); start += per_line) {
        if (start != 0U) output += '\n';
        output += "    ";
        const std::size_t end = std::min(values.size(), start + per_line);
        for (std::size_t index = start; index < end; ++index) {
            if (index != start) output += ',';
            output += std::to_string(values[index]);
        }
        output += ',';
    }
}

// The generated .inc text (the codec's render, line for line).
std::string render(const Records& records) {
    std::string output{};
    output += "// Generated by the stock icon generator (tools/stock_icon_generator/) from ";
    output += source_name;
    output += "; do not edit.\n";
    output += "// The record layout is stock_icon_records.hpp; the licence and provenance\n";
    output += "// are assets/icons/heroicons/.\n";
    output += "// clang-format off\n\n";
    output += "namespace gui_forms::detail::stock_icon_data {\n\n";
    output += "inline constexpr std::size_t name_count = " + std::to_string(records.names.size()) + "U;\n";
    output += "inline constexpr std::size_t variant_count = " + std::to_string(records.variants.size()) + "U;\n";
    output += "inline constexpr std::size_t layer_count = " + std::to_string(records.layers.size()) + "U;\n";
    output += "inline constexpr std::size_t verb_count = " + std::to_string(records.verbs.size()) + "U;\n";
    output += "inline constexpr std::size_t coordinate_count = " + std::to_string(records.coordinates.size()) + "U;\n\n";
    output += "// The icon names, sorted.\n";
    output += "inline constexpr std::array<std::string_view, name_count> names{\n";
    for (const std::string& name : records.names) output += "    \"" + name + "\",\n";
    output += "};\n\n";
    output += "// Name index, style, grid, first layer, layer count; sorted by name, style, grid.\n";
    output += "inline constexpr std::array<StockIconVariantRecord, variant_count> variants{{\n";
    for (const VariantRecord& variant : records.variants) {
        output += "    {" + std::to_string(variant.name) + "U, " + std::to_string(variant.style) + "U, " +
                  std::to_string(variant.grid) + "U, " + std::to_string(variant.first_layer) + "U, " +
                  std::to_string(variant.layer_count) + "U},\n";
    }
    output += "}};\n\n";
    output += "// First verb, verb count, first coordinate, paint, stroke width (thousandths), cap.\n";
    output += "inline constexpr std::array<StockIconLayerRecord, layer_count> layers{{\n";
    for (const LayerRecord& layer : records.layers) {
        output += "    {" + std::to_string(layer.first_verb) + "U, " + std::to_string(layer.verb_count) + "U, " +
                  std::to_string(layer.first_coordinate) + "U, " + std::to_string(layer.paint) + "U, " +
                  std::to_string(layer.width) + "U, " + std::to_string(layer.cap) + "U},\n";
    }
    output += "}};\n\n";
    output += "inline constexpr std::array<std::uint8_t, verb_count> verbs{\n";
    append_wrapped(output, records.verbs, 40U);
    output += "\n};\n\n";
    output += "// Thousandths of a grid unit, x then y.\n";
    output += "inline constexpr std::array<std::int16_t, coordinate_count> coordinates{\n";
    append_wrapped(output, records.coordinates, 20U);
    output += "\n};\n\n";
    output += "} // namespace gui_forms::detail::stock_icon_data\n";
    output += "// clang-format on\n";
    return output;
}

void write_whole(const std::filesystem::path& output, const std::string& text) {
    std::error_code error{};
    if (output.has_parent_path()) std::filesystem::create_directories(output.parent_path(), error);
    std::filesystem::path partial = output;
    partial += ".partial";
    {
        std::ofstream stream(partial, std::ios::binary | std::ios::trunc);
        if (!stream) throw CodecError(partial.string() + ": cannot be written");
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream) throw CodecError(partial.string() + ": cannot be written");
    }
    std::filesystem::rename(partial, output, error);
    if (error) throw CodecError(output.string() + ": cannot be replaced (" + error.message() + ")");
}

} // namespace

int main(int argument_count, char** arguments) {
    std::optional<std::filesystem::path> source{};
    std::optional<std::filesystem::path> output{};
    for (int index = 1; index < argument_count; ++index) {
        const std::string_view argument = arguments[index];
        const bool has_value = index + 1 < argument_count;
        if (argument == "--source" && has_value) {
            ++index;
            source = std::filesystem::path(arguments[index]);
        } else if (argument == "--output" && has_value) {
            ++index;
            output = std::filesystem::path(arguments[index]);
        } else {
            std::cerr << "usage: gui_forms_stock_icon_generator --source DIR --output FILE\n";
            return 2;
        }
    }
    if (!source || !output) {
        std::cerr << "usage: gui_forms_stock_icon_generator --source DIR --output FILE\n";
        return 2;
    }
    try {
        const std::vector<Variant> variants = read_variants(*source);
        const Records records = encode(variants);
        const std::string text = render(records);
        write_whole(*output, text);
    } catch (const CodecError& error) {
        std::cerr << "stock_icon_generator: " << error.what() << '\n';
        return 1;
    } catch (const std::filesystem::filesystem_error& error) {
        std::cerr << "stock_icon_generator: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
