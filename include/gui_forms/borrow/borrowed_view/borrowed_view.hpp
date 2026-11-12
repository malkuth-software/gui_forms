#pragma once

// Borrowed views and the storage that lends them (borrow sanitizer, stage 2;
// notes planning/borrow_sanitizer/, owner rulings 2026-11-11).
//
// A borrowed view names storage its owner keeps: a span of a member vector,
// a back link to the window a control is attached to. It stays valid only
// until the owner changes that storage's structure (an insertion, removal,
// reordering, replacement of the whole sequence, a re-attachment) or dies.
// AddressSanitizer reports such a use only when the old bytes were freed;
// an insertion within the capacity leaves them mapped, holding other
// elements.
//
// Two modes, chosen for the whole program by GUI_FORMS_CHECKED_BORROWS:
//
// - Ordinary build (0): every name below is the plain view or storage it
//   stands for, so a release build compiles exactly what it did before:
//   BorrowedSpan<T> is std::span<const T>, BorrowedMutableSpan<T> is
//   std::span<T>, BorrowedText is std::string_view, BorrowedPointer<T> is
//   T*, detail::LendingVector<T> is std::vector<T> and
//   detail::LendingPointer<T> is T*. The static_asserts at the end of this
//   header hold that.
// - Checked build (1, the sanitizer tree: tools/ci_build.py --sanitize sets
//   the CMake option GUI_FORMS_CHECKED_BORROWS): the lending storage keeps
//   an owner-state record (a generation and whether the owner lives), every
//   view carries a stamp (that record, the generation it was lent at, the
//   lending call's return address and thread), and every element access,
//   iteration step and conversion checks the stamp. A stale use ends the
//   process through detail::fail_borrow, which reports like fail_fast
//   (src/core/failure/fail_fast.hpp): the fault, where the view was lent,
//   where the owner last changed, and the stack.
//
// Which changes end the views lent so far (ruling 1): every structural
// change of the lending storage. A write to an element's fields in place
// (through operator[] or a mutable view) does not: the element keeps its
// identity. A reordering through mutable iterators (std::sort over
// begin()/end()) is not seen by the storage; the code that reorders calls
// detail::note_reordered afterwards.
//
// Thread rule: a view is used on the thread it was lent on (house style
// section 5: one execution context per piece of mutable state); the checked
// build reports a use on another thread.
//
// A view converts implicitly to the plain view in the checked build too
// (the stamp is checked once, at the conversion): a caller that keeps the
// plain std::span is not tracked after that. Spell the borrowed alias
// (Control::ChildView, BorrowedSpan<T>) to keep the check, and unchecked_view
// where the plain view is wanted on purpose (a callee that takes std::span
// and returns before the owner can change).
//
// Owners whose views cross threads (the live-surface leases, image pixel
// storage) lend from a lender made with BorrowThreading::any_thread: its
// generation is atomic and its views skip the thread check.
//
// Call-scope borrows: a detail::BorrowScope is the owner of what a raise or
// a call lends (an event payload's span, caller-lent text); the views it
// lends end when the scope does, so a handler that keeps one fails at its
// next use.
//
// Mixed modes are an ODR violation (the types differ). In the checked build
// every unit that includes this header refers to gui_forms_checked_borrows_v1,
// which only a checked library defines, so a checked consumer linked against
// an ordinary library fails at link time instead of misbehaving.
//
// Views are made from a pointer and a length, never from a container: the
// cp28 -ftime-trace study measured libc++'s constrained range constructor
// of std::span, instantiated by one-line `return member_;` accessors, at
// 23% of the build's compile time (notes results, time-trace-cp28).

#ifndef GUI_FORMS_CHECKED_BORROWS
#define GUI_FORMS_CHECKED_BORROWS 0
#endif

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if GUI_FORMS_CHECKED_BORROWS
#include <atomic>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <ranges>
#include <thread>
#endif

#if GUI_FORMS_CHECKED_BORROWS
// The checked library's link marker (see the header comment). Defined only
// in a library built with GUI_FORMS_CHECKED_BORROWS.
extern "C" {
GUI_FORMS_API extern const int gui_forms_checked_borrows_v1;
}
#endif

namespace gui_forms {

namespace detail {

// Which threads may use the views a lender lends. owner_thread (the
// default): the thread each view was lent on, the rule for UI-thread state
// (controls, surface roots, collections). any_thread: lenders whose storage
// is shared across threads (leases, immutable pixel storage); the checked
// build makes their record at construction and advances it atomically.
enum class BorrowThreading : std::uint8_t {
    owner_thread,
    any_thread,
};

} // namespace detail

#if GUI_FORMS_CHECKED_BORROWS

namespace detail {

// Every checked unit refers to the marker, so the link needs a checked
// library. `used` keeps the reference in an optimized unit that never reads
// it.
#if defined(__GNUC__) || defined(__clang__)
[[gnu::used]] static const int* const checked_borrows_link_marker = &gui_forms_checked_borrows_v1;
#endif

// Why a check failed.
enum class BorrowFault : std::uint8_t {
    // The owner changed the storage's structure after the view was lent.
    structurally_invalidated,
    // The owner (the lending storage) was destroyed.
    owner_destroyed,
    // The view was used on another thread than the one it was lent on.
    wrong_thread,
    // An index or a subview reached past the view's end.
    out_of_range,
    // A null borrowed pointer was dereferenced.
    null_dereference,
};

// One lending storage's state, shared by the storage and every stamp, so a
// stamp outlives the storage and can tell that it died. Written by the
// owner; atomic so an any_thread lender's views read it from other threads
// (relaxed: a view checks only its own lender's record).
struct GUI_FORMS_API BorrowOwnerState final {
    // Advanced by every structural change.
    std::atomic<std::uint64_t> generation{0U};
    // Cleared by the storage's destructor.
    std::atomic<bool> alive{true};
    // The return address of the last change (the code that made it).
    std::atomic<const void*> last_change_site{nullptr};
};

// What a view carries in the checked build. An empty stamp (no owner) is a
// view of nothing: a default view, which every check accepts.
struct GUI_FORMS_API BorrowStamp final {
    std::shared_ptr<const BorrowOwnerState> owner{};
    std::uint64_t generation{};
    // The return address of the lending call (the accessor's caller).
    const void* creation_site{};
    // The thread the view was lent on; the default id (no thread) for an
    // any_thread lender's view.
    std::thread::id thread{};
};

// Reports `fault` for the view stamped `stamp` (the fault, the creation
// site, the owner's last change, the stack) and ends the process. Never
// returns and never throws.
[[noreturn]] GUI_FORMS_API void fail_borrow(const BorrowStamp& stamp, BorrowFault fault) noexcept;

// Checks that the view stamped `stamp` is still valid here; reports through
// fail_borrow otherwise.
inline void require_current(const BorrowStamp& stamp) noexcept {
    if (!stamp.owner) return;
    const BorrowOwnerState& state = *stamp.owner;
    if (!state.alive.load(std::memory_order_relaxed)) fail_borrow(stamp, BorrowFault::owner_destroyed);
    if (state.generation.load(std::memory_order_relaxed) != stamp.generation) {
        fail_borrow(stamp, BorrowFault::structurally_invalidated);
    }
    const bool any_thread = stamp.thread == std::thread::id();
    if (!any_thread && std::this_thread::get_id() != stamp.thread) fail_borrow(stamp, BorrowFault::wrong_thread);
}

// The owner-state of one lending storage (ruling 5: a shared record). An
// owner_thread lender makes the record at the first loan, so storage that
// never lends allocates nothing; an any_thread lender makes it at
// construction (std::bad_alloc), so no thread races to make it. A copy lends
// afresh; an assignment ends the views lent so far.
class GUI_FORMS_API BorrowLender final {
public:
    BorrowLender() noexcept : state_(), threading_(BorrowThreading::owner_thread) {}
    explicit BorrowLender(BorrowThreading threading);
    BorrowLender(const BorrowLender& other);
    // A moved owner's storage now belongs to the new owner: the source's
    // views end (its record advances) and the record moves along, so an
    // owner's implicit move stays noexcept.
    BorrowLender(BorrowLender&& other) noexcept;
    BorrowLender& operator=(const BorrowLender&) noexcept {
        invalidate();
        return *this;
    }
    BorrowLender& operator=(BorrowLender&& other) noexcept {
        invalidate();
        other.invalidate();
        return *this;
    }
    ~BorrowLender();

    // Ends every view lent so far: a named storage operation (house style
    // section 1) called by each structural change. Out of line, so the site
    // it records is its caller.
    void invalidate() noexcept;
    // A stamp for a new view at the current generation. Out of line, so the
    // site it records is the lending accessor's caller. May allocate the
    // record (std::bad_alloc).
    [[nodiscard]] BorrowStamp stamp() const;

private:
    mutable std::shared_ptr<BorrowOwnerState> state_;
    BorrowThreading threading_;
};

// A random-access iterator over a borrowed view that checks the stamp at
// every dereference, so a loop over a view fails at the first element read
// after its owner changed (a callback inside the loop that edits the
// owner).
template <typename Element>
class BorrowedIterator final {
public:
    using iterator_concept = std::random_access_iterator_tag;
    using iterator_category = std::random_access_iterator_tag;
    using value_type = std::remove_cv_t<Element>;
    using difference_type = std::ptrdiff_t;
    using pointer = Element*;
    using reference = Element&;

    BorrowedIterator() noexcept : position_(nullptr), stamp_() {}
    BorrowedIterator(Element* position, BorrowStamp stamp) noexcept
        : position_(position), stamp_(std::move(stamp)) {}

    [[nodiscard]] reference operator*() const noexcept {
        require_current(stamp_);
        return *position_;
    }
    [[nodiscard]] reference operator[](difference_type offset) const noexcept {
        require_current(stamp_);
        return position_[offset];
    }
    BorrowedIterator& operator++() noexcept {
        ++position_;
        return *this;
    }
    BorrowedIterator operator++(int) noexcept {
        BorrowedIterator before = *this;
        ++position_;
        return before;
    }
    BorrowedIterator& operator--() noexcept {
        --position_;
        return *this;
    }
    BorrowedIterator operator--(int) noexcept {
        BorrowedIterator before = *this;
        --position_;
        return before;
    }
    BorrowedIterator& operator+=(difference_type offset) noexcept {
        position_ += offset;
        return *this;
    }
    BorrowedIterator& operator-=(difference_type offset) noexcept {
        position_ -= offset;
        return *this;
    }
    [[nodiscard]] friend BorrowedIterator operator+(BorrowedIterator iterator, difference_type offset) noexcept {
        iterator += offset;
        return iterator;
    }
    [[nodiscard]] friend BorrowedIterator operator+(difference_type offset, BorrowedIterator iterator) noexcept {
        iterator += offset;
        return iterator;
    }
    [[nodiscard]] friend BorrowedIterator operator-(BorrowedIterator iterator, difference_type offset) noexcept {
        iterator -= offset;
        return iterator;
    }
    [[nodiscard]] friend difference_type operator-(const BorrowedIterator& left,
                                                   const BorrowedIterator& right) noexcept {
        const difference_type distance = left.position_ - right.position_;
        return distance;
    }
    [[nodiscard]] friend bool operator==(const BorrowedIterator& left, const BorrowedIterator& right) noexcept {
        return left.position_ == right.position_;
    }
    [[nodiscard]] friend bool operator!=(const BorrowedIterator& left, const BorrowedIterator& right) noexcept {
        return left.position_ != right.position_;
    }
    [[nodiscard]] friend bool operator<(const BorrowedIterator& left, const BorrowedIterator& right) noexcept {
        return left.position_ < right.position_;
    }
    [[nodiscard]] friend bool operator>(const BorrowedIterator& left, const BorrowedIterator& right) noexcept {
        return left.position_ > right.position_;
    }
    [[nodiscard]] friend bool operator<=(const BorrowedIterator& left, const BorrowedIterator& right) noexcept {
        return left.position_ <= right.position_;
    }
    [[nodiscard]] friend bool operator>=(const BorrowedIterator& left, const BorrowedIterator& right) noexcept {
        return left.position_ >= right.position_;
    }

private:
    Element* position_;
    BorrowStamp stamp_;
};

} // namespace detail

// A checked view of `Element` (const-qualified for BorrowedSpan): the
// pointer and live length of the lender's storage and the stamp. Made only
// by detail::lend_view and the subviews.
template <typename Element>
class BasicBorrowedSpan final {
public:
    using element_type = Element;
    using value_type = std::remove_cv_t<Element>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = Element*;
    using reference = Element&;
    using iterator = detail::BorrowedIterator<Element>;
    using reverse_iterator = std::reverse_iterator<iterator>;

    // A view of nothing.
    BasicBorrowedSpan() noexcept : data_(nullptr), size_(0U), stamp_() {}
    // `size` elements at `data`, lent at `stamp`.
    BasicBorrowedSpan(Element* data, std::size_t size, detail::BorrowStamp stamp) noexcept
        : data_(data), size_(size), stamp_(std::move(stamp)) {}
    // A mutable view read as a const one (BorrowedMutableSpan<T> to
    // BorrowedSpan<T>), keeping the stamp.
    template <typename Other>
        requires(!std::is_same_v<Other, Element> && std::is_convertible_v<Other (*)[], Element (*)[]>)
    BasicBorrowedSpan(const BasicBorrowedSpan<Other>& other) noexcept
        : data_(other.data_), size_(other.size_), stamp_(other.stamp_) {}

    [[nodiscard]] std::size_t size() const noexcept {
        detail::require_current(stamp_);
        return size_;
    }
    [[nodiscard]] std::size_t size_bytes() const noexcept {
        detail::require_current(stamp_);
        return size_ * sizeof(Element);
    }
    [[nodiscard]] bool empty() const noexcept {
        detail::require_current(stamp_);
        return size_ == 0U;
    }
    [[nodiscard]] Element* data() const noexcept {
        detail::require_current(stamp_);
        return data_;
    }
    [[nodiscard]] Element& operator[](std::size_t index) const noexcept {
        detail::require_current(stamp_);
        if (index >= size_) detail::fail_borrow(stamp_, detail::BorrowFault::out_of_range);
        return data_[index];
    }
    [[nodiscard]] Element& front() const noexcept {
        Element& first = (*this)[0U];
        return first;
    }
    [[nodiscard]] Element& back() const noexcept {
        detail::require_current(stamp_);
        if (size_ == 0U) detail::fail_borrow(stamp_, detail::BorrowFault::out_of_range);
        return data_[size_ - 1U];
    }
    [[nodiscard]] iterator begin() const noexcept {
        detail::require_current(stamp_);
        return iterator(data_, stamp_);
    }
    [[nodiscard]] iterator end() const noexcept {
        detail::require_current(stamp_);
        return iterator(data_ + size_, stamp_);
    }
    [[nodiscard]] reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
    [[nodiscard]] reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }
    // The `count` elements from `offset` (to the end with dynamic_extent).
    [[nodiscard]] BasicBorrowedSpan subspan(std::size_t offset,
                                            std::size_t count = std::dynamic_extent) const noexcept {
        detail::require_current(stamp_);
        if (offset > size_) detail::fail_borrow(stamp_, detail::BorrowFault::out_of_range);
        const std::size_t available = size_ - offset;
        if (count != std::dynamic_extent && count > available) {
            detail::fail_borrow(stamp_, detail::BorrowFault::out_of_range);
        }
        const std::size_t length = count == std::dynamic_extent ? available : count;
        BasicBorrowedSpan part(data_ + offset, length, stamp_);
        return part;
    }
    [[nodiscard]] BasicBorrowedSpan first(std::size_t count) const noexcept {
        BasicBorrowedSpan part = subspan(0U, count);
        return part;
    }
    [[nodiscard]] BasicBorrowedSpan last(std::size_t count) const noexcept {
        detail::require_current(stamp_);
        if (count > size_) detail::fail_borrow(stamp_, detail::BorrowFault::out_of_range);
        BasicBorrowedSpan part(data_ + (size_ - count), count, stamp_);
        return part;
    }
    // The plain view, untracked from here on: the stamp is checked once. A
    // mutable view also converts to the plain const view (as std::span<T>
    // to std::span<const T>).
    template <typename Other>
        requires std::is_convertible_v<Element (*)[], Other (*)[]>
    operator std::span<Other>() const noexcept {
        detail::require_current(stamp_);
        const std::span<Other> plain(data_, size_);
        return plain;
    }

private:
    template <typename Other>
    friend class BasicBorrowedSpan;

    Element* data_;
    std::size_t size_;
    detail::BorrowStamp stamp_;
};

template <typename Element>
using BorrowedSpan = BasicBorrowedSpan<const Element>;
template <typename Element>
using BorrowedMutableSpan = BasicBorrowedSpan<Element>;

// A checked view of UTF-8 text: the pointer and length of the lender's
// characters and the stamp.
class GUI_FORMS_API BorrowedText final {
public:
    using iterator = detail::BorrowedIterator<const char>;

    BorrowedText() noexcept : data_(nullptr), size_(0U), stamp_() {}
    BorrowedText(const char* data, std::size_t size, detail::BorrowStamp stamp) noexcept
        : data_(data), size_(size), stamp_(std::move(stamp)) {}

    [[nodiscard]] std::size_t size() const noexcept {
        detail::require_current(stamp_);
        return size_;
    }
    [[nodiscard]] bool empty() const noexcept {
        detail::require_current(stamp_);
        return size_ == 0U;
    }
    [[nodiscard]] const char* data() const noexcept {
        detail::require_current(stamp_);
        return data_;
    }
    [[nodiscard]] char operator[](std::size_t index) const noexcept {
        detail::require_current(stamp_);
        if (index >= size_) detail::fail_borrow(stamp_, detail::BorrowFault::out_of_range);
        return data_[index];
    }
    [[nodiscard]] iterator begin() const noexcept {
        detail::require_current(stamp_);
        return iterator(data_, stamp_);
    }
    [[nodiscard]] iterator end() const noexcept {
        detail::require_current(stamp_);
        return iterator(data_ + size_, stamp_);
    }
    // The `count` bytes from `offset` (to the end with npos).
    [[nodiscard]] BorrowedText substr(std::size_t offset, std::size_t count = std::string_view::npos) const noexcept {
        detail::require_current(stamp_);
        if (offset > size_) detail::fail_borrow(stamp_, detail::BorrowFault::out_of_range);
        const std::size_t available = size_ - offset;
        const std::size_t length = count < available ? count : available;
        BorrowedText part(data_ + offset, length, stamp_);
        return part;
    }
    [[nodiscard]] std::size_t length() const noexcept {
        const std::size_t count = size();
        return count;
    }
    // The plain view, untracked from here on: the stamp is checked once.
    operator std::string_view() const noexcept {
        detail::require_current(stamp_);
        const std::string_view plain(data_, size_);
        return plain;
    }
    // Comparisons with text, as std::string_view's (each checks the stamp).
    friend bool operator==(const BorrowedText& left, const BorrowedText& right) noexcept {
        const std::string_view left_text = left;
        const std::string_view right_text = right;
        return left_text == right_text;
    }
    friend bool operator==(const BorrowedText& left, std::string_view right) noexcept {
        const std::string_view left_text = left;
        return left_text == right;
    }

private:
    const char* data_;
    std::size_t size_;
    detail::BorrowStamp stamp_;
};

// A checked borrowed pointer: the address and the stamp of the link it was
// read from. Converts to the plain pointer (checking the stamp) for
// comparisons and calls; `*pointer` checks the stamp and that it is not
// null. Made only by detail::lend_pointer, or null.
template <typename Target>
class BorrowedPointer final {
public:
    BorrowedPointer() noexcept : pointer_(nullptr), stamp_() {}
    BorrowedPointer(std::nullptr_t) noexcept : pointer_(nullptr), stamp_() {}
    BorrowedPointer(Target* pointer, detail::BorrowStamp stamp) noexcept
        : pointer_(pointer), stamp_(std::move(stamp)) {}

    // The plain pointer; the stamp is checked once.
    operator Target*() const noexcept {
        detail::require_current(stamp_);
        return pointer_;
    }
    [[nodiscard]] Target* get() const noexcept {
        detail::require_current(stamp_);
        return pointer_;
    }
    [[nodiscard]] Target& operator*() const noexcept {
        detail::require_current(stamp_);
        if (pointer_ == nullptr) detail::fail_borrow(stamp_, detail::BorrowFault::null_dereference);
        return *pointer_;
    }

private:
    Target* pointer_;
    detail::BorrowStamp stamp_;
};

namespace detail {

// A std::vector that lends checked views of its elements: the same
// operations, where every structural change (an insertion, erasure,
// assignment, swap, clear, resize or capacity change) ends the views lent
// so far. Element access and mutable iteration change nothing; a reorder
// through them is reported with note_reordered. Iterators are the vector's.
template <typename Element>
class LendingVector final {
public:
    using storage_type = std::vector<Element>;
    using value_type = Element;
    using size_type = typename storage_type::size_type;
    using difference_type = typename storage_type::difference_type;
    using reference = typename storage_type::reference;
    using const_reference = typename storage_type::const_reference;
    using pointer = typename storage_type::pointer;
    using const_pointer = typename storage_type::const_pointer;
    using iterator = typename storage_type::iterator;
    using const_iterator = typename storage_type::const_iterator;
    using reverse_iterator = typename storage_type::reverse_iterator;
    using const_reverse_iterator = typename storage_type::const_reverse_iterator;

    LendingVector() : storage_(), lender_() {}
    explicit LendingVector(size_type count) : storage_(count), lender_() {}
    LendingVector(size_type count, const Element& element) : storage_(count, element), lender_() {}
    LendingVector(storage_type elements) : storage_(std::move(elements)), lender_() {}
    LendingVector(std::initializer_list<Element> elements) : storage_(elements), lender_() {}
    LendingVector(const LendingVector& other) : storage_(other.storage_), lender_() {}
    // The moved storage keeps its address under this owner (house style
    // section 1): the source's views end.
    LendingVector(LendingVector&& other) noexcept : storage_(std::move(other.storage_)), lender_() {
        other.lender_.invalidate();
    }
    LendingVector& operator=(const LendingVector& other) {
        storage_ = other.storage_;
        lender_.invalidate();
        return *this;
    }
    LendingVector& operator=(LendingVector&& other) noexcept {
        storage_ = std::move(other.storage_);
        lender_.invalidate();
        other.lender_.invalidate();
        return *this;
    }
    LendingVector& operator=(storage_type elements) noexcept {
        storage_ = std::move(elements);
        lender_.invalidate();
        return *this;
    }
    LendingVector& operator=(std::initializer_list<Element> elements) {
        storage_ = elements;
        lender_.invalidate();
        return *this;
    }

    // Read access, as std::vector's.
    [[nodiscard]] size_type size() const noexcept { return storage_.size(); }
    [[nodiscard]] size_type max_size() const noexcept { return storage_.max_size(); }
    [[nodiscard]] size_type capacity() const noexcept { return storage_.capacity(); }
    [[nodiscard]] bool empty() const noexcept { return storage_.empty(); }
    [[nodiscard]] pointer data() noexcept { return storage_.data(); }
    [[nodiscard]] const_pointer data() const noexcept { return storage_.data(); }
    [[nodiscard]] reference operator[](size_type index) noexcept { return storage_[index]; }
    [[nodiscard]] const_reference operator[](size_type index) const noexcept { return storage_[index]; }
    [[nodiscard]] reference at(size_type index) { return storage_.at(index); }
    [[nodiscard]] const_reference at(size_type index) const { return storage_.at(index); }
    [[nodiscard]] reference front() noexcept { return storage_.front(); }
    [[nodiscard]] const_reference front() const noexcept { return storage_.front(); }
    [[nodiscard]] reference back() noexcept { return storage_.back(); }
    [[nodiscard]] const_reference back() const noexcept { return storage_.back(); }
    [[nodiscard]] iterator begin() noexcept { return storage_.begin(); }
    [[nodiscard]] const_iterator begin() const noexcept { return storage_.begin(); }
    [[nodiscard]] iterator end() noexcept { return storage_.end(); }
    [[nodiscard]] const_iterator end() const noexcept { return storage_.end(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return storage_.cbegin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return storage_.cend(); }
    [[nodiscard]] reverse_iterator rbegin() noexcept { return storage_.rbegin(); }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return storage_.rbegin(); }
    [[nodiscard]] reverse_iterator rend() noexcept { return storage_.rend(); }
    [[nodiscard]] const_reverse_iterator rend() const noexcept { return storage_.rend(); }

    // Structural changes: each ends the views lent so far, after the
    // vector's own operation (which may throw first and change nothing).
    void push_back(const Element& element) {
        storage_.push_back(element);
        lender_.invalidate();
    }
    void push_back(Element&& element) {
        storage_.push_back(std::move(element));
        lender_.invalidate();
    }
    template <typename... Arguments>
    reference emplace_back(Arguments&&... arguments) {
        reference added = storage_.emplace_back(std::forward<Arguments>(arguments)...);
        lender_.invalidate();
        return added;
    }
    void pop_back() noexcept {
        storage_.pop_back();
        lender_.invalidate();
    }
    iterator insert(const_iterator position, const Element& element) {
        const iterator inserted = storage_.insert(position, element);
        lender_.invalidate();
        return inserted;
    }
    iterator insert(const_iterator position, Element&& element) {
        const iterator inserted = storage_.insert(position, std::move(element));
        lender_.invalidate();
        return inserted;
    }
    iterator insert(const_iterator position, size_type count, const Element& element) {
        const iterator inserted = storage_.insert(position, count, element);
        lender_.invalidate();
        return inserted;
    }
    template <typename InputIterator>
    iterator insert(const_iterator position, InputIterator first, InputIterator last) {
        const iterator inserted = storage_.insert(position, first, last);
        lender_.invalidate();
        return inserted;
    }
    template <typename... Arguments>
    iterator emplace(const_iterator position, Arguments&&... arguments) {
        const iterator inserted = storage_.emplace(position, std::forward<Arguments>(arguments)...);
        lender_.invalidate();
        return inserted;
    }
    iterator erase(const_iterator position) {
        const iterator following = storage_.erase(position);
        lender_.invalidate();
        return following;
    }
    iterator erase(const_iterator first, const_iterator last) {
        const iterator following = storage_.erase(first, last);
        lender_.invalidate();
        return following;
    }
    template <typename InputIterator>
    void assign(InputIterator first, InputIterator last) {
        storage_.assign(first, last);
        lender_.invalidate();
    }
    void assign(size_type count, const Element& element) {
        storage_.assign(count, element);
        lender_.invalidate();
    }
    void clear() noexcept {
        storage_.clear();
        lender_.invalidate();
    }
    void reserve(size_type capacity) {
        storage_.reserve(capacity);
        lender_.invalidate();
    }
    void shrink_to_fit() {
        storage_.shrink_to_fit();
        lender_.invalidate();
    }
    void resize(size_type count) {
        storage_.resize(count);
        lender_.invalidate();
    }
    void resize(size_type count, const Element& element) {
        storage_.resize(count, element);
        lender_.invalidate();
    }
    void swap(storage_type& other) noexcept {
        storage_.swap(other);
        lender_.invalidate();
    }
    void swap(LendingVector& other) noexcept {
        storage_.swap(other.storage_);
        lender_.invalidate();
        other.lender_.invalidate();
    }
    // A reorder made through mutable iterators or element writes.
    void note_reordered() noexcept { lender_.invalidate(); }

    // The plain vector, read-only, for code that takes one.
    operator const storage_type&() const noexcept { return storage_; }
    // Moves the elements out (the views end).
    operator storage_type() && noexcept {
        storage_type moved = std::move(storage_);
        storage_.clear();
        lender_.invalidate();
        return moved;
    }

    [[nodiscard]] const BorrowLender& lender() const noexcept { return lender_; }

    friend bool operator==(const LendingVector& left, const LendingVector& right) {
        return left.storage_ == right.storage_;
    }
    friend bool operator==(const LendingVector& left, const storage_type& right) {
        return left.storage_ == right;
    }
    friend bool operator==(const storage_type& left, const LendingVector& right) {
        return left == right.storage_;
    }

private:
    storage_type storage_;
    BorrowLender lender_;
};

// A pointer member that lends checked copies of itself: every assignment
// ends the copies lent so far, and the member's destruction (its owner's)
// marks them dead.
template <typename Target>
class LendingPointer final {
public:
    LendingPointer() noexcept : pointer_(nullptr), lender_() {}
    LendingPointer(Target* pointer) noexcept : pointer_(pointer), lender_() {}
    LendingPointer(const LendingPointer& other) noexcept : pointer_(other.pointer_), lender_() {}
    LendingPointer& operator=(const LendingPointer& other) noexcept {
        pointer_ = other.pointer_;
        lender_.invalidate();
        return *this;
    }
    LendingPointer& operator=(Target* pointer) noexcept {
        pointer_ = pointer;
        lender_.invalidate();
        return *this;
    }

    // The owner's own reads: unchecked, as the raw member was.
    operator Target*() const noexcept { return pointer_; }
    [[nodiscard]] Target& operator*() const noexcept { return *pointer_; }

    [[nodiscard]] Target* get() const noexcept { return pointer_; }
    [[nodiscard]] const BorrowLender& lender() const noexcept { return lender_; }

private:
    Target* pointer_;
    BorrowLender lender_;
};

// A checked view of every element of `storage`.
template <typename Element>
[[nodiscard]] BorrowedSpan<Element> lend_view(const LendingVector<Element>& storage) {
    BorrowedSpan<Element> view(storage.data(), storage.size(), storage.lender().stamp());
    return view;
}
template <typename Element>
BorrowedSpan<Element> lend_view(const LendingVector<Element>&& storage) = delete;

// A checked mutable view of every element of `storage`.
template <typename Element>
[[nodiscard]] BorrowedMutableSpan<Element> lend_mutable_view(LendingVector<Element>& storage) {
    BorrowedMutableSpan<Element> view(storage.data(), storage.size(), storage.lender().stamp());
    return view;
}

// A checked copy of the link.
template <typename Target>
[[nodiscard]] BorrowedPointer<Target> lend_pointer(const LendingPointer<Target>& link) {
    BorrowedPointer<Target> borrowed(link.get(), link.lender().stamp());
    return borrowed;
}

// Records a reorder of `storage` made through its mutable iterators.
template <typename Element>
void note_reordered(LendingVector<Element>& storage) noexcept {
    storage.note_reordered();
}

// The plain vector inside `storage`, read-only, for code that copies or
// passes one (an explicit spelling of the conversion, where overloads would
// otherwise compete for it).
template <typename Element>
[[nodiscard]] const std::vector<Element>& lent_storage(const LendingVector<Element>& storage) noexcept {
    const std::vector<Element>& plain = storage;
    return plain;
}
template <typename Element>
const std::vector<Element>& lent_storage(const LendingVector<Element>&& storage) = delete;

// Views of storage an owner keeps beside a lender of its own (a lease's
// buffer, a layout workspace stamped per solve, a text store's characters):
// `size` elements at `data`, stamped by `lender`. The owner calls
// lender.invalidate() at each structural change. A public owner declares
// the lender [[no_unique_address]] (empty in the ordinary build).
template <typename Element>
[[nodiscard]] BorrowedSpan<Element> lend_view(const Element* data, std::size_t size, const BorrowLender& lender) {
    BorrowedSpan<Element> view(data, size, lender.stamp());
    return view;
}
template <typename Element>
[[nodiscard]] BorrowedMutableSpan<Element> lend_mutable_view(Element* data, std::size_t size,
                                                             const BorrowLender& lender) {
    BorrowedMutableSpan<Element> view(data, size, lender.stamp());
    return view;
}
[[nodiscard]] inline BorrowedText lend_text(std::string_view text, const BorrowLender& lender) {
    BorrowedText view(text.data(), text.size(), lender.stamp());
    return view;
}

// The owner of what one raise or call lends (house style section 5: the
// lifetime is the scope's, not a comment's). Declared before the record it
// fills; the views it lends end with it, so a handler that kept a payload's
// span or a caller's text fails at its next use (owner_destroyed). Lives on
// the raising thread.
class GUI_FORMS_API BorrowScope final {
public:
    BorrowScope() noexcept : lender_() {}
    BorrowScope(const BorrowScope&) = delete;
    BorrowScope& operator=(const BorrowScope&) = delete;

    template <typename Element>
    [[nodiscard]] BorrowedSpan<Element> lend_view(std::span<const Element> elements) const {
        BorrowedSpan<Element> view(elements.data(), elements.size(), lender_.stamp());
        return view;
    }
    template <typename Element>
    [[nodiscard]] BorrowedMutableSpan<Element> lend_mutable_view(std::span<Element> elements) const {
        BorrowedMutableSpan<Element> view(elements.data(), elements.size(), lender_.stamp());
        return view;
    }
    [[nodiscard]] BorrowedText lend_text(std::string_view text) const {
        BorrowedText view(text.data(), text.size(), lender_.stamp());
        return view;
    }

private:
    BorrowLender lender_;
};

} // namespace detail

// The plain view of `view`, valid in both builds: the checked build checks
// the stamp once here and the result is not tracked. For a callee that takes
// the plain view and returns before the owner can change.
template <typename Element>
[[nodiscard]] std::span<Element> unchecked_view(const BasicBorrowedSpan<Element>& view) noexcept {
    const std::span<Element> plain = view;
    return plain;
}
[[nodiscard]] inline std::string_view unchecked_view(const BorrowedText& view) noexcept {
    const std::string_view plain = view;
    return plain;
}
template <typename Target>
[[nodiscard]] Target* unchecked_view(const BorrowedPointer<Target>& pointer) noexcept {
    Target* const plain = pointer;
    return plain;
}

#else // the ordinary build: the plain views

template <typename Element>
using BorrowedSpan = std::span<const Element>;
template <typename Element>
using BorrowedMutableSpan = std::span<Element>;
using BorrowedText = std::string_view;
template <typename Target>
using BorrowedPointer = Target*;

namespace detail {

template <typename Element>
using LendingVector = std::vector<Element>;
template <typename Target>
using LendingPointer = Target*;

// The checked build's lender, empty here: owners that keep storage beside a
// lender hold one in both builds. A public class declares it
// [[no_unique_address]], so its ordinary layout and size are unchanged.
class GUI_FORMS_API BorrowLender final {
public:
    BorrowLender() noexcept {}
    explicit BorrowLender(BorrowThreading) noexcept {}
    void invalidate() noexcept {}
};

// The ordinary build's call scope: lends the plain views.
class GUI_FORMS_API BorrowScope final {
public:
    BorrowScope() noexcept {}
    BorrowScope(const BorrowScope&) = delete;
    BorrowScope& operator=(const BorrowScope&) = delete;

    template <typename Element>
    [[nodiscard]] BorrowedSpan<Element> lend_view(std::span<const Element> elements) const noexcept {
        return elements;
    }
    template <typename Element>
    [[nodiscard]] BorrowedMutableSpan<Element> lend_mutable_view(std::span<Element> elements) const noexcept {
        return elements;
    }
    [[nodiscard]] BorrowedText lend_text(std::string_view text) const noexcept { return text; }
};

template <typename Element>
[[nodiscard]] inline BorrowedSpan<Element> lend_view(const Element* data, std::size_t size,
                                                     const BorrowLender&) noexcept {
    const BorrowedSpan<Element> view(data, size);
    return view;
}
template <typename Element>
[[nodiscard]] inline BorrowedMutableSpan<Element> lend_mutable_view(Element* data, std::size_t size,
                                                                    const BorrowLender&) noexcept {
    const BorrowedMutableSpan<Element> view(data, size);
    return view;
}
[[nodiscard]] inline BorrowedText lend_text(std::string_view text, const BorrowLender&) noexcept { return text; }


// A view of every element of `storage`, from its pointer and length (not
// std::span's range constructor; see the header comment).
template <typename Element>
[[nodiscard]] inline BorrowedSpan<Element> lend_view(const LendingVector<Element>& storage) noexcept {
    const BorrowedSpan<Element> view(storage.data(), storage.size());
    return view;
}
template <typename Element>
BorrowedSpan<Element> lend_view(const LendingVector<Element>&& storage) = delete;

template <typename Element>
[[nodiscard]] inline BorrowedMutableSpan<Element> lend_mutable_view(LendingVector<Element>& storage) noexcept {
    const BorrowedMutableSpan<Element> view(storage.data(), storage.size());
    return view;
}

template <typename Target>
[[nodiscard]] inline BorrowedPointer<Target> lend_pointer(LendingPointer<Target> link) noexcept {
    return link;
}

template <typename Element>
inline void note_reordered(LendingVector<Element>&) noexcept {}

template <typename Element>
[[nodiscard]] inline const std::vector<Element>& lent_storage(const LendingVector<Element>& storage) noexcept {
    return storage;
}
template <typename Element>
const std::vector<Element>& lent_storage(const LendingVector<Element>&& storage) = delete;

} // namespace detail

// The plain view (the identity here; see the checked build).
template <typename Element>
[[nodiscard]] inline std::span<Element> unchecked_view(std::span<Element> view) noexcept {
    return view;
}
[[nodiscard]] inline std::string_view unchecked_view(std::string_view view) noexcept { return view; }
template <typename Target>
[[nodiscard]] inline Target* unchecked_view(Target* pointer) noexcept {
    return pointer;
}

// The ordinary build compiles exactly the plain views (ruling 2).
static_assert(std::is_same_v<BorrowedSpan<int>, std::span<const int>>);
static_assert(std::is_same_v<BorrowedMutableSpan<int>, std::span<int>>);
static_assert(std::is_same_v<BorrowedText, std::string_view>);
static_assert(std::is_same_v<BorrowedPointer<int>, int*>);
static_assert(std::is_same_v<detail::LendingVector<int>, std::vector<int>>);
static_assert(std::is_same_v<detail::LendingPointer<int>, int*>);
static_assert(sizeof(BorrowedSpan<int>) == sizeof(std::span<const int>));
static_assert(sizeof(BorrowedPointer<int>) == sizeof(int*));

#endif

} // namespace gui_forms

#if GUI_FORMS_CHECKED_BORROWS
// A borrowed view is a borrowed range: an iterator taken from a temporary
// view still names the lender's storage, as std::span's does.
namespace std::ranges {
template <typename Element>
inline constexpr bool enable_borrowed_range<gui_forms::BasicBorrowedSpan<Element>> = true;
}
#endif
