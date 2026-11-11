#pragma once

// A pre-order walk over a control subtree in explicit, fixed storage, for
// the walks that would otherwise call themselves once per level (house
// rule, owner 2026-11-11: no recursion over runtime-shaped data without an
// enforced depth). The storage is one frame per level,
// maximum_control_tree_depth of them on the walker: Control::attach_child
// refuses any tree deeper, so a walk never needs more. A control found at
// the last frame with children (which attach_child never builds) is
// visited, and its children are not.
//
// The walk reads each control's live child list by index and holds no
// owning pointer. Its users do not change the tree while walking (they
// clear flags, count, or forget registrations), so every control it
// returns stays owned by its parent's list until the walk ends.

#include "gui_forms/control/control/control.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace gui_forms::detail {

class SubtreeWalk final {
public:
    explicit SubtreeWalk(Control& root) noexcept : root_(&root) {}
    SubtreeWalk(const SubtreeWalk&) = delete;
    SubtreeWalk& operator=(const SubtreeWalk&) = delete;

    // The next control in pre-order: the root first, then each child's
    // subtree in painter order (children()). Null when the walk is done.
    [[nodiscard]] Control* next() noexcept;
    // The children of the control next() returned last are not visited.
    void skip_children() noexcept { descend_ = false; }

private:
    struct Frame final {
        Control* control{nullptr};
        std::size_t next_child{0U};
    };

    // The root, until next() first returns it.
    Control* root_{nullptr};
    // The control next() returned last, whose children open the next
    // frame on the following call unless skip_children() was called.
    Control* last_{nullptr};
    bool descend_{false};
    // Live frames: frames_[0, depth_).
    std::size_t depth_{0U};
    std::array<Frame, maximum_control_tree_depth> frames_{};
};

inline Control* SubtreeWalk::next() noexcept {
    if (root_ != nullptr) {
        last_ = root_;
        root_ = nullptr;
        descend_ = true;
        return last_;
    }
    if (last_ != nullptr && descend_ && depth_ < frames_.size() && !(*last_).children().empty()) {
        frames_[depth_] = Frame{last_, 0U};
        ++depth_;
    }
    last_ = nullptr;
    while (depth_ > 0U) {
        Frame& frame = frames_[depth_ - 1U];
        const std::span<const Control::Ptr> children = (*frame.control).children();
        if (frame.next_child >= children.size()) {
            --depth_;
            continue;
        }
        Control* const child = children[frame.next_child].get();
        ++frame.next_child;
        if (child == nullptr) continue;
        last_ = child;
        descend_ = true;
        return last_;
    }
    return nullptr;
}

} // namespace gui_forms::detail
