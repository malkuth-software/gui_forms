#pragma once

// The failures the core reports by type, so a boundary can name them without
// reading the message: a call on a thread that does not own the object, a
// mutation of an object that has been disposed, and a control tree nested
// past its limit. All are logic errors (the caller broke a rule it could
// have checked), so code that catches std::logic_error still catches them.
// A size or count limit is reported as std::length_error.

#include "gui_forms/export/application_export.hpp"
#include <stdexcept>
#include <string>

namespace gui_forms {

// Thrown when an object is used from a thread other than the one that owns
// it (a window's root thread).
class GUI_FORMS_API WrongThreadError final : public std::logic_error {
public:
    explicit WrongThreadError(const std::string& message) : std::logic_error(message) {}
};

// Thrown when a disposed object is mutated or attached.
class GUI_FORMS_API DisposedError final : public std::logic_error {
public:
    explicit DisposedError(const std::string& message) : std::logic_error(message) {}
};

// Thrown, before any change, when an attachment would nest a control tree
// deeper than maximum_control_tree_depth (control.hpp). A limit, so a
// std::length_error, which is also the std::logic_error the cycle refusal
// throws; the C ABI reports it as GF_STATUS_LIMIT_EXCEEDED.
class GUI_FORMS_API ControlTreeDepthError final : public std::length_error {
public:
    explicit ControlTreeDepthError(const std::string& message) : std::length_error(message) {}
};

} // namespace gui_forms
