// The process's family stack registry (gui_forms/typography/font_families/font_families.hpp).
//
// The registry is a fixed table of maximum_font_family_stacks entries in static storage, so it
// outlives every theme and window that holds a number. Registration takes the mutex, writes the
// next free entry and then publishes the new count with a release store; a reader loads the count
// with acquire and reads only entries below it, which are never written again. Readers therefore
// take no lock (the text engine reads a stack for every shaped run).

#include "gui_forms/typography/font_families/font_families.hpp"
#include "gui_forms/typography/app_fonts/app_fonts.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace gui_forms {
namespace {

struct FontFamilyStackTable final {
    std::mutex registration{};
    std::array<FontFamilyStack, maximum_font_family_stacks> stacks{};
    // How many entries of `stacks` are numbered; entry n has number n + 1.
    std::atomic<std::size_t> published{0U};
};

[[nodiscard]] FontFamilyStackTable& stack_table() noexcept {
    static FontFamilyStackTable table{};
    return table;
}

// Whether `families` is a list a stack may hold: one to maximum_font_stack_families drawable
// families (bundled, or live application families), none zero, none twice.
[[nodiscard]] bool admissible_stack(std::span<const FontFamilyId> families) noexcept {
    if (families.empty() || families.size() > maximum_font_stack_families) return false;
    for (std::size_t index = 0U; index < families.size(); ++index) {
        const FontFamilyId family = families[index];
        if (family.value == 0U || !font_family_drawable(family)) return false;
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (families[earlier] == family) return false;
        }
    }
    return true;
}

} // namespace

FontFamilyStackId register_font_family_stack(std::span<const FontFamilyId> families) {
    FontFamilyStackId result{};
    if (!admissible_stack(families)) return result;
    FontFamilyStack candidate{};
    for (std::size_t index = 0U; index < families.size(); ++index) candidate.families[index] = families[index];
    candidate.count = families.size();
    FontFamilyStackTable& table = stack_table();
    const std::lock_guard<std::mutex> lock(table.registration);
    const std::size_t published = table.published.load(std::memory_order_relaxed);
    for (std::size_t index = 0U; index < published; ++index) {
        if (!(table.stacks[index] == candidate)) continue;
        result.value = static_cast<std::uint16_t>(index + 1U);
        return result;
    }
    if (published == maximum_font_family_stacks) return result;
    table.stacks[published] = candidate;
    table.published.store(published + 1U, std::memory_order_release);
    result.value = static_cast<std::uint16_t>(published + 1U);
    return result;
}

FontFamilyStack font_family_stack(FontFamilyStackId stack) noexcept {
    FontFamilyStack result{};
    if (stack.value == 0U) return result;
    const FontFamilyStackTable& table = stack_table();
    const std::size_t published = table.published.load(std::memory_order_acquire);
    const std::size_t index = static_cast<std::size_t>(stack.value) - 1U;
    if (index >= published) return result;
    result = table.stacks[index];
    return result;
}

bool valid_font_family_stack(FontFamilyStackId stack) noexcept {
    if (stack.value == 0U) return true;
    const std::size_t published = stack_table().published.load(std::memory_order_acquire);
    const bool valid = static_cast<std::size_t>(stack.value) <= published;
    return valid;
}

FontFamilyOrder font_family_order(const FontSpec& font) noexcept {
    FontFamilyOrder order{};
    if (font.family.value != 0U && font_family_drawable(font.family)) {
        order.families[order.count] = font.family;
        order.count = order.count + 1U;
    }
    const FontFamilyStack stack = font_family_stack(font.stack);
    for (std::size_t index = 0U; index < stack.count; ++index) {
        const FontFamilyId family = stack.families[index];
        if (!font_family_drawable(family)) continue;
        if (font_family_position(order, family)) continue;
        order.families[order.count] = family;
        order.count = order.count + 1U;
    }
    return order;
}

} // namespace gui_forms
