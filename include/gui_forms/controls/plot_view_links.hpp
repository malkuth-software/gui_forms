#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/controls/plot_values.hpp"
#include "gui_forms/plots/axis/axis.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gui_forms {

class PlotView;

// Which of the members' first axes a PlotLinkGroup links.
enum class PlotLinkAxes : std::uint8_t { x, y, both };

// PlotLinkGroup: PlotViews whose first x axes, first y axes or both share
// one range (linked subplots, a plot and its overview; the plot
// demonstrator's gap P7). The ranges live in two plots::AxisLink objects,
// read by x_link and y_link.
//
// A member's view_changed (a gesture, a key, set_x_range, set_y_range or
// fit_view on it) writes the links; the group then sets the changed range on
// every other member, in joining order. Those set_x_range and set_y_range
// calls raise each member's view_changed as usual: the group ignores them
// (an echo, counted), so the change goes round once and the member that
// moved is never set again. Ranges a member refuses (an inadmissible range
// for its scale) are left; that member keeps its own.
//
// Members are observed, not owned: the group holds weak pointers, and a
// member that has died is skipped. The group's subscriptions end with the
// group (dispose or destruction) or with the member's events. One UI
// thread: the members' and the group's.
class GUI_FORMS_API PlotLinkGroup final : public Component {
public:
    // The links start at `x` and `y` (each must be valid); the first member
    // to join takes them.
    PlotLinkGroup(PlotLinkAxes axes, plots::PlotRange x, plots::PlotRange y);

    // Joins `plot`, which takes the linked ranges now. Throws
    // std::invalid_argument for a null plot or one already joined, adding
    // nothing.
    void add(const std::shared_ptr<PlotView>& plot);
    // Which axes are linked from now on. Linking an axis that was not makes
    // every member take that link's range.
    void set_axes(PlotLinkAxes axes);
    [[nodiscard]] PlotLinkAxes axes() const noexcept { return axes_; }

    [[nodiscard]] const plots::AxisLink& x_link() const noexcept { return x_link_; }
    [[nodiscard]] const plots::AxisLink& y_link() const noexcept { return y_link_; }
    [[nodiscard]] std::size_t member_count() const noexcept { return members_.size(); }
    // Ranges the group set on members, and member view changes it ignored
    // as echoes of its own sets (diagnostics and tests).
    [[nodiscard]] std::uint64_t propagations() const noexcept { return propagations_; }
    [[nodiscard]] std::uint64_t echoes() const noexcept { return echoes_; }

private:
    // view_changed of the member at `index`.
    void on_view_changed(const std::size_t& index, const PlotViewRanges& ranges);
    // Sets the links' ranges on every live member but `skipped` (none when
    // it is member_count()), for the axes named.
    void propagate(std::size_t skipped, bool x_axis, bool y_axis);
    [[nodiscard]] bool links_x() const noexcept { return axes_ != PlotLinkAxes::y; }
    [[nodiscard]] bool links_y() const noexcept { return axes_ != PlotLinkAxes::x; }

    PlotLinkAxes axes_{PlotLinkAxes::x};
    plots::AxisLink x_link_;
    plots::AxisLink y_link_;
    std::vector<std::weak_ptr<PlotView>> members_{};
    bool propagating_{};
    std::uint64_t propagations_{};
    std::uint64_t echoes_{};
};

} // namespace gui_forms
