#pragma once

// The failures the core reports by type, so a boundary can name them without
// reading the message: a call on a thread that does not own the object, and
// a mutation of an object that has been disposed. Both are logic errors (the
// caller broke a rule it could have checked), so code that catches
// std::logic_error still catches them. A size or count limit is reported as
// std::length_error.

#include <stdexcept>
#include <string>

namespace gui_forms {

// Thrown when an object is used from a thread other than the one that owns
// it (a window's root thread).
class WrongThreadError final : public std::logic_error {
public:
    explicit WrongThreadError(const std::string& message) : std::logic_error(message) {}
};

// Thrown when a disposed object is mutated or attached.
class DisposedError final : public std::logic_error {
public:
    explicit DisposedError(const std::string& message) : std::logic_error(message) {}
};

} // namespace gui_forms
