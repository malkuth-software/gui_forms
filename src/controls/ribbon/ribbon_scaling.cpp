// Ribbon scaling (stage RB, phase 2b; RIBBON_SCALING_STUDY.md §5): group
// layouts, the reduction ladder and its caches, and the plane's placement at
// one ladder entry, with scrolling when even the narrowest entry is too wide.

#include "ribbon_scaling.hpp"

#include "ribbon_geometry.hpp"

#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {

using namespace ribbon_detail;

namespace {

// Whether a generated layout step may make `command` small: a button-family
// command whose size so far, `size`, is large.
bool convertible(const RibbonCommandSpec& command, RibbonCommandSize size) noexcept {
    const bool result = !hosted(command.kind) && size == RibbonCommandSize::large;
    return result;
}

// Orders ladder entries from the widest: an entry comes before `width` while
// it is wider.
struct EntryWiderThan final {
    bool operator()(const RibbonLadderEntry& entry, double width) const noexcept { return entry.width > width; }
};

std::optional<std::size_t> gallery_of(const RibbonGroupSpec& group) noexcept {
    for (std::size_t command = 0; command < group.commands.size(); ++command) {
        if (group.commands[command].kind == RibbonCommandKind::gallery) return command;
    }
    return std::nullopt;
}

} // namespace

namespace ribbon_detail {

RibbonCommandSize layout_command_size(const RibbonGroupLayout& layout, const RibbonCommandSpec& command,
                                      std::size_t index) noexcept {
    if (index < layout.command_sizes.size()) return layout.command_sizes[index];
    return command.size;
}

std::vector<RibbonGroupLayout> prepare_group_layouts(const RibbonGroupSpec& group) {
    const std::size_t count = group.commands.size();
    if (!group.layouts.empty()) {
        if (group.layouts.size() > Ribbon::maximum_layouts_per_group) {
            throw std::invalid_argument("Ribbon group has more layouts than maximum_layouts_per_group");
        }
        for (std::size_t index = 0; index < group.layouts.size(); ++index) {
            const RibbonGroupLayout& layout = group.layouts[index];
            const bool collapsed = layout.size == RibbonGroupSize::collapsed;
            if (collapsed && (index == 0U || index + 1U != group.layouts.size())) {
                throw std::invalid_argument("Ribbon group's collapsed layout must be its last and not its first");
            }
            if (collapsed && count <= 1U) {
                throw std::invalid_argument("Ribbon group of one command never collapses");
            }
            if (collapsed && !layout.command_sizes.empty()) {
                throw std::invalid_argument("Ribbon group's collapsed layout has no command sizes");
            }
            if (index > 0U && static_cast<int>(layout.size) < static_cast<int>(group.layouts[index - 1U].size)) {
                throw std::invalid_argument("Ribbon group layouts must run from the largest size down");
            }
            if (!layout.command_sizes.empty() && layout.command_sizes.size() != count) {
                throw std::invalid_argument("Ribbon group layout needs one size per command");
            }
            if (layout.rows < 1U || layout.rows > rows_per_column) {
                throw std::invalid_argument("Ribbon group layout rows must be 1 to 3");
            }
            for (std::size_t command = 0; command < count && !collapsed; ++command) {
                const RibbonCommandSpec& spec = group.commands[command];
                if (!spec.show_label && !hosted(spec.kind) &&
                    layout_command_size(layout, spec, command) != RibbonCommandSize::small) {
                    throw std::invalid_argument("Ribbon icon-only commands must be small in every layout");
                }
            }
        }
        // A group never vanishes (RIBBON_SCALING_STUDY §1.1: shrink, then
        // the collapsed button, then scrolling): authored layouts that stop
        // short of the collapsed button gain it as their last step, except
        // for a group of one command, which never collapses (UX7).
        std::vector<RibbonGroupLayout> authored = group.layouts;
        const bool ends_collapsed = authored.back().size == RibbonGroupSize::collapsed;
        if (!ends_collapsed && count > 1U) {
            authored.push_back(RibbonGroupLayout{RibbonGroupSize::collapsed, {}, authored.back().rows});
        }
        return authored;
    }
    // Generated: the commands' own sizes first; then, step by step, the
    // rightmost run of up to three large commands becomes small (after WPF's
    // default size definitions, with labels kept); last, the collapsed
    // button, except for a group of one command (UX7).
    std::vector<RibbonGroupLayout> layouts;
    layouts.push_back(RibbonGroupLayout{RibbonGroupSize::large, {}, rows_per_column});
    if (count <= 1U) return layouts;
    std::vector<RibbonCommandSize> sizes;
    sizes.reserve(count);
    for (const RibbonCommandSpec& command : group.commands) sizes.push_back(command.size);
    bool first_conversion = true;
    for (;;) {
        std::size_t end = count;
        while (end > 0U && !convertible(group.commands[end - 1U], sizes[end - 1U])) --end;
        if (end == 0U) break;
        std::size_t converted = 0U;
        std::size_t index = end;
        while (index > 0U && converted < rows_per_column && convertible(group.commands[index - 1U], sizes[index - 1U])) {
            sizes[index - 1U] = RibbonCommandSize::small;
            --index;
            ++converted;
        }
        const RibbonGroupSize size = first_conversion ? RibbonGroupSize::medium : RibbonGroupSize::small;
        layouts.push_back(RibbonGroupLayout{size, sizes, rows_per_column});
        first_conversion = false;
    }
    layouts.push_back(RibbonGroupLayout{RibbonGroupSize::collapsed, {}, rows_per_column});
    return layouts;
}

void validate_reduction_order(const RibbonTabSpec& tab, const std::vector<std::vector<RibbonGroupLayout>>& layouts) {
    if (tab.reduction_order.size() > Ribbon::maximum_reduction_steps) {
        throw std::invalid_argument("Ribbon tab has more reduction steps than maximum_reduction_steps");
    }
    const std::size_t groups = tab.groups.size();
    std::vector<std::size_t> layout_steps(groups, 0U);
    std::vector<std::size_t> gallery_steps(groups, 0U);
    for (const RibbonReductionStep& step : tab.reduction_order) {
        std::optional<std::size_t> found{};
        for (std::size_t group = 0; group < groups; ++group) {
            if (tab.groups[group].stable_id == step.group) found = group;
        }
        if (!found) throw std::invalid_argument("Ribbon reduction step names a group not on its tab");
        const std::size_t group = *found;
        const std::vector<RibbonGroupLayout>& group_layouts = layouts[group];
        if (step.kind == RibbonReductionKind::layout) {
            ++layout_steps[group];
            if (layout_steps[group] >= group_layouts.size()) {
                throw std::invalid_argument("Ribbon reduction order shrinks a group past its last layout");
            }
            continue;
        }
        const std::optional<std::size_t> gallery = gallery_of(tab.groups[group]);
        if (!gallery) throw std::invalid_argument("Ribbon gallery step names a group without a gallery");
        if (group_layouts[layout_steps[group]].size == RibbonGroupSize::collapsed) {
            throw std::invalid_argument("Ribbon gallery step follows its group's collapse");
        }
        const RibbonGallery& control = *tab.groups[group].commands[*gallery].gallery;
        ++gallery_steps[group];
        if (gallery_steps[group] > control.maximum_columns() - control.minimum_columns()) {
            throw std::invalid_argument("Ribbon reduction order shrinks a gallery past its minimum columns");
        }
    }
}

} // namespace ribbon_detail

// -- public queries ------------------------------------------------------------

std::span<const RibbonGroupLayout> Ribbon::group_layouts(std::size_t tab, std::size_t group) const {
    if (tab >= layouts_.size() || group >= layouts_[tab].size()) {
        throw std::out_of_range("Ribbon tab or group index is past the end");
    }
    return layouts_[tab][group];
}

std::vector<RibbonLadderEntry> Ribbon::reduction_ladder(std::size_t tab) const {
    if (tab >= tabs_.size()) throw std::out_of_range("Ribbon tab index is past the end");
    const TabScaling& scaling = tab_scaling(tab);
    std::vector<RibbonLadderEntry> ladder = scaling.ladder;
    return ladder;
}

std::optional<std::size_t> Ribbon::ladder_step() const {
    const Layout placed = layout();
    return placed.step;
}

bool Ribbon::scrolling() const {
    const Layout placed = layout();
    return placed.scrolling;
}

// -- caches --------------------------------------------------------------------

double Ribbon::text_width(std::string_view text) const {
    if (text.empty()) return 0.0;
    ++counters_.text_measurements;
    const ResolvedTextLayout metrics = resolve_text_layout_utf8(text, text_font(font.read()));
    return metrics.logical_size.width;
}

Ribbon::ScalingKey Ribbon::current_scaling_key() const {
    ScalingKey key;
    key.content = content_generation_;
    key.text_scale = effective_text_scale();
    key.theme = &effective_theme();
    const Window* const host = attached_window();
    key.provider = host != nullptr ? (*host).text_metrics_provider() : nullptr;
    key.font = font.read();
    if (host != nullptr) {
        key.media = window_media_facts(*host);
        key.user_rules = (*host).user_theme_rules();
    }
    key.inline_rules = inline_style_rules();
    return key;
}

// Empties every measured cache when what they depend on changed. Capacity is
// kept for the rebuild.
void Ribbon::refresh_scaling_key() const {
    const ScalingKey key = current_scaling_key();
    if (key == scaling_key_) return;
    scaling_key_ = key;
    row_metrics_.built = false;
    for (TabScaling& scaling : scaling_) scaling.built = false;
}

// The tab row's metrics: the theme's where its rules set them (look
// integration, Plan Paint defect 5), else the ribbon's own; each scaled by
// the text scale. A tab is as wide as its authored width, or as its label
// at the wider of its resting and selected fonts plus the padding, so that
// selecting a tab never moves the others; then at least the theme's
// minimum width.
const Ribbon::RowMetrics& Ribbon::row_metrics() const {
    refresh_scaling_key();
    if (row_metrics_.built) return row_metrics_;
    const double scale = effective_text_scale();
    AnatomyStateSet selected;
    selected.insert(AnatomyState::selected);
    const ControlPartMetrics rail = part_metrics("tab_rail", ControlVisualRole::panel, {});
    const ControlPartMetrics plane = part_metrics("command_plane", ControlVisualRole::panel, {});
    const ControlPartMetrics application = part_metrics("application_button", ControlVisualRole::accent_button, {});
    const ControlPartMetrics rest = part_metrics("tab", ControlVisualRole::menu_item, {});
    const ControlPartMetrics chosen = part_metrics("tab", ControlVisualRole::menu_item, selected);
    const double row = std::max(1.0, rail.block_size.value_or(tab_row_extent)) * scale;
    const double rows = row_extent * static_cast<double>(rows_per_column);
    const double stock_plane = plane_top_padding + rows + caption_extent + plane_bottom_padding;
    row_metrics_.row_height = row;
    row_metrics_.plane_height = std::max(1.0, plane.block_size.value_or(stock_plane)) * scale;
    row_metrics_.padding = rest.padding_inline.value_or(tab_padding) * scale;
    row_metrics_.application_padding = application.padding_inline.value_or(tab_padding) * scale;
    row_metrics_.gap = rest.gap.value_or(tab_gap) * scale;
    // A top margin leaves at least one unit of the tab; a bottom margin
    // may be negative, running the tab below the rail into the plane.
    const double highest = std::max(0.0, row - 1.0);
    row_metrics_.application_margins.top = std::clamp(application.margin_block_start.value_or(0.0) * scale, 0.0, highest);
    row_metrics_.application_margins.bottom = application.margin_block_end.value_or(0.0) * scale;
    row_metrics_.rest.top = std::clamp(rest.margin_block_start.value_or(0.0) * scale, 0.0, highest);
    row_metrics_.rest.bottom = rest.margin_block_end.value_or(0.0) * scale;
    row_metrics_.selected.top = std::clamp(chosen.margin_block_start.value_or(0.0) * scale, 0.0, highest);
    row_metrics_.selected.bottom = chosen.margin_block_end.value_or(0.0) * scale;
    const double application_minimum = application.minimum_inline_size.value_or(application_minimum_width) * scale;
    const double application_width = text_width(parse_mnemonic_text(application_text.read()).display_text);
    row_metrics_.application =
        std::max(application_minimum, application_width + row_metrics_.application_padding * 2.0);
    const double tab_minimum = rest.minimum_inline_size.value_or(0.0) * scale;
    row_metrics_.tabs.clear();
    row_metrics_.tabs.reserve(tabs_.size());
    for (const RibbonTabSpec& tab : tabs_) {
        double width = 0.0;
        if (tab.width) {
            width = *tab.width * scale;
        } else {
            const std::string label = parse_mnemonic_text(tab.text).display_text;
            const double resting = tab_label_width(label, {});
            const double raised = tab_label_width(label, selected);
            width = std::max(resting, raised) + row_metrics_.padding * 2.0;
        }
        row_metrics_.tabs.push_back(std::max(width, tab_minimum));
    }
    row_metrics_.built = true;
    return row_metrics_;
}

const Ribbon::TabScaling& Ribbon::tab_scaling(std::size_t tab) const {
    refresh_scaling_key();
    TabScaling& scaling = scaling_[tab];
    if (scaling.built) {
        // A gallery's own range or item size may have changed since.
        const std::vector<RibbonGroupSpec>& groups = tabs_[tab].groups;
        for (std::size_t group = 0; group < groups.size() && scaling.built; ++group) {
            if (!scaling.galleries[group]) continue;
            const RibbonGallery& gallery = *groups[group].commands[*scaling.galleries[group]].gallery;
            const GalleryMetrics now{gallery.minimum_columns(), gallery.maximum_columns(),
                                     gallery.width_for_columns(gallery.maximum_columns())};
            if (!(now == scaling.gallery_metrics[group])) scaling.built = false;
        }
    }
    if (scaling.built) return scaling;
    measure_tab(tab, scaling);
    build_ladder(tab, scaling);
    scaling.built = true;
    return scaling;
}

Ribbon::CommandMetrics Ribbon::measure_command(const RibbonCommandSpec& command) const {
    const double scale = effective_text_scale();
    CommandMetrics metrics;
    if (command.kind == RibbonCommandKind::gallery) return metrics;
    // The application's row controls measure themselves, without text.
    if (command.kind == RibbonCommandKind::font_control || command.kind == RibbonCommandKind::color_picker) {
        const Size natural = command.kind == RibbonCommandKind::font_control ? (*command.font_control).natural_size()
                                                                            : (*command.color_picker).natural_size();
        double width = natural.width;
        if (command.width > 0.0) width = command.width * scale;
        metrics.large_width = width;
        metrics.small_width = width;
        return metrics;
    }
    if (hosted(command.kind)) {
        const double field = hosted_field_width(command);
        metrics.large_width = field;
        metrics.small_width = field;
        // A labelled field: its label at its left (small) or above it (large).
        if (field_labelled(command)) {
            const double label = text_width(command_text(command));
            metrics.large_width = std::max(field, label);
            metrics.small_width = label + field_label_gap * scale + field;
        }
        return metrics;
    }
    const double label = command.show_label ? text_width(command_text(command)) : 0.0;
    metrics.large_width = std::max(large_minimum_width * scale, label + large_label_padding * scale);
    double small = icon_gap * scale;
    if (command.image.value != 0U) small += (small_icon + icon_gap) * scale;
    if (command.show_label) small += label + small_label_padding * scale;
    if (command.image.value == 0U) small += icon_gap * scale;
    if (has_menu(command.kind)) small += arrow_zone * scale;
    metrics.small_width = small;
    return metrics;
}

// Whether `command` is a text or number field showing its label part.
bool Ribbon::field_labelled(const RibbonCommandSpec& command) const {
    const bool result = labelled_field_kind(command.kind) && command.show_label && !command_text(command).empty();
    return result;
}

// A hosted command's field width in pixels: its authored width or its kind's
// default, at the text scale; a check box is as wide as its mark and caption.
double Ribbon::hosted_field_width(const RibbonCommandSpec& command) const {
    const double scale = effective_text_scale();
    double width = command.width;
    if (width <= 0.0 && command.kind == RibbonCommandKind::combo_box) width = default_combo_box_width;
    if (width <= 0.0 && command.kind == RibbonCommandKind::numeric_up_down) width = default_numeric_width;
    if (width <= 0.0 && command.kind == RibbonCommandKind::text_box) width = default_text_box_width;
    double measured = width * scale;
    if (width <= 0.0) measured = check_box_mark_width * scale + text_width(command_text(command));
    return measured;
}

// Splits a hosted command's placed `bounds` into its label and its field.
// A field two rows tall carries its label above it (one-row layouts); a
// field one row tall carries its label at its left, the field at the right
// end. Measures no text: the field keeps its own width and the label takes
// the rest.
Ribbon::FieldSplit Ribbon::split_field(const RibbonCommandSpec& command, Rect bounds) const {
    FieldSplit split;
    split.field = bounds;
    if (bounds.empty() || !field_labelled(command)) return split;
    const double scale = effective_text_scale();
    const double row_height = row_extent * scale;
    const double field = std::min(hosted_field_width(command), bounds.width);
    if (bounds.height > row_height * 1.5) {
        split.label = {bounds.x, bounds.y, bounds.width, row_height};
        split.field = {bounds.x, bounds.y + row_height, field, bounds.height - row_height};
        return split;
    }
    const double label = std::max(0.0, bounds.width - field - field_label_gap * scale);
    split.label = {bounds.x, bounds.y, label, bounds.height};
    split.field = {bounds.x + bounds.width - field, bounds.y, field, bounds.height};
    return split;
}

// Measures every label and caption of `tab` once.
void Ribbon::measure_tab(std::size_t tab, TabScaling& scaling) const {
    const double scale = effective_text_scale();
    const std::vector<RibbonGroupSpec>& groups = tabs_[tab].groups;
    scaling.commands.resize(groups.size());
    scaling.captions.assign(groups.size(), 0.0);
    scaling.collapsed.assign(groups.size(), 0.0);
    scaling.galleries.assign(groups.size(), std::nullopt);
    scaling.gallery_metrics.assign(groups.size(), GalleryMetrics{});
    for (std::size_t group = 0; group < groups.size(); ++group) {
        const RibbonGroupSpec& spec = groups[group];
        std::vector<CommandMetrics>& commands = scaling.commands[group];
        commands.clear();
        commands.reserve(spec.commands.size());
        for (const RibbonCommandSpec& command : spec.commands) commands.push_back(measure_command(command));
        const double caption = text_width(spec.caption);
        double row = caption + caption_padding * scale;
        if (spec.dialog_launcher) row += (launcher_size + launcher_gap) * scale;
        scaling.captions[group] = row;
        scaling.collapsed[group] = std::max(large_minimum_width * scale, caption + large_label_padding * scale);
        scaling.galleries[group] = gallery_of(spec);
        if (scaling.galleries[group]) {
            const RibbonGallery& gallery = *spec.commands[*scaling.galleries[group]].gallery;
            scaling.gallery_metrics[group] = GalleryMetrics{gallery.minimum_columns(), gallery.maximum_columns(),
                                                            gallery.width_for_columns(gallery.maximum_columns())};
        }
    }
}

// The width the groups need side by side in `state`.
double Ribbon::state_width(const TabScaling& scaling, std::size_t tab,
                           const std::vector<RibbonLadderGroupState>& state) const {
    double width = 0.0;
    for (std::size_t group = 0; group < state.size(); ++group) {
        const RibbonGroupLayout& group_layout = layouts_[tab][group][state[group].layout];
        width += place_group(scaling, tab, group, group_layout, state[group].gallery_columns, 0.0, 0.0,
                             scratch_commands_);
    }
    return width;
}

// The reduction ladder (RIBBON_SCALING_STUDY.md §5.5): from every group at
// its largest layout and its gallery at its widest, each step shrinks one
// group, first by the tab's authored order, then by WPF's cycle (owner
// decision 32): from the rightmost group leftwards, one step per group,
// wrapping, where a group with a gallery above its minimum gives up a column
// (decision 8) before it changes layout. A step that does not narrow the tab
// is folded into the next entry, so widths strictly decrease. The ladder
// depends only on the definition and measured widths, never on the resize
// history.
void Ribbon::build_ladder(std::size_t tab, TabScaling& scaling) const {
    const RibbonTabSpec& spec = tabs_[tab];
    const std::size_t groups = spec.groups.size();
    const std::vector<std::vector<RibbonGroupLayout>>& layouts = layouts_[tab];
    std::vector<RibbonLadderGroupState> state(groups);
    for (std::size_t group = 0; group < groups; ++group) {
        state[group].layout = 0U;
        state[group].size = layouts[group][0].size;
        if (scaling.galleries[group]) state[group].gallery_columns = scaling.gallery_metrics[group].maximum;
    }
    scaling.ladder.clear();
    scaling.ladder.push_back(RibbonLadderEntry{state, state_width(scaling, tab, state)});
    std::uint64_t nonshrinking = 0U;
    std::size_t authored = 0U;
    std::size_t cursor = groups == 0U ? 0U : groups - 1U;
    while (groups != 0U) {
        bool stepped = false;
        // The authored order first; a step that no longer applies (its
        // gallery's range changed since the ribbon was defined) is skipped.
        while (!stepped && authored < spec.reduction_order.size()) {
            const RibbonReductionStep& step = spec.reduction_order[authored];
            ++authored;
            std::size_t group = groups;
            for (std::size_t candidate = 0; candidate < groups; ++candidate) {
                if (spec.groups[candidate].stable_id == step.group) group = candidate;
            }
            if (group == groups || !group_shown(tab, group)) continue;
            RibbonLadderGroupState& current = state[group];
            const bool collapsed = current.size == RibbonGroupSize::collapsed;
            if (step.kind == RibbonReductionKind::layout && current.layout + 1U < layouts[group].size()) {
                current.layout += 1U;
                current.size = layouts[group][current.layout].size;
                stepped = true;
            } else if (step.kind == RibbonReductionKind::gallery_column && scaling.galleries[group] && !collapsed &&
                       current.gallery_columns > scaling.gallery_metrics[group].minimum) {
                current.gallery_columns -= 1U;
                stepped = true;
            }
        }
        // Then the cycle.
        for (std::size_t tries = 0; !stepped && tries < groups; ++tries) {
            const std::size_t group = cursor;
            cursor = cursor == 0U ? groups - 1U : cursor - 1U;
            if (!group_shown(tab, group)) continue;
            RibbonLadderGroupState& current = state[group];
            const bool collapsed = current.size == RibbonGroupSize::collapsed;
            if (scaling.galleries[group] && !collapsed &&
                current.gallery_columns > scaling.gallery_metrics[group].minimum) {
                current.gallery_columns -= 1U;
                stepped = true;
            } else if (current.layout + 1U < layouts[group].size()) {
                current.layout += 1U;
                current.size = layouts[group][current.layout].size;
                stepped = true;
            }
        }
        if (!stepped) break;
        const double width = state_width(scaling, tab, state);
        if (width < scaling.ladder.back().width) {
            scaling.ladder.push_back(RibbonLadderEntry{state, width});
        } else {
            ++nonshrinking;
        }
    }
    ++counters_.ladder_builds;
    counters_.ladder_entries = scaling.ladder.size();
    counters_.nonshrinking_steps = nonshrinking;
}

// -- placement -----------------------------------------------------------------

// Lays out group `group` of tab `tab` in `group_layout`, its gallery at
// `columns`, its left edge at `x` and its command rows from `top`. Fills
// `commands` with one rectangle per command (empty for a hidden command and
// for every command of a collapsed layout) and returns the group's width.
// Measures no text: widths come from `scaling`.
//
// Columns: a large command or a gallery is a column of its own; small and
// hosted commands stack `rows` to a column, centred in equal row pitches, a
// column as wide as its widest. The group is as wide as its columns or its
// caption row, whichever is wider, plus padding and the divider.
double Ribbon::place_group(const TabScaling& scaling, std::size_t tab, std::size_t group,
                           const RibbonGroupLayout& group_layout, std::size_t columns, double x, double top,
                           std::vector<Rect>& commands) const {
    const double scale = effective_text_scale();
    const RibbonGroupSpec& spec = tabs_[tab].groups[group];
    commands.assign(spec.commands.size(), Rect{});
    // A group outside the application modes takes no room.
    if (!group_shown(tab, group)) return 0.0;
    const double padding = group_padding * scale;
    const double gap = column_gap * scale;
    if (group_layout.size == RibbonGroupSize::collapsed) {
        const double collapsed = scaling.collapsed[group] + padding * 2.0 + divider_width;
        return collapsed;
    }
    const double rows_height = row_extent * scale * static_cast<double>(rows_per_column);
    const double row_height = row_extent * scale;
    const double pitch = rows_height / static_cast<double>(group_layout.rows);
    const double row_offset = (pitch - row_height) * 0.5;
    double column_x = x + padding;
    double column_width = 0.0;
    std::size_t column_rows = 0U;
    for (std::size_t command = 0; command < spec.commands.size(); ++command) {
        const RibbonCommandSpec& item = spec.commands[command];
        if (!command_visible(item)) continue;
        const bool gallery = item.kind == RibbonCommandKind::gallery;
        const bool large =
            !hosted(item.kind) && layout_command_size(group_layout, item, command) == RibbonCommandSize::large;
        if ((gallery || large || column_rows == group_layout.rows) && column_rows != 0U) {
            column_x += column_width + gap;
            column_width = 0.0;
            column_rows = 0U;
        }
        if (gallery || large) {
            double width = scaling.commands[group][command].large_width;
            if (gallery) width = (*item.gallery).width_for_columns(columns);
            commands[command] = {column_x, top, width, rows_height};
            column_x += width + gap;
            continue;
        }
        // A labelled field in a one-row layout stacks its label above its
        // field, two rows tall and centred in the column.
        if (group_layout.rows == 1U && hosted(item.kind) && field_labelled(item)) {
            const double width = scaling.commands[group][command].large_width;
            const double height = row_height * 2.0;
            commands[command] = {column_x, top + (rows_height - height) * 0.5, width, height};
            column_width = std::max(column_width, width);
            ++column_rows;
            continue;
        }
        const double width = scaling.commands[group][command].small_width;
        const double y = top + static_cast<double>(column_rows) * pitch + row_offset;
        commands[command] = {column_x, y, width, row_height};
        column_width = std::max(column_width, width);
        ++column_rows;
    }
    if (column_rows != 0U) column_x += column_width + gap;
    const double content = column_x - gap - (x + padding);
    const double inner = std::max(std::max(0.0, content), scaling.captions[group]);
    const double width = inner + padding * 2.0 + divider_width;
    return width;
}

void Ribbon::reset_layout_groups(Layout& placed) const {
    const std::vector<RibbonGroupSpec>& groups = tabs_[*selected_tab_].groups;
    placed.groups.assign(groups.size(), Rect{});
    placed.group_layouts.assign(groups.size(), 0U);
    placed.group_sizes.assign(groups.size(), RibbonGroupSize::large);
    placed.gallery_columns.assign(groups.size(), 0U);
    placed.commands.resize(groups.size());
    for (std::size_t group = 0; group < groups.size(); ++group) {
        placed.commands[group].assign(groups[group].commands.size(), Rect{});
    }
}

// The selected tab's groups at the widest ladder entry the plane holds. When
// none fits the plane scrolls (owner decision 35): from first_group_, with a
// scroll button at the left once groups are scrolled away and at the right
// while groups remain past the edge; a group that does not fit whole before
// the right button is not shown, except the first, which is clipped.
void Ribbon::place_plane(Layout& placed) const {
    const std::size_t tab = *selected_tab_;
    const TabScaling& scaling = tab_scaling(tab);
    ++counters_.ladder_lookups;
    const std::vector<RibbonLadderEntry>& ladder = scaling.ladder;
    const double scale = effective_text_scale();
    const Rect plane = placed.plane;
    placed.groups_area = plane;
    const std::size_t groups = tabs_[tab].groups.size();
    std::vector<RibbonLadderEntry>::const_iterator found =
        std::lower_bound(ladder.begin(), ladder.end(), plane.width, EntryWiderThan{});
    // Within scroll_hysteresis of the narrowest entry a latched plane keeps
    // scrolling though that entry fits (owner decision 45).
    const bool fits = found != ladder.end();
    if (!fits || scroll_latched_) {
        found = ladder.end() - 1;
        placed.scrolling = groups != 0U;
    }
    const bool band = placed.scrolling && fits;
    const std::size_t step = static_cast<std::size_t>(found - ladder.begin());
    placed.step = step;
    const RibbonLadderEntry& entry = ladder[step];
    for (std::size_t group = 0; group < groups; ++group) {
        placed.group_layouts[group] = entry.groups[group].layout;
        placed.group_sizes[group] = entry.groups[group].size;
        placed.gallery_columns[group] = entry.groups[group].gallery_columns;
    }
    if (groups == 0U) return;
    const double top = plane.y + plane_top_padding * scale;
    const double button = scroll_button_width * scale;
    const std::size_t first = placed.scrolling ? std::min(first_group_, groups - 1U) : 0U;
    double x = plane.x;
    if (first > 0U) {
        placed.scroll_left = {plane.x, plane.y, button, plane.height};
        x += button;
    }
    const double start = x;
    for (std::size_t group = first; group < groups; ++group) {
        const RibbonGroupLayout& group_layout = layouts_[tab][group][placed.group_layouts[group]];
        const double width = place_group(scaling, tab, group, group_layout, placed.gallery_columns[group], x, top,
                                         placed.commands[group]);
        placed.groups[group] = {x, plane.y, width, plane.height};
        x += width;
    }
    const double plane_right = plane.x + plane.width;
    double limit = plane_right;
    // In the hysteresis band everything would fit: the right button still
    // shows, over the groups it covers.
    const bool right_needed = x > plane_right || (band && x > plane_right - button);
    if (placed.scrolling && right_needed) {
        limit = std::max(start, plane_right - button);
        placed.scroll_right = {limit, plane.y, plane_right - limit, plane.height};
        for (std::size_t group = first + 1U; group < groups; ++group) {
            const Rect bounds = placed.groups[group];
            if (bounds.x + bounds.width <= limit) continue;
            placed.groups[group] = {};
            for (Rect& command : placed.commands[group]) command = {};
        }
    }
    placed.groups_area = {start, plane.y, std::max(0.0, limit - start), plane.height};
}

Ribbon::Layout Ribbon::layout() const {
    return layout_for(client_rectangle());
}

Ribbon::Layout Ribbon::layout_for(Rect client) const {
    const RowMetrics& row_widths = row_metrics();
    Layout placed;
    const double row = row_widths.row_height;
    placed.tab_rail = {client.x, client.y, client.width, row};
    const TabMargins file = row_widths.application_margins;
    placed.application = {client.x, client.y + file.top, row_widths.application,
                          std::max(1.0, row - file.top - file.bottom)};
    double x = client.x + row_widths.application + row_widths.gap;
    placed.tabs.reserve(tabs_.size());
    // A tab's top and bottom follow its state's margins: the selected tab
    // stands taller than the rest and may run into the plane (RENDERING_
    // REFERENCE §4).
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        if (!shown(tab)) {
            placed.tabs.push_back(Rect{});
            continue;
        }
        const double width = row_widths.tabs[tab];
        const TabMargins margins = selected_tab_ == tab ? row_widths.selected : row_widths.rest;
        placed.tabs.push_back({x, client.y + margins.top, width, std::max(1.0, row - margins.top - margins.bottom)});
        x += width + row_widths.gap;
    }
    if (!selected_tab_) return placed;
    reset_layout_groups(placed);
    if (minimized.read() || plane_hidden_) return placed;
    placed.plane = {client.x, client.y + row, client.width, plane_height()};
    place_plane(placed);
    return placed;
}

// The overlay's view: the overlay's group at its largest layout, its gallery at
// its widest, in `panel` (the overlay content's local coordinates).
Ribbon::Layout Ribbon::overlay_layout(Rect panel) const {
    Layout placed;
    placed.overlay = true;
    if (!selected_tab_ || !overlay_group_) return placed;
    reset_layout_groups(placed);
    const std::size_t tab = *selected_tab_;
    const std::size_t group = *overlay_group_;
    const TabScaling& scaling = tab_scaling(tab);
    placed.plane = panel;
    placed.groups_area = panel;
    const std::size_t columns = scaling.galleries[group] ? scaling.gallery_metrics[group].maximum : 0U;
    const double top = panel.y + plane_top_padding * effective_text_scale();
    const RibbonGroupLayout& largest = layouts_[tab][group][0];
    const double width = place_group(scaling, tab, group, largest, columns, panel.x, top, placed.commands[group]);
    placed.groups[group] = {panel.x, panel.y, width, panel.height};
    placed.group_layouts[group] = 0U;
    placed.group_sizes[group] = largest.size;
    placed.gallery_columns[group] = columns;
    return placed;
}

Size Ribbon::overlay_panel_size() const {
    if (!selected_tab_ || !overlay_group_) return {};
    const std::size_t tab = *selected_tab_;
    const std::size_t group = *overlay_group_;
    const TabScaling& scaling = tab_scaling(tab);
    const std::size_t columns = scaling.galleries[group] ? scaling.gallery_metrics[group].maximum : 0U;
    const double width =
        place_group(scaling, tab, group, layouts_[tab][group][0], columns, 0.0, 0.0, scratch_commands_);
    const Size size{width, plane_height()};
    return size;
}

void Ribbon::scroll_groups(bool forward) {
    const Layout placed = layout();
    if (!placed.scrolling) return;
    if (forward && !placed.scroll_right.empty()) {
        ++first_group_;
    } else if (!forward && first_group_ > 0U) {
        --first_group_;
    } else {
        return;
    }
    hot_ = Hit{};
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

bool Ribbon::group_collapsed(const Layout& placed, std::size_t group) const noexcept {
    const bool result = !placed.overlay && group < placed.group_sizes.size() &&
                        placed.group_sizes[group] == RibbonGroupSize::collapsed;
    return result;
}

// The collapsed button's icon: the group's own, or its first command image
// (UX7: the most prominent command's).
ImageId Ribbon::group_image(const RibbonGroupSpec& group) const noexcept {
    if (group.image.value != 0U) return group.image;
    for (const RibbonCommandSpec& command : group.commands) {
        if (command.image.value != 0U) return command.image;
    }
    return ImageId{};
}

} // namespace gui_forms
