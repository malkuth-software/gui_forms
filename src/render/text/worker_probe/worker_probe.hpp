#pragma once

#include "text_shape_engine.hpp"
#include "gui_forms/document_view/types/document_view_types.hpp"

#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace gui_forms::render::worker_probe {

// Private experiment vocabulary, not a public prepared-layout contract.
struct Identity final {
    DocumentPageRequest page{};
    std::uint64_t layout_serial{};
    std::uint64_t provider_instance{};
    std::uint64_t provider_generation{};
    std::uint64_t font_set{};
    std::uint64_t font_generation{};
    std::uint64_t context_generation{};
    FontSpec font{};
    double scale{1.0};
    double wrap_width{};
    std::uint32_t tab_columns{4};
};
[[nodiscard]] bool same_identity(const Identity& left, const Identity& right) noexcept;

struct FontBytes final {
    std::shared_ptr<const std::vector<std::byte>> encoded{};
    std::optional<FontRole> role{};
    std::uint16_t weight{400};
    bool italic{};
    std::uint32_t face_index{};
};
struct FontSet final {
    std::uint64_t identity{1};
    std::uint64_t generation{1};
    std::array<FontBytes, 4> faces{};
};
struct Prepared final {
    Identity identity{};
    std::uint64_t authority_epoch{};
    std::string display_utf8{};
    text::ShapedText geometry{};
    std::shared_ptr<const FontSet> fonts{};
    std::array<FontFaceId, 4> face_ids{};
};
enum class Slot { empty, queued, running, ready };
enum class Outcome { success, busy, stale, invalid, failed, closed };
struct Snapshot final {
    Slot slot{Slot::empty};
    bool closing{};
    bool engine_alive{};
    bool joined{};
    bool desired{};
    std::uint64_t completed{};
    Outcome completion{Outcome::success};
};

// Control methods and displayed() belong to one foreground executor. Only
// immutable encoded font bytes/results cross to it from the owned worker.
// Supplied shared owners must originate immutable, with no mutable aliases.
// One replacement slot covers queued input, running work and an unreleased
// ready result. The displayed result is separately retained across failures.
class Worker final {
public:
    explicit Worker(std::shared_ptr<const FontSet> fonts);
    ~Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    void start();
    [[nodiscard]] Outcome desire(const Identity& identity);
    [[nodiscard]] Outcome submit(const Identity& identity, std::string_view input);
    void cancel();
    [[nodiscard]] Outcome publish();
    [[nodiscard]] Outcome discard();
    [[nodiscard]] Snapshot snapshot();
    // Borrow ends on successful publish or destruction; close preserves it.
    [[nodiscard]] const Prepared* displayed() const noexcept;
    void close();
private:
    struct Job final {
        Identity identity{};
        std::uint64_t authority_epoch{};
        std::string text{};
    };
    void run();
    [[nodiscard]] bool valid(const Identity& identity) const noexcept;
    std::shared_ptr<const FontSet> fonts_{};
    std::mutex mutex_{};
    std::condition_variable wake_{};
    std::thread thread_{};
    std::optional<Identity> desired_{};
    std::uint64_t authority_epoch_{};
    std::unique_ptr<Job> job_{};
    std::unique_ptr<const Prepared> result_{};
    std::unique_ptr<const Prepared> displayed_{};
    Snapshot state_{};
};

} // namespace gui_forms::render::worker_probe
