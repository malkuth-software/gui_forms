#pragma once

#include "gui_forms/allocation/counted_allocator/counted_allocator.hpp"
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms {

class GUI_FORMS_API DamageRegion {
public:
    static constexpr std::size_t maximum_rectangles = 64;

    void add(Rect rect);
    void clear() noexcept;

    [[nodiscard]] bool empty() const noexcept { return rectangles_.empty(); }
    [[nodiscard]] std::span<const Rect> rectangles() const& noexcept {
        return std::span<const Rect>(rectangles_.data(), rectangles_.size());
    }
    std::span<const Rect> rectangles() const&& = delete;
    [[nodiscard]] std::size_t rectangle_count() const noexcept { return rectangles_.size(); }
    [[nodiscard]] std::uint64_t compaction_count() const noexcept { return compaction_count_; }
    [[nodiscard]] std::uint64_t collapse_count() const noexcept { return collapse_count_; }
    [[nodiscard]] Rect bounds() const noexcept;
    [[nodiscard]] double area() const noexcept;

private:
    // Counted under AllocationSubsystem::damage_lists.
    using RectangleList = std::vector<Rect, detail::CountedAllocator<Rect, AllocationSubsystem::damage_lists>>;

    RectangleList rectangles_;
    std::uint64_t compaction_count_{};
    std::uint64_t collapse_count_{};
};

// The damage `damage` becomes once a scroll copy moved the pixels inside
// `clip` by `offset` (window coordinates): a rectangle's share inside the
// clip moves with its pixels and is cut to the clip; its share outside the
// clip stays where it was. The pixels the copy exposed are not included.
[[nodiscard]] GUI_FORMS_API DamageRegion damage_after_scroll_copy(const DamageRegion& damage, Rect clip, Point offset);

} // namespace gui_forms
