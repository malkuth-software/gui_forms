#include "gui_forms/controls/plot_view_links.hpp"

#include "gui_forms/controls/plot_view.hpp"
#include "gui_forms/event.hpp"

#include <stdexcept>

namespace gui_forms {
namespace {

// Marks the group as propagating for its lifetime, so the flag is cleared on
// every exit, including an exception from a member's range write or from a
// view_changed handler. A flag left set counted every later change as an
// echo and the group never linked again (A-gf-controls-10-F5).
class PropagationScope final {
public:
    explicit PropagationScope(bool& propagating) noexcept : propagating_(propagating) { propagating_ = true; }
    ~PropagationScope() { propagating_ = false; }
    PropagationScope(const PropagationScope&) = delete;
    PropagationScope& operator=(const PropagationScope&) = delete;

private:
    bool& propagating_;
};

} // namespace

PlotLinkGroup::PlotLinkGroup(PlotLinkAxes axes, plots::PlotRange x, plots::PlotRange y)
    : axes_(axes), x_link_(x), y_link_(y) {
    if (!x.valid() || !y.valid()) throw std::invalid_argument("linked ranges must be valid");
}

void PlotLinkGroup::add(const std::shared_ptr<PlotView>& plot) {
    if (!plot) throw std::invalid_argument("a linked plot must not be null");
    if (!(*plot).is_alive()) throw std::invalid_argument("a disposed plot cannot be linked");
    for (const std::weak_ptr<PlotView>& member : members_) {
        if (member.lock() == plot) throw std::invalid_argument("the plot is already linked");
    }
    const std::size_t index = members_.size();
    members_.push_back(plot);
    on((*plot).view_changed(), *this, &PlotLinkGroup::on_view_changed, index);
    const PropagationScope scope(propagating_);
    if (links_x() && (*plot).set_x_range(0U, x_link_.range())) ++propagations_;
    if (links_y() && (*plot).set_y_range(0U, y_link_.range())) ++propagations_;
}

void PlotLinkGroup::set_axes(PlotLinkAxes axes) {
    const bool x_added = axes != PlotLinkAxes::y && !links_x();
    const bool y_added = axes != PlotLinkAxes::x && !links_y();
    axes_ = axes;
    propagate(members_.size(), x_added, y_added);
}

void PlotLinkGroup::on_view_changed(const std::size_t& index, const PlotViewRanges& ranges) {
    if (propagating_) {
        ++echoes_;
        return;
    }
    const plots::PlotRange x{ranges.x_minimum, ranges.x_maximum};
    const plots::PlotRange y{ranges.y_minimum, ranges.y_maximum};
    const bool x_moved = links_x() && x_link_.set_range(x);
    const bool y_moved = links_y() && y_link_.set_range(y);
    if (!x_moved && !y_moved) return;
    propagate(index, x_moved, y_moved);
}

void PlotLinkGroup::propagate(std::size_t skipped, bool x_axis, bool y_axis) {
    if (!x_axis && !y_axis) return;
    const PropagationScope scope(propagating_);
    for (std::size_t index = 0U; index < members_.size(); ++index) {
        if (index == skipped) continue;
        const std::shared_ptr<PlotView> member = members_[index].lock();
        // A member disposed while the application still holds it (its page
        // closed) refuses range writes with an exception; it is skipped, as
        // an expired one is, so a pan on another member does not throw out
        // of pointer handling (A-gf-controls-10-F5).
        if (!member || !(*member).is_alive()) continue;
        if (x_axis && (*member).set_x_range(0U, x_link_.range())) ++propagations_;
        if (y_axis && (*member).set_y_range(0U, y_link_.range())) ++propagations_;
    }
}

} // namespace gui_forms
