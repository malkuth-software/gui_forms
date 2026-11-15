// Types the fixtures name, defined in another file than their uses: the
// index reads every file of the run before any is checked.
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fixture {

enum class Mode : std::uint8_t { automatic, manual };

struct Point final {
    double x{};
    double y{};
};

// Owns through a member.
struct Named final {
    Mode mode{Mode::automatic};
    std::string name{};
};

// Owns through a member of a first-party type that owns.
struct Wrapper final {
    Point at{};
    Named named{};
};

// Owns through its own destructor, though every member is trivial.
class Handle final {
public:
    Handle() = default;
    ~Handle();
    int value() const noexcept { return value_; }
private:
    int value_{};
};

// Trivial: an explicitly defaulted destructor over trivial members.
struct Defaulted final {
    ~Defaulted() = default;
    std::array<Point, 4> corners{};
    std::optional<Mode> mode{};
    std::atomic<std::uint32_t> count{};
};

// A template: trivial or owning by its argument.
template <typename Value>
struct Sides final {
    Value left{};
    Value right{};
};

using NamedList = std::vector<Named>;
using PointPair = Sides<Point>;

// Owns through a base.
struct Derived final : public Named {
    Point origin{};
};

} // namespace fixture
