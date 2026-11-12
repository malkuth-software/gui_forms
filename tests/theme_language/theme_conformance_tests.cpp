// The theme language's conformance corpus (stage T3, phase P11; PLAN.md
// section 6, items 1, 2 and 5). Each fixture in `conformance/` is a sheet,
// `<name>.css`, whose first comment names the forms it covers
// (`forms: selector.class value.unit.px ...`) and, for an inline style,
// `source: inline`. Its expected outcome, `<name>.expected`, holds the
// compilation's outcome, its diagnostics (code, line, column, rule,
// property, detail) and every lowered rule as text. The test:
//
// - compiles each fixture and compares the text with the expected file;
//   with GUI_FORMS_THEME_CONFORMANCE_WRITE=1 it writes the expected files
//   instead (review the difference before committing it);
// - checks that the sheet round-trips byte for byte (item 5), and that a
//   sheet that compiles is accepted by the engine without a diagnostic;
// - checks the coverage table, `conformance/COVERAGE.tsv` (form, fixture):
//   every form names a fixture that exists and lists the form, and every
//   form a fixture lists is in the table (item 1: a form with no fixture
//   fails);
// - for every diagnostic code, checks that a fixture reports it (item 2:
//   one fixture per error and warning code), except the codes the table's
//   `code-elsewhere` rows place in another test with the reason.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../src/core/theme_language/package/theme_manifest.hpp"
#include "../../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../src/core/theme_language/tokenizer/theme_tokenizer.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/engine/engine_expression/engine_expression.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef GUI_FORMS_THEME_CONFORMANCE_DIR
#error "GUI_FORMS_THEME_CONFORMANCE_DIR names the fixture directory"
#endif

namespace {

using namespace gui_forms;
namespace theme_language = gui_forms::theme_language;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// -- the vocabulary the fixtures compile against ------------------------------

// A button with the parts the fixtures name, every standard state and
// the custom states written `:state()`; a track bar for a kebab-case type;
// a text box for the parts its themed look leaves unpainted.
constexpr std::array<AnatomyState, 2> thumb_states{{AnatomyState::pressed, AnatomyState::hot}};
constexpr std::array<PartDeclaration, 4> button_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
    {.name = "label", .role = PartRole::label, .composed_from = {}, .states = {}},
    {.name = "glyph", .role = PartRole::glyph, .composed_from = {}, .states = {}},
    {.name = "focus_cue", .role = PartRole::focus, .composed_from = {}, .states = {}},
}};
constexpr std::array<AnatomyState, 16> button_states{{
    AnatomyState::hot, AnatomyState::pressed, AnatomyState::focused, AnatomyState::focus_visible,
    AnatomyState::checked, AnatomyState::indeterminate, AnatomyState::disabled, AnatomyState::enabled,
    AnatomyState::read_only, AnatomyState::invalid, AnatomyState::open, AnatomyState::preselect,
    AnatomyState::drop_accepted, AnatomyState::drag_proximate, AnatomyState::selected, AnatomyState::pending,
}};
constexpr std::array<PartDeclaration, 2> track_parts{{
    {.name = "track", .role = PartRole::track, .composed_from = {}, .states = {}},
    {.name = "thumb", .role = PartRole::thumb, .composed_from = {}, .states = thumb_states},
}};
constexpr std::array<AnatomyState, 3> track_states{{AnatomyState::hot, AnatomyState::focused, AnatomyState::disabled}};
// A text box, whose themed look paints none of its face, edge and content
// parts as a rule names them (W112 part_not_themed).
constexpr std::array<PartDeclaration, 3> text_box_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
    {.name = "edge", .role = PartRole::edge, .composed_from = {}, .states = {}},
    {.name = "content", .role = PartRole::content, .composed_from = {}, .states = {}},
}};
const std::array<ControlAnatomy, 3> controls{{
    {.control_id = "button", .parts = button_parts, .states = button_states, .custom_states = {}, .item_systems = {}},
    {.control_id = "track_bar", .parts = track_parts, .states = track_states, .custom_states = {}, .item_systems = {}},
    {.control_id = "text_box", .parts = text_box_parts, .states = track_states, .custom_states = {}, .item_systems = {}},
}};

// The resources a fixture's manifest would list; the images are bound to
// ids no window draws.
const std::array<std::string_view, 3> resources{{"icon.check", "mask.dots", "tile.quadrant"}};
const std::array<theme_language::ThemeImageResource, 2> images{{
    {.resource = "tile.quadrant", .image = ImageId{1U}, .pixel_size = Size{4.0, 4.0}, .pixels_per_logical_pixel = 1.0},
    {.resource = "mask.dots", .image = ImageId{2U}, .pixel_size = Size{8.0, 8.0}, .pixels_per_logical_pixel = 1.0},
}};
// A value the application publishes (R2-06), which var() reads as an
// input.
const std::array<std::string, 1> published{{"app-progress"}};

// -- the text of a compilation ------------------------------------------------

// The shortest text that reads back as `value`.
std::string number_text(double value) {
    std::array<char, 64> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (written.ec != std::errc{}) return "?";
    return std::string(buffer.data(), written.ptr);
}

std::string colour_text(Color colour) {
    std::array<char, 16> buffer{};
    const std::array<std::uint8_t, 4> channels{{colour.red, colour.green, colour.blue, colour.alpha}};
    std::string text = "#";
    for (const std::uint8_t channel : channels) {
        const std::to_chars_result written =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), static_cast<unsigned int>(channel), 16);
        if (written.ptr == buffer.data() + 1) text += "0";
        text.append(buffer.data(), written.ptr);
    }
    return text;
}

std::string origin_text(ColourOrigin origin) {
    if (origin.source == ColourSource::none) return "";
    if (origin.source == ColourSource::literal) return " origin=literal";
    const std::string side = origin.side == ColourRoleSide::foreground ? "foreground" : "background";
    return " origin=role(" + std::string(colour_role_name(origin.role)) + " " + side + ")";
}

std::string numbers_text(std::span<const double> values) {
    std::string text = "[";
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (index != 0U) text += " ";
        text += number_text(values[index]);
    }
    text += "]";
    return text;
}

std::string states_text(std::span<const AnatomyState> states) {
    std::string text{};
    for (const AnatomyState state : states) {
        text += ":";
        text += std::string(anatomy_state_name(state));
    }
    return text;
}

std::string selector_text(const RuleSelector& selector) {
    std::string text{};
    if (selector.surface == SurfaceKind::window) text += "surface=window ";
    if (selector.surface == SurfaceKind::popup) text += "surface=popup ";
    if (!selector.control_type.empty()) text += "type=" + selector.control_type + " ";
    if (!selector.stable_id.empty()) text += "id=" + selector.stable_id + " ";
    for (const std::string& name : selector.classes) text += "class=" + name + " ";
    if (!selector.part.empty()) text += "part=" + selector.part + " ";
    if (selector.role) text += "role=" + std::string(part_role_name(*selector.role)) + " ";
    if (!selector.states.empty()) text += "states=" + states_text(selector.states) + " ";
    if (!selector.family.empty()) text += "family=" + selector.family + " ";
    for (const RuleExclusion& exclusion : selector.excluded) {
        text += "not(";
        if (!exclusion.control_type.empty()) text += "type=" + exclusion.control_type + " ";
        if (!exclusion.stable_id.empty()) text += "id=" + exclusion.stable_id + " ";
        for (const std::string& name : exclusion.classes) text += "class=" + name + " ";
        if (!exclusion.states.empty()) text += "states=" + states_text(exclusion.states) + " ";
        if (!exclusion.family.empty()) text += "family=" + exclusion.family + " ";
        if (text.back() == ' ') text.pop_back();
        text += ") ";
    }
    for (const RuleAncestor& ancestor : selector.ancestors) {
        text += ancestor.combinator == AncestorCombinator::child ? "parent(" : "ancestor(";
        if (!ancestor.control_type.empty()) text += "type=" + ancestor.control_type + " ";
        if (!ancestor.stable_id.empty()) text += "id=" + ancestor.stable_id + " ";
        for (const std::string& name : ancestor.classes) text += "class=" + name + " ";
        if (text.back() == ' ') text.pop_back();
        text += ") ";
    }
    if (text.empty()) return "*";
    text.pop_back();
    return text;
}

std::string media_text(const MediaCondition& media) {
    if (media_condition_unconditional(media)) return "";
    std::string text = "  media";
    if (media.dark_scheme) text += *media.dark_scheme ? " dark" : " light";
    if (media.forced_colours) text += *media.forced_colours ? " forced" : " unforced";
    if (media.reduced_motion) text += *media.reduced_motion ? " reduced" : " full-motion";
    if (media.minimum_resolution) text += " min=" + number_text(*media.minimum_resolution);
    if (media.maximum_resolution) text += " max=" + number_text(*media.maximum_resolution);
    return text + "\n";
}

std::string program_text(std::span<const ExpressionInstruction> program) {
    std::string text = "[";
    for (std::size_t index = 0U; index < program.size(); ++index) {
        const ExpressionInstruction& instruction = program[index];
        if (index != 0U) text += " ";
        text += std::to_string(static_cast<unsigned int>(instruction.operation));
        if (instruction.operation == ExpressionOperation::constant) {
            text += "(" + number_text(instruction.constant) + ")";
            continue;
        }
        if (instruction.index != 0U) text += "." + std::to_string(static_cast<unsigned int>(instruction.index));
    }
    text += "]";
    return text;
}

std::string declaration_text(const RuleDeclaration& declaration) {
    std::string text = "  decl " + declaration.property + " = ";
    switch (declaration.kind) {
    case RuleValueKind::name:
        text += "name " + declaration.name;
        break;
    case RuleValueKind::number:
        text += "number " + number_text(declaration.number);
        break;
    case RuleValueKind::colour:
        text += "colour " + colour_text(declaration.colour);
        break;
    case RuleValueKind::expression:
        text += "expression " + program_text(declaration.program) + " fallback " + number_text(declaration.number);
        break;
    default:
        text += "kind " + std::to_string(static_cast<unsigned int>(declaration.kind));
        break;
    }
    text += origin_text(declaration.origin);
    if (declaration.number_em != 0.0) text += " em=" + number_text(declaration.number_em);
    return text + "\n";
}

// Whole microseconds, so the text does not depend on the clock's period.
std::string duration_text(FrameInterval interval) {
    const std::chrono::microseconds micros = std::chrono::duration_cast<std::chrono::microseconds>(interval);
    return std::to_string(static_cast<long long>(micros.count())) + "us";
}

std::string curve_text(const MotionCurve& curve) {
    std::string text = "curve kind=" + std::to_string(static_cast<unsigned int>(curve.kind));
    text += " duration=" + duration_text(curve.duration);
    text += " easing=" + std::to_string(static_cast<unsigned int>(curve.easing));
    text += " overshoot=" + number_text(curve.overshoot);
    text += " bezier=" + numbers_text(curve.bezier);
    text += " steps=" + std::to_string(curve.steps);
    text += " step-position=" + std::to_string(static_cast<unsigned int>(curve.step_position));
    text += " delay=" + duration_text(curve.delay);
    return text;
}

std::string transition_text(const PropertyTransition& transition) {
    std::string text = "  transition " + transition.property + " " + curve_text(transition.curve) + "\n";
    for (const PresentationState& state : transition.states) {
        text += "    state when=" + states_text(state.when) + " target=" + number_text(state.target);
        if (state.enter_from) text += " from=" + number_text(*state.enter_from);
        if (state.target_em != 0.0) text += " target-em=" + number_text(state.target_em);
        if (state.enter_from_em != 0.0) text += " from-em=" + number_text(state.enter_from_em);
        for (const MotionStop& stop : state.stops) {
            text += " stop(" + number_text(stop.offset) + " " + number_text(stop.value);
            if (stop.value_em != 0.0) text += " em " + number_text(stop.value_em);
            text += ")";
        }
        text += "\n";
    }
    return text;
}

// A material declaration's digest: its field, the count of each list it
// fills, and its lengths and colours in the order the engine reads them.
std::string material_text(const MaterialDeclaration& declaration) {
    std::string text = "  material " + std::string(material_field_name(declaration.field));
    if (declaration.initial) text += " initial";
    const std::array<std::size_t, 10> counts{{declaration.fills.size(), declaration.gradients.size(),
                                              declaration.blend_modes.size(), declaration.image_positions.size(),
                                              declaration.image_sizes.size(), declaration.image_repeats.size(),
                                              declaration.shadows.size(), declaration.glows.size(),
                                              declaration.glow_placements.size(), declaration.keylines.size()}};
    const std::array<std::string_view, 10> names{{"fills", "gradients", "blends", "positions", "sizes", "repeats",
                                                  "shadows", "glows", "placements", "keylines"}};
    for (std::size_t index = 0U; index < counts.size(); ++index) {
        if (counts[index] != 0U) text += " " + std::string(names[index]) + "=" + std::to_string(counts[index]);
    }
    for (const MaterialBlendMode mode : declaration.blend_modes) {
        text += " blend(" + std::to_string(static_cast<unsigned int>(mode)) + ")";
    }
    for (const MaterialImageSize& size : declaration.image_sizes) {
        text += " size(" + std::to_string(static_cast<unsigned int>(size.sizing)) + ")";
    }
    for (const MaterialImageRepeat repeat : declaration.image_repeats) {
        text += " repeat(" + std::to_string(static_cast<unsigned int>(repeat)) + ")";
    }
    if (declaration.border_style) {
        text += " border-style=" + std::to_string(static_cast<unsigned int>(*declaration.border_style));
    }
    if (declaration.edge_lines != MaterialEdgeLines::single) {
        text += " edge-lines=" + std::to_string(static_cast<unsigned int>(declaration.edge_lines));
    }
    if (declaration.border_image) text += " border-image";
    if (declaration.drop_shadow) text += " drop-shadow";
    if (declaration.mask) text += " mask";
    if (declaration.reflection) {
        const MaterialReflection& reflection = *declaration.reflection;
        text += " reflection edge=" + std::to_string(static_cast<unsigned int>(reflection.edge)) + " opacities=" +
                number_text(reflection.start_opacity) + "," + number_text(reflection.end_opacity);
        if (reflection.source == MaterialReflectionSource::backplane) text += " source=backplane";
    }
    if (declaration.number != 0.0) text += " number=" + number_text(declaration.number);
    if (declaration.relative) text += " relative";
    std::vector<double> lengths{};
    material_declaration_lengths(declaration, lengths);
    if (!lengths.empty()) text += " lengths=" + numbers_text(lengths);
    std::vector<Color> colours{};
    material_declaration_colours(declaration, colours);
    if (!colours.empty()) {
        text += " colours=[";
        for (std::size_t index = 0U; index < colours.size(); ++index) {
            if (index != 0U) text += " ";
            text += colour_text(colours[index]);
        }
        text += "]";
    }
    if (!declaration.em_lengths.empty()) text += " em=" + numbers_text(declaration.em_lengths);
    if (!declaration.current_colours.empty()) text += " current=" + std::to_string(declaration.current_colours.size());
    text += origin_text(declaration.origin);
    return text + "\n";
}

// A surface's backdrop chain (owner decision 2026-11-04): each step in order.
std::string backdrop_text(const BackdropDeclaration& backdrop) {
    std::string text = "  backdrop";
    if (backdrop.length == 0U) text += " none";
    for (std::size_t index = 0U; index < backdrop.length && index < BackdropDeclaration::maximum_steps; ++index) {
        const BackdropStep& step = backdrop.steps[index];
        if (step.kind == BackdropStepKind::blur) text += " blur(" + number_text(step.amount) + ")";
        if (step.kind == BackdropStepKind::saturate) text += " saturate(" + number_text(step.amount) + ")";
        if (step.kind == BackdropStepKind::tint) {
            text += " tint(" + colour_text(step.colour);
            if (step.origin.source == ColourSource::role) text += " role";
            text += ")";
        }
    }
    return text + "\n";
}

std::string recipe_text(const RecipeDeclaration& declaration) {
    std::string text = "  recipe " + std::string(recipe_field_name(declaration.field));
    if (declaration.initial) text += " initial";
    if (declaration.inherit) text += " inherit";
    if (declaration.current_colour) text += " currentcolor";
    text += " colour=" + colour_text(declaration.colour);
    text += " number=" + number_text(declaration.number);
    text += " outline=" + std::to_string(static_cast<unsigned int>(declaration.outline_style));
    // The font fields' own values (T4 fonts): the bundled role and the style.
    if (declaration.field == RecipeField::font_family) {
        text += " role=" + std::to_string(static_cast<unsigned int>(declaration.font_role));
        if (declaration.font_family.value != 0U) {
            text += " family=" + std::to_string(static_cast<unsigned int>(declaration.font_family.value));
        }
    }
    if (declaration.field == RecipeField::font_style && declaration.italic) text += " italic";
    if (declaration.shadowed) {
        text += " shadow(" + number_text(declaration.shadow_offset.x) + " " + number_text(declaration.shadow_offset.y) +
                " " + number_text(declaration.shadow_blur) + ")";
    }
    if (!declaration.em_lengths.empty()) text += " em=" + numbers_text(declaration.em_lengths);
    text += origin_text(declaration.origin);
    return text + "\n";
}

std::string rule_text(std::size_t index, const ThemeRule& rule) {
    std::string text = "rule " + std::to_string(index) + " " + std::string(cascade_level_name(rule.level)) + " " +
                       selector_text(rule.selector) + "\n";
    text += "  label " + rule.label + "\n";
    if (rule.specificity) {
        const RuleSpecificity& given = *rule.specificity;
        text += "  specificity " + std::to_string(given.ids) + "," + std::to_string(given.classes) + "," +
                std::to_string(given.types) + "\n";
    }
    if (rule.requires_engine) {
        text += "  requires " + std::to_string((*rule.requires_engine).major) + "." +
                std::to_string((*rule.requires_engine).minor) + "\n";
    }
    text += media_text(rule.media);
    for (const RuleDeclaration& declaration : rule.declarations) text += declaration_text(declaration);
    for (const PropertyTransition& transition : rule.transitions) text += transition_text(transition);
    if (rule.ambient) {
        const AmbientEffect& ambient = *rule.ambient;
        text += "  ambient period=" + duration_text(ambient.period) + " fps=" + number_text(ambient.frames_per_second) +
                " pose=" + duration_text(ambient.pose) + "\n";
    }
    for (const MaterialDeclaration& declaration : rule.materials) text += material_text(declaration);
    for (const RecipeDeclaration& declaration : rule.recipe_fields) text += recipe_text(declaration);
    if (rule.backdrop) text += backdrop_text(*rule.backdrop);
    for (const ScopedDefinition& definition : rule.scoped_definitions) {
        text += "  defines " + definition.name + " #" + std::to_string(definition.definition) + "\n";
    }
    for (const ScopedCondition& condition : rule.scoped_conditions) {
        const std::string which =
            condition.definition == custom_property_undefined ? "none" : "#" + std::to_string(condition.definition);
        text += "  when " + condition.name + " " + which + "\n";
    }
    if (!rule.reads.empty()) {
        text += "  reads";
        for (const std::string& name : rule.reads) text += " " + name;
        text += " at " + std::to_string(rule.source_position) + "\n";
    }
    return text;
}

std::string diagnostic_text(const theme_language::ThemeDiagnostic& diagnostic) {
    std::string text = theme_language::diagnostic_code_label(diagnostic.code) + " " +
                       std::string(theme_language::diagnostic_code_name(diagnostic.code)) + " " +
                       std::to_string(diagnostic.position.line.value) + ":" +
                       std::to_string(diagnostic.position.column.value);
    if (!diagnostic.rule.empty()) text += " rule=" + diagnostic.rule;
    if (!diagnostic.property.empty()) text += " property=" + diagnostic.property;
    if (!diagnostic.detail.empty()) text += " detail=" + diagnostic.detail;
    return text;
}

// -- fixtures -------------------------------------------------------------------

struct Fixture final {
    std::string name{};
    std::string text{};
    std::vector<std::string> forms{};
    theme_language::ThemeSheetForm form{theme_language::ThemeSheetForm::sheet};
    // `engine: newer`: the sheet declares the engine version after the
    // running one (R2-07).
    bool newer_engine{};
    // The codes its compilation reports, by label (`E409`).
    std::vector<std::string> codes{};
};

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::ostringstream contents{};
    contents << stream.rdbuf();
    return contents.str();
}

bool write_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(stream);
}

// The words after `key:` in the sheet's first comment, one or more lines.
void read_header(Fixture& fixture) {
    // A leading byte order mark comes before the comment.
    const std::size_t start = std::string_view(fixture.text).starts_with("\xEF\xBB\xBF") ? 3U : 0U;
    const std::string_view text = std::string_view(fixture.text).substr(start);
    const std::size_t open = text.find("/*");
    const std::size_t close = text.find("*/");
    if (open != 0U || close == std::string_view::npos) return;
    std::istringstream lines(std::string(text.substr(2U, close - 2U)));
    std::string line{};
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string key{};
        words >> key;
        if (key == "source:") {
            std::string value{};
            words >> value;
            if (value == "inline") fixture.form = theme_language::ThemeSheetForm::inline_style;
            continue;
        }
        if (key == "engine:") {
            std::string value{};
            words >> value;
            fixture.newer_engine = value == "newer";
            continue;
        }
        if (key != "forms:") continue;
        std::string form{};
        while (words >> form) fixture.forms.push_back(form);
    }
}

// The compilation of `fixture` as text: outcome, diagnostics and rules.
std::string compile_text(Fixture& fixture) {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    const std::string file = fixture.name + ".css";
    const theme_language::ThemeSource source{.file = file, .text = fixture.text};
    const EngineVersion newer{current_engine_version.major, static_cast<std::uint16_t>(current_engine_version.minor + 1U)};
    const bool inline_form = fixture.form == theme_language::ThemeSheetForm::inline_style;
    const std::span<const ControlAnatomy> vocabulary =
        inline_form ? std::span<const ControlAnatomy>(controls).first(1U) : std::span<const ControlAnatomy>(controls);
    const theme_language::ThemeCompileInput input{
        .source = source,
        .registry = registry,
        .vocabulary = theme_language::SelectorVocabulary{vocabulary},
        .published_values = inline_form ? std::span<const std::string>{} : std::span<const std::string>(published),
        .resources = inline_form ? std::span<const std::string_view>{} : std::span<const std::string_view>(resources),
        .images = inline_form ? std::span<const theme_language::ThemeImageResource>{}
                              : std::span<const theme_language::ThemeImageResource>(images),
        .requires_engine = fixture.newer_engine ? newer : current_engine_version,
        .form = fixture.form,
        .effects = &built_in_effect_registry()};
    ThemeRuleSheet sheet{};
    theme_language::ThemeCompiler compiler{};
    theme_language::ThemeDiagnosticList diagnostics{};
    const theme_language::CompileOutcome outcome = compiler.compile(input, sheet, diagnostics);
    const bool compiled = outcome == theme_language::CompileOutcome::compiled;
    std::string text = compiled ? "compiled\n" : "refused\n";
    for (const theme_language::ThemeDiagnostic& diagnostic : diagnostics.records()) {
        text += diagnostic_text(diagnostic) + "\n";
        fixture.codes.push_back(theme_language::diagnostic_code_label(diagnostic.code));
    }
    for (std::size_t index = 0U; index < sheet.rules.size(); ++index) text += rule_text(index, sheet.rules[index]);
    if (compiled) {
        const CompiledThemeRules engine = inline_form ? CompiledThemeRules::compile_inline(sheet, registry)
                                                      : CompiledThemeRules::compile(sheet, registry);
        for (const RuleDiagnostic& diagnostic : engine.diagnostics()) {
            std::cerr << "  engine: " << rule_diagnostic_name(diagnostic.kind) << " rule " << diagnostic.rule << " "
                      << diagnostic.label << " " << diagnostic.property << '\n';
        }
        require(engine.diagnostics().empty(), fixture.name + ": the engine accepts the compiled sheet");
    }
    return text;
}

// Whether the sheet's tokens and tree give back its bytes (PLAN 6, item 5);
// a sheet that does not parse has no tree to write.
void check_round_trip(const Fixture& fixture) {
    const theme_language::ThemeSource source{.file = fixture.name, .text = fixture.text};
    theme_language::ThemeTokenBuffer tokens{};
    theme_language::LineStartTable lines{};
    theme_language::ThemeSyntaxTree tree{};
    theme_language::ThemeDiagnosticList diagnostics{};
    if (fixture.text.size() > theme_language::maximum_sheet_bytes) return;
    const theme_language::TokenizeOutcome tokenized =
        theme_language::tokenize_theme_sheet(source, tokens, lines, diagnostics);
    if (tokenized != theme_language::TokenizeOutcome::tokens_cover_sheet) return;
    const theme_language::ParseOutcome parsed =
        theme_language::parse_theme_sheet(source, tokens, lines, tree, diagnostics, fixture.form);
    if (parsed != theme_language::ParseOutcome::tree_complete) return;
    std::string written{};
    const theme_language::WriteOutcome outcome =
        theme_language::write_theme_sheet(tree, tokens, fixture.text, written);
    require(outcome == theme_language::WriteOutcome::written && written == fixture.text,
            fixture.name + ": the sheet round-trips byte for byte");
}

// A 2 x 2 RGBA PNG, the package's one image resource.
const std::string green_png("\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00"
                            "\x00\x02\x00\x00\x00\x02\x08\x06\x00\x00\x00\x72\xb6\x0d\x24\x00\x00\x00\x0e"
                            "\x49\x44\x41\x54\x78\x9c\x63\x60\xf8\x0f\x85\x30\x06\x00\x43\xce\x07\xf9\xf7"
                            "\xa2\xe2\xef\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82",
                            71U);

std::string hex_of(std::string_view bytes) {
    return theme_language::package_sha256_hex(theme_language::package_sha256(bytes));
}

// A package of `sheet` holding the PNG as `icon.check`.
ThemePackage package_of(std::string_view name, std::string_view sheet) {
    ThemePackage package{};
    package.manifest = "{\"format\": \"gui-forms-theme/1\", \"name\": \"" + std::string(name) +
                       "\", \"version\": \"1.0.0\", \"engine\": \">=0.3\", \"sheet\": {\"path\": "
                       "\"theme.theme.css\", \"sha256\": \"" +
                       hex_of(sheet) +
                       "\"}, \"resources\": [{\"name\": \"icon.check\", \"path\": \"check.png\", \"sha256\": \"" +
                       hex_of(green_png) + "\"}]}";
    package.files.push_back(ThemePackageFile{"check.png", green_png});
    package.files.push_back(ThemePackageFile{"theme.theme.css", std::string(sheet)});
    return package;
}

// A refused sheet, compiled as a package holding an image beside a running
// theme that holds the same image, is refused there too and leaves nothing
// behind (PLAN 6, item 2): the running theme keeps its image and the
// registry holds no image of the refused compilation.
void check_refusal_leaves_nothing(const Fixture& fixture) {
    ImageRegistry registry{};
    ThemeLibrary library{};
    ThemeCompileOptions options{};
    options.controls = controls;
    options.published_values = published;
    // Onto windows-professional, as the fixture was written (the default
    // base is gui-forms, T4 phase 6).
    options.base = ThemeBase::windows_professional;
    // The registry the direct compile checks effect names against (EP-3),
    // so an unknown effect (E417) is refused as a package too.
    options.effects = &built_in_effect_registry();
    const ThemePackage running_package =
        package_of("running", "button::part(face) { background-image: url(\"resource:icon.check\") }");
    const ThemeCompileResult running = compile_theme_package(running_package, library, registry, options);
    require(running.succeeded(), "the running theme compiles");
    const std::size_t images_before = registry.image_ids().size();
    const ThemeCompileResult result = compile_theme_package(package_of("refused", fixture.text), library, registry, options);
    require(!result.succeeded() && result.theme() == nullptr, fixture.name + ": refused as a package too");
    require(running.theme() != nullptr && registry.image_ids().size() == images_before,
            fixture.name + ": the running theme and its image are untouched, and nothing is left behind");
}

struct CoverageRow final {
    std::string form{};
    std::string fixture{};
};

std::vector<CoverageRow> read_coverage(const std::filesystem::path& path) {
    std::vector<CoverageRow> rows{};
    const std::optional<std::string> text = read_file(path);
    require(text.has_value(), "the coverage table is readable");
    if (!text) return rows;
    std::istringstream lines(*text);
    std::string line{};
    while (std::getline(lines, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::size_t tab = line.find('\t');
        CoverageRow row{};
        row.form = line.substr(0U, tab);
        if (tab != std::string::npos) row.fixture = line.substr(tab + 1U);
        rows.push_back(row);
    }
    return rows;
}

const Fixture* find_fixture(std::span<const Fixture> fixtures, std::string_view name) {
    for (const Fixture& fixture : fixtures) {
        if (fixture.name == name) return &fixture;
    }
    return nullptr;
}

bool contains(std::span<const std::string> values, std::string_view value) {
    for (const std::string& one : values) {
        if (one == value) return true;
    }
    return false;
}

// Every form row names a fixture listing it; every listed form has a row.
// `code-elsewhere <label>` rows name the test that reports a code no sheet
// alone can (a manifest's, a package's, an inline style's at a window).
void check_coverage(std::span<const CoverageRow> rows, std::span<const Fixture> fixtures) {
    std::vector<std::string> forms{};
    std::vector<std::string> codes_elsewhere{};
    for (const CoverageRow& row : rows) {
        if (row.form.starts_with("code-elsewhere ")) {
            codes_elsewhere.push_back(row.form.substr(15U));
            require(!row.fixture.empty(), row.form + ": names the test reporting it");
            continue;
        }
        forms.push_back(row.form);
        const Fixture* const fixture = find_fixture(fixtures, row.fixture);
        require(!row.fixture.empty(), row.form + ": a form with no fixture");
        if (row.fixture.empty()) continue;
        require(fixture != nullptr, row.form + ": its fixture " + row.fixture + " exists");
        if (fixture == nullptr) continue;
        require(contains((*fixture).forms, row.form), row.form + ": its fixture " + row.fixture + " lists it");
    }
    for (const Fixture& fixture : fixtures) {
        for (const std::string& form : fixture.forms) {
            require(contains(forms, form), fixture.name + ": its form " + form + " is in the coverage table");
        }
    }
    // Every code, by number, is reported by a fixture or placed elsewhere.
    for (std::uint32_t number = 1U; number < 2000U; ++number) {
        const theme_language::DiagnosticCode code = static_cast<theme_language::DiagnosticCode>(number);
        const std::string_view name = theme_language::diagnostic_code_name(code);
        if (name.empty() || name == "unknown") continue;
        const std::string label = theme_language::diagnostic_code_label(code);
        bool reported = contains(codes_elsewhere, label);
        for (const Fixture& fixture : fixtures) reported = reported || contains(fixture.codes, label);
        require(reported, label + " " + std::string(name) + ": a fixture reports it");
    }
}

struct FixtureNameLess final {
    bool operator()(const Fixture& left, const Fixture& right) const noexcept { return left.name < right.name; }
};

} // namespace

int main() {
    const std::filesystem::path directory(GUI_FORMS_THEME_CONFORMANCE_DIR);
    const char* const write_flag = std::getenv("GUI_FORMS_THEME_CONFORMANCE_WRITE");
    const bool writing = write_flag != nullptr && std::string_view(write_flag) == "1";
    std::vector<Fixture> fixtures{};
    std::error_code error{};
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".css") continue;
        const std::optional<std::string> text = read_file(entry.path());
        require(text.has_value(), entry.path().string() + ": readable");
        if (!text) continue;
        Fixture fixture{};
        fixture.name = entry.path().stem().string();
        fixture.text = *text;
        read_header(fixture);
        require(!fixture.forms.empty(), fixture.name + ": names the forms it covers");
        fixtures.push_back(std::move(fixture));
    }
    require(!error, "the fixture directory is readable");
    std::sort(fixtures.begin(), fixtures.end(), FixtureNameLess{});
    std::size_t written = 0U;
    for (Fixture& fixture : fixtures) {
        const std::string actual = compile_text(fixture);
        check_round_trip(fixture);
        const bool refused = actual.starts_with("refused");
        if (refused && fixture.form == theme_language::ThemeSheetForm::sheet) check_refusal_leaves_nothing(fixture);
        const std::filesystem::path expected_path = directory / (fixture.name + ".expected");
        if (writing) {
            require(write_file(expected_path, actual), fixture.name + ": the expected file is written");
            written = written + 1U;
            continue;
        }
        const std::optional<std::string> expected = read_file(expected_path);
        require(expected.has_value(), fixture.name + ": has an expected file");
        if (!expected) continue;
        if (*expected != actual) {
            std::cerr << "FAIL: " << fixture.name << ": the compilation differs from " << fixture.name
                      << ".expected; it gives:\n"
                      << actual;
            ++failures;
        }
    }
    const std::vector<CoverageRow> rows = read_coverage(directory / "COVERAGE.tsv");
    check_coverage(rows, fixtures);
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme conformance tests passed: " << fixtures.size() << " fixtures, " << rows.size()
              << " coverage rows";
    if (writing) std::cout << ", " << written << " expected files written";
    std::cout << '\n';
    return 0;
}
