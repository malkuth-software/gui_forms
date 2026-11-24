#include "gui_forms/binding.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

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

ControlBindingsCollection::~ControlBindingsCollection() { clear(); }

std::shared_ptr<Binding> ControlBindingsCollection::add(
    std::string property_name, std::shared_ptr<BindingSource> source,
    std::string data_member) {
    BindingOptions options;
    options.data_source_update_mode = default_update_mode_;
    return add(std::move(property_name), std::move(source),
               std::move(data_member), std::move(options));
}

std::shared_ptr<Binding> ControlBindingsCollection::add(
    std::string property_name, std::shared_ptr<BindingSource> source,
    std::string data_member, BindingOptions options) {
    std::shared_ptr<gui_forms::Binding> binding = gui_forms::detail::make_library_shared<Binding>(
        *target_, std::move(property_name), std::move(source),
        std::move(data_member), std::move(options));
    add(binding);
    return binding;
}

void ControlBindingsCollection::add(std::shared_ptr<Binding> binding) {
    if (!target_ || !(*target_).is_alive()) {
        throw std::logic_error("GUI.Forms cannot bind a disposed Control");
    }
    if (!binding || (*binding).target() != target_) {
        throw std::invalid_argument(
            "GUI.Forms ControlBindingsCollection requires its own target");
    }
    if (find((*binding).property_name())) {
        throw std::invalid_argument(
            "GUI.Forms Control already has a binding for property " +
            (*binding).property_name());
    }
    (*binding).start();
    bindings_.push_back(std::move(binding));
}

bool ControlBindingsCollection::remove(const Binding& binding) {
    BindingList::iterator found = bindings_.begin();
    while (found != bindings_.end() && (*found).get() != &binding) {
        ++found;
    }
    if (found == bindings_.end()) return false;
    const std::shared_ptr<Binding> removed = std::move(*found);
    bindings_.erase(found);
    if (removed && (*removed).is_alive()) (*removed).dispose();
    return true;
}

void ControlBindingsCollection::clear() noexcept {
    BindingList bindings = std::exchange(bindings_, {});
    for (const std::shared_ptr<gui_forms::Binding>& binding : bindings) {
        if (binding && (*binding).is_alive()) {
            try { (*binding).dispose(); } catch (...) {}
        }
    }
}

std::shared_ptr<Binding> ControlBindingsCollection::find(
    std::string_view property_name) const {
    const std::string canonical = registered_name(property_name);
    BindingList::const_iterator found = bindings_.begin();
    while (found != bindings_.end() &&
           (!*found || (*(*found)).property_name() != canonical)) {
        ++found;
    }
    return found == bindings_.end() ? std::shared_ptr<Binding>{} : *found;
}

} // namespace gui_forms
