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
//
// Each lent text sits in its own LentTextBlock: a heap block that starts on
// an AddressSanitizer granule (8 bytes) and spans whole granules. ASan
// shadow describes a granule only as "the first k bytes addressable", so a
// region that starts or ends inside a live granule (a short text in a
// std::string's inline buffer) cannot be poisoned; a whole block can.

#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"

#include <cstddef>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <stdexcept>
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

// One lent text in a heap block of whole granules. assign() copies a text in
// (reclaiming the block first), retire() scribbles the whole block and, under
// AddressSanitizer, poisons it, and the next assign() or the destructor
// unpoisons it. new char[] returns the allocation's own start (no array
// cookie for a trivial type) at __STDCPP_DEFAULT_NEW_ALIGNMENT__, at least
// one granule. Neither copied nor moved: views into it are lent out.
class LentTextBlock final {
public:
    LentTextBlock() = default;
    LentTextBlock(const LentTextBlock&) = delete;
    LentTextBlock& operator=(const LentTextBlock&) = delete;
    ~LentTextBlock() { reclaim(); }

    // Copies text into the block, growing it to a granule multiple (at
    // least one granule) when it is too small; returns the copy. Throws
    // std::length_error when the rounded size overflows and std::bad_alloc
    // when growing fails; either leaves the block reclaimed and unchanged.
    [[nodiscard]] std::string_view assign(std::string_view text) {
        reclaim();
        if (bytes_ == nullptr || text.size() > capacity_) {
            const std::size_t wanted = text.size() > granule ? text.size() : granule;
            if (wanted > std::numeric_limits<std::size_t>::max() - (granule - 1U)) {
                throw std::length_error("lent text too long");
            }
            const std::size_t rounded = (wanted + (granule - 1U)) / granule * granule;
            std::unique_ptr<char[]> grown = std::make_unique_for_overwrite<char[]>(rounded);
            bytes_ = std::move(grown);
            capacity_ = rounded;
        }
        if (!text.empty()) {
            std::memcpy(bytes_.get(), text.data(), text.size());
        }
        size_ = text.size();
        const std::string_view copy(bytes_.get(), size_);
        return copy;
    }
    // Scribbles the whole block and poisons it; a block never assigned has
    // nothing to retire.
    void retire() noexcept {
        if (bytes_ == nullptr || poisoned_) {
            return;
        }
        std::memset(bytes_.get(), retired_byte, capacity_);
#if defined(GUI_FORMS_LENT_SCRATCH_POISONED)
        ASAN_POISON_MEMORY_REGION(bytes_.get(), capacity_);
#endif
        poisoned_ = true;
    }
    // Unpoisons the block if the last retire() poisoned it.
    void reclaim() noexcept {
        if (!poisoned_) {
            return;
        }
#if defined(GUI_FORMS_LENT_SCRATCH_POISONED)
        ASAN_UNPOISON_MEMORY_REGION(bytes_.get(), capacity_);
#endif
        poisoned_ = false;
    }

private:
    // AddressSanitizer's shadow granule, in bytes.
    static constexpr std::size_t granule = 8U;
    // What a retired block reads as when nothing poisons it.
    static constexpr unsigned char retired_byte = 0xA5U;

    // bytes_[0, size_) is the text, [size_, capacity_) spare; capacity_ is
    // a granule multiple, 0 only while bytes_ is null.
    std::unique_ptr<char[]> bytes_;
    std::size_t capacity_ = 0U;
    std::size_t size_ = 0U;
    bool poisoned_ = false;
};

#if GUI_FORMS_CHECKED_BORROWS
class LentCallTexts final {
public:
    LentCallTexts() = default;
    LentCallTexts(const LentCallTexts&) = delete;
    LentCallTexts& operator=(const LentCallTexts&) = delete;

    // Copies `text` into the next slot and returns the copy; pair with
    // retire() when the callback returns (LentCallText does).
    [[nodiscard]] std::string_view lend(std::string_view text) {
        if (depth_ == slots_.size()) {
            slots_.emplace_back();
        }
        const std::string_view lent = slots_[depth_].assign(text);
        depth_ = depth_ + 1U;
        return lent;
    }
    // Ends the innermost lend: scribbles its block and poisons it.
    void retire() noexcept {
        if (depth_ == 0U) {
            return;
        }
        depth_ = depth_ - 1U;
        slots_[depth_].retire();
    }

private:
    // slots_[0, depth_) are lent now, the rest retired or spare.
    std::deque<LentTextBlock> slots_;
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
