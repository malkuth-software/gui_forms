#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// Private bounded native measurement storage. Disabled in ordinary use.
struct MacDisplayLinkTrace final {
    std::array<std::uint64_t, 2048> timestamps{};
    std::size_t count{0};
    bool overflow{false};
};

// One drawRect: of a view whose draw trace is enabled (live resize probe,
// cp33). Times are steady-clock nanoseconds; sizes and the dirty rectangle
// are logical view units, origin top left (the view is flipped).
struct MacDrawRecord final {
    std::uint64_t started_ns{0};
    // From the start of drawRetainedRect: to the end of the copy into
    // AppKit's backing store.
    std::uint64_t duration_ns{0};
    double width{0.0};
    double height{0.0};
    double dirty_x{0.0};
    double dirty_y{0.0};
    double dirty_width{0.0};
    double dirty_height{0.0};
    // The raster reached the backing store.
    bool presented{false};
    // AppKit drew the view during a live resize (inLiveResize).
    bool live_resize{false};
};

// The view's draw trace: a bounded record of every draw while enabled and,
// when capture_directory is set, a PNG of every capture_stride-th presented
// frame (frame_NNNN.png, at most capture_limit) written inside the draw. A
// capturing run's timings include the encoding, so timing and capture are
// separate passes.
struct MacDrawTrace final {
    std::array<MacDrawRecord, 4096> records{};
    // Live length of records; capacity is records.size().
    std::size_t count{0};
    bool overflow{false};
    std::string capture_directory{};
    std::size_t capture_stride{1};
    std::size_t capture_limit{0};
    std::size_t presented{0};
    std::size_t captured{0};
};
