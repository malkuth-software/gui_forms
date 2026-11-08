// The creation-cost gate (APP_WINDOW_RULES_DESIGN K.2): the time and heap
// allocations of making a bare window, a popup from its owner and a form
// window of 22 controls with handlers, measured in interleaved rounds.
//
//   gui_forms_creation_benchmark [--rounds N] [--joined]            prints the costs
//   benchmarks/creation/creation_gate.py record BENCHMARK MANIFEST  writes the baseline
//   benchmarks/creation/creation_gate.py check BENCHMARK MANIFEST   fails on a regression
//
// Run it in Release on the M4, sequential with builds and other measurements.
// The allocations are counted by this executable's own operator new (the
// library has no allocation counters yet); a global replacement in a library
// is never admitted. Only the measuring thread counts, so worker threads do
// not make the count vary. Bytes are the bytes requested.
//
// --joined makes an application current first (Application::create), so the
// windows join it and share its resources: the cases are named *_joined.
//
// creation_gate.py runs this program as separate processes and compares
// with the committed baseline (benchmarks/creation/creation_baseline.txt).

#include "gui_forms/application.hpp"
#include "gui_forms/gui_forms.hpp"
#include "creation_form_form.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

thread_local bool counting_allocations = false;
thread_local std::uint64_t counted_allocations = 0U;
thread_local std::uint64_t counted_bytes = 0U;

void* counted_allocation(std::size_t size) {
    if (counting_allocations) {
        ++counted_allocations;
        counted_bytes += size;
    }
    void* allocation = std::malloc(size == 0U ? 1U : size);
    if (allocation == nullptr) throw std::bad_alloc();
    return allocation;
}

void* counted_aligned_allocation(std::size_t size, std::align_val_t alignment) {
    if (counting_allocations) {
        ++counted_allocations;
        counted_bytes += size;
    }
    void* allocation = nullptr;
    const std::size_t bytes = size == 0U ? 1U : size;
    const std::size_t align = std::max(static_cast<std::size_t>(alignment), sizeof(void*));
    if (posix_memalign(&allocation, align, bytes) != 0) throw std::bad_alloc();
    return allocation;
}

} // namespace

void* operator new(std::size_t size) {
    return counted_allocation(size);
}

void* operator new[](std::size_t size) {
    return counted_allocation(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return counted_aligned_allocation(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return counted_aligned_allocation(size, alignment);
}

void operator delete(void* allocation) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation) noexcept {
    std::free(allocation);
}

void operator delete(void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

void operator delete(void* allocation, std::align_val_t) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation, std::align_val_t) noexcept {
    std::free(allocation);
}

void operator delete(void* allocation, std::size_t, std::align_val_t) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation, std::size_t, std::align_val_t) noexcept {
    std::free(allocation);
}

namespace {

using namespace gui_forms;
using Clock = std::chrono::steady_clock;

enum class CreationCase : std::uint8_t {
    bare_window,
    popup,
    form_window,
};

constexpr std::array<CreationCase, 3> creation_cases{CreationCase::bare_window, CreationCase::popup,
                                                     CreationCase::form_window};

std::string_view case_name(CreationCase creation_case) {
    switch (creation_case) {
    case CreationCase::bare_window: return "bare_window";
    case CreationCase::popup: return "popup";
    case CreationCase::form_window: return "form_window";
    default: return "unknown";
    }
}

// One measured creation: its time and the allocations made during it.
struct Sample final {
    double nanoseconds{};
    std::uint64_t allocations{};
    std::uint64_t bytes{};
};

// A case's result over one run: the median time, the 10th to 90th
// percentile spread of its samples, and the most allocations of a sample.
struct CaseCost final {
    double median_nanoseconds{};
    double spread_nanoseconds{};
    std::uint64_t allocations{};
    std::uint64_t bytes{};
};

// The measured region: counts allocations on this thread from construction
// to finish().
class Measurement final {
public:
    Measurement() noexcept {
        counted_allocations = 0U;
        counted_bytes = 0U;
        counting_allocations = true;
        start_ = Clock::now();
    }

    Sample finish() noexcept {
        const Clock::time_point end = Clock::now();
        counting_allocations = false;
        const std::chrono::duration<double, std::nano> elapsed = end - start_;
        Sample sample{.nanoseconds = elapsed.count(), .allocations = counted_allocations, .bytes = counted_bytes};
        return sample;
    }

private:
    Clock::time_point start_{};
};

// The form's handlers.
// The program side of the form: one rule action, armed on each button.
class FormOwner final {
public:
    static void clicked(FormOwner& owner, const RuleFiring& firing) {
        static_cast<void>(firing);
        ++owner.clicks;
    }
    int clicks{};
};

using CreationForm = form_generated::CreationSettingsForm<FormOwner>;

// Arms FormOwner::clicked on `button`'s clicked interaction; a refusal is a
// defect of the benchmark.
void arm_click(RuleSet& rules, Button& button, FormOwner& owner) {
    const RuleAddResult added = on(rules, trigger::interaction(button, button.clicked()), owner, &FormOwner::clicked);
    if (added.refusal != RuleRefusal::none) std::abort();
}

constexpr Size bare_size{640.0, 480.0};

// The window and an empty root.
Sample measure_bare_window() {
    std::optional<Window> window;
    Measurement measurement;
    const Control::Ptr root = make_control<Panel>(StableId("creation.bare"));
    window.emplace(root, bare_size);
    const Sample sample = measurement.finish();
    return sample;
}

// A popup panel opened from a control of a live window. Outside a run it
// opens as an overlay (NativePopupFallback::overlay), the path a popup takes
// until the native popup lands for 0.5. The owner window is made outside
// the measurement.
Sample measure_popup() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("creation.owner"));
    const std::shared_ptr<Button> owner = make_control<Button>(StableId("creation.owner.button"));
    (*root).add_child(owner);
    Window window(root, bare_size);
    window.perform_layout();
    std::optional<NativePopupToken> token;
    Measurement measurement;
    const Control::Ptr content = make_control<Panel>(StableId("creation.popup"));
    token.emplace(window.open_native_popup(owner, content, Rect{20.0, 20.0, 240.0, 160.0}, NativePopupOptions{}));
    const Sample sample = measurement.finish();
    (*token).disconnect();
    return sample;
}

// The generated form and its window, sharing the application's resources
// (made once, as a program does today).
Sample measure_form_window(const std::shared_ptr<ApplicationResources>& resources) {
    FormOwner owner{};
    std::optional<CreationForm> form;
    std::optional<Window> window;
    Measurement measurement;
    form.emplace();
    (*form).initialize_component(owner);
    window.emplace((*form).root_control(), CreationForm::window_client_size.value_or(bare_size), resources);
    (*form).attach_buttons(*window);
    // The five button rules the form's handler references used to be.
    RuleSet& rules = (*window).rules();
    arm_click(rules, *(*form).creation_apply, owner);
    arm_click(rules, *(*form).creation_reset, owner);
    arm_click(rules, *(*form).creation_help, owner);
    arm_click(rules, *(*form).creation_ok, owner);
    arm_click(rules, *(*form).creation_cancel, owner);
    const Sample sample = measurement.finish();
    return sample;
}

Sample measure(CreationCase creation_case, const std::shared_ptr<ApplicationResources>& resources) {
    switch (creation_case) {
    case CreationCase::bare_window: return measure_bare_window();
    case CreationCase::popup: return measure_popup();
    case CreationCase::form_window: return measure_form_window(resources);
    default: return Sample{};
    }
}

double percentile(std::vector<double>& values, double fraction) {
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1U));
    return values[index];
}

// One run: a warm-up round, then `rounds` rounds of every case in turn.
std::array<CaseCost, 3> run(std::size_t rounds, const std::shared_ptr<ApplicationResources>& resources) {
    for (const CreationCase creation_case : creation_cases) static_cast<void>(measure(creation_case, resources));
    std::array<std::vector<double>, 3> times{};
    std::array<CaseCost, 3> costs{};
    for (std::vector<double>& case_times : times) case_times.reserve(rounds);
    for (std::size_t round = 0U; round < rounds; ++round) {
        for (std::size_t index = 0U; index < creation_cases.size(); ++index) {
            const Sample sample = measure(creation_cases[index], resources);
            times[index].push_back(sample.nanoseconds);
            costs[index].allocations = std::max(costs[index].allocations, sample.allocations);
            costs[index].bytes = std::max(costs[index].bytes, sample.bytes);
        }
    }
    for (std::size_t index = 0U; index < creation_cases.size(); ++index) {
        costs[index].median_nanoseconds = percentile(times[index], 0.5);
        const double low = percentile(times[index], 0.1);
        const double high = percentile(times[index], 0.9);
        costs[index].spread_nanoseconds = high - low;
    }
    return costs;
}

void print_costs(const std::array<CaseCost, 3>& costs, bool joined) {
    for (std::size_t index = 0U; index < creation_cases.size(); ++index) {
        std::string name(case_name(creation_cases[index]));
        if (joined) name += "_joined";
        std::printf("%-12s median %10.0f ns  p10-p90 %9.0f ns  allocations %6llu  bytes %9llu\n", name.c_str(),
                    costs[index].median_nanoseconds, costs[index].spread_nanoseconds,
                    static_cast<unsigned long long>(costs[index].allocations),
                    static_cast<unsigned long long>(costs[index].bytes));
    }
}

} // namespace

int main(int argc, char** argv) {
    std::size_t rounds = 200U;
    bool joined = false;
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--joined") joined = true;
    }
    for (int index = 1; index + 1 < argc; index += 2) {
        const std::string_view option = argv[index];
        if (option == "--rounds") rounds = static_cast<std::size_t>(std::strtoull(argv[index + 1], nullptr, 10));
    }
    if (rounds == 0U) rounds = 1U;
    // The application, when joined, is made before anything measured and
    // outlives every window; its resources are the form window's too.
    ApplicationCreateResult created{};
    if (joined) {
        created = Application::create(ApplicationConfig{});
        if (!created) {
            std::fprintf(stderr, "the application was refused\n");
            return 2;
        }
    }
    const std::shared_ptr<ApplicationResources> resources =
        joined ? (*created.application).resources() : std::make_shared<ApplicationResources>();
    const std::array<CaseCost, 3> costs = run(rounds, resources);
    print_costs(costs, joined);
    return 0;
}
