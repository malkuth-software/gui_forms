#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace file_manager_demoboard {

struct FixtureObject final {
    std::string id;
    std::string name;
    std::string kind;
    std::string size;
    std::string icon_role;
    bool selected{};
};

struct CapabilityEntry final {
    std::string id;
    std::string state;
    std::string detail;
};

struct FixturePath final {
    std::string id;
    std::string path;
    std::vector<std::string> segments;
    bool available{true};
};

struct FixturePathCompletion final {
    std::string id;
    std::string path;
};

struct FixturePathResolution final {
    bool valid{};
    std::string expanded_path;
    std::string destination_id;
    std::string explanation;
};

struct FixtureSearchResult final {
    std::string id;
    std::string name;
    std::string location;
    std::string metadata;
    std::string excerpt;
    std::string metric;
    std::vector<std::string> providers;
    std::string provider_detail;
    std::string icon_role;
    std::string destination_id;
    std::string object_id;
    bool available{true};
    bool stale{};
    bool default_pinned{};
};

struct FixtureCriterionField final {
    std::string id;
    std::string name;
    std::string value;
    std::vector<std::string> choices;
    bool editable_text{};
    bool required{};
    double width_weight{1.0};
};

struct FixtureCriterionModule final {
    std::string id;
    std::string name;
    std::vector<FixtureCriterionField> fields;
    std::string status;
    bool enabled{true};
    bool expensive{};
    bool staged{};
    std::int32_t priority{};
};

class FixtureCatalogue final {
public:
    [[nodiscard]] static const FixtureCatalogue& instance();
    [[nodiscard]] std::string_view id() const& noexcept { return id_; }
    std::string_view id() const&& = delete;
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] std::string_view current_path() const& noexcept {
        return current_path_;
    }
    std::string_view current_path() const&& = delete;
    [[nodiscard]] std::span<const FixtureObject> project_objects() const& noexcept {
        return project_objects_;
    }
    std::span<const FixtureObject> project_objects() const&& = delete;
    [[nodiscard]] const FixturePath& current_path_stack() const noexcept {
        return current_path_stack_;
    }
    [[nodiscard]] std::span<const FixturePath> recent_paths() const& noexcept {
        return recent_paths_;
    }
    std::span<const FixturePath> recent_paths() const&& = delete;
    [[nodiscard]] std::span<const FixturePathCompletion> path_completions()
        const& noexcept { return path_completions_; }
    std::span<const FixturePathCompletion> path_completions() const&& = delete;
    [[nodiscard]] std::string_view search_query() const& noexcept {
        return search_query_;
    }
    std::string_view search_query() const&& = delete;
    [[nodiscard]] std::span<const FixtureSearchResult> search_results()
        const& noexcept { return search_results_; }
    std::span<const FixtureSearchResult> search_results() const&& = delete;
    [[nodiscard]] std::span<const FixtureCriterionModule> criteria_modules()
        const& noexcept { return criteria_modules_; }
    std::span<const FixtureCriterionModule> criteria_modules() const&& = delete;
    [[nodiscard]] std::span<const FixtureCriterionModule> criterion_templates()
        const& noexcept { return criterion_templates_; }
    std::span<const FixtureCriterionModule> criterion_templates() const&& = delete;
    [[nodiscard]] std::span<const FixtureObject> criteria_objects()
        const& noexcept { return criteria_objects_; }
    std::span<const FixtureObject> criteria_objects() const&& = delete;
    [[nodiscard]] FixturePathResolution resolve_path(
        std::string_view input) const;
    [[nodiscard]] std::vector<FixturePathCompletion> complete_path(
        std::string_view input) const;

private:
    FixtureCatalogue();

    std::string id_;
    std::uint64_t generation_{};
    std::string current_path_;
    std::vector<FixtureObject> project_objects_;
    FixturePath current_path_stack_;
    std::vector<FixturePath> recent_paths_;
    std::vector<FixturePathCompletion> path_completions_;
    std::string search_query_;
    std::vector<FixtureSearchResult> search_results_;
    std::vector<FixtureCriterionModule> criteria_modules_;
    std::vector<FixtureCriterionModule> criterion_templates_;
    std::vector<FixtureObject> criteria_objects_;
};

[[nodiscard]] std::vector<CapabilityEntry> initial_capability_report();

} // namespace file_manager_demoboard
