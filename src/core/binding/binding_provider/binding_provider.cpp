// The optional parts of a binding provider. See binding_provider.hpp; the
// link is implemented with the source, in binding_source.cpp.

#include "gui_forms/binding/binding_provider/binding_provider.hpp"

namespace gui_forms {

BindingProvider::~BindingProvider() = default;

BindingProviderCapabilities BindingProvider::capabilities() const {
    const BindingProviderCapabilities none{};
    return none;
}

bool BindingProvider::cell_read_only(std::size_t row, std::size_t column) {
    static_cast<void>(row);
    static_cast<void>(column);
    return false;
}

bool BindingProvider::read_error(std::size_t row, std::size_t column, std::string& text) {
    static_cast<void>(row);
    static_cast<void>(column);
    static_cast<void>(text);
    return false;
}

BindingWriteResult BindingProvider::insert_row(std::size_t row, const BindingRow& values) {
    static_cast<void>(row);
    static_cast<void>(values);
    BindingWriteResult refused{BindingWriteStatus::refused, "the provider does not insert rows"};
    return refused;
}

BindingWriteResult BindingProvider::remove_rows(std::size_t row, std::size_t count) {
    static_cast<void>(row);
    static_cast<void>(count);
    BindingWriteResult refused{BindingWriteStatus::refused, "the provider does not remove rows"};
    return refused;
}

bool BindingProvider::apply_sort(std::span<const BindingSortKey> keys) {
    static_cast<void>(keys);
    return false;
}

bool BindingProvider::apply_filter(const BindingFilter& filter) {
    static_cast<void>(filter);
    return false;
}

void BindingProvider::attach(const BindingProviderLink& link) {
    static_cast<void>(link);
}

void BindingProvider::detach() noexcept {}

} // namespace gui_forms
