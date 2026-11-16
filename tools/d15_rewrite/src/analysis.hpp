#pragma once

// One translation unit's analysis (README.md, "How it works"). In the plan
// phase it records entities and the edges between them; in the rewrite
// phase, given the converted set, it records edits and residue.

#include "model.hpp"

#include "clang/Tooling/Tooling.h"

#include <memory>
#include <set>
#include <string>

namespace d15 {

enum class Phase : std::uint8_t {
    plan,
    rewrite,
};

// What every unit's analysis reads. Borrowed: the driver owns the options
// and the converted set and keeps them alive and unchanged while any unit
// runs.
struct AnalysisInput final {
    const Options* options{nullptr};
    Phase phase{Phase::plan};
    const std::set<std::string>* converted{nullptr};
};

// A factory whose actions analyse into `harvest`, which the caller owns and
// which must outlive the factory's use by one ClangTool run.
[[nodiscard]] std::unique_ptr<clang::tooling::FrontendActionFactory> make_action_factory(
    const AnalysisInput& input, TranslationUnitHarvest& harvest);

} // namespace d15
