#include "gui_forms/controls/text_block/text_block.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include "line_wrapping.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

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

// TextBlock's fore_color entry, which replaces Control's: the resolved
// colour, neither bindable nor nullable, as TextBlock has described it.
PropertyDescriptor text_block_fore_color_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "fore_color";
    descriptor.kind = BindingValueKind::color;
    descriptor.category = "Appearance";
    descriptor.description = "Effective text color; reset resumes inherited theme color.";
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_text_block_fore_color() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        text_block_fore_color_descriptor(), PropertyOperations{true, true, true, false});
    return descriptor;
}

} // namespace

TextBlock::TextBlock(StableId stable_id, std::string initial_text)
    : Control(std::move(stable_id)) {
    text = std::move(initial_text);
}

FontSpec TextBlock::resolved_font() const noexcept {
    const std::optional<FontSpec>& own = font.read();
    if (own) return *own;
    return effective_theme().structure().typography.field;
}

Color TextBlock::foreground() const noexcept {
    const std::optional<Color>& own = fore_color.read();
    if (own) return *own;
    return effective_theme().resolve(ControlVisualRole::panel, visual_context()).text;
}

BindingValue TextBlock::font_property_value() const {
    const FontSpec resolved = resolved_font();
    return BindingValue{resolved};
}

void TextBlock::reset_font() {
    font = std::nullopt;
}

bool TextBlock::font_serialized() const {
    const bool own = font.read().has_value();
    return own;
}

PropertyValueOrigin TextBlock::font_property_origin() const noexcept {
    return font.read().has_value() ? PropertyValueOrigin::local : PropertyValueOrigin::inherited;
}

BindingValue TextBlock::fore_color_property_value() const {
    const Color resolved = foreground();
    return BindingValue{resolved};
}

void TextBlock::write_fore_color_property(const BindingValue& value) {
    fore_color = std::get<Color>(value);
}

void TextBlock::reset_fore_color_property() {
    fore_color = std::nullopt;
}

bool TextBlock::fore_color_serialized() const {
    const bool own = fore_color.read().has_value();
    return own;
}

PropertyValueOrigin TextBlock::fore_color_property_origin() const noexcept {
    return fore_color.read().has_value() ? PropertyValueOrigin::local : PropertyValueOrigin::inherited;
}

BindingValue TextBlock::line_height_property_value() const {
    const std::optional<double>& own = line_height.read();
    const double value = own ? *own : 0.0;
    return BindingValue{value};
}

void TextBlock::write_line_height_property(const BindingValue& value) {
    const double requested = std::get<double>(value);
    if (requested == 0.0) {
        line_height = std::nullopt;
        return;
    }
    line_height = requested;
}

// A present font must be valid; writing one records that code chose it,
// and clearing it lets the theme's typography return.
void TextBlock::prepare_write(const decltype(font)&, std::optional<FontSpec>& incoming) {
    require_mutable();
    if (incoming && !valid_font_spec(*incoming)) {
        throw std::invalid_argument("TextBlock font is invalid");
    }
    set_font_from_code(incoming.has_value());
}

void TextBlock::written(const decltype(font)&) {
    invalidate(invalidation::text_content);
    on_font_changed();
}

void TextBlock::prepare_text(Workspace& workspace) const {
    // White space collapses as CSS's `white-space: normal` collapses it: a
    // run of spaces, tabs and line ends becomes one space, and the text's
    // leading and trailing white space goes.
    workspace.collapsed.clear();
    workspace.collapsed.reserve(text.read().size());
    bool pending_space = false;
    for (const char character : text.read()) {
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

double TextBlock::run_width(std::string_view run, FontSpec face) const {
    if (run.empty()) return 0.0;
    const ResolvedTextLayout layout = resolve_text_layout_utf8(run, face);
    return layout.logical_size.width;
}

void TextBlock::break_lines(double width, FontSpec face, Workspace& workspace) const {
    // Greedy, as CSS fills lines (line_wrapping_detail::wrap_lines_greedy,
    // which DataGridView's wrapped cells share). The collapsed text has no
    // line ends, so no break is mandatory.
    const line_wrapping_detail::ControlRunWidth measure{this, face};
    line_wrapping_detail::wrap_lines_greedy(std::string_view(workspace.collapsed),
                                            std::span<const LineBreakOpportunity>(workspace.opportunities), width,
                                            measure, workspace.lines);
}

double TextBlock::widest_unbreakable_run(const Workspace& workspace, FontSpec face) const {
    const std::string_view run_text(workspace.collapsed);
    double widest = 0.0;
    std::size_t begin = 0U;
    for (std::size_t position = 1U; position <= run_text.size(); ++position) {
        const bool last = position == run_text.size();
        if (!last && !may_end_line(workspace.opportunities[position - 1U])) continue;
        const std::size_t end = content_end(run_text, begin, position);
        widest = std::max(widest, run_width(run_text.substr(begin, end - begin), face));
        begin = position;
    }
    return widest;
}

LineBoxMetrics TextBlock::line_metrics(FontSpec face) const {
    // The primary face's metrics do not depend on the text, so a space
    // resolves them; a set line height scales with the text, as the font
    // does.
    const ResolvedTextLayout primary = resolve_text_layout_utf8(" ", face);
    const std::optional<double>& own_line_height = line_height.read();
    std::optional<double> used_line_height;
    if (own_line_height) used_line_height = *own_line_height * effective_text_scale();
    const LineBoxMetrics metrics = line_box_metrics(primary, used_line_height);
    return metrics;
}

Size TextBlock::content_size(double width, Workspace& workspace) const {
    const FontSpec face = text_font(resolved_font());
    prepare_text(workspace);
    break_lines(width, face, workspace);
    double widest = 0.0;
    for (const TextLine& line : workspace.lines) widest = std::max(widest, line.width);
    const LineBoxMetrics metrics = line_metrics(face);
    const Size measured{widest, static_cast<double>(workspace.lines.size()) * metrics.height};
    return measured;
}

Size TextBlock::measure(Size available) {
    const Rect requested = requested_bounds();
    const Insets inset = padding.read();
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
    const Insets inset = padding.read();
    const double horizontal = inset.left + inset.right;
    const double vertical = inset.top + inset.bottom;
    const FontSpec face = text_font(resolved_font());
    double content_width = 0.0;
    switch (constraint.width.kind) {
    case AvailableExtentKind::min_content:
        prepare_text(workspace_);
        content_width = widest_unbreakable_run(workspace_, face);
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
    if (!has_visible_text(text.read())) return std::nullopt;
    const LineBoxMetrics metrics = line_metrics(text_font(resolved_font()));
    const double baseline = padding.read().top + metrics.baseline;
    return baseline;
}

std::optional<double> TextBlock::last_baseline(double width) {
    const Insets inset = padding.read();
    const Size content = content_size(std::max(0.0, width - (inset.left + inset.right)), workspace_);
    if (workspace_.lines.empty()) return std::nullopt;
    const LineBoxMetrics metrics = line_metrics(text_font(resolved_font()));
    const double baseline = inset.top + content.height - metrics.height + metrics.baseline;
    return baseline;
}

double TextBlock::line_x(const TextLine& line, double content_width) const noexcept {
    const bool right_to_left = effective_layout_direction() == LayoutDirection::right_to_left;
    const double free = content_width - line.width;
    switch (text_align.read()) {
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
    const Insets inset = padding.read();
    const double content_width = std::max(0.0, client.width - (inset.left + inset.right));
    static_cast<void>(content_size(content_width, workspace_));
    const FontSpec face = text_font(resolved_font());
    const LineBoxMetrics metrics = line_metrics(face);
    // Each line is a label part placed by its baseline, in the theme's text
    // (disabled text while disabled) or the authored foreground (T4-1).
    const std::string_view run_text(workspace_.collapsed);
    PartPaint part;
    part.family = ControlVisualRole::panel;
    part.role = PartRole::label;
    part.font = face;
    part.authored_foreground = fore_color.read();
    for (std::size_t index = 0U; index < workspace_.lines.size(); ++index) {
        const TextLine& line = workspace_.lines[index];
        const double top = inset.top + static_cast<double>(index) * metrics.height;
        const Point origin{client.x + inset.left + line_x(line, content_width),
                           snap_text_baseline(client.y + top + metrics.baseline)};
        part.text = run_text.substr(line.begin_byte, line.end_byte - line.begin_byte);
        paint_text_part_at(painter, *this, part, origin, line.width);
    }
}

bool TextBlock::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor TextBlock::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::static_text;
    descriptor.name = accessible_name.read().empty() ? text.read() : accessible_name.read();
    descriptor.description = accessible_description.read();
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
    const Insets inset = padding.read();
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

// property_codemod begin TextBlock (tools/property_families/text_block.json; generated, do not edit)

namespace {

PropertyDescriptor text_block_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "Text shown by the block; white space collapses as in CSS.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = invalidation::text_content;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_text_block_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        text_block_text_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor text_block_font_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "font";
    descriptor.kind = BindingValueKind::font;
    descriptor.category = "Appearance";
    descriptor.description = "Effective font; reset resumes the theme's field typography.";
    descriptor.invalidation_effects = invalidation::text_content;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_text_block_font() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        text_block_font_descriptor(), PropertyOperations{true, true, true, false});
    return descriptor;
}

PropertyDescriptor text_block_text_align_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text_align";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "CSS text-align; start and end follow the layout direction.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"text_alignment", "start", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = text_block_alignment_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_text_block_text_align() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        text_block_text_align_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor text_block_line_height_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "line_height";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Layout";
    descriptor.description = "CSS line-height in logical pixels at text scale 1; zero is normal.";
    descriptor.default_value = BindingValue{0.0};
    descriptor.invalidation_effects = invalidation::text_content;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_text_block_line_height() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        text_block_line_height_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<TextAlignment>::type() {
    return PropertyType{BindingValueKind::enumeration, false, text_block_alignment_enum()};
}

BindingValue PropertyValueTraits<TextAlignment>::to_binding(TextAlignment value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = text_block_alignment_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

TextAlignment PropertyValueTraits<TextAlignment>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<TextAlignment>(choice.value);
}

const std::array<PropertyEntry, TextBlock::text_block_property_count>& TextBlock::text_block_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> overrides{{
        {"fore_color", &describe_text_block_fore_color,
         &read_member_entry<TextBlock, decltype(&TextBlock::fore_color_property_value), &TextBlock::fore_color_property_value>,
         &write_member_entry<TextBlock, decltype(&TextBlock::write_fore_color_property), &TextBlock::write_fore_color_property>,
         &reset_member_entry<TextBlock, decltype(&TextBlock::reset_fore_color_property), &TextBlock::reset_fore_color_property>,
         &should_serialize_member_entry<TextBlock, decltype(&TextBlock::fore_color_serialized), &TextBlock::fore_color_serialized>,
         &origin_member_entry<TextBlock, decltype(&TextBlock::fore_color_property_origin), &TextBlock::fore_color_property_origin>,
         nullptr},
    }};
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"text", &describe_text_block_text,
         &read_cell_entry<TextBlock, decltype(&TextBlock::text), &TextBlock::text>,
         &write_cell_entry<TextBlock, decltype(&TextBlock::text), &TextBlock::text>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<TextBlock, decltype(&TextBlock::text_changed_), &TextBlock::text_changed_>},
        {"font", &describe_text_block_font,
         &read_member_entry<TextBlock, decltype(&TextBlock::font_property_value), &TextBlock::font_property_value>,
         &write_cell_entry<TextBlock, decltype(&TextBlock::font), &TextBlock::font>,
         &reset_member_entry<TextBlock, decltype(&TextBlock::reset_font), &TextBlock::reset_font>, &should_serialize_member_entry<TextBlock, decltype(&TextBlock::font_serialized), &TextBlock::font_serialized>, &origin_member_entry<TextBlock, decltype(&TextBlock::font_property_origin), &TextBlock::font_property_origin>,
         nullptr},
        {"text_align", &describe_text_block_text_align,
         &read_cell_entry<TextBlock, decltype(&TextBlock::text_align), &TextBlock::text_align>,
         &write_cell_entry<TextBlock, decltype(&TextBlock::text_align), &TextBlock::text_align>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"line_height", &describe_text_block_line_height,
         &read_member_entry<TextBlock, decltype(&TextBlock::line_height_property_value), &TextBlock::line_height_property_value>,
         &write_member_entry<TextBlock, decltype(&TextBlock::write_line_height_property), &TextBlock::write_line_height_property>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "TextBlock's property table names a property twice");
    static const std::array<PropertyEntry, text_block_property_count> entries =
        join_property_tables(override_property_entries(Control::control_property_entries(), overrides), own);
    return entries;
}

std::span<const PropertyEntry> TextBlock::property_table() const noexcept {
    const std::array<PropertyEntry, text_block_property_count>& entries = text_block_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void TextBlock::prepare_write(const decltype(text)&, std::string&) {
    require_mutable();
}

void TextBlock::written(const decltype(text)&) {
    invalidate(invalidation::text_content);
    on_text_changed();
}

void TextBlock::prepare_write(const decltype(text_align)&, TextAlignment& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(TextAlignment::center)) throw std::invalid_argument("TextBlock text alignment is invalid");
}

void TextBlock::written(const decltype(text_align)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void TextBlock::prepare_write(const decltype(line_height)&, std::optional<double>& incoming) {
    require_mutable();
    if (incoming && (!std::isfinite(*incoming) || *incoming <= 0.0)) throw std::invalid_argument("TextBlock line height must be finite and positive");
}

void TextBlock::written(const decltype(line_height)&) {
    invalidate(invalidation::text_content);
}

void TextBlock::on_text_changed() {
    publish_change(text_changed_, text.read());
}

void TextBlock::on_font_changed() {}

// property_codemod end TextBlock

} // namespace gui_forms
