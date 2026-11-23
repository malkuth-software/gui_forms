// The script runner of the automation run (automation_run.hpp): a JSON
// Lines file of tool calls run by the program itself (study 15:
// `--mcp-script file.jsonl`), as the probes that once drove the Windows
// automation channel now do. Each line is one step: {"tool": name,
// ...arguments} or {"expect": "node"|"visual", ...}; blank lines and lines
// starting with '#' are skipped. The steps run as one batch (ruling 22), on
// the script's own thread, which drives each step on the UI thread; the
// reply goes to standard output, each image a capture step made is written
// beside the script as <script>.<n>.png (numbered from 1, in step order) and
// named on standard output, and every window is then asked to close.

#include "automation_run_internal.hpp"

#include <stxlib/threads.hpp>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>

namespace gui_forms::automation {
namespace {

// The longest script, in bytes and in steps (the batch's own limit).
constexpr std::size_t maximum_script_bytes = 4U * 1024U * 1024U;

class FlagStop final : public CallStop {
public:
    explicit FlagStop(const std::atomic<bool>& flag) noexcept : flag_(flag) {}
    [[nodiscard]] bool stopped() const noexcept override { return flag_.load(); }

private:
    const std::atomic<bool>& flag_;
};

void script_thread_main(const stxlib::StopSignal& signal, void* argument) noexcept;

} // namespace

struct ScriptRunner::State final {
    AutomationRun* run{};
    std::string path{};
    std::string batch_json{};
    std::size_t steps{};
    std::atomic<bool> stop{false};
    stxlib::Thread thread{};
    bool joinable{};
};

namespace {

void script_thread_main(const stxlib::StopSignal&, void* argument) noexcept {
    ScriptRunner::State& state = *static_cast<ScriptRunner::State*>(argument);
    const FlagStop stop(state.stop);
    try {
        if (!(*state.run).wait_for_window(&stop)) return;
        const ToolReply reply = (*state.run).drive("batch", state.batch_json, &stop);
        if (const ToolSuccess* const success = reply.success_value()) {
            std::fprintf(stdout, "%s\n", (*success).text.c_str());
            for (std::size_t index = 0U; index < (*success).images.size(); ++index) {
                const ToolImage& image = (*success).images[index];
                const std::string written = state.path + "." + std::to_string(index + 1U) + ".png";
                std::ofstream file(written, std::ios::binary | std::ios::trunc);
                file.write(reinterpret_cast<const char*>(image.bytes.data()),
                           static_cast<std::streamsize>(image.bytes.size()));
                std::fprintf(stdout, "image %zu: %s%s\n", index + 1U, written.c_str(),
                             file ? "" : " (not written)");
            }
            std::fprintf(stderr, "GUI.Forms: --mcp-script: %zu steps passed\n", state.steps);
        } else {
            const ToolFailure& failure = *reply.failure_value();
            std::fprintf(stdout, "%s\n", failure.message.c_str());
            std::fprintf(stderr, "GUI.Forms: --mcp-script failed: %s%s%s\n", tool_error_name(failure.error),
                         failure.hint.empty() ? "" : ": ", failure.hint.c_str());
        }
        std::fflush(stdout);
        if (!state.stop.load()) (*state.run).post_close_all();
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "GUI.Forms: --mcp-script stopped: %s\n", failure.what());
    }
}

} // namespace

std::unique_ptr<ScriptRunner> ScriptRunner::start(AutomationRun& run, const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot read " + path;
        return nullptr;
    }
    std::unique_ptr<State> state = std::make_unique<State>();
    (*state).run = &run;
    (*state).path = path;
    std::string& batch = (*state).batch_json;
    batch = "{\"steps\":[";
    std::string line{};
    std::size_t bytes = 0U;
    std::size_t number = 0U;
    while (std::getline(file, line)) {
        ++number;
        bytes += line.size() + 1U;
        if (bytes > maximum_script_bytes) {
            error = path + " is longer than 4 MiB";
            return nullptr;
        }
        std::size_t first = 0U;
        while (first < line.size() && (line[first] == ' ' || line[first] == '\t' || line[first] == '\r')) ++first;
        if (first == line.size() || line[first] == '#') continue;
        if (line[first] != '{') {
            error = path + ":" + std::to_string(number) + " is not a JSON object";
            return nullptr;
        }
        std::size_t last = line.size();
        while (last > first && (line[last - 1U] == ' ' || line[last - 1U] == '\t' || line[last - 1U] == '\r')) {
            --last;
        }
        if ((*state).steps != 0U) batch += ',';
        batch.append(line, first, last - first);
        ++(*state).steps;
    }
    batch += "]}";
    if ((*state).steps == 0U) {
        error = path + " has no steps";
        return nullptr;
    }
    // The batch checks each step (objects, tools, the step limit) when it
    // runs and replies with the first refusal.
    State* const started_state = state.get();
    stxlib::OperationResult<stxlib::Thread> started = stxlib::Thread::start(&script_thread_main, started_state);
    if (started.failed()) {
        error = "the script's thread did not start";
        return nullptr;
    }
    (*state).thread = std::move(*started.value());
    (*state).joinable = true;
    return std::unique_ptr<ScriptRunner>(new ScriptRunner(std::move(state)));
}

ScriptRunner::ScriptRunner(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

ScriptRunner::~ScriptRunner() {
    if (!state_ || !(*state_).joinable) return;
    (*state_).stop.store(true);
    static_cast<void>((*state_).thread.join());
    (*state_).joinable = false;
}

} // namespace gui_forms::automation
