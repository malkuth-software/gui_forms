#include "gui_forms/components/help_provider/help_provider.hpp"

#include "../guidance_utilities.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/text/locale_file/locale_file.hpp"
#include "gui_forms/window.hpp"
#include "../../../core/surface_root/access/surface_help_access.hpp"
#include "../../../core/surface_root/access/surface_lifetime_access.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {

using namespace guidance_detail;

// The provider's own settings for one target. The help string and keyword
// are not here: they are the target's help_line and keywords (help system
// study, section 4: HelpProvider is a view onto the text record), so there
// is one source of help text, which a language file translates.
struct HelpProvider::Entry final {
    std::weak_ptr<Control> target;
    HelpNavigator navigator{HelpNavigator::topic};
    std::optional<bool> show_help;
};

namespace {

// The help string and keyword a request carries: the target's own fields.
struct TargetHelpText final {
    std::string help_line{};
    std::string keywords{};
};

[[nodiscard]] TargetHelpText target_help_text(const std::weak_ptr<Control>& weak_target) {
    TargetHelpText text{};
    const std::shared_ptr<Control> target = weak_target.lock();
    if (!target || !(*target).is_alive()) return text;
    text.help_line = (*target).help_line.read();
    text.keywords = (*target).keywords.read();
    return text;
}

// Refuses a value the target's field would refuse, before any entry is
// made, so a refused write leaves the provider as it was.
void require_help_value(LocaleField field, std::string_view value) {
    const std::optional<LocaleRefusal> refusal = check_locale_value(field, value);
    if (refusal.has_value()) {
        throw std::invalid_argument("GUI.Forms help " + std::string(locale_field_name(field)) +
                                    " refused: " + std::string(locale_refusal_name(*refusal)));
    }
}

} // namespace

class HelpProvider::AcceleratorHolder final {
public:
    explicit AcceleratorHolder(AcceleratorToken value) : token(std::move(value)) {}
    AcceleratorToken token;
};

bool HelpProvider::FocusedHelpAccelerator::operator()() const {
    return provider != nullptr && (*provider).request_focused_help();
}

class HelpProvider::WindowFallback final : public gui_forms::detail::WindowHelpFallback {
public:
    explicit WindowFallback(HelpProvider& owner) noexcept : provider(&owner) {}
    bool offer_window_help(HelpRequestEvent& request) override { return (*provider).offer_window_help(request); }

    HelpProvider* provider{};
};

HelpProvider::HelpProvider(Window& window)
    : window_lifetime_(detail::SurfaceLifetimeAccess::observe(window)),
      provider_id_(next_help_provider_id.fetch_add(1U)) {
    window.verify_access("HelpProvider construction");
    accelerator_ = std::make_unique<AcceleratorHolder>(window.register_accelerator(
        *this, KeyGesture{PhysicalKey::f1, Modifier::none},
        FocusedHelpAccelerator{this}));
    window_fallback_ = std::make_unique<WindowFallback>(*this);
    detail::SurfaceHelpAccess::add_help_fallback(window, *window_fallback_);
}

HelpProvider::~HelpProvider() {
    if (is_alive()) {
        try {
            dispose();
        } catch (...) {
            // Disposal refused off the UI thread: the window still must not
            // offer help to a destroyed provider.
            if (Window* owner = bound_window(); owner != nullptr && window_fallback_) {
                detail::SurfaceHelpAccess::remove_help_fallback(*owner, *window_fallback_);
            }
            accelerator_.reset();
            entries_.clear();
        }
    }
}

Window* HelpProvider::bound_window() const noexcept {
    const std::shared_ptr<gui_forms::detail::WindowLifetime> lifetime = window_lifetime_.lock();
    return lifetime ? (*lifetime).window : nullptr;
}

void HelpProvider::require_access(std::string_view operation) const {
    if (!is_alive()) {
        throw std::logic_error("GUI.Forms cannot use a disposed HelpProvider");
    }
    Window* owner = bound_window();
    if (!owner) {
        throw std::logic_error("GUI.Forms cannot use a HelpProvider after Window shutdown");
    }
    (*owner).verify_access(operation);
}

bool HelpProvider::can_extend(const std::shared_ptr<Control>& target) const {
    Window* owner = bound_window();
    if (!owner || !target || !(*target).is_alive()) return false;
    (*owner).verify_access("HelpProvider target query");
    return (*target).attached_window() == owner &&
           (*owner).find((*target).stable_id().value()).get() == target.get();
}

HelpProvider::Entry* HelpProvider::find_entry(const Control& target) {
    const EntryMap::iterator found = entries_.find(target.runtime_id().value);
    return found == entries_.end() ? nullptr : (*found).second.get();
}

const HelpProvider::Entry* HelpProvider::find_entry(const Control& target) const {
    const EntryMap::const_iterator found =
        entries_.find(target.runtime_id().value);
    return found == entries_.end() ? nullptr : (*found).second.get();
}

HelpProvider::Entry& HelpProvider::require_entry(
    const std::shared_ptr<Control>& target) {
    if (!can_extend(target)) {
        throw std::invalid_argument(
            "GUI.Forms HelpProvider target must be live and attached to its Window");
    }
    if (Entry* existing = find_entry(*target)) return *existing;
    std::unique_ptr<gui_forms::HelpProvider::Entry> entry = std::make_unique<Entry>();
    (*entry).target = target;
    Entry& result = *entry;
    entries_.emplace((*target).runtime_id().value, std::move(entry));
    return result;
}

bool HelpProvider::entry_effective(const Entry& entry) const {
    if (entry.show_help.has_value()) return *entry.show_help;
    const TargetHelpText text = target_help_text(entry.target);
    return !text.help_line.empty() || !text.keywords.empty();
}

void HelpProvider::publish_semantics(Entry& entry) {
    const std::shared_ptr<gui_forms::Control> target = entry.target.lock();
    if (!target || !(*target).is_alive() || (*target).attached_window() != bound_window()) {
        return;
    }
    std::string description;
    if (entry_effective(entry)) {
        TargetHelpText text = target_help_text(entry.target);
        description = text.help_line.empty() ? std::move(text.keywords) : std::move(text.help_line);
    }
    if (description.empty()) {
        (*target).clear_provider_help(provider_id_);
    } else {
        (*target).set_provider_help(provider_id_, std::move(description));
    }
}

void HelpProvider::set_help_string(const std::shared_ptr<Control>& target,
                                   std::string text) {
    require_access("HelpProvider help-string mutation");
    require_help_value(LocaleField::help_line, text);
    Entry& entry = require_entry(target);
    if ((*target).help_line.read() != text) (*target).help_line = std::move(text);
    publish_semantics(entry);
    erase_if_empty((*target).runtime_id().value);
}

std::string HelpProvider::help_string(const Control& target) const {
    if (Window* owner = bound_window()) (*owner).verify_access("HelpProvider string query");
    std::string text = target.help_line.read();
    return text;
}

void HelpProvider::set_help_keyword(const std::shared_ptr<Control>& target,
                                    std::string keyword) {
    require_access("HelpProvider keyword mutation");
    require_help_value(LocaleField::keywords, keyword);
    Entry& entry = require_entry(target);
    if ((*target).keywords.read() != keyword) (*target).keywords = std::move(keyword);
    publish_semantics(entry);
    erase_if_empty((*target).runtime_id().value);
}

std::string HelpProvider::help_keyword(const Control& target) const {
    if (Window* owner = bound_window()) (*owner).verify_access("HelpProvider keyword query");
    std::string keyword = target.keywords.read();
    return keyword;
}

void HelpProvider::set_help_navigator(const std::shared_ptr<Control>& target,
                                      HelpNavigator navigator) {
    require_access("HelpProvider navigator mutation");
    if (!valid_help_navigator(navigator)) {
        throw std::invalid_argument("GUI.Forms help navigator is invalid");
    }
    Entry& entry = require_entry(target);
    entry.navigator = navigator;
    erase_if_empty((*target).runtime_id().value);
}

HelpNavigator HelpProvider::help_navigator(const Control& target) const {
    if (Window* owner = bound_window()) (*owner).verify_access("HelpProvider navigator query");
    const Entry* entry = find_entry(target);
    return entry ? (*entry).navigator : HelpNavigator::topic;
}

void HelpProvider::set_show_help(const std::shared_ptr<Control>& target, bool show) {
    require_access("HelpProvider show-help mutation");
    Entry& entry = require_entry(target);
    if (entry.show_help == show) return;
    entry.show_help = show;
    publish_semantics(entry);
}

bool HelpProvider::show_help(const Control& target) const {
    if (Window* owner = bound_window()) (*owner).verify_access("HelpProvider show-help query");
    const Entry* entry = find_entry(target);
    return entry && entry_effective(*entry);
}

void HelpProvider::reset_show_help(const Control& target) {
    require_access("HelpProvider show-help reset");
    Entry* entry = find_entry(target);
    if (!entry || !(*entry).show_help.has_value()) return;
    (*entry).show_help.reset();
    publish_semantics(*entry);
    erase_if_empty(target.runtime_id().value);
}

void HelpProvider::clear() {
    require_access("HelpProvider clear");
    for (std::pair<const std::uint64_t, std::unique_ptr<Entry>>& mapped_entry :
         entries_) {
        std::unique_ptr<Entry>& entry = mapped_entry.second;
        if (const std::shared_ptr<gui_forms::Control> target = (*entry).target.lock(); target && (*target).is_alive()) {
            (*target).clear_provider_help(provider_id_);
        }
    }
    entries_.clear();
}

void HelpProvider::write_view(const decltype(tag)&, std::any incoming) {
    require_access("HelpProvider tag mutation");
    tag_ = std::move(incoming);
}

std::any HelpProvider::read_view(const decltype(tag)&) const {
    std::any held = tag_;
    return held;
}

bool HelpProvider::request_help(const std::shared_ptr<Control>& target,
                                Point position,
                                bool keyboard_initiated) {
    require_access("HelpProvider help request");
    if (!target || !can_extend(target)) return false;
    const Entry* entry = find_entry(*target);
    if (!entry || !entry_effective(*entry)) return false;
    if (keyboard_initiated) {
        const Rect bounds = (*target).absolute_bounds();
        position = {bounds.x + bounds.width * 0.5,
                    bounds.y + bounds.height * 0.5};
    }
    TargetHelpText text = target_help_text(target);
    HelpRequestEvent request{
        std::string((*target).stable_id().value()), position, help_namespace.read(),
        std::move(text.help_line), std::move(text.keywords), (*entry).navigator,
        keyboard_initiated, false};
    ++request_count_;
    (*target).help_requested_.emit(request);
    if (!request.handled) help_requested_.emit(request);
    if (request.handled) ++handled_request_count_;
    return request.handled;
}

// The window's own help request, offered after its handlers and the
// providers created earlier: this provider's namespace, and the root's
// mapping where it has one.
bool HelpProvider::offer_window_help(HelpRequestEvent& request) {
    if (!is_alive()) return false;
    HelpRequestEvent offered = request;
    offered.help_namespace = help_namespace.read();
    Window* owner = bound_window();
    const std::shared_ptr<Control> root = owner != nullptr ? (*owner).root() : nullptr;
    const Entry* entry = root ? find_entry(*root) : nullptr;
    if (entry != nullptr && entry_effective(*entry)) {
        TargetHelpText text = target_help_text(root);
        offered.help_string = std::move(text.help_line);
        offered.keyword = std::move(text.keywords);
        offered.navigator = (*entry).navigator;
    }
    ++request_count_;
    help_requested_.emit(offered);
    if (!offered.handled) return false;
    ++handled_request_count_;
    request.handled = true;
    return true;
}

bool HelpProvider::request_focused_help() {
    Window* owner = bound_window();
    if (!owner) return false;
    std::shared_ptr<Control> target = (*owner).focused_control();
    if (!target) target = (*owner).root();
    for (std::shared_ptr<Control> current = target; current; current = (*current).parent()) {
        const Entry* entry = find_entry(*current);
        if (entry && entry_effective(*entry)) {
            return request_help(current, {}, true);
        }
    }
    return false;
}

void HelpProvider::erase_if_empty(std::uint64_t runtime_id) {
    const EntryMap::iterator found = entries_.find(runtime_id);
    if (found == entries_.end()) return;
    const Entry& entry = *(*found).second;
    const TargetHelpText text = target_help_text(entry.target);
    if (text.help_line.empty() && text.keywords.empty() &&
        entry.navigator == HelpNavigator::topic && !entry.show_help.has_value()) {
        if (const std::shared_ptr<gui_forms::Control> target = entry.target.lock(); target && (*target).is_alive()) {
            (*target).clear_provider_help(provider_id_);
        }
        entries_.erase(found);
    }
}

HelpProviderSnapshot HelpProvider::snapshot() const {
    HelpProviderSnapshot result;
    result.mappings = entries_.size();
    result.requests = request_count_;
    result.handled_requests = handled_request_count_;
    for (const EntryMap::value_type& pair : entries_) {
        const std::shared_ptr<Control> target = (*pair.second).target.lock();
        if (target && (*target).is_alive() && entry_effective(*pair.second)) {
            ++result.effective_mappings;
        }
    }
    return result;
}

void HelpProvider::verify_dispose_thread() {
    if (Window* owner = bound_window()) (*owner).verify_access("HelpProvider disposal");
}

void HelpProvider::on_dispose() noexcept {
    try {
        for (std::pair<const std::uint64_t, std::unique_ptr<Entry>>&
                 mapped_entry : entries_) {
            std::unique_ptr<Entry>& entry = mapped_entry.second;
            if (const std::shared_ptr<gui_forms::Control> target = (*entry).target.lock(); target && (*target).is_alive()) {
                (*target).clear_provider_help(provider_id_);
            }
        }
    } catch (...) {
    }
    entries_.clear();
    accelerator_.reset();
    if (Window* owner = bound_window(); owner != nullptr && window_fallback_) {
        detail::SurfaceHelpAccess::remove_help_fallback(*owner, *window_fallback_);
    }
    tag_.reset();
    window_lifetime_.reset();
}

namespace {

// The events HelpProvider adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 1> help_provider_interactions{{
    {.name = "help_requested", .payload = "HelpRequestEvent", .mutable_payload = true},
}};

} // namespace

void HelpProvider::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), help_provider_interactions.begin(), help_provider_interactions.end());
}

// property_codemod begin HelpProvider (tools/property_families/help_provider.json; generated, do not edit)

void HelpProvider::prepare_write(const decltype(help_namespace)&, std::string& incoming) {
    require_access("HelpProvider namespace mutation");
    if (!validate_utf8(incoming).valid()) {
        throw std::invalid_argument("GUI.Forms help namespace must be valid UTF-8");
    }
}

void HelpProvider::written(const decltype(help_namespace)&) {}

// property_codemod end HelpProvider

} // namespace gui_forms
