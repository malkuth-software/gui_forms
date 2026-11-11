#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"

#include "../../failure/fail_fast.hpp"

#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>
#include <type_traits>
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

BorrowLender::~BorrowLender() {
    if (state_) {
        (*state_).alive = false;
        ++(*state_).generation;
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void BorrowLender::invalidate() noexcept {
    if (!state_) return;
    ++(*state_).generation;
    (*state_).last_change_site = GUI_FORMS_BORROW_CALLER();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
BorrowStamp BorrowLender::stamp() const {
    if (!state_) state_ = std::make_shared<BorrowOwnerState>();
    BorrowStamp made{};
    made.owner = state_;
    made.generation = (*state_).generation;
    made.creation_site = GUI_FORMS_BORROW_CALLER();
    made.thread = std::this_thread::get_id();
    return made;
}

void fail_borrow(const BorrowStamp& stamp, BorrowFault fault) noexcept {
    std::fprintf(stderr, "GUI.Forms borrow sanitizer: %s\n", fault_text(fault));
    std::fputs("GUI.Forms borrow sanitizer: lent at ", stderr);
    write_code_address(stamp.creation_site);
    if (stamp.owner) {
        const BorrowOwnerState& state = *stamp.owner;
        std::fprintf(stderr, " at generation %llu; the owner is at generation %llu and %s\n",
                     static_cast<unsigned long long>(stamp.generation),
                     static_cast<unsigned long long>(state.generation), state.alive ? "alive" : "destroyed");
        std::fputs("GUI.Forms borrow sanitizer: the owner last changed at ", stderr);
        write_code_address(state.last_change_site);
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
