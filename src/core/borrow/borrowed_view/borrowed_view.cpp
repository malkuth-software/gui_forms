#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"

#include "../../failure/fail_fast.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// The checked build's owner-state and report (borrowed_view.hpp). The
// ordinary build compiles only the type checks at the end: every borrowed
// and lending type there is the plain one.

namespace gui_forms {

#if GUI_FORMS_CHECKED_BORROWS

namespace detail {
namespace {

// The return address of the function that calls the function this is
// written in, read where the compiler gives it: the code that changed the
// storage (invalidate) or lent the view (stamp). Null elsewhere; the report
// then says "unknown".
#if defined(__GNUC__) || defined(__clang__)
#define GUI_FORMS_BORROW_CALLER() __builtin_return_address(0)
#else
#define GUI_FORMS_BORROW_CALLER() nullptr
#endif

const char* fault_text(BorrowFault fault) noexcept {
    switch (fault) {
    case BorrowFault::structurally_invalidated:
        return "a borrowed view was used after a structural change of its owner";
    case BorrowFault::owner_destroyed:
        return "a borrowed view was used after its owner was destroyed";
    case BorrowFault::wrong_thread:
        return "a borrowed view was used on another thread than the one it was lent on";
    case BorrowFault::out_of_range:
        return "a borrowed view was indexed past its end";
    case BorrowFault::null_dereference:
        return "a null borrowed pointer was dereferenced";
    }
    return "a borrowed view failed an unknown check";
}

} // namespace

BorrowLender::BorrowLender(const BorrowThreading threading) : state_(), threading_(threading) {
    if (threading_ == BorrowThreading::any_thread) state_ = std::make_shared<BorrowOwnerState>();
}

BorrowLender::BorrowLender(const BorrowLender& other) : state_(), threading_(other.threading_) {
    if (threading_ == BorrowThreading::any_thread) state_ = std::make_shared<BorrowOwnerState>();
}

BorrowLender::BorrowLender(BorrowLender&& other) noexcept : state_(), threading_(other.threading_) {
    other.invalidate();
    state_ = std::move(other.state_);
}

BorrowLender::~BorrowLender() {
    if (state_) {
        (*state_).alive.store(false, std::memory_order_relaxed);
        (*state_).generation.fetch_add(1U, std::memory_order_relaxed);
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void BorrowLender::invalidate() noexcept {
    if (!state_) return;
    (*state_).generation.fetch_add(1U, std::memory_order_relaxed);
    (*state_).last_change_site.store(GUI_FORMS_BORROW_CALLER(), std::memory_order_relaxed);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
BorrowStamp BorrowLender::stamp() const {
    // An any_thread lender made its record at construction; only an
    // owner_thread lender makes it here, on its own thread.
    if (!state_) state_ = std::make_shared<BorrowOwnerState>();
    BorrowStamp made{};
    made.owner = state_;
    made.generation = (*state_).generation.load(std::memory_order_relaxed);
    made.creation_site = GUI_FORMS_BORROW_CALLER();
    if (threading_ == BorrowThreading::owner_thread) made.thread = std::this_thread::get_id();
    return made;
}

void fail_borrow(const BorrowStamp& stamp, BorrowFault fault) noexcept {
    std::fprintf(stderr, "GUI.Forms borrow sanitizer: %s\n", fault_text(fault));
    std::fputs("GUI.Forms borrow sanitizer: lent at ", stderr);
    write_code_address(stamp.creation_site);
    if (stamp.owner) {
        const BorrowOwnerState& state = *stamp.owner;
        const std::uint64_t owner_generation = state.generation.load(std::memory_order_relaxed);
        const bool alive = state.alive.load(std::memory_order_relaxed);
        std::fprintf(stderr, " at generation %llu; the owner is at generation %llu and %s\n",
                     static_cast<unsigned long long>(stamp.generation),
                     static_cast<unsigned long long>(owner_generation), alive ? "alive" : "destroyed");
        std::fputs("GUI.Forms borrow sanitizer: the owner last changed at ", stderr);
        write_code_address(state.last_change_site.load(std::memory_order_relaxed));
        std::fputs("\n", stderr);
    } else {
        std::fputs("\n", stderr);
    }
    std::fputs("GUI.Forms borrow sanitizer: stack:\n", stderr);
    write_fail_fast_stack();
    std::fflush(stderr);
    std::abort();
}

#undef GUI_FORMS_BORROW_CALLER

} // namespace detail

#else

// The ordinary build compiles the plain views (ruling 2): the same checks
// as the header's, kept here so this unit is not empty.
static_assert(std::is_same_v<BorrowedSpan<char>, std::span<const char>>);
static_assert(std::is_same_v<BorrowedPointer<char>, char*>);
static_assert(std::is_same_v<detail::LendingVector<char>, std::vector<char>>);

#endif

} // namespace gui_forms

#if GUI_FORMS_CHECKED_BORROWS
// The link marker every checked unit refers to (borrowed_view.hpp): only a
// checked library defines it.
extern "C" {
GUI_FORMS_API const int gui_forms_checked_borrows_v1 = 1;
}
#endif
