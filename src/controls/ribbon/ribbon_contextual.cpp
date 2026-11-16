// Ribbon contextual tab sets and application modes (stage RB, phase 5; owner
// decisions 14, 22, 25; RIBBON_STUDY.md §5.4): which tabs and groups show,
// the sets' accents from the theme's ramp, and the built-in selection rules.

#include "gui_forms/controls/ribbon.hpp"

#include <algorithm>
#include <stdexcept>

namespace gui_forms {

// -- what shows ----------------------------------------------------------------

// A tab shows while it shares a mode with the ribbon and, for a contextual
// tab, while its set is available or active.
bool Ribbon::shown(std::size_t tab) const noexcept {
    if (tab >= tabs_.size()) return false;
    if ((tabs_[tab].modes & application_modes.read()) == 0U) return false;
    const stxlib::Optional<std::size_t> set = tab_set_of_[tab];
    if (set.absent()) return true;
    const bool result = set_states_[*set.value()].availability != RibbonTabSetAvailability::not_available;
    return result;
}

bool Ribbon::selectable(std::size_t tab) const noexcept {
    const bool result = shown(tab) && tabs_[tab].enabled;
    return result;
}

bool Ribbon::tab_shown(std::size_t tab) const {
    if (tab >= tabs_.size()) throw std::out_of_range("Ribbon tab index is past the end");
    return shown(tab);
}

bool Ribbon::group_shown(std::size_t tab, std::size_t group) const noexcept {
    const bool result = (tabs_[tab].groups[group].modes & application_modes.read()) != 0U;
    return result;
}

stxlib::Optional<std::size_t> Ribbon::find_tab_set(std::string_view set_id) const noexcept {
    for (std::size_t set = 0; set < tab_sets_.size(); ++set) {
        if (tab_sets_[set].stable_id == set_id) return stxlib::Optional<std::size_t>::of(set);
    }
    return stxlib::Optional<std::size_t>::none();
}

stxlib::Optional<AccentIndex> Ribbon::tab_accent(std::size_t tab) const noexcept {
    if (tab >= tab_set_of_.size() || tab_set_of_[tab].absent()) return stxlib::Optional<AccentIndex>::none();
    return set_states_[*tab_set_of_[tab].value()].accent;
}

// Where the selection goes when its tab stops showing: the core tab chosen
// last, else the first core tab shown (Home), else the first tab shown.
stxlib::Optional<std::size_t> Ribbon::fallback_tab() const noexcept {
    if (last_core_tab_.present() && selectable(*last_core_tab_.value())) return last_core_tab_;
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        if (tab_set_of_[tab].absent() && selectable(tab)) return stxlib::Optional<std::size_t>::of(tab);
    }
    for (std::size_t tab = 0; tab < tabs_.size(); ++tab) {
        if (selectable(tab)) return stxlib::Optional<std::size_t>::of(tab);
    }
    return stxlib::Optional<std::size_t>::none();
}

// A shown tab's 1-based place among the shown tabs of its own kind (the core
// tabs, or its set's tabs) and how many there are.
Ribbon::TabPosition Ribbon::position_among_shown(std::size_t tab) const noexcept {
    const stxlib::Optional<std::size_t> set = tab_set_of_[tab];
    TabPosition result;
    for (std::size_t other = 0; other < tabs_.size(); ++other) {
        if (tab_set_of_[other] != set || !shown(other)) continue;
        ++result.count;
        if (other <= tab) result.position = result.count;
    }
    return result;
}

// -- availability and modes ------------------------------------------------------

RibbonTabSetAvailability Ribbon::tab_set_availability(std::string_view set_id) const {
    const stxlib::Optional<std::size_t> set = find_tab_set(set_id);
    if (set.absent()) throw std::out_of_range("Ribbon has no tab set with this ID");
    return set_states_[*set.value()].availability;
}

stxlib::Optional<AccentIndex> Ribbon::tab_set_accent(std::string_view set_id) const {
    const stxlib::Optional<std::size_t> set = find_tab_set(set_id);
    if (set.absent()) throw std::out_of_range("Ribbon has no tab set with this ID");
    return set_states_[*set.value()].accent;
}

// The selection rules (owner decision 14, F3-1): becoming active selects the
// set's remembered tab, else its first shown tab; leaving while selected
// hands the selection to fallback_tab. Making a set available changes no
// selection.
void Ribbon::set_tab_set_availability(std::string_view set_id, RibbonTabSetAvailability availability) {
    require_mutable();
    const stxlib::Optional<std::size_t> found = find_tab_set(set_id);
    if (found.absent()) throw std::out_of_range("Ribbon has no tab set with this ID");
    TabSetState& state = set_states_[*found.value()];
    const RibbonTabSetAvailability previous = state.availability;
    if (previous == availability) return;
    state.availability = availability;
    refresh_accents();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    if (availability == RibbonTabSetAvailability::active) {
        stxlib::Optional<std::size_t> target{};
        if (state.remembered_tab.present() && selectable(*state.remembered_tab.value())) target = state.remembered_tab;
        for (std::size_t tab = state.first_tab; target.absent() && tab < state.first_tab + state.tab_count; ++tab) {
            if (selectable(tab)) target.set(tab);
        }
        if (target.present()) select_tab(*target.value(), SelectionCause::rule);
    }
    tabs_shown_changed();
}

void Ribbon::prepare_write(const decltype(application_modes)&, std::uint32_t& incoming) {
    require_mutable();
    if (incoming == 0U) throw std::invalid_argument("Ribbon application modes may not be empty");
}

void Ribbon::written(const decltype(application_modes)&) {
    // Groups that come and go change the measured ladders.
    ++content_generation_;
    close_group_overlay();
    close_plane_popup();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    tabs_shown_changed();
}

// After tabs came or went: a selection whose tab no longer shows gives way to
// fallback_tab; a ribbon without a selection takes one once a tab shows.
void Ribbon::tabs_shown_changed() {
    if (selected_tab_.present() && shown(*selected_tab_.value())) return;
    const stxlib::Optional<std::size_t> fallback = fallback_tab();
    if (fallback.present()) {
        select_tab(*fallback.value(), SelectionCause::rule);
        return;
    }
    if (selected_tab_.absent()) return;
    close_menus();
    close_group_overlay();
    close_plane_popup();
    selected_tab_.reset();
    hot_ = Hit{};
    pressed_command_.reset();
    pressed_launcher_.reset();
    pressed_group_.reset();
    const Stop* const held_stop = stop_.value();
    if (held_stop != nullptr && (*held_stop).kind != StopKind::application) stop_.set(Stop{StopKind::application, 0U, {}});
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// Gives every shown set an accent no other shown set has (UX guidance: a
// newly shown set takes a colour different from those already shown),
// keeping a shown set's accent and, when free, a returning set's previous
// one. Hidden sets give theirs up. With more shown sets than ramp colours the
// indices wrap the ramp.
void Ribbon::refresh_accents() {
    const std::size_t ramp = std::max<std::size_t>(1U, effective_theme().definition().accent_ramp.size());
    std::vector<bool> used(ramp, false);
    for (TabSetState& state : set_states_) {
        const bool visible = state.availability != RibbonTabSetAvailability::not_available;
        if (!visible && state.accent.present()) {
            state.last_accent = state.accent;
            state.accent.reset();
        }
        if (visible && state.accent.present()) used[(*state.accent.value()).value % ramp] = true;
    }
    std::size_t shown_sets = 0U;
    for (const TabSetState& state : set_states_) {
        if (state.accent.present()) ++shown_sets;
    }
    for (TabSetState& state : set_states_) {
        const bool visible = state.availability != RibbonTabSetAvailability::not_available;
        if (!visible || state.accent.present()) continue;
        std::optional<std::size_t> chosen{};
        if (state.last_accent.present() && !used[(*state.last_accent.value()).value % ramp]) {
            chosen = (*state.last_accent.value()).value % ramp;
        }
        for (std::size_t index = 0; !chosen && index < ramp; ++index) {
            if (!used[index]) chosen = index;
        }
        if (!chosen) chosen = shown_sets % ramp;
        state.accent.set(AccentIndex{static_cast<std::uint8_t>(*chosen)});
        used[*chosen] = true;
        ++shown_sets;
    }
}

} // namespace gui_forms
