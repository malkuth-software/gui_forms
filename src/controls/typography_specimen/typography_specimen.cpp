#include "gui_forms/controls/typography_specimen/typography_specimen.hpp"

#include "gui_forms/visual_inspection.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

template <typename Resolver>
std::vector<std::string> wrap_lines(std::string_view text, double width,
                                    bool wrap, Resolver&& resolve) {
    if (!wrap || width <= 8.0) return {std::string(text)};
    std::vector<std::string> lines;
    std::string line;
    std::size_t cursor{};
    while (cursor < text.size()) {
        while (cursor < text.size() &&
               std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
            ++cursor;
        }
        if (cursor >= text.size()) break;
        std::size_t end = cursor;
        while (end < text.size() &&
               std::isspace(static_cast<unsigned char>(text[end])) == 0) {
            ++end;
        }
        const std::string_view word = text.substr(cursor, end - cursor);
        std::string candidate = line;
        if (!candidate.empty()) candidate.push_back(' ');
        candidate.append(word);
        if (!line.empty() && resolve(candidate).logical_size.width > width) {
            lines.push_back(std::move(line));
            line.assign(word);
        } else {
            line = std::move(candidate);
        }
        cursor = end;
    }
    if (!line.empty()) lines.push_back(std::move(line));
    if (lines.empty()) lines.emplace_back();
    return lines;
}

// Resolves one line through the control's attached terminal provider.
struct ControlLineResolver final {
    const Control& control;
    FontSpec effective;

    [[nodiscard]] ResolvedTextLayout operator()(std::string_view line) const {
        return control.resolve_text_layout_utf8(line, effective);
    }
};

// Resolves one line through the painter that will draw it.
struct PainterLineResolver final {
    Painter& painter;
    FontSpec effective;

    [[nodiscard]] ResolvedTextLayout operator()(std::string_view line) const {
        return painter.resolve_text_layout_utf8(line, effective);
    }
};

} // namespace

// The run-time schema of a specimen (stage FD6.3), what a form document
// sets on it, is the generated table below; no property raises a change
// event.
TypographySpecimen::TypographySpecimen(StableId stable_id, std::string initial_text)
    : Control(std::move(stable_id)) {
    text = std::move(initial_text);
}

ResolvedTextLayout TypographySpecimen::resolved_layout() const {
    return resolve_text_layout_utf8(text.read(), text_font(font.read()));
}

Size TypographySpecimen::measure(Size available) {
    const FontSpec effective = text_font(font.read());
    const ControlLineResolver resolve{*this, effective};
    const std::vector<std::string> lines = wrap_lines(
        text.read(), std::max(0.0, available.width - 20.0), wrap_text.read(), resolve);
    double content_width{};
    double content_height{};
    for (const std::string& line : lines) {
        const ResolvedTextLayout resolved = resolve(line);
        content_width = std::max(content_width, resolved.logical_size.width);
        content_height += std::max(effective.size, resolved.logical_size.height);
    }
    const double scale = effective_text_scale();
    const double caption_height = caption.read().empty() ? 0.0 : 8.0 * scale * 1.2 + 6.0;
    const double diagnostics = diagnostics_visible.read()
        ? 8.0 * scale * 1.2 + 8.0 : 0.0;
    return {std::min(available.width,
                     std::max(160.0, content_width + 24.0)),
            std::min(available.height,
                     std::max(28.0, content_height + caption_height +
                                        diagnostics + 8.0))};
}

void TypographySpecimen::on_paint(Painter& painter, Rect) {
    const Rect bounds = client_rectangle();
    const FontSpec effective = text_font(font.read());
    const PainterLineResolver resolve{painter, effective};
    const std::vector<std::string> lines = wrap_lines(
        text.read(), std::max(0.0, bounds.width - 20.0), wrap_text.read(), resolve);
    std::vector<ResolvedTextLayout> resolved_lines;
    resolved_lines.reserve(lines.size());
    for (const std::string& line : lines) resolved_lines.push_back(resolve(line));
    const ResolvedTextLayout& resolved = resolved_lines.front();
    const Color ink = Color::rgba(27, 43, 57);
    const Color muted = Color::rgba(83, 103, 118);
    const Color baseline_ink = Color::rgba(64, 145, 196, 145);
    const Color fault = Color::rgba(157, 63, 55);
    const FontSpec caption_font =
        effective_font({FontRole::control, 8.0, 700, false, 0.24});

    painter.fill_rect(bounds, Color::rgba(249, 251, 252));
    painter.stroke_rect({0.0, 0.0, std::max(0.0, bounds.width - 1.0),
                         std::max(0.0, bounds.height - 1.0)},
                        Color::rgba(172, 184, 191), 1.0);
    if (!caption.read().empty()) {
        painter.draw_text_utf8(
            {10.0, snap_text_baseline(caption_font.size + 3.0)}, caption.read(),
            caption_font, muted);
    }
    // The first line's baselines, which the footer reports for every run
    // (sweep defect D27: a wrapped run reported its last line's).
    const double first_logical_baseline = caption_font.size + 8.0 + resolved.ascent;
    const double first_baseline = snap_text_baseline(first_logical_baseline);
    const Color line_box_ink = Color::rgba(64, 145, 196, 70);
    double logical_baseline = first_logical_baseline;
    double baseline = first_baseline;
    for (std::size_t index = 0U; index < lines.size(); ++index) {
        if (index != 0U) {
            logical_baseline += std::max(
                effective.size, resolved_lines[index - 1U].logical_size.height);
            baseline = snap_text_baseline(logical_baseline);
        }
        // The adornments the diagnostics switch shows (D22: the line box
        // was promised but never drawn, and the baseline was always on):
        // the line box from ascent to descent, and the baseline.
        if (diagnostics_visible.read()) {
            const ResolvedTextLayout& line = resolved_lines[index];
            const Rect line_box{8.5, baseline - line.ascent + 0.5, std::max(0.0, bounds.width - 17.0),
                                std::max(0.0, line.ascent + line.descent - 1.0)};
            painter.stroke_rect(line_box, line_box_ink, 1.0);
            painter.draw_line({8.0, baseline}, {bounds.width - 8.0, baseline}, baseline_ink, 1.0);
        }
        painter.draw_text_utf8(
            {10.0, baseline}, lines[index], effective,
            resolved_lines[index].status == TextResolutionStatus::exact
                ? ink : fault);
    }

    if (diagnostics_visible.read()) {
        std::ostringstream details;
        details << text_resolution_status_name(resolved.status) << ' ';
        if (resolved.primary_family.empty()) details << "no resolved face";
        else details << resolved.primary_family;
        details << ' ' << resolved.runs.size() << "r";
        if (resolved.missing_clusters != 0U) {
            details << " !" << resolved.missing_clusters;
        }
        details << " A" << std::fixed << std::setprecision(0)
                << resolved.ascent << "/D"
                << resolved.descent << " B" << std::setprecision(1)
                << first_logical_baseline << ">" << first_baseline
                << " @" << (window() != nullptr ? (*window()).scale() : 1.0)
                << "×";
        painter.draw_text_utf8(
            {10.0, snap_text_baseline(bounds.height - 7.0)}, details.str(),
            effective_font({FontRole::monospace, 8.0, 400, false}), muted);
    }
}

bool TypographySpecimen::hit_test_local(Point) const { return false; }

SemanticDescriptor TypographySpecimen::semantic_descriptor() const {
    const ResolvedTextLayout resolved = resolved_layout();
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::static_text;
    descriptor.name = caption.read().empty() ? text.read() : caption.read() + ": " + text.read();
    descriptor.value = std::string(text_resolution_status_name(resolved.status)) +
        "; " + (resolved.primary_family.empty() ? "no resolved face"
                                                  : resolved.primary_family);
    descriptor.exposed = true;
    return descriptor;
}

namespace {

constexpr std::array<PartField<TypographySpecimen::Parts>, 4> typography_specimen_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &TypographySpecimen::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &TypographySpecimen::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &TypographySpecimen::Parts::edge},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &TypographySpecimen::Parts::focus},
}};

constexpr std::array<StateField<TypographySpecimen::States>, 5> typography_specimen_state_fields{{
    {.state = AnatomyState::normal, .member = &TypographySpecimen::States::normal},
    {.state = AnatomyState::enabled, .member = &TypographySpecimen::States::enabled},
    {.state = AnatomyState::disabled, .member = &TypographySpecimen::States::disabled},
    {.state = AnatomyState::focused, .member = &TypographySpecimen::States::focused},
    {.state = AnatomyState::focus_visible, .member = &TypographySpecimen::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 4> typography_specimen_parts =
    part_declarations(typography_specimen_part_fields);
constexpr std::array<AnatomyState, 5> typography_specimen_states =
    state_declarations(typography_specimen_state_fields);

constexpr ControlAnatomy typography_specimen_anatomy{
    .control_id = "typography_specimen",
    .parts = typography_specimen_parts,
    .states = typography_specimen_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& TypographySpecimen::anatomy() const noexcept {
    return typography_specimen_anatomy;
}

void TypographySpecimen::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.face = PartGeometry::from_bounds(bounds);
    record.edge = PartGeometry::from_bounds(bounds);
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), typography_specimen_part_fields, record, geometry);
}

TypographySpecimen::Parts TypographySpecimen::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), typography_specimen_part_fields, published.parts(), record);
    return record;
}

TypographySpecimen::States TypographySpecimen::states() const {
    States record;
    read_states(typography_specimen_state_fields, anatomy_states(), record);
    return record;
}

// property_codemod begin TypographySpecimen (tools/property_families/typography_specimen.json; generated, do not edit)

namespace {

PropertyDescriptor typography_specimen_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The run of text the specimen commits and measures.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_typography_specimen_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        typography_specimen_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor typography_specimen_font_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "font";
    descriptor.kind = BindingValueKind::font;
    descriptor.category = "Appearance";
    descriptor.description = "The FontSpec the run is committed with.";
    descriptor.default_value = BindingValue{FontSpec{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.content_value = true;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_typography_specimen_font() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        typography_specimen_font_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor typography_specimen_caption_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "caption";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The line under the run that names its font.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_typography_specimen_caption() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        typography_specimen_caption_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor typography_specimen_wrap_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "wrap_text";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Layout";
    descriptor.description = "Whether the run wraps at the specimen's width.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_typography_specimen_wrap_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        typography_specimen_wrap_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, TypographySpecimen::typography_specimen_property_count>& TypographySpecimen::typography_specimen_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"text", &describe_typography_specimen_text,
         &read_cell_entry<TypographySpecimen, decltype(&TypographySpecimen::text), &TypographySpecimen::text>,
         &write_cell_entry<TypographySpecimen, decltype(&TypographySpecimen::text), &TypographySpecimen::text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"font", &describe_typography_specimen_font,
         &read_cell_entry<TypographySpecimen, decltype(&TypographySpecimen::font), &TypographySpecimen::font>,
         &write_cell_entry<TypographySpecimen, decltype(&TypographySpecimen::font), &TypographySpecimen::font>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"caption", &describe_typography_specimen_caption,
         &read_cell_entry<TypographySpecimen, decltype(&TypographySpecimen::caption), &TypographySpecimen::caption>,
         &write_cell_entry<TypographySpecimen, decltype(&TypographySpecimen::caption), &TypographySpecimen::caption>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"wrap_text", &describe_typography_specimen_wrap_text,
         &read_cell_entry<TypographySpecimen, decltype(&TypographySpecimen::wrap_text), &TypographySpecimen::wrap_text>,
         &write_cell_entry<TypographySpecimen, decltype(&TypographySpecimen::wrap_text), &TypographySpecimen::wrap_text>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "TypographySpecimen's property table names a property twice");
    static const std::array<PropertyEntry, typography_specimen_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> TypographySpecimen::property_table() const noexcept {
    const std::array<PropertyEntry, typography_specimen_property_count>& entries = typography_specimen_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void TypographySpecimen::prepare_write(const decltype(text)&, std::string&) {
    require_mutable();
}

void TypographySpecimen::written(const decltype(text)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics | Dirty::accessibility);
}

void TypographySpecimen::prepare_write(const decltype(font)&, FontSpec& incoming) {
    require_mutable();
    if (!valid_font_spec(incoming)) throw std::invalid_argument("TypographySpecimen font is invalid");
    set_font_from_code(true);
}

void TypographySpecimen::written(const decltype(font)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void TypographySpecimen::prepare_write(const decltype(caption)&, std::string&) {
    require_mutable();
}

void TypographySpecimen::written(const decltype(caption)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void TypographySpecimen::prepare_write(const decltype(diagnostics_visible)&, bool&) {
    require_mutable();
}

void TypographySpecimen::written(const decltype(diagnostics_visible)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void TypographySpecimen::prepare_write(const decltype(wrap_text)&, bool&) {
    require_mutable();
}

void TypographySpecimen::written(const decltype(wrap_text)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

// property_codemod end TypographySpecimen

} // namespace gui_forms
