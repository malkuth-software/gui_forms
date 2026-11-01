#pragma once

#include "../source_text/theme_source_text.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::theme_language {

// What the theme compiler reports (PLAN.md section 2). An error refuses the
// whole theme, or the whole inline style, and the previously active one
// stays; a warning drops one declaration or rule and the rest applies (T3
// decisions B2 and B3).
enum class DiagnosticSeverity : std::uint8_t { error, warning };

// Each code's number is its stable record; its class is its hundreds digit
// (PLAN.md section 2): E1xx lexical, E2xx structure, E3xx selectors, E4xx
// values, E5xx limits, E7xx reserved names, W1xx unknown names. Later
// phases add the other classes. An error's enumerator value is its number; a warning's is 1000 plus its number, so W104 and
// E104 stay distinct values.
enum class DiagnosticCode : std::uint16_t {
    // E1xx lexical.
    invalid_utf8 = 101,
    nul_byte = 102,
    unterminated_string = 103,
    unterminated_comment = 104,
    // A backslash outside a string (round 002, item 16).
    escape_not_admitted = 105,
    // `url(` not followed by a quoted string.
    bad_url = 106,
    // `<!--` or `-->`.
    token_not_admitted = 107,
    // A number with more than maximum_significant_digits significant digits.
    number_too_precise = 108,
    // A number that is not zero and lies outside the admitted magnitudes.
    number_out_of_range = 109,
    // E2xx structure: the first one found stops the parse (B2).
    // A token where the sheet's structure admits none of its kind.
    unexpected_token = 201,
    // A `{`, `(`, `[` or function never closed, or a closer with no opener
    // or the wrong one.
    unbalanced_block = 202,
    // A declaration's `;` outside every rule's block.
    declaration_outside_rule = 203,
    // An `@import` after another item.
    import_after_rules = 204,
    important_in_keyframes = 205,
    // A rule or at-rule inside a rule's block, or an at-rule inside an
    // at-rule's block (B1).
    nesting_not_admitted = 206,
    // A `@theme` header (T3 user themes) after another item, or a second
    // one: the header opens the sheet.
    theme_header_misplaced = 207,
    // E3xx selectors (B1).
    // A selector that is not one compound selector of the admitted forms.
    selector_syntax = 301,
    // A descendant (whitespace), `>`, `+` or `~` combinator, or nesting `&`.
    combinator_not_admitted = 302,
    // A CSS pseudo-class or pseudo-element outside the language: `:not()`,
    // `:where()`, `:has()`, the structural ones, `::before` and the rest.
    pseudo_class_not_admitted = 303,
    attribute_selector_not_admitted = 304,
    // E4xx values (B2: a bad value fails). 412 and 414
    // (transition_invalid, variable_kind_mismatch) are kept for later
    // phases: the motion lowering reports a transition the engine cannot
    // run as E405 or E403.
    // A value whose tokens are not a component of the admitted forms.
    value_syntax = 401,
    // An operand or argument of the wrong kind: units that do not agree, a
    // colour where a number is needed.
    value_wrong_kind = 402,
    // A number outside the range its position admits.
    value_out_of_range = 403,
    // A unit outside the language: `rem`, `vw`, `ch` and the rest.
    unit_not_admitted = 404,
    // A CSS form outside the language, such as `hwb()`, `sin()`, `attr()`
    // or `color-mix()` in a space other than srgb.
    value_form_not_admitted = 405,
    // A keyword the property's position does not list, such as an unknown
    // choice of a primitive parameter (raised from P5, the lowering).
    keyword_unknown = 406,
    // More layers than a material holds (SurfaceMaterial's
    // maximum_fill_layers), from P6b.
    too_many_layers = 407,
    // An expression the engine's compiler refuses (other than its budget).
    expression_invalid = 408,
    // A var() of a name no rule defines, with no fallback.
    variable_undefined = 409,
    variable_cycle = 410,
    // An `animation` naming keyframes no `@keyframes` rule defines (P7b).
    keyframes_undefined = 411,
    // A `url("resource:<name>")` the theme's resources do not list.
    resource_unknown = 413,
    // role() of a colour role the theme's palette does not declare (P8,
    // owner decision D2 of 2026-10-28).
    colour_role_undefined = 415,
    // A var() of a custom property defined in more than one place, read
    // where it cannot vary (P12): in `@keyframes` or in the palette.
    variable_cannot_vary = 416,
    // E5xx limits (T3 decision E2).
    sheet_too_large = 501,
    too_many_rules = 502,
    too_many_declarations = 503,
    nesting_too_deep = 504,
    string_too_long = 505,
    too_many_imports = 506,
    // An expression whose program has more than
    // maximum_expression_operations instructions or needs a deeper stack
    // than maximum_expression_stack.
    expression_over_budget = 507,
    // A rule reading custom properties defined in more than one place whose
    // definitions combine more than maximum_variable_combinations ways
    // (P12, PLAN.md section 1.11).
    too_many_variable_combinations = 508,
    // The derived limits of PLAN.md section 6:
    // more than maximum_is_arguments arguments in one `:is()`.
    too_many_is_arguments = 509,
    // An expanded selector of more than maximum_simple_selectors simple
    // selectors.
    selector_too_long = 510,
    // More than maximum_custom_properties distinct custom property names.
    too_many_custom_properties = 511,
    // A value of more than maximum_substituted_tokens tokens once its var()s
    // are substituted.
    substitution_too_long = 512,
    // A sheet whose lowered records would take more than
    // maximum_lowered_bytes (the selector lists and their copies of each
    // rule's declarations, PLAN.md P5).
    lowered_sheet_too_large = 513,
    // More than maximum_keyframes_rules `@keyframes` rules in one sheet
    // (R2-14, P7b).
    too_many_keyframes = 514,
    // More than maximum_scoped_custom_properties names that some rule's
    // selector redefines read in one sheet (P12, PLAN.md section 1.11).
    too_many_scoped_variables = 515,
    // E6xx package (P10, owner decisions of 2026-10-28 on theme.json): the
    // theme is refused, and the running one stays.
    // A manifest that is not JSON, or not the schema: a key missing, of
    // the wrong kind, ill-formed or unknown.
    manifest_invalid = 601,
    // A package file, the sheet or a resource, whose bytes do not have the
    // hash the manifest gives them; the detail is its path (owner decision
    // 2a of 2026-10-28).
    file_hash_mismatch = 602,
    // An `@import` of a theme that is not installed, or that the manifest's
    // `imports` does not list.
    import_not_installed = 603,
    // An installed theme whose manifest's hash is not the one the
    // importing manifest pins, or whose own files fail their hashes.
    import_hash_mismatch = 604,
    // A theme that imports itself, through any chain of imports.
    import_cycle = 605,
    // A published value the manifest lists that is ill-formed, listed
    // twice, or that the application does not publish.
    published_value_invalid = 606,
    // A manifest whose engine bound the running engine does not meet; the
    // detail is the version needed (C8).
    engine_too_old = 607,
    // E7xx reserved names: a theme that defines an engine input or a
    // published value (R2-06).
    reserved_input_set = 701,
    // E999 is temporary: a form a later phase gives its meaning, refused
    // until then so that no interim meaning ships (PLAN.md P4: a custom
    // property with several definitions, until P12; P5: `!important`, the
    // at-rules, and the properties of P6 to P8).
    not_yet_built = 999,
    // W1xx unknown names (B3): the item is dropped and the rest applies.
    // A property name the language does not know; the declaration is
    // dropped.
    unknown_property = 1101,
    // A `primitive:` the registry does not have; its parameters are dropped
    // with it.
    unknown_primitive = 1102,
    // A function the language does not know, as a property's value.
    unknown_function = 1103,
    // An at-rule other than @import, @media and @keyframes; its block is
    // skipped.
    unknown_at_rule = 1104,
    // An `@media` feature the language does not know (P8): its query is
    // skipped, and with it a block whose only query it is.
    unknown_media_feature = 1105,
    // A control type, part, role, state or pseudo-class the vocabulary does
    // not know; the rule is dropped.
    unknown_selector_name = 1106,
    // A known CSS property the language leaves out, such as `float`,
    // `grid-template` or `aspect-ratio`; the declaration is dropped, with
    // the property named.
    property_not_admitted = 1107,
    // A registered primitive the engine's budgets refuse (R2-08): one
    // declaring more than maximum_operations_per_device_pixel operations
    // per device pixel, or declaring no bound.
    primitive_over_budget = 1108,
    primitive_cost_unbounded = 1109,
    // W3xx versions (R2-07): the sheet declares a newer engine than the
    // running one, reported once; and, in such a sheet, a keyword the
    // running engine lacks, whose declaration is dropped.
    needs_newer_engine = 1301,
    keyword_from_newer_engine = 1302,
    // W2xx colour (P8, A4): a literal colour in a text or edge position
    // (`color`, the border colours, keylines and the outline), which
    // forced high contrast can only replace by its position. The
    // declaration still applies.
    literal_colour_unremappable = 1201,
    // W4xx inline (P9, E1): a window drawing a control's inline style
    // under a theme with rules of its own, reported once per window; the
    // file names the control (`inline:<control>`).
    inline_with_theme = 1401,
};

[[nodiscard]] DiagnosticSeverity diagnostic_severity(DiagnosticCode code) noexcept;
// The code's snake_case name, such as `invalid_utf8`.
[[nodiscard]] std::string_view diagnostic_code_name(DiagnosticCode code) noexcept;
// The code as diagnostics print it: its severity letter and number, such as
// `E101`.
[[nodiscard]] std::string diagnostic_code_label(DiagnosticCode code);

// One diagnostic. `file` is the sheet's name (ThemeSource::file); `rule` is
// the rule's label and `property` the declaration's property, each empty
// where there is none (always in lexical diagnostics); `detail` is one
// argument, such as the limit exceeded.
struct ThemeDiagnostic final {
    DiagnosticCode code{DiagnosticCode::invalid_utf8};
    std::string file{};
    SourcePosition position{};
    std::string rule{};
    std::string property{};
    std::string detail{};

    friend bool operator==(const ThemeDiagnostic& left, const ThemeDiagnostic& right) noexcept {
        return left.code == right.code && left.file == right.file && left.position == right.position &&
               left.rule == right.rule && left.property == right.property && left.detail == right.detail;
    }
};

// The most diagnostic records one compilation keeps (a derived limit, T3
// round 002 item 14); the rest are counted.
inline constexpr std::size_t maximum_diagnostic_records = 1000U;

// A compilation's diagnostics, in the order found. Caller-owned and reused:
// clear() ends the records' lifetimes and keeps the capacity.
class ThemeDiagnosticList final {
public:
    // Keeps the record if fewer than maximum_diagnostic_records are kept,
    // and counts it either way.
    void add(ThemeDiagnostic diagnostic);
    // Gives each kept record from position `first_record` on the rule and
    // the property, where it names none: the lowering names them for the
    // diagnostics of the stages it calls.
    void name_context(std::size_t first_record, std::string_view rule, std::string_view property);
    // While muted, a warning is neither kept nor counted: the lowering
    // mutes the warnings of a block it lowers again (a second media query,
    // a palette variant's copy of a rule), which its first lowering
    // already reported. Errors are kept as ever. clear() unmutes.
    void mute_warnings(bool muted) noexcept { warnings_muted_ = muted; }
    [[nodiscard]] bool warnings_muted() const noexcept { return warnings_muted_; }
    void clear() noexcept;

    [[nodiscard]] std::span<const ThemeDiagnostic> records() const& noexcept { return records_; }
    std::span<const ThemeDiagnostic> records() const&& = delete;
    [[nodiscard]] std::size_t error_count() const noexcept { return errors_; }
    [[nodiscard]] std::size_t warning_count() const noexcept { return warnings_; }
    // Records counted but not kept.
    [[nodiscard]] std::size_t dropped_count() const noexcept { return dropped_; }

private:
    std::vector<ThemeDiagnostic> records_{};
    std::size_t errors_{};
    std::size_t warnings_{};
    std::size_t dropped_{};
    bool warnings_muted_{};
};

// The diagnostic's log line, without a line break:
//   gui-forms-theme=error|code:E101 invalid_utf8|file:a.theme.css|line:3|column:7|rule:|property:|detail:
// In the file, rule, property and detail fields, '%', '|' and the bytes
// below 0x20 and 0x7F are written as '%' and two uppercase hex digits, so a
// line holds exactly the fields it names.
[[nodiscard]] std::string theme_diagnostic_log_line(const ThemeDiagnostic& diagnostic);

// The longest detail a diagnostic copies from a sheet.
inline constexpr std::size_t maximum_detail_bytes = 64U;

// At most maximum_detail_bytes of `text`, cut before a UTF-8 continuation
// byte so the detail stays whole scalar values.
[[nodiscard]] std::string clipped_theme_detail(std::string_view text);

// Writes each kept record's log line to `stream` (the library passes
// stderr), then, when records were dropped, one line counting them:
//   gui-forms-theme=dropped|count:<n>
void write_theme_diagnostics(const ThemeDiagnosticList& diagnostics, std::FILE* stream);

} // namespace gui_forms::theme_language
