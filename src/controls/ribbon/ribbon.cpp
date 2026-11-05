#include "gui_forms/controls/ribbon/ribbon.hpp"

#include "../menu/menu_utilities.hpp"
#include "ribbon_application_menu.hpp"
#include "ribbon_geometry.hpp"
#include "ribbon_group_popup.hpp"
#include "ribbon_scaling.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {

using namespace ribbon_detail;

namespace {

std::string node_identity(std::string_view ribbon_id, std::string_view kind, std::string_view id) {
    std::string identity(ribbon_id);
    identity.append(".");
    identity.append(kind);
    identity.append(".");
    identity.append(id);
    return identity;
}

// The part of `identity` after "<ribbon>.<kind>.", or empty.
std::string_view node_suffix(std::string_view identity, std::string_view ribbon_id, std::string_view kind) {
    const std::size_t prefix = ribbon_id.size() + kind.size() + 2U;
    if (identity.size() <= prefix || !identity.starts_with(ribbon_id) || identity[ribbon_id.size()] != '.') {
        return {};
    }
    const std::string_view rest = identity.substr(ribbon_id.size() + 1U);
    if (!rest.starts_with(kind) || rest[kind.size()] != '.') return {};
    return identity.substr(prefix);
}

Rect to_window(Rect absolute, Rect local) noexcept {
    const Rect result{absolute.x + local.x, absolute.y + local.y, local.width, local.height};
    return result;
}

// The key under which a hosted control carries over a set_tabs: its
// command's ID and kind.
std::string hosted_key(const RibbonCommandSpec& command) {
    std::string key = command.stable_id;
    key.push_back('/');
    key.push_back(static_cast<char>('0' + static_cast<int>(command.kind)));
    return key;
}

} // namespace

// -- construction --------------------------------------------------------------

Ribbon::Ribbon(StableId ribbon_id) : Control(std::move(ribbon_id)) {
    set_focusable(true);
    set_paint_plane(PaintPlane::control);
    popup_ = std::make_unique<ContextMenu>(std::string(stable_id().value()) + ".menu");
    popup_invoked_ = (*popup_).item_invoked().subscribe(
        *this, Delegate<const MenuItemInvocation&>::bind<Ribbon, &Ribbon::on_popup_item_invoked>(*this));
    popup_changed_ = (*popup_).open_changed().subscribe(
        *this, Delegate<bool>::bind<Ribbon, &Ribbon::on_popup_open_changed>(*this));
}

// A live ribbon is disposed (closing its popups) before it is destroyed;
// here the popup content only stops forwarding to it.
Ribbon::~Ribbon() {
    if (popup_) (*popup_).close();
    if (group_popup_) (*group_popup_).detach();
    if (application_popup_) (*application_popup_).detach();
}

void Ribbon::on_dispose() noexcept {
    close_menus();
    close_group_popup();
    close_backstage();
    if (popup_) (*popup_).dispose();
    if (group_popup_) (*group_popup_).detach();
    if (group_layer_) (*group_layer_).dispose();
    if (application_popup_) (*application_popup_).detach();
}

void Ribbon::on_attachment_committed() noexcept {
    Control::on_attachment_committed();
    Window* const host = attached_window();
    if (!focus_region_ || host == nullptr) return;
    // A ribbon nested in another region, or already registered, stays out.
    try {
        static_cast<void>((*host).add_focus_region(shared_from_this()));
    } catch (...) {
    }
}

void Ribbon::set_focus_region(bool region) {
    require_mutable();
    if (focus_region_ == region) return;
    focus_region_ = region;
    Window* const host = attached_window();
    if (host == nullptr) return;
    const Control::Ptr self = shared_from_this();
    if (region) {
        static_cast<void>((*host).add_focus_region(self));
        return;
    }
    std::vector<Control::Ptr> remaining = (*host).focus_regions();
    remaining.erase(std::remove(remaining.begin(), remaining.end(), self), remaining.end());
    (*host).set_focus_regions(std::move(remaining));
}

// -- model ---------------------------------------------------------------------

namespace {

// Checks the tab sets (their IDs join `identities`) and where each tab sits:
// core tabs first, then each set's tabs together, in the sets' order, every
// set with tabs and every tab with a mode. Returns each tab's set, none for a
// core tab. Throws std::invalid_argument and changes nothing else.
std::vector<std::optional<std::size_t>> validate_tab_sets(const std::vector<RibbonTabSpec>& tabs,
                                                          const std::vector<RibbonTabSetSpec>& sets,
                                                          std::unordered_set<std::string>& identities) {
    for (const RibbonTabSetSpec& set : sets) {
        if (!valid_stable_id(set.stable_id) || !validate_utf8(set.caption).valid() ||
            !identities.insert(set.stable_id).second) {
            throw std::invalid_argument(
                "Ribbon tab sets require unique stable IDs following the StableId segment rule and valid UTF-8");
        }
    }
    std::vector<std::optional<std::size_t>> tab_set_of(tabs.size());
    std::vector<std::size_t> set_tab_counts(sets.size(), 0U);
    std::optional<std::size_t> current_set{};
    for (std::size_t tab_index = 0; tab_index < tabs.size(); ++tab_index) {
        const RibbonTabSpec& tab = tabs[tab_index];
        if (tab.modes == 0U) throw std::invalid_argument("Ribbon tabs need at least one application mode");
        if (tab.tab_set.empty()) {
            if (current_set) throw std::invalid_argument("Ribbon core tabs come before every contextual tab");
            continue;
        }
        std::optional<std::size_t> set{};
        for (std::size_t candidate = 0; candidate < sets.size(); ++candidate) {
            if (sets[candidate].stable_id == tab.tab_set) set = candidate;
        }
        if (!set) throw std::invalid_argument("Ribbon tab names an undeclared tab set");
        if (current_set && *set < *current_set) {
            throw std::invalid_argument("Ribbon contextual tabs come together, in their sets' order");
        }
        if (current_set != set && set_tab_counts[*set] != 0U) {
            throw std::invalid_argument("Ribbon contextual tabs come together, in their sets' order");
        }
        current_set = set;
        ++set_tab_counts[*set];
        tab_set_of[tab_index] = set;
    }
    for (const std::size_t count : set_tab_counts) {
        if (count == 0U) throw std::invalid_argument("Ribbon tab set has no tabs");
    }
    return tab_set_of;
}

// Checks one command record: its ID (joining `identities`), its Command, menu,
// label, choices and width by kind, and the application's control it holds
// (joining `authored`), which must be unowned or already the ribbon's
// (`ribbon`) or its group popup's (`popup`). Throws std::invalid_argument.
void validate_command(const RibbonCommandSpec& command, std::unordered_set<std::string>& identities,
                      std::unordered_set<const Control*>& authored, const Control* ribbon, const Control* popup) {
    if (!valid_stable_id(command.stable_id) ||
        !validate_utf8(command.text).valid() || !identities.insert(command.stable_id).second) {
        throw std::invalid_argument(
            "Ribbon commands require unique stable IDs following the StableId segment rule and valid UTF-8");
    }
    if (needs_command(command.kind) && !command.command) {
        throw std::invalid_argument("Ribbon buttons, toggles and splits require a Command");
    }
    if (hosted(command.kind) && command.command) {
        throw std::invalid_argument("Ribbon hosted commands take no Command");
    }
    if (has_menu(command.kind) != !command.drop_down.empty()) {
        throw std::invalid_argument("Ribbon split and drop-down commands require a menu, and others have none");
    }
    if (!command.show_label && (command.size != RibbonCommandSize::small || command.image.value == 0U ||
                                hosted(command.kind))) {
        throw std::invalid_argument("Ribbon icon-only commands must be small and have an image");
    }
    if (command.kind != RibbonCommandKind::combo_box && !command.choices.empty()) {
        throw std::invalid_argument("Ribbon choices belong to combo box commands only");
    }
    for (const std::string& choice : command.choices) {
        if (!validate_utf8(choice).valid()) throw std::invalid_argument("Ribbon choices must be valid UTF-8");
    }
    if (!std::isfinite(command.width) || command.width < 0.0 || command.width > maximum_hosted_width) {
        throw std::invalid_argument("Ribbon command width must be finite and between 0 and 1024");
    }
    // A gallery, font control or colour picker command holds exactly its own
    // kind's control; every other kind holds none.
    const std::size_t controls = static_cast<std::size_t>(static_cast<bool>(command.gallery)) +
                                 static_cast<std::size_t>(static_cast<bool>(command.font_control)) +
                                 static_cast<std::size_t>(static_cast<bool>(command.color_picker));
    const Control::Ptr own = authored_control(command);
    if (authored_kind(command.kind) != static_cast<bool>(own) || controls > (own ? 1U : 0U)) {
        throw std::invalid_argument(
            "Ribbon gallery, font control and colour picker commands hold their control, and others none");
    }
    if (own) {
        const Control::Ptr parent = (*own).parent();
        const bool ours = !parent || parent.get() == ribbon || parent.get() == popup;
        if (!ours || !authored.insert(own.get()).second) {
            throw std::invalid_argument("Ribbon hosted controls must be used once and not owned elsewhere");
        }
    }
    if (command.gallery && (*command.gallery).form() != RibbonGalleryForm::in_ribbon) {
        throw std::invalid_argument("Ribbon galleries must be in-ribbon, used once and not owned by another parent");
    }
    if (!command.drop_down.empty()) {
        std::unordered_set<std::string> menu_identities{};
        std::size_t count{};
        menu_detail::validate_specs(command.drop_down, menu_identities, 1U, count);
    }
}

} // namespace

// Validates the whole definition before anything changes (house style §2)
// and prepares what set_tabs commits: each tab's set and every group's
// layouts, authored (validated) or generated. Throws std::invalid_argument.
Ribbon::PreparedTabs Ribbon::prepare_tabs(const std::vector<RibbonTabSpec>& tabs,
                                          const std::vector<RibbonTabSetSpec>& sets) const {
    if (tabs.size() > maximum_tabs) throw std::invalid_argument("Ribbon has more tabs than maximum_tabs");
    // One ID namespace across sets, tabs, groups and commands.
    std::unordered_set<std::string> identities{};
    PreparedTabs prepared;
    prepared.tab_set_of = validate_tab_sets(tabs, sets, identities);
    std::unordered_set<const Control*> authored{};
    prepared.layouts.resize(tabs.size());
    for (std::size_t tab_index = 0; tab_index < tabs.size(); ++tab_index) {
        const RibbonTabSpec& tab = tabs[tab_index];
        if (!valid_stable_id(tab.stable_id) || !validate_utf8(tab.text).valid() ||
            !identities.insert(tab.stable_id).second) {
            throw std::invalid_argument(
                "Ribbon tabs require unique stable IDs following the StableId segment rule and valid UTF-8");
        }
        if (tab.groups.size() > maximum_groups_per_tab) {
            throw std::invalid_argument("Ribbon tab has more groups than maximum_groups_per_tab");
        }
        prepared.layouts[tab_index].reserve(tab.groups.size());
        for (const RibbonGroupSpec& group : tab.groups) {
            if (!valid_stable_id(group.stable_id) ||
                !validate_utf8(group.caption).valid() || !identities.insert(group.stable_id).second) {
                throw std::invalid_argument(
                    "Ribbon groups require unique stable IDs following the StableId segment rule and valid UTF-8");
            }
            if (group.commands.size() > maximum_commands_per_group) {
                throw std::invalid_argument("Ribbon group has more commands than maximum_commands_per_group");
            }
            if (group.modes == 0U) throw std::invalid_argument("Ribbon groups need at least one application mode");
            std::size_t group_galleries = 0U;
            for (const RibbonCommandSpec& command : group.commands) {
                validate_command(command, identities, authored, this, group_popup_.get());
                if (command.gallery) ++group_galleries;
            }
            if (group_galleries > 1U) throw std::invalid_argument("Ribbon group holds more than one gallery");
            prepared.layouts[tab_index].push_back(prepare_group_layouts(group));
        }
        validate_reduction_order(tab, prepared.layouts[tab_index]);
    }
    return prepared;
}

// Takes the hosted controls out of the current records, leaving those
// records without them: ribbon-made controls with their records, keyed by
// their command's ID and kind; the application's controls by identity.
Ribbon::HostedCarryOver Ribbon::take_hosted_controls() {
    HostedCarryOver carried;
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        for (std::size_t group = 0; group < tabs_[tab].groups.size(); ++group) {
            for (std::size_t command = 0; command < tabs_[tab].groups[group].commands.size(); ++command) {
                CommandRecord& record = records_[tab][group][command];
                if (!record.hosted) continue;
                const RibbonCommandSpec& spec = tabs_[tab].groups[group].commands[command];
                if (authored_kind(spec.kind)) {
                    carried.authored.push_back(std::move(record.hosted));
                    continue;
                }
                carried.made.emplace_back(hosted_key(spec), std::move(record));
            }
        }
    }
    return carried;
}

// The new sets' states: availability and last accent carried over by set ID,
// and each set's range of tabs from `tab_set_of`.
std::vector<Ribbon::TabSetState> Ribbon::carried_set_states(
    const std::vector<RibbonTabSetSpec>& sets, const std::vector<std::optional<std::size_t>>& tab_set_of) const {
    std::vector<TabSetState> states(sets.size());
    for (std::size_t set = 0; set < sets.size(); ++set) {
        for (std::size_t old = 0; old < tab_sets_.size(); ++old) {
            if (tab_sets_[old].stable_id != sets[set].stable_id) continue;
            states[set].availability = set_states_[old].availability;
            states[set].last_accent = set_states_[old].accent ? set_states_[old].accent : set_states_[old].last_accent;
        }
    }
    for (std::size_t tab = 0; tab < tab_set_of.size(); ++tab) {
        if (!tab_set_of[tab]) continue;
        TabSetState& state = states[*tab_set_of[tab]];
        if (state.tab_count == 0U) state.first_tab = tab;
        ++state.tab_count;
    }
    return states;
}

// Makes one record per command of tabs_: its hosted control, carried over
// (a ribbon-made one by ID and kind, the application's by identity) or made
// and added, configured from its command; and a watch on its Command. Takes
// what it reuses out of `carried`.
void Ribbon::rebuild_records(HostedCarryOver& carried) {
    records_.clear();
    records_.resize(tabs_.size());
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        records_[tab].resize(tabs_[tab].groups.size());
        for (std::size_t group = 0; group < tabs_[tab].groups.size(); ++group) {
            const std::vector<RibbonCommandSpec>& commands = tabs_[tab].groups[group].commands;
            records_[tab][group].resize(commands.size());
            for (std::size_t command = 0; command < commands.size(); ++command) {
                const RibbonCommandSpec& spec = commands[command];
                CommandRecord& record = records_[tab][group][command];
                if (authored_kind(spec.kind)) {
                    const Control::Ptr own = authored_control(spec);
                    for (Control::Ptr& old : carried.authored) {
                        if (old.get() != own.get()) continue;
                        record.hosted = std::move(old);
                        break;
                    }
                    if (!record.hosted) {
                        record.hosted = own;
                        if ((*own).parent().get() != this) add_child(record.hosted);
                    }
                } else if (hosted(spec.kind)) {
                    const std::string key = hosted_key(spec);
                    for (std::pair<std::string, CommandRecord>& old : carried.made) {
                        if (old.first != key || !old.second.hosted) continue;
                        record.hosted = std::move(old.second.hosted);
                        break;
                    }
                    if (!record.hosted) {
                        record.hosted = make_hosted(spec);
                        add_child(record.hosted);
                    }
                    configure_hosted(spec, *record.hosted);
                }
                if (spec.command) {
                    const CommandWatch watch{this, tab, group, command};
                    record.watch = (*spec.command).state_changed().subscribe(*this, watch);
                    record.text = (*spec.command).state().text;
                    record.visible = (*spec.command).state().visible;
                }
            }
        }
    }
}

// Removes the hosted controls no new command took.
void Ribbon::release_carried(const HostedCarryOver& carried) {
    for (const std::pair<std::string, CommandRecord>& old : carried.made) {
        if (old.second.hosted) static_cast<void>(remove_child((*old.second.hosted).runtime_id()));
    }
    for (const Control::Ptr& old : carried.authored) {
        if (old && (*old).parent().get() == this) static_cast<void>(remove_child((*old).runtime_id()));
    }
}

// Validates and prepares everything first (prepare_tabs), so that a refusal
// leaves the ribbon as it was; then closes the popups, carries the hosted
// controls and the sets' states over, commits, and restores the selection.
void Ribbon::set_tabs(std::vector<RibbonTabSpec> tabs, std::vector<RibbonTabSetSpec> sets) {
    require_mutable();
    PreparedTabs prepared = prepare_tabs(tabs, sets);
    close_menus();
    close_group_popup();
    // Lives to the end: its records keep their Command watches until then.
    HostedCarryOver carried = take_hosted_controls();
    std::string selected_id;
    if (selected_tab_) selected_id = tabs_[*selected_tab_].stable_id;
    std::vector<TabSetState> states = carried_set_states(sets, prepared.tab_set_of);
    tabs_ = std::move(tabs);
    tab_sets_ = std::move(sets);
    set_states_ = std::move(states);
    tab_set_of_ = std::move(prepared.tab_set_of);
    layouts_ = std::move(prepared.layouts);
    rebuild_records(carried);
    release_carried(carried);
    refresh_accents();
    selected_tab_.reset();
    last_core_tab_.reset();
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        if (tabs_[tab].stable_id == selected_id && shown(tab)) selected_tab_ = tab;
    }
    if (!selected_tab_) selected_tab_ = fallback_tab();
    if (selected_tab_ && !tab_set_of_[*selected_tab_]) last_core_tab_ = selected_tab_;
    scaling_.assign(tabs_.size(), TabScaling{});
    ++content_generation_;
    first_group_ = 0U;
    hot_ = Hit{};
    pressed_command_.reset();
    pressed_launcher_.reset();
    pressed_group_.reset();
    pressed_scroll_ = Hit::Kind::none;
    stop_.reset();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

Control::Ptr Ribbon::make_hosted(const RibbonCommandSpec& command) const {
    const StableId identity(std::string(stable_id().value()) + "." + command.stable_id);
    if (command.kind == RibbonCommandKind::combo_box) return make_control<ComboBox>(identity);
    if (command.kind == RibbonCommandKind::numeric_up_down) return make_control<NumericUpDown>(identity);
    if (command.kind == RibbonCommandKind::check_box) return make_control<CheckBox>(identity, command.text);
    return make_control<TextBox>(identity);
}

void Ribbon::configure_hosted(const RibbonCommandSpec& command, Control& control) const {
    control.accessible_name = parse_mnemonic_text(command.text).display_text;
    if (command.kind == RibbonCommandKind::combo_box) {
        ComboBox& combo = static_cast<ComboBox&>(control);
        const std::span<const std::string> current = combo.items();
        const bool same = current.size() == command.choices.size() &&
                          std::equal(current.begin(), current.end(), command.choices.begin());
        if (!same) combo.set_items(command.choices);
    }
    if (command.kind == RibbonCommandKind::check_box) {
        CheckBox& box = static_cast<CheckBox&>(control);
        if (box.text() != command.text) box.set_text(command.text);
    }
}

Control::Ptr Ribbon::hosted_control(std::string_view command_id, RibbonCommandKind kind) const {
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        for (std::size_t group = 0; group < tabs_[tab].groups.size(); ++group) {
            const std::vector<RibbonCommandSpec>& commands = tabs_[tab].groups[group].commands;
            for (std::size_t command = 0; command < commands.size(); ++command) {
                if (commands[command].stable_id != command_id) continue;
                if (commands[command].kind != kind) break;
                return records_[tab][group][command].hosted;
            }
        }
    }
    throw std::out_of_range("Ribbon has no hosted command of this kind with this ID");
}

std::shared_ptr<ComboBox> Ribbon::combo_box(std::string_view command_id) const {
    return std::static_pointer_cast<ComboBox>(hosted_control(command_id, RibbonCommandKind::combo_box));
}

std::shared_ptr<NumericUpDown> Ribbon::numeric_up_down(std::string_view command_id) const {
    return std::static_pointer_cast<NumericUpDown>(hosted_control(command_id, RibbonCommandKind::numeric_up_down));
}

std::shared_ptr<CheckBox> Ribbon::check_box(std::string_view command_id) const {
    return std::static_pointer_cast<CheckBox>(hosted_control(command_id, RibbonCommandKind::check_box));
}

std::shared_ptr<TextBox> Ribbon::text_box(std::string_view command_id) const {
    return std::static_pointer_cast<TextBox>(hosted_control(command_id, RibbonCommandKind::text_box));
}

void Ribbon::set_selected_tab(std::size_t tab) {
    require_mutable();
    if (tab >= tabs_.size()) throw std::out_of_range("Ribbon tab index is past the end");
    if (!shown(tab)) throw std::invalid_argument("Ribbon tab is not shown");
    select_tab(tab);
}

// Selects a shown tab. The user's choices are remembered for the selection
// rules: a set's tab until a core tab or another set's tab is chosen, and
// the core tab chosen last.
void Ribbon::select_tab(std::size_t tab, SelectionCause cause) {
    const std::optional<std::size_t> set = tab_set_of_[tab];
    if (cause == SelectionCause::user) {
        for (std::size_t other = 0; other < set_states_.size(); ++other) {
            if (!set || other != *set) set_states_[other].remembered_tab.reset();
        }
        if (set) set_states_[*set].remembered_tab = tab;
    }
    if (!set) last_core_tab_ = tab;
    if (selected_tab_ == tab) return;
    // Another tab has its own ladder; arrange latches scrolling afresh.
    scroll_latched_ = false;
    close_menus();
    close_group_popup();
    selected_tab_ = tab;
    first_group_ = 0U;
    hot_ = Hit{};
    pressed_command_.reset();
    pressed_launcher_.reset();
    pressed_group_.reset();
    if (stop_ && (*stop_).kind != StopKind::tab && (*stop_).kind != StopKind::application) {
        stop_ = Stop{StopKind::tab, tab, {}};
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    const RibbonTabChange change{std::string(stable_id().value()), tab, tabs_[tab].stable_id};
    publish_change(selected_tab_changed_, change);
}

void Ribbon::set_minimized(bool minimized) {
    require_mutable();
    if (minimized_ == minimized) return;
    close_menus();
    close_group_popup();
    minimized_ = minimized;
    hot_ = Hit{};
    if (minimized && stop_ && (*stop_).kind != StopKind::tab && (*stop_).kind != StopKind::application) {
        stop_ = Stop{StopKind::tab, selected_tab_.value_or(0U), {}};
    }
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    publish_change(minimized_changed_, minimized_);
}

void Ribbon::set_application_text(std::string text) {
    require_mutable();
    if (!validate_utf8(text).valid()) throw std::invalid_argument("Ribbon application text must be valid UTF-8");
    if (application_text_ == text) return;
    application_text_ = std::move(text);
    ++content_generation_;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void Ribbon::set_application_menu(std::vector<MenuItemSpec> items) {
    require_mutable();
    std::unordered_set<std::string> identities{};
    std::size_t count{};
    menu_detail::validate_specs(items, identities, 1U, count);
    validate_two_pane(items, recent_items_.size());
    close_menus();
    application_menu_ = std::move(items);
    invalidate(Dirty::semantics);
}

void Ribbon::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) throw std::invalid_argument("Ribbon font specification is invalid");
    set_font_from_code(true);
    if (font_ == font) return;
    font_ = font;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

const RibbonGroupSpec* Ribbon::selected_group(std::size_t group) const noexcept {
    if (!selected_tab_ || group >= tabs_[*selected_tab_].groups.size()) return nullptr;
    return &tabs_[*selected_tab_].groups[group];
}

const RibbonCommandSpec* Ribbon::selected_command(CommandAddress address) const noexcept {
    const RibbonGroupSpec* const group = selected_group(address.group);
    if (group == nullptr || address.command >= (*group).commands.size()) return nullptr;
    return &(*group).commands[address.command];
}

std::optional<Ribbon::CommandAddress> Ribbon::find_command(std::string_view command_id) const {
    if (!selected_tab_) return std::nullopt;
    const std::vector<RibbonGroupSpec>& groups = tabs_[*selected_tab_].groups;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        for (std::size_t command = 0; command < groups[group].commands.size(); ++command) {
            if (groups[group].commands[command].stable_id == command_id) return CommandAddress{group, command};
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> Ribbon::find_group(std::string_view group_id) const {
    if (!selected_tab_) return std::nullopt;
    const std::vector<RibbonGroupSpec>& groups = tabs_[*selected_tab_].groups;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (groups[group].stable_id == group_id) return group;
    }
    return std::nullopt;
}

// The command's text as authored, mnemonic marker included: its own, or its
// Command's when it has none.
const std::string& Ribbon::authored_text(const RibbonCommandSpec& command) const noexcept {
    if (command.text.empty() && command.command) return (*command.command).state().text;
    return command.text;
}

std::string Ribbon::command_text(const RibbonCommandSpec& command) const {
    return parse_mnemonic_text(authored_text(command)).display_text;
}

bool Ribbon::command_enabled(const RibbonCommandSpec& command) const {
    const bool own = !command.command || (*command.command).state().enabled;
    return own && effectively_enabled();
}

bool Ribbon::command_visible(const RibbonCommandSpec& command) const {
    const bool result = !command.command || (*command.command).state().visible;
    return result;
}

bool Ribbon::command_checked(const RibbonCommandSpec& command) const {
    const bool result = command.kind == RibbonCommandKind::toggle && command.command &&
                        (*command.command).state().checked;
    return result;
}

void Ribbon::CommandWatch::operator()(const CommandState&) const {
    if (ribbon != nullptr && (*ribbon).is_alive()) (*ribbon).command_state_changed(tab, group, command);
}

// A Command's state changed. Text and visibility change the layout (and the
// measured ladder); enabled and checked change only the command's own pixels,
// so only its bounds are repainted.
void Ribbon::command_state_changed(std::size_t tab, std::size_t group, std::size_t command) {
    if (tab >= tabs_.size() || group >= tabs_[tab].groups.size() || command >= tabs_[tab].groups[group].commands.size()) {
        return;
    }
    const RibbonCommandSpec& spec = tabs_[tab].groups[group].commands[command];
    CommandRecord& record = records_[tab][group][command];
    const gui_forms::CommandState& state = (*spec.command).state();
    if (state.text != record.text || state.visible != record.visible) {
        record.text = state.text;
        record.visible = state.visible;
        ++content_generation_;
        invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
        if (group_popup_ && popup_group_) (*group_popup_).invalidate(Dirty::arrange | Dirty::paint);
        return;
    }
    invalidate(Dirty::semantics);
    if (selected_tab_ != tab || minimized_ || plane_hidden_) return;
    if (popup_group_ == group && group_popup_) {
        (*group_popup_).invalidate(Dirty::paint);
        return;
    }
    const Layout placed = layout();
    const Rect bounds = Rect::intersection(placed.commands[group][command], placed.groups_area);
    if (!bounds.empty()) invalidate(bounds);
}

// -- layout --------------------------------------------------------------------

double Ribbon::tab_row_height() const noexcept {
    const double result = tab_row_extent * effective_text_scale();
    return result;
}

double Ribbon::plane_height() const noexcept {
    const double rows = row_extent * static_cast<double>(rows_per_column);
    const double result = (plane_top_padding + rows + caption_extent + plane_bottom_padding) * effective_text_scale();
    return result;
}

// The document-area rule (owner decisions 34 to 36). `available.height`
// holds the ribbon and the document area under it; the area is what would
// be left with the plane shown. The plane hides when that area falls below
// the declared minimum (none: below zero) and shows again only once it is
// the minimum plus hide_hysteresis or more. Width never hides it (decision
// 35): a narrow ribbon scrolls.
Size Ribbon::measure(Size available) {
    const double area = available.height - tab_row_height() - plane_height();
    bool hidden = plane_hidden_;
    if (!hidden && area < document_minimum_height_) {
        hidden = true;
    } else if (hidden && area >= document_minimum_height_ + hide_hysteresis) {
        hidden = false;
    }
    if (hidden != plane_hidden_) {
        plane_hidden_ = hidden;
        if (hidden) {
            close_menus();
            close_group_popup();
            if (stop_ && (*stop_).kind != StopKind::tab && (*stop_).kind != StopKind::application) {
                stop_ = Stop{StopKind::tab, selected_tab_.value_or(0U), {}};
            }
        }
        hot_ = Hit{};
        invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    }
    const double height = tab_row_height() + (minimized_ || plane_hidden_ ? 0.0 : plane_height());
    const Size result{available.width, std::min(available.height, height)};
    return result;
}

void Ribbon::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    // The bounds as arranged (final_bounds within the minimum and maximum
    // sizes): the same client paint, hit testing and the published parts
    // lay out in.
    const Rect client = client_rectangle();
    // Scrolling starts below the narrowest entry and ends only
    // scroll_hysteresis above it (owner decision 45).
    bool latched = false;
    if (selected_tab_ && !minimized_ && !plane_hidden_ && !tabs_[*selected_tab_].groups.empty()) {
        const double narrowest = tab_scaling(*selected_tab_).ladder.back().width;
        latched = scroll_latched_ ? client.width < narrowest + scroll_hysteresis : client.width < narrowest;
    }
    scroll_latched_ = latched;
    // Backstage follows the window's size.
    if (backstage_open_) place_backstage();
    Layout placed = layout_for(client);
    if (!placed.scrolling) first_group_ = 0U;
    // A popup whose group grew back closes; the keyboard's command in it
    // keeps its place in the plane (RIBBON_SCALING_STUDY.md §5.8).
    if (popup_group_ && (placed.plane.empty() || !group_collapsed(placed, *popup_group_))) {
        const std::optional<Stop> carried = popup_focused_ ? popup_stop_ : std::nullopt;
        close_group_popup();
        if (carried) stop_ = carried;
        placed = layout_for(client);
    }
    // A stop that no longer exists (its group collapsed or grew back) moves
    // to the group's button or the group's first stop.
    if (stop_) {
        const std::vector<Stop> all = stops_for(placed);
        const Stop resolved = resolve_stop(all, *stop_);
        if (!(resolved == *stop_)) stop_ = resolved;
    }
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        for (std::size_t group = 0; group < tabs_[tab].groups.size(); ++group) {
            for (std::size_t command = 0; command < tabs_[tab].groups[group].commands.size(); ++command) {
                const Control::Ptr& control = records_[tab][group][command].hosted;
                if (!control || !is_current_layout_child(control)) continue;
                Rect bounds{};
                if (selected_tab_ == tab) bounds = Rect::intersection(placed.commands[group][command], placed.groups_area);
                set_child_layout_collapsed(control, bounds.empty());
                if (bounds.empty()) continue;
                set_child_layout(control, bounds);
                const RibbonCommandSpec& spec = tabs_[tab].groups[group].commands[command];
                if (spec.kind != RibbonCommandKind::gallery) continue;
                RibbonGallery& gallery = *spec.gallery;
                const std::size_t columns =
                    std::clamp(placed.gallery_columns[group], gallery.minimum_columns(), gallery.maximum_columns());
                gallery.set_columns(columns);
            }
        }
    }
}

// -- parts -------------------------------------------------------------------

Ribbon::TabParts Ribbon::tab_parts(const Layout& placed, std::size_t tab) const {
    const Rect bounds = placed.tabs[tab];
    const double padding = tab_padding * effective_text_scale();
    TabParts parts;
    parts.tab = PartGeometry::from_bounds(bounds);
    // A contextual tab's face carries its set's tint (owner decision 25).
    if (parts.tab.present) parts.tab.accent = tab_accent(tab);
    parts.tab_label = PartGeometry::from_bounds(
        {bounds.x + padding, bounds.y, std::max(0.0, bounds.width - padding * 2.0), bounds.height});
    parts.tab_focus = PartGeometry::from_bounds(bounds);
    return parts;
}

// A group's caption runs along its bottom, the launcher in its corner, and
// the divider down its right edge (none in the popup). A collapsed group is
// one button the group's height: its icon, its caption beneath and a chevron
// below (owner decision 27).
Ribbon::GroupParts Ribbon::group_parts(const Layout& placed, std::size_t group) const {
    const double scale = effective_text_scale();
    const RibbonGroupSpec& spec = *selected_group(group);
    const Rect bounds = placed.groups[group];
    const Rect visible = Rect::intersection(bounds, placed.groups_area);
    const double padding = group_padding * scale;
    Rect divider{};
    if (!placed.popup) {
        divider = {bounds.x + bounds.width - divider_width, bounds.y + 3.0 * scale, divider_width,
                   std::max(0.0, bounds.height - 6.0 * scale)};
    }
    GroupParts parts;
    parts.group_region = PartGeometry::from_bounds(visible);
    parts.group_divider = PartGeometry::from_bounds(Rect::intersection(divider, visible));
    if (group_collapsed(placed, group)) {
        const double top = plane_top_padding * scale;
        const double bottom = plane_bottom_padding * scale;
        const Rect button{bounds.x + padding, bounds.y + top,
                          std::max(0.0, bounds.width - padding * 2.0 - divider_width),
                          std::max(0.0, bounds.height - top - bottom)};
        const double side = large_icon * scale;
        const double icon_top = button.y + icon_gap * scale;
        Rect icon{};
        if (group_image(spec).value != 0U) icon = {std::floor(button.x + (button.width - side) * 0.5), icon_top, side, side};
        const Rect label{button.x + 2.0 * scale, icon_top + side + icon_gap * scale,
                         std::max(0.0, button.width - 4.0 * scale), label_extent * scale};
        const Rect glyph = centred_square(
            {button.x, label.y + label.height, button.width, arrow_glyph * scale + 2.0 * scale}, arrow_glyph * scale);
        const AnatomyStateSet states = group_button_states(group);
        parts.group_button = PartGeometry::from_bounds(Rect::intersection(button, visible));
        parts.group_button_icon = PartGeometry::from_bounds(Rect::intersection(icon, visible));
        parts.group_button_label = PartGeometry::from_bounds(Rect::intersection(label, visible));
        parts.group_button_glyph = PartGeometry::from_bounds(Rect::intersection(glyph, visible));
        parts.group_button_focus = parts.group_button;
        parts.group_button.states = states;
        parts.group_button_icon.states = states;
        parts.group_button_label.states = states;
        parts.group_button_glyph.states = states;
        parts.group_button_focus.states = states;
        return parts;
    }
    const double caption_height = caption_extent * scale;
    const double caption_bottom = bounds.y + bounds.height - plane_bottom_padding * scale;
    const Rect caption_row{bounds.x + padding, caption_bottom - caption_height,
                           std::max(0.0, bounds.width - padding * 2.0 - divider_width), caption_height};
    Rect caption = caption_row;
    Rect launcher{};
    if (spec.dialog_launcher) {
        const double side = launcher_size * scale;
        launcher = {caption_row.x + caption_row.width - side, caption_row.y + std::floor((caption_height - side) * 0.5),
                    side, side};
        caption.width = std::max(0.0, caption.width - side - launcher_gap * scale);
    }
    const double inset = launcher_glyph_inset * scale;
    const Rect glyph{launcher.x + inset, launcher.y + inset, std::max(0.0, launcher.width - inset * 2.0),
                     std::max(0.0, launcher.height - inset * 2.0)};
    parts.group_caption = PartGeometry::from_bounds(Rect::intersection(caption, visible));
    parts.group_launcher = PartGeometry::from_bounds(Rect::intersection(launcher, visible));
    parts.group_launcher_glyph = PartGeometry::from_bounds(Rect::intersection(glyph, visible));
    parts.group_launcher_focus = parts.group_launcher;
    if (parts.group_launcher.present) {
        AnatomyStateSet states;
        if (hot_.kind == Hit::Kind::launcher && hot_.index == group) states.insert(AnatomyState::hot);
        if (pressed_launcher_ == group) states.insert(AnatomyState::pressed);
        if (stop_focus_visible(Stop{StopKind::launcher, group, {}})) states.insert(AnatomyState::focus_visible);
        parts.group_launcher.states = states;
        parts.group_launcher_glyph.states = states;
        parts.group_launcher_focus.states = states;
    }
    return parts;
}

// A command's parts within its bounds. A large command shows its icon over
// its label; a large split's face is the icon, its arrow the label and the
// glyph beneath. A small command shows its icon, label and, for a split or
// drop-down, an arrow zone at its right end. A hosted command or gallery is
// its control.
Ribbon::CommandParts Ribbon::command_parts(const Layout& placed, CommandAddress address) const {
    const double scale = effective_text_scale();
    const RibbonCommandSpec& spec = *selected_command(address);
    const Rect bounds = placed.commands[address.group][address.command];
    CommandParts parts;
    if (bounds.empty()) return parts;
    const Rect clip = Rect::intersection(placed.groups[address.group], placed.groups_area);
    const Rect visible = Rect::intersection(bounds, clip);
    if (hosted(spec.kind)) {
        const PartGeometry control = PartGeometry::from_bounds(visible);
        if (spec.kind == RibbonCommandKind::combo_box) parts.command_combo_box = control;
        if (spec.kind == RibbonCommandKind::numeric_up_down) parts.command_numeric_up_down = control;
        if (spec.kind == RibbonCommandKind::check_box) parts.command_check_box = control;
        if (spec.kind == RibbonCommandKind::text_box) parts.command_text_box = control;
        if (spec.kind == RibbonCommandKind::gallery) parts.command_gallery = control;
        if (spec.kind == RibbonCommandKind::font_control) parts.command_font_control = control;
        if (spec.kind == RibbonCommandKind::color_picker) parts.command_color_picker = control;
        return parts;
    }
    const RibbonGroupLayout& group_layout =
        layouts_[*selected_tab_][address.group][placed.group_layouts[address.group]];
    const bool large = layout_command_size(group_layout, spec, address.command) == RibbonCommandSize::large;
    Rect face = bounds;
    Rect icon{};
    Rect label{};
    Rect arrow{};
    Rect glyph{};
    if (large) {
        const double side = large_icon * scale;
        const double icon_top = bounds.y + icon_gap * scale;
        if (spec.image.value != 0U) icon = {std::floor(bounds.x + (bounds.width - side) * 0.5), icon_top, side, side};
        label = {bounds.x + 2.0 * scale, icon_top + side + icon_gap * scale, std::max(0.0, bounds.width - 4.0 * scale),
                 label_extent * scale};
        if (has_menu(spec.kind)) {
            glyph = centred_square({bounds.x, label.y + label.height, bounds.width, arrow_glyph * scale + 2.0 * scale},
                                   arrow_glyph * scale);
        }
        if (spec.kind == RibbonCommandKind::split) {
            const double split_line = icon_top + side + icon_gap * scale * 0.5;
            face = {bounds.x, bounds.y, bounds.width, split_line - bounds.y};
            arrow = {bounds.x, split_line, bounds.width, bounds.y + bounds.height - split_line};
        }
    } else {
        double x = bounds.x + icon_gap * scale;
        const double middle = bounds.y + bounds.height * 0.5;
        const double arrow_width = has_menu(spec.kind) ? arrow_zone * scale : 0.0;
        if (spec.image.value != 0U) {
            const double side = small_icon * scale;
            icon = {x, std::floor(middle - side * 0.5), side, side};
            x += side + icon_gap * scale;
        } else {
            x += icon_gap * scale;
        }
        if (spec.show_label) {
            label = {x, bounds.y, std::max(0.0, bounds.x + bounds.width - arrow_width - x), bounds.height};
        }
        if (has_menu(spec.kind)) {
            const Rect zone{bounds.x + bounds.width - arrow_width, bounds.y, arrow_width, bounds.height};
            glyph = centred_square(zone, arrow_glyph * scale);
            if (spec.kind == RibbonCommandKind::split) {
                face = {bounds.x, bounds.y, std::max(0.0, bounds.width - arrow_width), bounds.height};
                arrow = zone;
            }
        }
    }
    parts.command_face = PartGeometry::from_bounds(Rect::intersection(face, visible));
    parts.command_icon = PartGeometry::from_bounds(Rect::intersection(icon, visible));
    parts.command_label = PartGeometry::from_bounds(Rect::intersection(label, visible));
    parts.command_arrow = PartGeometry::from_bounds(Rect::intersection(arrow, visible));
    parts.command_arrow_glyph = PartGeometry::from_bounds(Rect::intersection(glyph, visible));
    parts.command_focus = PartGeometry::from_bounds(visible);
    if (parts.command_arrow.present) {
        const AnatomyStateSet states = command_states(address);
        if (states.contains(AnatomyState::hot)) parts.command_arrow.states.insert(AnatomyState::hot);
        if (pressed_command_ == address && pressed_zone_ == CommandZone::arrow) {
            parts.command_arrow.states.insert(AnatomyState::pressed);
        }
    }
    return parts;
}

// The ribbon's own parts: the tab row, the File tab, the plane and, while it
// scrolls, its scroll buttons.
Ribbon::Parts Ribbon::control_parts(const Layout& placed) const {
    const double scale = effective_text_scale();
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.tab_rail = PartGeometry::from_bounds(placed.tab_rail);
    record.application_button = PartGeometry::from_bounds(placed.application);
    const double padding = tab_padding * scale;
    record.application_label = PartGeometry::from_bounds(
        {placed.application.x + padding, placed.application.y, std::max(0.0, placed.application.width - padding * 2.0),
         placed.application.height});
    AnatomyStateSet application;
    if (hot_.kind == Hit::Kind::application) application.insert(AnatomyState::hot);
    if (application_pressed_) application.insert(AnatomyState::pressed);
    if (file_area_open()) application.insert(AnatomyState::open);
    if (stop_focus_visible(Stop{StopKind::application, 0U, {}})) application.insert(AnatomyState::focus_visible);
    record.application_button.states = application;
    record.application_label.states = application;
    record.command_plane = PartGeometry::from_bounds(placed.plane);
    if (record.command_plane.present && selected_tab_) record.command_plane.accent = tab_accent(*selected_tab_);
    const double glyph = arrow_glyph * scale;
    record.scroll_left = PartGeometry::from_bounds(placed.scroll_left);
    record.scroll_left_glyph = PartGeometry::from_bounds(centred_square(placed.scroll_left, glyph));
    record.scroll_right = PartGeometry::from_bounds(placed.scroll_right);
    record.scroll_right_glyph = PartGeometry::from_bounds(centred_square(placed.scroll_right, glyph));
    AnatomyStateSet left;
    AnatomyStateSet right;
    if (hot_.kind == Hit::Kind::scroll_left) left.insert(AnatomyState::hot);
    if (pressed_scroll_ == Hit::Kind::scroll_left) left.insert(AnatomyState::pressed);
    if (hot_.kind == Hit::Kind::scroll_right) right.insert(AnatomyState::hot);
    if (pressed_scroll_ == Hit::Kind::scroll_right) right.insert(AnatomyState::pressed);
    record.scroll_left.states = left;
    record.scroll_left_glyph.states = left;
    record.scroll_right.states = right;
    record.scroll_right_glyph.states = right;
    record.focus = PartGeometry::from_bounds(client_rectangle());
    return record;
}

// Whether `stop` shows the focus cue: the ribbon's own stop while it has
// focus, or the popup's stop while the popup has focus.
bool Ribbon::stop_focus_visible(const Stop& stop) const {
    const Window* const host = attached_window();
    if (host == nullptr || !(*host).focus_cue_visible()) return false;
    const bool own = focused_ && stop_ && *stop_ == stop;
    const bool popup = popup_focused_ && popup_stop_ && *popup_stop_ == stop;
    return own || popup;
}

AnatomyStateSet Ribbon::tab_states(std::size_t tab) const {
    AnatomyStateSet states;
    if (hot_.kind == Hit::Kind::tab && hot_.index == tab) states.insert(AnatomyState::hot);
    if (selected_tab_ == tab) states.insert(AnatomyState::selected);
    if (stop_focus_visible(Stop{StopKind::tab, tab, {}})) states.insert(AnatomyState::focus_visible);
    return states;
}

AnatomyStateSet Ribbon::command_states(CommandAddress address) const {
    AnatomyStateSet states;
    const RibbonCommandSpec* const spec = selected_command(address);
    if (spec == nullptr) return states;
    const bool enabled = command_enabled(*spec);
    if (!enabled) states.insert(AnatomyState::disabled);
    if (enabled && !hosted((*spec).kind) && hot_.kind == Hit::Kind::command && hot_.command == address) {
        states.insert(AnatomyState::hot);
    }
    if (pressed_command_ == address) states.insert(AnatomyState::pressed);
    if (command_checked(*spec)) states.insert(AnatomyState::checked);
    if (open_command_ == address) states.insert(AnatomyState::open);
    if (stop_focus_visible(Stop{StopKind::command, 0U, address})) states.insert(AnatomyState::focus_visible);
    return states;
}

AnatomyStateSet Ribbon::group_button_states(std::size_t group) const {
    AnatomyStateSet states;
    if (hot_.kind == Hit::Kind::group_button && hot_.index == group) states.insert(AnatomyState::hot);
    if (pressed_group_ == group) states.insert(AnatomyState::pressed);
    if (popup_group_ == group) states.insert(AnatomyState::open);
    if (stop_focus_visible(Stop{StopKind::group_button, group, {}})) states.insert(AnatomyState::focus_visible);
    return states;
}

// -- paint -------------------------------------------------------------------

void Ribbon::on_paint(Painter& painter, Rect) {
    const Layout placed = layout();
    const FontSpec font = text_font(font_);
    const Parts own = control_parts(placed);
    PartPaint part;
    part.font = font;
    part.family = ControlVisualRole::panel;
    part.role = PartRole::face;
    part.bounds = placed.tab_rail;
    paint_part(painter, *this, part);
    // The File tab: a solid accent face, as the design language's File tab.
    const std::string application_label = parse_mnemonic_text(application_text_).display_text;
    part.family = ControlVisualRole::accent_button;
    part.states = own.application_button.states;
    part.bounds = placed.application;
    paint_part(painter, *this, part);
    part.role = PartRole::label;
    part.text = application_label;
    part.alignment = PartTextAlignment::center;
    paint_part(painter, *this, part);
    part.text = {};
    part.role = PartRole::focus;
    paint_part(painter, *this, part);
    // Tabs: flat at rest, a plate when hot or selected (the menu-item
    // family) until T4 draws the tab stack.
    part.family = ControlVisualRole::menu_item;
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        if (!shown(tab)) continue;
        const TabParts parts = tab_parts(placed, tab);
        part.states = tab_states(tab);
        part.role = PartRole::face;
        part.bounds = parts.tab.bounds;
        part.accent = parts.tab.accent;
        paint_part(painter, *this, part);
        part.accent.reset();
        const std::string label = parse_mnemonic_text(tabs_[tab].text).display_text;
        part.role = PartRole::label;
        part.bounds = parts.tab_label.bounds;
        part.text = label;
        paint_part(painter, *this, part);
        part.text = {};
        part.role = PartRole::focus;
        part.bounds = parts.tab_focus.bounds;
        paint_part(painter, *this, part);
    }
    part.alignment = PartTextAlignment::start;
    if (placed.plane.empty() || !selected_tab_) return;
    part.states = {};
    part.family = ControlVisualRole::panel;
    part.role = PartRole::face;
    part.bounds = placed.plane;
    part.accent = own.command_plane.accent;
    paint_part(painter, *this, part);
    part.accent.reset();
    painter.save();
    painter.clip_rect(placed.groups_area);
    paint_groups(painter, placed);
    painter.restore();
    // The scroll buttons, over the plane's ends (menu-item family, flat at
    // rest, until T4).
    part.family = ControlVisualRole::menu_item;
    if (own.scroll_left.present) {
        part.role = PartRole::actuator;
        part.states = own.scroll_left.states;
        part.bounds = own.scroll_left.bounds;
        paint_part(painter, *this, part);
        part.role = PartRole::glyph;
        part.bounds = own.scroll_left_glyph.bounds;
        part.mark = PartMark::arrow_left;
        paint_part(painter, *this, part);
    }
    if (own.scroll_right.present) {
        part.role = PartRole::actuator;
        part.states = own.scroll_right.states;
        part.bounds = own.scroll_right.bounds;
        part.mark = PartMark::none;
        paint_part(painter, *this, part);
        part.role = PartRole::glyph;
        part.bounds = own.scroll_right_glyph.bounds;
        part.mark = PartMark::arrow_right;
        paint_part(painter, *this, part);
    }
}

// Paints the shown groups of `placed` (the plane or the popup), clipped by
// the caller.
void Ribbon::paint_groups(Painter& painter, const Layout& placed) const {
    const FontSpec font = text_font(font_);
    PartPaint part;
    part.font = font;
    const std::vector<RibbonGroupSpec>& groups = tabs_[*selected_tab_].groups;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (group >= placed.groups.size() || Rect::intersection(placed.groups[group], placed.groups_area).empty()) {
            continue;
        }
        const GroupParts parts = group_parts(placed, group);
        part.family = ControlVisualRole::panel;
        part.states = {};
        part.role = PartRole::divider;
        part.bounds = parts.group_divider.bounds;
        paint_part(painter, *this, part);
        if (group_collapsed(placed, group)) {
            part.family = ControlVisualRole::menu_item;
            part.states = parts.group_button.states;
            part.role = PartRole::face;
            part.bounds = parts.group_button.bounds;
            // Keyed by the group (T2 GIVEN H3-1), so that the engine's
            // built-in rules raise its dithered bulge while it is hot.
            part.key = PartKey{"group_button", groups[group].stable_id, {}};
            paint_part(painter, *this, part);
            part.key.reset();
            if (parts.group_button_icon.present) {
                part.role = PartRole::icon;
                part.bounds = parts.group_button_icon.bounds;
                part.image = group_image(groups[group]);
                paint_part(painter, *this, part);
                part.image = {};
            }
            part.role = PartRole::label;
            part.bounds = parts.group_button_label.bounds;
            part.alignment = PartTextAlignment::center;
            part.text = groups[group].caption;
            paint_part(painter, *this, part);
            part.text = {};
            part.alignment = PartTextAlignment::start;
            part.role = PartRole::glyph;
            part.bounds = parts.group_button_glyph.bounds;
            part.mark = PartMark::chevron_down;
            paint_part(painter, *this, part);
            part.mark = PartMark::none;
            part.role = PartRole::focus;
            part.bounds = parts.group_button_focus.bounds;
            paint_part(painter, *this, part);
            continue;
        }
        const RibbonGroupLayout& group_layout = layouts_[*selected_tab_][group][placed.group_layouts[group]];
        part.role = PartRole::caption;
        part.bounds = parts.group_caption.bounds;
        part.alignment = PartTextAlignment::center;
        part.text = groups[group].caption;
        paint_part(painter, *this, part);
        part.text = {};
        part.alignment = PartTextAlignment::start;
        if (parts.group_launcher.present) {
            part.family = ControlVisualRole::menu_item;
            part.states = parts.group_launcher.states;
            part.role = PartRole::actuator;
            part.bounds = parts.group_launcher.bounds;
            paint_part(painter, *this, part);
            part.role = PartRole::glyph;
            part.bounds = parts.group_launcher_glyph.bounds;
            part.mark = PartMark::dialog_launcher;
            paint_part(painter, *this, part);
            part.mark = PartMark::none;
            part.role = PartRole::focus;
            part.bounds = parts.group_launcher_focus.bounds;
            paint_part(painter, *this, part);
        }
        for (std::size_t command = 0; command < groups[group].commands.size(); ++command) {
            const RibbonCommandSpec& spec = groups[group].commands[command];
            const CommandAddress address{group, command};
            const CommandParts command_part = command_parts(placed, address);
            if (hosted(spec.kind) || !command_part.command_face.present) continue;
            const AnatomyStateSet states = command_states(address);
            // A split's face and arrow press apart, as ToolStrip's do.
            AnatomyStateSet face_states = states;
            if (spec.kind == RibbonCommandKind::split && pressed_zone_ != CommandZone::main) {
                face_states.erase(AnatomyState::pressed);
            }
            part.family = ControlVisualRole::menu_item;
            part.role = PartRole::face;
            part.states = face_states;
            part.bounds = command_part.command_face.bounds;
            // Keyed by the command (T2 GIVEN H3-1): the engine's built-in
            // rules draw its dithered bulge, flat at rest and rising while
            // it is hot (GIVEN D3-1, G4-1).
            part.key = PartKey{"command_face", spec.stable_id, {}};
            paint_part(painter, *this, part);
            part.key.reset();
            if (command_part.command_arrow.present) {
                part.role = PartRole::actuator;
                part.states = command_part.command_arrow.states;
                part.bounds = command_part.command_arrow.bounds;
                paint_part(painter, *this, part);
            }
            part.states = states;
            if (command_part.command_icon.present) {
                part.role = PartRole::icon;
                part.bounds = command_part.command_icon.bounds;
                part.image = spec.image;
                paint_part(painter, *this, part);
                part.image = {};
            }
            if (command_part.command_label.present) {
                const std::string label = command_text(spec);
                const bool large = layout_command_size(group_layout, spec, command) == RibbonCommandSize::large;
                part.role = PartRole::label;
                part.bounds = command_part.command_label.bounds;
                part.text = label;
                part.alignment = large ? PartTextAlignment::center : PartTextAlignment::start;
                paint_part(painter, *this, part);
                part.text = {};
                part.alignment = PartTextAlignment::start;
            }
            if (command_part.command_arrow_glyph.present) {
                part.role = PartRole::glyph;
                part.bounds = command_part.command_arrow_glyph.bounds;
                part.mark = PartMark::arrow_down;
                paint_part(painter, *this, part);
                part.mark = PartMark::none;
            }
            part.role = PartRole::focus;
            part.bounds = command_part.command_focus.bounds;
            paint_part(painter, *this, part);
        }
    }
}

void Ribbon::popup_paint(Painter& painter, Rect panel) const {
    if (!selected_tab_ || !popup_group_) return;
    const Layout placed = popup_layout(panel);
    painter.save();
    painter.clip_rect(panel);
    paint_groups(painter, placed);
    painter.restore();
}

// -- input -------------------------------------------------------------------

Ribbon::Hit Ribbon::hit_test(const Layout& placed, Point local) const {
    Hit hit;
    if (placed.application.contains(local)) {
        hit.kind = Hit::Kind::application;
        return hit;
    }
    for (std::size_t tab = 0; tab < placed.tabs.size(); ++tab) {
        if (!placed.tabs[tab].contains(local)) continue;
        hit.kind = Hit::Kind::tab;
        hit.index = tab;
        return hit;
    }
    if (placed.plane.empty() || !placed.plane.contains(local) || !selected_tab_) return hit;
    if (placed.scroll_left.contains(local)) {
        hit.kind = Hit::Kind::scroll_left;
        return hit;
    }
    if (placed.scroll_right.contains(local)) {
        hit.kind = Hit::Kind::scroll_right;
        return hit;
    }
    if (!placed.groups_area.contains(local)) return hit;
    const std::vector<RibbonGroupSpec>& groups = tabs_[*selected_tab_].groups;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (!placed.groups[group].contains(local)) continue;
        const GroupParts parts = group_parts(placed, group);
        if (group_collapsed(placed, group)) {
            if (parts.group_button.present && parts.group_button.bounds.contains(local)) {
                hit.kind = Hit::Kind::group_button;
                hit.index = group;
            }
            return hit;
        }
        if (parts.group_launcher.present && parts.group_launcher.bounds.contains(local)) {
            hit.kind = Hit::Kind::launcher;
            hit.index = group;
            return hit;
        }
        for (std::size_t command = 0; command < groups[group].commands.size(); ++command) {
            if (!placed.commands[group][command].contains(local)) continue;
            const CommandAddress address{group, command};
            const CommandParts command_part = command_parts(placed, address);
            hit.kind = Hit::Kind::command;
            hit.command = address;
            hit.zone = command_part.command_arrow.present && command_part.command_arrow.bounds.contains(local)
                           ? CommandZone::arrow
                           : CommandZone::main;
            return hit;
        }
    }
    return hit;
}

void Ribbon::set_hot(const Hit& hit) {
    if (hot_.kind == hit.kind && hot_.index == hit.index && hot_.command == hit.command && hot_.zone == hit.zone) {
        return;
    }
    hot_ = hit;
    invalidate_views(Dirty::paint);
}

// Repaints (or otherwise invalidates) the ribbon and, while it is open, the
// group popup that shares its interaction state.
void Ribbon::invalidate_views(Dirty dirty) {
    invalidate(dirty);
    if (group_popup_ && popup_group_ && (*group_popup_).is_alive()) (*group_popup_).invalidate(dirty);
}

void Ribbon::click_tab(std::size_t tab) {
    if (tab >= tabs_.size()) return;
    // The selected tab minimizes the command plane or shows it again
    // (owner decision 19); another tab is selected and shows the plane.
    if (selected_tab_ == tab) {
        set_minimized(!minimized_);
        return;
    }
    select_tab(tab);
    if (minimized_) set_minimized(false);
}

// The one path every activation of a command takes: pointer, keyboard,
// mnemonic and accessibility, in the plane or in a group popup. A command
// run from a group popup closes it, as a menu closes.
void Ribbon::click_command(CommandAddress address) {
    const RibbonCommandSpec* const found = selected_command(address);
    if (found == nullptr || hosted((*found).kind) || !command_enabled(*found)) return;
    // A copy: running the command, or a handler of its events, may replace
    // the tabs and so the record `found` points into.
    const RibbonCommandSpec spec = *found;
    if (spec.kind == RibbonCommandKind::drop_down) {
        static_cast<void>(show_drop_down(spec.stable_id));
        return;
    }
    if (spec.kind == RibbonCommandKind::toggle) {
        (*spec.command).set_checked(!(*spec.command).state().checked);
    }
    const Control::Ptr lifetime = shared_from_this();
    if (window() != nullptr) {
        static_cast<void>((*spec.command).execute_routed(*window(), stable_id().value()));
    } else {
        static_cast<void>((*spec.command).execute(stable_id().value()));
    }
    if (!is_alive()) return;
    const RibbonCommandClick click{std::string(stable_id().value()), spec.stable_id};
    command_clicked_.emit(click);
    if (is_alive() && popup_group_ == address.group) close_group_popup();
}

void Ribbon::launch(std::size_t group) {
    const RibbonGroupSpec* const found = selected_group(group);
    if (found == nullptr || !(*found).dialog_launcher || !effectively_enabled()) return;
    const Control::Ptr lifetime = shared_from_this();
    const RibbonDialogLaunch launched{std::string(stable_id().value()), (*found).stable_id};
    dialog_launcher_clicked_.emit(launched);
    if (is_alive() && popup_group_ == group) close_group_popup();
}

// A primary press on `hit`, in the plane or the popup. Menus, the group
// popup and scrolling act on the press; commands and launchers on the
// release over the same part.
void Ribbon::press(const Hit& hit, PointerEvent& event) {
    event.handled = true;
    if (hit.kind == Hit::Kind::application) {
        if (application_open_) {
            close_menus();
        } else {
            static_cast<void>(show_application_menu(MenuOpenMode::pointer));
        }
    } else if (hit.kind == Hit::Kind::tab) {
        click_tab(hit.index);
    } else if (hit.kind == Hit::Kind::launcher) {
        pressed_launcher_ = hit.index;
        invalidate_views(Dirty::paint);
    } else if (hit.kind == Hit::Kind::group_button) {
        pressed_group_ = hit.index;
        invalidate_views(Dirty::paint);
        const RibbonGroupSpec* const group = selected_group(hit.index);
        if (group != nullptr) static_cast<void>(show_group_popup((*group).stable_id, MenuOpenMode::pointer));
    } else if (hit.kind == Hit::Kind::scroll_left || hit.kind == Hit::Kind::scroll_right) {
        pressed_scroll_ = hit.kind;
        scroll_groups(hit.kind == Hit::Kind::scroll_right);
        invalidate(Dirty::paint);
    } else if (hit.kind == Hit::Kind::command) {
        const RibbonCommandSpec* const spec = selected_command(hit.command);
        if (spec == nullptr || hosted((*spec).kind) || !command_enabled(*spec)) return;
        const bool opens = (*spec).kind == RibbonCommandKind::drop_down ||
                           ((*spec).kind == RibbonCommandKind::split && hit.zone == CommandZone::arrow);
        if (opens) {
            if (open_command_ == hit.command) {
                close_menus();
            } else {
                static_cast<void>(show_drop_down((*spec).stable_id, MenuOpenMode::pointer));
            }
            return;
        }
        pressed_command_ = hit.command;
        pressed_zone_ = hit.zone;
        invalidate_views(Dirty::paint);
    }
}

// A primary release on `hit`: a click is a press and release on the same
// part.
void Ribbon::release(const Hit& hit, PointerEvent& event) {
    const std::optional<CommandAddress> pressed = pressed_command_;
    const CommandZone zone = pressed_zone_;
    const std::optional<std::size_t> launcher = pressed_launcher_;
    const bool group = pressed_group_.has_value();
    const bool scroll = pressed_scroll_ != Hit::Kind::none;
    if (!pressed && !launcher && !group && !scroll) return;
    pressed_command_.reset();
    pressed_zone_ = CommandZone::none;
    pressed_launcher_.reset();
    pressed_group_.reset();
    pressed_scroll_ = Hit::Kind::none;
    invalidate_views(Dirty::paint);
    event.handled = true;
    if (pressed && hit.kind == Hit::Kind::command && hit.command == *pressed && hit.zone == zone) {
        click_command(*pressed);
    } else if (launcher && hit.kind == Hit::Kind::launcher && hit.index == *launcher) {
        launch(*launcher);
    }
}

void Ribbon::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    if (event.action == PointerAction::leave) {
        set_hot(Hit{});
        return;
    }
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const Layout placed = layout();
    const Hit hit = hit_test(placed, local);
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        set_hot(hit);
        return;
    }
    if (event.button != PointerButton::primary) return;
    const Control::Ptr lifetime = shared_from_this();
    if (event.action == PointerAction::down) {
        if (hit.kind == Hit::Kind::none) return;
        press(hit, event);
        return;
    }
    if (event.action != PointerAction::up) return;
    release(hit, event);
}

// The popup's layer lets presses on the ribbon (its anchor) through, but its
// focus scope would refuse the ribbon focus: a press on the ribbon closes the
// popup in the preview phase, before the window focuses the target. A press
// on the popup's own group button, or on nothing, only closes it.
//
// The two-pane menu and Backstage likewise: a press on the File tab only
// closes them, and a press on a tab while Backstage shows closes it and
// selects that tab (without minimizing).
void Ribbon::on_pointer_preview(PointerEvent& event) {
    Control::on_pointer_preview(event);
    if (event.action != PointerAction::down || event.button != PointerButton::primary) return;
    if (!popup_group_ && !application_popup_open_ && !backstage_open_) return;
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const Hit hit = hit_test(layout(), local);
    if (popup_group_) {
        const bool same = hit.kind == Hit::Kind::group_button && hit.index == *popup_group_;
        close_group_popup();
        if (same || hit.kind == Hit::Kind::none) event.handled = true;
        return;
    }
    const bool backstage = backstage_open_;
    close_application_popup();
    close_backstage();
    if (hit.kind == Hit::Kind::application || hit.kind == Hit::Kind::none) {
        event.handled = true;
    } else if (backstage && hit.kind == Hit::Kind::tab) {
        event.handled = true;
        select_tab(hit.index);
    }
}

void Ribbon::popup_pointer(PointerEvent& event, Rect panel) {
    if (!eligible_for_input() || !group_popup_ || !popup_group_) return;
    if (event.action == PointerAction::leave) {
        set_hot(Hit{});
        return;
    }
    const Rect absolute = (*group_popup_).absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const Layout placed = popup_layout(panel);
    const Hit hit = hit_test(placed, local);
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        set_hot(hit);
        return;
    }
    if (event.button != PointerButton::primary) return;
    const Control::Ptr lifetime = shared_from_this();
    if (event.action == PointerAction::down) {
        if (hit.kind == Hit::Kind::none) return;
        press(hit, event);
        return;
    }
    if (event.action == PointerAction::up) release(hit, event);
}

// The keyboard stops in Tab order: the File tab, the tabs, then (while the
// plane shows) each group's visible button-family commands and launcher, or
// a collapsed group's button.
std::vector<Ribbon::Stop> Ribbon::stops_for(const Layout& placed) const {
    std::vector<Stop> result;
    result.push_back(Stop{StopKind::application, 0U, {}});
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        if (shown(tab)) result.push_back(Stop{StopKind::tab, tab, {}});
    }
    if (minimized_ || plane_hidden_ || !selected_tab_ || placed.plane.empty()) return result;
    const std::vector<RibbonGroupSpec>& groups = tabs_[*selected_tab_].groups;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (!group_shown(*selected_tab_, group)) continue;
        if (group_collapsed(placed, group)) {
            result.push_back(Stop{StopKind::group_button, group, {}});
            continue;
        }
        for (std::size_t command = 0; command < groups[group].commands.size(); ++command) {
            const RibbonCommandSpec& spec = groups[group].commands[command];
            if (hosted(spec.kind) || !command_visible(spec)) continue;
            result.push_back(Stop{StopKind::command, 0U, CommandAddress{group, command}});
        }
        if (groups[group].dialog_launcher) result.push_back(Stop{StopKind::launcher, group, {}});
    }
    return result;
}

std::vector<Ribbon::Stop> Ribbon::stops() const {
    return stops_for(layout());
}

// The popup's stops: its group's visible button-family commands, then its
// launcher.
std::vector<Ribbon::Stop> Ribbon::popup_stops() const {
    std::vector<Stop> result;
    if (!selected_tab_ || !popup_group_) return result;
    const std::size_t group = *popup_group_;
    const RibbonGroupSpec& spec = tabs_[*selected_tab_].groups[group];
    for (std::size_t command = 0; command < spec.commands.size(); ++command) {
        const RibbonCommandSpec& item = spec.commands[command];
        if (hosted(item.kind) || !command_visible(item)) continue;
        result.push_back(Stop{StopKind::command, 0U, CommandAddress{group, command}});
    }
    if (spec.dialog_launcher) result.push_back(Stop{StopKind::launcher, group, {}});
    return result;
}

// `stop` if it is one of `all`; otherwise the stop that took its place: a
// command or launcher of a collapsed group moves to the group's button, a
// group button whose group grew back to the group's first stop, and anything
// else to the selected tab.
Ribbon::Stop Ribbon::resolve_stop(const std::vector<Stop>& all, const Stop& stop) const {
    for (const Stop& candidate : all) {
        if (candidate == stop) return stop;
    }
    std::optional<std::size_t> group{};
    if (stop.kind == StopKind::command) group = stop.command.group;
    if (stop.kind == StopKind::launcher || stop.kind == StopKind::group_button) group = stop.index;
    for (const Stop& candidate : all) {
        if (!group) break;
        if (candidate.kind == StopKind::group_button && candidate.index == *group) return candidate;
        if (candidate.kind == StopKind::command && candidate.command.group == *group) return candidate;
        if (candidate.kind == StopKind::launcher && candidate.index == *group) return candidate;
    }
    const Stop fallback{StopKind::tab, selected_tab_.value_or(0U), {}};
    return fallback;
}

// Moves the keyboard's stop, scrolling its group into view while the plane
// scrolls (RIBBON_SCALING_STUDY.md §5.6).
void Ribbon::set_stop(std::optional<Stop> stop) {
    if (stop_ == stop) return;
    stop_ = stop;
    if (stop && (*stop).kind != StopKind::application && (*stop).kind != StopKind::tab) {
        const std::size_t group = (*stop).kind == StopKind::command ? (*stop).command.group : (*stop).index;
        const Layout placed = layout();
        if (placed.scrolling && group < placed.groups.size() && placed.groups[group].empty()) {
            first_group_ = group;
            invalidate(Dirty::arrange | Dirty::hit_test);
        } else if (placed.scrolling && group < first_group_) {
            first_group_ = group;
            invalidate(Dirty::arrange | Dirty::hit_test);
        }
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void Ribbon::activate_stop(const Stop& stop) {
    if (stop.kind == StopKind::application) {
        static_cast<void>(show_application_menu());
    } else if (stop.kind == StopKind::tab) {
        click_tab(stop.index);
    } else if (stop.kind == StopKind::launcher) {
        launch(stop.index);
    } else if (stop.kind == StopKind::group_button) {
        const RibbonGroupSpec* const group = selected_group(stop.index);
        if (group != nullptr) static_cast<void>(show_group_popup((*group).stable_id));
    } else {
        click_command(stop.command);
    }
}

void Ribbon::on_key(KeyEvent& event) {
    if (!focused_ || event.action != KeyAction::down || !effectively_enabled()) return;
    const std::vector<Stop> all = stops();
    if (!stop_) stop_ = Stop{StopKind::tab, selected_tab_.value_or(0U), {}};
    const Stop resolved = resolve_stop(all, *stop_);
    std::size_t position = 0U;
    for (std::size_t index = 0; index < all.size(); ++index) {
        if (all[index] == resolved) position = index;
    }
    const Stop current = all[position];
    const bool in_row = current.kind == StopKind::application || current.kind == StopKind::tab;
    std::size_t row_count = 0U;
    for (const Stop& stop : all) {
        if (stop.kind == StopKind::application || stop.kind == StopKind::tab) ++row_count;
    }
    const std::uint32_t key = event.physical_key;
    const bool shift = has_modifier(event.modifiers, Modifier::shift);
    if (key == PhysicalKey::tab) {
        // Tab moves through every stop and leaves the ribbon after the last
        // (or, with Shift, before the first).
        if (!shift && position + 1U < all.size()) {
            set_stop(all[position + 1U]);
        } else if (shift && position > 0U) {
            set_stop(all[position - 1U]);
        } else {
            return;
        }
    } else if (in_row && (key == PhysicalKey::left || key == PhysicalKey::right)) {
        // Along the tab row; selection follows the focused tab.
        const std::size_t next = key == PhysicalKey::right ? std::min(position + 1U, row_count - 1U)
                                                           : (position == 0U ? 0U : position - 1U);
        set_stop(all[next]);
        if (all[next].kind == StopKind::tab) select_tab(all[next].index);
    } else if (in_row && key == PhysicalKey::down) {
        if (all.size() <= row_count) return;
        set_stop(all[row_count]);
    } else if (!in_row && (key == PhysicalKey::right || key == PhysicalKey::left || key == PhysicalKey::up ||
                           key == PhysicalKey::down)) {
        const RibbonCommandSpec* const spec =
            current.kind == StopKind::command ? selected_command(current.command) : nullptr;
        if (key == PhysicalKey::down && spec != nullptr && has_menu((*spec).kind)) {
            static_cast<void>(show_drop_down((*spec).stable_id));
        } else if (key == PhysicalKey::down && current.kind == StopKind::group_button) {
            activate_stop(current);
        } else if (key == PhysicalKey::right || key == PhysicalKey::down) {
            if (position + 1U < all.size()) set_stop(all[position + 1U]);
        } else if (position > row_count) {
            set_stop(all[position - 1U]);
        } else {
            set_stop(Stop{StopKind::tab, selected_tab_.value_or(0U), {}});
        }
    } else if (key == PhysicalKey::enter || key == PhysicalKey::space) {
        set_stop(current);
        activate_stop(current);
    } else if (key == PhysicalKey::escape) {
        if (menu_open()) {
            close_menus();
        } else if (!in_row) {
            set_stop(Stop{StopKind::tab, selected_tab_.value_or(0U), {}});
        } else {
            return;
        }
    } else {
        return;
    }
    event.handled = true;
}

// Keys in the group popup: Tab and Shift+Tab through its stops and then out
// to its hosted controls; the arrows through its stops, wrapping; Down opens
// a split or drop-down command's menu; Enter and Space activate. Escape
// reaches the popup's layer, which closes it.
void Ribbon::popup_key(KeyEvent& event) {
    if (event.action != KeyAction::down || !effectively_enabled() || !popup_group_) return;
    const std::vector<Stop> all = popup_stops();
    if (all.empty()) return;
    std::size_t position = 0U;
    for (std::size_t index = 0; index < all.size() && popup_stop_; ++index) {
        if (all[index] == *popup_stop_) position = index;
    }
    const Stop current = all[position];
    // The current stop's command when it has a menu (a split or drop-down).
    const RibbonCommandSpec* current_menu = nullptr;
    if (current.kind == StopKind::command) {
        const RibbonCommandSpec* const spec = selected_command(current.command);
        if (spec != nullptr && has_menu((*spec).kind)) current_menu = spec;
    }
    const std::uint32_t key = event.physical_key;
    const bool shift = has_modifier(event.modifiers, Modifier::shift);
    std::optional<Stop> next{};
    if (key == PhysicalKey::tab) {
        if (!shift && position + 1U < all.size()) {
            next = all[position + 1U];
        } else if (shift && position > 0U) {
            next = all[position - 1U];
        } else {
            return;
        }
    } else if (key == PhysicalKey::down && current_menu != nullptr) {
        static_cast<void>(show_drop_down((*current_menu).stable_id));
    } else if (key == PhysicalKey::right || key == PhysicalKey::down) {
        next = all[(position + 1U) % all.size()];
    } else if (key == PhysicalKey::left || key == PhysicalKey::up) {
        next = all[position == 0U ? all.size() - 1U : position - 1U];
    } else if (key == PhysicalKey::enter || key == PhysicalKey::space) {
        popup_stop_ = current;
        event.handled = true;
        activate_stop(current);
        return;
    } else {
        return;
    }
    if (next) {
        popup_stop_ = next;
        invalidate_views(Dirty::paint | Dirty::semantics);
    }
    event.handled = true;
}

void Ribbon::on_focus_changed(bool focused) {
    focused_ = focused;
    if (focused_ && !stop_) stop_ = Stop{StopKind::tab, selected_tab_.value_or(0U), {}};
    invalidate(Dirty::paint | Dirty::semantics);
}

void Ribbon::popup_focus_changed(bool focused) {
    popup_focused_ = focused;
    if (focused && !popup_stop_) {
        const std::vector<Stop> all = popup_stops();
        if (!all.empty()) popup_stop_ = all[0];
    }
    invalidate_views(Dirty::paint | Dirty::semantics);
}

bool Ribbon::mnemonic_matches(char32_t character) const noexcept {
    if (is_mnemonic(character, application_text_)) return true;
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        if (shown(tab) && is_mnemonic(character, tabs_[tab].text)) return true;
    }
    if (minimized_ || plane_hidden_ || !selected_tab_) return false;
    for (const RibbonGroupSpec& group : tabs_[*selected_tab_].groups) {
        for (const RibbonCommandSpec& command : group.commands) {
            if (!hosted(command.kind) && is_mnemonic(character, authored_text(command))) return true;
        }
    }
    return false;
}

// A mnemonic opens the File menu, selects (and shows) a tab, or activates a
// command of the selected tab, in that order.
bool Ribbon::process_mnemonic_self(char32_t character) {
    if (is_mnemonic(character, application_text_)) {
        static_cast<void>(show_application_menu());
        return true;
    }
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        if (!shown(tab) || !is_mnemonic(character, tabs_[tab].text)) continue;
        select_tab(tab);
        if (minimized_) set_minimized(false);
        return true;
    }
    if (minimized_ || plane_hidden_ || !selected_tab_) return false;
    const std::vector<RibbonGroupSpec>& groups = tabs_[*selected_tab_].groups;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        for (std::size_t command = 0; command < groups[group].commands.size(); ++command) {
            const RibbonCommandSpec& spec = groups[group].commands[command];
            if (hosted(spec.kind) || !is_mnemonic(character, authored_text(spec))) continue;
            click_command(CommandAddress{group, command});
            return true;
        }
    }
    return false;
}

// -- menus -------------------------------------------------------------------

bool Ribbon::show_application_menu(MenuOpenMode mode) {
    require_mutable();
    if (backstage_) return show_backstage();
    if (application_menu_.empty() || attached_window() == nullptr || !effectively_enabled()) return false;
    if (!recent_items_.empty()) return show_application_popup(mode);
    if (application_open_ && (*popup_).is_open()) return true;
    close_menus();
    close_group_popup();
    const Layout placed = layout();
    (*popup_).set_items(application_menu_);
    const Rect absolute = absolute_bounds();
    // Over the ribbon, below the File tab (owner decision 23).
    (*popup_).show(shared_from_this(), {absolute.x + placed.application.x, absolute.y + placed.application.y +
                                                                               placed.application.height},
                   mode);
    application_open_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

// A command's bounds in window coordinates, in the plane or in the open
// group popup; none where it is not shown.
std::optional<Rect> Ribbon::command_window_bounds(CommandAddress address) const {
    if (popup_group_ == address.group && group_popup_) {
        const Layout placed = popup_layout((*group_popup_).panel());
        if (address.group >= placed.commands.size() || address.command >= placed.commands[address.group].size()) {
            return std::nullopt;
        }
        const Rect local = Rect::intersection(placed.commands[address.group][address.command], placed.groups_area);
        if (local.empty()) return std::nullopt;
        return to_window((*group_popup_).absolute_bounds(), local);
    }
    const Layout placed = layout();
    if (address.group >= placed.commands.size() || address.command >= placed.commands[address.group].size()) {
        return std::nullopt;
    }
    const Rect local = Rect::intersection(placed.commands[address.group][address.command], placed.groups_area);
    if (local.empty()) return std::nullopt;
    return to_window(absolute_bounds(), local);
}

bool Ribbon::show_drop_down(std::string_view command_id, MenuOpenMode mode) {
    require_mutable();
    const std::optional<CommandAddress> address = find_command(command_id);
    if (!address || minimized_ || plane_hidden_ || attached_window() == nullptr) return false;
    const RibbonCommandSpec& spec = *selected_command(*address);
    if (!has_menu(spec.kind) || !command_enabled(spec)) return false;
    const std::optional<Rect> bounds = command_window_bounds(*address);
    if (!bounds) return false;
    if (open_command_ == address && (*popup_).is_open()) return true;
    close_menus();
    (*popup_).set_items(spec.drop_down);
    (*popup_).show(shared_from_this(), {(*bounds).x, (*bounds).y + (*bounds).height}, mode);
    open_command_ = address;
    invalidate_views(Dirty::paint | Dirty::semantics);
    return true;
}

void Ribbon::close_menus() noexcept {
    if (popup_) (*popup_).close();
    close_application_popup();
}

bool Ribbon::menu_open() const noexcept {
    const bool result = (popup_ && (*popup_).is_open()) || application_popup_open_;
    return result;
}

void Ribbon::on_popup_open_changed(bool open) {
    if (open) return;
    application_open_ = false;
    open_command_.reset();
    invalidate_views(Dirty::paint | Dirty::semantics);
}

void Ribbon::on_popup_item_invoked(const MenuItemInvocation& invocation) {
    if (application_open_) {
        application_menu_item_invoked_.emit(invocation);
        return;
    }
    if (!open_command_) return;
    const RibbonCommandSpec* const spec = selected_command(*open_command_);
    if (spec == nullptr) return;
    const RibbonDropDownInvocation chosen{std::string(stable_id().value()), (*spec).stable_id, invocation};
    drop_down_item_invoked_.emit(chosen);
}

// -- group popup -----------------------------------------------------------------

bool Ribbon::show_group_popup(std::string_view group_id, MenuOpenMode mode) {
    require_mutable();
    const std::optional<std::size_t> found = find_group(group_id);
    Window* const window = attached_window();
    if (!found || window == nullptr || !effectively_enabled()) return false;
    const std::size_t group = *found;
    const Layout placed = layout();
    if (!group_collapsed(placed, group) || Rect::intersection(placed.groups[group], placed.groups_area).empty()) {
        return false;
    }
    if (popup_group_ == group) return true;
    close_menus();
    close_group_popup();
    const std::string ribbon_id(stable_id().value());
    const Control::Ptr self = shared_from_this();
    if (!group_popup_) {
        group_popup_ = make_control<RibbonGroupPopup>(StableId(ribbon_id + ".group-popup"), *this);
        group_layer_ = make_control<AnchoredPopupLayer>(StableId(ribbon_id + ".group-popup.layer"), self);
        (*group_layer_).set_content(group_popup_);
        group_layer_dismissed_ = (*group_layer_).dismiss_requested().subscribe(
            Delegate<PopupDismissReason>::bind<Ribbon, &Ribbon::on_group_layer_dismissed>(*this));
    }
    popup_group_ = group;
    popup_stop_.reset();
    const RibbonGroupSpec& spec = tabs_[*selected_tab_].groups[group];
    (*group_popup_).accessible_name = spec.caption;
    move_hosted_to_popup(group);
    // The content runs from the ribbon's left edge (near alignment) to the
    // panel's right; the panel starts under the group's button.
    const Rect absolute = absolute_bounds();
    const Size panel = popup_panel_size();
    AnchoredPopupPlacement placement;
    placement.preferred_size = {placed.groups[group].x + panel.width, panel.height};
    placement.horizontal_alignment = PopupHorizontalAlignment::near;
    placement.vertical_preference = PopupVerticalPreference::below;
    placement.gap = popup_gap;
    (*group_layer_).set_anchor(self);
    (*group_layer_).set_placement(placement);
    (*group_popup_).set_anchor_x(absolute.x + placed.groups[group].x);
    const Size client = (*window).client_size();
    (*group_layer_).set_requested_bounds({0.0, 0.0, client.width, client.height});
    group_popup_token_ = (*window).open_popup(self, group_layer_);
    if (Event<>* closed = group_popup_token_.closed_event()) {
        group_popup_revoked_ =
            (*closed).subscribe(*this, Delegate<>::bind<Ribbon, &Ribbon::on_group_popup_revoked>(*this));
    }
    if (mode == MenuOpenMode::keyboard) {
        const std::vector<Stop> all = popup_stops();
        if (!all.empty()) popup_stop_ = all[0];
    }
    group_scope_ = (*window).begin_focus_scope(group_layer_, group_popup_);
    invalidate_views(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    return true;
}

void Ribbon::close_group_popup() noexcept {
    if (!popup_group_) return;
    Window* const window = attached_window();
    if (window != nullptr && group_scope_) {
        try {
            static_cast<void>((*window).end_focus_scope(group_scope_));
        } catch (...) {
        }
    }
    group_scope_ = {};
    finish_group_popup();
    group_popup_revoked_.disconnect();
    group_popup_token_.disconnect();
}

// The popup is gone or going: its hosted controls return to the ribbon and
// its interaction state is cleared.
void Ribbon::finish_group_popup() noexcept {
    move_hosted_to_ribbon();
    if (popup_group_ && pressed_command_ && (*pressed_command_).group == *popup_group_) pressed_command_.reset();
    popup_group_.reset();
    popup_stop_.reset();
    popup_focused_ = false;
    pressed_group_.reset();
    hot_ = Hit{};
    if (!is_alive()) return;
    try {
        invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    } catch (...) {
    }
}

std::optional<std::string> Ribbon::popup_group() const {
    if (!popup_group_ || !selected_tab_) return std::nullopt;
    return tabs_[*selected_tab_].groups[*popup_group_].stable_id;
}

Control::Ptr Ribbon::group_popup() const noexcept {
    return group_popup_;
}

// The popup's group's hosted controls move into the popup content while it
// is open, and galleries show at their widest.
void Ribbon::move_hosted_to_popup(std::size_t group) {
    const std::size_t tab = *selected_tab_;
    std::vector<CommandRecord>& records = records_[tab][group];
    for (std::size_t command = 0; command < records.size(); ++command) {
        const Control::Ptr control = records[command].hosted;
        if (!control || (*control).parent().get() != this) continue;
        Control::Ptr moved = remove_child((*control).runtime_id());
        if (!moved) continue;
        (*group_popup_).add_child(std::move(moved));
        const RibbonCommandSpec& spec = tabs_[tab].groups[group].commands[command];
        if (spec.kind == RibbonCommandKind::gallery) (*spec.gallery).set_columns((*spec.gallery).maximum_columns());
    }
}

// Returns the popup's hosted controls to the ribbon, in record order (the
// hosted controls' Tab order).
void Ribbon::move_hosted_to_ribbon() noexcept {
    if (!group_popup_) return;
    try {
        const Control::Ptr popup = group_popup_;
        std::vector<Control::Ptr> returning;
        for (const Control::Ptr& child : (*popup).children()) returning.push_back(child);
        for (const Control::Ptr& child : returning) {
            Control::Ptr moved = (*popup).remove_child((*child).runtime_id());
            if (moved && is_alive()) add_child(std::move(moved));
        }
        if (!is_alive()) return;
        std::size_t position = 0U;
        for (const std::vector<std::vector<CommandRecord>>& tab : records_) {
            for (const std::vector<CommandRecord>& group : tab) {
                for (const CommandRecord& record : group) {
                    if (!record.hosted || (*record.hosted).parent().get() != this) continue;
                    static_cast<void>(set_child_index((*record.hosted).runtime_id(), position));
                    ++position;
                }
            }
        }
    } catch (...) {
    }
}

void Ribbon::on_group_layer_dismissed(PopupDismissReason) {
    close_group_popup();
}

// The window closed the popup itself (its owner left the window, or the
// window closes): nothing remains to end but the ribbon's own state.
void Ribbon::on_group_popup_revoked() {
    if (!popup_group_) return;
    group_scope_ = {};
    finish_group_popup();
    group_popup_revoked_.disconnect();
}

// -- semantics -----------------------------------------------------------------

SemanticDescriptor Ribbon::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read().empty() ? "Ribbon" : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

// One group's node under `parent`: a group of its commands and launcher, or,
// collapsed, a menu button that opens its popup.
void Ribbon::append_group_node(SemanticNode& parent, const Layout& placed, std::size_t group, Rect absolute) const {
    const std::string ribbon_id(stable_id().value());
    const RibbonGroupSpec& spec = tabs_[*selected_tab_].groups[group];
    const Rect visible = Rect::intersection(placed.groups[group], placed.groups_area);
    if (visible.empty()) return;
    SemanticState base = SemanticState::visible;
    if (effectively_enabled()) base |= SemanticState::enabled;
    SemanticNode group_node{};
    group_node.stable_id = node_identity(ribbon_id, "group", spec.stable_id);
    group_node.name = spec.caption;
    group_node.bounds = to_window(absolute, visible);
    if (group_collapsed(placed, group)) {
        group_node.role = SemanticRole::menu_button;
        group_node.has_popup = true;
        group_node.states = base | SemanticState::focusable;
        if (popup_group_ == group) group_node.states |= SemanticState::expanded;
        if (focused_ && stop_ && *stop_ == Stop{StopKind::group_button, group, {}}) {
            group_node.states |= SemanticState::focused;
        }
        group_node.actions = {SemanticAction::press, SemanticAction::show_menu, SemanticAction::expand,
                              SemanticAction::collapse};
        parent.children.push_back(std::move(group_node));
        return;
    }
    group_node.role = SemanticRole::group;
    group_node.states = base;
    // Commands count their place among the group's ribbon-drawn commands;
    // hosted controls describe themselves as children of the ribbon (or of
    // the popup).
    std::size_t set_size = 0U;
    for (std::size_t command = 0; command < spec.commands.size(); ++command) {
        if (!hosted(spec.commands[command].kind) && !placed.commands[group][command].empty()) ++set_size;
    }
    std::size_t position = 0U;
    for (std::size_t command = 0; command < spec.commands.size(); ++command) {
        const RibbonCommandSpec& item = spec.commands[command];
        const Rect bounds = Rect::intersection(placed.commands[group][command], visible);
        if (hosted(item.kind) || placed.commands[group][command].empty()) continue;
        ++position;
        const CommandAddress address{group, command};
        const Stop stop{StopKind::command, 0U, address};
        SemanticNode node{};
        node.stable_id = node_identity(ribbon_id, "command", item.stable_id);
        node.role = SemanticRole::button;
        if (item.kind == RibbonCommandKind::split) node.role = SemanticRole::split_button;
        if (item.kind == RibbonCommandKind::drop_down) node.role = SemanticRole::menu_button;
        node.has_popup = has_menu(item.kind);
        node.name = command_text(item);
        // The command's keyboard letter, its mnemonic (the ribbon has no
        // KeyTips, RB-16), is its access key (owner decision, stage A gap 5).
        node.access_key = mnemonic_access_key(authored_text(item));
        node.position_in_set = position;
        node.set_size = set_size;
        node.bounds = to_window(absolute, bounds);
        node.states = SemanticState::visible | SemanticState::focusable;
        if (command_enabled(item)) node.states |= SemanticState::enabled;
        if (item.kind == RibbonCommandKind::toggle) node.states |= SemanticState::checkable;
        if (command_checked(item)) node.states |= SemanticState::checked;
        if (open_command_ == address) node.states |= SemanticState::expanded;
        const bool own_focus = focused_ && stop_ && *stop_ == stop;
        const bool popup_focus = popup_focused_ && popup_stop_ && *popup_stop_ == stop;
        if (own_focus || popup_focus) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::press};
        if (has_menu(item.kind)) node.actions.push_back(SemanticAction::show_menu);
        group_node.children.push_back(std::move(node));
    }
    if (spec.dialog_launcher) {
        const GroupParts parts = group_parts(placed, group);
        SemanticNode launcher{};
        launcher.stable_id = node_identity(ribbon_id, "launcher", spec.stable_id);
        launcher.role = SemanticRole::button;
        launcher.name = spec.caption + " dialog launcher";
        launcher.bounds = to_window(absolute, parts.group_launcher.bounds);
        launcher.states = base | SemanticState::focusable;
        launcher.actions = {SemanticAction::press};
        group_node.children.push_back(std::move(launcher));
    }
    parent.children.push_back(std::move(group_node));
}

std::vector<SemanticNode> Ribbon::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    const std::string ribbon_id(stable_id().value());
    const Rect absolute = absolute_bounds();
    const Layout placed = layout();
    const bool enabled = effectively_enabled();
    SemanticState base = SemanticState::visible;
    if (enabled) base |= SemanticState::enabled;

    SemanticNode application{};
    application.stable_id = ribbon_id + ".application";
    application.runtime_id = 0U;
    application.role = SemanticRole::menu_button;
    application.name = parse_mnemonic_text(application_text_).display_text;
    application.access_key = mnemonic_access_key(application_text_);
    application.has_popup = true;
    application.bounds = to_window(absolute, placed.application);
    application.states = base | SemanticState::focusable;
    if (file_area_open()) application.states |= SemanticState::expanded;
    application.actions = {SemanticAction::press, SemanticAction::show_menu};
    nodes.push_back(std::move(application));

    SemanticNode row{};
    row.stable_id = ribbon_id + ".tab-row";
    row.role = SemanticRole::tab_group;
    row.name = "Ribbon tabs";
    row.orientation = SemanticOrientation::horizontal;
    row.bounds = to_window(absolute, placed.tab_rail);
    row.states = base;
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        if (!shown(tab)) continue;
        // Core tabs count their place among the core tabs shown, and a
        // contextual tab among its set's; its set's caption describes it.
        const TabPosition place = position_among_shown(tab);
        SemanticNode node{};
        node.stable_id = node_identity(ribbon_id, "tab", tabs_[tab].stable_id);
        node.role = SemanticRole::tab;
        node.name = parse_mnemonic_text(tabs_[tab].text).display_text;
        node.access_key = mnemonic_access_key(tabs_[tab].text);
        if (tab_set_of_[tab]) node.description = tab_sets_[*tab_set_of_[tab]].caption;
        node.position_in_set = place.position;
        node.set_size = place.count;
        node.bounds = to_window(absolute, placed.tabs[tab]);
        node.states = base | SemanticState::focusable;
        if (selected_tab_ == tab) node.states |= SemanticState::selected;
        if (focused_ && stop_ && *stop_ == Stop{StopKind::tab, tab, {}}) node.states |= SemanticState::focused;
        // The selected tab shows the command plane, its panel (owner
        // decision DK-40). Tabs publish select, not press (DK-35); a press
        // sent anyway still clicks the tab (on_semantic_child_action).
        if (selected_tab_ == tab && !placed.plane.empty()) node.controlled_panel = ribbon_id + ".command-plane";
        node.actions = {SemanticAction::select};
        row.children.push_back(std::move(node));
    }
    nodes.push_back(std::move(row));
    if (!selected_tab_) return nodes;

    // The command plane is published while minimized too, collapsed, so that
    // assistive technology can restore it: tabs expose selection and never
    // invoke, so clicking the selected tab is no path for them (owner
    // decision 46). Expand and collapse drive the same minimize and restore.
    SemanticNode plane{};
    plane.stable_id = ribbon_id + ".command-plane";
    plane.role = SemanticRole::tab_panel;
    plane.name = parse_mnemonic_text(tabs_[*selected_tab_].text).display_text;
    plane.bounds = to_window(absolute, placed.plane);
    plane.states = base;
    if (!placed.plane.empty()) plane.states |= SemanticState::expanded;
    plane.actions = {minimized_ ? SemanticAction::expand : SemanticAction::collapse};
    if (placed.plane.empty()) {
        nodes.push_back(std::move(plane));
        return nodes;
    }
    if (!placed.scroll_left.empty()) {
        SemanticNode scroll{};
        scroll.stable_id = ribbon_id + ".scroll-left";
        scroll.role = SemanticRole::button;
        scroll.name = "Scroll groups left";
        scroll.bounds = to_window(absolute, placed.scroll_left);
        scroll.states = base;
        scroll.actions = {SemanticAction::press};
        plane.children.push_back(std::move(scroll));
    }
    for (std::size_t group = 0; group < tabs_[*selected_tab_].groups.size(); ++group) {
        append_group_node(plane, placed, group, absolute);
    }
    if (!placed.scroll_right.empty()) {
        SemanticNode scroll{};
        scroll.stable_id = ribbon_id + ".scroll-right";
        scroll.role = SemanticRole::button;
        scroll.name = "Scroll groups right";
        scroll.bounds = to_window(absolute, placed.scroll_right);
        scroll.states = base;
        scroll.actions = {SemanticAction::press};
        plane.children.push_back(std::move(scroll));
    }
    nodes.push_back(std::move(plane));
    return nodes;
}

std::vector<SemanticNode> Ribbon::popup_semantic_children(Rect panel) const {
    if (!selected_tab_ || !popup_group_ || !group_popup_) return {};
    const Layout placed = popup_layout(panel);
    SemanticNode holder{};
    append_group_node(holder, placed, *popup_group_, (*group_popup_).absolute_bounds());
    if (holder.children.empty()) return {};
    std::vector<SemanticNode> nodes = std::move(holder.children[0].children);
    return nodes;
}

bool Ribbon::popup_semantic_action(std::string_view id, SemanticAction action) {
    const bool handled = on_semantic_child_action(id, action, {});
    return handled;
}

bool Ribbon::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    if (!effectively_enabled()) return false;
    const std::string ribbon_id(stable_id().value());
    if (id == ribbon_id + ".application") {
        if (action == SemanticAction::collapse) {
            close_menus();
            close_backstage();
            return true;
        }
        if (action != SemanticAction::press && action != SemanticAction::show_menu &&
            action != SemanticAction::expand) {
            return false;
        }
        return show_application_menu();
    }
    if (id == ribbon_id + ".command-plane" && (action == SemanticAction::expand || action == SemanticAction::collapse)) {
        if (!selected_tab_) return false;
        set_minimized(action == SemanticAction::collapse);
        return true;
    }
    if (action == SemanticAction::press && (id == ribbon_id + ".scroll-left" || id == ribbon_id + ".scroll-right")) {
        scroll_groups(id == ribbon_id + ".scroll-right");
        return true;
    }
    const std::string_view tab_id = node_suffix(id, ribbon_id, "tab");
    for (std::size_t tab = 0; tab < tabs_.size() && !tab_id.empty(); ++tab) {
        if (tabs_[tab].stable_id != tab_id) continue;
        if (!shown(tab)) return false;
        if (action == SemanticAction::press) {
            click_tab(tab);
            return true;
        }
        if (action != SemanticAction::select) return false;
        select_tab(tab);
        if (minimized_) set_minimized(false);
        return true;
    }
    const std::string_view group_id = node_suffix(id, ribbon_id, "group");
    if (!group_id.empty()) {
        if (action == SemanticAction::collapse && popup_group() == std::string(group_id)) {
            close_group_popup();
            return true;
        }
        if (action != SemanticAction::press && action != SemanticAction::show_menu &&
            action != SemanticAction::expand) {
            return false;
        }
        return show_group_popup(group_id);
    }
    const std::string_view command_id = node_suffix(id, ribbon_id, "command");
    if (!command_id.empty()) {
        const std::optional<CommandAddress> address = find_command(command_id);
        if (!address || minimized_ || plane_hidden_) return false;
        if (action == SemanticAction::press) {
            click_command(*address);
            return true;
        }
        if (action == SemanticAction::show_menu || action == SemanticAction::expand) {
            return show_drop_down(command_id);
        }
        return false;
    }
    const std::string_view launcher_id = node_suffix(id, ribbon_id, "launcher");
    if (!launcher_id.empty() && action == SemanticAction::press && selected_tab_ && !minimized_ && !plane_hidden_) {
        const std::vector<RibbonGroupSpec>& groups = tabs_[*selected_tab_].groups;
        for (std::size_t group = 0; group < groups.size(); ++group) {
            if (groups[group].stable_id != launcher_id) continue;
            launch(group);
            return true;
        }
    }
    return false;
}

// -- anatomy -----------------------------------------------------------------

namespace {

constexpr std::array<AnatomyState, 4> application_states{
    {AnatomyState::hot, AnatomyState::pressed, AnatomyState::open, AnatomyState::focus_visible}};
constexpr std::array<AnatomyState, 3> launcher_states{
    {AnatomyState::hot, AnatomyState::pressed, AnatomyState::focus_visible}};
constexpr std::array<AnatomyState, 2> arrow_states{{AnatomyState::hot, AnatomyState::pressed}};
constexpr std::array<AnatomyState, 4> group_button_part_states{
    {AnatomyState::hot, AnatomyState::pressed, AnatomyState::open, AnatomyState::focus_visible}};

constexpr std::array<PartField<Ribbon::Parts>, 10> ribbon_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Ribbon::Parts::region},
    {.declaration = {.name = "tab_rail", .role = PartRole::face, .composed_from = {}},
     .member = &Ribbon::Parts::tab_rail},
    {.declaration = {.name = "application_button", .role = PartRole::face, .composed_from = "button",
                     .states = application_states},
     .member = &Ribbon::Parts::application_button},
    {.declaration = {.name = "application_label", .role = PartRole::label, .composed_from = {},
                     .states = application_states},
     .member = &Ribbon::Parts::application_label},
    {.declaration = {.name = "command_plane", .role = PartRole::face, .composed_from = {}},
     .member = &Ribbon::Parts::command_plane},
    {.declaration = {.name = "scroll_left", .role = PartRole::actuator, .composed_from = "button",
                     .states = arrow_states},
     .member = &Ribbon::Parts::scroll_left},
    {.declaration = {.name = "scroll_left_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = arrow_states},
     .member = &Ribbon::Parts::scroll_left_glyph},
    {.declaration = {.name = "scroll_right", .role = PartRole::actuator, .composed_from = "button",
                     .states = arrow_states},
     .member = &Ribbon::Parts::scroll_right},
    {.declaration = {.name = "scroll_right_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = arrow_states},
     .member = &Ribbon::Parts::scroll_right_glyph},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &Ribbon::Parts::focus},
}};

constexpr std::array<PartField<Ribbon::TabParts>, 3> ribbon_tab_part_fields{{
    {.declaration = {.name = "tab", .role = PartRole::face, .composed_from = {}},
     .member = &Ribbon::TabParts::tab},
    {.declaration = {.name = "tab_label", .role = PartRole::label, .composed_from = {}},
     .member = &Ribbon::TabParts::tab_label},
    {.declaration = {.name = "tab_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &Ribbon::TabParts::tab_focus},
}};

constexpr std::array<StateField<Ribbon::TabStates>, 3> ribbon_tab_state_fields{{
    {.state = AnatomyState::hot, .member = &Ribbon::TabStates::hot},
    {.state = AnatomyState::selected, .member = &Ribbon::TabStates::selected},
    {.state = AnatomyState::focus_visible, .member = &Ribbon::TabStates::focus_visible},
}};

constexpr std::array<PartField<Ribbon::GroupParts>, 11> ribbon_group_part_fields{{
    {.declaration = {.name = "group_region", .role = PartRole::region, .composed_from = {}},
     .member = &Ribbon::GroupParts::group_region},
    {.declaration = {.name = "group_caption", .role = PartRole::caption, .composed_from = {}},
     .member = &Ribbon::GroupParts::group_caption},
    {.declaration = {.name = "group_divider", .role = PartRole::divider, .composed_from = {}},
     .member = &Ribbon::GroupParts::group_divider},
    {.declaration = {.name = "group_launcher", .role = PartRole::actuator, .composed_from = "button",
                     .states = launcher_states},
     .member = &Ribbon::GroupParts::group_launcher},
    {.declaration = {.name = "group_launcher_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = launcher_states},
     .member = &Ribbon::GroupParts::group_launcher_glyph},
    {.declaration = {.name = "group_launcher_focus", .role = PartRole::focus, .composed_from = {},
                     .states = launcher_states},
     .member = &Ribbon::GroupParts::group_launcher_focus},
    {.declaration = {.name = "group_button", .role = PartRole::face, .composed_from = "button",
                     .states = group_button_part_states},
     .member = &Ribbon::GroupParts::group_button},
    {.declaration = {.name = "group_button_icon", .role = PartRole::icon, .composed_from = {},
                     .states = group_button_part_states},
     .member = &Ribbon::GroupParts::group_button_icon},
    {.declaration = {.name = "group_button_label", .role = PartRole::label, .composed_from = {},
                     .states = group_button_part_states},
     .member = &Ribbon::GroupParts::group_button_label},
    {.declaration = {.name = "group_button_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = group_button_part_states},
     .member = &Ribbon::GroupParts::group_button_glyph},
    {.declaration = {.name = "group_button_focus", .role = PartRole::focus, .composed_from = {},
                     .states = group_button_part_states},
     .member = &Ribbon::GroupParts::group_button_focus},
}};

constexpr std::array<PartField<Ribbon::CommandParts>, 13> ribbon_command_part_fields{{
    {.declaration = {.name = "command_face", .role = PartRole::face, .composed_from = {}},
     .member = &Ribbon::CommandParts::command_face},
    {.declaration = {.name = "command_icon", .role = PartRole::icon, .composed_from = {}},
     .member = &Ribbon::CommandParts::command_icon},
    {.declaration = {.name = "command_label", .role = PartRole::label, .composed_from = {}},
     .member = &Ribbon::CommandParts::command_label},
    {.declaration = {.name = "command_arrow", .role = PartRole::actuator, .composed_from = "button",
                     .states = arrow_states},
     .member = &Ribbon::CommandParts::command_arrow},
    {.declaration = {.name = "command_arrow_glyph", .role = PartRole::glyph, .composed_from = {}},
     .member = &Ribbon::CommandParts::command_arrow_glyph},
    {.declaration = {.name = "command_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &Ribbon::CommandParts::command_focus},
    {.declaration = {.name = "command_combo_box", .role = PartRole::well, .composed_from = "combo_box"},
     .member = &Ribbon::CommandParts::command_combo_box},
    {.declaration = {.name = "command_numeric_up_down", .role = PartRole::well,
                     .composed_from = "numeric_up_down"},
     .member = &Ribbon::CommandParts::command_numeric_up_down},
    {.declaration = {.name = "command_check_box", .role = PartRole::indicator, .composed_from = "check_box"},
     .member = &Ribbon::CommandParts::command_check_box},
    {.declaration = {.name = "command_text_box", .role = PartRole::well, .composed_from = "text_box"},
     .member = &Ribbon::CommandParts::command_text_box},
    {.declaration = {.name = "command_gallery", .role = PartRole::well, .composed_from = "ribbon_gallery"},
     .member = &Ribbon::CommandParts::command_gallery},
    {.declaration = {.name = "command_font_control", .role = PartRole::well, .composed_from = "ribbon_font_control"},
     .member = &Ribbon::CommandParts::command_font_control},
    {.declaration = {.name = "command_color_picker", .role = PartRole::actuator,
                     .composed_from = "ribbon_color_picker"},
     .member = &Ribbon::CommandParts::command_color_picker},
}};

constexpr std::array<StateField<Ribbon::CommandStates>, 6> ribbon_command_state_fields{{
    {.state = AnatomyState::hot, .member = &Ribbon::CommandStates::hot},
    {.state = AnatomyState::pressed, .member = &Ribbon::CommandStates::pressed},
    {.state = AnatomyState::checked, .member = &Ribbon::CommandStates::checked},
    {.state = AnatomyState::open, .member = &Ribbon::CommandStates::open},
    {.state = AnatomyState::disabled, .member = &Ribbon::CommandStates::disabled},
    {.state = AnatomyState::focus_visible, .member = &Ribbon::CommandStates::focus_visible},
}};

constexpr std::array<PartDeclaration, 10> ribbon_parts = part_declarations(ribbon_part_fields);
constexpr std::array<PartDeclaration, 3> ribbon_tab_parts = part_declarations(ribbon_tab_part_fields);
constexpr std::array<AnatomyState, 3> ribbon_tab_states = state_declarations(ribbon_tab_state_fields);
constexpr std::array<PartDeclaration, 11> ribbon_group_parts = part_declarations(ribbon_group_part_fields);
constexpr std::array<PartDeclaration, 13> ribbon_command_parts = part_declarations(ribbon_command_part_fields);
constexpr std::array<AnatomyState, 6> ribbon_command_states = state_declarations(ribbon_command_state_fields);
constexpr std::array<AnatomyState, 5> ribbon_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ItemSystemIndex tab_system{0U};
constexpr ItemSystemIndex group_system{1U};
constexpr ItemSystemIndex command_system{2U};

constexpr std::array<ItemSystemDeclaration, 3> ribbon_item_systems{{
    {.name = "tab", .parts = ribbon_tab_parts, .states = ribbon_tab_states, .custom_states = {}},
    {.name = "group", .parts = ribbon_group_parts, .states = {}, .custom_states = {}},
    {.name = "command", .parts = ribbon_command_parts, .states = ribbon_command_states, .custom_states = {}},
}};

constexpr ControlAnatomy ribbon_anatomy{
    .control_id = "ribbon",
    .parts = ribbon_parts,
    .states = ribbon_states,
    .custom_states = {},
    .item_systems = ribbon_item_systems,
};

} // namespace

const ControlAnatomy& Ribbon::anatomy() const noexcept {
    return ribbon_anatomy;
}

void Ribbon::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Layout placed = layout();
    const Parts record = control_parts(placed);
    write_parts(anatomy(), ribbon_part_fields, record, geometry);
}

Ribbon::Parts Ribbon::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), ribbon_part_fields, published.parts(), record);
    return record;
}

// The realized items in paint order: the tabs, then the selected tab's shown
// groups, then the shown commands of its groups that are not collapsed.
std::vector<Ribbon::RealizedItem> Ribbon::realized_list(const Layout& placed) const {
    std::vector<RealizedItem> list;
    list.reserve(tabs_.size());
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        if (!shown(tab)) continue;
        RealizedItem item;
        item.kind = RealizedItem::Kind::tab;
        item.index = tab;
        list.push_back(item);
    }
    if (placed.plane.empty() || !selected_tab_) return list;
    const std::vector<RibbonGroupSpec>& groups = tabs_[*selected_tab_].groups;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (Rect::intersection(placed.groups[group], placed.groups_area).empty()) continue;
        RealizedItem item;
        item.kind = RealizedItem::Kind::group;
        item.index = group;
        list.push_back(item);
    }
    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (Rect::intersection(placed.groups[group], placed.groups_area).empty()) continue;
        for (std::size_t command = 0; command < groups[group].commands.size(); ++command) {
            if (Rect::intersection(placed.commands[group][command], placed.groups_area).empty()) continue;
            RealizedItem item;
            item.kind = RealizedItem::Kind::command;
            item.command = CommandAddress{group, command};
            list.push_back(item);
        }
    }
    return list;
}

Ribbon::RealizedItem Ribbon::locate_realized(const Layout& placed, std::size_t item) const {
    const std::vector<RealizedItem> list = realized_list(placed);
    if (item >= list.size()) return RealizedItem{};
    return list[item];
}

std::size_t Ribbon::realized_item_count() const {
    const std::vector<RealizedItem> list = realized_list(layout());
    return list.size();
}

std::string Ribbon::realized_item_stable_id(std::size_t item) const {
    const RealizedItem located = locate_realized(layout(), item);
    const std::string ribbon_id(stable_id().value());
    if (located.kind == RealizedItem::Kind::tab) return node_identity(ribbon_id, "tab", tabs_[located.index].stable_id);
    if (located.kind == RealizedItem::Kind::group) {
        return node_identity(ribbon_id, "group", (*selected_group(located.index)).stable_id);
    }
    if (located.kind == RealizedItem::Kind::command) {
        return node_identity(ribbon_id, "command", (*selected_command(located.command)).stable_id);
    }
    return {};
}

ItemSystemIndex Ribbon::publish_realized_item_system(std::size_t item) const {
    const RealizedItem located = locate_realized(layout(), item);
    if (located.kind == RealizedItem::Kind::group) return group_system;
    if (located.kind == RealizedItem::Kind::command) return command_system;
    return tab_system;
}

void Ribbon::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const Layout placed = layout();
    const RealizedItem located = locate_realized(placed, item);
    switch (located.kind) {
    case RealizedItem::Kind::tab: {
        const TabParts record = tab_parts(placed, located.index);
        write_item_parts(anatomy().item_systems[tab_system.value], ribbon_tab_part_fields, record, geometry);
        return;
    }
    case RealizedItem::Kind::group: {
        const GroupParts record = group_parts(placed, located.index);
        write_item_parts(anatomy().item_systems[group_system.value], ribbon_group_part_fields, record, geometry);
        return;
    }
    case RealizedItem::Kind::command: {
        const CommandParts record = command_parts(placed, located.command);
        write_item_parts(anatomy().item_systems[command_system.value], ribbon_command_part_fields, record, geometry);
        return;
    }
    case RealizedItem::Kind::none:
        // Past the end: no item, no parts.
        return;
    }
    // A kind outside the enumeration publishes no parts.
    return;
}

AnatomyStateSet Ribbon::realized_item_anatomy_states(std::size_t item) const {
    const RealizedItem located = locate_realized(layout(), item);
    if (located.kind == RealizedItem::Kind::tab) return tab_states(located.index);
    if (located.kind == RealizedItem::Kind::command) return command_states(located.command);
    return AnatomyStateSet{};
}

std::vector<Ribbon::Tab> Ribbon::realized_tabs() const {
    const Layout placed = layout();
    std::vector<Tab> records;
    records.reserve(tabs_.size());
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        if (!shown(tab)) continue;
        Tab record;
        record.index = tab;
        record.stable_id = tabs_[tab].stable_id;
        record.parts = tab_parts(placed, tab);
        read_states(ribbon_tab_state_fields, tab_states(tab), record.states);
        records.push_back(record);
    }
    return records;
}

std::vector<Ribbon::Group> Ribbon::groups_of(const Layout& placed) const {
    std::vector<Group> records;
    if (placed.plane.empty() || !selected_tab_) return records;
    const std::vector<RibbonGroupSpec>& groups = tabs_[*selected_tab_].groups;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (Rect::intersection(placed.groups[group], placed.groups_area).empty()) continue;
        Group record;
        record.index = group;
        record.stable_id = groups[group].stable_id;
        record.layout = placed.group_layouts[group];
        record.collapsed = group_collapsed(placed, group);
        record.parts = group_parts(placed, group);
        records.push_back(record);
    }
    return records;
}

std::vector<Ribbon::CommandItem> Ribbon::commands_of(const Layout& placed) const {
    std::vector<CommandItem> records;
    if (placed.plane.empty() || !selected_tab_) return records;
    const std::vector<RibbonGroupSpec>& groups = tabs_[*selected_tab_].groups;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (Rect::intersection(placed.groups[group], placed.groups_area).empty()) continue;
        for (std::size_t command = 0; command < groups[group].commands.size(); ++command) {
            if (Rect::intersection(placed.commands[group][command], placed.groups_area).empty()) continue;
            const CommandAddress address{group, command};
            CommandItem record;
            record.group = group;
            record.index = command;
            record.stable_id = groups[group].commands[command].stable_id;
            record.parts = command_parts(placed, address);
            read_states(ribbon_command_state_fields, command_states(address), record.states);
            records.push_back(record);
        }
    }
    return records;
}

std::vector<Ribbon::Group> Ribbon::realized_groups() const {
    const std::vector<Group> records = groups_of(layout());
    return records;
}

std::vector<Ribbon::CommandItem> Ribbon::realized_commands() const {
    const std::vector<CommandItem> records = commands_of(layout());
    return records;
}

std::vector<Ribbon::Group> Ribbon::realized_popup_groups() const {
    if (!popup_group_ || !group_popup_) return {};
    const std::vector<Group> records = groups_of(popup_layout((*group_popup_).panel()));
    return records;
}

std::vector<Ribbon::CommandItem> Ribbon::realized_popup_commands() const {
    if (!popup_group_ || !group_popup_) return {};
    const std::vector<CommandItem> records = commands_of(popup_layout((*group_popup_).panel()));
    return records;
}

namespace {

// The events Ribbon adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 8> ribbon_interactions{{
    {.name = "command_clicked", .payload = "RibbonCommandClick", .mutable_payload = false},
    {.name = "drop_down_item_invoked", .payload = "RibbonDropDownInvocation", .mutable_payload = false},
    {.name = "dialog_launcher_clicked", .payload = "RibbonDialogLaunch", .mutable_payload = false},
    {.name = "selected_tab_changed", .payload = "RibbonTabChange", .mutable_payload = false},
    {.name = "minimized_changed", .payload = "bool", .mutable_payload = false},
    {.name = "application_menu_item_invoked", .payload = "MenuItemInvocation", .mutable_payload = false},
    {.name = "recent_item_invoked", .payload = "RibbonRecentItemInvocation", .mutable_payload = false},
    {.name = "backstage_open_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

void Ribbon::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), ribbon_interactions.begin(), ribbon_interactions.end());
}

} // namespace gui_forms
