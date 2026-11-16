#include "closure.hpp"

#include "edits.hpp"

#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <system_error>
#include <utility>

namespace d15 {

namespace {

// Union-find over entity indices, with path halving. Owned by close_plan.
class Components final {
public:
    explicit Components(const std::size_t count) : parents_(count) {
        for (std::size_t index = 0U; index < count; ++index) parents_[index] = index;
    }

    [[nodiscard]] std::size_t root(std::size_t index) {
        while (parents_[index] != index) {
            parents_[index] = parents_[parents_[index]];
            index = parents_[index];
        }
        return index;
    }

    // Joins two sets; the smaller root index becomes the root, so the
    // result does not depend on edge order.
    void join(const std::size_t first, const std::size_t second) {
        const std::size_t first_root = root(first);
        const std::size_t second_root = root(second);
        if (first_root == second_root) return;
        if (first_root < second_root) {
            parents_[second_root] = first_root;
        } else {
            parents_[first_root] = second_root;
        }
    }

private:
    std::vector<std::size_t> parents_;
};

[[nodiscard]] bool fact_location_less(const EntityFact& left, const EntityFact& right) noexcept {
    if (left.path != right.path) return left.path < right.path;
    if (left.line != right.line) return left.line < right.line;
    return left.column < right.column;
}

[[nodiscard]] EntityKind kind_from_name(llvm::StringRef name) noexcept {
    if (name == "variable") return EntityKind::variable;
    if (name == "local") return EntityKind::local;
    if (name == "parameter") return EntityKind::parameter;
    if (name == "return") return EntityKind::function_return;
    if (name == "alias") return EntityKind::alias;
    if (name == "shared_base") return EntityKind::shared_base;
    return EntityKind::field;
}

[[nodiscard]] Family family_from_name(llvm::StringRef name) noexcept {
    const Family families[] = {Family::optional, Family::text_view, Family::text_reference, Family::owned_text,
                               Family::vector, Family::view, Family::mutable_view, Family::shared,
                               Family::weak, Family::unique, Family::shared_from_this};
    for (const Family family : families) {
        if (name == family_name(family)) return family;
    }
    return Family::none;
}

} // namespace

void close_plan(const std::vector<EntityFact>& facts, const std::vector<EdgeFact>& edges, Closure& closure,
                std::vector<Residue>& residue) {
    // Merge by key: the earliest location names the entity.
    for (const EntityFact& fact : facts) {
        std::map<std::string, EntityFact>::iterator found = closure.entities.find(fact.key);
        if (found == closure.entities.end()) {
            closure.entities.emplace(fact.key, fact);
            continue;
        }
        EntityFact& merged = (*found).second;
        const bool seeded = merged.seeded || fact.seeded;
        std::string ineligible = merged.ineligible;
        if (ineligible.empty() || (!fact.ineligible.empty() && fact.ineligible < ineligible)) {
            if (!fact.ineligible.empty()) ineligible = fact.ineligible;
        }
        if (fact_location_less(fact, merged)) merged = fact;
        merged.seeded = seeded;
        merged.ineligible = ineligible;
    }

    std::vector<std::string> keys;
    std::map<std::string, std::size_t> indices;
    for (const std::pair<const std::string, EntityFact>& entry : closure.entities) {
        indices.emplace(entry.first, keys.size());
        keys.push_back(entry.first);
    }

    // Ineligibility spreads over strict edges.
    Components strict(keys.size());
    Components all(keys.size());
    for (const EdgeFact& edge : edges) {
        std::map<std::string, std::size_t>::const_iterator first = indices.find(edge.first);
        std::map<std::string, std::size_t>::const_iterator second = indices.find(edge.second);
        if (first == indices.end() || second == indices.end()) continue;
        all.join((*first).second, (*second).second);
        if (edge.strict) strict.join((*first).second, (*second).second);
    }
    std::map<std::size_t, std::string> strict_reasons;
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        const EntityFact& fact = closure.entities[keys[index]];
        if (fact.ineligible.empty()) continue;
        const std::size_t root = strict.root(index);
        if (strict_reasons.find(root) == strict_reasons.end()) {
            strict_reasons.emplace(root, "tied to " + fact.qualified_name + " (" + fact.path + ":" +
                                             std::to_string(fact.line) + "), which cannot change: " +
                                             fact.ineligible);
        }
    }
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        EntityFact& fact = closure.entities[keys[index]];
        if (!fact.ineligible.empty()) continue;
        std::map<std::size_t, std::string>::const_iterator reason = strict_reasons.find(strict.root(index));
        if (reason != strict_reasons.end()) fact.ineligible = (*reason).second;
    }

    // A component changes when it holds a seeded, eligible entity.
    std::set<std::size_t> roots;
    std::set<std::size_t> converting;
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        const EntityFact& fact = closure.entities[keys[index]];
        const std::size_t root = all.root(index);
        roots.insert(root);
        if (fact.seeded && fact.ineligible.empty()) converting.insert(root);
    }
    closure.components = roots.size();
    closure.converting_components = converting.size();
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        const EntityFact& fact = closure.entities[keys[index]];
        const bool component_changes = converting.find(all.root(index)) != converting.end();
        if (component_changes && fact.ineligible.empty()) {
            closure.converted.insert(keys[index]);
            continue;
        }
        if (fact.ineligible.empty()) continue;
        if (!component_changes && !fact.seeded) continue;
        Residue item;
        item.path = fact.path;
        item.line = fact.line;
        item.column = fact.column;
        item.reason = std::string(entity_kind_name(fact.kind)) + " " + fact.qualified_name + " not converted: " +
            fact.ineligible;
        residue.push_back(item);
    }
}

bool write_plan(const std::string& path, const Stage stage, const std::vector<EntityFact>& facts,
                const std::vector<EdgeFact>& edges) {
    std::error_code error;
    llvm::raw_fd_ostream output(path, error);
    if (error) return false;
    llvm::json::Array entity_records;
    for (const EntityFact& fact : facts) {
        llvm::json::Object record;
        record["key"] = fact.key;
        record["path"] = fact.path;
        record["line"] = static_cast<std::int64_t>(fact.line);
        record["column"] = static_cast<std::int64_t>(fact.column);
        record["kind"] = entity_kind_name(fact.kind);
        record["family"] = family_name(fact.family);
        record["name"] = fact.name;
        record["qualifiedName"] = fact.qualified_name;
        record["seeded"] = fact.seeded;
        record["ineligible"] = fact.ineligible;
        entity_records.push_back(llvm::json::Value(std::move(record)));
    }
    llvm::json::Array edge_records;
    for (const EdgeFact& edge : edges) {
        llvm::json::Object record;
        record["first"] = edge.first;
        record["second"] = edge.second;
        record["strict"] = edge.strict;
        edge_records.push_back(llvm::json::Value(std::move(record)));
    }
    llvm::json::Object root;
    root["schema"] = "gui.forms.d15-rewrite-plan/v1";
    root["stage"] = stage_name(stage);
    root["entities"] = std::move(entity_records);
    root["edges"] = std::move(edge_records);
    output << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(root)));
    return true;
}

bool read_plan(const std::string& path, const Stage stage, std::vector<EntityFact>& facts,
               std::vector<EdgeFact>& edges, std::string& error) {
    std::string text;
    if (!read_file(path, text)) {
        error = "cannot read " + path;
        return false;
    }
    llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(text);
    if (!parsed) {
        error = llvm::toString(parsed.takeError());
        return false;
    }
    const llvm::json::Object* const root = (*parsed).getAsObject();
    if (root == nullptr) {
        error = "the plan is not a JSON object";
        return false;
    }
    const std::optional<llvm::StringRef> recorded_stage = (*root).getString("stage");
    if (!recorded_stage || *recorded_stage != stage_name(stage)) {
        error = "the plan was made for another stage";
        return false;
    }
    const llvm::json::Array* const entity_records = (*root).getArray("entities");
    const llvm::json::Array* const edge_records = (*root).getArray("edges");
    if (entity_records == nullptr || edge_records == nullptr) {
        error = "the plan has no entities or edges";
        return false;
    }
    for (const llvm::json::Value& value : *entity_records) {
        const llvm::json::Object* const record = value.getAsObject();
        if (record == nullptr) continue;
        EntityFact fact;
        fact.key = std::string((*record).getString("key").value_or(""));
        fact.path = std::string((*record).getString("path").value_or(""));
        fact.line = static_cast<unsigned>((*record).getInteger("line").value_or(0));
        fact.column = static_cast<unsigned>((*record).getInteger("column").value_or(0));
        fact.kind = kind_from_name((*record).getString("kind").value_or("field"));
        fact.family = family_from_name((*record).getString("family").value_or("none"));
        fact.name = std::string((*record).getString("name").value_or(""));
        fact.qualified_name = std::string((*record).getString("qualifiedName").value_or(""));
        fact.seeded = (*record).getBoolean("seeded").value_or(false);
        fact.ineligible = std::string((*record).getString("ineligible").value_or(""));
        facts.push_back(fact);
    }
    for (const llvm::json::Value& value : *edge_records) {
        const llvm::json::Object* const record = value.getAsObject();
        if (record == nullptr) continue;
        EdgeFact edge;
        edge.first = std::string((*record).getString("first").value_or(""));
        edge.second = std::string((*record).getString("second").value_or(""));
        edge.strict = (*record).getBoolean("strict").value_or(false);
        edges.push_back(edge);
    }
    return true;
}

} // namespace d15
