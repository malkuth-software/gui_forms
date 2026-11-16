#pragma once

// The plan's closure: which entities change (README.md, "How it works").
// Entities joined by edges form components; a component changes when it
// holds a seeded, eligible entity, and then every eligible member changes.
// Ineligibility spreads over strict edges only.

#include "model.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d15 {

struct Closure final {
    std::map<std::string, EntityFact> entities;    // merged by key
    std::set<std::string> converted;
    std::size_t components{0U};
    std::size_t converting_components{0U};
};

// Merges the facts by key (seeded if any unit seeded it; the first
// ineligibility reason in key order) and computes the converted set.
// Reports every seeded entity left unconverted, and every ineligible
// member of a converting component, as residue.
void close_plan(const std::vector<EntityFact>& facts, const std::vector<EdgeFact>& edges, Closure& closure,
                std::vector<Residue>& residue);

// The plan as JSON, for --plan-output and --plan-input.
[[nodiscard]] bool write_plan(const std::string& path, Stage stage, const std::vector<EntityFact>& facts,
                              const std::vector<EdgeFact>& edges);
[[nodiscard]] bool read_plan(const std::string& path, Stage stage, std::vector<EntityFact>& facts,
                             std::vector<EdgeFact>& edges, std::string& error);

} // namespace d15
