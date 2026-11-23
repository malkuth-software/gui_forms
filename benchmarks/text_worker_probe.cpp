#include "worker_probe.hpp"
#include "../src/platform/library_files.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <stxlib/clocks.hpp>
#include <stxlib/threads.hpp>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;
using namespace gui_forms::render::worker_probe;
using Clock = std::chrono::steady_clock;

void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

std::shared_ptr<const FontSet> load_fonts(const std::filesystem::path& root) {
    constexpr std::array<const char*, 4> names{
        "Carlito-Regular.ttf", "NotoSansArabic-Minimal.ttf",
        "NotoSansHebrew-Regular.ttf", "NotoEmoji-Regular.ttf"};
    FontSet set{};
    const std::size_t face_count = names.size();
    for (std::size_t index = 0; index < face_count; ++index) {
        const std::filesystem::path path = root / names[index];
        require(gui_forms::platform::names_regular_file(path), "font open");
        std::optional<std::vector<std::byte>> read = gui_forms::platform::read_file_bytes(path, 16 * 1024 * 1024);
        require(read.has_value() && !(*read).empty(), "bounded font input");
        std::vector<std::byte> bytes = std::move(*read);
        set.faces[index].encoded = std::make_shared<const std::vector<std::byte>>(std::move(bytes));
    }
    set.faces[0].role = FontRole::content;
    std::shared_ptr<const FontSet> result = std::make_shared<const FontSet>(std::move(set));
    return result;
}

Identity make_identity(std::uint64_t serial, std::size_t bytes) {
    Identity identity{};
    identity.page.revision = {1, 1};
    identity.page.serial = serial;
    identity.page.permitted.end = SourceByteOffset(static_cast<std::uint64_t>(bytes));
    identity.layout_serial = serial;
    identity.provider_instance = 1;
    identity.provider_generation = 1;
    identity.font_set = 1;
    identity.font_generation = 1;
    identity.context_generation = 1;
    identity.font = {FontRole::content, 16.0, 400, false};
    return identity;
}

void request_and_submit(Worker& worker, const Identity& identity, std::string_view input) {
    const Outcome desired = worker.desire(identity);
    require(desired == Outcome::success, "desire admitted");
    const Outcome submitted = worker.submit(identity, input);
    require(submitted == Outcome::success, "job admitted");
}

struct ForegroundSample final {
    std::uint64_t ticks{};
    std::uint64_t running_ticks{};
    double maximum_gap_ms{};
    double elapsed_ms{};
};
double milliseconds(Clock::duration duration) {
    const std::chrono::duration<double, std::milli> converted(duration);
    const double result = converted.count();
    return result;
}

ForegroundSample wait_ready(Worker& worker) {
    ForegroundSample sample{};
    const Clock::time_point start = Clock::now();
    Clock::time_point previous = start;
    for (;;) {
        const Snapshot state = worker.snapshot();
        const Clock::time_point now = Clock::now();
        const double gap = milliseconds(now - previous);
        sample.maximum_gap_ms = std::max(sample.maximum_gap_ms, gap);
        sample.elapsed_ms = milliseconds(now - start);
        if (state.slot == Slot::ready) { break; }
        require(sample.elapsed_ms < 10'000.0, "worker deadline exceeded");
        ++sample.ticks;
        if (state.slot == Slot::running) { ++sample.running_ticks; }
        previous = now;
        stxlib::sleep_for(stxlib::Duration::milliseconds(1));
    }
    return sample;
}

void require_displayed(Worker& worker, const Identity& identity) {
    const Prepared* displayed = worker.displayed();
    require(displayed != nullptr, "old displayed result retained");
    require(same_identity((*displayed).identity, identity), "displayed identity coherent");
}

void publish_ready(Worker& worker) {
    const Outcome published = worker.publish();
    require(published == Outcome::success, "publish ready result");
}

void check_identity_fields(const Identity& identity) {
    std::array<Identity, 22> changed{};
    changed.fill(identity);
    ++changed[0].page.revision.document;
    ++changed[1].page.revision.revision;
    ++changed[2].page.serial;
    ++changed[3].page.permitted.begin.value;
    ++changed[4].page.permitted.end.value;
    ++changed[5].page.viewport.anchor.value;
    changed[6].page.viewport.horizontal_dip += 1.0;
    ++changed[7].layout_serial;
    ++changed[8].provider_instance;
    ++changed[9].provider_generation;
    ++changed[10].font_set;
    ++changed[11].font_generation;
    ++changed[12].context_generation;
    changed[13].font.role = FontRole::control;
    changed[14].font.size += 1.0;
    ++changed[15].font.weight;
    changed[16].font.italic = !identity.font.italic;
    changed[17].font.letter_spacing += 0.5;
    changed[18].scale += 1.0;
    changed[19].wrap_width += 1.0;
    ++changed[20].tab_columns;
    changed[21].page.revision = {2, 2};
    for (const Identity& altered : changed) {
        require(!same_identity(identity, altered), "full identity rejects changed field");
    }
}

void exercise_lifecycle_edges(const std::shared_ptr<const FontSet>& fonts) {
    const Identity identity = make_identity(1, 5);
    {
        Worker unstarted(fonts);
        unstarted.close();
        unstarted.close();
        const Snapshot state = unstarted.snapshot();
        require(state.closing && state.joined && !state.engine_alive, "unstarted/repeated close");
        const Outcome rejected = unstarted.desire(identity);
        require(rejected == Outcome::closed, "unstarted close revokes new intent");
    }
    {
        Worker idle(fonts);
        idle.start();
        idle.close();
        idle.close();
        const Snapshot state = idle.snapshot();
        require(state.joined && !state.engine_alive && state.slot == Slot::empty, "idle close");
    }
    {
        Worker ready(fonts);
        ready.start();
        const Outcome desired = ready.desire(identity);
        require(desired == Outcome::success, "initial edge intent");
        Identity invalid = identity;
        invalid.font_generation = 2;
        const Outcome bad_intent = ready.desire(invalid);
        require(bad_intent == Outcome::invalid, "invalid intent refused");
        const std::string oversized(16'385, 'x');
        const Outcome bad_input = ready.submit(identity, oversized);
        require(bad_input == Outcome::invalid, "oversize input refused before copy");
        const Outcome valid_retry = ready.submit(identity, "hello");
        require(valid_retry == Outcome::success, "invalid operations preserved original authority/slot");
        static_cast<void>(wait_ready(ready));
        ready.close();
        ready.close();
        const Snapshot state = ready.snapshot();
        require(state.joined && state.slot == Slot::empty && ready.displayed() == nullptr,
                "ready close releases unpublished payload");
    }
}

void exercise(const std::shared_ptr<const FontSet>& fonts) {
    const std::string unit = "ABC שלום العربية 123 ";
    std::string slow{};
    slow.reserve(16'384);
    const std::size_t repeats = 16'384 / unit.size();
    for (std::size_t index = 0; index < repeats; ++index) { slow.append(unit); }
    const Identity first = make_identity(1, 5);
    const Identity second = make_identity(2, slow.size());
    const Identity third = make_identity(3, slow.size());
    const Identity newest = make_identity(4, slow.size());
    check_identity_fields(first);
    Worker worker(fonts);
    worker.start();
    request_and_submit(worker, first, "hello");
    static_cast<void>(wait_ready(worker));
    publish_ready(worker);
    require_displayed(worker, first);

    request_and_submit(worker, second, slow);
    worker.cancel();
    const Outcome repeated_desired = worker.desire(second);
    require(repeated_desired == Outcome::success, "same identity can express a new intent");
    const Outcome busy_after_cancel = worker.submit(second, slow);
    require(busy_after_cancel == Outcome::busy, "cancel retains in-flight slot");
    const ForegroundSample foreground = wait_ready(worker);
    require(foreground.running_ticks > 0, "foreground serviced while native shape ran");
    const Outcome cancelled_result = worker.publish();
    require(cancelled_result == Outcome::stale, "cancelled authority cannot revive under identical key");
    const Outcome third_desired = worker.desire(third);
    const Outcome newest_desired = worker.desire(newest);
    require(third_desired == Outcome::success && newest_desired == Outcome::success,
            "desired metadata coalesced");
    const Outcome premature = worker.submit(newest, slow);
    require(premature == Outcome::busy, "cancel does not free replacement slot");
    const Outcome rejected = worker.publish();
    require(rejected == Outcome::stale, "stale generation cannot publish");
    require_displayed(worker, first);
    const Outcome still_occupied = worker.submit(newest, slow);
    require(still_occupied == Outcome::busy, "ready stale payload still occupies slot");
    const Outcome released = worker.discard();
    require(released == Outcome::success, "release stale payload before slot reuse");
    const Outcome submitted = worker.submit(newest, slow);
    require(submitted == Outcome::success, "newest coalesced request admitted after release");
    static_cast<void>(wait_ready(worker));
    publish_ready(worker);
    require_displayed(worker, newest);

    Identity missing = make_identity(5, 5);
    missing.font.role = FontRole::monospace;
    request_and_submit(worker, missing, "hello");
    static_cast<void>(wait_ready(worker));
    const Outcome failed = worker.publish();
    require(failed == Outcome::failed, "missing dependency refuses publication");
    require_displayed(worker, newest);
    const Outcome failure_released = worker.discard();
    require(failure_released == Outcome::success, "failed completion acknowledged");

    const Identity closing = make_identity(6, slow.size());
    request_and_submit(worker, closing, slow);
    const Clock::time_point started = Clock::now();
    for (;;) {
        const Snapshot state = worker.snapshot();
        if (state.slot == Slot::running) { break; }
        require(state.slot != Slot::ready, "close fixture observes live work");
        require(milliseconds(Clock::now() - started) < 10'000.0, "close start deadline");
        stxlib::sleep_for(stxlib::Duration::milliseconds(1));
    }
    const Clock::time_point close_start = Clock::now();
    worker.close();
    const double close_ms = milliseconds(Clock::now() - close_start);
    const Snapshot closed = worker.snapshot();
    require(closed.joined && closed.closing && !closed.engine_alive && closed.slot == Slot::empty,
            "close drained and joined native owner");
    const Outcome after_close = worker.submit(closing, slow);
    require(after_close == Outcome::closed, "close revokes admission");
    require_displayed(worker, newest);
    const Prepared& retained = *worker.displayed();
    require(static_cast<bool>(retained.fonts), "result retains font table");
    const std::shared_ptr<const std::vector<std::byte>>& primary_bytes = (*retained.fonts).faces[0].encoded;
    require(primary_bytes && !(*primary_bytes).empty(),
            "encoded font lease survives native engine destruction");
    std::size_t glyph_capacity = 0;
    for (const gui_forms::render::text::ShapedFontRun& run : retained.geometry.runs) {
        const std::size_t capacity = run.glyphs.capacity();
        require(capacity <= std::numeric_limits<std::size_t>::max() - glyph_capacity,
                "glyph capacity sum bounded");
        glyph_capacity += capacity;
    }
    std::size_t font_capacity = 0;
    for (const FontBytes& face : (*retained.fonts).faces) {
        const std::size_t capacity = (*face.encoded).capacity();
        require(capacity <= std::numeric_limits<std::size_t>::max() - font_capacity,
                "font capacity sum bounded");
        font_capacity += capacity;
    }
    std::cout << "input_bytes,foreground_ticks,running_ticks,max_gap_ms,elapsed_ms,close_ms,completed\n"
              << slow.size() << ',' << foreground.ticks << ',' << foreground.running_ticks
              << ',' << foreground.maximum_gap_ms << ',' << foreground.elapsed_ms
              << ',' << close_ms << ',' << closed.completed << '\n';
    std::cout << "retained_text_capacity,retained_run_capacity_elements,retained_glyph_capacity_elements,shared_encoded_capacity_bytes\n"
              << retained.display_utf8.capacity() << ',' << retained.geometry.runs.capacity()
              << ',' << glyph_capacity << ',' << font_capacity << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: text_worker_probe FONT_DIRECTORY");
        const std::filesystem::path root(argv[1]);
        const std::shared_ptr<const FontSet> fonts = load_fonts(root);
        exercise_lifecycle_edges(fonts);
        exercise(fonts);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
