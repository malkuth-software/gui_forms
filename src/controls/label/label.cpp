#include "gui_forms/controls/label/label.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "../basic/basic_control_rendering.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"

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
// These resolvers borrow their host only for the synchronous label_lines call.
struct LabelWidth final {
    const Label& label;
    FontSpec font{};
    double operator()(const std::string_view value) const {
        const ResolvedTextLayout layout = label.resolve_text_layout_utf8(value, font);
        return layout.logical_size.width;
    }
};

struct PainterLabelWidth final {
    Painter& painter;
    FontSpec font{};
    double operator()(const std::string_view value) const {
        const ResolvedTextLayout layout = painter.resolve_text_layout_utf8(value, font);
        return layout.logical_size.width;
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

} // namespace

Label::Label(StableId stable_id, std::string text)
    : Control(std::move(stable_id)), text_(std::move(text)) {
    define_bindable_property({
        {"text", BindingValueKind::text, "Appearance",
         "Text displayed by the label.", BindingValue{std::string{}},
         invalidation::text_content},
        detail::BindingMemberGetter<Label, std::string>(
            *this, &Label::text_),
        detail::ConvertedPropertySetter<Label, std::string>(
            *this, &Label::set_text, BindingValueKind::text,
            "Label.Text binding requires text"),
        detail::EventChangeConnector<const std::string&>(text_changed_), {}, {}});

    PropertyDescriptor font;
    font.name = "font";
    font.kind = BindingValueKind::font;
    font.category = "Appearance";
    font.description =
        "Effective font; reset resumes inherited theme typography.";
    font.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    font.bindable = false;
    define_bindable_property({
        std::move(font),
        detail::BindingNoexceptMethodGetter<Label, FontSpec>(
            *this, &Label::font),
        detail::DirectPropertySetter<Label, FontSpec>(
            *this, &Label::set_font), {},
        detail::BoundMemberFunction<void (Label::*)()>(
            *this, &Label::clear_font),
        detail::BoundMemberFunction<bool (Label::*)() const noexcept>(
            *this, &Label::has_font_override),
        detail::BoundMemberFunction<
            PropertyValueOrigin (Label::*)() const noexcept>(
                *this, &Label::font_property_origin)});

    PropertyDescriptor foreground;
    foreground.name = "fore_color";
    foreground.kind = BindingValueKind::color;
    foreground.category = "Appearance";
    foreground.description =
        "Effective text color; reset resumes inherited theme color.";
    foreground.invalidation_effects = Dirty::paint | Dirty::semantics;
    foreground.bindable = false;
    define_bindable_property({
        std::move(foreground),
        detail::BindingMethodGetter<Label, Color>(
            *this, &Label::foreground),
        detail::DirectPropertySetter<Label, Color>(
            *this, &Label::set_foreground), {},
        detail::BoundMemberFunction<void (Label::*)()>(
            *this, &Label::clear_foreground),
        detail::BoundMemberFunction<bool (Label::*)() const noexcept>(
            *this, &Label::has_foreground_override),
        detail::BoundMemberFunction<
            PropertyValueOrigin (Label::*)() const noexcept>(
                *this, &Label::foreground_property_origin)});

    PropertyDescriptor alignment;
    alignment.name = "alignment";
    alignment.kind = BindingValueKind::enumeration;
    alignment.category = "Appearance";
    alignment.description = "Horizontal placement of the text: near, center or far.";
    alignment.default_value = BindingValue{PropertyEnumValue{"horizontal_alignment", "near", 0}};
    alignment.invalidation_effects = Dirty::paint | Dirty::semantics;
    alignment.bindable = false;
    alignment.enumeration = horizontal_alignment_enum();
    define_bindable_property({
        std::move(alignment),
        detail::EnumerationPropertyGetter<Label, HorizontalAlignment>(*this, &Label::alignment,
                                                                      horizontal_alignment_enum()),
        detail::EnumerationPropertySetter<Label, HorizontalAlignment>(*this, &Label::set_alignment), {}, {},
        {}});

    PropertyDescriptor vertical_alignment;
    vertical_alignment.name = "vertical_alignment";
    vertical_alignment.kind = BindingValueKind::enumeration;
    vertical_alignment.category = "Appearance";
    vertical_alignment.description = "Vertical placement of the text: near, center or far.";
    vertical_alignment.default_value = BindingValue{PropertyEnumValue{"vertical_alignment", "center", 1}};
    vertical_alignment.invalidation_effects = Dirty::paint | Dirty::semantics;
    vertical_alignment.bindable = false;
    vertical_alignment.enumeration = vertical_alignment_enum();
    define_bindable_property({
        std::move(vertical_alignment),
        detail::EnumerationPropertyGetter<Label, VerticalAlignment>(*this, &Label::vertical_alignment,
                                                                    vertical_alignment_enum()),
        detail::EnumerationPropertySetter<Label, VerticalAlignment>(*this, &Label::set_vertical_alignment),
        {}, {}, {}});

    PropertyDescriptor text_wrapping;
    text_wrapping.name = "text_wrapping";
    text_wrapping.kind = BindingValueKind::enumeration;
    text_wrapping.category = "Layout";
    text_wrapping.description = "Whether the text wraps at word boundaries: no_wrap or word.";
    text_wrapping.default_value = BindingValue{PropertyEnumValue{"text_wrapping", "no_wrap", 0}};
    text_wrapping.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    text_wrapping.bindable = false;
    text_wrapping.enumeration = text_wrapping_enum();
    define_bindable_property({
        std::move(text_wrapping),
        detail::EnumerationPropertyGetter<Label, TextWrapping>(*this, &Label::text_wrapping, text_wrapping_enum()),
        detail::EnumerationPropertySetter<Label, TextWrapping>(*this, &Label::set_text_wrapping),
        {}, {}, {}});

    PropertyDescriptor maximum_lines;
    maximum_lines.name = "maximum_lines";
    maximum_lines.kind = BindingValueKind::unsigned_integer;
    maximum_lines.category = "Layout";
    maximum_lines.description =
        "Maximum painted and measured line count; zero keeps every line.";
    maximum_lines.default_value = BindingValue{std::uint64_t{0}};
    maximum_lines.invalidation_effects = Dirty::measure | Dirty::paint;
    maximum_lines.bindable = false;
    define_bindable_property({
        std::move(maximum_lines),
        detail::BoundMemberFunction<BindingValue (Label::*)() const>(
            *this, &Label::maximum_lines_property_value),
        detail::BoundMemberFunction<
            void (Label::*)(const BindingValue&)>(
                *this, &Label::set_maximum_lines_property_value),
        {}, {}, {}});
}

PropertyValueOrigin Label::font_property_origin() const noexcept {
    return has_font_override() ? PropertyValueOrigin::local
                               : PropertyValueOrigin::inherited;
}

PropertyValueOrigin Label::foreground_property_origin() const noexcept {
    return has_foreground_override() ? PropertyValueOrigin::local
                                     : PropertyValueOrigin::inherited;
}

BindingValue Label::maximum_lines_property_value() const {
    return BindingValue{static_cast<std::uint64_t>(maximum_lines_)};
}

void Label::set_maximum_lines_property_value(const BindingValue& value) {
    set_maximum_lines(static_cast<std::size_t>(
        std::get<std::uint64_t>(value)));
}

void Label::set_text(std::string text) {
    require_mutable();
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    // Text only changes retained geometry when this label is content-sized.
    // Fixed labels are a common high-rate telemetry surface; making every text
    // update a measure request needlessly invalidates every layout ancestor.
    const Rect requested = requested_bounds();
    Dirty effects = Dirty::paint | Dirty::semantics | Dirty::accessibility;
    if (auto_size.read() || requested.width <= 0.0 || requested.height <= 0.0) {
        effects |= Dirty::measure | Dirty::arrange;
    }
    invalidate(effects);
    publish_change(text_changed_, text_);
}

FontSpec Label::font() const noexcept {
    if (font_override_) return *font_override_;
    const ThemeTypographyTokens& typography =
        effective_theme().structure().typography;
    switch (text_style_role_) {
    case TextStyleRole::body: return typography.field;
    case TextStyleRole::control: return typography.control;
    case TextStyleRole::caption: return typography.caption;
    case TextStyleRole::heading: return typography.heading;
    case TextStyleRole::title: return typography.title;
    case TextStyleRole::monospace: return typography.monospace;
    }
    return typography.field;
}

void Label::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) {
        throw std::invalid_argument("Label font specification is invalid");
    }
    set_font_from_code(true);
    if (font_override_ && *font_override_ == font) {
        return;
    }
    font_override_ = font;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void Label::clear_font() {
    require_mutable();
    if (!font_override_) return;
    font_override_.reset();
    set_font_from_code(false);
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void Label::set_text_style_role(TextStyleRole role) {
    require_mutable();
    switch (role) {
    case TextStyleRole::body:
    case TextStyleRole::control:
    case TextStyleRole::caption:
    case TextStyleRole::heading:
    case TextStyleRole::title:
    case TextStyleRole::monospace:
        break;
    default:
        throw std::invalid_argument("Label text style role is invalid");
    }
    if (text_style_role_ == role) return;
    text_style_role_ = role;
    if (!font_override_) {
        invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    } else {
        invalidate(Dirty::semantics);
    }
}

// The text colour paint_label_text draws: the authored foreground, or the
// theme's text with the fields of the rules that select this label (its
// type, stable ID, classes and states) in place (G3).
Color Label::foreground() const {
    if (foreground_override_) return *foreground_override_;
    const ResolvedControlRecipe resolved =
        effective_theme().resolve(*this, ControlVisualRole::panel, visual_context());
    const Color text = resolved.recipe().text;
    return text;
}

void Label::set_foreground(Color color) {
    require_mutable();
    if (foreground_override_ && *foreground_override_ == color) {
        return;
    }
    foreground_override_ = color;
    invalidate(Dirty::paint | Dirty::semantics);
}

void Label::clear_foreground() {
    require_mutable();
    if (!foreground_override_) return;
    foreground_override_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
}

void Label::set_alignment(HorizontalAlignment alignment) {
    require_mutable();
    switch (alignment) {
    case HorizontalAlignment::near:
    case HorizontalAlignment::center:
    case HorizontalAlignment::far:
        break;
    default:
        throw std::invalid_argument("Label horizontal alignment is invalid");
    }
    if (alignment_ == alignment) {
        return;
    }
    alignment_ = alignment;
    invalidate(Dirty::paint | Dirty::semantics);
}

void Label::set_vertical_alignment(VerticalAlignment alignment) {
    require_mutable();
    switch (alignment) {
    case VerticalAlignment::near:
    case VerticalAlignment::center:
    case VerticalAlignment::far:
        break;
    default:
        throw std::invalid_argument("Label vertical alignment is invalid");
    }
    if (vertical_alignment_ == alignment) {
        return;
    }
    vertical_alignment_ = alignment;
    invalidate(Dirty::paint | Dirty::semantics);
}

void Label::set_text_wrapping(TextWrapping wrapping) {
    require_mutable();
    switch (wrapping) {
    case TextWrapping::no_wrap:
    case TextWrapping::word:
        break;
    default:
        throw std::invalid_argument("Label text wrapping mode is invalid");
    }
    if (text_wrapping_ == wrapping) {
        return;
    }
    text_wrapping_ = wrapping;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void Label::set_line_spacing(double spacing) {
    require_mutable();
    if (!std::isfinite(spacing) || spacing < 0.75 || spacing > 3.0) {
        throw std::invalid_argument("Label line spacing must be finite and between 0.75 and 3.0");
    }
    if (line_spacing_ == spacing) {
        return;
    }
    line_spacing_ = spacing;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void Label::set_text_case_transform(TextCaseTransform transform) {
    require_mutable();
    switch (transform) {
    case TextCaseTransform::none:
    case TextCaseTransform::uppercase_ascii:
    case TextCaseTransform::lowercase_ascii:
        break;
    default:
        throw std::invalid_argument("Label text case transform is invalid");
    }
    if (text_case_transform_ == transform) return;
    text_case_transform_ = transform;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void Label::set_maximum_lines(std::size_t maximum_lines) {
    require_mutable();
    if (maximum_lines > 4096U) {
        throw std::out_of_range(
            "Label maximum lines must be zero or at most 4096");
    }
    if (maximum_lines_ == maximum_lines) return;
    maximum_lines_ = maximum_lines;
    invalidate(Dirty::measure | Dirty::paint);
}

void Label::set_use_mnemonic(bool value) {
    require_mutable();
    if (use_mnemonic_ == value) return;
    use_mnemonic_ = value;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
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
    const Size measured = measure_wrapped_text(std::max(0.0, wrap_width - 4.0), available);
    return measured;
}

Size Label::measure_intrinsic(const LayoutConstraint& constraint) {
    const AvailableExtentKind kind = constraint.width.kind;
    const bool content_query = kind == AvailableExtentKind::min_content ||
                               kind == AvailableExtentKind::max_content;
    if (!content_query || requested_bounds().width > 0.0 ||
        text_wrapping_ == TextWrapping::no_wrap) {
        const Size measured = Control::measure_intrinsic(constraint);
        return measured;
    }
    const Size available = measure_available_size(constraint);
    double content_width_limit = practical_unbounded_extent;
    if (kind == AvailableExtentKind::min_content) {
        const std::string text = display_text();
        content_width_limit = widest_unbreakable_segment(text, text_font((*this).font()));
    }
    Size measured = measure_wrapped_text(content_width_limit, available);
    // A container that lays the label out at this width hands it back as a
    // definite width, and measure and paint wrap at that width less the
    // inset. Adding and then subtracting the inset can round below the
    // widest word, which would then break inside it; step up by the unit in
    // the last place until it cannot. One step corrects the rounding of one
    // addition; four bound the loop.
    if (kind == AvailableExtentKind::min_content) {
        for (int step = 0; step < 4 && measured.width - 4.0 < content_width_limit; ++step) {
            measured.width = std::nextafter(measured.width, practical_unbounded_extent);
        }
    }
    if (constraint.height.kind == AvailableExtentKind::definite) {
        measured.height = constraint.height.extent.value();
    }
    return measured;
}

double Label::widest_unbreakable_segment(const std::string_view text,
                                         const FontSpec font) const {
    // The same segments label_lines wraps between: runs of bytes that are
    // not ASCII whitespace. A newline is whitespace, so paragraphs need no
    // separate pass.
    double widest = 0.0;
    std::size_t cursor = 0U;
    while (cursor < text.size()) {
        while (cursor < text.size() &&
               std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
            ++cursor;
        }
        std::size_t word_end = cursor;
        while (word_end < text.size() &&
               std::isspace(static_cast<unsigned char>(text[word_end])) == 0) {
            ++word_end;
        }
        if (word_end > cursor) {
            const ResolvedTextLayout word =
                resolve_text_layout_utf8(text.substr(cursor, word_end - cursor), font);
            widest = std::max(widest, word.logical_size.width);
        }
        cursor = word_end;
    }
    return widest;
}

Size Label::measure_wrapped_text(const double content_width_limit,
                                 const Size available) const {
    const Rect requested = requested_bounds();
    const std::string text = display_text();
    const FontSpec font = text_font((*this).font());
    const TextWidthResolver resolve{LabelWidth{*this, font}};
    std::vector<std::string> lines = label_lines(
        text, font, content_width_limit, text_wrapping_, resolve, maximum_lines_);
    double content_width{};
    double line_height{};
    for (const std::string& line : lines) {
        const ResolvedTextLayout metrics = resolve_text_layout_utf8(line, font);
        content_width = std::max(content_width, metrics.logical_size.width);
        line_height = std::max(
            line_height, std::max(font.size, metrics.logical_size.height) *
                             line_spacing_);
    }
    if (line_height <= 0.0) line_height = font.size * line_spacing_;
    const double preferred_width = requested.width > 0.0
        ? requested.width : content_width + 4.0;
    const double preferred_height = requested.height > 0.0
        ? requested.height
        : static_cast<double>(lines.size()) * line_height + 4.0;
    const Size measured{std::min(available.width, preferred_width),
                        std::min(available.height, preferred_height)};
    return measured;
}

std::string Label::display_text() const {
    std::string display = use_mnemonic_
        ? parse_mnemonic_text(text_).display_text : text_;
    for (char& character : display) {
        if (text_case_transform_ == TextCaseTransform::uppercase_ascii &&
            character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        } else if (text_case_transform_ == TextCaseTransform::lowercase_ascii &&
                   character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return display;
}

Label::TextLayout Label::layout_label_text(const std::string_view text,
                                           Painter* painter) const {
    const Rect arranged = committed_arranged_bounds();
    const FontSpec font = text_font((*this).font());
    const TextWidthResolver resolve = painter != nullptr
        ? TextWidthResolver{PainterLabelWidth{*painter, font}}
        : TextWidthResolver{LabelWidth{*this, font}};
    TextLayout layout;
    layout.lines = label_lines(
        text, font, std::max(0.0, arranged.width - 4.0), text_wrapping_, resolve, maximum_lines_);
    std::vector<ResolvedTextLayout> metrics{};
    metrics.reserve(layout.lines.size());
    double line_height{};
    for (const std::string& line : layout.lines) {
        if (painter != nullptr) {
            metrics.push_back((*painter).resolve_text_layout_utf8(line, font));
        } else {
            metrics.push_back(resolve_text_layout_utf8(line, font));
        }
        line_height = std::max(
            line_height,
            std::max(font.size, metrics.back().logical_size.height) *
                line_spacing_);
    }
    if (line_height <= 0.0) line_height = font.size * line_spacing_;
    const double block_height = static_cast<double>(layout.lines.size()) * line_height;
    double top = 1.0;
    if (vertical_alignment_ == VerticalAlignment::center) {
        top = std::max(1.0, (arranged.height - block_height) * 0.5);
    } else if (vertical_alignment_ == VerticalAlignment::far) {
        top = std::max(1.0, arranged.height - block_height - 1.0);
    }
    layout.origins.reserve(layout.lines.size());
    layout.widths.reserve(layout.lines.size());
    double block_left = arranged.width;
    double block_right = 0.0;
    for (std::size_t index = 0; index < layout.lines.size(); ++index) {
        const double text_width = metrics[index].logical_size.width;
        double x = 2.0;
        if (alignment_ == HorizontalAlignment::center) {
            x = std::max(2.0, (arranged.width - text_width) * 0.5);
        } else if (alignment_ == HorizontalAlignment::far) {
            x = std::max(2.0, arranged.width - text_width - 2.0);
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
void Label::paint_label_text(Painter& painter, const std::string_view text) const {
    const FontSpec font = text_font((*this).font());
    const TextLayout layout = layout_label_text(text, &painter);
    PartPaint line;
    line.family = ControlVisualRole::panel;
    line.role = PartRole::label;
    line.font = font;
    line.authored_foreground = foreground_override_;
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
    return use_mnemonic_ && is_mnemonic(character, text_);
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
        ? (use_mnemonic_ ? parse_mnemonic_text(text_).display_text : text_)
        : accessible_name.read();
    descriptor.description = accessible_description.read();
    // The mnemonic moves the focus to the next control; the label publishes
    // it, as a Win32 static control does.
    if (use_mnemonic_) descriptor.access_key = mnemonic_access_key(text_);
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

} // namespace gui_forms
