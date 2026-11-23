// Curved paths (paint_path.hpp, exhibit gap E7): validation, conservative
// bounds, flattening into caller storage for the base painter's fallbacks,
// and the owned builder. Renderer-neutral; the Skia painter draws the
// verbs exactly instead of flattening them.
#include "gui_forms/types/paint_path/paint_path.hpp"
#include "stxlib/math.hpp"

#include <algorithm>
#include <array>
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
    double turn_sine = 0.0;
    double turn_cosine = 0.0;
    stxlib::math::sincosd(degrees, turn_sine, turn_cosine);
    const Point point{center.x + radii.x * turn_cosine, center.y + radii.y * turn_sine};
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

// Path length and trimming. The path is walked as a sequence of segments
// (SegmentCursor), each measured on its own chords (segment_chords); the
// measuring pass and the cutting pass sum the same chord lengths in the
// same order, so a cut at the path's measured end lands exactly on it.
namespace {

enum class SegmentKind : std::uint8_t { line, quadratic, cubic, arc };

// One segment of a path. `points` by kind: line 0 and 1 (from, to);
// quadratic 0 to 2; cubic 0 to 3; arc 0 the centre, 1 the radii, 2 the
// start and the sweep (held to a turn), in degrees. `starts_contour` marks
// a contour's first segment, `closes_contour` a close's closing segment.
struct PathSegment final {
    SegmentKind kind{SegmentKind::line};
    std::array<Point, 4> points{};
    bool starts_contour{};
    bool closes_contour{};
};

PathSegment line_segment(Point from, Point to) noexcept {
    PathSegment segment{};
    segment.kind = SegmentKind::line;
    segment.points[0] = from;
    segment.points[1] = to;
    return segment;
}

// Walks a valid path's segments in order, with the contour rules of
// flatten_paint_path: move_to starts a contour; a drawing verb after a
// close starts a new contour at the closed contour's start; arc_to with a
// current point first yields the straight join to its start (a segment of
// its own), and with none starts a contour at its start; close yields the
// closing segment (of length 0 when the contour already ends at its start).
class SegmentCursor final {
public:
    explicit SegmentCursor(PaintPathView path) noexcept : path_(path) {}

    // Writes the next segment into `segment`; false after the last.
    [[nodiscard]] bool next(PathSegment& segment) noexcept {
        if (pending_arc_) {
            pending_arc_ = false;
            segment = arc_;
            mark(segment);
            current_ = arc_end_;
            return true;
        }
        while (verb_ < path_.verbs.size()) {
            const PaintPathVerb verb = path_.verbs[verb_];
            const std::span<const Point> operands = path_.points.subspan(consumed_, paint_path_points_for(verb));
            verb_ = verb_ + 1U;
            consumed_ = consumed_ + operands.size();
            switch (verb) {
            case PaintPathVerb::move_to:
                current_ = operands[0];
                contour_start_ = current_;
                have_current_ = true;
                fresh_ = true;
                break;
            case PaintPathVerb::line_to:
                segment = line_segment(current_, operands[0]);
                mark(segment);
                current_ = operands[0];
                return true;
            case PaintPathVerb::quad_to:
                segment = PathSegment{};
                segment.kind = SegmentKind::quadratic;
                segment.points = {current_, operands[0], operands[1], Point{}};
                mark(segment);
                current_ = operands[1];
                return true;
            case PaintPathVerb::cubic_to:
                segment = PathSegment{};
                segment.kind = SegmentKind::cubic;
                segment.points = {current_, operands[0], operands[1], operands[2]};
                mark(segment);
                current_ = operands[2];
                return true;
            case PaintPathVerb::arc_to: {
                const Point angles{operands[2].x, held_sweep(operands[2].y)};
                arc_ = PathSegment{};
                arc_.kind = SegmentKind::arc;
                arc_.points = {operands[0], operands[1], angles, Point{}};
                const Point start = arc_point(operands[0], operands[1], angles.x);
                arc_end_ = arc_point(operands[0], operands[1], angles.x + angles.y);
                if (!have_current_) {
                    have_current_ = true;
                    contour_start_ = start;
                    fresh_ = true;
                    segment = arc_;
                    mark(segment);
                    current_ = arc_end_;
                    return true;
                }
                segment = line_segment(current_, start);
                mark(segment);
                current_ = start;
                pending_arc_ = true;
                return true;
            }
            case PaintPathVerb::close:
                segment = line_segment(current_, contour_start_);
                segment.closes_contour = true;
                mark(segment);
                current_ = contour_start_;
                fresh_ = true;
                return true;
            default:
                // Unreachable for a valid path; ends the walk.
                return false;
            }
        }
        return false;
    }

private:
    void mark(PathSegment& segment) noexcept {
        segment.starts_contour = fresh_;
        fresh_ = false;
    }

    PaintPathView path_;
    std::size_t verb_{};
    std::size_t consumed_{};
    Point current_{};
    Point contour_start_{};
    bool have_current_{};
    bool fresh_{true};
    bool pending_arc_{};
    PathSegment arc_{};
    Point arc_end_{};
};

double point_distance(Point from, Point to) noexcept {
    const double length = stxlib::math::hypot(to.x - from.x, to.y - from.y);
    return length;
}

// The measuring chord count of `segment` at `tolerance` (the header's
// rules). An arc's chord turns by `step` radians, whose sagitta
// r (1 - cos(step / 2)) is at most r step^2 / 8; step = sqrt(8 tolerance / r)
// keeps it within the tolerance with sqrt alone.
std::size_t segment_chords(const PathSegment& segment, double tolerance) noexcept {
    constexpr double quarter_turn = 3.14159265358979323846 * 0.5;
    const std::array<Point, 4>& p = segment.points;
    double wanted = 1.0;
    std::size_t limit = maximum_measured_curve_chords;
    switch (segment.kind) {
    case SegmentKind::line: return 1U;
    case SegmentKind::quadratic: {
        const double second = stxlib::math::hypot(p[0].x - 2.0 * p[1].x + p[2].x, p[0].y - 2.0 * p[1].y + p[2].y);
        wanted = std::ceil(std::sqrt(0.25 * second / tolerance));
        break;
    }
    case SegmentKind::cubic: {
        const double first = stxlib::math::hypot(p[0].x - 2.0 * p[1].x + p[2].x, p[0].y - 2.0 * p[1].y + p[2].y);
        const double second = stxlib::math::hypot(p[1].x - 2.0 * p[2].x + p[3].x, p[1].y - 2.0 * p[2].y + p[3].y);
        wanted = std::ceil(std::sqrt(0.75 * std::max(first, second) / tolerance));
        break;
    }
    case SegmentKind::arc: {
        const double radius = std::max(p[1].x, p[1].y);
        const double sweep = std::fabs(p[2].y) * degrees_to_radians;
        const double step = std::min(std::sqrt(8.0 * tolerance / radius), quarter_turn);
        wanted = std::ceil(sweep / step);
        limit = maximum_measured_arc_chords;
        break;
    }
    default: return 1U;
    }
    if (!(wanted >= 1.0)) return 1U;
    if (wanted >= static_cast<double>(limit)) return limit;
    return static_cast<std::size_t>(wanted);
}

// The segment's point at parameter `t` in [0, 1]: Bernstein forms for the
// line and the curves (exactly the end points at 0 and 1), the angle
// start + t sweep for the arc.
Point segment_point(const PathSegment& segment, double t) noexcept {
    const std::array<Point, 4>& p = segment.points;
    const double u = 1.0 - t;
    switch (segment.kind) {
    case SegmentKind::line: return Point{u * p[0].x + t * p[1].x, u * p[0].y + t * p[1].y};
    case SegmentKind::quadratic: {
        const double a = u * u;
        const double b = 2.0 * u * t;
        const double c = t * t;
        return Point{a * p[0].x + b * p[1].x + c * p[2].x, a * p[0].y + b * p[1].y + c * p[2].y};
    }
    case SegmentKind::cubic: {
        const double a = u * u * u;
        const double b = 3.0 * u * u * t;
        const double c = 3.0 * u * t * t;
        const double d = t * t * t;
        return Point{a * p[0].x + b * p[1].x + c * p[2].x + d * p[3].x,
                     a * p[0].y + b * p[1].y + c * p[2].y + d * p[3].y};
    }
    case SegmentKind::arc: {
        const double angle = t == 1.0 ? p[2].x + p[2].y : p[2].x + t * p[2].y;
        return arc_point(p[0], p[1], angle);
    }
    default: return p[0];
    }
}

// Chord `index` of `chords` ends at parameter index / chords.
double chord_parameter(std::size_t index, std::size_t chords) noexcept {
    const double t = static_cast<double>(index) / static_cast<double>(chords);
    return t;
}

double segment_length(const PathSegment& segment, std::size_t chords) noexcept {
    double length = 0.0;
    Point previous = segment_point(segment, 0.0);
    for (std::size_t index = 1U; index <= chords; ++index) {
        const Point point = segment_point(segment, chord_parameter(index, chords));
        length = length + point_distance(previous, point);
        previous = point;
    }
    return length;
}

// The parameter where the segment's measured length reaches `distance`
// (within [0, its length]): the chord holding it, interpolated linearly.
double parameter_at(const PathSegment& segment, std::size_t chords, double distance) noexcept {
    double walked = 0.0;
    Point previous = segment_point(segment, 0.0);
    for (std::size_t index = 1U; index <= chords; ++index) {
        const Point point = segment_point(segment, chord_parameter(index, chords));
        const double chord = point_distance(previous, point);
        if (walked + chord >= distance) {
            const double share = chord > 0.0 ? std::clamp((distance - walked) / chord, 0.0, 1.0) : 0.0;
            const double t = (static_cast<double>(index - 1U) + share) / static_cast<double>(chords);
            return t;
        }
        walked = walked + chord;
        previous = point;
    }
    return 1.0;
}

// The quadratic's blossom at (a, b): the control points of its piece over
// [t0, t1] are blossom(t0, t0), blossom(t0, t1), blossom(t1, t1).
Point quadratic_blossom(const std::array<Point, 4>& p, double a, double b) noexcept {
    const double w0 = (1.0 - a) * (1.0 - b);
    const double w1 = (1.0 - a) * b + a * (1.0 - b);
    const double w2 = a * b;
    return Point{w0 * p[0].x + w1 * p[1].x + w2 * p[2].x, w0 * p[0].y + w1 * p[1].y + w2 * p[2].y};
}

// The cubic's blossom at (a, b, c); its piece over [t0, t1] has controls
// blossom(t0, t0, t0), (t0, t0, t1), (t0, t1, t1), (t1, t1, t1).
Point cubic_blossom(const std::array<Point, 4>& p, double a, double b, double c) noexcept {
    const double w0 = (1.0 - a) * (1.0 - b) * (1.0 - c);
    const double w1 = (1.0 - a) * (1.0 - b) * c + (1.0 - a) * b * (1.0 - c) + a * (1.0 - b) * (1.0 - c);
    const double w2 = (1.0 - a) * b * c + a * (1.0 - b) * c + a * b * (1.0 - c);
    const double w3 = a * b * c;
    return Point{w0 * p[0].x + w1 * p[1].x + w2 * p[2].x + w3 * p[3].x,
                 w0 * p[0].y + w1 * p[1].y + w2 * p[2].y + w3 * p[3].y};
}

// The start of the segment's piece from `t0`: the curves' first blossom
// control (so the piece's start and its first control agree in rounding).
Point piece_start(const PathSegment& segment, double t0) noexcept {
    if (segment.kind == SegmentKind::quadratic) return quadratic_blossom(segment.points, t0, t0);
    if (segment.kind == SegmentKind::cubic) return cubic_blossom(segment.points, t0, t0, t0);
    const Point start = segment_point(segment, t0);
    return start;
}

// Appends the segment's piece over [t0, t1] to `kept`, starting a contour
// at its start when `starting`.
void append_piece(const PathSegment& segment, double t0, double t1, bool starting, PaintPath& kept) {
    const std::array<Point, 4>& p = segment.points;
    if (starting) kept.move_to(piece_start(segment, t0));
    switch (segment.kind) {
    case SegmentKind::line: kept.line_to(segment_point(segment, t1)); break;
    case SegmentKind::quadratic: kept.quad_to(quadratic_blossom(p, t0, t1), quadratic_blossom(p, t1, t1)); break;
    case SegmentKind::cubic:
        kept.cubic_to(cubic_blossom(p, t0, t0, t1), cubic_blossom(p, t0, t1, t1), cubic_blossom(p, t1, t1, t1));
        break;
    case SegmentKind::arc: {
        const double start = p[2].x + t0 * p[2].y;
        const double sweep = (t1 - t0) * p[2].y;
        kept.arc_to(p[0], Size{p[1].x, p[1].y}, start, sweep);
        break;
    }
    default: break;
    }
}

// What the measuring pass finds: the whole length, the contours with
// positive or zero length that have segments, and whether the last of
// them is closed.
struct PathMeasure final {
    double length{};
    std::size_t contour_count{};
    bool last_closed{};
};

PathMeasure measure_path(PaintPathView path, double tolerance) noexcept {
    PathMeasure measure{};
    SegmentCursor cursor(path);
    PathSegment segment{};
    while (cursor.next(segment)) {
        if (segment.starts_contour) {
            measure.contour_count = measure.contour_count + 1U;
            measure.last_closed = false;
        }
        if (segment.closes_contour) measure.last_closed = true;
        const double length = segment_length(segment, segment_chords(segment, tolerance));
        measure.length = measure.length + length;
    }
    return measure;
}

// A kept distance range along the path, from `from` to `to`.
struct KeptRange final {
    double from{};
    double to{};
};

// Appends the pieces of `path` within `range` to `kept`. With
// `continues`, the range's first contour continues the contour `kept`
// already ends with (the wrapped ring) instead of starting one.
void append_range(PaintPathView path, double tolerance, KeptRange range, bool continues, PaintPath& kept) {
    SegmentCursor cursor(path);
    PathSegment segment{};
    double before = 0.0;
    bool pen_down = false;
    bool contour_whole = false;
    bool first_contour = true;
    while (cursor.next(segment)) {
        const std::size_t chords = segment_chords(segment, tolerance);
        const double length = segment_length(segment, chords);
        const double after = before + length;
        if (segment.starts_contour) {
            pen_down = first_contour && continues;
            first_contour = false;
            // Whole so far: the range holds the contour's start (and is
            // not continuing a cut one).
            contour_whole = !pen_down && range.from <= before && range.to > before;
        }
        const bool overlaps = range.from < after && range.to > before;
        if (length > 0.0 && overlaps) {
            const bool from_start = range.from <= before;
            const bool to_end = range.to >= after;
            if (!to_end) contour_whole = false;
            if (segment.closes_contour && to_end && contour_whole && pen_down) {
                kept.close();
            } else {
                const double t0 = from_start ? 0.0 : parameter_at(segment, chords, range.from - before);
                const double t1 = to_end ? 1.0 : parameter_at(segment, chords, range.to - before);
                append_piece(segment, t0, t1, !pen_down, kept);
                pen_down = true;
            }
        } else if (length == 0.0 && segment.closes_contour && contour_whole && pen_down && range.to >= after) {
            // A contour already back at its start keeps its close.
            kept.close();
        }
        before = after;
    }
}

} // namespace

stxlib::Optional<double> paint_path_length(PaintPathView path, double tolerance) noexcept {
    if (!(tolerance > 0.0) || !std::isfinite(tolerance) || !valid_paint_path(path)) return stxlib::Optional<double>::none();
    const PathMeasure measure = measure_path(path, tolerance);
    const stxlib::Optional<double> result = stxlib::Optional<double>::of(measure.length);
    return result;
}

bool valid_stroke_trim(StrokeTrim trim) noexcept {
    const bool start = std::isfinite(trim.start) && trim.start >= 0.0 && trim.start <= 1.0;
    const bool end = std::isfinite(trim.end) && trim.end >= 0.0 && trim.end <= 1.0;
    return start && end && std::isfinite(trim.offset);
}

bool stroke_trim_keeps_whole(StrokeTrim trim) noexcept {
    const double low = std::min(trim.start, trim.end);
    const double high = std::max(trim.start, trim.end);
    return low == 0.0 && high == 1.0;
}

PaintPathTrimResult trim_paint_path(PaintPathView path, StrokeTrim trim, double tolerance, PaintPath& kept) {
    if (!(tolerance > 0.0) || !std::isfinite(tolerance) || !valid_stroke_trim(trim) || !valid_paint_path(path)) {
        return PaintPathTrimResult::refused;
    }
    kept.clear();
    if (stroke_trim_keeps_whole(trim)) return PaintPathTrimResult::whole;
    const double low = std::min(trim.start, trim.end);
    const double share = std::max(trim.start, trim.end) - low;
    const PathMeasure measure = measure_path(path, tolerance);
    if (!(share > 0.0) || !(measure.length > 0.0)) return PaintPathTrimResult::empty;
    // The kept part's start as a fraction in [0, 1); x - floor(x) can
    // round up to 1 for a tiny negative x, which is the path's start.
    double from_fraction = low + trim.offset;
    from_fraction = from_fraction - std::floor(from_fraction);
    if (!(from_fraction < 1.0)) from_fraction = 0.0;
    const double to_fraction = from_fraction + share;
    const double from = from_fraction * measure.length;
    kept.reserve(path.verbs.size() * 2U + 8U, path.points.size() * 2U + 8U);
    if (to_fraction <= 1.0) {
        append_range(path, tolerance, KeptRange{from, to_fraction * measure.length}, false, kept);
    } else {
        const double wrapped = (to_fraction - 1.0) * measure.length;
        const bool ring = measure.contour_count == 1U && measure.last_closed;
        if (ring) {
            append_range(path, tolerance, KeptRange{from, measure.length}, false, kept);
            const bool continues = !kept.view().verbs.empty();
            append_range(path, tolerance, KeptRange{0.0, wrapped}, continues, kept);
        } else {
            append_range(path, tolerance, KeptRange{0.0, wrapped}, false, kept);
            append_range(path, tolerance, KeptRange{from, measure.length}, false, kept);
        }
    }
    if (kept.view().verbs.empty()) return PaintPathTrimResult::empty;
    return PaintPathTrimResult::trimmed;
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
