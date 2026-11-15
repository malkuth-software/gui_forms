// Namespace-scope objects of first-party types: each line after an
// `expect` comment is a finding, every other object passes.
#include "fixture_types.hpp"

namespace fixture {
namespace {

const Point origin{};
const Mode default_mode{Mode::manual};
const Defaulted defaulted{};
const Sides<bool> flags{};
const PointPair pair{};
Point* current = nullptr;
// expect
const Named unnamed{};
// expect
Wrapper wrapped{};
// expect
const Handle handle{};
// expect
const Sides<std::string> labels{};
// expect
NamedList names{};
// expect
const Derived derived{};
// expect
static std::array<Named, 2> both{};
constexpr Point corner{1.0, 2.0};
extern Named declared_elsewhere;

} // namespace
} // namespace fixture
