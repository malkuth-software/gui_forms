#include "paint_probe.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
using namespace gui_forms;
using namespace gui_forms::render;
using namespace worker_probe;

void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

std::shared_ptr<const FontSet> load_fonts(const std::filesystem::path& root) {
    constexpr std::array<const char*, 4> names{
        "Carlito-Regular.ttf", "NotoSansArabic-Minimal.ttf",
        "NotoSansHebrew-Regular.ttf", "NotoEmoji-Regular.ttf"};
    FontSet set{};
    const std::size_t count = names.size();
    for (std::size_t index = 0; index < count; ++index) {
        const std::filesystem::path path = root / names[index];
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        require(static_cast<bool>(stream), "font open");
        const std::streamsize length = stream.tellg();
        require(length > 0 && length <= 16 * 1024 * 1024, "font size admission");
        const std::size_t size = static_cast<std::size_t>(length);
        std::vector<std::byte> bytes(size);
        char* destination = reinterpret_cast<char*>(bytes.data());
        stream.seekg(0);
        stream.read(destination, length);
        require(static_cast<bool>(stream), "complete font read");
        set.faces[index].encoded = std::make_shared<const std::vector<std::byte>>(std::move(bytes));
    }
    set.faces[0].role = FontRole::content;
    std::shared_ptr<const FontSet> result = std::make_shared<const FontSet>(std::move(set));
    return result;
}

Prepared worker_result(const std::shared_ptr<const FontSet>& fonts, std::string_view input) {
    Identity identity{};
    identity.page.revision = {1, 1};
    identity.page.serial = 1;
    identity.page.permitted.end = SourceByteOffset(static_cast<std::uint64_t>(input.size()));
    identity.layout_serial = 1;
    identity.provider_instance = 1;
    identity.provider_generation = 1;
    identity.font_set = 1;
    identity.font_generation = 1;
    identity.context_generation = 1;
    identity.font = {FontRole::content, 20.0, 400, false, 0.25};
    Worker worker(fonts);
    worker.start();
    const Outcome desired = worker.desire(identity);
    const Outcome submitted = worker.submit(identity, input);
    require(desired == Outcome::success && submitted == Outcome::success, "worker request");
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (;;) {
        const Snapshot state = worker.snapshot();
        if (state.slot == Slot::ready) { break; }
        const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start;
        require(elapsed < std::chrono::seconds(10), "worker completion deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const Outcome published = worker.publish();
    require(published == Outcome::success, "worker publication");
    worker.close();
    const Snapshot closed = worker.snapshot();
    require(closed.joined && !closed.engine_alive, "native worker faces destroyed before painting");
    const Prepared* displayed = worker.displayed();
    require(displayed != nullptr, "prepared result retained");
    Prepared copied = *displayed;
    return copied;
}

Prepared reference_result(const Prepared& prepared) {
    Prepared reference = prepared;
    text::TextShapeEngine engine{};
    const FontSet& fonts = *prepared.fonts;
    const std::size_t count = fonts.faces.size();
    for (std::size_t index = 0; index < count; ++index) {
        const FontBytes& face = fonts.faces[index];
        const std::optional<FontFaceId> registered = engine.register_shared_typeface(
            face.role, face.weight, face.italic, face.encoded, face.face_index);
        require(registered.has_value(), "reference face registration");
        reference.face_ids[index] = *registered;
    }
    reference.geometry = engine.shape(reference.display_utf8, reference.identity.font);
    return reference;
}

void refuse(paint_probe::Painter& painter, const Prepared& altered,
            const Identity& expected, std::uint64_t epoch, paint_probe::Status status,
            const std::vector<std::uint8_t>& pixels, const char* label) {
    const paint_probe::Status actual = painter.paint(altered, expected, epoch);
    require(actual == status, label);
    require(painter.pixels() == pixels, "refusal preserves complete old pixels");
    require(same_identity(painter.painted_identity(), expected), "refusal preserves painted identity");
    std::cout << "refused," << label << ',' << static_cast<unsigned>(actual) << '\n';
}

void exercise(const std::shared_ptr<const FontSet>& fonts) {
    const Prepared prepared = worker_result(fonts, "ABC שלום العربية 123 a\xcc\x81 office");
    const Prepared reference = reference_result(prepared);
    // Both shaping engines have been destroyed before either painter opens FT.
    paint_probe::Painter painter(fonts);
    paint_probe::Painter control(fonts);
    const paint_probe::Status painted = painter.paint(prepared, prepared.identity, prepared.authority_epoch);
    const paint_probe::Status reference_painted = control.paint(reference, reference.identity, reference.authority_epoch);
    std::cout << "initial_status," << static_cast<unsigned>(painted) << ','
              << static_cast<unsigned>(reference_painted) << ",missing_clusters="
              << prepared.geometry.missing_clusters << ",missing_primary="
              << prepared.geometry.missing_primary_face << '\n';
    require(painted == paint_probe::Status::success && reference_painted == paint_probe::Status::success,
            "independent native paint faces consume prepared glyphs");
    const std::vector<std::uint8_t> baseline = painter.pixels();
    require(baseline == control.pixels(), "worker transfer matches independent synchronous geometry raster");
    std::size_t ink_pixels = 0;
    for (const std::uint8_t coverage : baseline) { if (coverage != 0) { ++ink_pixels; } }
    require(ink_pixels > 0, "nonempty actual raster output");
    const Prepared uncovered = worker_result(fonts, "\xcc\x81");
    require(uncovered.geometry.missing_clusters == 1, "retained real missing-coverage fixture");
    const paint_probe::Status missing_coverage = painter.paint(uncovered, uncovered.identity, uncovered.authority_epoch);
    require(missing_coverage == paint_probe::Status::missing_font && painter.pixels() == baseline &&
            same_identity(painter.painted_identity(), prepared.identity), "uncovered input preserves old raster");
    std::cout << "refused,real missing cluster," << static_cast<unsigned>(missing_coverage) << '\n';

    Prepared altered = prepared;
    altered.fonts.reset();
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::missing_font, baseline, "missing lease");
    altered = prepared;
    ++altered.identity.provider_generation;
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::stale, baseline, "provider generation");
    altered = prepared;
    refuse(painter, altered, prepared.identity, prepared.authority_epoch + 1,
           paint_probe::Status::stale, baseline, "revoked epoch with living bytes");
    altered.identity.font.size += 1.0;
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::stale, baseline, "effective size");
    altered = prepared;
    FontSet wrong_table = *fonts;
    ++wrong_table.generation;
    altered.fonts = std::make_shared<const FontSet>(wrong_table);
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::incompatible_face, baseline, "font generation");
    wrong_table = *fonts;
    ++wrong_table.faces[0].face_index;
    altered.fonts = std::make_shared<const FontSet>(wrong_table);
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::incompatible_face, baseline, "face index");
    wrong_table = *fonts;
    ++wrong_table.faces[0].weight;
    altered.fonts = std::make_shared<const FontSet>(wrong_table);
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::incompatible_face, baseline, "registered style");
    wrong_table = *fonts;
    wrong_table.faces[0].encoded = wrong_table.faces[1].encoded;
    altered.fonts = std::make_shared<const FontSet>(wrong_table);
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::incompatible_face, baseline, "wrong encoded font");
    altered = prepared;
    require(!altered.geometry.runs.empty() && !altered.geometry.runs[0].glyphs.empty(), "glyph mutation fixture");
    altered.geometry.runs[0].face.value = 999;
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::incompatible_face, baseline, "unknown local face");
    altered = prepared;
    altered.geometry.runs[0].glyphs[0].glyph.value = std::numeric_limits<std::uint32_t>::max();
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::incompatible_glyph, baseline, "glyph range");
    altered.geometry.runs[0].glyphs[0].glyph.value = 0;
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::incompatible_glyph, baseline, "missing glyph zero");
    altered = prepared;
    altered.geometry.runs[0].glyphs[0].x = std::numeric_limits<float>::quiet_NaN();
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::invalid_geometry, baseline, "nonfinite position");
    altered.geometry.runs[0].glyphs[0].x = 2'000'000.0F;
    refuse(painter, altered, prepared.identity, prepared.authority_epoch,
           paint_probe::Status::invalid_geometry, baseline, "position conversion bound");
    const paint_probe::Status restored = painter.paint(prepared, prepared.identity, prepared.authority_epoch);
    require(restored == paint_probe::Status::success && painter.pixels() == baseline, "paint recovery");
    std::cout << "raster_bytes,ink_pixels,runs\n" << baseline.size() << ',' << ink_pixels
              << ',' << prepared.geometry.runs.size() << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: text_paint_probe FONT_DIRECTORY");
        const std::filesystem::path root(argv[1]);
        const std::shared_ptr<const FontSet> fonts = load_fonts(root);
        exercise(fonts);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
