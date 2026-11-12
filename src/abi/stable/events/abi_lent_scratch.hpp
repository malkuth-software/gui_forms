#pragma once

// Call-scope text the library lends a C callback outside an event's
// EventFields (a custom control's set_property value, borrow sanitizer
// study section 2.5, C ABI row). The text is valid only during the callback.
// The plain build lends the caller's own storage (zero copy). The checked
// build (GUI_FORMS_CHECKED_BORROWS) copies it into a slot of this scratch,
// and when the callback returns scribbles the slot and, under
// AddressSanitizer, poisons it, so a callback that kept the pointer reads
// poisoned memory and ASan reports it in that caller's frame.
//
// One slot per nesting depth, so a callback that causes another lend (a
// set_property that sets another property) never overwrites the text its
// caller is still reading. The slots live in a deque, whose elements never
// move. One thread: the owner's.

#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"

#include <cstddef>
#include <cstring>
#include <deque>
#include <string>
#include <string_view>

#if GUI_FORMS_CHECKED_BORROWS
#if defined(__SANITIZE_ADDRESS__)
#define GUI_FORMS_LENT_SCRATCH_POISONED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GUI_FORMS_LENT_SCRATCH_POISONED 1
#endif
#endif
#endif
#if defined(GUI_FORMS_LENT_SCRATCH_POISONED)
#include <sanitizer/asan_interface.h>
#endif

namespace gui_forms::abi1 {

#if GUI_FORMS_CHECKED_BORROWS
class LentCallTexts final {
public:
    LentCallTexts() = default;
    LentCallTexts(const LentCallTexts&) = delete;
    LentCallTexts& operator=(const LentCallTexts&) = delete;
    // Unpoisons every retired slot before the strings are released.
    ~LentCallTexts() {
        std::size_t index = 0U;
        for (index = 0U; index < slots_.size(); ++index) reclaim(slots_[index]);
    }

    // Copies `text` into the next slot and returns the copy; pair with
    // retire() when the callback returns (LentCallText does).
    [[nodiscard]] std::string_view lend(std::string_view text) {
        if (depth_ == slots_.size()) slots_.emplace_back();
        Slot& slot = slots_[depth_];
        reclaim(slot);
        slot.text.assign(text);
        depth_ = depth_ + 1U;
        const std::string_view lent = slot.text;
        return lent;
    }
    // Ends the innermost lend: scribbles its characters and poisons them.
    void retire() noexcept {
        if (depth_ == 0U) return;
        depth_ = depth_ - 1U;
        Slot& slot = slots_[depth_];
        if (slot.text.empty()) return;
        std::memset(slot.text.data(), retired_byte, slot.text.size());
#if defined(GUI_FORMS_LENT_SCRATCH_POISONED)
        ASAN_POISON_MEMORY_REGION(slot.text.data(), slot.text.size());
#endif
        slot.poisoned_size = slot.text.size();
    }

private:
    struct Slot final {
        std::string text{};
        // The characters poisoned at the last retire, unpoisoned before the
        // string is touched again; 0 when none.
        std::size_t poisoned_size{};
    };

    // What a retired slot reads as when nothing poisons it.
    static constexpr unsigned char retired_byte = 0xA5U;

    static void reclaim(Slot& slot) noexcept {
        if (slot.poisoned_size == 0U) return;
#if defined(GUI_FORMS_LENT_SCRATCH_POISONED)
        ASAN_UNPOISON_MEMORY_REGION(slot.text.data(), slot.poisoned_size);
#endif
        slot.poisoned_size = 0U;
    }

    std::deque<Slot> slots_;
    std::size_t depth_ = 0U;
};
#else
class LentCallTexts final {
public:
    [[nodiscard]] std::string_view lend(std::string_view text) noexcept { return text; }
    void retire() noexcept {}
};
#endif

// One lend for the length of a callback: retires it when the scope ends,
// thrown through or not. Declared before the callback it covers.
class LentCallText final {
public:
    LentCallText(LentCallTexts& texts, std::string_view text) : texts_(texts), text_(texts.lend(text)) {}
    ~LentCallText() { texts_.retire(); }
    LentCallText(const LentCallText&) = delete;
    LentCallText& operator=(const LentCallText&) = delete;

    [[nodiscard]] std::string_view text() const& noexcept { return text_; }
    std::string_view text() const&& = delete;

private:
    LentCallTexts& texts_;
    std::string_view text_;
};

} // namespace gui_forms::abi1
