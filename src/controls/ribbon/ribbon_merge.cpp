// Ribbon merging (Ribbon::merge, revert_merge, merge_source): an MDI child's
// ribbon moves its tabs into the main window's ribbon while the child is
// active, and takes them back after.

#include "gui_forms/controls/ribbon.hpp"

#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <stxlib/result.hpp>
#include <stxlib/unique.hpp>

namespace gui_forms {
namespace {

// A tab's text as merging compares it: mnemonic marks dropped ("&&" keeps
// one ampersand) and ASCII letters lowered.
std::string merge_key(std::string_view text) {
    std::string key;
    key.reserve(text.size());
    for (std::size_t at = 0; at < text.size(); ++at) {
        char letter = text[at];
        if (letter == '&') {
            if (at + 1U < text.size() && text[at + 1U] == '&') {
                key += '&';
                ++at;
            }
            continue;
        }
        if (letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter - 'A' + 'a');
        key += letter;
    }
    return key;
}

bool is_core(const RibbonTabSpec& tab) noexcept {
    return tab.tab_set.empty();
}

bool is_panel(const RibbonTabSpec& tab) noexcept {
    return tab.panel != nullptr;
}

// The merged definition: this ribbon's core tabs, with matched source tabs'
// groups added; the unmatched source core tabs; this ribbon's contextual
// tabs; the source's contextual tabs.
std::vector<RibbonTabSpec> merged_tabs(const std::vector<RibbonTabSpec>& own,
                                       const std::vector<RibbonTabSpec>& source) {
    std::vector<RibbonTabSpec> merged;
    merged.reserve(own.size() + source.size());
    for (const RibbonTabSpec& tab : own) {
        if (is_core(tab)) merged.push_back(tab);
    }
    const std::size_t own_core = merged.size();
    for (const RibbonTabSpec& tab : source) {
        if (!is_core(tab)) continue;
        bool joined = false;
        if (!is_panel(tab)) {
            const std::string key = merge_key(tab.text);
            for (std::size_t at = 0; at < own_core && !joined; ++at) {
                RibbonTabSpec& target = merged[at];
                if (is_panel(target) || merge_key(target.text) != key) continue;
                target.groups.insert(target.groups.end(), tab.groups.begin(), tab.groups.end());
                target.reduction_order.insert(target.reduction_order.end(), tab.reduction_order.begin(),
                                              tab.reduction_order.end());
                joined = true;
            }
        }
        if (!joined) merged.push_back(tab);
    }
    for (const RibbonTabSpec& tab : own) {
        if (!is_core(tab)) merged.push_back(tab);
    }
    for (const RibbonTabSpec& tab : source) {
        if (!is_core(tab)) merged.push_back(tab);
    }
    return merged;
}

} // namespace

// The merge this ribbon holds as a target: its own definition, and the
// source's definition and sets' availability to give back.
struct Ribbon::MergeState final {
    std::weak_ptr<Ribbon> source{};
    std::vector<RibbonTabSpec> own_tabs{};
    std::vector<RibbonTabSetSpec> own_sets{};
    std::vector<RibbonTabSpec> source_tabs{};
    std::vector<RibbonTabSetSpec> source_sets{};
    std::vector<RibbonTabSetAvailability> source_availability{};
};

bool Ribbon::merge(Ribbon& source) {
    require_mutable();
    if (&source == this) throw std::invalid_argument("Ribbon cannot merge into itself");
    const std::shared_ptr<Ribbon> held = std::static_pointer_cast<Ribbon>(source.shared_from_this());
    if (merge_) static_cast<void>(revert_merge());
    if (source.tabs_.empty() || source.merge_) return false;
    stxlib::OperationResult<stxlib::Unique<MergeState>> made = stxlib::Unique<MergeState>::make();
    if (made.failed()) throw std::bad_alloc();
    stxlib::Unique<MergeState> state = std::move(made).take_value();
    (*state).source = held;
    (*state).own_tabs = tabs_;
    (*state).own_sets = tab_sets_;
    (*state).source_tabs = source.tabs_;
    (*state).source_sets = source.tab_sets_;
    for (const RibbonTabSetSpec& set : source.tab_sets_) {
        (*state).source_availability.push_back(source.tab_set_availability(set.stable_id));
    }
    std::vector<RibbonTabSpec> tabs = merged_tabs((*state).own_tabs, (*state).source_tabs);
    std::vector<RibbonTabSetSpec> sets = (*state).own_sets;
    sets.insert(sets.end(), (*state).source_sets.begin(), (*state).source_sets.end());
    // The source lets go of its hosted controls (galleries, panels) first,
    // so this ribbon can take them; a refused merge gives them back.
    source.set_tabs({}, {});
    merging_ = true;
    try {
        set_tabs(std::move(tabs), std::move(sets));
    } catch (const std::invalid_argument&) {
        merging_ = false;
        source.set_tabs((*state).source_tabs, (*state).source_sets);
        for (std::size_t set = 0; set < (*state).source_sets.size(); ++set) {
            source.set_tab_set_availability((*state).source_sets[set].stable_id, (*state).source_availability[set]);
        }
        return false;
    } catch (...) {
        merging_ = false;
        throw;
    }
    merging_ = false;
    for (std::size_t set = 0; set < (*state).source_sets.size(); ++set) {
        set_tab_set_availability((*state).source_sets[set].stable_id, (*state).source_availability[set]);
    }
    merge_ = std::move(state);
    return true;
}

bool Ribbon::revert_merge() {
    require_mutable();
    if (!merge_) return false;
    stxlib::Unique<MergeState> state = std::move(merge_);
    std::vector<RibbonTabSetAvailability> availability;
    for (const RibbonTabSetSpec& set : (*state).source_sets) {
        availability.push_back(tab_set_availability(set.stable_id));
    }
    merging_ = true;
    try {
        set_tabs(std::move((*state).own_tabs), std::move((*state).own_sets));
    } catch (...) {
        merging_ = false;
        throw;
    }
    merging_ = false;
    const std::shared_ptr<Ribbon> source = (*state).source.lock();
    if (!source) return true;
    (*source).set_tabs(std::move((*state).source_tabs), std::move((*state).source_sets));
    for (std::size_t set = 0; set < (*source).tab_sets_.size() && set < availability.size(); ++set) {
        (*source).set_tab_set_availability((*source).tab_sets_[set].stable_id, availability[set]);
    }
    return true;
}

std::shared_ptr<Ribbon> Ribbon::merge_source() const {
    if (!merge_) return nullptr;
    std::shared_ptr<Ribbon> source = (*merge_).source.lock();
    return source;
}

} // namespace gui_forms
