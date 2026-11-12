#pragma once

// A data provider a C caller describes (bindings.set_provider, since 1.2,
// audit N31): the BindingProvider over a gf_binding_provider_callbacks, and
// the ids bindings.notify reaches a provider's link by. See gf_abi.h.

#include "gui_forms/c/gf_abi.h"

#include "../failure/abi_failure.hpp"

#include "gui_forms/binding/binding_provider/binding_provider.hpp"
#include "gui_forms/binding/binding_schema/binding_schema.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gui_forms::abi1 {

// A C provider. It keeps copies of the caller's columns and callbacks and
// never a pointer the caller lends: every answer (a value, its text, an id,
// a reason) is copied before the next callback, as the lending rule allows.
// A callback that fails, or an answer that does not convert, is no answer;
// the source turns it into a cell error. Used only on the source's UI
// thread; release is called once, from the destructor, when the source lets
// the provider go.
class AbiBindingProvider final : public BindingProvider {
public:
    AbiBindingProvider(BindingSchema schema, const gf_binding_provider_callbacks& callbacks, std::uint64_t id);
    ~AbiBindingProvider() override;
    AbiBindingProvider(const AbiBindingProvider&) = delete;
    AbiBindingProvider& operator=(const AbiBindingProvider&) = delete;

    [[nodiscard]] BindingSchema schema() const override;
    [[nodiscard]] BindingProviderCapabilities capabilities() const override;
    [[nodiscard]] std::size_t row_count() override;
    bool read_cell(std::size_t row, std::size_t column, BindingValue& value) override;
    bool read_stable_id(std::size_t row, std::string& stable_id) override;
    BindingWriteResult write_cell(std::size_t row, std::size_t column, const BindingValue& value) override;
    [[nodiscard]] bool cell_read_only(std::size_t row, std::size_t column) override;
    bool read_error(std::size_t row, std::size_t column, std::string& text) override;
    BindingWriteResult insert_row(std::size_t row, const BindingRow& values) override;
    BindingWriteResult remove_rows(std::size_t row, std::size_t count) override;
    bool apply_sort(std::span<const BindingSortKey> keys) override;
    bool apply_filter(const BindingFilter& filter) override;
    void attach(const BindingProviderLink& link) override;
    void detach() noexcept override;

private:
    BindingSchema schema_;
    gf_binding_provider_callbacks callbacks_{};
    std::uint64_t id_{};
};

// The id the next provider is given; never zero, never reused.
[[nodiscard]] std::uint64_t next_binding_provider_id() noexcept;

// Reports notification to the provider with id: its link, found under the
// registry's lock and used outside it (the link is thread-safe).
// GF_STATUS_REMOVED for an id no source holds.
[[nodiscard]] gf_status notify_binding_provider(std::uint64_t id, const BindingNotification& notification,
                                                SlotIdentity slot);

// A caller's columns as a schema; fails with slot's status for a short
// record, a kind past the list, a read_only other than 0 or 1, an empty or
// repeated name, or more than GF_BINDING_MAXIMUM_FIELDS columns.
[[nodiscard]] gf_status read_binding_columns(const gf_binding_column* columns, std::uint64_t count,
                                             SlotIdentity slot, BindingSchema& schema);

// Checks a caller's callbacks: the size, the required callbacks and those
// its flags require, and zero reserved bits.
[[nodiscard]] gf_status check_binding_provider_callbacks(const gf_binding_provider_callbacks& callbacks,
                                                         SlotIdentity slot);

// A caller's sort keys and filter predicates (the array rule: the first
// record's struct_size is the stride).
[[nodiscard]] gf_status read_binding_sort_keys(const gf_binding_sort_key* keys, std::uint64_t count,
                                               SlotIdentity slot, std::vector<BindingSortKey>& converted);
[[nodiscard]] gf_status read_binding_predicates(const gf_binding_predicate* predicates, std::uint64_t count,
                                                SlotIdentity slot, BindingFilter& converted);

} // namespace gui_forms::abi1
