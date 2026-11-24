#include "automation_internal.hpp"

#include <array>

namespace gui_forms::automation::detail {
namespace {

struct KeyName final {
    const char* name{};
    std::uint32_t key{};
};

// Key names a model writes, matched in any case. Letters and digits are
// handled apart; digits are the HID usage codes of the top row (1 at 0x1E
// up to 9 at 0x26, then 0 at 0x27), which PhysicalKey follows.
constexpr std::array<KeyName, 36> key_names{{
    {"enter", PhysicalKey::enter},       {"return", PhysicalKey::enter},
    {"escape", PhysicalKey::escape},     {"esc", PhysicalKey::escape},
    {"backspace", PhysicalKey::backspace}, {"tab", PhysicalKey::tab},
    {"space", PhysicalKey::space},       {"minus", PhysicalKey::minus},
    {"equal", PhysicalKey::equal},       {"slash", PhysicalKey::slash},
    {"f1", PhysicalKey::f1},             {"f2", PhysicalKey::f2},
    {"f3", PhysicalKey::f3},             {"f4", PhysicalKey::f4},
    {"f5", PhysicalKey::f5},             {"f6", PhysicalKey::f6},
    {"f7", PhysicalKey::f7},             {"f8", PhysicalKey::f8},
    {"f9", PhysicalKey::f9},             {"f10", PhysicalKey::f10},
    {"f11", PhysicalKey::f11},           {"f12", PhysicalKey::f12},
    {"insert", PhysicalKey::insert},     {"home", PhysicalKey::home},
    {"pageup", PhysicalKey::page_up},    {"end", PhysicalKey::end},
    {"pagedown", PhysicalKey::page_down}, {"delete", PhysicalKey::delete_forward},
    {"right", PhysicalKey::right},       {"left", PhysicalKey::left},
    {"down", PhysicalKey::down},         {"up", PhysicalKey::up},
    {"menu", PhysicalKey::context_menu}, {"keypadminus", PhysicalKey::keypad_minus},
    {"keypadplus", PhysicalKey::keypad_plus}, {"del", PhysicalKey::delete_forward},
}};

char lower(char character) noexcept {
    if (character >= 'A' && character <= 'Z') return static_cast<char>(character - 'A' + 'a');
    return character;
}

bool same_name(std::string_view text, std::string_view name) noexcept {
    if (text.size() != name.size()) return false;
    for (std::size_t index = 0U; index < text.size(); ++index) {
        if (lower(text[index]) != name[index]) return false;
    }
    return true;
}

bool parse_key(std::string_view text, std::uint32_t& key) noexcept {
    if (text.size() == 1U) {
        const char character = lower(text[0]);
        if (character >= 'a' && character <= 'z') {
            key = PhysicalKey::a + static_cast<std::uint32_t>(character - 'a');
            return true;
        }
        if (character >= '1' && character <= '9') {
            key = 0x1EU + static_cast<std::uint32_t>(character - '1');
            return true;
        }
        if (character == '0') {
            key = 0x27U;
            return true;
        }
    }
    for (const KeyName& entry : key_names) {
        if (!same_name(text, entry.name)) continue;
        key = entry.key;
        return true;
    }
    return false;
}

struct ActionName final {
    const char* name{};
    SemanticAction action{SemanticAction::press};
};

constexpr std::array<ActionName, 10> action_names{{
    {"focus", SemanticAction::focus},
    {"press", SemanticAction::press},
    {"select", SemanticAction::select},
    {"increment", SemanticAction::increment},
    {"decrement", SemanticAction::decrement},
    {"expand", SemanticAction::expand},
    {"collapse", SemanticAction::collapse},
    {"show_menu", SemanticAction::show_menu},
    {"set_value", SemanticAction::set_value},
    {"close", SemanticAction::close},
}};

} // namespace

bool parse_modifier(std::string_view text, Modifier& modifier) {
    if (same_name(text, "shift")) {
        modifier = Modifier::shift;
    } else if (same_name(text, "ctrl") || same_name(text, "control")) {
        modifier = Modifier::control;
    } else if (same_name(text, "alt") || same_name(text, "option")) {
        modifier = Modifier::alt;
    } else if (same_name(text, "meta") || same_name(text, "cmd") || same_name(text, "command") ||
               same_name(text, "win") || same_name(text, "super")) {
        modifier = Modifier::meta;
    } else {
        return false;
    }
    return true;
}

bool parse_key_chord(std::string_view text, KeyChord& chord) {
    KeyChord made{};
    std::size_t start = 0U;
    while (true) {
        const std::size_t plus = text.find('+', start);
        // "Ctrl++" names the plus key: a '+' at the very end is the key.
        const bool last = plus == std::string_view::npos || plus + 1U == text.size();
        const std::string_view part = last ? text.substr(start) : text.substr(start, plus - start);
        if (last) {
            std::uint32_t key = 0U;
            if (part == "+") {
                key = PhysicalKey::keypad_plus;
            } else if (!parse_key(part, key)) {
                return false;
            }
            made.physical_key = key;
            chord = made;
            return true;
        }
        Modifier modifier = Modifier::none;
        if (!parse_modifier(part, modifier)) return false;
        made.modifiers = made.modifiers | modifier;
        start = plus + 1U;
    }
}

std::optional<SemanticAction> parse_semantic_action(std::string_view name) {
    for (const ActionName& entry : action_names) {
        if (name == entry.name) return std::optional<SemanticAction>(entry.action);
    }
    return std::nullopt;
}

std::uint64_t nanoseconds_of(FrameTime time) noexcept {
    const std::int64_t count =
        std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
    const std::uint64_t result = count < 0 ? 0U : static_cast<std::uint64_t>(count);
    return result;
}

} // namespace gui_forms::automation::detail
