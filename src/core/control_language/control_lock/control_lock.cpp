// A composite type's lock file (control_lock.hpp).

#include "control_lock.hpp"

#include "gui_forms/anatomy/types/anatomy_types.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

namespace gui_forms::control_language {

namespace {

using theme_language::ByteOffset;
using theme_language::ColumnNumber;
using theme_language::DiagnosticCode;
using theme_language::LineNumber;
using theme_language::ThemeDiagnostic;

std::string kebab_of(const std::string_view snake) {
    std::string kebab(snake);
    std::replace(kebab.begin(), kebab.end(), '_', '-');
    return kebab;
}

// One entry: its category and name (the identity a later version keeps)
// and its kind (empty where the category has none).
struct LockEntry final {
    std::string category{};
    std::string name{};
    std::string kind{};
};

// Fills `entries` with the model's entries in document order, by category.
void model_entries(const ControlModel& model, std::vector<LockEntry>& entries) {
    entries.clear();
    for (const ControlProperty& property : model.properties) {
        const std::string kind =
            property.kind ? std::string(control_property_kind_name(*property.kind)) : std::string("alias");
        entries.push_back(LockEntry{"property", property.name, kind});
    }
    for (const ControlEvent& event : model.events) {
        const std::string payload =
            event.payload ? std::string(control_property_kind_name(*event.payload)) : std::string("none");
        entries.push_back(LockEntry{"event", event.name, payload});
    }
    for (const ControlCommand& command : model.commands) entries.push_back(LockEntry{"command", command.name, {}});
    for (const ControlState& state : model.states) {
        const std::string category = state.custom ? std::string("state") : std::string("publish");
        entries.push_back(LockEntry{category, state.name, {}});
    }
    for (const ControlPart& part : model.parts) {
        entries.push_back(LockEntry{"part", part.name, kebab_of(part_role_name(part.role))});
    }
}

// The whitespace-separated words of one line, borrowed from it.
void split_words(const std::string_view line, std::vector<std::string_view>& words) {
    words.clear();
    std::size_t at = 0U;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) ++at;
        const std::size_t start = at;
        while (at < line.size() && line[at] != ' ' && line[at] != '\t' && line[at] != '\r') ++at;
        if (at > start) words.push_back(line.substr(start, at - start));
    }
}

// `<major>.<minor>`, each one to six digits.
bool read_version(const std::string_view text, std::uint32_t& major, std::uint32_t& minor) {
    const std::size_t dot = text.find('.');
    if (dot == std::string_view::npos || dot == 0U || dot + 1U >= text.size()) return false;
    const std::string_view major_text = text.substr(0U, dot);
    const std::string_view minor_text = text.substr(dot + 1U);
    if (major_text.size() > 6U || minor_text.size() > 6U) return false;
    for (const char character : text) {
        if (character != '.' && (character < '0' || character > '9')) return false;
    }
    if (text.find('.', dot + 1U) != std::string_view::npos) return false;
    major = static_cast<std::uint32_t>(std::strtoul(std::string(major_text).c_str(), nullptr, 10));
    minor = static_cast<std::uint32_t>(std::strtoul(std::string(minor_text).c_str(), nullptr, 10));
    return true;
}

// Whether a category takes a kind word.
bool kinded(const std::string_view category) noexcept {
    const bool result = category == "property" || category == "event" || category == "part";
    return result;
}

class LockChecker final {
public:
    LockChecker(std::string_view lock_file, theme_language::ThemeDiagnosticList& diagnostics) noexcept
        : lock_file_(lock_file), diagnostics_(diagnostics) {}

    void report(std::size_t offset, std::uint32_t line, std::string_view rule, std::string detail) {
        ThemeDiagnostic diagnostic{};
        diagnostic.code = DiagnosticCode::control_lock_violation;
        diagnostic.file = std::string(lock_file_);
        diagnostic.position.offset = ByteOffset{static_cast<std::uint32_t>(offset)};
        diagnostic.position.line = LineNumber{line};
        diagnostic.position.column = ColumnNumber{1U};
        diagnostic.rule = std::string(rule);
        diagnostic.detail = std::move(detail);
        diagnostics_.add(std::move(diagnostic));
        ++violations_;
    }

    [[nodiscard]] std::size_t violations() const noexcept { return violations_; }

private:
    std::string_view lock_file_;
    theme_language::ThemeDiagnosticList& diagnostics_;
    std::size_t violations_{};
};

} // namespace

std::string write_control_lock(const ControlModel& model) {
    std::vector<LockEntry> entries{};
    model_entries(model, entries);
    std::string text = "control " + model.name + " " + std::to_string(model.version_major) + "." +
                       std::to_string(model.version_minor) + "\n";
    for (const LockEntry& entry : entries) {
        text += entry.category;
        text += ' ';
        text += entry.name;
        if (!entry.kind.empty()) {
            text += ' ';
            text += entry.kind;
        }
        text += '\n';
    }
    return text;
}

ControlLockOutcome check_control_lock(const ControlModel& model, const std::string_view lock,
                                      const std::string_view lock_file,
                                      theme_language::ThemeDiagnosticList& diagnostics) {
    LockChecker checker(lock_file, diagnostics);
    std::vector<LockEntry> entries{};
    model_entries(model, entries);
    std::vector<std::string_view> words{};
    bool header_seen = false;
    bool same_major = false;
    std::size_t line_start = 0U;
    std::uint32_t line_number = 0U;
    while (line_start < lock.size()) {
        std::size_t line_end = lock.find('\n', line_start);
        if (line_end == std::string_view::npos) line_end = lock.size();
        const std::string_view line = lock.substr(line_start, line_end - line_start);
        const std::size_t offset = line_start;
        line_start = line_end + 1U;
        ++line_number;
        split_words(line, words);
        if (words.empty() || words[0].starts_with("#")) continue;
        if (!header_seen) {
            header_seen = true;
            std::uint32_t major = 0U;
            std::uint32_t minor = 0U;
            if (words.size() != 3U || words[0] != "control" || !read_version(words[2], major, minor)) {
                checker.report(offset, line_number, "control", "the lock begins `control <name> <major>.<minor>`");
                break;
            }
            if (words[1] != model.name) {
                checker.report(offset, line_number, "control",
                               "the lock is " + std::string(words[1]) + "'s, not " + model.name + "'s");
                break;
            }
            const bool lower = model.version_major < major ||
                               (model.version_major == major && model.version_minor < minor);
            if (lower) {
                checker.report(offset, line_number, "control",
                               "version " + std::to_string(model.version_major) + "." +
                                   std::to_string(model.version_minor) + " is lower than the locked " +
                                   std::string(words[2]));
                break;
            }
            same_major = model.version_major == major;
            if (!same_major) break;
            continue;
        }
        const std::string_view category = words[0];
        const bool known_category = category == "property" || category == "event" || category == "command" ||
                                    category == "publish" || category == "state" || category == "part";
        const std::size_t expected_words = kinded(category) ? 3U : 2U;
        if (!known_category || words.size() != expected_words) {
            checker.report(offset, line_number, category, "malformed lock entry");
            continue;
        }
        const std::string_view name = words[1];
        const std::string_view kind = expected_words == 3U ? words[2] : std::string_view{};
        const LockEntry* found = nullptr;
        for (const LockEntry& entry : entries) {
            if (entry.category == category && entry.name == name) found = &entry;
        }
        if (found == nullptr) {
            checker.report(offset, line_number, name,
                           std::string(category) + " " + std::string(name) + " is locked at version " +
                               std::to_string(model.version_major) + " and may not be removed or renamed");
            continue;
        }
        if ((*found).kind != kind) {
            checker.report(offset, line_number, name,
                           std::string(category) + " " + std::string(name) + " is locked as " + std::string(kind) +
                               "; it is now " + (*found).kind);
        }
    }
    if (!header_seen) checker.report(0U, 1U, "control", "the lock is empty");
    const ControlLockOutcome outcome =
        checker.violations() == 0U ? ControlLockOutcome::kept : ControlLockOutcome::violated;
    return outcome;
}

} // namespace gui_forms::control_language
