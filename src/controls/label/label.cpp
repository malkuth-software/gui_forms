#include "gui_forms/controls/label.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "../basic/basic_control_rendering.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {
// The label's text insets (audit F6), named once for measure, layout and
// paint. Logical units.
// The text sits this far inside the left and right edges.
constexpr double horizontal_inset = 2.0;
// Both horizontal insets: the width measure adds and wrapping subtracts.
constexpr double horizontal_insets = 2.0 * horizontal_inset;
// The text block sits at least this far inside the top and bottom edges.
constexpr double vertical_inset = 1.0;
// The height the measure adds around the text block.
constexpr double vertical_allowance = 4.0;

// These resolvers borrow their host only for the synchronous label_lines call.
struct LabelWidth final {
    const Label& label;
    FontSpec font{};
    double operator()(const std::string_view value) const {
        const TextExtent extent = label.measure_text_extent_utf8(value, font);
        return extent.logical_size.width;
    }
};

struct PainterLabelWidth final {
    Painter& painter;
    FontSpec font{};
    double operator()(const std::string_view value) const {
        const TextExtent extent = painter.measure_text_extent_utf8(value, font);
        return extent.logical_size.width;
    }
};
std::shared_ptr<const PropertyEnumDescriptor> horizontal_alignment_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "horizontal_alignment", {{"near", 0}, {"center", 1}, {"far", 2}}, false});
    return value;
}

std::shared_ptr<const PropertyEnumDescriptor> vertical_alignment_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "vertical_alignment", {{"near", 0}, {"center", 1}, {"far", 2}}, false});
    return value;
}

std::shared_ptr<const PropertyEnumDescriptor> text_wrapping_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "text_wrapping", {{"no_wrap", 0}, {"word", 1}}, false});
    return value;
}

// Label's fore_color entry (it replaces Control's in Label's table): the
// resolved text colour, as Label's registration described it before M-S.
PropertyDescriptor label_fore_color_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "fore_color";
    descriptor.kind = BindingValueKind::color;
    descriptor.category = "Appearance";
    descriptor.description = "Effective text color; reset resumes inherited theme color.";
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    return descriptor;
}

const PropertyDescriptor& describe_label_fore_color() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        label_fore_color_descriptor(), PropertyOperations{true, true, true, false});
    return descriptor;
}

} // namespace

Label::Label(StableId stable_id, std::string initial_text) : Control(std::move(stable_id)) {
    text = std::move(initial_text);
}

FontSpec Label::resolved_font() const noexcept {
    const std::optional<FontSpec>& own = font.read();
    if (own) return *own;
    const ThemeTypographyTokens& typography = effective_theme().structure().typography;
    switch (text_style_role.read()) {
    case TextStyleRole::body: return typography.field;
    case TextStyleRole::control: return typography.control;
    case TextStyleRole::caption: return typography.caption;
    case TextStyleRole::heading: return typography.heading;
    case TextStyleRole::title: return typography.title;
    case TextStyleRole::monospace: return typography.monospace;
    }
    return typography.field;
}

// The text colour paint_label_text draws: the authored fore_color, or the
// theme's text with the fields of the rules that select this label (its
// type, stable ID, classes and states) in place (G3).
Color Label::foreground() const {
    const std::optional<Color>& own = fore_color.read();
    if (own) return *own;
    const ResolvedControlRecipe resolved =
        effective_theme().resolve(*this, ControlVisualRole::panel, visual_context());
    const Color theme_text = resolved.recipe().text;
    return theme_text;
}

BindingValue Label::font_property_value() const {
    const FontSpec resolved = resolved_font();
    return BindingValue{resolved};
}

void Label::reset_font() {
    font = std::nullopt;
}

bool Label::font_serialized() const {
    const bool own = font.read().has_value();
    return own;
}

PropertyValueOrigin Label::font_property_origin() const noexcept {
    return font.read().has_value() ? PropertyValueOrigin::local : PropertyValueOrigin::inherited;
}

BindingValue Label::fore_color_property_value() const {
    const Color resolved = foreground();
    return BindingValue{resolved};
}

void Label::write_fore_color_property(const BindingValue& value) {
    fore_color = std::get<Color>(value);
}

void Label::reset_fore_color_property() {
    fore_color = std::nullopt;
}

bool Label::fore_color_serialized() const {
    const bool own = fore_color.read().has_value();
    return own;
}

PropertyValueOrigin Label::fore_color_property_origin() const noexcept {
    return fore_color.read().has_value() ? PropertyValueOrigin::local : PropertyValueOrigin::inherited;
}

// Text only changes retained geometry when this label is content-sized.
// Fixed labels are a common high-rate telemetry surface; making every text
// update a measure request needlessly invalidates every layout ancestor.
void Label::prepare_write(const decltype(text)&, std::string&) {
    require_mutable();
}

void Label::written(const decltype(text)&) {
    const Rect requested = requested_bounds();
    Dirty effects = Dirty::paint | Dirty::semantics | Dirty::accessibility;
    if (auto_size.read() || requested.width <= 0.0 || requested.height <= 0.0) {
        effects |= Dirty::measure | Dirty::arrange;
    }
    invalidate(effects);
    on_text_changed();
}

// A present font must be valid; writing one records that code chose it,
// and clearing it lets the theme's typography return.
void Label::prepare_write(const decltype(font)&, std::optional<FontSpec>& incoming) {
    require_mutable();
    if (incoming && !valid_font_spec(*incoming)) {
        throw std::invalid_argument("Label font specification is invalid");
    }
    set_font_from_code(incoming.has_value());
}

void Label::written(const decltype(font)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    on_font_changed();
}

void Label::prepare_write(const decltype(text_style_role)&, TextStyleRole& incoming) {
    require_mutable();
    switch (incoming) {
    case TextStyleRole::body:
    case TextStyleRole::control:
    case TextStyleRole::caption:
    case TextStyleRole::heading:
    case TextStyleRole::title:
    case TextStyleRole::monospace:
        return;
    default:
        throw std::invalid_argument("Label text style role is invalid");
    }
}

// The role chooses the font only while the label has none of its own.
void Label::written(const decltype(text_style_role)&) {
    if (!font.read().has_value()) {
        invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    } else {
        invalidate(Dirty::semantics);
    }
}

Size Label::measure(const Size available) {
    const Rect requested = requested_bounds();
    if (requested.width > 0.0 && requested.height > 0.0) {
        // Neither dimension depends on text. Painting still resolves wrapping
        // against the arranged width, with the current font and complete text.
        const Size measured{std::min(available.width, requested.width),
                            std::min(available.height, requested.height)};
        return measured;
    }
    const double wrap_width = requested.width > 0.0
        ? requested.width : available.width;
    const Size measured = measure_wrapped_text(std::max(0.0, wrap_width - horizontal_insets), available);
    return measured;
}

Size Label::measure_intrinsic(const LayoutConstraint& constraint) {
    const AvailableExtentKind kind = constraint.width.kind;
    const bool content_query = kind == AvailableExtentKind::min_content ||
                               kind == AvailableExtentKind::max_content;
    if (!content_query || requested_bounds().width > 0.0 ||
        text_wrapping.read() == TextWrapping::no_wrap) {
        const Size measured = Control::measure_intrinsic(constraint);
        return measured;
    }
    const Size available = measure_available_size(constraint);
    double content_width_limit = practical_unbounded_extent;
    if (kind == AvailableExtentKind::min_content) {
        const std::string shown = display_text();
        content_width_limit = widest_unbreakable_segment(shown, text_font(resolved_font()));
    }
    Size measured = measure_wrapped_text(content_width_limit, available);
    // A container that lays the label out at this width hands it back as a
    // definite width, and measure and paint wrap at that width less the
    // inset. Adding and then subtracting the inset can round below the
    // widest word, which would then break inside it; step up by the unit in
    // the last place until it cannot. One step corrects the rounding of one
    // addition; four bound the loop.
    if (kind == AvailableExtentKind::min_content) {
        for (int step = 0; step < 4 && measured.width - horizontal_insets < content_width_limit; ++step) {
            measured.width = std::nextafter(measured.width, practical_unbounded_extent);
        }
    }
    if (constraint.height.kind == AvailableExtentKind::definite) {
        measured.height = constraint.height.extent.value();
    }
    return measured;
}

double Label::widest_unbreakable_segment(const std::string_view shown,
                                         const FontSpec face) const {
    // The same segments label_lines wraps between: runs of bytes that are
    // not ASCII whitespace. A newline is whitespace, so paragraphs need no
    // separate pass.
    double widest = 0.0;
    std::size_t cursor = 0U;
    while (cursor < shown.size()) {
        while (cursor < shown.size() &&
               std::isspace(static_cast<unsigned char>(shown[cursor])) != 0) {
            ++cursor;
        }
        std::size_t word_end = cursor;
        while (word_end < shown.size() &&
               std::isspace(static_cast<unsigned char>(shown[word_end])) == 0) {
            ++word_end;
        }
        if (word_end > cursor) {
            const TextExtent word =
                measure_text_extent_utf8(shown.substr(cursor, word_end - cursor), face);
            widest = std::max(widest, word.logical_size.width);
        }
        cursor = word_end;
    }
    return widest;
}

Size Label::measure_wrapped_text(const double content_width_limit,
                                 const Size available) const {
    const Rect requested = requested_bounds();
    const std::string shown = display_text();
    const FontSpec face = text_font(resolved_font());
    const TextWidthResolver resolve{LabelWidth{*this, face}};
    std::vector<std::string> lines = label_lines(
        shown, face, content_width_limit, text_wrapping.read(), resolve, maximum_lines.read());
    double content_width{};
    double line_height{};
    for (const std::string& line : lines) {
        const TextExtent metrics = measure_text_extent_utf8(line, face);
        content_width = std::max(content_width, metrics.logical_size.width);
        line_height = std::max(
            line_height, std::max(face.size, metrics.logical_size.height) *
                             line_spacing.read());
    }
    if (line_height <= 0.0) line_height = face.size * line_spacing.read();
    const double preferred_width = requested.width > 0.0
        ? requested.width : content_width + horizontal_insets;
    const double preferred_height = requested.height > 0.0
        ? requested.height
        : static_cast<double>(lines.size()) * line_height + vertical_allowance;
    const Size measured{std::min(available.width, preferred_width),
                        std::min(available.height, preferred_height)};
    return measured;
}

std::string Label::display_text() const {
    std::string display = use_mnemonic.read()
        ? parse_mnemonic_text(text.read()).display_text : text.read();
    for (char& character : display) {
        if (text_case_transform.read() == TextCaseTransform::uppercase_ascii &&
            character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        } else if (text_case_transform.read() == TextCaseTransform::lowercase_ascii &&
                   character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return display;
}

Label::TextLayout Label::layout_label_text(const std::string_view shown,
                                           Painter* painter) const {
    const Rect arranged = committed_arranged_bounds();
    const FontSpec face = text_font(resolved_font());
    const TextWidthResolver resolve = painter != nullptr
        ? TextWidthResolver{PainterLabelWidth{*painter, face}}
        : TextWidthResolver{LabelWidth{*this, face}};
    TextLayout layout;
    layout.lines = label_lines(
        shown, face, std::max(0.0, arranged.width - horizontal_insets), text_wrapping.read(), resolve, maximum_lines.read());
    std::vector<TextExtent> metrics{};
    metrics.reserve(layout.lines.size());
    double line_height{};
    for (const std::string& line : layout.lines) {
        if (painter != nullptr) {
            metrics.push_back((*painter).measure_text_extent_utf8(line, face));
        } else {
            metrics.push_back(measure_text_extent_utf8(line, face));
        }
        line_height = std::max(
            line_height,
            std::max(face.size, metrics.back().logical_size.height) *
                line_spacing.read());
    }
    if (line_height <= 0.0) line_height = face.size * line_spacing.read();
    const double block_height = static_cast<double>(layout.lines.size()) * line_height;
    double top = vertical_inset;
    if (vertical_alignment.read() == VerticalAlignment::center) {
        top = std::max(vertical_inset, (arranged.height - block_height) * 0.5);
    } else if (vertical_alignment.read() == VerticalAlignment::far) {
        top = std::max(vertical_inset, arranged.height - block_height - vertical_inset);
    }
    layout.origins.reserve(layout.lines.size());
    layout.widths.reserve(layout.lines.size());
    double block_left = arranged.width;
    double block_right = 0.0;
    for (std::size_t index = 0; index < layout.lines.size(); ++index) {
        const double text_width = metrics[index].logical_size.width;
        double x = horizontal_inset;
        if (alignment.read() == HorizontalAlignment::center) {
            x = std::max(horizontal_inset, (arranged.width - text_width) * 0.5);
        } else if (alignment.read() == HorizontalAlignment::far) {
            x = std::max(horizontal_inset, arranged.width - text_width - horizontal_inset);
        }
        const double leading = std::max(
            0.0, line_height - metrics[index].logical_size.height);
        const double baseline = snap_text_baseline(
            top + leading * 0.5 + metrics[index].ascent +
                static_cast<double>(index) * line_height);
        layout.origins.push_back({x, baseline});
        layout.widths.push_back(text_width);
        block_left = std::min(block_left, x);
        block_right = std::max(block_right, x + text_width);
    }
    if (!layout.lines.empty() && block_right > block_left) {
        layout.block = {block_left, top, block_right - block_left, block_height};
    }
    return layout;
}

// Each line is a label part placed by its baseline, in the theme's text
// (disabled text while disabled) or the authored foreground (T4-1).
void Label::paint_label_text(Painter& painter, const std::string_view shown) const {
    const FontSpec face = text_font(resolved_font());
    const TextLayout layout = layout_label_text(shown, &painter);
    PartPaint line;
    line.family = ControlVisualRole::panel;
    line.role = PartRole::label;
    line.font = face;
    line.authored_foreground = fore_color.read();
    for (std::size_t index = 0; index < layout.lines.size(); ++index) {
        line.text = layout.lines[index];
        paint_text_part_at(painter, *this, line, layout.origins[index], layout.widths[index]);
    }
}

namespace {

constexpr std::array<PartField<Label::Parts>, 2> label_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Label::Parts::region},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &Label::Parts::label},
}};

constexpr std::array<PartDeclaration, 2> label_parts = part_declarations(label_part_fields);

constexpr std::array<AnatomyState, 5> label_states{
    AnatomyState::normal,
    AnatomyState::enabled,
    AnatomyState::disabled,
    AnatomyState::focused,
    AnatomyState::focus_visible,
};

constexpr ControlAnatomy label_anatomy{
    .control_id = "label",
    .parts = label_parts,
    .states = label_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& Label::anatomy() const noexcept {
    return label_anatomy;
}

void Label::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const TextLayout layout = layout_label_text(display_text(), nullptr);
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.label = PartGeometry::from_bounds(layout.block);
    write_parts(anatomy(), label_part_fields, record, geometry);
}

Label::Parts Label::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), label_part_fields, published.parts(), record);
    return record;
}

void Label::on_paint(Painter& painter, Rect) {
    paint_label_text(painter, display_text());
}

bool Label::mnemonic_matches(char32_t character) const noexcept {
    return use_mnemonic.read() && is_mnemonic(character, text.read());
}

bool Label::process_mnemonic_self(char32_t character) {
    if (!mnemonic_matches(character)) return false;
    static_cast<void>(focus_next_after_self());
    return true;
}

bool Label::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor Label::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::static_text;
    descriptor.name = accessible_name.read().empty()
        ? (use_mnemonic.read() ? parse_mnemonic_text(text.read()).display_text : text.read())
        : accessible_name.read();
    descriptor.description = accessible_description.read();
    // The mnemonic moves the focus to the next control; the label publishes
    // it, as a Win32 static control does.
    if (use_mnemonic.read()) descriptor.access_key = mnemonic_access_key(text.read());
    descriptor.exposed = !descriptor.name.empty();
    return descriptor;
}

namespace {

// The events Label adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 1> label_interactions{{
    {.name = "text_changed", .payload = "std::string", .mutable_payload = false},
}};

} // namespace

void Label::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), label_interactions.begin(), label_interactions.end());
}

// property_codemod begin Label (tools/property_families/label.json; generated, do not edit)

namespace {

PropertyDescriptor label_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "Text displayed by the label.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = invalidation::text_content;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_label_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        label_text_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor label_font_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "font";
    descriptor.kind = BindingValueKind::font;
    descriptor.category = "Appearance";
    descriptor.description = "Effective font; reset resumes inherited theme typography.";
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_label_font() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        label_font_descriptor(), PropertyOperations{true, true, true, false});
    return descriptor;
}

PropertyDescriptor label_alignment_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "alignment";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "Horizontal placement of the text: near, center or far.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"horizontal_alignment", "near", 0}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = horizontal_alignment_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_label_alignment() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        label_alignment_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor label_vertical_alignment_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "vertical_alignment";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "Vertical placement of the text: near, center or far.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"vertical_alignment", "center", 1}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = vertical_alignment_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_label_vertical_alignment() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        label_vertical_alignment_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor label_text_wrapping_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text_wrapping";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Layout";
    descriptor.description = "Whether the text wraps at word boundaries: no_wrap or word.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"text_wrapping", "no_wrap", 0}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = text_wrapping_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_label_text_wrapping() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        label_text_wrapping_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor label_maximum_lines_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "maximum_lines";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Layout";
    descriptor.description = "Maximum painted and measured line count; zero keeps every line.";
    descriptor.default_value = BindingValue{std::uint64_t{0}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_label_maximum_lines() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        label_maximum_lines_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<HorizontalAlignment>::type() {
    return PropertyType{BindingValueKind::enumeration, false, horizontal_alignment_enum()};
}

BindingValue PropertyValueTraits<HorizontalAlignment>::to_binding(HorizontalAlignment value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = horizontal_alignment_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

HorizontalAlignment PropertyValueTraits<HorizontalAlignment>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<HorizontalAlignment>(choice.value);
}

PropertyType PropertyValueTraits<VerticalAlignment>::type() {
    return PropertyType{BindingValueKind::enumeration, false, vertical_alignment_enum()};
}

BindingValue PropertyValueTraits<VerticalAlignment>::to_binding(VerticalAlignment value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = vertical_alignment_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

VerticalAlignment PropertyValueTraits<VerticalAlignment>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<VerticalAlignment>(choice.value);
}

PropertyType PropertyValueTraits<TextWrapping>::type() {
    return PropertyType{BindingValueKind::enumeration, false, text_wrapping_enum()};
}

BindingValue PropertyValueTraits<TextWrapping>::to_binding(TextWrapping value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = text_wrapping_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

TextWrapping PropertyValueTraits<TextWrapping>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<TextWrapping>(choice.value);
}

const std::array<PropertyEntry, Label::label_property_count>& Label::label_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> overrides{{
        {"fore_color", &describe_label_fore_color,
         &read_member_entry<Label, decltype(&Label::fore_color_property_value), &Label::fore_color_property_value>,
         &write_member_entry<Label, decltype(&Label::write_fore_color_property), &Label::write_fore_color_property>,
         &reset_member_entry<Label, decltype(&Label::reset_fore_color_property), &Label::reset_fore_color_property>,
         &should_serialize_member_entry<Label, decltype(&Label::fore_color_serialized), &Label::fore_color_serialized>,
         &origin_member_entry<Label, decltype(&Label::fore_color_property_origin), &Label::fore_color_property_origin>,
         nullptr},
    }};
    static constexpr std::array<PropertyEntry, 6U> own{{
        {"text", &describe_label_text,
         &read_cell_entry<Label, decltype(&Label::text), &Label::text>,
         &write_cell_entry<Label, decltype(&Label::text), &Label::text>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<Label, decltype(&Label::text_changed_), &Label::text_changed_>},
        {"font", &describe_label_font,
         &read_member_entry<Label, decltype(&Label::font_property_value), &Label::font_property_value>,
         &write_cell_entry<Label, decltype(&Label::font), &Label::font>,
         &reset_member_entry<Label, decltype(&Label::reset_font), &Label::reset_font>, &should_serialize_member_entry<Label, decltype(&Label::font_serialized), &Label::font_serialized>, &origin_member_entry<Label, decltype(&Label::font_property_origin), &Label::font_property_origin>,
         nullptr},
        {"alignment", &describe_label_alignment,
         &read_cell_entry<Label, decltype(&Label::alignment), &Label::alignment>,
         &write_cell_entry<Label, decltype(&Label::alignment), &Label::alignment>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"vertical_alignment", &describe_label_vertical_alignment,
         &read_cell_entry<Label, decltype(&Label::vertical_alignment), &Label::vertical_alignment>,
         &write_cell_entry<Label, decltype(&Label::vertical_alignment), &Label::vertical_alignment>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"text_wrapping", &describe_label_text_wrapping,
         &read_cell_entry<Label, decltype(&Label::text_wrapping), &Label::text_wrapping>,
         &write_cell_entry<Label, decltype(&Label::text_wrapping), &Label::text_wrapping>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"maximum_lines", &describe_label_maximum_lines,
         &read_cell_entry<Label, decltype(&Label::maximum_lines), &Label::maximum_lines>,
         &write_cell_entry<Label, decltype(&Label::maximum_lines), &Label::maximum_lines>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "Label's property table names a property twice");
    static const std::array<PropertyEntry, label_property_count> entries =
        join_property_tables(override_property_entries(Control::control_property_entries(), overrides), own);
    return entries;
}

std::span<const PropertyEntry> Label::property_table() const noexcept {
    const std::array<PropertyEntry, label_property_count>& entries = label_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void Label::on_text_changed() {
    publish_change(text_changed_, text.read());
}

void Label::on_font_changed() {}

void Label::prepare_write(const decltype(alignment)&, HorizontalAlignment& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(HorizontalAlignment::far)) throw std::invalid_argument("Label horizontal alignment is invalid");
}

void Label::written(const decltype(alignment)&) {
    invalidate(Dirty::paint | Dirty::semantics);
    on_alignment_changed();
}

void Label::on_alignment_changed() {}

void Label::prepare_write(const decltype(vertical_alignment)&, VerticalAlignment& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(VerticalAlignment::far)) throw std::invalid_argument("Label vertical alignment is invalid");
}

void Label::written(const decltype(vertical_alignment)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void Label::prepare_write(const decltype(text_wrapping)&, TextWrapping& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(TextWrapping::word)) throw std::invalid_argument("Label text wrapping mode is invalid");
}

void Label::written(const decltype(text_wrapping)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void Label::prepare_write(const decltype(maximum_lines)&, std::uint32_t& incoming) {
    require_mutable();
    if (incoming > 4096U) throw std::out_of_range("Label maximum lines must be zero or at most 4096");
}

void Label::written(const decltype(maximum_lines)&) {
    invalidate(Dirty::measure | Dirty::paint);
}

void Label::prepare_write(const decltype(line_spacing)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.75 || incoming > 3.0) throw std::invalid_argument("Label line spacing must be finite and between 0.75 and 3.0");
}

void Label::written(const decltype(line_spacing)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void Label::prepare_write(const decltype(text_case_transform)&, TextCaseTransform& incoming) {
    require_mutable();
    if (static_cast<std::uint8_t>(incoming) > static_cast<std::uint8_t>(TextCaseTransform::lowercase_ascii)) throw std::invalid_argument("Label text case transform is invalid");
}

void Label::written(const decltype(text_case_transform)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void Label::prepare_write(const decltype(use_mnemonic)&, bool&) {
    require_mutable();
}

void Label::written(const decltype(use_mnemonic)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

// property_codemod end Label

} // namespace gui_forms
