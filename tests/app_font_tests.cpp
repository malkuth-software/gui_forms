// Application-loaded font families (owner decision of 2026-11-15, "App-loaded fonts: inside
// composites and subsurfaces"; gui_forms/typography/app_fonts/app_fonts.hpp):
// - validation: each stated table check refuses the face it names, with its table;
// - loading: the name and face rules, the family's number, state and lookup by name;
// - a composite's look names the family and its child draws it; an application theme naming
//   it gets the unavailable warning (W113) and a stock control drops it (refusal counted);
// - quarantine: a face that passes the checks but the font reader cannot open quarantines its
//   family, and the text draws with the role's faces;
// - unload while in use: deferred while an engine holds the bytes, released when it goes.
//
// The process registry is shared by every case: each case loads families under its own names.

#include "../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../src/core/resources/fonts/program_typefaces.hpp"
#include "../src/render/text/harfbuzz/harfbuzz_font_engine.hpp"
#include "gui_forms/composites/composite_control/composite_control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/typography/app_fonts/app_fonts.hpp"
#include "gui_forms/typography/font_families/font_families.hpp"
#include "gui_forms/window/window.hpp"

#include <stxlib/glyph_face.hpp>
#include <stxlib/view.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef GUI_FORMS_TEST_CARLITO_REGULAR
#error "GUI_FORMS_TEST_CARLITO_REGULAR names assets/fonts/Carlito-Regular.ttf"
#endif

namespace {

using namespace gui_forms;
using render::text::FontFaceSource;
using render::text::HarfBuzzFontEngine;
using render::text::ShapedFontRun;
using render::text::ShapedText;

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::shared_ptr<const std::vector<std::byte>> read_font(const char* path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> read{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(read.size());
    for (std::size_t index = 0U; index < read.size(); ++index) bytes[index] = static_cast<std::byte>(read[index]);
    std::shared_ptr<const std::vector<std::byte>> shared =
        std::make_shared<const std::vector<std::byte>>(std::move(bytes));
    return shared;
}

constexpr std::uint32_t tag_of(const std::string_view tag) noexcept {
    const std::uint32_t value = (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[0])) << 24U) |
                                (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[1])) << 16U) |
                                (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[2])) << 8U) |
                                static_cast<std::uint32_t>(static_cast<unsigned char>(tag[3]));
    return value;
}

// -- Synthetic faces --------------------------------------------------------

// One table of a synthetic face: its tag and bytes.
struct SyntheticTable final {
    std::uint32_t tag{};
    std::vector<std::byte> bytes{};
};

void put16(std::vector<std::byte>& bytes, const std::size_t at, const std::uint32_t value) {
    bytes[at] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    bytes[at + 1U] = static_cast<std::byte>(value & 0xFFU);
}

void put32(std::vector<std::byte>& bytes, const std::size_t at, const std::uint32_t value) {
    put16(bytes, at, value >> 16U);
    put16(bytes, at + 2U, value & 0xFFFFU);
}

SyntheticTable table(const std::string_view tag, const std::size_t length) {
    SyntheticTable made{};
    made.tag = tag_of(tag);
    made.bytes.assign(length, std::byte{0});
    return made;
}

// The tables of a minimal TrueType face: one empty glyph, a format 4 cmap holding only the
// closing segment, and the other required tables. `glyph_count` glyphs, all empty.
std::vector<SyntheticTable> minimal_tables(const std::uint32_t glyph_count = 1U) {
    std::vector<SyntheticTable> tables{};
    SyntheticTable cmap = table("cmap", 36U);
    put16(cmap.bytes, 2U, 1U);
    put16(cmap.bytes, 4U, 3U);
    put16(cmap.bytes, 6U, 1U);
    put32(cmap.bytes, 8U, 12U);
    put16(cmap.bytes, 12U, 4U);
    put16(cmap.bytes, 14U, 24U);
    put16(cmap.bytes, 18U, 2U);
    put16(cmap.bytes, 20U, 2U);
    put16(cmap.bytes, 26U, 0xFFFFU);
    put16(cmap.bytes, 30U, 0xFFFFU);
    put16(cmap.bytes, 32U, 1U);
    tables.push_back(std::move(cmap));
    tables.push_back(table("glyf", 0U));
    SyntheticTable head = table("head", 54U);
    put32(head.bytes, 0U, 0x00010000U);
    put32(head.bytes, 12U, 0x5F0F3CF5U);
    put16(head.bytes, 18U, 1000U);
    tables.push_back(std::move(head));
    SyntheticTable hhea = table("hhea", 36U);
    put32(hhea.bytes, 0U, 0x00010000U);
    put16(hhea.bytes, 4U, 800U);
    put16(hhea.bytes, 6U, 0xFF38U);
    put16(hhea.bytes, 34U, 1U);
    tables.push_back(std::move(hhea));
    SyntheticTable hmtx = table("hmtx", 4U + 2U * (glyph_count - 1U));
    put16(hmtx.bytes, 0U, 500U);
    tables.push_back(std::move(hmtx));
    tables.push_back(table("loca", 2U * (glyph_count + 1U)));
    SyntheticTable maxp = table("maxp", 6U);
    put32(maxp.bytes, 0U, 0x00005000U);
    put16(maxp.bytes, 4U, glyph_count);
    tables.push_back(std::move(maxp));
    SyntheticTable name = table("name", 6U);
    put16(name.bytes, 4U, 6U);
    tables.push_back(std::move(name));
    SyntheticTable post = table("post", 32U);
    put32(post.bytes, 0U, 0x00030000U);
    tables.push_back(std::move(post));
    return tables;
}

// The face `tables` make under `version`: the 12-byte header, the records in the given order
// and each table at a 4-byte aligned offset.
std::vector<std::byte> assemble(const std::uint32_t version, const std::vector<SyntheticTable>& tables) {
    std::size_t size = 12U + 16U * tables.size();
    for (const SyntheticTable& each : tables) size += (each.bytes.size() + 3U) & ~std::size_t{3U};
    std::vector<std::byte> face(size, std::byte{0});
    put32(face, 0U, version);
    put16(face, 4U, static_cast<std::uint32_t>(tables.size()));
    std::size_t offset = 12U + 16U * tables.size();
    for (std::size_t index = 0U; index < tables.size(); ++index) {
        const SyntheticTable& each = tables[index];
        const std::size_t record = 12U + 16U * index;
        put32(face, record, each.tag);
        put32(face, record + 8U, static_cast<std::uint32_t>(offset));
        put32(face, record + 12U, static_cast<std::uint32_t>(each.bytes.size()));
        for (std::size_t at = 0U; at < each.bytes.size(); ++at) face[offset + at] = each.bytes[at];
        offset += (each.bytes.size() + 3U) & ~std::size_t{3U};
    }
    return face;
}

constexpr std::uint32_t truetype = 0x00010000U;

// The table `tag` of `tables`.
SyntheticTable& find_table(std::vector<SyntheticTable>& tables, const std::string_view tag) {
    for (SyntheticTable& each : tables) {
        if (each.tag == tag_of(tag)) return each;
    }
    return tables.front();
}

AppFontValidation check(const std::vector<std::byte>& face) {
    const AppFontValidation result = validate_app_font(std::span<const std::byte>(face.data(), face.size()));
    return result;
}

bool refused_with(const AppFontValidation result, const AppFontStatus status, const std::string_view tag = {}) {
    const std::uint32_t expected = tag.empty() ? 0U : tag_of(tag);
    const bool refused = result.status == status && result.table == expected;
    return refused;
}

AppFontFace face_of(std::shared_ptr<const std::vector<std::byte>> bytes, const std::uint16_t weight = 400U) {
    AppFontFace face{};
    face.bytes = std::move(bytes);
    face.weight = weight;
    return face;
}

// -- Validation -------------------------------------------------------------

void validation_states_each_check() {
    require(check(assemble(truetype, minimal_tables())).status == AppFontStatus::loaded,
            "the minimal TrueType face passes every check");
    const std::shared_ptr<const std::vector<std::byte>> carlito = read_font(GUI_FORMS_TEST_CARLITO_REGULAR);
    require(check(*carlito).status == AppFontStatus::loaded, "a bundled face passes every check");

    require(refused_with(check({}), AppFontStatus::empty), "check 1: empty bytes");
    require(refused_with(check(std::vector<std::byte>(8U, std::byte{0})), AppFontStatus::not_sfnt),
            "check 1: shorter than the header");
    require(refused_with(check(assemble(tag_of("ttcf"), minimal_tables())), AppFontStatus::collection_refused),
            "check 2: a collection is refused");
    require(refused_with(check(assemble(tag_of("wOFF"), minimal_tables())), AppFontStatus::not_sfnt),
            "check 2: another version is no sfnt");

    std::vector<std::byte> no_tables = assemble(truetype, minimal_tables());
    put16(no_tables, 4U, 0U);
    require(refused_with(check(no_tables), AppFontStatus::table_directory_invalid), "check 3: no tables");
    std::vector<std::byte> outside = assemble(truetype, minimal_tables());
    put32(outside, 12U + 12U, 0x00100000U);
    require(refused_with(check(outside), AppFontStatus::table_directory_invalid, "cmap"),
            "check 3: a table past the bytes");
    std::vector<SyntheticTable> twice = minimal_tables();
    twice.push_back(table("post", 32U));
    require(refused_with(check(assemble(truetype, twice)), AppFontStatus::table_directory_invalid, "post"),
            "check 3: a tag listed twice");

    std::vector<SyntheticTable> no_cmap = minimal_tables();
    find_table(no_cmap, "cmap").tag = tag_of("cmaq");
    require(refused_with(check(assemble(truetype, no_cmap)), AppFontStatus::table_missing, "cmap"),
            "check 4: cmap missing");
    std::vector<SyntheticTable> no_loca = minimal_tables();
    find_table(no_loca, "loca").tag = tag_of("locb");
    require(refused_with(check(assemble(truetype, no_loca)), AppFontStatus::table_missing, "loca"),
            "check 4: TrueType outlines need loca");
    require(refused_with(check(assemble(tag_of("OTTO"), minimal_tables())), AppFontStatus::table_missing, "CFF "),
            "check 4: CFF outlines need CFF or CFF2");

    std::vector<SyntheticTable> bad_magic = minimal_tables();
    put32(find_table(bad_magic, "head").bytes, 12U, 0x12345678U);
    require(refused_with(check(assemble(truetype, bad_magic)), AppFontStatus::table_invalid, "head"),
            "check 5: head's magic number");
    std::vector<SyntheticTable> bad_units = minimal_tables();
    put16(find_table(bad_units, "head").bytes, 18U, 8U);
    require(refused_with(check(assemble(truetype, bad_units)), AppFontStatus::table_invalid, "head"),
            "check 5: unitsPerEm below 16");
    std::vector<SyntheticTable> no_glyphs = minimal_tables();
    put16(find_table(no_glyphs, "maxp").bytes, 4U, 0U);
    require(refused_with(check(assemble(truetype, no_glyphs)), AppFontStatus::table_invalid, "maxp"),
            "check 6: no glyphs");
    std::vector<SyntheticTable> no_metrics = minimal_tables();
    put16(find_table(no_metrics, "hhea").bytes, 34U, 0U);
    require(refused_with(check(assemble(truetype, no_metrics)), AppFontStatus::table_invalid, "hhea"),
            "check 7: no horizontal metrics");
    std::vector<SyntheticTable> short_hmtx = minimal_tables(3U);
    find_table(short_hmtx, "hmtx").bytes.resize(4U);
    require(refused_with(check(assemble(truetype, short_hmtx)), AppFontStatus::table_invalid, "hmtx"),
            "check 8: hmtx shorter than its metrics");
    std::vector<SyntheticTable> descending = minimal_tables(2U);
    put16(find_table(descending, "loca").bytes, 2U, 4U);
    require(refused_with(check(assemble(truetype, descending)), AppFontStatus::table_invalid, "loca"),
            "check 9: descending loca offsets");
    std::vector<SyntheticTable> past_glyf = minimal_tables();
    put16(find_table(past_glyf, "loca").bytes, 2U, 2U);
    require(refused_with(check(assemble(truetype, past_glyf)), AppFontStatus::table_invalid, "loca"),
            "check 9: loca past glyf");
    std::vector<SyntheticTable> mac_only = minimal_tables();
    put16(find_table(mac_only, "cmap").bytes, 4U, 1U);
    put16(find_table(mac_only, "cmap").bytes, 6U, 0U);
    require(refused_with(check(assemble(truetype, mac_only)), AppFontStatus::table_invalid, "cmap"),
            "check 10: no Unicode subtable");
}

// -- Loading ----------------------------------------------------------------

void loading_rules_and_lookup() {
    const std::shared_ptr<const std::vector<std::byte>> carlito = read_font(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::array<AppFontFace, 1> faces{face_of(carlito)};
    const AppFontStatistics before = app_font_statistics();

    require(load_app_font_family("Carlito", faces).validation.status == AppFontStatus::name_taken,
            "a bundled family's name is taken");
    require(load_app_font_family("serif", faces).validation.status == AppFontStatus::name_taken,
            "a CSS generic's name is taken");
    require(load_app_font_family("", faces).validation.status == AppFontStatus::name_invalid, "an empty name");
    require(load_app_font_family("A, B", faces).validation.status == AppFontStatus::name_invalid,
            "a name a font-family list cannot hold");
    require(load_app_font_family(std::string(65U, 'x'), faces).validation.status == AppFontStatus::name_invalid,
            "an over-long name");
    require(load_app_font_family("Load Case", {}).validation.status == AppFontStatus::no_faces, "no faces");
    const std::array<AppFontFace, 5> five{face_of(carlito), face_of(carlito), face_of(carlito), face_of(carlito),
                                          face_of(carlito)};
    require(load_app_font_family("Load Case", five).validation.status == AppFontStatus::too_many_faces,
            "more than the most faces");
    const std::array<AppFontFace, 1> weightless{face_of(carlito, 0U)};
    require(load_app_font_family("Load Case", weightless).validation.status == AppFontStatus::weight_invalid,
            "a weight of zero");
    const std::array<AppFontFace, 2> second_broken{face_of(carlito), face_of(nullptr)};
    const AppFontLoad broken = load_app_font_family("Load Case", second_broken);
    require(broken.validation.status == AppFontStatus::empty && broken.face_index == 1U &&
                broken.family.id().value == 0U,
            "a refused face names its index and registers nothing");

    AppFontLoad loaded = load_app_font_family("Load Case", faces);
    const FontFamilyId family = loaded.family.id();
    require(loaded.validation.status == AppFontStatus::loaded && app_font_family_number(family),
            "a family loads with a number above the bundled families");
    require(app_font_family_state(family) == AppFontState::live && font_family_drawable(family) &&
                known_font_family(family),
            "a loaded family is live, drawable and known");
    require(app_font_family_named("load case") == family, "a family is found by name without case");
    require(load_app_font_family("LOAD CASE", faces).validation.status == AppFontStatus::name_taken,
            "a loaded family's name is taken");
    {
        // Scoped: a lent owner, like an engine's, would defer the unload below.
        std::array<AppFontFaceSource, maximum_app_font_faces> sources{};
        require(app_font_family_faces(family, sources) == 1U && sources[0].bytes.size() == (*carlito).size() &&
                    sources[0].owner != nullptr,
                "a live family lends its faces with their owner");
    }
    const std::array<FontFamilyId, 2> listed{family, carlito_font_family};
    const FontFamilyStackId stack = register_font_family_stack(listed);
    require(stack.value != 0U, "a stack may list a live application family");

    const AppFontStatistics after = app_font_statistics();
    require(after.loads == before.loads + 1U && after.refused_loads >= before.refused_loads + 9U,
            "loads and refusals are counted");
    require(loaded.family.unload() == AppFontUnload::released, "an unused family is released at once");
    require(app_font_family_state(family) == AppFontState::released && !font_family_drawable(family) &&
                known_font_family(family),
            "a released family draws nothing and its number stays known");
    require(app_font_family_named("Load Case").value == 0U, "an unloaded family is not found by name");
    FontSpec font{};
    font.family = family;
    font.stack = stack;
    const FontFamilyOrder order = font_family_order(font);
    require(order.count == 1U && order.families[0] == carlito_font_family,
            "an unloaded family leaves every font's order");
    AppFontLoad reloaded = load_app_font_family("Load Case", faces);
    require(reloaded.family.id().value != 0U && !(reloaded.family.id() == family),
            "the name is free again and the number is not reused");
}

// -- Where a family draws ---------------------------------------------------

// A composite whose look sets its button child's font family to an application family. A
// <look> rule's first compound names the composite itself (W920 control_look_foreign_type), so
// the child is reached through it.
constexpr std::string_view app_font_pair = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<control xmlns="urn:gui-forms:control:1" name="app-font-pair" version="1.0" base="user-control" family="panel">
  <part name="face" role="face" box="inset(region, 0)"/>
  <children>
    <button id="@action" text="Go"/>
  </children>
  <focus target="none"/>
  <look>
    app-font-pair > button { font-family: "App Look Face"; }
  </look>
</control>
)xml";

constexpr std::array<CompositeChildType, 1> pair_child_types{
    composite_child_type<Button>("button"),
};

void composite_look_draws_family() {
    const std::shared_ptr<const std::vector<std::byte>> carlito = read_font(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::array<AppFontFace, 1> faces{face_of(carlito)};
    const AppFontLoad loaded = load_app_font_family("App Look Face", faces);
    const FontFamilyId family = loaded.family.id();
    require(family.value != 0U, "the look's family loads");

    const CompositeLoadResult definition = load_composite_definition("app-font-pair.control", app_font_pair,
                                                                     pair_child_types);
    for (const std::string& line : definition.diagnostics) std::cerr << "  " << line << '\n';
    require(static_cast<bool>(definition) && definition.diagnostics.empty(),
            "a look naming a loaded application family compiles without a warning");
    if (!definition) return;
    const std::shared_ptr<CompositeControl> pair =
        make_control<CompositeControl>(StableId(std::string("pair")), definition.definition);
    (*pair).set_requested_bounds({0.0, 0.0, 200.0, 80.0});
    Window window(pair, {200.0, 80.0});
    window.perform_layout();
    const Control::Ptr action = (*pair).child("action");
    require(action != nullptr, "the pair makes its button");
    if (action == nullptr) return;
    const FontSpec drawn = (*action).text_font(FontSpec{});
    require(drawn.family == family, "the composite's child draws the look's application family");

    // A stock button outside the composite, given the family in code, drops it.
    const std::shared_ptr<Button> outside = make_control<Button>(StableId(std::string("outside")));
    (*outside).set_requested_bounds({0.0, 0.0, 80.0, 24.0});
    Window other(outside, {80.0, 24.0});
    const AppFontStatistics before = app_font_statistics();
    FontSpec coded{};
    coded.family = family;
    const FontSpec refused = (*outside).effective_font(coded);
    require(refused.family.value == 0U, "a stock control drops an application family");
    const std::array<FontFamilyId, 2> listed{carlito_font_family, family};
    coded.family = FontFamilyId{};
    coded.stack = register_font_family_stack(listed);
    const FontSpec stacked = (*outside).effective_font(coded);
    require(stacked.stack.value == 0U, "a stock control drops a stack that lists an application family");
    const AppFontStatistics after = app_font_statistics();
    require(after.refused_uses == before.refused_uses + 2U, "each refused font is counted");

    FontSpec content{};
    content.family = family;
    require(admit_font(content, FontContext::content).family == family &&
                admit_font(content, FontContext::composite).family == family,
            "content and composite contexts keep the family");
}

void theme_does_not_resolve_family() {
    const std::shared_ptr<const std::vector<std::byte>> carlito = read_font(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::array<AppFontFace, 1> faces{face_of(carlito)};
    const AppFontLoad loaded = load_app_font_family("App Theme Face", faces);
    require(loaded.family.id().value != 0U, "the theme case's family loads");
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    theme_language::ThemeSource source{};
    source.file = "app.theme.css";
    source.text = "button { font-family: \"App Theme Face\", content; }\n";
    for (const bool admitted : {false, true}) {
        theme_language::ThemeCompileInput input{
            .source = source,
            .registry = registry,
            .vocabulary = theme_language::SelectorVocabulary{stock_control_anatomies()},
            .admit_app_fonts = admitted};
        ThemeRuleSheet sheet{};
        theme_language::ThemeCompiler compiler{};
        theme_language::ThemeDiagnosticList diagnostics{};
        const theme_language::CompileOutcome outcome = compiler.compile(input, sheet, diagnostics);
        bool unavailable = false;
        for (const theme_language::ThemeDiagnostic& diagnostic : diagnostics.records()) {
            if (diagnostic.code == theme_language::DiagnosticCode::font_family_unavailable) unavailable = true;
        }
        require(outcome == theme_language::CompileOutcome::compiled, "the sheet compiles");
        if (admitted) {
            require(!unavailable, "a look resolves the application family");
        } else {
            require(unavailable, "a theme sheet does not resolve an application family (W113)");
        }
    }
}

// -- Quarantine and unload --------------------------------------------------

// A face with CFF outlines whose CFF table is zeros: it passes the table checks, and STXlib's
// font reader refuses it (CFF major version 0).
std::shared_ptr<const std::vector<std::byte>> unopenable_face() {
    std::vector<SyntheticTable> tables = minimal_tables();
    std::vector<SyntheticTable> cff_tables{};
    for (SyntheticTable& each : tables) {
        if (each.tag == tag_of("glyf") || each.tag == tag_of("loca")) continue;
        cff_tables.push_back(std::move(each));
    }
    cff_tables.insert(cff_tables.begin(), table("CFF ", 32U));
    std::shared_ptr<const std::vector<std::byte>> face =
        std::make_shared<const std::vector<std::byte>>(assemble(tag_of("OTTO"), cff_tables));
    return face;
}

// Whether a run of `shaped` was drawn by a face of `family`.
bool drawn_with(const HarfBuzzFontEngine& engine, const ShapedText& shaped, const FontFamilyId family) {
    for (const ShapedFontRun& run : shaped.runs) {
        const std::optional<FontFaceSource> source = engine.face_source(run.face);
        if (source && (*source).family == family) return true;
    }
    return false;
}

void malformed_face_is_quarantined() {
    const std::shared_ptr<const std::vector<std::byte>> broken = unopenable_face();
    require(check(*broken).status == AppFontStatus::loaded, "the unopenable face passes the table checks");
    const std::array<AppFontFace, 1> faces{face_of(broken)};
    const AppFontLoad loaded = load_app_font_family("Quarantine Face", faces);
    const FontFamilyId family = loaded.family.id();
    require(family.value != 0U, "the unopenable family loads");

    const std::shared_ptr<const std::vector<std::byte>> carlito = read_font(GUI_FORMS_TEST_CARLITO_REGULAR);
    HarfBuzzFontEngine engine;
    require(engine.register_shared_typeface(FontRole::content, 400U, false, carlito).has_value(),
            "the content role registers");
    const AppFontStatistics before = app_font_statistics();
    FontSpec font{};
    font.role = FontRole::content;
    font.family = family;
    const ShapedText shaped = engine.shape("Quarantine", font);
    require(app_font_family_state(family) == AppFontState::quarantined && !font_family_drawable(family),
            "a face the font reader cannot open quarantines its family");
    require(app_font_statistics().quarantines == before.quarantines + 1U, "the quarantine is counted");
    require(!shaped.missing_primary_face && !shaped.runs.empty() && shaped.missing_clusters == 0U,
            "the role's faces draw the text instead");
    require(!drawn_with(engine, shaped, family), "no run draws with the quarantined family");
    const ShapedText again = engine.shape("Again", font);
    require(!again.runs.empty() && app_font_statistics().quarantines == before.quarantines + 1U,
            "a quarantined family is not tried again");
}

// A loaded family's faces each have one typeface, opened and registered at
// load (program_typefaces.hpp), which every executor drawing the family
// borrows. Unloading retires them at once, even while an engine defers the
// bytes' release: a face open over one is rejected and released on its next
// draw, and none opens again, so no executor draws an unloaded family.
void unloaded_family_faces_are_rejected() {
    const std::shared_ptr<const std::vector<std::byte>> carlito = read_font(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::array<AppFontFace, 1> faces{face_of(carlito)};
    AppFontLoad loaded = load_app_font_family("Retire Face", faces);
    const FontFamilyId family = loaded.family.id();
    require(family.value != 0U, "the retirement case's family loads");
    FontSpec font{};
    font.role = FontRole::content;
    font.family = family;
    HarfBuzzFontEngine engine;
    require(engine.register_shared_typeface(FontRole::content, 400U, false, carlito).has_value(),
            "the content role registers");
    const ShapedText shaped = engine.shape("Retire", font);
    std::optional<FontFaceSource> source{};
    std::uint32_t glyph = 0U;
    for (const ShapedFontRun& run : shaped.runs) {
        const std::optional<FontFaceSource> candidate = engine.face_source(run.face);
        if (!candidate || !((*candidate).family == family) || run.glyphs.empty()) continue;
        source = candidate;
        glyph = run.glyphs.front().glyph.value;
        break;
    }
    require(source.has_value() && glyph != 0U, "the engine draws with the application family");
    if (!source) return;
    const std::size_t typefaces = gui_forms::detail::program_typeface_count();
    const gui_forms::detail::TypefaceBorrow borrow =
        gui_forms::detail::program_typeface((*source).bytes, (*source).owner, 0U);
    require(borrow.typeface != nullptr && !(*borrow.typeface).retired() &&
                gui_forms::detail::program_typeface_count() == typefaces,
            "executors borrow the typeface the registry opened at load");

    gui_forms::detail::ExecutorFace face;
    require(face.open(borrow, gui_forms::detail::FaceByteLimits{}), "a face opens over the family's typeface");
    const std::array<stxlib::glyphs::RunGlyph, 1> placed{{{glyph, 0, 832}}};
    const stxlib::View<stxlib::glyphs::RunGlyph> run(placed.data(), placed.size());
    stxlib::glyphs::DrawStyle style{};
    style.size = 13 * 64;
    style.fit = stxlib::glyphs::Fit::light;
    stxlib::glyphs::RunCoverage drawn{};
    require(!face.draw_run(run, style, drawn).failed(), "the face draws while the family is loaded");

    require(loaded.family.unload() == AppFontUnload::deferred, "the engine defers the bytes' release");
    require((*borrow.typeface).retired(), "unloading retires the family's typeface at once");
    require(face.draw_run(run, style, drawn).failed() && !face.is_open(),
            "the face over it is rejected and released on its next draw");
    gui_forms::detail::ExecutorFace late;
    require(!late.open(gui_forms::detail::program_typeface((*source).bytes, (*source).owner, 0U),
                       gui_forms::detail::FaceByteLimits{}),
            "no face opens over an unloaded family's typeface");
}

void unload_while_in_use_is_deferred() {
    const std::shared_ptr<const std::vector<std::byte>> carlito = read_font(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::array<AppFontFace, 1> faces{face_of(carlito)};
    AppFontLoad loaded = load_app_font_family("Unload Face", faces);
    const FontFamilyId family = loaded.family.id();
    require(family.value != 0U, "the unload case's family loads");
    FontSpec font{};
    font.role = FontRole::content;
    font.family = family;
    {
        std::unique_ptr<HarfBuzzFontEngine> engine = std::make_unique<HarfBuzzFontEngine>();
        require((*engine).register_shared_typeface(FontRole::content, 400U, false, carlito).has_value(),
                "the content role registers");
        const ShapedText shaped = (*engine).shape("In use", font);
        require(drawn_with(*engine, shaped, family), "the engine draws with the application family");

        const AppFontStatistics before = app_font_statistics();
        require(loaded.family.unload() == AppFontUnload::deferred, "unloading while an engine holds it defers");
        require(app_font_statistics().unloads_deferred == before.unloads_deferred + 1U, "the deferral is counted");
        require(app_font_family_state(family) == AppFontState::unloading && !font_family_drawable(family),
                "an unloading family draws no new text");
        {
            // A FontFaceSource holds the bytes' owner too, so it is scoped: the release below
            // waits only for the engine.
            const std::optional<FontFaceSource> kept = (*engine).face_source(shaped.runs.front().face);
            require(kept.has_value() && !(*kept).bytes.empty(), "text already shaped keeps its face's bytes");
        }
        const ShapedText reshaped = (*engine).shape("In use", font);
        require(!reshaped.runs.empty() && !drawn_with(*engine, reshaped, family),
                "new text draws with the role's faces");

        // The name is free as soon as unload returns, before the deferred release completes.
        AppFontLoad during = load_app_font_family("Unload Face", faces);
        const FontFamilyId second = during.family.id();
        require(during.validation.status == AppFontStatus::loaded && second.value != 0U && !(second == family),
                "the name loads again while the old family's release is deferred, as a new family");
        require(app_font_family_named("Unload Face") == second, "the name finds the new family");
        require(app_font_family_state(family) == AppFontState::unloading,
                "the old family stays unloading beside the new one");
        FontSpec renamed = font;
        renamed.family = second;
        const ShapedText fresh = (*engine).shape("In use", renamed);
        require(drawn_with(*engine, fresh, second), "the same engine draws with the reloaded family");
        require(during.family.unload() == AppFontUnload::deferred, "the reloaded family defers in turn");
        engine.reset();
        require(app_font_family_state(family) == AppFontState::released &&
                    app_font_family_state(second) == AppFontState::released,
                "the bytes are released when the last engine that opened them goes");
    }
    // A later session: load, unload and load the same name again once every release completed.
    for (std::size_t session = 0U; session < 3U; ++session) {
        AppFontLoad shell = load_app_font_family("Unload Face", faces);
        const FontFamilyId shell_family = shell.family.id();
        require(shell.validation.status == AppFontStatus::loaded && shell_family.value != 0U &&
                    !(shell_family == family),
                "a later session loads the name again");
        require(shell.family.unload() == AppFontUnload::released, "a session's unused family is released");
    }
    require(loaded.family.unload() == AppFontUnload::not_loaded, "an emptied handle unloads nothing");
}

} // namespace

int main() {
    validation_states_each_check();
    loading_rules_and_lookup();
    composite_look_draws_family();
    theme_does_not_resolve_family();
    malformed_face_is_quarantined();
    unload_while_in_use_is_deferred();
    unloaded_family_faces_are_rejected();
    if (failures != 0) {
        std::cerr << failures << " app font check(s) failed\n";
        return 1;
    }
    std::cout << "app font tests passed\n";
    return 0;
}
