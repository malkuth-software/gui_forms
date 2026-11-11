#include "gui_forms/binding.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"

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

std::size_t CurrencyManager::count() const noexcept {
    return source_ == nullptr ? 0U : (*source_).count();
}

std::string CurrencyManager::current_stable_id() const {
    return source_ == nullptr ? std::string{} : (*source_).current_stable_id();
}

std::ptrdiff_t CurrencyManager::position() const noexcept {
    return source_ == nullptr ? -1 : (*source_).position();
}

bool CurrencyManager::binding_suspended() const noexcept {
    return source_ != nullptr && (*source_).binding_suspended();
}

bool CurrencyManager::set_position(std::ptrdiff_t position_value) {
    return source_ != nullptr && (*source_).set_position(position_value);
}

void CurrencyManager::cancel_current_edit() {
    if (source_ != nullptr) (*source_).cancel_edit();
}

void CurrencyManager::end_current_edit() {
    if (source_ != nullptr) (*source_).end_edit();
}

bool CurrencyManager::remove_at(std::size_t index) {
    return source_ != nullptr && (*source_).remove_at(index);
}

void CurrencyManager::suspend_binding() {
    if (source_ != nullptr) (*source_).suspend_binding();
}

void CurrencyManager::resume_binding() {
    if (source_ != nullptr) (*source_).resume_binding();
}

bool CurrencyManager::pull_data() {
    return source_ != nullptr && (*source_).transfer_bindings(false);
}

bool CurrencyManager::push_data() {
    return source_ != nullptr && (*source_).transfer_bindings(true);
}

Event<BindingCompleteEvent&>& CurrencyManager::binding_complete() noexcept {
    return (*source_).binding_complete();
}

Event<>& CurrencyManager::current_changed() noexcept {
    return (*source_).current_changed();
}

Event<>& CurrencyManager::current_item_changed() noexcept {
    return (*source_).current_item_changed();
}

Event<std::ptrdiff_t>& CurrencyManager::position_changed() noexcept {
    return (*source_).position_changed();
}

Event<const std::string&>& CurrencyManager::data_error() noexcept {
    return (*source_).data_error();
}


Event<const BindingListChange&>& CurrencyManager::list_changed() noexcept {
    return (*source_).list_changed();
}

void CurrencyManager::refresh() {
    if (source_ != nullptr) (*source_).reset_bindings(false);
}

BindingSource& CurrencyManager::source() const noexcept { return *source_; }

} // namespace gui_forms
