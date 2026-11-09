#include "../support/drawing_support.hpp"

GraphicsStateToken GraphicsRecorder::save() {
    require_recordable();
    if (saved_.size() >= maximum_state_depth) {
        throw std::length_error("GUI.Drawing state stack limit exceeded");
    }
    const GraphicsStateToken token{next_token_++};
    saved_.push_back({token, state_});
    DrawingCommand command;
    command.kind = CommandKind::save;
    command.state = state_;
    command.token = token;
    append(std::move(command));
    return token;
}

void GraphicsRecorder::restore(GraphicsStateToken token) {
    require_recordable();
    std::vector<SavedState>::reverse_iterator found = saved_.rbegin();
    while (found != saved_.rend() && (*found).token != token) {
        ++found;
    }
    if (token.value == 0 || found == saved_.rend()) {
        throw std::invalid_argument("graphics state token is invalid or already restored");
    }
    state_ = (*found).state;
    saved_.erase(found.base() - 1, saved_.end());
    DrawingCommand command;
    command.kind = CommandKind::restore;
    command.state = state_;
    command.token = token;
    append(std::move(command));
}

void GraphicsRecorder::translate(double x, double y) {
    require_recordable();
    state_.transform = state_.transform.followed_by(Matrix::translation(x, y));
    DrawingCommand command;
    command.kind = CommandKind::translate;
    command.state = state_;
    command.first = {x, y};
    append(std::move(command));
}

void GraphicsRecorder::set_transform(Matrix transform) {
    require_recordable();
    if (!transform.finite()) throw std::invalid_argument("graphics transform must be finite");
    state_.transform = transform;
    DrawingCommand command;
    command.kind = CommandKind::set_transform;
    command.state = state_;
    append(std::move(command));
}

void GraphicsRecorder::set_clip(RectF clip) {
    require_recordable();
    require_finite(clip, "graphics clip");
    state_.clip = state_.transform.transform_bounds(clip);
    DrawingCommand command;
    command.kind = CommandKind::set_clip;
    command.state = state_;
    command.rect = clip;
    append(std::move(command));
}

void GraphicsRecorder::reset_clip() {
    require_recordable();
    state_.clip.reset();
    DrawingCommand command;
    command.kind = CommandKind::reset_clip;
    command.state = state_;
    append(std::move(command));
}

void GraphicsRecorder::set_quality(SmoothingMode smoothing,
                                   InterpolationMode interpolation,
                                   PixelOffsetMode pixel_offset,
                                   CompositingMode compositing,
                                   CompositingQuality compositing_quality) {
    require_recordable();
    require_enum(smoothing, 4U, "smoothing mode");
    require_enum(interpolation, 7U, "interpolation mode");
    require_enum(pixel_offset, 4U, "pixel offset mode");
    require_enum(compositing, 1U, "compositing mode");
    require_enum(compositing_quality, 4U, "compositing quality");
    state_.smoothing = smoothing;
    state_.interpolation = interpolation;
    state_.pixel_offset = pixel_offset;
    state_.compositing = compositing;
    state_.compositing_quality = compositing_quality;
    DrawingCommand command;
    command.kind = CommandKind::set_quality;
    command.state = state_;
    append(std::move(command));
}

GraphicsState GraphicsRecorder::current_state() const {
    require_alive();
    return state_;
}

bool GraphicsRecorder::is_visible(PointF point) const {
    require_alive();
    require_finite(point, "visibility point");
    return !state_.clip || (*state_.clip).contains(state_.transform.transform(point));
}

SizeF GraphicsRecorder::measure_string(std::string_view utf8, const Font& font,
                                       const StringFormat& format,
                                       const TextMetricsProvider& provider) const {
    require_alive();
    if (utf8.size() > maximum_text_bytes || !valid_utf8(utf8)) {
        throw std::invalid_argument("measured text must be bounded valid UTF-8");
    }
    const SizeF result = provider.measure(utf8, font.snapshot(), format.snapshot());
    if (!std::isfinite(result.width) || !std::isfinite(result.height) ||
        result.width < 0.0 || result.height < 0.0) {
        throw std::logic_error("text metrics provider returned invalid dimensions");
    }
    return result;
}

void GraphicsRecorder::clear(Color color) {
    require_recordable();
    DrawingCommand command;
    command.kind = CommandKind::clear;
    command.state = state_;
    command.color = color;
    append(std::move(command));
}

void GraphicsRecorder::fill_rectangle(const Brush& brush, RectF rect) {
    require_recordable();
    require_finite(rect, "fill rectangle");
    DrawingCommand command;
    command.kind = CommandKind::fill_rectangle;
    command.state = state_;
    command.rect = rect;
    command.brush = brush.snapshot();
    command.color = command.brush.primary;
    append(std::move(command));
}

void GraphicsRecorder::draw_rectangle(const Pen& pen, RectF rect) {
    require_recordable();
    require_finite(rect, "stroke rectangle");
    DrawingCommand command;
    command.kind = CommandKind::draw_rectangle;
    command.state = state_;
    command.rect = rect;
    command.pen = pen.snapshot();
    append(std::move(command));
}

void GraphicsRecorder::draw_line(const Pen& pen, PointF from, PointF to) {
    require_recordable();
    require_finite(from, "line start");
    require_finite(to, "line end");
    DrawingCommand command;
    command.kind = CommandKind::draw_line;
    command.state = state_;
    command.first = from;
    command.second = to;
    command.pen = pen.snapshot();
    append(std::move(command));
}

void GraphicsRecorder::draw_string(std::string_view utf8, const Font& font,
                                   const SolidBrush& brush, PointF origin,
                                   const StringFormat& format) {
    require_recordable();
    require_finite(origin, "text origin");
    if (utf8.size() > maximum_text_bytes || !valid_utf8(utf8)) {
        throw std::invalid_argument("drawn text must be bounded valid UTF-8");
    }
    DrawingCommand command;
    command.kind = CommandKind::draw_string;
    command.state = state_;
    command.first = origin;
    command.color = brush.color();
    command.font = font.snapshot();
    command.format = format.snapshot();
    command.text.assign(utf8);
    append(std::move(command));
}

void GraphicsRecorder::draw_ellipse(const Pen& pen, RectF bounds) {
    require_recordable();
    require_finite(bounds, "stroke ellipse");
    DrawingCommand command;
    command.kind = CommandKind::draw_ellipse;
    command.state = state_;
    command.rect = bounds;
    command.pen = pen.snapshot();
    append(std::move(command));
}

void GraphicsRecorder::fill_ellipse(const Brush& brush, RectF bounds) {
    require_recordable();
    require_finite(bounds, "fill ellipse");
    DrawingCommand command;
    command.kind = CommandKind::fill_ellipse;
    command.state = state_;
    command.rect = bounds;
    command.brush = brush.snapshot();
    command.color = command.brush.primary;
    append(std::move(command));
}

void GraphicsRecorder::fill_polygon(const Brush& brush,
                                    std::span<const PointF> points,
                                    FillMode fill_mode) {
    require_recordable();
    require_enum(fill_mode, 1U, "polygon fill mode");
    if (points.size() < 3U || points.size() > GraphicsPath::maximum_elements) {
        throw std::invalid_argument("polygon must contain 3 through 1000000 points");
    }
    for (const PointF point : points) require_finite(point, "polygon point");
    DrawingCommand command;
    command.kind = CommandKind::fill_polygon;
    command.state = state_;
    command.brush = brush.snapshot();
    command.color = command.brush.primary;
    command.path.fill_mode = fill_mode;
    command.points.assign(points.begin(), points.end());
    append(std::move(command));
}

void GraphicsRecorder::draw_path(const Pen& pen, const GraphicsPath& path) {
    require_recordable();
    DrawingCommand command;
    command.kind = CommandKind::draw_path;
    command.state = state_;
    command.pen = pen.snapshot();
    command.path = path.snapshot();
    append(std::move(command));
}

void GraphicsRecorder::fill_path(const Brush& brush,
                                 const GraphicsPath& path) {
    require_recordable();
    DrawingCommand command;
    command.kind = CommandKind::fill_path;
    command.state = state_;
    command.brush = brush.snapshot();
    command.color = command.brush.primary;
    command.path = path.snapshot();
    append(std::move(command));
}

void GraphicsRecorder::draw_image(const ImageReference& image,
                                  RectF destination, RectF source,
                                  const ImageAttributes& attributes) {
    require_recordable();
    require_finite(destination, "image destination");
    require_finite(source, "image source");
    DrawingCommand command;
    command.kind = CommandKind::draw_image;
    command.state = state_;
    command.rect = destination;
    command.first = {source.x, source.y};
    command.second = {source.width, source.height};
    command.image = image.snapshot();
    command.image_attributes = attributes.snapshot();
    append(std::move(command));
}

void GraphicsRecorder::draw_image(const Bitmap& image, RectF destination,
                                  RectF source,
                                  const ImageAttributes& attributes) {
    require_recordable();
    require_finite(destination, "bitmap destination");
    require_finite(source, "bitmap source");
    DrawingCommand command;
    command.kind = CommandKind::draw_image;
    command.state = state_;
    command.rect = destination;
    command.first = {source.x, source.y};
    command.second = {source.width, source.height};
    command.image = image.snapshot();
    command.image_attributes = attributes.snapshot();
    append(std::move(command));
}

namespace {

void require_batch_count(std::size_t count, std::size_t minimum, std::string_view field) {
    if (count < minimum || count > GraphicsRecorder::maximum_batch_elements) {
        throw std::invalid_argument(std::string(field) + " count is outside its bounds");
    }
}

// The largest device extent draw_string_over_fill bands reach without a clip.
constexpr double unclipped_extent = 1073741824.0;

} // namespace

void GraphicsRecorder::draw_lines(const Pen& pen, std::span<const PointF> points) {
    require_recordable();
    require_batch_count(points.size(), 2U, "polyline point");
    for (const PointF point : points) require_finite(point, "polyline point");
    DrawingCommand command;
    command.kind = CommandKind::draw_lines;
    command.state = state_;
    command.pen = pen.snapshot();
    command.points.assign(points.begin(), points.end());
    append(std::move(command));
}

void GraphicsRecorder::fill_rectangles(const Brush& brush, std::span<const RectF> rects) {
    require_recordable();
    require_batch_count(rects.size(), 1U, "fill rectangles");
    for (const RectF rect : rects) require_finite(rect, "fill rectangles entry");
    DrawingCommand command;
    command.kind = CommandKind::fill_rectangles;
    command.state = state_;
    command.brush = brush.snapshot();
    command.color = command.brush.primary;
    command.rects.assign(rects.begin(), rects.end());
    append(std::move(command));
}

void GraphicsRecorder::draw_line_segments(const Pen& pen, std::span<const PointF> points) {
    require_recordable();
    require_batch_count(points.size(), 2U, "line segment point");
    if (points.size() % 2U != 0U) throw std::invalid_argument("line segments take their points in pairs");
    for (const PointF point : points) require_finite(point, "line segment point");
    DrawingCommand command;
    command.kind = CommandKind::draw_line_segments;
    command.state = state_;
    command.pen = pen.snapshot();
    command.points.assign(points.begin(), points.end());
    append(std::move(command));
}

void GraphicsRecorder::draw_markers(std::span<const PointF> centers, const MarkerStyle& style) {
    require_recordable();
    require_batch_count(centers.size(), 1U, "marker center");
    require_enum(style.shape, 9U, "marker shape");
    require_finite(style.size, "marker size");
    require_finite(style.edge_width, "marker edge width");
    if (style.size <= 0.0 || style.edge_width < 0.0) {
        throw std::invalid_argument("marker size must be positive and its edge width non-negative");
    }
    for (const PointF center : centers) require_finite(center, "marker center");
    DrawingCommand command;
    command.kind = CommandKind::draw_markers;
    command.state = state_;
    command.marker = style;
    command.points.assign(centers.begin(), centers.end());
    append(std::move(command));
}

void GraphicsRecorder::fill_grid_pattern(RectF rect, const GridPatternStyle& style) {
    require_recordable();
    require_finite(rect, "grid rectangle");
    require_finite(style.origin, "grid origin");
    require_finite(style.spacing, "grid spacing");
    require_finite(style.line_width, "grid line width");
    if (style.spacing < 2.0 || style.major_every == 0U || style.line_width <= 0.0) {
        throw std::invalid_argument("grid spacing must be at least 2, majors every 1 or more, width positive");
    }
    constexpr double maximum_lines = 65536.0;
    if (rect.width / style.spacing > maximum_lines || rect.height / style.spacing > maximum_lines) {
        throw std::invalid_argument("grid would cross more than 65536 lines");
    }
    DrawingCommand command;
    command.kind = CommandKind::fill_grid_pattern;
    command.state = state_;
    command.rect = rect;
    command.grid = style;
    append(std::move(command));
}

void GraphicsRecorder::fill_proportion(const Brush& brush, RectF track, CornerRadii radii, double proportion,
                                       FillDirection direction) {
    require_recordable();
    require_finite(track, "proportion track");
    require_finite(proportion, "proportion");
    require_enum(direction, 3U, "fill direction");
    const std::array<double, 4> given{radii.top_left, radii.top_right, radii.bottom_right, radii.bottom_left};
    for (const double radius : given) {
        require_finite(radius, "track corner radius");
        if (radius < 0.0) throw std::invalid_argument("track corner radii may not be negative");
    }
    DrawingCommand command;
    command.kind = CommandKind::fill_proportion;
    command.state = state_;
    command.rect = track;
    command.radii = radii;
    command.scalar = std::clamp(proportion, 0.0, 1.0);
    command.direction = direction;
    command.brush = brush.snapshot();
    command.color = command.brush.primary;
    append(std::move(command));
}

void GraphicsRecorder::draw_string_over_fill(std::string_view utf8, const Font& font, const SolidBrush& brush,
                                             const SolidBrush& on_fill_brush, PointF origin,
                                             const StringFormat& format, RectF fill) {
    require_recordable();
    require_finite(origin, "text origin");
    require_finite(fill, "text fill");
    if (utf8.size() > maximum_text_bytes || !valid_utf8(utf8)) {
        throw std::invalid_argument("drawn text must be bounded valid UTF-8");
    }
    if (state_.transform.m12() != 0.0 || state_.transform.m21() != 0.0) {
        throw std::invalid_argument("text over a fill needs a scale and translation transform");
    }
    constexpr std::size_t most_commands = 5U;
    if (commands_.size() > maximum_commands - most_commands) {
        throw std::length_error("GUI.Drawing command limit exceeded");
    }
    // Every part in device space, within the clip in force (or a bound far
    // beyond any surface when there is none).
    const RectF base = state_.clip ? *state_.clip
                                   : RectF{-unclipped_extent, -unclipped_extent, unclipped_extent * 2.0,
                                           unclipped_extent * 2.0};
    const RectF covered = RectF::intersection(base, state_.transform.transform_bounds(fill));
    std::array<RectF, 5> parts{};
    std::array<Color, 5> colours{};
    std::size_t part_count = 0U;
    if (covered.empty()) {
        parts[0] = base;
        colours[0] = brush.color();
        part_count = 1U;
    } else {
        parts = {covered,
                 RectF{base.x, base.y, base.width, covered.y - base.y},
                 RectF{base.x, covered.bottom(), base.width, base.bottom() - covered.bottom()},
                 RectF{base.x, covered.y, covered.x - base.x, covered.height},
                 RectF{covered.right(), covered.y, base.right() - covered.right(), covered.height}};
        colours = {on_fill_brush.color(), brush.color(), brush.color(), brush.color(), brush.color()};
        part_count = parts.size();
    }
    const FontSnapshot font_snapshot = font.snapshot();
    const StringFormatSnapshot format_snapshot = format.snapshot();
    for (std::size_t index = 0U; index < part_count; ++index) {
        if (parts[index].empty()) continue;
        DrawingCommand command;
        command.kind = CommandKind::draw_string;
        command.state = state_;
        command.state.clip = parts[index];
        command.first = origin;
        command.color = colours[index];
        command.font = font_snapshot;
        command.format = format_snapshot;
        command.text.assign(utf8);
        append(std::move(command));
    }
}

void GraphicsRecorder::close() {
    require_alive();
    closed_ = true;
    saved_.clear();
}

bool GraphicsRecorder::closed() const {
    require_alive();
    return closed_;
}

std::span<const DrawingCommand> GraphicsRecorder::commands() const {
    require_alive();
    return commands_;
}

namespace {

std::string rects_text(std::span<const RectF> rects) {
    std::string result;
    for (std::size_t index = 0; index < rects.size(); ++index) {
        if (index != 0U) result += ";";
        result += rect_text(rects[index]);
    }
    return result;
}

std::string marker_text(const MarkerStyle& marker) {
    return std::to_string(static_cast<unsigned>(marker.shape)) + "," + number(marker.size) + "," +
           color_text(marker.fill) + "," + color_text(marker.edge) + "," + number(marker.edge_width);
}

std::string grid_text(const GridPatternStyle& grid) {
    return point_text(grid.origin) + "," + number(grid.spacing) + "," + std::to_string(grid.major_every) + "," +
           color_text(grid.minor) + "," + color_text(grid.major) + "," + number(grid.line_width);
}

std::string radii_text(const CornerRadii& radii) {
    return number(radii.top_left) + "," + number(radii.top_right) + "," + number(radii.bottom_right) + "," +
           number(radii.bottom_left);
}

// The fields of one command's trace entry that its kind carries.
void append_command_fields(const DrawingCommand& command, std::string& trace) {
    switch (command.kind) {
    case CommandKind::save:
    case CommandKind::restore:
        trace += "|token=" + std::to_string(command.token.value);
        return;
    case CommandKind::translate:
        trace += "|offset=" + point_text(command.first);
        return;
    case CommandKind::set_clip:
        trace += "|rect=" + rect_text(command.rect);
        return;
    case CommandKind::clear:
        trace += "|color=" + color_text(command.color);
        return;
    case CommandKind::fill_rectangle:
        trace += "|rect=" + rect_text(command.rect) +
                 "|brush=" + brush_text(command.brush);
        return;
    case CommandKind::draw_rectangle:
        trace += "|rect=" + rect_text(command.rect) +
                 "|pen=" + pen_text(command.pen);
        return;
    case CommandKind::draw_line:
        trace += "|from=" + point_text(command.first) +
                 "|to=" + point_text(command.second) +
                 "|pen=" + pen_text(command.pen);
        return;
    case CommandKind::draw_string:
        trace += "|at=" + point_text(command.first) +
                 "|font=" + escaped(command.font.family) + "," +
                 number(command.font.size) + "," +
                 std::to_string(command.font.style) +
                 "|brush=" + color_text(command.color) +
                 "|format=" + std::to_string(static_cast<unsigned>(command.format.alignment)) +
                 "," + std::to_string(static_cast<unsigned>(command.format.line_alignment)) +
                 "," + std::to_string(static_cast<unsigned>(command.format.trimming)) +
                 "," + std::to_string(command.format.flags) +
                 "|text=" + escaped(command.text);
        return;
    case CommandKind::draw_ellipse:
        trace += "|bounds=" + rect_text(command.rect) +
                 "|pen=" + pen_text(command.pen);
        return;
    case CommandKind::fill_ellipse:
        trace += "|bounds=" + rect_text(command.rect) +
                 "|brush=" + brush_text(command.brush);
        return;
    case CommandKind::fill_polygon:
        trace += "|fill=" +
                 std::to_string(static_cast<unsigned>(command.path.fill_mode)) +
                 "|points=" + points_text(command.points) +
                 "|brush=" + brush_text(command.brush);
        return;
    case CommandKind::draw_path:
        trace += "|path=" + path_text(command.path) +
                 "|pen=" + pen_text(command.pen);
        return;
    case CommandKind::fill_path:
        trace += "|path=" + path_text(command.path) +
                 "|brush=" + brush_text(command.brush);
        return;
    case CommandKind::draw_image:
        trace += "|image=" + std::to_string(command.image.stable_id) + "," +
                 std::to_string(command.image.width) + "," +
                 std::to_string(command.image.height) + "," +
                 std::to_string(static_cast<unsigned>(command.image.pixel_format)) +
                 "," + std::to_string(command.image.generation) +
                 "|destination=" + rect_text(command.rect) +
                 "|source=" + point_text(command.first) + "," +
                 point_text(command.second) +
                 "|attributes=" + image_attributes_text(command.image_attributes);
        return;
    case CommandKind::draw_lines:
    case CommandKind::draw_line_segments:
        trace += "|points=" + points_text(command.points) + "|pen=" + pen_text(command.pen);
        return;
    case CommandKind::fill_rectangles:
        trace += "|rects=" + rects_text(command.rects) + "|brush=" + brush_text(command.brush);
        return;
    case CommandKind::draw_markers:
        trace += "|centers=" + points_text(command.points) + "|marker=" + marker_text(command.marker);
        return;
    case CommandKind::fill_grid_pattern:
        trace += "|rect=" + rect_text(command.rect) + "|grid=" + grid_text(command.grid);
        return;
    case CommandKind::fill_proportion:
        trace += "|track=" + rect_text(command.rect) + "|radii=" + radii_text(command.radii) +
                 "|proportion=" + number(command.scalar) +
                 "|direction=" + std::to_string(static_cast<unsigned>(command.direction)) +
                 "|brush=" + brush_text(command.brush);
        return;
    case CommandKind::set_transform:
    case CommandKind::reset_clip:
    case CommandKind::set_quality:
        // Their effect is the state every entry traces after its fields.
        return;
    }
    // A kind outside the enumeration carries no fields of its own.
    return;
}

} // namespace

std::string GraphicsRecorder::deterministic_trace() const {
    require_alive();
    std::string trace = "gui.drawing.trace/v1\n";
    for (std::size_t index = 0; index < commands_.size(); ++index) {
        const DrawingCommand& command = commands_[index];
        trace += std::to_string(index) + "|" + command_name(command.kind);
        append_command_fields(command, trace);
        trace += "|transform=" + matrix_text(command.state.transform);
        trace += command.state.clip ? "|clip=" + rect_text(*command.state.clip) : "|clip=none";
        trace += "|quality=" +
                 std::to_string(static_cast<unsigned>(command.state.smoothing)) + "," +
                 std::to_string(static_cast<unsigned>(command.state.interpolation)) + "," +
                 std::to_string(static_cast<unsigned>(command.state.pixel_offset)) + "," +
                 std::to_string(static_cast<unsigned>(command.state.compositing)) + "," +
                 std::to_string(static_cast<unsigned>(command.state.compositing_quality));
        trace += "\n";
    }
    trace += "end|commands=" + std::to_string(commands_.size()) +
             "|closed=" + std::string(closed_ ? "1" : "0") + "\n";
    return trace;
}

void GraphicsRecorder::on_dispose() noexcept {
    closed_ = true;
    saved_.clear();
    commands_.clear();
}

void GraphicsRecorder::require_recordable() const {
    require_alive();
    if (closed_) throw std::logic_error("GUI.Drawing recorder is closed");
    if (commands_.size() >= maximum_commands) {
        throw std::length_error("GUI.Drawing command limit exceeded");
    }
}

void GraphicsRecorder::append(DrawingCommand command) {
    if (commands_.size() >= maximum_commands) {
        throw std::length_error("GUI.Drawing command limit exceeded");
    }
    commands_.push_back(std::move(command));
}

} // namespace gui_drawing
