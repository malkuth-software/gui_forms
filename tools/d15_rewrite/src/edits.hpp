#pragma once

// The merged edits of every translation unit, by file: dedupe, conflicts,
// header insertions and the final write (README.md, "Determinism").

#include "model.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d15 {

struct FileOutcome final {
    std::string path;          // relative to the source root
    std::size_t edit_count{0U};
    bool changed{false};
};

class EditPlan final {
public:
    // Adds one unit's edits and header needs. Identical edits from several
    // units (a header seen by each) collapse in resolve().
    void merge(const TranslationUnitHarvest& harvest);

    // Sorts and dedupes every file's edits and removes conflicting ones
    // (overlapping replacements, or an insertion inside a replacement),
    // each reported as residue. Reads each file to place its residue.
    void resolve(const std::string& source_root, std::vector<Residue>& residue);

    // Computes every file's new text, adds the header includes it needs
    // and, when `write` is set, replaces the file (written beside it, then
    // renamed over it). A file that cannot be read or written is residue.
    // Fills `outcomes` in path order.
    void apply(const Options& options, bool write, std::vector<FileOutcome>& outcomes,
               std::vector<Residue>& residue) const;

    [[nodiscard]] std::size_t pending_edits() const noexcept;

    // The unresolved edits, header needs, residue and seen files of one
    // configuration's run, as JSON, so that runs of several
    // configurations (macOS, high-integrity, the Linux VM) over the same
    // tree are merged before anything is written (README.md, "Several
    // configurations"). Paths are relative to the source root.
    [[nodiscard]] bool save(const std::string& path, const std::vector<Residue>& residue,
                            const std::set<std::string>& files_seen) const;
    [[nodiscard]] bool load(const std::string& path, std::vector<Residue>& residue,
                            std::set<std::string>& files_seen, std::string& error);

private:
    std::map<std::string, std::vector<Edit>> edits_;
    std::map<std::string, std::uint32_t> includes_;
};

// Line and column (both from 1) of a byte offset in `text`.
void line_and_column(const std::string& text, std::uint64_t offset, unsigned& line, unsigned& column);

// Reads a whole file; false when it cannot be read.
[[nodiscard]] bool read_file(const std::string& path, std::string& text);

} // namespace d15
