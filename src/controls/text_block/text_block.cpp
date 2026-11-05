#include "gui_forms/controls/text_block/text_block.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include "line_wrapping.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

[[nodiscard]] bool collapsible_space(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\r' || character == '\f';
}

// Whether any of the text survives white-space collapsing.
[[nodiscard]] bool has_visible_text(std::string_view text) noexcept {
    for (const char character : text) {
        if (!collapsible_space(character)) return true;
    }
    return false;
}

// Whether a line may end after a byte with this opportunity.
[[nodiscard]] bool may_end_line(LineBreakOpportunity opportunity) noexcept {
    return opportunity == LineBreakOpportunity::allowed ||
           opportunity == LineBreakOpportunity::mandatory;
}

// The end of a line's content: trailing spaces hang and are not drawn or
// measured, as CSS's `white-space: normal` treats them.
[[nodiscard]] std::size_t content_end(std::string_view text, std::size_t begin,
                                      std::size_t end) noexcept {
    while (end > begin && text[end - 1U] == ' ') --end;
    return end;
}

std::shared_ptr<const PropertyEnumDescriptor> text_block_alignment_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "text_alignment",
            {{"start", 0}, {"end", 1}, {"left", 2}, {"right", 3}, {"center", 4}},
            false});
    return value;
}

} // namespace

TextBlock::TextBlock(StableId stable_id, std::string text)
    : Control(std::move(stable_id)), text_(std::move(text)) {
    define_bindable_property({
        {"text", BindingValueKind::text, "Appearance",
         "Text shown by the block; white space collapses as in CSS.",
         BindingValue{std::string{}}, invalidation::text_content},
        detail::BindingMemberGetter<TextBlock, std::string>(*this, &TextBlock::text_),
        detail::ConvertedPropertySetter<TextBlock, std::string>(
            *this, &TextBlock::set_text, BindingValueKind::text,
            "TextBlock.Text binding requires text"),
        detail::EventChangeConnector<const std::string&>(text_changed_), {}, {}});

    PropertyDescriptor font;
    font.name = "font";
    font.kind = BindingValueKind::font;
    font.category = "Appearance";
    font.description = "Effective font; reset resumes the theme's field typography.";
    font.invalidation_effects = invalidation::text_content;
    font.bindable = false;
    define_bindable_property({
        std::move(font),
        detail::BindingNoexceptMethodGetter<TextBlock, FontSpec>(*this, &TextBlock::font),
        detail::DirectPropertySetter<TextBlock, FontSpec>(*this, &TextBlock::set_font), {},
        detail::BoundMemberFunction<void (TextBlock::*)()>(*this, &TextBlock::clear_font),
        detail::BoundMemberFunction<bool (TextBlock::*)() const noexcept>(
            *this, &TextBlock::has_font_override),
        detail::BoundMemberFunction<PropertyValueOrigin (TextBlock::*)() const noexcept>(
            *this, &TextBlock::font_property_origin)});

    PropertyDescriptor foreground;
    foreground.name = "fore_color";
    foreground.kind = BindingValueKind::color;
    foreground.category = "Appearance";
    foreground.description = "Effective text color; reset resumes inherited theme color.";
    foreground.invalidation_effects = Dirty::paint | Dirty::semantics;
    foreground.bindable = false;
    define_bindable_property({
        std::move(foreground),
        detail::BindingNoexceptMethodGetter<TextBlock, Color>(*this, &TextBlock::foreground),
        detail::DirectPropertySetter<TextBlock, Color>(*this, &TextBlock::set_foreground), {},
        detail::BoundMemberFunction<void (TextBlock::*)()>(*this, &TextBlock::clear_foreground),
        detail::BoundMemberFunction<bool (TextBlock::*)() const noexcept>(
            *this, &TextBlock::has_foreground_override),
        detail::BoundMemberFunction<PropertyValueOrigin (TextBlock::*)() const noexcept>(
            *this, &TextBlock::foreground_property_origin)});

    PropertyDescriptor alignment;
    alignment.name = "text_align";
    alignment.kind = BindingValueKind::enumeration;
    alignment.category = "Appearance";
    alignment.description =
        "CSS text-align; start and end follow the layout direction.";
    alignment.default_value = BindingValue{PropertyEnumValue{"text_alignment", "start", 0}};
    alignment.invalidation_effects = Dirty::paint | Dirty::semantics;
    alignment.bindable = false;
    alignment.enumeration = text_block_alignment_enum();
    define_bindable_property({
        std::move(alignment),
        detail::BoundMemberFunction<BindingValue (TextBlock::*)() const>(
            *this, &TextBlock::text_align_property_value),
        detail::BoundMemberFunction<void (TextBlock::*)(const BindingValue&)>(
            *this, &TextBlock::set_text_align_property_value),
        {}, {}, {}});

    PropertyDescriptor line_height;
    line_height.name = "line_height";
    line_height.kind = BindingValueKind::number;
    line_height.category = "Layout";
    line_height.description =
        "CSS line-height in logical pixels at text scale 1; zero is normal.";
    line_height.default_value = BindingValue{0.0};
    line_height.invalidation_effects = invalidation::text_content;
    line_height.bindable = false;
    define_bindable_property({
        std::move(line_height),
        detail::BoundMemberFunction<BindingValue (TextBlock::*)() const>(
            *this, &TextBlock::line_height_property_value),
        detail::BoundMemberFunction<void (TextBlock::*)(const BindingValue&)>(
            *this, &TextBlock::set_line_height_property_value),
        {}, {}, {}});
}

PropertyValueOrigin TextBlock::font_property_origin() const noexcept {
    return has_font_override() ? PropertyValueOrigin::local : PropertyValueOrigin::inherited;
}

PropertyValueOrigin TextBlock::foreground_property_origin() const noexcept {
    return has_foreground_override() ? PropertyValueOrigin::local : PropertyValueOrigin::inherited;
}

BindingValue TextBlock::text_align_property_value() const {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = text_block_alignment_enum();
    const std::optional<BindingValue> normalized = convert_property_value(
        BindingValue{static_cast<std::int64_t>(text_align_)}, descriptor);
    if (!normalized) {
        throw std::logic_error("GUI.Forms retained TextAlignment is outside its property schema");
    }
    return *normalized;
}

void TextBlock::set_text_align_property_value(const BindingValue& value) {
    const PropertyEnumValue& enumerated = std::get<PropertyEnumValue>(value);
    set_text_align(static_cast<TextAlignment>(enumerated.value));
}

BindingValue TextBlock::line_height_property_value() const {
    const double value = line_height_ ? *line_height_ : 0.0;
    return BindingValue{value};
}

void TextBlock::set_line_height_property_value(const BindingValue& value) {
    const double requested = std::get<double>(value);
    if (requested == 0.0) {
        clear_line_height();
        return;
    }
    set_line_height(requested);
}

void TextBlock::set_text(std::string text) {
    require_mutable();
    if (text_ == text) return;
    text_ = std::move(text);
    invalidate(invalidation::text_content);
    publish_change(text_changed_, text_);
}

FontSpec TextBlock::font() const noexcept {
    if (font_override_) return *font_override_;
    return effective_theme().structure().typography.field;
}

void TextBlock::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) throw std::invalid_argument("TextBlock font is invalid");
    set_font_from_code(true);
    if (font_override_ && *font_override_ == font) return;
    font_override_ = font;
    invalidate(invalidation::text_content);
}

void TextBlock::clear_font() {
    require_mutable();
    if (!font_override_) return;
    font_override_.reset();
    set_font_from_code(false);
    invalidate(invalidation::text_content);
}

Color TextBlock::foreground() const noexcept {
    if (foreground_override_) return *foreground_override_;
    return effective_theme().resolve(ControlVisualRole::panel, visual_context()).text;
}

void TextBlock::set_foreground(Color color) {
    require_mutable();
    if (foreground_override_ && *foreground_override_ == color) return;
    foreground_override_ = color;
    invalidate(Dirty::paint | Dirty::semantics);
}

void TextBlock::clear_foreground() {
    require_mutable();
    if (!foreground_override_) return;
    foreground_override_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
}

void TextBlock::set_text_align(TextAlignment alignment) {
    require_mutable();
    switch (alignment) {
    case TextAlignment::start:
    case TextAlignment::end:
    case TextAlignment::left:
    case TextAlignment::right:
    case TextAlignment::center:
        // A listed alignment is stored as given, below.
        break;
    default:
        throw std::invalid_argument("TextBlock text alignment is invalid");
    }
    if (text_align_ == alignment) return;
    text_align_ = alignment;
    invalidate(Dirty::paint | Dirty::semantics);
}

void TextBlock::set_line_height(double line_height) {
    require_mutable();
    if (!std::isfinite(line_height) || line_height <= 0.0) {
        throw std::invalid_argument("TextBlock line height must be finite and positive");
    }
    if (line_height_ && *line_height_ == line_height) return;
    line_height_ = line_height;
    invalidate(invalidation::text_content);
}

void TextBlock::clear_line_height() {
    require_mutable();
    if (!line_height_) return;
    line_height_.reset();
    invalidate(invalidation::text_content);
}

void TextBlock::prepare_text(Workspace& workspace) const {
    // White space collapses as CSS's `white-space: normal` collapses it: a
    // run of spaces, tabs and line ends becomes one space, and the text's
    // leading and trailing white space goes.
    workspace.collapsed.clear();
    workspace.collapsed.reserve(text_.size());
    bool pending_space = false;
    for (const char character : text_) {
        if (collapsible_space(character)) {
            pending_space = !workspace.collapsed.empty();
            continue;
        }
        if (pending_space) workspace.collapsed.push_back(' ');
        pending_space = false;
        workspace.collapsed.push_back(character);
    }
    // Prohibited everywhere until the provider answers, so text it rejects
    // (invalid UTF-8) lays out as one unbroken line.
    workspace.opportunities.assign(workspace.collapsed.size(), LineBreakOpportunity::prohibited);
    if (!workspace.collapsed.empty()) {
        static_cast<void>(fill_line_break_opportunities_utf8(
            workspace.collapsed, std::span<LineBreakOpportunity>(workspace.opportunities)));
    }
}

double TextBlock::run_width(std::string_view run, FontSpec font) const {
    if (run.empty()) return 0.0;
    const ResolvedTextLayout layout = resolve_text_layout_utf8(run, font);
    return layout.logical_size.width;
}

void TextBlock::break_lines(double width, FontSpec font, Workspace& workspace) const {
    // Greedy, as CSS fills lines (line_wrapping_detail::wrap_lines_greedy,
    // which DataGridView's wrapped cells share). The collapsed text has no
    // line ends, so no break is mandatory.
    const line_wrapping_detail::ControlRunWidth measure{this, font};
    line_wrapping_detail::wrap_lines_greedy(std::string_view(workspace.collapsed),
                                            std::span<const LineBreakOpportunity>(workspace.opportunities), width,
                                            measure, workspace.lines);
}

double TextBlock::widest_unbreakable_run(const Workspace& workspace, FontSpec font) const {
    const std::string_view text(workspace.collapsed);
    double widest = 0.0;
    std::size_t begin = 0U;
    for (std::size_t position = 1U; position <= text.size(); ++position) {
        const bool last = position == text.size();
        if (!last && !may_end_line(workspace.opportunities[position - 1U])) continue;
        const std::size_t end = content_end(text, begin, position);
        widest = std::max(widest, run_width(text.substr(begin, end - begin), font));
        begin = position;
    }
    return widest;
}

LineBoxMetrics TextBlock::line_metrics(FontSpec font) const {
    // The primary face's metrics do not depend on the text, so a space
    // resolves them; a set line height scales with the text, as the font
    // does.
    const ResolvedTextLayout primary = resolve_text_layout_utf8(" ", font);
    std::optional<double> used_line_height;
    if (line_height_) used_line_height = *line_height_ * effective_text_scale();
    const LineBoxMetrics metrics = line_box_metrics(primary, used_line_height);
    return metrics;
}

Size TextBlock::content_size(double width, Workspace& workspace) const {
    const FontSpec font = text_font((*this).font());
    prepare_text(workspace);
    break_lines(width, font, workspace);
    double widest = 0.0;
    for (const TextLine& line : workspace.lines) widest = std::max(widest, line.width);
    const LineBoxMetrics metrics = line_metrics(font);
    const Size measured{widest, static_cast<double>(workspace.lines.size()) * metrics.height};
    return measured;
}

Size TextBlock::measure(Size available) {
    const Rect requested = requested_bounds();
    const Insets inset = padding.type();
    const double horizontal = inset.left + inset.right;
    const double vertical = inset.top + inset.bottom;
    const double outer_width = requested.width > 0.0 ? requested.width : available.width;
    const Size content = content_size(std::max(0.0, outer_width - horizontal), workspace_);
    const double width = requested.width > 0.0 ? requested.width : content.width + horizontal;
    const double height = requested.height > 0.0 ? requested.height : content.height + vertical;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

Size TextBlock::measure_intrinsic(const LayoutConstraint& constraint) {
    const Insets inset = padding.type();
    const double horizontal = inset.left + inset.right;
    const double vertical = inset.top + inset.bottom;
    const FontSpec font = text_font((*this).font());
    double content_width = 0.0;
    switch (constraint.width.kind) {
    case AvailableExtentKind::min_content:
        prepare_text(workspace_);
        content_width = widest_unbreakable_run(workspace_, font);
        break;
    case AvailableExtentKind::max_content:
    default: // Not an AvailableExtentKind: the text measures unbounded, as max-content.
        content_width = practical_unbounded_extent;
        break;
    case AvailableExtentKind::definite:
    case AvailableExtentKind::at_most:
        content_width = std::max(0.0, constraint.width.extent.value() - horizontal);
        break;
    }
    const Size content = content_size(content_width, workspace_);
    Size measured{content.width + horizontal, content.height + vertical};
    if (constraint.width.kind == AvailableExtentKind::definite) {
        measured.width = constraint.width.extent.value();
    }
    if (constraint.height.kind == AvailableExtentKind::definite) {
        measured.height = constraint.height.extent.value();
    }
    return measured;
}

std::optional<double> TextBlock::first_baseline(double width) {
    // The first line's baseline does not depend on the width; a block with
    // no text has no line box and so no baseline.
    static_cast<void>(width);
    if (!has_visible_text(text_)) return std::nullopt;
    const LineBoxMetrics metrics = line_metrics(text_font((*this).font()));
    const double baseline = padding.type().top + metrics.baseline;
    return baseline;
}

std::optional<double> TextBlock::last_baseline(double width) {
    const Insets inset = padding.type();
    const Size content = content_size(std::max(0.0, width - (inset.left + inset.right)), workspace_);
    if (workspace_.lines.empty()) return std::nullopt;
    const LineBoxMetrics metrics = line_metrics(text_font((*this).font()));
    const double baseline = inset.top + content.height - metrics.height + metrics.baseline;
    return baseline;
}

double TextBlock::line_x(const TextLine& line, double content_width) const noexcept {
    const bool right_to_left = effective_layout_direction() == LayoutDirection::right_to_left;
    const double free = content_width - line.width;
    switch (text_align_) {
    case TextAlignment::start: return right_to_left ? free : 0.0;
    case TextAlignment::end: return right_to_left ? 0.0 : free;
    case TextAlignment::left: return 0.0;
    case TextAlignment::right: return free;
    case TextAlignment::center: return free * 0.5;
    }
    return 0.0;
}

void TextBlock::on_paint(Painter& painter, Rect) {
    const Rect client = client_rectangle();
    const Insets inset = padding.type();
    const double content_width = std::max(0.0, client.width - (inset.left + inset.right));
    static_cast<void>(content_size(content_width, workspace_));
    const FontSpec font = text_font((*this).font());
    const LineBoxMetrics metrics = line_metrics(font);
    // Each line is a label part placed by its baseline, in the theme's text
    // (disabled text while disabled) or the authored foreground (T4-1).
    const std::string_view text(workspace_.collapsed);
    PartPaint part;
    part.family = ControlVisualRole::panel;
    part.role = PartRole::label;
    part.font = font;
    part.authored_foreground = foreground_override_;
    for (std::size_t index = 0U; index < workspace_.lines.size(); ++index) {
        const TextLine& line = workspace_.lines[index];
        const double top = inset.top + static_cast<double>(index) * metrics.height;
        const Point origin{client.x + inset.left + line_x(line, content_width),
                           snap_text_baseline(client.y + top + metrics.baseline)};
        part.text = text.substr(line.begin_byte, line.end_byte - line.begin_byte);
        paint_text_part_at(painter, *this, part, origin, line.width);
    }
}

bool TextBlock::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor TextBlock::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::static_text;
    descriptor.name = accessible_name.type().empty() ? text_ : accessible_name.type();
    descriptor.description = accessible_description.type();
    descriptor.exposed = !descriptor.name.empty();
    return descriptor;
}

namespace {

constexpr std::array<PartField<TextBlock::Parts>, 2> text_block_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &TextBlock::Parts::region},
    {.declaration = {.name = "text", .role = PartRole::label, .composed_from = {}},
     .member = &TextBlock::Parts::text},
}};

constexpr std::array<PartDeclaration, 2> text_block_parts = part_declarations(text_block_part_fields);

constexpr std::array<AnatomyState, 5> text_block_states{
    AnatomyState::normal,
    AnatomyState::enabled,
    AnatomyState::disabled,
    AnatomyState::focused,
    AnatomyState::focus_visible,
};

constexpr ControlAnatomy text_block_anatomy{
    .control_id = "text_block",
    .parts = text_block_parts,
    .states = text_block_states,
    .custom_states = {},
};

// The events TextBlock adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 1> text_block_interactions{{
    {.name = "text_changed", .payload = "std::string", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& TextBlock::anatomy() const noexcept {
    return text_block_anatomy;
}

void TextBlock::publish_part_geometry(std::span<PartGeometry> geometry) const {
    // The text part is the box the lines fill: the content box's width (the
    // lines align within it) and the lines' height. The method is const, so
    // it lays out into storage of its own.
    const Rect client = client_rectangle();
    const Insets inset = padding.type();
    const double content_width = std::max(0.0, client.width - (inset.left + inset.right));
    Workspace workspace{};
    const Size content = content_size(content_width, workspace);
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    if (!workspace.lines.empty()) {
        record.text = PartGeometry::from_bounds(
            Rect{client.x + inset.left, client.y + inset.top, content_width, content.height});
    }
    write_parts(anatomy(), text_block_part_fields, record, geometry);
}

TextBlock::Parts TextBlock::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), text_block_part_fields, published.parts(), record);
    return record;
}

void TextBlock::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), text_block_interactions.begin(),
                        text_block_interactions.end());
}

} // namespace gui_forms
