// Curved paths (paint_path.hpp, exhibit gap E7): validation, conservative
// bounds, flattening into caller storage for the base painter's fallbacks,
// and the owned builder. Renderer-neutral; the Skia painter draws the
// verbs exactly instead of flattening them.
#include "gui_forms/types/paint_path/paint_path.hpp"
#include "gui_forms/engine/engine_math/engine_math.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms {
namespace {

constexpr double degrees_to_radians = 0.017453292519943295769;
constexpr double full_turn_degrees = 360.0;
// The most chords one curve or arc is flattened into, so a huge curve at a
// tiny tolerance cannot exhaust the storage on its own.
constexpr std::size_t maximum_curve_chords = 1024U;
constexpr std::size_t maximum_arc_chords = 4096U;

bool finite_point(Point point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

double distance_from_origin(double x, double y) noexcept {
    const double length = std::sqrt(x * x + y * y);
    return length;
}

// The sweep held to one turn either way.
double held_sweep(double sweep_degrees) noexcept {
    const double held = std::clamp(sweep_degrees, -full_turn_degrees, full_turn_degrees);
    return held;
}

Point arc_point(Point center, Point radii, double degrees) noexcept {
    const EngineSineCosine turn = engine_sine_cosine_degrees(degrees);
    const Point point{center.x + radii.x * turn.cosine, center.y + radii.y * turn.sine};
    return point;
}

// Chords for a Bezier curve of `degree` whose largest second difference
// has length `second_difference` (Wang's formula: the chords of n equal
// parameter steps lie within degree (degree - 1) / 8 * M / n^2 of it).
std::size_t bezier_chords(double degree, double second_difference, double tolerance) noexcept {
    const double factor = degree * (degree - 1.0) / 8.0;
    const double wanted = std::ceil(std::sqrt(factor * second_difference / tolerance));
    if (!(wanted >= 1.0)) return 1U;
    if (wanted >= static_cast<double>(maximum_curve_chords)) return maximum_curve_chords;
    return static_cast<std::size_t>(wanted);
}

// Chords for an arc of `sweep_degrees` on an ellipse whose larger radius
// is `radius`: each chord's sagitta, radius (1 - cos(step / 2)), within the
// tolerance.
std::size_t arc_chords(double radius, double sweep_degrees, double tolerance) noexcept {
    const double sweep = std::fabs(sweep_degrees) * degrees_to_radians;
    if (!(sweep > 0.0)) return 1U;
    double step = 3.14159265358979323846 * 0.5;
    if (tolerance < radius) step = std::min(step, 2.0 * std::acos(1.0 - tolerance / radius));
    const double wanted = std::ceil(sweep / step);
    if (!(wanted >= 1.0)) return 1U;
    if (wanted >= static_cast<double>(maximum_arc_chords)) return maximum_arc_chords;
    return static_cast<std::size_t>(wanted);
}

// The flattening's output: the caller's arrays and how much of them is
// written. Each write checks the room first; a full array sets `truncated`.
class FlattenWriter final {
public:
    FlattenWriter(std::span<Point> points, std::span<FlattenedContour> contours) noexcept
        : points_(points), contours_(contours) {}

    [[nodiscard]] bool full() const noexcept { return result_.truncated; }
    [[nodiscard]] bool in_contour() const noexcept { return result_.point_count > contour_start_; }

    // Appends `point` to the open contour (starting one when none is open).
    void add(Point point) noexcept {
        if (result_.truncated) return;
        if (result_.point_count == points_.size()) {
            result_.truncated = true;
            return;
        }
        points_[result_.point_count] = point;
        result_.point_count = result_.point_count + 1U;
    }

    // Ends the open contour; one of fewer than two points is dropped.
    void end_contour(bool closed) noexcept {
        const std::size_t length = result_.point_count - contour_start_;
        if (length < 2U) {
            result_.point_count = contour_start_;
            return;
        }
        if (result_.contour_count == contours_.size()) {
            result_.point_count = contour_start_;
            result_.truncated = true;
            return;
        }
        contours_[result_.contour_count].end = result_.point_count;
        contours_[result_.contour_count].closed = closed;
        result_.contour_count = result_.contour_count + 1U;
        contour_start_ = result_.point_count;
    }

    [[nodiscard]] FlattenedPath finish() noexcept {
        end_contour(false);
        return result_;
    }

private:
    std::span<Point> points_;
    std::span<FlattenedContour> contours_;
    std::size_t contour_start_{};
    FlattenedPath result_{};
};

void add_quadratic(FlattenWriter& writer, Point from, Point control, Point to, double tolerance) noexcept {
    const double second_x = from.x - 2.0 * control.x + to.x;
    const double second_y = from.y - 2.0 * control.y + to.y;
    const std::size_t chords = bezier_chords(2.0, distance_from_origin(second_x, second_y), tolerance);
    const double step = 1.0 / static_cast<double>(chords);
    for (std::size_t index = 1U; index <= chords && !writer.full(); ++index) {
        const double t = static_cast<double>(index) * step;
        const double u = 1.0 - t;
        const Point point{u * u * from.x + 2.0 * u * t * control.x + t * t * to.x,
                          u * u * from.y + 2.0 * u * t * control.y + t * t * to.y};
        writer.add(index == chords ? to : point);
    }
}

void add_cubic(FlattenWriter& writer, Point from, Point first, Point second, Point to, double tolerance) noexcept {
    const double first_difference = distance_from_origin(from.x - 2.0 * first.x + second.x,
                                                         from.y - 2.0 * first.y + second.y);
    const double second_difference = distance_from_origin(first.x - 2.0 * second.x + to.x,
                                                          first.y - 2.0 * second.y + to.y);
    const std::size_t chords = bezier_chords(3.0, std::max(first_difference, second_difference), tolerance);
    const double step = 1.0 / static_cast<double>(chords);
    for (std::size_t index = 1U; index <= chords && !writer.full(); ++index) {
        const double t = static_cast<double>(index) * step;
        const double u = 1.0 - t;
        const double a = u * u * u;
        const double b = 3.0 * u * u * t;
        const double c = 3.0 * u * t * t;
        const double d = t * t * t;
        const Point point{a * from.x + b * first.x + c * second.x + d * to.x,
                          a * from.y + b * first.y + c * second.y + d * to.y};
        writer.add(index == chords ? to : point);
    }
}

// The arc's points after its start (which the caller has added).
void add_arc(FlattenWriter& writer, Point center, Point radii, Point angles, double tolerance) noexcept {
    const double sweep = held_sweep(angles.y);
    const std::size_t chords = arc_chords(std::max(radii.x, radii.y), sweep, tolerance);
    const double step = sweep / static_cast<double>(chords);
    for (std::size_t index = 1U; index <= chords && !writer.full(); ++index) {
        writer.add(arc_point(center, radii, angles.x + static_cast<double>(index) * step));
    }
}

void include_point(Rect& box, bool& any, Point point) noexcept {
    if (!any) {
        box = Rect{point.x, point.y, 0.0, 0.0};
        any = true;
        return;
    }
    const double left = std::min(box.x, point.x);
    const double top = std::min(box.y, point.y);
    const double right = std::max(box.x + box.width, point.x);
    const double bottom = std::max(box.y + box.height, point.y);
    box = Rect{left, top, right - left, bottom - top};
}

} // namespace

std::size_t paint_path_points_for(PaintPathVerb verb) noexcept {
    switch (verb) {
    case PaintPathVerb::move_to: return 1U;
    case PaintPathVerb::line_to: return 1U;
    case PaintPathVerb::quad_to: return 2U;
    case PaintPathVerb::cubic_to: return 3U;
    case PaintPathVerb::arc_to: return 3U;
    case PaintPathVerb::close: return 0U;
    }
    return 0U;
}

bool valid_paint_path(PaintPathView path) noexcept {
    if (path.verbs.empty() || path.verbs.size() > maximum_paint_path_verbs ||
        path.points.size() > maximum_paint_path_points) {
        return false;
    }
    std::size_t consumed = 0U;
    bool have_current = false;
    for (const PaintPathVerb verb : path.verbs) {
        const bool known = verb == PaintPathVerb::move_to || verb == PaintPathVerb::line_to ||
                           verb == PaintPathVerb::quad_to || verb == PaintPathVerb::cubic_to ||
                           verb == PaintPathVerb::arc_to || verb == PaintPathVerb::close;
        if (!known) return false;
        const std::size_t count = paint_path_points_for(verb);
        if (count > path.points.size() - consumed) return false;
        for (std::size_t index = consumed; index < consumed + count; ++index) {
            if (!finite_point(path.points[index])) return false;
        }
        const bool starts = verb == PaintPathVerb::move_to || verb == PaintPathVerb::arc_to;
        if (!starts && verb != PaintPathVerb::close && !have_current) return false;
        if (verb == PaintPathVerb::close && !have_current) return false;
        if (verb == PaintPathVerb::arc_to) {
            const Point radii = path.points[consumed + 1U];
            if (!(radii.x > 0.0) || !(radii.y > 0.0)) return false;
        }
        have_current = true;
        consumed = consumed + count;
    }
    return consumed == path.points.size();
}

Rect paint_path_bounds(PaintPathView path) noexcept {
    if (!valid_paint_path(path)) return Rect{};
    Rect box{};
    bool any = false;
    std::size_t consumed = 0U;
    for (const PaintPathVerb verb : path.verbs) {
        const std::size_t count = paint_path_points_for(verb);
        if (verb == PaintPathVerb::arc_to) {
            const Point center = path.points[consumed];
            const Point radii = path.points[consumed + 1U];
            include_point(box, any, Point{center.x - radii.x, center.y - radii.y});
            include_point(box, any, Point{center.x + radii.x, center.y + radii.y});
        } else {
            for (std::size_t index = consumed; index < consumed + count; ++index) {
                include_point(box, any, path.points[index]);
            }
        }
        consumed = consumed + count;
    }
    return box;
}

double stroke_bounds_outset(double width, double miter_limit) noexcept {
    constexpr double square_cap_diagonal = 1.4142135623730951;
    const double reach = std::max(std::max(miter_limit, 1.0), square_cap_diagonal);
    const double outset = width * 0.5 * reach;
    return outset;
}

FlattenedPath flatten_paint_path(PaintPathView path, double tolerance, std::span<Point> points,
                                 std::span<FlattenedContour> contours) noexcept {
    if (!(tolerance > 0.0) || !std::isfinite(tolerance) || !valid_paint_path(path)) return FlattenedPath{};
    FlattenWriter writer(points, contours);
    Point current{};
    Point contour_start{};
    bool have_current = false;
    std::size_t consumed = 0U;
    for (const PaintPathVerb verb : path.verbs) {
        if (writer.full()) break;
        const std::span<const Point> operands = path.points.subspan(consumed, paint_path_points_for(verb));
        consumed = consumed + operands.size();
        // After a close the current point is the closed contour's start,
        // and the next segment starts a new contour there (an arc with no
        // current point at all starts at its own start instead).
        const bool continues = verb != PaintPathVerb::move_to && verb != PaintPathVerb::close;
        if (continues && have_current && !writer.in_contour()) writer.add(current);
        have_current = true;
        switch (verb) {
        case PaintPathVerb::move_to:
            writer.end_contour(false);
            current = operands[0];
            contour_start = current;
            writer.add(current);
            break;
        case PaintPathVerb::line_to:
            current = operands[0];
            writer.add(current);
            break;
        case PaintPathVerb::quad_to:
            add_quadratic(writer, current, operands[0], operands[1], tolerance);
            current = operands[1];
            break;
        case PaintPathVerb::cubic_to:
            add_cubic(writer, current, operands[0], operands[1], operands[2], tolerance);
            current = operands[2];
            break;
        case PaintPathVerb::arc_to: {
            const Point start = arc_point(operands[0], operands[1], operands[2].x);
            writer.add(start);
            add_arc(writer, operands[0], operands[1], operands[2], tolerance);
            current = arc_point(operands[0], operands[1], operands[2].x + held_sweep(operands[2].y));
            break;
        }
        case PaintPathVerb::close:
            if (writer.in_contour()) writer.add(contour_start);
            writer.end_contour(true);
            current = contour_start;
            break;
        default:
            // Unreachable for a valid path; ends the contour as close would.
            writer.end_contour(false);
            break;
        }
    }
    return writer.finish();
}

void PaintPath::reserve(std::size_t verb_capacity, std::size_t point_capacity) {
    verbs_.reserve(verb_capacity);
    points_.reserve(point_capacity);
}

void PaintPath::clear() noexcept {
    verbs_.clear();
    points_.clear();
}

void PaintPath::move_to(Point point) {
    verbs_.push_back(PaintPathVerb::move_to);
    points_.push_back(point);
}

void PaintPath::line_to(Point point) {
    verbs_.push_back(PaintPathVerb::line_to);
    points_.push_back(point);
}

void PaintPath::quad_to(Point control, Point end) {
    verbs_.push_back(PaintPathVerb::quad_to);
    points_.push_back(control);
    points_.push_back(end);
}

void PaintPath::cubic_to(Point first_control, Point second_control, Point end) {
    verbs_.push_back(PaintPathVerb::cubic_to);
    points_.push_back(first_control);
    points_.push_back(second_control);
    points_.push_back(end);
}

void PaintPath::arc_to(Point center, Size radii, double start_degrees, double sweep_degrees) {
    verbs_.push_back(PaintPathVerb::arc_to);
    points_.push_back(center);
    points_.push_back(Point{radii.width, radii.height});
    points_.push_back(Point{start_degrees, sweep_degrees});
}

void PaintPath::close() {
    verbs_.push_back(PaintPathVerb::close);
}

PaintPathView PaintPath::view() const& noexcept {
    PaintPathView path{};
    path.verbs = std::span<const PaintPathVerb>(verbs_.data(), verbs_.size());
    path.points = std::span<const Point>(points_.data(), points_.size());
    return path;
}

} // namespace gui_forms
