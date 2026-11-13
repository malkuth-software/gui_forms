#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/binding/binding/binding.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class BindingSource;
class Control;

class GUI_FORMS_API ControlBindingsCollection final {
public:
    explicit ControlBindingsCollection(Control& target) : target_(&target) {}
    ~ControlBindingsCollection();
    ControlBindingsCollection(const ControlBindingsCollection&) = delete;
    ControlBindingsCollection& operator=(const ControlBindingsCollection&) = delete;

    std::shared_ptr<Binding> add(
        std::string property_name, std::shared_ptr<BindingSource> source,
        std::string data_member);
    std::shared_ptr<Binding> add(
        std::string property_name, std::shared_ptr<BindingSource> source,
        std::string data_member, BindingOptions options);
    void add(std::shared_ptr<Binding> binding);
    bool remove(const Binding& binding);
    void clear() noexcept;
    [[nodiscard]] std::shared_ptr<Binding> find(
        std::string_view property_name) const;
    // Borrowed: add, remove and clear end the views lent so far (the
    // sanitizer tree reports a later use).
    [[nodiscard]] BorrowedSpan<std::shared_ptr<Binding>> items() const& noexcept { return detail::lend_view(bindings_); }
    BorrowedSpan<std::shared_ptr<Binding>> items() const&& = delete;
    [[nodiscard]] std::size_t size() const noexcept { return bindings_.size(); }
    [[nodiscard]] bool empty() const noexcept { return bindings_.empty(); }
    [[nodiscard]] DataSourceUpdateMode
    default_data_source_update_mode() const noexcept {
        return default_update_mode_;
    }
    void set_default_data_source_update_mode(
        DataSourceUpdateMode mode) noexcept {
        default_update_mode_ = mode;
    }

private:
    // The empty collection a control without bindings reads through its
    // const data_bindings() (audit A16): no target, never added to.
    friend class Control;
    ControlBindingsCollection() noexcept {}

    using BindingList = detail::LendingVector<std::shared_ptr<Binding>>;
    Control* target_{};
    BindingList bindings_;
    DataSourceUpdateMode default_update_mode_{
        DataSourceUpdateMode::on_validation};
};

} // namespace gui_forms
