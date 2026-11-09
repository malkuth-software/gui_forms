#include "gui_forms/controls/quick_access_toolbar/ribbon_state.hpp"

#include "gui_forms/text.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace gui_forms {
namespace {

constexpr std::string_view header = "gui_forms.ribbon_state 1";
constexpr std::string_view header_prefix = "gui_forms.ribbon_state ";
constexpr std::string_view placement_key = "quick_access.placement";
constexpr std::string_view minimized_key = "ribbon.minimized";
constexpr std::string_view command_key = "quick_access.command";

// An ID or entry fits on one line of the text form.
bool single_line(std::string_view text) noexcept {
    for (const char value : text) {
        const unsigned char byte = static_cast<unsigned char>(value);
        if (byte < 0x20U || byte == 0x7FU) return false;
    }
    return true;
}

bool valid_command_id(std::string_view id) {
    const bool result = !id.empty() && single_line(id) && validate_utf8(id).valid();
    return result;
}

std::string_view placement_name(QuickAccessToolbarPlacement placement) noexcept {
    if (placement == QuickAccessToolbarPlacement::below_ribbon) return "below_ribbon";
    return "above_ribbon";
}

void report(RibbonStateParseResult& result, std::size_t line, RibbonStateIssue issue) {
    result.diagnostics.push_back({line, issue});
}

} // namespace

std::string serialize_ribbon_state(const RibbonState& state) {
    if (state.quick_access_command_ids.size() > QuickAccessToolbar::maximum_commands) {
        throw std::invalid_argument("Ribbon state holds at most 20 Quick Access Toolbar commands");
    }
    std::unordered_set<std::string> seen{};
    for (const std::string& id : state.quick_access_command_ids) {
        if (!valid_command_id(id) || !seen.insert(id).second) {
            throw std::invalid_argument("Ribbon state command IDs must be unique single-line UTF-8");
        }
    }
    for (const std::string& entry : state.unrecognized_entries) {
        if (!single_line(entry) || !validate_utf8(entry).valid()) {
            throw std::invalid_argument("Ribbon state entries must be single-line UTF-8");
        }
    }
    std::string text;
    text += header;
    text += '\n';
    text += placement_key;
    text += '=';
    text += placement_name(state.quick_access_placement);
    text += '\n';
    text += minimized_key;
    text += state.minimized ? "=true\n" : "=false\n";
    for (const std::string& id : state.quick_access_command_ids) {
        text += command_key;
        text += '=';
        text += id;
        text += '\n';
    }
    for (const std::string& entry : state.unrecognized_entries) {
        text += entry;
        text += '\n';
    }
    return text;
}

RibbonStateParseResult parse_ribbon_state(std::string_view text) {
    RibbonStateParseResult result{};
    std::unordered_set<std::string> seen_ids{};
    bool seen_placement = false;
    bool seen_minimized = false;
    std::size_t line_number = 0U;
    std::size_t start = 0U;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1U;
        ++line_number;
        // A file edited on Windows may end its lines with CR LF.
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        if (line_number == 1U) {
            if (line == header) continue;
            const bool other_version = line.substr(0U, header_prefix.size()) == header_prefix;
            report(result, 1U, other_version ? RibbonStateIssue::unsupported_version : RibbonStateIssue::missing_header);
            return result;
        }
        if (line.empty()) continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || equals == 0U) {
            report(result, line_number, RibbonStateIssue::malformed_line);
            continue;
        }
        const std::string_view key = line.substr(0U, equals);
        const std::string_view value = line.substr(equals + 1U);
        if (key == placement_key) {
            if (seen_placement) {
                report(result, line_number, RibbonStateIssue::duplicate_key);
            } else if (value == "above_ribbon") {
                result.state.quick_access_placement = QuickAccessToolbarPlacement::above_ribbon;
            } else if (value == "below_ribbon") {
                result.state.quick_access_placement = QuickAccessToolbarPlacement::below_ribbon;
            } else {
                report(result, line_number, RibbonStateIssue::invalid_value);
            }
            seen_placement = true;
        } else if (key == minimized_key) {
            if (seen_minimized) {
                report(result, line_number, RibbonStateIssue::duplicate_key);
            } else if (value == "true" || value == "false") {
                result.state.minimized = value == "true";
            } else {
                report(result, line_number, RibbonStateIssue::invalid_value);
            }
            seen_minimized = true;
        } else if (key == command_key) {
            if (!valid_command_id(value)) {
                report(result, line_number, RibbonStateIssue::invalid_value);
            } else if (seen_ids.count(std::string(value)) != 0U) {
                report(result, line_number, RibbonStateIssue::duplicate_key);
            } else if (result.state.quick_access_command_ids.size() >= QuickAccessToolbar::maximum_commands) {
                report(result, line_number, RibbonStateIssue::too_many_commands);
            } else {
                seen_ids.emplace(value);
                result.state.quick_access_command_ids.emplace_back(value);
            }
        } else if (single_line(line) && validate_utf8(line).valid()) {
            result.state.unrecognized_entries.emplace_back(line);
        } else {
            report(result, line_number, RibbonStateIssue::malformed_line);
        }
    }
    if (line_number == 0U) report(result, 1U, RibbonStateIssue::missing_header);
    return result;
}

RibbonState capture_ribbon_state(const QuickAccessToolbar& toolbar, bool minimized) {
    RibbonState state{};
    state.quick_access_command_ids = toolbar.shown_command_ids();
    state.quick_access_placement = toolbar.placement.read();
    state.minimized = minimized;
    return state;
}

void apply_ribbon_state(QuickAccessToolbar& toolbar, const RibbonState& state) {
    toolbar.set_shown_command_ids(state.quick_access_command_ids);
    toolbar.placement = state.quick_access_placement;
}

} // namespace gui_forms
