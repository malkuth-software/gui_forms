#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/event.hpp"

#include <cstddef>
#include <memory>
#include <unordered_map>

namespace gui_forms {

class Window;
namespace detail {
struct WindowLifetime;
}

class GUI_FORMS_API BindingContext final : public Component {
public:
    explicit BindingContext(Window& window);
    ~BindingContext() override;
    void add(const std::shared_ptr<BindingSource>& source);
    CurrencyManager& manager(const std::shared_ptr<BindingSource>& source);
    [[nodiscard]] bool contains(const BindingSource& source) const noexcept;
    bool remove(const BindingSource& source);
    void clear();
    [[nodiscard]] std::size_t size() const noexcept { return sources_.size(); }
    [[nodiscard]] Event<const BindingContextChange&>& collection_changed() noexcept {
        return collection_changed_;
    }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    struct SourceEntry final {
        std::weak_ptr<BindingSource> source;
        SubscriptionToken disposed;
    };
    struct SourceDisposedCallback final {
        BindingContext* context{};
        BindingSource* source{};

        void operator()() const;
    };
    using SourceMap = std::unordered_map<BindingSource*, SourceEntry>;

    [[nodiscard]] Window* bound_window() const noexcept;
    bool remove_entry(BindingSource* source, bool publish);
    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    SourceMap sources_;
    Event<const BindingContextChange&> collection_changed_;
};

} // namespace gui_forms
