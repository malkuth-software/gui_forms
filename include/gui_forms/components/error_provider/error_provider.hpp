#pragma once

#include "gui_forms/component.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/types.hpp"

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

enum class ErrorBlinkStyle : std::uint8_t {
    blink_if_different_error,
    always_blink,
    never_blink,
};

enum class ErrorIconAlignment : std::uint8_t {
    top_left,
    top_right,
    middle_left,
    middle_right,
    bottom_left,
    bottom_right,
};

struct ErrorProviderChange final {
    std::string target_stable_id;
    // The target's new message; empty when its error was cleared.
    std::string error;
};

struct ErrorIconSnapshot final {
    std::string target_stable_id;
    std::string error;
    Rect bounds{};
    bool target_available{};
    bool presented{};
    bool blink_active{};
    bool blink_phase_visible{true};
};

struct ErrorProviderSnapshot final {
    std::vector<ErrorIconSnapshot> icons;
    std::size_t live_errors{};
    std::size_t presented_icons{};
};

// Retained validation adornment provider. Error text is projected onto the
// target semantic node while each visible target owns an independent overlay
// glyph. Blink work uses the Window frame scheduler, so occlusion and reduced
// motion suppress wakeups rather than running a provider-local loop.
class ErrorProvider final : public Component {
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
    [[nodiscard]] double icon_size() const noexcept { return icon_size_; }
    void set_icon_size(double size);

    [[nodiscard]] std::chrono::milliseconds blink_rate() const noexcept {
        return blink_rate_;
    }
    void set_blink_rate(std::chrono::milliseconds rate);
    [[nodiscard]] ErrorBlinkStyle blink_style() const noexcept {
        return blink_style_;
    }
    void set_blink_style(ErrorBlinkStyle style);

    [[nodiscard]] bool right_to_left() const noexcept { return right_to_left_; }
    void set_right_to_left(bool value);
    [[nodiscard]] Event<bool>& right_to_left_changed() noexcept {
        return right_to_left_changed_;
    }

    [[nodiscard]] std::optional<ImageId> icon() const noexcept { return icon_; }
    void set_icon(std::optional<ImageId> icon);
    [[nodiscard]] std::shared_ptr<Control> container_control() const noexcept;

    [[nodiscard]] std::shared_ptr<BindingSource> data_source() const noexcept {
        return data_source_.lock();
    }
    void set_data_source(std::shared_ptr<BindingSource> source);
    [[nodiscard]] const std::string& data_member() const noexcept {
        return data_member_;
    }
    void set_data_member(std::string member);
    void bind_to_data_and_errors(std::shared_ptr<BindingSource> source,
                                 std::string member = {});
    void update_binding();
    [[nodiscard]] Event<>& data_source_changed() noexcept {
        return data_source_changed_;
    }
    [[nodiscard]] Event<const std::string&>& data_member_changed() noexcept {
        return data_member_changed_;
    }

    [[nodiscard]] const std::any& tag() const noexcept { return tag_; }
    void set_tag(std::any tag);
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
    std::string data_member_;
    std::unordered_map<std::uint64_t, std::weak_ptr<Control>> bound_targets_;
    BindingErrorMap binding_errors_;
    std::any tag_;
    std::chrono::milliseconds blink_rate_{250};
    ErrorBlinkStyle blink_style_{ErrorBlinkStyle::blink_if_different_error};
    std::optional<ImageId> icon_;
    double icon_size_{16.0};
    bool right_to_left_{};
    std::uint64_t provider_id_{};
};

} // namespace gui_forms
