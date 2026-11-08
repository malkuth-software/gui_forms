// Opt-in provider-owned surrogate. Included by the existing native fixture;
// default CTest behavior remains the short visibility regression.
namespace {

constexpr std::size_t cost_window_count = 9U;
constexpr std::size_t cost_phase_count = 4U;
constexpr std::size_t cost_tick_count = 18U;
constexpr std::chrono::milliseconds cost_cadence(530);
constexpr std::chrono::seconds cost_interval(10);
constexpr std::chrono::milliseconds cost_lateness_limit(100);

struct CostExtents final {
    double native_dirty{0.0};
    double frame_damage{0.0};
    double destination{0.0};
    double clip{0.0};
    std::uint64_t source_bytes{0};
};

struct CostProbe final {
    // The native application owns models until run_macos_application returns.
    std::array<gui_forms::Window*, cost_window_count> models{};
    std::array<std::shared_ptr<gui_forms::TextBox>, cost_window_count> editors{};
    std::array<std::string, cost_window_count> before{};
    std::array<double, cost_phase_count> cpu_percent{};
    __weak NSWindow* primary{nil};
    std::size_t phase{0};
    std::size_t tick{0};
    std::size_t closed{0};
    bool running{false};
    bool completed{false};
    bool have_extents{false};
    CostExtents expected{};
    std::chrono::steady_clock::time_point started{};
    std::chrono::nanoseconds maximum_lateness{0};
    std::clock_t cpu_started{0};
    std::string failure{};
};

std::string cost_name(const std::size_t index) {
    const std::string name = "Paint cost window " + std::to_string(index);
    return name;
}

NSWindow* cost_window(const std::size_t index) {
    const std::string name = cost_name(index);
    NSString* title = [NSString stringWithUTF8String:name.c_str()];
    NSWindow* window = find_window(title);
    if (window == nil) throw std::runtime_error("missing cost experiment window");
    return window;
}

void cost_require(const bool condition, const char* const message) {
    if (!condition) throw std::runtime_error(message);
}

bool cost_rebuild(const std::size_t phase) noexcept {
    const bool rebuild = phase == 0U || phase == 3U; // ABBA crossover order.
    return rebuild;
}

std::uint64_t cost_delta(const std::string& before, const std::string& after,
                         const std::string& key) {
    const std::uint64_t first = counter(before, key);
    const std::uint64_t last = counter(after, key);
    cost_require(last >= first, "diagnostic counter decreased during interval");
    const std::uint64_t delta = last - first;
    return delta;
}

CostExtents read_cost_extents(const std::string& json) {
    const CostExtents result{
        .native_dirty = extent(json, "last_native_dirty_area"),
        .frame_damage = extent(json, "last_frame_damage_bounds_area"),
        .destination = extent(json, "last_cg_destination_area"),
        .clip = extent(json, "last_cg_clip_bounds_area"),
        .source_bytes = counter(json, "last_cg_source_bytes")};
    return result;
}

bool same_cost_extents(const CostExtents& left, const CostExtents& right) noexcept {
    const bool equal = std::abs(left.native_dirty - right.native_dirty) < 1.0e-6 &&
        std::abs(left.frame_damage - right.frame_damage) < 1.0e-6 &&
        std::abs(left.destination - right.destination) < 1.0e-6 &&
        std::abs(left.clip - right.clip) < 1.0e-6 && left.source_bytes == right.source_bytes;
    return equal;
}

void advance_cost(void* context);

void queue_cost(CostProbe& probe, const std::chrono::steady_clock::time_point deadline) {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const std::chrono::nanoseconds remaining =
        std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now);
    const std::int64_t delay = remaining.count() > 0 ? remaining.count() : 0;
    const dispatch_time_t release = dispatch_time(DISPATCH_TIME_NOW, delay);
    // One queued callback, borrowed stack owner survives the complete run loop.
    dispatch_after_f(release, dispatch_get_main_queue(), &probe, advance_cost);
}

void begin_cost_phase(CostProbe& probe) {
    for (std::size_t index = 0; index < cost_window_count; ++index) {
        NSWindow* window = cost_window(index);
        gui_forms::Window& model = *probe.models[index];
        const gui_forms::MetricsSnapshot metrics = model.metrics_snapshot();
        cost_require(metrics.renderer_name.find("bundled fonts (optional Unicode fallback)") !=
            std::string::npos, "bundled fonts not ready; paint-cost interval rejected");
        cost_require(!model.focused_control(), "surrogate must have no focused control");
        cost_require(!model.next_wake().has_value(), "surrogate acquired an autonomous deadline");
        if (index == 0U) {
            cost_require(window.isVisible && !model.occluded(), "primary must be visible");
            probe.primary = window;
        } else {
            cost_require(!window.isVisible && model.occluded(), "owned children must remain hidden");
        }
        probe.before[index] = [window.contentView hostJSON];
        model.reset_activity_metrics();
    }
    probe.tick = 0U;
    probe.maximum_lateness = std::chrono::nanoseconds(0);
    probe.cpu_started = std::clock();
    cost_require(probe.cpu_started != static_cast<std::clock_t>(-1), "process CPU clock unavailable");
    probe.started = std::chrono::steady_clock::now();
    probe.running = true;
    queue_cost(probe, probe.started + cost_cadence);
}

void cost_tick(CostProbe& probe) {
    NSWindow* window = probe.primary;
    cost_require(window != nil && window.isVisible, "primary disappeared during interval");
    gui_forms::Window& model = *probe.models[0];
    cost_require(!model.occluded() && !model.focused_control() && !model.next_wake().has_value(),
                 "focus, occlusion or autonomous scheduling changed during interval");
    const std::chrono::steady_clock::time_point expected = probe.started +
        cost_cadence * static_cast<std::int64_t>(probe.tick + 1U);
    const std::chrono::nanoseconds late = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - expected);
    probe.maximum_lateness = std::max(probe.maximum_lateness, late);
    cost_require(late <= cost_lateness_limit, "cadence exceeded declared lateness bound");
    const gui_forms::Rect damage{6.0, 4.0, 2.0, 20.0};
    if (cost_rebuild(probe.phase)) (*probe.editors[0]).invalidate(damage);
    NSView* view = window.contentView;
    [view collectDamage];
    [view setNeedsDisplayInRect:NSMakeRect(damage.x, damage.y, damage.width, damage.height)];
    [view displayIfNeeded];
    // Per-tick snapshot/validation overhead is included in process CPU in both
    // modes; it is not subtracted or presented as application-only CPU.
    const std::string json = [view hostJSON];
    const std::uint64_t draws = cost_delta(probe.before[0], json, "native_draw_count");
    cost_require(draws == probe.tick + 1U, "native draws diverged from controlled cadence");
    const CostExtents current = read_cost_extents(json);
    cost_require(current.native_dirty > 0.0 && current.frame_damage > 0.0 &&
        current.clip > 0.0 && current.destination > 0.0 && current.source_bytes > 0U,
        "presentation extent missing");
    if (!probe.have_extents) {
        probe.expected = current;
        probe.have_extents = true;
    } else {
        cost_require(same_cost_extents(current, probe.expected),
                     "presentation extents differ between ticks or modes");
    }
    ++probe.tick;
    if (probe.tick < cost_tick_count) {
        queue_cost(probe, probe.started + cost_cadence * static_cast<std::int64_t>(probe.tick + 1U));
    } else {
        queue_cost(probe, probe.started + cost_interval);
    }
}

void print_cost_phases(const std::string& before, const std::string& after,
                      const std::size_t phase, const std::size_t index) {
    const std::array<std::string_view, 6> names{
        "raster_prepare", "retained", "raster_finish", "cg_setup", "cg_draw", "cg_release"};
    for (const std::string_view name : names) {
        const std::string prefix = "paint_" + std::string(name);
        const std::string unsaturated = "\"" + prefix + "_saturated\":false";
        cost_require(before.find(unsaturated) != std::string::npos &&
                     after.find(unsaturated) != std::string::npos, "phase aggregate saturated");
        const std::uint64_t calls = cost_delta(before, after, prefix + "_calls");
        const std::uint64_t duration = cost_delta(before, after, prefix + "_nanoseconds");
        const std::uint64_t expected = index == 0U ? cost_tick_count : 0U;
        cost_require(calls == expected, "phase calls differ from controlled visible/hidden count");
        std::printf("cost-phase|interval=%zu|window=%zu|delta={\"phase\":\"%.*s\",\"calls\":%llu,\"nanoseconds\":%llu}\n",
            phase, index, static_cast<int>(name.size()), name.data(),
            static_cast<unsigned long long>(calls), static_cast<unsigned long long>(duration));
    }
}

void end_cost_phase(CostProbe& probe) {
    const std::chrono::steady_clock::time_point ended = std::chrono::steady_clock::now();
    const std::clock_t cpu_ended = std::clock();
    cost_require(cpu_ended != static_cast<std::clock_t>(-1) && cpu_ended >= probe.cpu_started,
                 "process CPU clock invalid or wrapped");
    const std::chrono::duration<double> elapsed = ended - probe.started;
    cost_require(elapsed.count() >= 10.0 && elapsed.count() <= 10.1,
                 "controlled interval duration outside declared bound");
    const double cpu_seconds = static_cast<double>(cpu_ended - probe.cpu_started) /
        static_cast<double>(CLOCKS_PER_SEC);
    probe.cpu_percent[probe.phase] = cpu_seconds * 100.0 / elapsed.count();
    for (std::size_t index = 0; index < cost_window_count; ++index) {
        NSWindow* window = cost_window(index);
        gui_forms::Window& model = *probe.models[index];
        const gui_forms::MetricsSnapshot metrics = model.metrics_snapshot();
        const std::string after = [window.contentView hostJSON];
        print_cost_phases(probe.before[index], after, probe.phase, index);
        cost_require(counter(after, "native_callback_faults") == 0U, "native callback fault");
        const std::uint64_t expected_paints = index == 0U ? cost_tick_count : 0U;
        const std::uint64_t expected_rebuilds = index == 0U && cost_rebuild(probe.phase) ? cost_tick_count : 0U;
        cost_require(metrics.paint_passes == expected_paints &&
            metrics.display_chunks_rebuilt == expected_rebuilds && metrics.frame_deadlines_fired == 0U &&
            metrics.measure_passes == 0U && metrics.arrange_passes == 0U,
            "paint, rebuild or layout metrics differ from controlled mode");
        const double expected_area = index == 0U
            ? probe.expected.frame_damage * static_cast<double>(cost_tick_count) : 0.0;
        cost_require(std::abs(metrics.painted_damage_area - expected_area) < 1.0e-6,
                     "painted area differs from controlled presentation extent");
        cost_require(model.occluded() == (index != 0U) && !model.focused_control(),
                     "window visibility/focus changed at interval boundary");
        const std::string metrics_json = metrics.to_json();
        std::printf("cost-window|interval=%zu|window=%zu|metrics=%s|host=%s\n",
                    probe.phase, index, metrics_json.c_str(), after.c_str());
    }
    std::printf("cost-interval|interval=%zu|mode=%s|ticks=%zu|wall_seconds=%.9f|cpu_seconds=%.9f|one_core_percent=%.9f|max_lateness_ns=%lld\n",
        probe.phase, cost_rebuild(probe.phase) ? "rebuild" : "exposure", probe.tick,
        elapsed.count(), cpu_seconds, probe.cpu_percent[probe.phase],
        static_cast<long long>(probe.maximum_lateness.count()));
    std::fflush(stdout);
    ++probe.phase;
    probe.running = false;
    if (probe.phase == cost_phase_count) {
        probe.completed = true;
        NSWindow* primary = probe.primary;
        [primary performClose:nil];
    } else {
        queue_cost(probe, std::chrono::steady_clock::now() + std::chrono::seconds(2));
    }
}

void advance_cost(void* context) {
    CostProbe& probe = *static_cast<CostProbe*>(context);
    try {
        if (!probe.running) begin_cost_phase(probe);
        else if (probe.tick < cost_tick_count) cost_tick(probe);
        else end_cost_phase(probe);
    } catch (const std::exception& error) {
        probe.failure = error.what();
        std::fprintf(stderr, "cost-experiment-rejected: %s\n", error.what());
        const std::chrono::duration<double> elapsed =
            std::chrono::steady_clock::now() - probe.started;
        std::fprintf(stderr,
            "cost-rejection-context|interval=%zu|mode=%s|completed_ticks=%zu|running=%d|elapsed_seconds=%.9f|max_lateness_ns=%lld\n",
            probe.phase, cost_rebuild(probe.phase) ? "rebuild" : "exposure",
            probe.tick, probe.running ? 1 : 0, elapsed.count(),
            static_cast<long long>(probe.maximum_lateness.count()));
        NSWindow* primary = probe.primary;
        if (primary == nil) primary = find_window(@"Paint cost window 0");
        try {
            if (primary != nil) {
                const std::string host = [primary.contentView hostJSON];
                std::fprintf(stderr, "cost-rejection-host|host=%s\n", host.c_str());
            }
        } catch (const std::exception& diagnostic_error) {
            std::fprintf(stderr, "cost-rejection-diagnostic-failed: %s\n", diagnostic_error.what());
        }
        [primary performClose:nil];
    }
}

class StartCostProbe final {
public:
    explicit StartCostProbe(CostProbe& probe) noexcept : probe_(probe) {}
    void operator()(const std::function<void()>, const std::function<void()>) const {
        queue_cost(probe_, std::chrono::steady_clock::now() + std::chrono::seconds(3));
    }
private:
    CostProbe& probe_;
};

class CountCostClosed final {
public:
    explicit CountCostClosed(CostProbe& probe) noexcept : probe_(probe) {}
    void operator()() const { ++probe_.closed; }
private:
    CostProbe& probe_;
};

int run_paint_cost_experiment() {
    CostProbe probe{};
    std::vector<gui_forms::host::MacApplicationWindow> windows{};
    windows.reserve(cost_window_count);
    for (std::size_t index = 0; index < cost_window_count; ++index) {
        gui_forms::host::MacApplicationWindow entry{};
        entry.stable_id = "cost.window." + std::to_string(index);
        if (index != 0U) entry.owner_id = "cost.window.0";
        probe.editors[index] = std::make_shared<gui_forms::TextBox>(
            gui_forms::StableId("cost.editor." + std::to_string(index)));
        (*probe.editors[index]).multiline = true;
        entry.model = std::make_unique<gui_forms::Window>(probe.editors[index], gui_forms::Size{640.0, 480.0});
        probe.models[index] = entry.model.get();
        const bool cleared = (*entry.model).request_focus({});
        cost_require(cleared, "could not clear surrogate control focus");
        entry.options.title = cost_name(index);
        entry.options.initial_size = {640.0, 480.0};
        entry.options.initially_visible = index == 0U;
        entry.options.print_metrics_on_close = false;
        entry.options.closed = CountCostClosed(probe);
        if (index == 0U) entry.options.visibility_ready = StartCostProbe(probe);
        windows.push_back(std::move(entry));
    }
    std::printf("cost-experiment=static-unfocused-provider-surrogate|order=ABBA|intervals=4x10s|cadence_ms=530|tick_validation_overhead=included\n");
    std::fflush(stdout);
    const int result = gui_forms::host::run_macos_application(std::move(windows));
    if (result != 0 || !probe.completed || probe.closed != cost_window_count || !probe.failure.empty()) return 1;
    std::printf("cost-experiment=accepted-comparable-intervals|cpu_rebuild_0=%.9f|cpu_exposure_1=%.9f|cpu_exposure_2=%.9f|cpu_rebuild_3=%.9f\n",
                probe.cpu_percent[0], probe.cpu_percent[1], probe.cpu_percent[2], probe.cpu_percent[3]);
    return 0;
}

} // namespace
