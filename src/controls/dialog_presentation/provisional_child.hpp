#pragma once

#include "gui_forms/control.hpp"

namespace gui_forms {

// Puts `child` into `parent` for the length of a show() step, and takes it
// out again on every exit that does not commit, an exception included, so a
// show() that fails part way leaves the child where it was (without a
// parent) rather than in a layer or popup root nobody owns. Both are held
// until the guard ends. UI thread only.
class ProvisionalChild final {
public:
    ProvisionalChild(Control::Ptr parent, Control::Ptr child);
    ~ProvisionalChild();
    ProvisionalChild(const ProvisionalChild&) = delete;
    ProvisionalChild& operator=(const ProvisionalChild&) = delete;
    ProvisionalChild(ProvisionalChild&&) = delete;
    ProvisionalChild& operator=(ProvisionalChild&&) = delete;

    // Keeps the child in the parent when the guard ends.
    void commit() noexcept;
    // Takes the child out now and keeps it out when the guard ends.
    void undo() noexcept;

private:
    Control::Ptr parent_;
    Control::Ptr child_;
    bool settled_{false};
};

} // namespace gui_forms
