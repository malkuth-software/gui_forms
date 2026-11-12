#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/types.hpp"
#include "gui_forms/components/help_provider/help_provider_capabilities/help_provider_capabilities.hpp"

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
namespace detail {
class WindowHelpFallback;
}
class BindingSource;
struct BindingCompleteEvent;
class ToolTip;
class Window;

namespace detail {
struct WindowLifetime;
}
enum class HelpNavigator : std::uint8_t {
    topic,
    table_of_contents,
    index,
    find,
    associate_index,
    keyword_index,
};

struct GUI_FORMS_API HelpRequestEvent final {
    std::string target_stable_id;
    Point position{};
    std::string help_namespace;
    std::string help_string;
    std::string keyword;
    HelpNavigator navigator{HelpNavigator::topic};
    bool keyboard_initiated{};
    bool handled{};
};

struct GUI_FORMS_API HelpProviderSnapshot final {
    std::size_t mappings{};
    std::size_t effective_mappings{};
    std::uint64_t requests{};
    std::uint64_t handled_requests{};
};

// Help resolution is intentionally policy-free. The provider retains authored
// metadata and routes F1/mouse requests, but never opens a browser, help file,
// or network resource. It is a view onto the text record (help system study,
// section 4): set_help_string writes the target's help_line and
// set_help_keyword its keywords, and the getters read them, so a value set
// on the control or through any provider is one value, a program value a
// language file translates. clear() and disposal drop the provider's own
// settings (navigator, show-help, its semantic description) and leave the
// controls' fields. A value the target's field refuses (check_locale_value)
// throws std::invalid_argument and changes nothing. Consumers or capability-gated plugins handle the event.
// It is also offered the window's own help requests (a message box's Help
// button or F1, Window::request_help) that no window handler took, after the
// window's earlier-created providers, with its namespace and the root's
// mapping where it has one.
class GUI_FORMS_API HelpProvider final : public Component, public HelpProviderCapabilities<HelpProvider> {
public:
    explicit HelpProvider(Window& window);
    ~HelpProvider() override;

    [[nodiscard]] bool can_extend(const std::shared_ptr<Control>& target) const;
    void set_help_string(const std::shared_ptr<Control>& target, std::string text);
    [[nodiscard]] std::string help_string(const Control& target) const;
    void set_help_keyword(const std::shared_ptr<Control>& target,
                          std::string keyword);
    [[nodiscard]] std::string help_keyword(const Control& target) const;
    void set_help_navigator(const std::shared_ptr<Control>& target,
                            HelpNavigator navigator);
    [[nodiscard]] HelpNavigator help_navigator(const Control& target) const;
    void set_show_help(const std::shared_ptr<Control>& target, bool show);
    [[nodiscard]] bool show_help(const Control& target) const;
    void reset_show_help(const Control& target);
    void clear();

    bool request_help(const std::shared_ptr<Control>& target, Point position,
                      bool keyboard_initiated = false);
    [[nodiscard]] Event<HelpRequestEvent&>& help_requested() noexcept {
        return help_requested_;
    }
    [[nodiscard]] HelpProviderSnapshot snapshot() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    struct Entry;
    struct FocusedHelpAccelerator final {
        HelpProvider* provider{};

        bool operator()() const;
    };
    // The provider's place in its window's help offers.
    class WindowFallback;
    class AcceleratorHolder;
    using EntryMap =
        std::unordered_map<std::uint64_t, std::unique_ptr<Entry>>;

    [[nodiscard]] Window* bound_window() const noexcept;
    void require_access(std::string_view operation) const;
    [[nodiscard]] Entry* find_entry(const Control& target);
    [[nodiscard]] const Entry* find_entry(const Control& target) const;
    [[nodiscard]] Entry& require_entry(const std::shared_ptr<Control>& target);
    [[nodiscard]] bool entry_effective(const Entry& entry) const;
    void publish_semantics(Entry& entry);
    void erase_if_empty(std::uint64_t runtime_id);
    bool request_focused_help();
    bool offer_window_help(HelpRequestEvent& request);

    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    EntryMap entries_;
    std::unique_ptr<AcceleratorHolder> accelerator_;
    std::unique_ptr<WindowFallback> window_fallback_;
    Event<HelpRequestEvent&> help_requested_;
    std::any tag_;
    std::uint64_t provider_id_{};
    std::uint64_t request_count_{};
    std::uint64_t handled_request_count_{};
    // property_codemod begin HelpProvider (tools/property_families/help_provider.json; generated, do not edit)
private:
    friend struct HelpProviderHelpNamespaceCapability<HelpProvider>;
    friend struct HelpProviderTagCapability<HelpProvider>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(help_namespace)& cell, std::string& incoming);
    void written(const decltype(help_namespace)& cell);
    void write_view(const decltype(tag)& cell, std::any incoming);
    [[nodiscard]] std::any read_view(const decltype(tag)& cell) const;
    // property_codemod end HelpProvider
};

} // namespace gui_forms
