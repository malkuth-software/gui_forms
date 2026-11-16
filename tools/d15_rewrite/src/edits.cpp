#include "edits.hpp"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

namespace d15 {

namespace {

// Order at one offset: closers (inner first), then openers (outer first),
// then the replacement starting there.
[[nodiscard]] unsigned role_rank(const EditRole role) noexcept {
    switch (role) {
    case EditRole::close: return 0U;
    case EditRole::open: return 1U;
    case EditRole::replace: return 2U;
    default: break;
    }
    return 3U;
}

[[nodiscard]] bool edit_less(const Edit& left, const Edit& right) noexcept {
    if (left.offset != right.offset) return left.offset < right.offset;
    const unsigned left_rank = role_rank(left.role);
    const unsigned right_rank = role_rank(right.role);
    if (left_rank != right_rank) return left_rank < right_rank;
    if (left.role == EditRole::close && left.span != right.span) return left.span < right.span;
    if (left.role == EditRole::open && left.span != right.span) return left.span > right.span;
    if (left.length != right.length) return left.length < right.length;
    return left.text < right.text;
}

[[nodiscard]] bool edit_equal(const Edit& left, const Edit& right) noexcept {
    const bool result = left.offset == right.offset && left.length == right.length && left.role == right.role &&
        left.span == right.span && left.text == right.text;
    return result;
}

[[nodiscard]] std::string join_path(const std::string& root, const std::string& relative) {
    llvm::SmallString<512U> path(root);
    llvm::sys::path::append(path, relative);
    const std::string result(path.str());
    return result;
}

// The include spelling of the private bridge from a file at `relative`
// (both relative to the source root), as GUI.Forms' sources write it.
[[nodiscard]] std::string bridge_include(const std::string& relative) {
    std::vector<std::string> from;
    for (llvm::sys::path::const_iterator part = llvm::sys::path::begin(relative);
         part != llvm::sys::path::end(relative); ++part) {
        from.push_back(std::string(*part));
    }
    if (!from.empty()) from.pop_back();
    const std::vector<std::string> to = {"src", "platform", "stxlib_bridge.hpp"};
    std::size_t common = 0U;
    while (common < from.size() && common + 1U < to.size() && from[common] == to[common]) ++common;
    std::string spelled;
    for (std::size_t index = common; index < from.size(); ++index) spelled += "../";
    for (std::size_t index = common; index < to.size(); ++index) {
        spelled += to[index];
        if (index + 1U < to.size()) spelled += "/";
    }
    const std::string result = "#include \"" + spelled + "\"\n";
    return result;
}

struct HeaderLine final {
    std::uint32_t need;
    const char* line;
    const char* probe;
};

const HeaderLine owned_headers[] = {
    {include_fail_fast, "#include <stxlib/fail_fast.hpp>\n", "stxlib/fail_fast.hpp"},
    {include_optional, "#include <stxlib/optional.hpp>\n", "stxlib/optional.hpp"},
    {include_result, "#include <stxlib/result.hpp>\n", "stxlib/result.hpp"},
    {include_shared, "#include <stxlib/shared.hpp>\n", "stxlib/shared.hpp"},
    {include_text, "#include <stxlib/text.hpp>\n", "stxlib/text.hpp"},
    {include_text_view, "#include <stxlib/text_view.hpp>\n", "stxlib/text_view.hpp"},
    {include_unique, "#include <stxlib/unique.hpp>\n", "stxlib/unique.hpp"},
    {include_vector, "#include <stxlib/vector.hpp>\n", "stxlib/vector.hpp"},
    {include_view, "#include <stxlib/view.hpp>\n", "stxlib/view.hpp"},
};

// Byte offsets of the start of every line beginning with `prefix`.
void lines_starting_with(const std::string& text, llvm::StringRef prefix, std::vector<std::size_t>& starts) {
    std::size_t line_start = 0U;
    while (line_start < text.size()) {
        const llvm::StringRef line = llvm::StringRef(text).substr(line_start);
        if (line.starts_with(prefix)) starts.push_back(line_start);
        const std::size_t newline = text.find('\n', line_start);
        if (newline == std::string::npos) break;
        line_start = newline + 1U;
    }
}

[[nodiscard]] std::size_t line_end_after(const std::string& text, std::size_t line_start) {
    const std::size_t newline = text.find('\n', line_start);
    if (newline == std::string::npos) return text.size();
    return newline + 1U;
}

// True when the include line at `start` names a standard header
// (<optional>, <cstddef>: no directory and no extension).
[[nodiscard]] bool standard_header_line(const std::string& text, const std::size_t start) {
    const std::size_t open = text.find('<', start);
    const std::size_t close = text.find('>', start);
    const std::size_t end = line_end_after(text, start);
    if (open == std::string::npos || close == std::string::npos || close > end || open > close) return false;
    const std::string name = text.substr(open + 1U, close - open - 1U);
    const bool result = name.find('/') == std::string::npos && name.find('.') == std::string::npos;
    return result;
}

// Where an angle-bracket stxlib include goes: after the last stxlib
// include, else before the first standard-library include, else after
// the last include, else after #pragma once, else at the top.
[[nodiscard]] std::size_t angle_include_position(const std::string& text) {
    std::vector<std::size_t> owned;
    lines_starting_with(text, "#include <stxlib/", owned);
    if (!owned.empty()) return line_end_after(text, owned.back());
    std::vector<std::size_t> system;
    lines_starting_with(text, "#include <", system);
    for (const std::size_t start : system) {
        if (standard_header_line(text, start)) return start;
    }
    std::vector<std::size_t> any;
    lines_starting_with(text, "#include", any);
    if (!any.empty()) return line_end_after(text, any.back());
    std::vector<std::size_t> pragma;
    lines_starting_with(text, "#pragma once", pragma);
    if (!pragma.empty()) return line_end_after(text, pragma.front());
    return 0U;
}

// Where the quoted bridge include goes: after the last quoted include,
// else after the last include, else where an angle include would go.
[[nodiscard]] std::size_t quoted_include_position(const std::string& text) {
    std::vector<std::size_t> quoted;
    lines_starting_with(text, "#include \"", quoted);
    if (!quoted.empty()) return line_end_after(text, quoted.back());
    std::vector<std::size_t> any;
    lines_starting_with(text, "#include", any);
    if (!any.empty()) return line_end_after(text, any.back());
    const std::size_t result = angle_include_position(text);
    return result;
}

struct StandardHeader final {
    Stage stage;
    const char* line;
    const char* tokens[6];
};

// Standard headers a stage can make unused, and the names that still need
// them. Used only with --drop-standard-includes.
const StandardHeader standard_headers[] = {
    {Stage::v1_optional, "#include <optional>", {"std::optional", "std::nullopt", "std::make_optional", "std::bad_optional_access", nullptr, nullptr}},
    {Stage::v2_text_view, "#include <string_view>", {"std::string_view", "std::basic_string_view", "std::u8string_view", "std::u16string_view", "std::u32string_view", nullptr}},
    {Stage::v4_sequences, "#include <vector>", {"std::vector", nullptr, nullptr, nullptr, nullptr, nullptr}},
    {Stage::v4_sequences, "#include <span>", {"std::span", "std::dynamic_extent", "std::as_bytes", "std::as_writable_bytes", nullptr, nullptr}},
};

void drop_unused_standard_includes(std::string& text, const Stage stage) {
    for (const StandardHeader& header : standard_headers) {
        if (header.stage != stage) continue;
        std::vector<std::size_t> starts;
        lines_starting_with(text, header.line, starts);
        if (starts.empty()) continue;
        bool used = false;
        for (const char* token : header.tokens) {
            if (token == nullptr) break;
            if (text.find(token) != std::string::npos) used = true;
        }
        if (used) continue;
        const std::size_t start = starts.front();
        const std::size_t end = line_end_after(text, start);
        text.erase(start, end - start);
    }
}

} // namespace

void line_and_column(const std::string& text, const std::uint64_t offset, unsigned& line, unsigned& column) {
    line = 1U;
    column = 1U;
    const std::size_t limit = std::min<std::size_t>(static_cast<std::size_t>(offset), text.size());
    for (std::size_t index = 0U; index < limit; ++index) {
        if (text[index] == '\n') {
            ++line;
            column = 1U;
        } else {
            ++column;
        }
    }
}

bool read_file(const std::string& path, std::string& text) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer = llvm::MemoryBuffer::getFile(path);
    if (!buffer) return false;
    text = std::string((*(*buffer)).getBuffer());
    return true;
}

void EditPlan::merge(const TranslationUnitHarvest& harvest) {
    for (const std::pair<const std::string, std::vector<Edit>>& file : harvest.edits) {
        std::vector<Edit>& target = edits_[file.first];
        target.insert(target.end(), file.second.begin(), file.second.end());
    }
    for (const std::pair<const std::string, std::uint32_t>& file : harvest.includes) {
        includes_[file.first] |= file.second;
    }
}

void EditPlan::resolve(const std::string& source_root, std::vector<Residue>& residue) {
    for (std::pair<const std::string, std::vector<Edit>>& file : edits_) {
        std::vector<Edit>& edits = file.second;
        std::sort(edits.begin(), edits.end(), edit_less);
        std::vector<Edit>::iterator unique_end = std::unique(edits.begin(), edits.end(), edit_equal);
        edits.erase(unique_end, edits.end());

        // A conflict: a replacement overlapping another, or an insertion
        // strictly inside a replacement. Both ends are dropped.
        std::vector<bool> conflicting(edits.size(), false);
        for (std::size_t first = 0U; first < edits.size(); ++first) {
            if (edits[first].role != EditRole::replace) continue;
            const std::uint64_t first_end = edits[first].offset + edits[first].length;
            for (std::size_t second = first + 1U; second < edits.size(); ++second) {
                if (edits[second].offset >= first_end) break;
                const bool overlapping_replace = edits[second].role == EditRole::replace;
                const bool inside = edits[second].offset > edits[first].offset;
                if (overlapping_replace || inside) {
                    conflicting[first] = true;
                    conflicting[second] = true;
                }
            }
        }
        bool any = false;
        for (std::size_t index = 0U; index < edits.size(); ++index) any = any || conflicting[index];
        if (!any) continue;
        std::string text;
        const bool readable = read_file(join_path(source_root, file.first), text);
        std::vector<Edit> kept;
        for (std::size_t index = 0U; index < edits.size(); ++index) {
            if (!conflicting[index]) {
                kept.push_back(edits[index]);
                continue;
            }
            Residue item;
            item.path = file.first;
            if (readable) line_and_column(text, edits[index].offset, item.line, item.column);
            item.reason = "conflicting edits from different rules or configurations; this site was left unchanged";
            item.snippet = edits[index].text;
            residue.push_back(item);
        }
        edits = kept;
    }
}

std::size_t EditPlan::pending_edits() const noexcept {
    std::size_t count = 0U;
    for (const std::pair<const std::string, std::vector<Edit>>& file : edits_) count += file.second.size();
    return count;
}

bool EditPlan::save(const std::string& path, const std::vector<Residue>& residue,
                    const std::set<std::string>& files_seen) const {
    std::error_code error;
    llvm::raw_fd_ostream output(path, error);
    if (error) return false;
    llvm::json::Array file_records;
    for (const std::pair<const std::string, std::vector<Edit>>& file : edits_) {
        llvm::json::Array edit_records;
        for (const Edit& edit : file.second) {
            llvm::json::Object record;
            record["offset"] = static_cast<std::int64_t>(edit.offset);
            record["length"] = static_cast<std::int64_t>(edit.length);
            record["text"] = edit.text;
            record["role"] = static_cast<std::int64_t>(edit.role);
            record["span"] = static_cast<std::int64_t>(edit.span);
            edit_records.push_back(llvm::json::Value(std::move(record)));
        }
        llvm::json::Object record;
        record["path"] = file.first;
        record["edits"] = std::move(edit_records);
        file_records.push_back(llvm::json::Value(std::move(record)));
    }
    llvm::json::Array include_records;
    for (const std::pair<const std::string, std::uint32_t>& file : includes_) {
        llvm::json::Object record;
        record["path"] = file.first;
        record["needs"] = static_cast<std::int64_t>(file.second);
        include_records.push_back(llvm::json::Value(std::move(record)));
    }
    llvm::json::Array residue_records;
    for (const Residue& item : residue) {
        llvm::json::Object record;
        record["path"] = item.path;
        record["line"] = static_cast<std::int64_t>(item.line);
        record["column"] = static_cast<std::int64_t>(item.column);
        record["reason"] = item.reason;
        record["snippet"] = item.snippet;
        residue_records.push_back(llvm::json::Value(std::move(record)));
    }
    llvm::json::Array seen_records;
    for (const std::string& seen : files_seen) seen_records.push_back(seen);
    llvm::json::Object root;
    root["schema"] = "gui.forms.d15-rewrite-edits/v1";
    root["files"] = std::move(file_records);
    root["includes"] = std::move(include_records);
    root["residue"] = std::move(residue_records);
    root["seen"] = std::move(seen_records);
    output << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(root)));
    return true;
}

bool EditPlan::load(const std::string& path, std::vector<Residue>& residue, std::set<std::string>& files_seen,
                    std::string& error) {
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
    if (root == nullptr || (*root).getString("schema").value_or("") != "gui.forms.d15-rewrite-edits/v1") {
        error = path + " is not a d15_rewrite edits file";
        return false;
    }
    const llvm::json::Array* const files = (*root).getArray("files");
    if (files != nullptr) {
        for (const llvm::json::Value& value : *files) {
            const llvm::json::Object* const record = value.getAsObject();
            if (record == nullptr) continue;
            const std::string file((*record).getString("path").value_or(""));
            const llvm::json::Array* const edits = (*record).getArray("edits");
            if (file.empty() || edits == nullptr) continue;
            std::vector<Edit>& target = edits_[file];
            for (const llvm::json::Value& edit_value : *edits) {
                const llvm::json::Object* const edit_record = edit_value.getAsObject();
                if (edit_record == nullptr) continue;
                Edit edit;
                edit.offset = static_cast<std::uint64_t>((*edit_record).getInteger("offset").value_or(0));
                edit.length = static_cast<std::uint64_t>((*edit_record).getInteger("length").value_or(0));
                edit.text = std::string((*edit_record).getString("text").value_or(""));
                const std::int64_t role = (*edit_record).getInteger("role").value_or(0);
                edit.role = role == 1 ? EditRole::open : role == 2 ? EditRole::close : EditRole::replace;
                edit.span = static_cast<std::uint64_t>((*edit_record).getInteger("span").value_or(0));
                target.push_back(edit);
            }
        }
    }
    const llvm::json::Array* const includes = (*root).getArray("includes");
    if (includes != nullptr) {
        for (const llvm::json::Value& value : *includes) {
            const llvm::json::Object* const record = value.getAsObject();
            if (record == nullptr) continue;
            const std::string file((*record).getString("path").value_or(""));
            includes_[file] |= static_cast<std::uint32_t>((*record).getInteger("needs").value_or(0));
        }
    }
    const llvm::json::Array* const residue_records = (*root).getArray("residue");
    if (residue_records != nullptr) {
        for (const llvm::json::Value& value : *residue_records) {
            const llvm::json::Object* const record = value.getAsObject();
            if (record == nullptr) continue;
            Residue item;
            item.path = std::string((*record).getString("path").value_or(""));
            item.line = static_cast<unsigned>((*record).getInteger("line").value_or(0));
            item.column = static_cast<unsigned>((*record).getInteger("column").value_or(0));
            item.reason = std::string((*record).getString("reason").value_or(""));
            item.snippet = std::string((*record).getString("snippet").value_or(""));
            residue.push_back(item);
        }
    }
    const llvm::json::Array* const seen = (*root).getArray("seen");
    if (seen != nullptr) {
        for (const llvm::json::Value& value : *seen) {
            const std::optional<llvm::StringRef> file = value.getAsString();
            if (file) files_seen.insert(std::string(*file));
        }
    }
    return true;
}

void EditPlan::apply(const Options& options, const bool write, std::vector<FileOutcome>& outcomes,
                     std::vector<Residue>& residue) const {
    for (const std::pair<const std::string, std::vector<Edit>>& file : edits_) {
        if (file.second.empty()) continue;
        const std::string absolute = join_path(options.source_root, file.first);
        std::string original;
        if (!read_file(absolute, original)) {
            Residue item;
            item.path = file.first;
            item.reason = "cannot read the file to rewrite it";
            residue.push_back(item);
            continue;
        }
        std::vector<Edit> edits = file.second;

        // The headers the rewritten declarations and helpers need.
        std::map<std::string, std::uint32_t>::const_iterator needed = includes_.find(file.first);
        const std::uint32_t needs = needed == includes_.end() ? include_none : (*needed).second;
        for (const HeaderLine& header : owned_headers) {
            if ((needs & header.need) == 0U) continue;
            if (original.find(header.probe) != std::string::npos) continue;
            Edit insertion;
            insertion.offset = angle_include_position(original);
            insertion.text = header.line;
            insertion.role = EditRole::open;
            edits.push_back(insertion);
        }
        if ((needs & include_bridge) != 0U && original.find("platform/stxlib_bridge.hpp") == std::string::npos) {
            Edit insertion;
            insertion.offset = quoted_include_position(original);
            insertion.text = bridge_include(file.first);
            insertion.role = EditRole::open;
            edits.push_back(insertion);
        }
        std::stable_sort(edits.begin(), edits.end(), edit_less);

        std::string rewritten;
        rewritten.reserve(original.size() + 256U);
        std::uint64_t cursor = 0U;
        bool out_of_range = false;
        for (const Edit& edit : edits) {
            if (edit.offset < cursor || edit.offset + edit.length > original.size()) {
                out_of_range = true;
                break;
            }
            rewritten.append(original, static_cast<std::size_t>(cursor),
                             static_cast<std::size_t>(edit.offset - cursor));
            rewritten.append(edit.text);
            cursor = edit.offset + edit.length;
        }
        if (out_of_range) {
            Residue item;
            item.path = file.first;
            item.reason = "edits do not fit the file (it changed since it was parsed); nothing written";
            residue.push_back(item);
            continue;
        }
        rewritten.append(original, static_cast<std::size_t>(cursor), std::string::npos);
        if (options.drop_standard_includes) drop_unused_standard_includes(rewritten, options.stage);

        FileOutcome outcome;
        outcome.path = file.first;
        outcome.edit_count = file.second.size();
        outcome.changed = rewritten != original;
        if (write && outcome.changed) {
            const std::string temporary = absolute + ".d15-rewrite";
            std::error_code error;
            {
                llvm::raw_fd_ostream stream(temporary, error);
                if (!error) {
                    stream << rewritten;
                    stream.close();
                    if (stream.has_error()) error = stream.error();
                }
            }
            if (!error) error = llvm::sys::fs::rename(temporary, absolute);
            if (error) {
                Residue item;
                item.path = file.first;
                item.reason = "cannot write the rewritten file: " + error.message();
                residue.push_back(item);
                outcome.changed = false;
            }
        }
        outcomes.push_back(outcome);
    }
}

} // namespace d15
