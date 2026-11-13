#include "gui_forms/controls/plot_view_links.hpp"

#include "gui_forms/controls/plot_view.hpp"
#include "gui_forms/event.hpp"

#include <stdexcept>

namespace gui_forms {

PlotLinkGroup::PlotLinkGroup(PlotLinkAxes axes, plots::PlotRange x, plots::PlotRange y)
    : axes_(axes), x_link_(x), y_link_(y) {
    if (!x.valid() || !y.valid()) throw std::invalid_argument("linked ranges must be valid");
}

void PlotLinkGroup::add(const std::shared_ptr<PlotView>& plot) {
    if (!plot) throw std::invalid_argument("a linked plot must not be null");
    for (const std::weak_ptr<PlotView>& member : members_) {
        if (member.lock() == plot) throw std::invalid_argument("the plot is already linked");
    }
    const std::size_t index = members_.size();
    members_.push_back(plot);
    on((*plot).view_changed(), *this, &PlotLinkGroup::on_view_changed, index);
    propagating_ = true;
    if (links_x() && (*plot).set_x_range(0U, x_link_.range())) ++propagations_;
    if (links_y() && (*plot).set_y_range(0U, y_link_.range())) ++propagations_;
    propagating_ = false;
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
    propagating_ = true;
    for (std::size_t index = 0U; index < members_.size(); ++index) {
        if (index == skipped) continue;
        const std::shared_ptr<PlotView> member = members_[index].lock();
        if (!member) continue;
        if (x_axis && (*member).set_x_range(0U, x_link_.range())) ++propagations_;
        if (y_axis && (*member).set_y_range(0U, y_link_.range())) ++propagations_;
    }
    propagating_ = false;
}

} // namespace gui_forms
