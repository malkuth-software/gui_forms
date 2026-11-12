#pragma once

#include <cstdint>
#include <string>

namespace gui_forms {

enum class ReviewDisposition : std::uint8_t {
    neutral,
    information,
    accepted,
    pending,
    warning,
    rejected,
};

struct ReviewRecord final {
    std::string key;
    std::string title;
    std::string summary;
    std::string verdict;
    ReviewDisposition disposition{ReviewDisposition::neutral};

    friend bool operator==(const ReviewRecord& left,
                           const ReviewRecord& right) noexcept(noexcept(
        left.key == right.key && left.title == right.title &&
        left.summary == right.summary && left.verdict == right.verdict &&
        left.disposition == right.disposition)) {
        return left.key == right.key && left.title == right.title &&
               left.summary == right.summary && left.verdict == right.verdict &&
               left.disposition == right.disposition;
    }
};

} // namespace gui_forms
