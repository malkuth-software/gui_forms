#include "../support/drawing_support.hpp"

GraphicsPath::GraphicsPath(FillMode fill_mode) : fill_mode_(fill_mode) {
    require_enum(fill_mode, 1U, "path fill mode");
}

FillMode GraphicsPath::fill_mode() const {
    require_alive();
    return fill_mode_;
}

void GraphicsPath::set_fill_mode(FillMode fill_mode) {
    require_alive();
    require_enum(fill_mode, 1U, "path fill mode");
    fill_mode_ = fill_mode;
}

void GraphicsPath::reset() {
    require_alive();
    elements_.clear();
}

void GraphicsPath::start_figure() {
    require_alive();
    append({PathVerb::start_figure});
}

void GraphicsPath::close_figure() {
    require_alive();
    append({PathVerb::close_figure});
}

void GraphicsPath::add_line(PointF from, PointF to) {
    require_alive();
    require_finite(from, "path line start");
    require_finite(to, "path line end");
    PathElement element;
    element.verb = PathVerb::line;
    element.first = from;
    element.second = to;
    append(std::move(element));
}

void GraphicsPath::add_quadratic(PointF from, PointF control, PointF to) {
    require_alive();
    require_finite(from, "path quadratic start");
    require_finite(control, "path quadratic control");
    require_finite(to, "path quadratic end");
    PathElement element;
    element.verb = PathVerb::quadratic;
    element.first = from;
    element.second = control;
    element.third = to;
    append(std::move(element));
}

void GraphicsPath::add_bezier(PointF from, PointF control1,
                              PointF control2, PointF to) {
    require_alive();
    require_finite(from, "path bezier start");
    require_finite(control1, "path bezier first control");
    require_finite(control2, "path bezier second control");
    require_finite(to, "path bezier end");
    PathElement element;
    element.verb = PathVerb::bezier;
    element.first = from;
    element.second = control1;
    element.third = control2;
    element.fourth = to;
    append(std::move(element));
}

void GraphicsPath::add_beziers(std::span<const PointF> points) {
    require_alive();
    if (points.size() < 4U || (points.size() - 1U) % 3U != 0U) {
        throw std::invalid_argument(
            "path bezier sequence requires 4 + 3n points");
    }
    const std::size_t count = (points.size() - 1U) / 3U;
    if (count > maximum_elements - elements_.size()) {
        throw std::length_error("GUI.Drawing path element limit exceeded");
    }
    for (const PointF point : points) require_finite(point, "path bezier point");
    std::vector<PathElement> appended;
    appended.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        PathElement element;
        element.verb = PathVerb::bezier;
        element.first = points[index * 3U];
        element.second = points[index * 3U + 1U];
        element.third = points[index * 3U + 2U];
        element.fourth = points[index * 3U + 3U];
        appended.push_back(element);
    }
    elements_.insert(elements_.end(), appended.begin(), appended.end());
}

void GraphicsPath::add_polygon(std::span<const PointF> points) {
    require_alive();
    if (points.size() < 3U) {
        throw std::invalid_argument("path polygon requires at least three points");
    }
    // start + (n - 1) edges + closing edge + close verb = n + 2.
    if (points.size() + 2U > maximum_elements - elements_.size()) {
        throw std::length_error("GUI.Drawing path element limit exceeded");
    }
    for (const PointF point : points) require_finite(point, "path polygon point");
    std::vector<PathElement> appended;
    appended.reserve(points.size() + 2U);
    appended.push_back({PathVerb::start_figure});
    for (std::size_t index = 1U; index < points.size(); ++index) {
        PathElement line;
        line.verb = PathVerb::line;
        line.first = points[index - 1U];
        line.second = points[index];
        appended.push_back(line);
    }
    PathElement closing;
    closing.verb = PathVerb::line;
    closing.first = points.back();
    closing.second = points.front();
    appended.push_back(closing);
    appended.push_back({PathVerb::close_figure});
    elements_.insert(elements_.end(), appended.begin(), appended.end());
}

void GraphicsPath::add_rectangle(RectF rectangle) {
    require_alive();
    require_finite(rectangle, "path rectangle");
    PathElement element;
    element.verb = PathVerb::rectangle;
    element.rect = rectangle;
    append(std::move(element));
}

void GraphicsPath::add_ellipse(RectF bounds) {
    require_alive();
    require_finite(bounds, "path ellipse");
    PathElement element;
    element.verb = PathVerb::ellipse;
    element.rect = bounds;
    append(std::move(element));
}

void GraphicsPath::add_rounded_rectangle(RectF rectangle, CornerRadii radii) {
    require_alive();
    require_finite(rectangle, "path rounded rectangle");
    const std::array<double, 4> given{radii.top_left, radii.top_right, radii.bottom_right, radii.bottom_left};
    for (const double radius : given) {
        require_finite(radius, "path corner radius");
        if (radius < 0.0) throw std::invalid_argument("path corner radii may not be negative");
    }
    if (rectangle.empty()) throw std::invalid_argument("path rounded rectangle must be nonempty");
    // start, four edges, four corners, close.
    constexpr std::size_t appended_count = 10U;
    if (appended_count > maximum_elements - elements_.size()) {
        throw std::length_error("GUI.Drawing path element limit exceeded");
    }
    const double limit = std::min(rectangle.width, rectangle.height) * 0.5;
    const double top_left = std::min(radii.top_left, limit);
    const double top_right = std::min(radii.top_right, limit);
    const double bottom_right = std::min(radii.bottom_right, limit);
    const double bottom_left = std::min(radii.bottom_left, limit);
    // The control-point distance of a cubic quarter circle, per unit radius.
    constexpr double kappa = 0.55228474983079339840;
    const double left = rectangle.x;
    const double top = rectangle.y;
    const double right = rectangle.right();
    const double bottom = rectangle.bottom();
    std::vector<PathElement> appended;
    appended.reserve(appended_count);
    appended.push_back({PathVerb::start_figure});
    PathElement edge;
    edge.verb = PathVerb::line;
    PathElement corner;
    corner.verb = PathVerb::bezier;
    // Each element starts exactly where the previous ended, so the figure
    // stays one contour.
    edge.first = PointF{left + top_left, top};
    edge.second = PointF{right - top_right, top};
    appended.push_back(edge);
    corner.first = edge.second;
    corner.second = PointF{right - top_right + top_right * kappa, top};
    corner.third = PointF{right, top + top_right - top_right * kappa};
    corner.fourth = PointF{right, top + top_right};
    appended.push_back(corner);
    edge.first = corner.fourth;
    edge.second = PointF{right, bottom - bottom_right};
    appended.push_back(edge);
    corner.first = edge.second;
    corner.second = PointF{right, bottom - bottom_right + bottom_right * kappa};
    corner.third = PointF{right - bottom_right + bottom_right * kappa, bottom};
    corner.fourth = PointF{right - bottom_right, bottom};
    appended.push_back(corner);
    edge.first = corner.fourth;
    edge.second = PointF{left + bottom_left, bottom};
    appended.push_back(edge);
    corner.first = edge.second;
    corner.second = PointF{left + bottom_left - bottom_left * kappa, bottom};
    corner.third = PointF{left, bottom - bottom_left + bottom_left * kappa};
    corner.fourth = PointF{left, bottom - bottom_left};
    appended.push_back(corner);
    edge.first = corner.fourth;
    edge.second = PointF{left, top + top_left};
    appended.push_back(edge);
    corner.first = edge.second;
    corner.second = PointF{left, top + top_left - top_left * kappa};
    corner.third = PointF{left + top_left - top_left * kappa, top};
    corner.fourth = PointF{left + top_left, top};
    appended.push_back(corner);
    appended.push_back({PathVerb::close_figure});
    elements_.insert(elements_.end(), appended.begin(), appended.end());
}

void GraphicsPath::add_arc(RectF bounds, double start_angle,
                           double sweep_angle) {
    require_alive();
    require_finite(bounds, "path arc bounds");
    require_finite(start_angle, "path arc start angle");
    require_finite(sweep_angle, "path arc sweep angle");
    if (bounds.empty() || sweep_angle == 0.0 ||
        std::abs(start_angle) > 1'000'000.0 ||
        std::abs(sweep_angle) > 1'000'000.0) {
        throw std::invalid_argument("path arc requires bounded nonempty geometry");
    }
    PathElement element;
    element.verb = PathVerb::arc;
    element.rect = bounds;
    element.start_angle = start_angle;
    element.sweep_angle = sweep_angle;
    append(std::move(element));
}

void GraphicsPath::add_pie(RectF bounds, double start_angle,
                           double sweep_angle) {
    require_alive();
    // Validate atomically before appending the five-element closed figure.
    require_finite(bounds, "path pie bounds");
    require_finite(start_angle, "path pie start angle");
    require_finite(sweep_angle, "path pie sweep angle");
    if (bounds.empty() || sweep_angle == 0.0 ||
        std::abs(start_angle) > 1'000'000.0 ||
        std::abs(sweep_angle) > 1'000'000.0) {
        throw std::invalid_argument("path pie requires bounded nonempty geometry");
    }
    if (5U > maximum_elements - elements_.size()) {
        throw std::length_error("GUI.Drawing path element limit exceeded");
    }
    const PointF center{bounds.x + bounds.width * 0.5,
                        bounds.y + bounds.height * 0.5};
    const PointF start = point_on_ellipse(bounds, start_angle);
    const PointF end = point_on_ellipse(bounds, start_angle + sweep_angle);
    PathElement radial_start{PathVerb::line};
    radial_start.first = center;
    radial_start.second = start;
    PathElement arc{PathVerb::arc};
    arc.rect = bounds;
    arc.start_angle = start_angle;
    arc.sweep_angle = sweep_angle;
    PathElement radial_end{PathVerb::line};
    radial_end.first = end;
    radial_end.second = center;
    elements_.push_back({PathVerb::start_figure});
    elements_.push_back(radial_start);
    elements_.push_back(arc);
    elements_.push_back(radial_end);
    elements_.push_back({PathVerb::close_figure});
}

void GraphicsPath::add_path(const GraphicsPath& path, bool connect) {
    require_alive();
    const PathSnapshot source = path.snapshot();
    if (source.elements.empty()) return;
    const std::vector<PointF> left = connect && !elements_.empty() ?
        path_points() : std::vector<PointF>{};
    const std::vector<PointF> right = connect && !elements_.empty() ?
        path.path_points() : std::vector<PointF>{};
    const bool add_connector = !left.empty() && !right.empty();
    const std::size_t extra = source.elements.size() + (add_connector ? 1U : 0U);
    if (extra > maximum_elements - elements_.size()) {
        throw std::length_error("GUI.Drawing path element limit exceeded");
    }
    std::vector<PathElement> replacement = elements_;
    replacement.reserve(elements_.size() + extra);
    if (add_connector) {
        PathElement connector;
        connector.verb = PathVerb::line;
        connector.first = left.back();
        connector.second = right.front();
        replacement.push_back(connector);
    }
    replacement.insert(replacement.end(), source.elements.begin(), source.elements.end());
    elements_.swap(replacement);
}

namespace {

// One element's geometry through `matrix`; shapes keep their bounds' box.
void transform_element(PathElement& element, const Matrix& matrix) {
    switch (element.verb) {
    case PathVerb::line:
        element.first = matrix.transform(element.first);
        element.second = matrix.transform(element.second);
        return;
    case PathVerb::quadratic:
        element.first = matrix.transform(element.first);
        element.second = matrix.transform(element.second);
        element.third = matrix.transform(element.third);
        return;
    case PathVerb::bezier:
        element.first = matrix.transform(element.first);
        element.second = matrix.transform(element.second);
        element.third = matrix.transform(element.third);
        element.fourth = matrix.transform(element.fourth);
        return;
    case PathVerb::rectangle:
    case PathVerb::ellipse:
    case PathVerb::arc:
        element.rect = matrix.transform_bounds(element.rect);
        return;
    case PathVerb::start_figure:
    case PathVerb::close_figure:
        // Figure marks carry no geometry.
        return;
    }
    // A verb outside the enumeration has no geometry to move.
    return;
}

} // namespace

void GraphicsPath::transform(const Matrix& matrix) {
    require_alive();
    if (!matrix.finite()) {
        throw std::invalid_argument("path transform must be finite");
    }
    std::vector<PathElement> transformed = elements_;
    for (PathElement& element : transformed) {
        transform_element(element, matrix);
    }
    elements_ = std::move(transformed);
}

bool GraphicsPath::is_visible(PointF point) const {
    require_alive();
    require_finite(point, "path visibility point");
    return path_snapshot_visible(snapshot(), point);
}

namespace {

// One element's points in the compatibility PathPoints order, appended
// to `result`.
void append_element_points(const PathElement& element, std::vector<PointF>& result) {
    switch (element.verb) {
    case PathVerb::line:
        if (result.empty() || result.back() != element.first) {
            result.push_back(element.first);
        }
        result.push_back(element.second);
        return;
    case PathVerb::quadratic:
        if (result.empty() || result.back() != element.first) {
            result.push_back(element.first);
        }
        result.push_back(element.second);
        result.push_back(element.third);
        return;
    case PathVerb::bezier:
        if (result.empty() || result.back() != element.first) {
            result.push_back(element.first);
        }
        result.push_back(element.second);
        result.push_back(element.third);
        result.push_back(element.fourth);
        return;
    case PathVerb::rectangle:
        result.insert(result.end(), {{element.rect.left(), element.rect.top()},
            {element.rect.right(), element.rect.top()},
            {element.rect.right(), element.rect.bottom()},
            {element.rect.left(), element.rect.bottom()}});
        return;
    case PathVerb::ellipse: {
        // The compatibility API exposes four cubic ellipse segments through
        // PathPoints:
        // the right-middle start, three points per quadrant, and the
        // repeated closing point. Keep that public geometry rather than a
        // four-cardinal approximation.
        constexpr double kappa = 0.55228474983079339840;
        const double center_x = element.rect.x + element.rect.width / 2.0;
        const double center_y = element.rect.y + element.rect.height / 2.0;
        const double radius_x = element.rect.width / 2.0;
        const double radius_y = element.rect.height / 2.0;
        const double control_x = radius_x * kappa;
        const double control_y = radius_y * kappa;
        result.insert(result.end(), {
            {center_x + radius_x, center_y},
            {center_x + radius_x, center_y + control_y},
            {center_x + control_x, center_y + radius_y},
            {center_x, center_y + radius_y},
            {center_x - control_x, center_y + radius_y},
            {center_x - radius_x, center_y + control_y},
            {center_x - radius_x, center_y},
            {center_x - radius_x, center_y - control_y},
            {center_x - control_x, center_y - radius_y},
            {center_x, center_y - radius_y},
            {center_x + control_x, center_y - radius_y},
            {center_x + radius_x, center_y - control_y},
            {center_x + radius_x, center_y},
        });
        return;
    }
    case PathVerb::arc: {
        const double center_x = element.rect.x + element.rect.width / 2.0;
        const double center_y = element.rect.y + element.rect.height / 2.0;
        const double radius_x = element.rect.width / 2.0;
        const double radius_y = element.rect.height / 2.0;
        constexpr double two_pi = 6.283185307179586476925286766559;
        constexpr double half_pi = 1.5707963267948966192313216916398;
        const double sweep = std::clamp(element.sweep_angle, -360.0, 360.0);
        double start = ellipse_parameter_angle(
            radius_x, radius_y, element.start_angle);
        double finish = ellipse_parameter_angle(
            radius_x, radius_y, element.start_angle + sweep);
        if (sweep > 0.0) {
            while (finish <= start) finish += two_pi;
            if (sweep == 360.0) finish = start + two_pi;
        } else {
            while (finish >= start) finish -= two_pi;
            if (sweep == -360.0) finish = start - two_pi;
        }
        result.push_back(ellipse_point(center_x, center_y, radius_x,
                                       radius_y, start));
        while ((sweep > 0.0 && start < finish) ||
               (sweep < 0.0 && start > finish)) {
            const double delta = sweep > 0.0
                ? std::min(half_pi, finish - start)
                : std::max(-half_pi, finish - start);
            const double end = start + delta;
            const double alpha = 4.0 / 3.0 * std::tan(delta / 4.0);
            const PointF first = ellipse_point(
                center_x, center_y, radius_x, radius_y, start);
            const PointF last = ellipse_point(
                center_x, center_y, radius_x, radius_y, end);
            result.push_back({first.x - alpha * radius_x * std::sin(start),
                              first.y + alpha * radius_y * std::cos(start)});
            result.push_back({last.x + alpha * radius_x * std::sin(end),
                              last.y - alpha * radius_y * std::cos(end)});
            result.push_back(last);
            start = end;
        }
        return;
    }
    case PathVerb::start_figure:
    case PathVerb::close_figure:
        // Figure marks carry no points.
        return;
    }
    // A verb outside the enumeration has no points.
    return;
}

} // namespace

std::vector<PointF> GraphicsPath::path_points() const {
    require_alive();
    std::vector<PointF> result;
    result.reserve(elements_.size() * 2U);
    for (const PathElement& element : elements_) {
        append_element_points(element, result);
    }
    return result;
}

std::unique_ptr<GraphicsPath> GraphicsPath::clone() const {
    require_alive();
    std::unique_ptr<gui_drawing::GraphicsPath> result = std::make_unique<GraphicsPath>(fill_mode_);
    (*result).elements_ = elements_;
    return result;
}

RectF GraphicsPath::bounds() const {
    require_alive();
    RectBoundsAccumulator accumulator;
    for (const PathElement& element : elements_) {
        include_path_element(element, accumulator);
    }
    return accumulator.bounds();
}

PathSnapshot GraphicsPath::snapshot() const {
    require_alive();
    return {fill_mode_, elements_};
}

void GraphicsPath::append(PathElement element) {
    if (elements_.size() >= maximum_elements) {
        throw std::length_error("GUI.Drawing path element limit exceeded");
    }
    elements_.push_back(std::move(element));
}


namespace {

constexpr double flatten_degrees_to_radians = 0.01745329251994329576923690768489;
constexpr double flatten_quarter_turn = 1.5707963267948966192313216916398;

// Where a flattening goes: its counts always, and its points and figures
// only on the writing pass (`writing`), whose arrays the caller sized from
// the counting pass. Both passes run the same code, so they count alike.
class FlattenSink final {
public:
    FlattenSink(std::span<PointF> points, std::span<FlattenedFigure> figures, bool writing) noexcept
        : points_(points), figures_(figures), writing_(writing) {}

    [[nodiscard]] bool exceeded() const noexcept { return exceeded_; }
    [[nodiscard]] bool has_current() const noexcept { return has_current_; }
    [[nodiscard]] PointF current() const noexcept { return current_; }
    [[nodiscard]] std::size_t point_count() const noexcept { return point_count_; }
    [[nodiscard]] std::size_t figure_count() const noexcept { return figure_count_; }

    // Whether `count` more points fit under maximum_flattened_points; when
    // not, the flattening is refused from here on.
    bool room_for(std::size_t count) noexcept {
        if (exceeded_) return false;
        if (count > GraphicsPath::maximum_flattened_points - point_count_) {
            exceeded_ = true;
            return false;
        }
        return true;
    }

    // Refuses the flattening from here on (an element needs more chords
    // than the limit allows).
    void refuse() noexcept { exceeded_ = true; }

    // Ends any open figure (open) and starts one at `first`.
    void begin_figure(PointF first) noexcept {
        end_figure(false);
        if (!room_for(1U)) return;
        has_current_ = true;
        put(first);
    }

    // Appends `point` to the open figure. The caller asked room_for first.
    void add(PointF point) noexcept { put(point); }

    // Ends the open figure, repeating its first point when `closed`; a
    // figure of fewer than two points is dropped.
    void end_figure(bool closed) noexcept {
        if (!has_current_) return;
        has_current_ = false;
        if (closed && point_count_ - figure_start_ >= 2U && room_for(1U)) put(figure_first_);
        if (exceeded_) return;
        if (point_count_ - figure_start_ < 2U) {
            point_count_ = figure_start_;
            return;
        }
        if (writing_) {
            figures_[figure_count_].end = point_count_;
            figures_[figure_count_].closed = closed;
        }
        figure_count_ = figure_count_ + 1U;
        figure_start_ = point_count_;
    }

private:
    void put(PointF point) noexcept {
        if (point_count_ == figure_start_) figure_first_ = point;
        if (writing_) points_[point_count_] = point;
        point_count_ = point_count_ + 1U;
        current_ = point;
    }

    std::span<PointF> points_;
    std::span<FlattenedFigure> figures_;
    bool writing_{};
    bool exceeded_{};
    bool has_current_{};
    PointF current_{};
    PointF figure_first_{};
    std::size_t figure_start_{};
    std::size_t point_count_{};
    std::size_t figure_count_{};
};

double flatten_length(double x, double y) noexcept {
    const double length = std::sqrt(x * x + y * y);
    return length;
}

// Chords for a Bezier curve of `degree` whose largest second difference
// has length `second_difference`. The curve's second derivative is at most
// degree (degree - 1) times that, and a chord over a parameter step h lies
// within h^2 / 8 of the second derivative's bound from the curve (Wang's
// formula). Zero when more than `limit` chords would be needed.
std::size_t bezier_chord_count(double degree, double second_difference, double tolerance,
                               std::size_t limit) noexcept {
    const double factor = degree * (degree - 1.0) / 8.0;
    const double wanted = std::ceil(std::sqrt(factor * second_difference / tolerance));
    if (!(wanted >= 1.0)) return 1U;
    if (!(wanted <= static_cast<double>(limit))) return 0U;
    return static_cast<std::size_t>(wanted);
}

// Chords for `sweep` radians of an ellipse whose larger radius is
// `radius`: on the circle, a chord over a step h lies within
// radius (1 - cos(h / 2)) of its arc, and the ellipse is the circle under
// a map that stretches no distance by more than the larger radius over
// the circle's. At most a quarter turn per chord. Zero when more than
// `limit` chords would be needed.
std::size_t arc_chord_count(double radius, double sweep, double tolerance, std::size_t limit) noexcept {
    const double magnitude = std::fabs(sweep);
    if (!(magnitude > 0.0)) return 1U;
    double step = flatten_quarter_turn;
    if (tolerance < radius) step = std::min(step, 2.0 * std::acos(1.0 - tolerance / radius));
    const double wanted = std::ceil(magnitude / step);
    if (!(wanted >= 1.0)) return 1U;
    if (!(wanted <= static_cast<double>(limit))) return 0U;
    return static_cast<std::size_t>(wanted);
}

// The chords of `element` (a quadratic or a cubic) after its start point,
// which the figure already holds.
void flatten_curve(const PathElement& element, double tolerance, FlattenSink& sink) {
    const bool cubic = element.verb == PathVerb::bezier;
    double second_difference = flatten_length(element.first.x - 2.0 * element.second.x + element.third.x,
                                              element.first.y - 2.0 * element.second.y + element.third.y);
    if (cubic) {
        const double later = flatten_length(element.second.x - 2.0 * element.third.x + element.fourth.x,
                                            element.second.y - 2.0 * element.third.y + element.fourth.y);
        second_difference = std::max(second_difference, later);
    }
    const double degree = cubic ? 3.0 : 2.0;
    const std::size_t chords = bezier_chord_count(degree, second_difference, tolerance,
                                                  GraphicsPath::maximum_flattened_points);
    if (chords == 0U || !sink.room_for(chords)) {
        sink.refuse();
        return;
    }
    const PointF end = cubic ? element.fourth : element.third;
    const double step = 1.0 / static_cast<double>(chords);
    for (std::size_t index = 1U; index < chords; ++index) {
        const double t = static_cast<double>(index) * step;
        const double u = 1.0 - t;
        PointF point{};
        if (cubic) {
            const double a = u * u * u;
            const double b = 3.0 * u * u * t;
            const double c = 3.0 * u * t * t;
            const double d = t * t * t;
            point = PointF{a * element.first.x + b * element.second.x + c * element.third.x + d * element.fourth.x,
                           a * element.first.y + b * element.second.y + c * element.third.y + d * element.fourth.y};
        } else {
            const double a = u * u;
            const double b = 2.0 * u * t;
            const double c = t * t;
            point = PointF{a * element.first.x + b * element.second.x + c * element.third.x,
                           a * element.first.y + b * element.second.y + c * element.third.y};
        }
        sink.add(point);
    }
    sink.add(end);
}

// The points of `sweep_degrees` of the ellipse in `bounds` from
// `start_degrees`, after the start point, which the figure already holds.
// The last point is the arc's end computed directly, not by accumulated
// steps; for a closed ellipse (`closes`) it is left to the close, which
// repeats the start exactly.
void flatten_ellipse_arc(RectF bounds, double start_degrees, double sweep_degrees, bool closes,
                         double tolerance, FlattenSink& sink) {
    const double radius = std::max(std::fabs(bounds.width), std::fabs(bounds.height)) * 0.5;
    const double sweep = sweep_degrees * flatten_degrees_to_radians;
    const std::size_t chords = arc_chord_count(radius, sweep, tolerance, GraphicsPath::maximum_flattened_points);
    if (chords == 0U || !sink.room_for(chords)) {
        sink.refuse();
        return;
    }
    const double step = sweep_degrees / static_cast<double>(chords);
    for (std::size_t index = 1U; index < chords; ++index) {
        const double degrees = start_degrees + static_cast<double>(index) * step;
        sink.add(point_on_ellipse(bounds, degrees));
    }
    if (!closes) sink.add(point_on_ellipse(bounds, start_degrees + sweep_degrees));
}

// One element's part of the flattening (the figure rules of
// GraphicsPath::flatten, which follow the raster's path conversion).
void flatten_element(const PathElement& element, double tolerance, FlattenSink& sink) {
    switch (element.verb) {
    case PathVerb::start_figure:
        sink.end_figure(false);
        return;
    case PathVerb::line:
        if (!sink.has_current() || sink.current() != element.first) sink.begin_figure(element.first);
        if (sink.room_for(1U)) sink.add(element.second);
        return;
    case PathVerb::quadratic:
    case PathVerb::bezier:
        if (!sink.has_current() || sink.current() != element.first) sink.begin_figure(element.first);
        flatten_curve(element, tolerance, sink);
        return;
    case PathVerb::rectangle: {
        sink.begin_figure(PointF{element.rect.left(), element.rect.top()});
        if (!sink.room_for(3U)) return;
        sink.add(PointF{element.rect.right(), element.rect.top()});
        sink.add(PointF{element.rect.right(), element.rect.bottom()});
        sink.add(PointF{element.rect.left(), element.rect.bottom()});
        sink.end_figure(true);
        return;
    }
    case PathVerb::ellipse: {
        // The parameter-angle start of the ellipse at 0 degrees, the right
        // middle point; the chords run clockwise on screen (y down) and the
        // close repeats the start exactly.
        sink.begin_figure(point_on_ellipse(element.rect, 0.0));
        flatten_ellipse_arc(element.rect, 0.0, 360.0, true, tolerance, sink);
        sink.end_figure(true);
        return;
    }
    case PathVerb::arc: {
        const double sweep = std::clamp(element.sweep_angle, -360.0, 360.0);
        const PointF start = point_on_ellipse(element.rect, element.start_angle);
        if (!sink.has_current()) {
            sink.begin_figure(start);
        } else if (sink.current() != start && sink.room_for(1U)) {
            sink.add(start);
        }
        flatten_ellipse_arc(element.rect, element.start_angle, sweep, false, tolerance, sink);
        return;
    }
    case PathVerb::close_figure:
        sink.end_figure(true);
        return;
    }
    // A verb outside the enumeration ends the figure, as the raster refuses
    // the path there.
    sink.end_figure(false);
    return;
}

void require_flattening_tolerance(double tolerance) {
    if (!std::isfinite(tolerance) || !(tolerance > 0.0)) {
        throw std::invalid_argument("path flattening tolerance must be positive and finite");
    }
}

// One pass over `elements` into `sink`, and its counts.
PathFlattening run_flattening(const std::vector<PathElement>& elements, double tolerance, FlattenSink& sink) {
    for (const PathElement& element : elements) {
        if (sink.exceeded()) break;
        flatten_element(element, tolerance, sink);
    }
    sink.end_figure(false);
    PathFlattening result{};
    if (sink.exceeded()) {
        result.status = PathFlatteningStatus::limit_exceeded;
        return result;
    }
    result.point_count = sink.point_count();
    result.figure_count = sink.figure_count();
    return result;
}

} // namespace

PathFlattening GraphicsPath::flattened_size(double tolerance) const {
    require_alive();
    require_flattening_tolerance(tolerance);
    FlattenSink counter(std::span<PointF>{}, std::span<FlattenedFigure>{}, false);
    const PathFlattening counted = run_flattening(elements_, tolerance, counter);
    return counted;
}

PathFlattening GraphicsPath::flatten(double tolerance, std::span<PointF> points,
                                     std::span<FlattenedFigure> figures) const {
    require_alive();
    require_flattening_tolerance(tolerance);
    // Counted first, so storage that is too small is reported before any
    // of it is written.
    FlattenSink counter(std::span<PointF>{}, std::span<FlattenedFigure>{}, false);
    PathFlattening needed = run_flattening(elements_, tolerance, counter);
    if (needed.status != PathFlatteningStatus::complete) return needed;
    if (needed.point_count > points.size() || needed.figure_count > figures.size()) {
        needed.status = PathFlatteningStatus::storage_too_small;
        return needed;
    }
    FlattenSink writer(points, figures, true);
    const PathFlattening written = run_flattening(elements_, tolerance, writer);
    return written;
}


} // namespace gui_drawing
