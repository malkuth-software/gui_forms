#include "gui_forms/types.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace gui_forms {

void DamageRegion::add(Rect rect) {
    if (rect.empty()) {
        return;
    }

    bool merged = true;
    while (merged) {
        merged = false;
        for (RectangleList::iterator iterator = rectangles_.begin();
             iterator != rectangles_.end(); ++iterator) {
            const Rect united = Rect::united(*iterator, rect);
            const double exact_union_area = (*iterator).area() + rect.area() -
                                            Rect::intersection(*iterator, rect).area();
            const double tolerance = std::max(1.0, exact_union_area) * 1.0e-12;
            if (std::abs(united.area() - exact_union_area) <= tolerance) {
                rect = united;
                rectangles_.erase(iterator);
                ++compaction_count_;
                merged = true;
                break;
            }
        }
    }
    rectangles_.push_back(rect);

    if (rectangles_.size() > maximum_rectangles) {
        const Rect collapsed = bounds();
        rectangles_.assign(1, collapsed);
        ++collapse_count_;
    }
}

void DamageRegion::clear() noexcept {
    rectangles_.clear();
}

Rect DamageRegion::bounds() const noexcept {
    Rect result{};
    for (const Rect rect : rectangles_) {
        result = Rect::united(result, rect);
    }
    return result;
}

double DamageRegion::area() const noexcept {
    if (rectangles_.empty()) {
        return 0.0;
    }

    std::vector<double> edges;
    edges.reserve(rectangles_.size() * 2U);
    for (const Rect rect : rectangles_) {
        if (!rect.empty()) {
            edges.push_back(rect.x);
            edges.push_back(rect.x + rect.width);
        }
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

    double result = 0.0;
    for (std::size_t index = 1; index < edges.size(); ++index) {
        const double x0 = edges[index - 1U];
        const double x1 = edges[index];
        if (x1 <= x0) {
            continue;
        }
        std::vector<std::pair<double, double>> intervals;
        for (const Rect rect : rectangles_) {
            if (!rect.empty() && rect.x < x1 && rect.x + rect.width > x0) {
                intervals.emplace_back(rect.y, rect.y + rect.height);
            }
        }
        std::sort(intervals.begin(), intervals.end());
        double covered = 0.0;
        if (!intervals.empty()) {
            double start = intervals.front().first;
            double end = intervals.front().second;
            for (std::size_t interval = 1; interval < intervals.size(); ++interval) {
                if (intervals[interval].first <= end) {
                    end = std::max(end, intervals[interval].second);
                } else {
                    covered += end - start;
                    start = intervals[interval].first;
                    end = intervals[interval].second;
                }
            }
            covered += end - start;
        }
        result += (x1 - x0) * covered;
    }
    return result;
}

DamageRegion damage_after_scroll_copy(const DamageRegion& damage, Rect clip, Point offset) {
    DamageRegion result;
    for (const Rect rect : damage.rectangles()) {
        const Rect inside = Rect::intersection(rect, clip);
        if (inside.empty()) {
            result.add(rect);
            continue;
        }
        // The share outside the clip: the full-width bands above and below
        // it, then the parts left and right of it within its rows.
        const double right = rect.x + rect.width;
        const double bottom = rect.y + rect.height;
        const double inside_right = inside.x + inside.width;
        const double inside_bottom = inside.y + inside.height;
        result.add(Rect{rect.x, rect.y, rect.width, inside.y - rect.y});
        result.add(Rect{rect.x, inside_bottom, rect.width, bottom - inside_bottom});
        result.add(Rect{rect.x, inside.y, inside.x - rect.x, inside.height});
        result.add(Rect{inside_right, inside.y, right - inside_right, inside.height});
        const Rect moved{inside.x + offset.x, inside.y + offset.y, inside.width, inside.height};
        result.add(Rect::intersection(moved, clip));
    }
    return result;
}

} // namespace gui_forms
