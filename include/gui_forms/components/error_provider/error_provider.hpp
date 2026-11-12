#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/components/error_provider/error_provider_values/error_provider_values.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/types.hpp"
#include "gui_forms/components/error_provider/error_provider_capabilities/error_provider_capabilities.hpp"

#include <any>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gui_forms {

class Control;
class Binding;
class BindingSource;
struct BindingCompleteEvent;
struct BindingListChange;
struct ControlAvailabilityChange;
struct PresentationSettings;
class Window;

namespace detail {
struct WindowLifetime;
}

enum class ErrorIconAlignment : std::uint8_t {
    top_left,
    top_right,
    middle_left,
    middle_right,
    bottom_left,
    bottom_right,
};

struct GUI_FORMS_API ErrorProviderChange final {
    std::string target_stable_id;
    // The target's new message; empty when its error was cleared.
    std::string error;
};

struct GUI_FORMS_API ErrorIconSnapshot final {
    std::string target_stable_id;
    std::string error;
    Rect bounds{};
    bool target_available{};
    bool presented{};
    bool blink_active{};
    bool blink_phase_visible{true};
};

struct GUI_FORMS_API ErrorProviderSnapshot final {
    std::vector<ErrorIconSnapshot> icons;
    std::size_t live_errors{};
    std::size_t presented_icons{};
};

// Retained validation adornment provider. Error text is projected onto the
// target semantic node while each visible target owns an independent overlay
// glyph. Blink work uses the Window frame scheduler, so occlusion and reduced
// motion suppress wakeups rather than running a provider-local loop.
class GUI_FORMS_API ErrorProvider final : public Component, public ErrorProviderCapabilities<ErrorProvider> {
public:
    explicit ErrorProvider(Window& window);
    ~ErrorProvider() override;

    [[nodiscard]] bool can_extend(const std::shared_ptr<Control>& target) const;
    void set_error(const std::shared_ptr<Control>& target, std::string error);
    [[nodiscard]] std::string error(const Control& target) const;
    void clear();
    [[nodiscard]] bool has_errors() const noexcept;

    void set_icon_alignment(const std::shared_ptr<Control>& target,
                            ErrorIconAlignment alignment);
    [[nodiscard]] ErrorIconAlignment icon_alignment(const Control& target) const;
    void set_icon_padding(const std::shared_ptr<Control>& target, double padding);
    [[nodiscard]] double icon_padding(const Control& target) const;

    [[nodiscard]] Event<bool>& right_to_left_changed() noexcept {
        return right_to_left_changed_;
    }

    [[nodiscard]] std::shared_ptr<Control> container_control() const noexcept;

    void bind_to_data_and_errors(std::shared_ptr<BindingSource> source,
                                 std::string member = {});
    void update_binding();
    [[nodiscard]] Event<>& data_source_changed() noexcept {
        return data_source_changed_;
    }
    [[nodiscard]] Event<const std::string&>& data_member_changed() noexcept {
        return data_member_changed_;
    }

    [[nodiscard]] Event<const ErrorProviderChange&>& error_changed() noexcept {
        return error_changed_;
    }
    [[nodiscard]] ErrorProviderSnapshot snapshot() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    struct Entry;
    struct TargetBoundsObserver;
    using EntryMap =
        std::unordered_map<std::uint64_t, std::unique_ptr<Entry>>;
    using BindingErrorMap = std::unordered_map<
        const Binding*, std::pair<std::weak_ptr<Control>, std::string>>;

    [[nodiscard]] Window* bound_window() const noexcept;
    void require_access(std::string_view operation) const;
    [[nodiscard]] Entry* find_entry(const Control& target);
    [[nodiscard]] const Entry* find_entry(const Control& target) const;
    [[nodiscard]] Entry& require_entry(const std::shared_ptr<Control>& target);
    void refresh_visual(Entry& entry, bool error_changed);
    void close_visual(Entry& entry) noexcept;
    void refresh_all_visuals(bool restart_blink);
    void position_visual(Entry& entry);
    [[nodiscard]] Rect icon_bounds(const Entry& entry) const noexcept;
    void erase_if_empty(std::uint64_t runtime_id);
    void control_availability_changed(const ControlAvailabilityChange&);
    void presentation_settings_changed(const PresentationSettings&);
    void root_bounds_changed(Rect);
    void source_list_changed(const BindingListChange&);
    void source_current_changed();
    void source_disposed() noexcept;
    static void append_unique_error(
        std::vector<std::string>& values, std::string value);
    void clear_bound_errors() noexcept;
    void binding_completed(BindingCompleteEvent& event);

    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    EntryMap entries_;
    SubscriptionToken availability_subscription_;
    SubscriptionToken presentation_subscription_;
    SubscriptionToken root_bounds_subscription_;
    SubscriptionToken source_list_subscription_;
    SubscriptionToken source_current_subscription_;
    SubscriptionToken source_completion_subscription_;
    SubscriptionToken source_disposed_subscription_;
    Event<const ErrorProviderChange&> error_changed_;
    Event<bool> right_to_left_changed_;
    Event<> data_source_changed_;
    Event<const std::string&> data_member_changed_;
    std::weak_ptr<BindingSource> data_source_;
    std::unordered_map<std::uint64_t, std::weak_ptr<Control>> bound_targets_;
    BindingErrorMap binding_errors_;
    std::any tag_;
    std::uint64_t provider_id_{};
    // property_codemod begin ErrorProvider (tools/property_families/error_provider.json; generated, do not edit)
private:
    friend struct ErrorProviderIconSizeCapability<ErrorProvider>;
    friend struct ErrorProviderBlinkRateCapability<ErrorProvider>;
    friend struct ErrorProviderBlinkStyleCapability<ErrorProvider>;
    friend struct ErrorProviderRightToLeftCapability<ErrorProvider>;
    friend struct ErrorProviderIconCapability<ErrorProvider>;
    friend struct ErrorProviderDataSourceCapability<ErrorProvider>;
    friend struct ErrorProviderDataMemberCapability<ErrorProvider>;
    friend struct ErrorProviderTagCapability<ErrorProvider>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(icon_size)& cell, double& incoming);
    void written(const decltype(icon_size)& cell);
    void prepare_write(const decltype(blink_rate)& cell, std::chrono::milliseconds& incoming);
    void written(const decltype(blink_rate)& cell);
    void prepare_write(const decltype(blink_style)& cell, ErrorBlinkStyle& incoming);
    void written(const decltype(blink_style)& cell);
    void prepare_write(const decltype(right_to_left)& cell, bool& incoming);
    void written(const decltype(right_to_left)& cell);
    void prepare_write(const decltype(icon)& cell, std::optional<ImageId>& incoming);
    void written(const decltype(icon)& cell);
    void write_view(const decltype(data_source)& cell, std::shared_ptr<BindingSource> incoming);
    [[nodiscard]] std::shared_ptr<BindingSource> read_view(const decltype(data_source)& cell) const;
    void prepare_write(const decltype(data_member)& cell, std::string& incoming);
    void written(const decltype(data_member)& cell);
    void write_view(const decltype(tag)& cell, std::any incoming);
    [[nodiscard]] std::any read_view(const decltype(tag)& cell) const;
    // property_codemod end ErrorProvider
};

} // namespace gui_forms
