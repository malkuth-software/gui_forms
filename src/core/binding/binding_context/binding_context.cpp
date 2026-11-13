#include "gui_forms/binding.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "../../surface_root/access/surface_lifetime_access.hpp"

#include <array>
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms {

BindingContext::BindingContext(Window& window)
    : window_lifetime_(detail::SurfaceLifetimeAccess::observe(window)) {
    window.verify_access("BindingContext construction");
}

BindingContext::~BindingContext() {
    if (is_alive()) {
        try { dispose(); } catch (...) {}
    }
}

void BindingContext::SourceDisposedCallback::operator()() const {
    if (context != nullptr) {
        static_cast<void>((*context).remove_entry(source, true));
    }
}

void BindingContext::add(const std::shared_ptr<BindingSource>& source) {
    static_cast<void>(manager(source));
}

Window* BindingContext::bound_window() const noexcept {
    const std::shared_ptr<gui_forms::detail::WindowLifetime> lifetime = window_lifetime_.lock();
    return lifetime ? (*lifetime).window : nullptr;
}

CurrencyManager& BindingContext::manager(
    const std::shared_ptr<BindingSource>& source) {
    Window* owner = bound_window();
    if (!is_alive() || !owner) {
        throw std::logic_error("GUI.Forms cannot use a retired BindingContext");
    }
    (*owner).verify_access("BindingContext lookup");
    if (!source || !(*source).is_alive() || (*source).bound_window() != owner) {
        throw std::invalid_argument(
            "GUI.Forms BindingContext source must belong to the same Window");
    }
    SourceMap::iterator expired = sources_.begin();
    while (expired != sources_.end()) {
        if ((*expired).second.source.expired()) {
            expired = sources_.erase(expired);
        } else {
            ++expired;
        }
    }
    if (!sources_.contains(source.get())) {
        BindingSource* key = source.get();
        SourceEntry entry;
        entry.source = source;
        entry.disposed = (*source).disposed_event().subscribe(
            *this, SourceDisposedCallback{this, key});
        sources_.emplace(key, std::move(entry));
        BindingContextChange change{source.get(), true};
        collection_changed_.emit(change);
    }
    return (*source).currency_manager();
}

bool BindingContext::contains(const BindingSource& source) const noexcept {
    const SourceMap::const_iterator found =
        sources_.find(const_cast<BindingSource*>(&source));
    return found != sources_.end() && !(*found).second.source.expired();
}

bool BindingContext::remove(const BindingSource& source) {
    if (Window* owner = bound_window()) (*owner).verify_access("BindingContext removal");
    return remove_entry(const_cast<BindingSource*>(&source), true);
}

bool BindingContext::remove_entry(BindingSource* source, bool publish) {
    const SourceMap::iterator found = sources_.find(source);
    if (found == sources_.end()) return false;
    SourceEntry removed = std::move((*found).second);
    sources_.erase(found);
    removed.disposed.disconnect();
    if (publish) {
        BindingContextChange change{source, false};
        collection_changed_.emit(change);
    }
    return true;
}

void BindingContext::clear() {
    if (Window* owner = bound_window()) (*owner).verify_access("BindingContext clear");
    std::vector<BindingSource*> removed;
    removed.reserve(sources_.size());
    for (const std::pair<BindingSource* const, SourceEntry>& source_entry :
         sources_) {
        BindingSource* const source = source_entry.first;
        const SourceEntry& entry = source_entry.second;
        if (!entry.source.expired()) removed.push_back(source);
    }
    sources_.clear();
    for (BindingSource* source : removed) {
        BindingContextChange change{source, false};
        collection_changed_.emit(change);
    }
}

void BindingContext::verify_dispose_thread() {
    if (Window* owner = bound_window()) (*owner).verify_access("BindingContext disposal");
}

void BindingContext::on_dispose() noexcept {
    sources_.clear();
    collection_changed_.disconnect_all();
    window_lifetime_.reset();
}

namespace {

// The events BindingContext adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 1> binding_context_interactions{{
    {.name = "collection_changed", .payload = "BindingContextChange", .mutable_payload = false},
}};

} // namespace

void BindingContext::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), binding_context_interactions.begin(), binding_context_interactions.end());
}

} // namespace gui_forms
