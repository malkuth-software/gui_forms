#pragma once

#include "gui_forms/controls/quick_access_toolbar.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// What a ribbon window keeps between sessions: the Quick Access Toolbar's
// command IDs in order, its placement, and whether the ribbon is minimized.
// The last selected tab is not kept.
//
// The ribbon is not a layout (owner decision RB-44, revising RB-12): these
// are a settings permutation the program saves with its own settings; the
// ribbon never saves them on its own. The program captures and applies the
// state (capture_ribbon_state, apply_ribbon_state) and stores it as text
// (serialize_ribbon_state, parse_ribbon_state).
struct RibbonState final {
    std::vector<std::string> quick_access_command_ids{};
    QuickAccessToolbarPlacement quick_access_placement{QuickAccessToolbarPlacement::above_ribbon};
    bool minimized{};
    // Lines with keys this version does not know, kept in order and written
    // back unchanged, so a state from a later version survives a round trip.
    std::vector<std::string> unrecognized_entries{};

    friend bool operator==(const RibbonState& left, const RibbonState& right) noexcept {
        return left.quick_access_command_ids == right.quick_access_command_ids &&
               left.quick_access_placement == right.quick_access_placement &&
               left.minimized == right.minimized && left.unrecognized_entries == right.unrecognized_entries;
    }
};

// Why a line of saved state was not used.
enum class RibbonStateIssue : std::uint8_t {
    missing_header,      // the first line is not "gui_forms.ribbon_state 1"
    unsupported_version, // the header names another version
    malformed_line,      // no "key=value" form
    invalid_value,       // a known key with a value it cannot take
    duplicate_key,       // a single-valued key repeated, or a command ID repeated
    too_many_commands,   // more than QuickAccessToolbar::maximum_commands IDs
};

struct RibbonStateDiagnostic final {
    std::size_t line{};  // one-based
    RibbonStateIssue issue{RibbonStateIssue::malformed_line};
};

// A parse never fails as a whole: a malformed known entry is reported and
// its built-in default kept, as the configuration contract (§7) asks. A
// missing or unsupported header yields the default state.
struct RibbonStateParseResult final {
    RibbonState state{};
    std::vector<RibbonStateDiagnostic> diagnostics{};
};

// The stable text form, one entry per line, LF-terminated:
//
//     gui_forms.ribbon_state 1
//     quick_access.placement=above_ribbon      (or below_ribbon)
//     ribbon.minimized=false                   (or true)
//     quick_access.command=<command ID>        (one line per ID, in order)
//     <unrecognized entries, unchanged>
//
// Command IDs are written verbatim; an ID may not be empty or hold a line
// break or other control character. Throws std::invalid_argument for such an
// ID, a repeated ID, more than QuickAccessToolbar::maximum_commands IDs, or an
// unrecognized entry that holds a line break.
[[nodiscard]] std::string serialize_ribbon_state(const RibbonState& state);
[[nodiscard]] RibbonStateParseResult parse_ribbon_state(std::string_view text);

// The toolbar's part of the state, and its application to a toolbar.
// `minimized` is the Ribbon's, which applies it. Command IDs the toolbar
// does not declare survive apply and capture (the toolbar keeps them, inert);
// unrecognized entries are not the toolbar's, so capture returns none and a
// caller that keeps them copies them from the state it loaded. Applying sets
// the toolbar's shown commands and placement, emitting placement_changed
// when the placement changes.
[[nodiscard]] RibbonState capture_ribbon_state(const QuickAccessToolbar& toolbar, bool minimized);
void apply_ribbon_state(QuickAccessToolbar& toolbar, const RibbonState& state);

} // namespace gui_forms
