// Function-local statics: an owning or unknown type is a finding; a type
// the index reads as trivial, or a never-destroyed holder, passes.
#include "fixture_types.hpp"

namespace fixture {

const Point& home() {
    static const Point point{};
    return point;
}

const Defaulted& defaults() {
    static const Defaulted value{};
    return value;
}

const Named& named() {
    // expect
    static const Named value{};
    return value;
}

const Wrapper& wrapper() {
    static const Wrapper* const value = new const Wrapper{};
    return *value;
}

const ThirdParty& foreign() {
    // expect
    static const ThirdParty value{};
    return value;
}

} // namespace fixture
