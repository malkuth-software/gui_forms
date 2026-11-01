#include "theme_manifest.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <utility>

namespace gui_forms::theme_language {

namespace {

using detail::JsonKind;
using detail::JsonValue;

constexpr std::array<std::string_view, 11> manifest_keys{
    "format", "name", "version", "engine", "author", "provenance",
    "sheet", "imports", "resources", "published_values", "coverage",
};

bool lower_letter_or_digit(char character) noexcept {
    const bool letter = character >= 'a' && character <= 'z';
    const bool digit = character >= '0' && character <= '9';
    return letter || digit;
}

bool digit(char character) noexcept {
    return character >= '0' && character <= '9';
}

// A decimal of at most `most` digits, without a sign or a leading zero,
// into `value`.
bool read_decimal(std::string_view text, std::size_t most, std::uint32_t& value) noexcept {
    if (text.empty() || text.size() > most) return false;
    if (text.size() > 1U && text[0] == '0') return false;
    std::uint32_t read = 0U;
    for (const char character : text) {
        if (!digit(character)) return false;
        read = read * 10U + static_cast<std::uint32_t>(character - '0');
    }
    value = read;
    return true;
}

// `MAJOR.MINOR.PATCH`, each a decimal of at most 9 digits.
bool version_valid(std::string_view text) noexcept {
    std::size_t parts = 0U;
    std::size_t start = 0U;
    while (start <= text.size()) {
        std::size_t end = text.find('.', start);
        if (end == std::string_view::npos) end = text.size();
        std::uint32_t unused = 0U;
        if (!read_decimal(text.substr(start, end - start), 9U, unused)) return false;
        parts = parts + 1U;
        start = end + 1U;
    }
    return parts == 3U;
}

// A resource's name: a package name that may also hold `.` and `_` after
// its first character.
bool resource_name_valid(std::string_view name) noexcept {
    if (name.empty() || name.size() > maximum_package_name_bytes) return false;
    if (!lower_letter_or_digit(name[0])) return false;
    for (const char character : name) {
        const bool admitted = lower_letter_or_digit(character) || character == '-' || character == '.' ||
                              character == '_';
        if (!admitted) return false;
    }
    return true;
}

// Reads one manifest's JSON into a ThemeManifest, stopping at the first
// problem.
class ManifestReader final {
public:
    ManifestReader(const JsonValue& root, ThemeManifest& manifest, ThemeManifestProblem& problem) noexcept
        : root_(root), manifest_(manifest), problem_(problem) {}

    [[nodiscard]] bool read_engine(EngineVersion running);
    [[nodiscard]] bool read_rest();

private:
    bool fail(DiagnosticCode code, std::string detail) {
        problem_.code = code;
        problem_.detail = std::move(detail);
        problem_.position = detail::JsonSourcePosition{};
        return false;
    }
    bool invalid(std::string_view key) { return fail(DiagnosticCode::manifest_invalid, std::string(key)); }
    // The member `key` of `object` as a string of at most `most` bytes;
    // null where it is absent (absent is a problem where `required`).
    [[nodiscard]] bool read_text(const JsonValue& object, std::string_view key, std::string_view path,
                                 std::size_t most, bool required, std::string& text);
    [[nodiscard]] bool read_hash(const JsonValue& value, std::string_view path, detail::Sha256Digest& digest);
    [[nodiscard]] bool read_file(const JsonValue& object, std::string_view path, ThemePackageFile& file);
    [[nodiscard]] bool only_keys(const JsonValue& object, std::string_view path,
                                 std::span<const std::string_view> keys);
    [[nodiscard]] bool read_imports(const JsonValue& value);
    [[nodiscard]] bool read_resources(const JsonValue& value);
    [[nodiscard]] bool read_published_values(const JsonValue& value);
    [[nodiscard]] bool read_coverage(const JsonValue& value);

    const JsonValue& root_;
    ThemeManifest& manifest_;
    ThemeManifestProblem& problem_;
};

bool ManifestReader::read_text(const JsonValue& object, std::string_view key, std::string_view path,
                               std::size_t most, bool required, std::string& text) {
    const JsonValue* const value = object.find(key);
    if (value == nullptr) {
        if (required) return invalid(path);
        text.clear();
        return true;
    }
    if ((*value).kind != JsonKind::string || (*value).text.size() > most) return invalid(path);
    text = (*value).text;
    return true;
}

bool ManifestReader::read_hash(const JsonValue& value, std::string_view path, detail::Sha256Digest& digest) {
    if (value.kind != JsonKind::string || value.text.size() != sha256_hex_digits) return invalid(path);
    for (const char character : value.text) {
        const bool lower_hex = digit(character) || (character >= 'a' && character <= 'f');
        if (!lower_hex) return invalid(path);
    }
    if (!detail::parse_sha256_hex(std::span<const char>(value.text), digest)) return invalid(path);
    return true;
}

bool ManifestReader::only_keys(const JsonValue& object, std::string_view path,
                               std::span<const std::string_view> keys) {
    for (const std::string& name : object.names) {
        bool known = false;
        for (const std::string_view key : keys) {
            if (name == key) known = true;
        }
        if (!known) return invalid(std::string(path) + clipped_theme_detail(name));
    }
    return true;
}

bool ManifestReader::read_file(const JsonValue& object, std::string_view path, ThemePackageFile& file) {
    constexpr std::array<std::string_view, 2> keys{"path", "sha256"};
    const std::string prefix = std::string(path) + ".";
    if (object.kind != JsonKind::object) return invalid(path);
    if (!only_keys(object, prefix, keys)) return false;
    if (!read_text(object, "path", prefix + "path", maximum_package_path_bytes, true, file.path)) return false;
    if (!package_path_valid(file.path)) return invalid(prefix + "path");
    const JsonValue* const hash = object.find("sha256");
    if (hash == nullptr) return invalid(prefix + "sha256");
    return read_hash(*hash, prefix + "sha256", file.sha256);
}

bool ManifestReader::read_engine(EngineVersion running) {
    std::string bound{};
    if (!read_text(root_, "engine", "engine", maximum_manifest_text_bytes, true, bound)) return false;
    const std::optional<EngineVersion> parsed = parse_engine_bound(bound);
    if (!parsed) return invalid("engine");
    manifest_.engine = *parsed;
    if (running < *parsed) return fail(DiagnosticCode::engine_too_old, engine_too_old_detail(*parsed));
    return true;
}

bool ManifestReader::read_rest() {
    if (!only_keys(root_, "", manifest_keys)) return false;
    std::string format{};
    if (!read_text(root_, "format", "format", maximum_manifest_text_bytes, true, format)) return false;
    if (format != theme_manifest_format) return invalid("format");
    if (!read_text(root_, "name", "name", maximum_package_name_bytes, true, manifest_.name)) return false;
    if (!package_name_valid(manifest_.name)) return invalid("name");
    if (!read_text(root_, "version", "version", maximum_manifest_text_bytes, true, manifest_.version)) {
        return false;
    }
    if (!version_valid(manifest_.version)) return invalid("version");
    if (!read_text(root_, "author", "author", maximum_manifest_text_bytes, false, manifest_.author)) return false;
    if (!read_text(root_, "provenance", "provenance", maximum_manifest_text_bytes, false, manifest_.provenance)) {
        return false;
    }
    const JsonValue* const sheet = root_.find("sheet");
    if (sheet == nullptr) return invalid("sheet");
    if (!read_file(*sheet, "sheet", manifest_.sheet)) return false;
    manifest_.imports.clear();
    manifest_.resources.clear();
    manifest_.published_values.clear();
    manifest_.coverage.clear();
    const JsonValue* const imports = root_.find("imports");
    if (imports != nullptr && !read_imports(*imports)) return false;
    const JsonValue* const resources = root_.find("resources");
    if (resources != nullptr && !read_resources(*resources)) return false;
    const JsonValue* const published = root_.find("published_values");
    if (published != nullptr && !read_published_values(*published)) return false;
    const JsonValue* const coverage = root_.find("coverage");
    if (coverage != nullptr && !read_coverage(*coverage)) return false;
    return true;
}

bool ManifestReader::read_imports(const JsonValue& value) {
    if (value.kind != JsonKind::object) return invalid("imports");
    if (value.items.size() > maximum_imports) {
        return fail(DiagnosticCode::too_many_imports, std::to_string(maximum_imports));
    }
    for (std::size_t index = 0U; index < value.items.size(); ++index) {
        const std::string& name = value.names[index];
        const std::string path = "imports." + clipped_theme_detail(name);
        if (!package_name_valid(name)) return invalid(path);
        if (name == manifest_.name) return fail(DiagnosticCode::import_cycle, clipped_theme_detail(name));
        ThemeManifestImport pinned{};
        pinned.name = name;
        if (!read_hash(value.items[index], path, pinned.manifest_sha256)) return false;
        manifest_.imports.push_back(std::move(pinned));
    }
    return true;
}

bool ManifestReader::read_resources(const JsonValue& value) {
    constexpr std::array<std::string_view, 3> keys{"name", "path", "sha256"};
    if (value.kind != JsonKind::array || value.items.size() > maximum_manifest_resources) {
        return invalid("resources");
    }
    for (std::size_t index = 0U; index < value.items.size(); ++index) {
        const JsonValue& entry = value.items[index];
        const std::string path = "resources[" + std::to_string(index) + "]";
        if (entry.kind != JsonKind::object) return invalid(path);
        if (!only_keys(entry, path + ".", keys)) return false;
        ThemeManifestResource resource{};
        if (!read_text(entry, "name", path + ".name", maximum_package_name_bytes, true, resource.name)) {
            return false;
        }
        if (!resource_name_valid(resource.name)) return invalid(path + ".name");
        for (const ThemeManifestResource& earlier : manifest_.resources) {
            if (earlier.name == resource.name) return invalid(path + ".name");
        }
        if (!read_text(entry, "path", path + ".path", maximum_package_path_bytes, true, resource.file.path)) {
            return false;
        }
        if (!package_path_valid(resource.file.path)) return invalid(path + ".path");
        const JsonValue* const hash = entry.find("sha256");
        if (hash == nullptr) return invalid(path + ".sha256");
        if (!read_hash(*hash, path + ".sha256", resource.file.sha256)) return false;
        manifest_.resources.push_back(std::move(resource));
    }
    return true;
}

bool ManifestReader::read_published_values(const JsonValue& value) {
    if (value.kind != JsonKind::array || value.items.size() > maximum_manifest_published_values) {
        return invalid("published_values");
    }
    for (const JsonValue& entry : value.items) {
        if (entry.kind != JsonKind::string) return invalid("published_values");
        const std::string detail = clipped_theme_detail(entry.text);
        if (!package_name_valid(entry.text)) return fail(DiagnosticCode::published_value_invalid, detail);
        for (const std::string& earlier : manifest_.published_values) {
            if (earlier == entry.text) return fail(DiagnosticCode::published_value_invalid, detail);
        }
        manifest_.published_values.push_back(entry.text);
    }
    return true;
}

bool ManifestReader::read_coverage(const JsonValue& value) {
    if (value.kind != JsonKind::array || value.items.size() > maximum_manifest_coverage) {
        return invalid("coverage");
    }
    for (std::size_t index = 0U; index < value.items.size(); ++index) {
        const JsonValue& entry = value.items[index];
        const std::string path = "coverage[" + std::to_string(index) + "]";
        if (entry.kind != JsonKind::string || !package_name_valid(entry.text)) return invalid(path);
        for (const std::string& earlier : manifest_.coverage) {
            if (earlier == entry.text) return invalid(path);
        }
        manifest_.coverage.push_back(entry.text);
    }
    return true;
}

} // namespace

ThemeManifestOutcome read_theme_manifest(std::string_view bytes, EngineVersion running, ThemeManifest& manifest,
                                         ThemeManifestProblem& problem) {
    detail::JsonLimits limits{};
    limits.maximum_input_bytes = maximum_manifest_bytes;
    limits.maximum_depth = 4U;
    limits.maximum_values = 4096U;
    limits.maximum_members = 1024U;
    limits.maximum_string_bytes = maximum_package_path_bytes + 1U;
    const detail::JsonReadResult read = detail::read_json(bytes, limits);
    if (!read) {
        problem.code = DiagnosticCode::manifest_invalid;
        problem.detail = std::string(detail::json_error_name(read.error));
        problem.position = read.position;
        return ThemeManifestOutcome::refused;
    }
    if (read.root.kind != JsonKind::object) {
        problem.code = DiagnosticCode::manifest_invalid;
        problem.detail = "the manifest is not an object";
        problem.position = detail::JsonSourcePosition{};
        return ThemeManifestOutcome::refused;
    }
    ManifestReader reader(read.root, manifest, problem);
    if (!reader.read_engine(running) || !reader.read_rest()) return ThemeManifestOutcome::refused;
    return ThemeManifestOutcome::read;
}

detail::Sha256Digest package_sha256(std::string_view bytes) noexcept {
    const std::span<const std::uint8_t> view(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    return detail::sha256(view);
}

std::string package_sha256_hex(const detail::Sha256Digest& digest) {
    constexpr std::string_view hex_digits = "0123456789abcdef";
    std::string text{};
    text.reserve(sha256_hex_digits);
    for (const std::uint8_t value : digest.bytes) {
        text.push_back(hex_digits[value >> 4U]);
        text.push_back(hex_digits[value & 0x0FU]);
    }
    return text;
}

std::optional<EngineVersion> parse_engine_bound(std::string_view text) noexcept {
    constexpr std::string_view prefix = ">=";
    if (text.substr(0U, prefix.size()) != prefix) return std::nullopt;
    const std::string_view version = text.substr(prefix.size());
    const std::size_t dot = version.find('.');
    if (dot == std::string_view::npos) return std::nullopt;
    std::uint32_t major = 0U;
    std::uint32_t minor = 0U;
    if (!read_decimal(version.substr(0U, dot), 5U, major) || !read_decimal(version.substr(dot + 1U), 5U, minor)) {
        return std::nullopt;
    }
    if (major > 65535U || minor > 65535U) return std::nullopt;
    return EngineVersion{static_cast<std::uint16_t>(major), static_cast<std::uint16_t>(minor)};
}

std::string engine_too_old_detail(EngineVersion needed) {
    return "this theme needs engine >= " + std::to_string(needed.major) + "." + std::to_string(needed.minor) +
           "; upgrade to run it";
}

bool package_resource_name_valid(std::string_view name) noexcept {
    const bool valid = resource_name_valid(name);
    return valid;
}

bool package_name_valid(std::string_view name) noexcept {
    if (name.empty() || name.size() > maximum_package_name_bytes) return false;
    if (!lower_letter_or_digit(name[0])) return false;
    for (const char character : name) {
        if (!lower_letter_or_digit(character) && character != '-') return false;
    }
    return true;
}

bool package_path_valid(std::string_view path) noexcept {
    if (path.empty() || path.size() > maximum_package_path_bytes) return false;
    std::size_t segments = 0U;
    std::size_t start = 0U;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        const std::string_view segment = path.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        if (segment.find('\\') != std::string_view::npos) return false;
        for (const char character : segment) {
            if (static_cast<unsigned char>(character) < 0x20U || character == ':') return false;
        }
        segments = segments + 1U;
        start = end + 1U;
    }
    return segments <= maximum_package_path_segments;
}

} // namespace gui_forms::theme_language
