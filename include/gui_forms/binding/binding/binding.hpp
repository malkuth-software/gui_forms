#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/types/binding_contract_types.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/event.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace gui_forms {

class BindingSource;
class Control;
struct ControlValidationEvent;

class GUI_FORMS_API Binding final : public Component,
                      public std::enable_shared_from_this<Binding> {
public:
    Binding(Control& target, std::string property_name,
            std::shared_ptr<BindingSource> source, std::string data_member,
            BindingOptions options = {});
    ~Binding() override;

    [[nodiscard]] Control* target() const noexcept { return target_; }
    [[nodiscard]] std::shared_ptr<BindingSource> source() const noexcept {
        return source_.lock();
    }
    [[nodiscard]] const std::string& property_name() const& noexcept {
        return property_name_;
    }
    const std::string& property_name() const&& = delete;
    [[nodiscard]] const std::string& data_member() const& noexcept {
        return data_member_;
    }
    const std::string& data_member() const&& = delete;
    [[nodiscard]] const BindingOptions& options() const noexcept {
        return options_;
    }
    void set_options(BindingOptions options);
    [[nodiscard]] bool active() const noexcept { return active_; }
    bool read_value();
    bool write_value();
    bool validate();
    [[nodiscard]] BindingSnapshot snapshot() const;

    [[nodiscard]] Event<BindingConvertEvent&>& format() noexcept { return format_; }
    [[nodiscard]] Event<BindingConvertEvent&>& parse() noexcept { return parse_; }
    [[nodiscard]] Event<BindingCompleteEvent&>& binding_complete() noexcept {
        return binding_complete_;
    }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    friend class ControlBindingsCollection;
    void start();
    bool update_control(bool automatic);
    bool update_source(bool automatic);
    bool complete(BindingCompleteContext context,
                  BindingCompleteState state, std::string error = {});
    void source_changed(const BindingListChange& change);
    void target_changed();

    struct SourceChangedCallback final {
        std::weak_ptr<Binding> binding;
        void operator()(const BindingListChange& change) const;
    };

    struct SourceDisposedCallback final {
        std::weak_ptr<Binding> binding;
        void operator()() const;
    };

    struct TargetChangedCallback final {
        std::weak_ptr<Binding> binding;
        void operator()() const;
    };

    struct TargetValidatingCallback final {
        std::weak_ptr<Binding> binding;
        void operator()(ControlValidationEvent& event) const;
    };

    Control* target_{};
    std::weak_ptr<BindingSource> source_;
    std::string property_name_;
    std::string data_member_;
    BindingOptions options_;
    SubscriptionToken source_changed_;
    SubscriptionToken source_disposed_;
    SubscriptionToken target_changed_;
    SubscriptionToken target_validating_;
    Event<BindingConvertEvent&> format_;
    Event<BindingConvertEvent&> parse_;
    Event<BindingCompleteEvent&> binding_complete_;
    std::uint64_t control_reads_{};
    std::uint64_t source_writes_{};
    std::uint64_t successful_updates_{};
    std::uint64_t failed_updates_{};
    std::uint64_t suppressed_reentrant_updates_{};
    bool active_{};
    bool updating_{};
};

} // namespace gui_forms
