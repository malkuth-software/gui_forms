#include "gui_forms/components/error_provider/error_provider.hpp"

#include "../error_layer/error_layer.hpp"
#include "../error_glyph/error_glyph.hpp"
#include "../guidance_utilities.hpp"
#include "gui_forms/binding.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"

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
struct ErrorProvider::Entry final {
    std::weak_ptr<Control> target;
    std::string error;
    ErrorIconAlignment alignment{ErrorIconAlignment::middle_right};
    double padding{};
    std::shared_ptr<ErrorLayer> layer;
    std::shared_ptr<ErrorGlyph> glyph;
    std::unique_ptr<OverlayToken> overlay;
    SubscriptionToken bounds_subscription;
};

struct ErrorProvider::TargetBoundsObserver final {
    ErrorProvider* provider{};
    std::weak_ptr<Control> target;

    void operator()(Rect) const {
        const std::shared_ptr<Control> retained = target.lock();
        if (!retained) return;
        Entry* entry = (*provider).find_entry(*retained);
        if (entry != nullptr) (*provider).position_visual(*entry);
    }
};

[[nodiscard]] bool error_snapshot_less(
    const ErrorIconSnapshot& left,
    const ErrorIconSnapshot& right) noexcept {
    return left.target_stable_id < right.target_stable_id;
}

ErrorProvider::ErrorProvider(Window& window)
    : window_lifetime_(window.lifetime_),
      provider_id_(next_error_provider_id.fetch_add(1U)) {
    window.verify_access("ErrorProvider construction");
    availability_subscription_ = window.control_availability_changed().subscribe(
        *this, Delegate<const ControlAvailabilityChange&>::bind<
            ErrorProvider, &ErrorProvider::control_availability_changed>(*this));
    presentation_subscription_ = window.presentation_changed().subscribe(
        *this, Delegate<const PresentationSettings&>::bind<
            ErrorProvider, &ErrorProvider::presentation_settings_changed>(*this));
    root_bounds_subscription_ = (*window.root()).arranged_bounds_changed().subscribe(
        *this, Delegate<Rect>::bind<
            ErrorProvider, &ErrorProvider::root_bounds_changed>(*this));
}

ErrorProvider::~ErrorProvider() {
    if (is_alive()) {
        try {
            dispose();
        } catch (...) {
            entries_.clear();
        }
    }
}

void ErrorProvider::control_availability_changed(
    const ControlAvailabilityChange&) {
    refresh_all_visuals(false);
}

void ErrorProvider::presentation_settings_changed(
    const PresentationSettings&) {
    refresh_all_visuals(false);
}

void ErrorProvider::root_bounds_changed(Rect) {
    Window* owner = bound_window();
    if (owner == nullptr) return;
    for (std::pair<const std::uint64_t, std::unique_ptr<Entry>>& mapped_entry :
         entries_) {
        std::unique_ptr<Entry>& entry = mapped_entry.second;
        if ((*entry).layer) {
            (*(*entry).layer).set_requested_bounds(
                {0.0, 0.0, (*owner).client_size().width,
                 (*owner).client_size().height});
        }
        position_visual(*entry);
    }
}

Window* ErrorProvider::bound_window() const noexcept {
    const std::shared_ptr<gui_forms::detail::WindowLifetime> lifetime = window_lifetime_.lock();
    return lifetime ? (*lifetime).window : nullptr;
}

void ErrorProvider::require_access(std::string_view operation) const {
    if (!is_alive()) {
        throw std::logic_error("GUI.Forms cannot use a disposed ErrorProvider");
    }
    Window* owner = bound_window();
    if (!owner) {
        throw std::logic_error("GUI.Forms cannot use an ErrorProvider after Window shutdown");
    }
    (*owner).verify_access(operation);
}

bool ErrorProvider::can_extend(const std::shared_ptr<Control>& target) const {
    Window* owner = bound_window();
    if (!owner || !target || !(*target).is_alive()) return false;
    (*owner).verify_access("ErrorProvider target query");
    return (*target).attached_window() == owner &&
           (*owner).find((*target).stable_id().value()).get() == target.get();
}

ErrorProvider::Entry* ErrorProvider::find_entry(const Control& target) {
    const EntryMap::iterator found = entries_.find(target.runtime_id().value);
    return found == entries_.end() ? nullptr : (*found).second.get();
}

const ErrorProvider::Entry* ErrorProvider::find_entry(const Control& target) const {
    const EntryMap::const_iterator found =
        entries_.find(target.runtime_id().value);
    return found == entries_.end() ? nullptr : (*found).second.get();
}

ErrorProvider::Entry& ErrorProvider::require_entry(
    const std::shared_ptr<Control>& target) {
    if (!can_extend(target)) {
        throw std::invalid_argument(
            "GUI.Forms ErrorProvider target must be live and attached to its Window");
    }
    if (Entry* existing = find_entry(*target)) return *existing;
    std::unique_ptr<gui_forms::ErrorProvider::Entry> entry = std::make_unique<Entry>();
    (*entry).target = target;
    const std::weak_ptr<Control> weak_target = target;
    (*entry).bounds_subscription = (*target).arranged_bounds_changed().subscribe(
        *this, TargetBoundsObserver{this, weak_target});
    Entry& result = *entry;
    entries_.emplace((*target).runtime_id().value, std::move(entry));
    return result;
}

void ErrorProvider::set_error(const std::shared_ptr<Control>& target,
                              std::string error_value) {
    require_access("ErrorProvider error mutation");
    if (!validate_utf8(error_value).valid()) {
        throw std::invalid_argument("GUI.Forms error text must be valid UTF-8");
    }
    Entry& entry = require_entry(target);
    if (entry.error == error_value) return;
    entry.error = std::move(error_value);
    if (entry.error.empty()) {
        (*target).clear_provider_error(provider_id_);
    } else {
        (*target).set_provider_error(provider_id_, entry.error);
    }
    refresh_visual(entry, true);
    const ErrorProviderChange change{
        std::string((*target).stable_id().value()), entry.error};
    error_changed_.emit(change);
    erase_if_empty((*target).runtime_id().value);
}

std::string ErrorProvider::error(const Control& target) const {
    if (Window* owner = bound_window()) (*owner).verify_access("ErrorProvider error query");
    const Entry* entry = find_entry(target);
    return entry ? (*entry).error : std::string{};
}

void ErrorProvider::clear() {
    require_access("ErrorProvider clear");
    for (std::pair<const std::uint64_t, std::unique_ptr<Entry>>& mapped_entry :
         entries_) {
        std::unique_ptr<Entry>& entry = mapped_entry.second;
        if (const std::shared_ptr<gui_forms::Control> target = (*entry).target.lock(); target && (*target).is_alive()) {
            (*target).clear_provider_error(provider_id_);
        }
        close_visual(*entry);
    }
    entries_.clear();
}

bool ErrorProvider::has_errors() const noexcept {
    Window* owner = bound_window();
    for (const std::pair<const std::uint64_t, std::unique_ptr<Entry>>& pair :
         entries_) {
        const Entry& entry = *pair.second;
        const std::shared_ptr<gui_forms::Control> target = entry.target.lock();
        if (!entry.error.empty() && target && (*target).is_alive() &&
            (*target).attached_window() == owner) {
            return true;
        }
    }
    return false;
}

void ErrorProvider::set_icon_alignment(const std::shared_ptr<Control>& target,
                                       ErrorIconAlignment alignment) {
    require_access("ErrorProvider icon-alignment mutation");
    if (!valid_alignment(alignment)) {
        throw std::invalid_argument("GUI.Forms error icon alignment is invalid");
    }
    Entry& entry = require_entry(target);
    if (entry.alignment == alignment) return;
    entry.alignment = alignment;
    position_visual(entry);
}

ErrorIconAlignment ErrorProvider::icon_alignment(const Control& target) const {
    if (Window* owner = bound_window()) {
        (*owner).verify_access("ErrorProvider icon-alignment query");
    }
    const Entry* entry = find_entry(target);
    return entry ? (*entry).alignment : ErrorIconAlignment::middle_right;
}

void ErrorProvider::set_icon_padding(const std::shared_ptr<Control>& target,
                                     double padding) {
    require_access("ErrorProvider icon-padding mutation");
    if (!std::isfinite(padding) || padding < -1024.0 || padding > 1024.0) {
        throw std::invalid_argument(
            "GUI.Forms error icon padding must be finite and bounded");
    }
    Entry& entry = require_entry(target);
    if (entry.padding == padding) return;
    entry.padding = padding;
    position_visual(entry);
}

double ErrorProvider::icon_padding(const Control& target) const {
    if (Window* owner = bound_window()) {
        (*owner).verify_access("ErrorProvider icon-padding query");
    }
    const Entry* entry = find_entry(target);
    return entry ? (*entry).padding : 0.0;
}

void ErrorProvider::set_icon_size(double size) {
    require_access("ErrorProvider icon-size mutation");
    if (!std::isfinite(size) || size < 8.0 || size > 64.0) {
        throw std::invalid_argument(
            "ErrorProvider icon size must be finite and between 8 and 64");
    }
    if (icon_size_ == size) return;
    icon_size_ = size;
    refresh_all_visuals(false);
}

void ErrorProvider::set_blink_rate(std::chrono::milliseconds rate) {
    require_access("ErrorProvider blink-rate mutation");
    if (rate < std::chrono::milliseconds(50) ||
        rate > std::chrono::minutes(1)) {
        throw std::invalid_argument(
            "GUI.Forms ErrorProvider blink rate must be between 50 ms and 1 minute");
    }
    if (blink_rate_ == rate) return;
    blink_rate_ = rate;
    refresh_all_visuals(false);
}

void ErrorProvider::set_blink_style(ErrorBlinkStyle style) {
    require_access("ErrorProvider blink-style mutation");
    if (!valid_blink_style(style)) {
        throw std::invalid_argument("GUI.Forms ErrorProvider blink style is invalid");
    }
    if (blink_style_ == style) return;
    blink_style_ = style;
    refresh_all_visuals(true);
}

void ErrorProvider::set_right_to_left(bool value) {
    require_access("ErrorProvider direction mutation");
    if (right_to_left_ == value) return;
    right_to_left_ = value;
    refresh_all_visuals(false);
    right_to_left_changed_.emit(value);
}

void ErrorProvider::set_icon(std::optional<ImageId> icon_value) {
    require_access("ErrorProvider icon mutation");
    Window* owner = bound_window();
    if (icon_value && ((*icon_value).value == 0U || !owner ||
                       !(*owner).image_resources().find(*icon_value))) {
        throw std::invalid_argument(
            "GUI.Forms ErrorProvider icon must identify a live Window image");
    }
    if (icon_ == icon_value) return;
    icon_ = icon_value;
    refresh_all_visuals(false);
}

std::shared_ptr<Control> ErrorProvider::container_control() const noexcept {
    Window* owner = bound_window();
    return owner ? (*owner).root() : std::shared_ptr<Control>{};
}

void ErrorProvider::set_data_source(std::shared_ptr<BindingSource> source) {
    require_access("ErrorProvider data-source mutation");
    if (data_source_.lock() == source) {
        update_binding();
        return;
    }
    if (source && (!(*source).is_alive() || (*source).bound_window() != bound_window())) {
        throw std::invalid_argument(
            "GUI.Forms ErrorProvider data source must be live and owned by its Window");
    }
    source_list_subscription_.disconnect();
    source_current_subscription_.disconnect();
    source_completion_subscription_.disconnect();
    source_disposed_subscription_.disconnect();
    clear_bound_errors();
    binding_errors_.clear();
    data_source_ = source;
    if (source) {
        source_list_subscription_ = (*source).list_changed().subscribe(
            *this, Delegate<const BindingListChange&>::bind<
                ErrorProvider, &ErrorProvider::source_list_changed>(*this));
        source_current_subscription_ = (*source).current_changed().subscribe(
            *this, Delegate<>::bind<
                ErrorProvider, &ErrorProvider::source_current_changed>(*this));
        source_completion_subscription_ = (*source).binding_complete().subscribe(
            *this, Delegate<BindingCompleteEvent&>::bind<
                ErrorProvider, &ErrorProvider::binding_completed>(*this));
        source_disposed_subscription_ = (*source).disposed_event().subscribe(
            *this, Delegate<>::bind<
                ErrorProvider, &ErrorProvider::source_disposed>(*this));
    }
    data_source_changed_.emit();
    update_binding();
}

void ErrorProvider::source_list_changed(const BindingListChange&) {
    update_binding();
}

void ErrorProvider::source_current_changed() {
    binding_errors_.clear();
    update_binding();
}

void ErrorProvider::source_disposed() noexcept {
    try {
        source_list_subscription_.disconnect();
        source_current_subscription_.disconnect();
        source_completion_subscription_.disconnect();
        source_disposed_subscription_.disconnect();
        clear_bound_errors();
        binding_errors_.clear();
        data_source_.reset();
        data_source_changed_.emit();
    } catch (...) {
    }
}

void ErrorProvider::append_unique_error(
    std::vector<std::string>& values, std::string value) {
    if (!value.empty() &&
        std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

void ErrorProvider::set_data_member(std::string member) {
    require_access("ErrorProvider data-member mutation");
    if (!validate_utf8(member).valid()) {
        throw std::invalid_argument(
            "GUI.Forms ErrorProvider data member must be valid UTF-8");
    }
    if (!member.empty()) member = canonical_binding_name(member);
    if (data_member_ == member) return;
    data_member_ = std::move(member);
    binding_errors_.clear();
    data_member_changed_.emit(data_member_);
    update_binding();
}

void ErrorProvider::bind_to_data_and_errors(
    std::shared_ptr<BindingSource> source, std::string member) {
    set_data_member(std::move(member));
    set_data_source(std::move(source));
}

void ErrorProvider::clear_bound_errors() noexcept {
    std::unordered_map<std::uint64_t, std::weak_ptr<Control>> targets =
        std::move(bound_targets_);
    bound_targets_.clear();
    for (std::pair<const std::uint64_t, std::weak_ptr<Control>>& target_entry :
         targets) {
        std::weak_ptr<Control>& weak = target_entry.second;
        if (const std::shared_ptr<gui_forms::Control> target = weak.lock(); target && (*target).is_alive()) {
            try { set_error(target, {}); } catch (...) {}
        }
    }
}

void ErrorProvider::binding_completed(BindingCompleteEvent& event) {
    if (!event.binding || (*event.binding).source() != data_source_.lock()) return;
    Control* raw = (*event.binding).target();
    if (!raw) return;
    const std::shared_ptr<gui_forms::Control> target = (*raw).weak_from_this().lock();
    if (event.state == BindingCompleteState::success && !event.cancel) {
        binding_errors_.erase(event.binding);
    } else if (target) {
        std::string error = event.error_text;
        if (error.empty()) error = "The value was not accepted.";
        binding_errors_[event.binding] = {target, std::move(error)};
    }
    update_binding();
}

void ErrorProvider::update_binding() {
    require_access("ErrorProvider binding refresh");
    const std::shared_ptr<gui_forms::BindingSource> source = data_source_.lock();
    if (!source || !(*source).is_alive()) {
        clear_bound_errors();
        binding_errors_.clear();
        return;
    }

    struct Aggregate final {
        std::shared_ptr<Control> target;
        std::vector<std::string> errors;
    };
    std::unordered_map<std::uint64_t, Aggregate> aggregates;
    const std::vector<std::shared_ptr<Binding>> bindings = (*source).bindings();
    std::unordered_set<const Binding*> active_bindings;
    for (const std::shared_ptr<gui_forms::Binding>& binding : bindings) {
        if (binding) active_bindings.insert(binding.get());
        Control* raw = binding ? (*binding).target() : nullptr;
        const std::shared_ptr<gui_forms::Control> target = raw ? (*raw).weak_from_this().lock() : nullptr;
        if (!target || !can_extend(target)) continue;
        Aggregate& aggregate = aggregates[(*target).runtime_id().value];
        aggregate.target = target;
        append_unique_error(aggregate.errors, (*source).current_error({}));
        std::string field = (*binding).data_member();
        if (!data_member_.empty()) field = data_member_ + "." + field;
        std::string record_error = (*source).current_error(field);
        if (record_error.empty() && !data_member_.empty()) {
            record_error = (*source).current_error((*binding).data_member());
        }
        append_unique_error(aggregate.errors, std::move(record_error));
        if (const BindingErrorMap::iterator found =
                binding_errors_.find(binding.get());
            found != binding_errors_.end()) {
            append_unique_error(aggregate.errors, (*found).second.second);
        }
    }

    std::unordered_map<std::uint64_t, std::weak_ptr<Control>> previous =
        std::move(bound_targets_);
    bound_targets_.clear();
    for (std::pair<const std::uint64_t, Aggregate>& aggregate_entry :
         aggregates) {
        const std::uint64_t id = aggregate_entry.first;
        Aggregate& aggregate = aggregate_entry.second;
        std::string text;
        for (const std::string& item : aggregate.errors) {
            if (!text.empty()) text += "\n";
            text += item;
        }
        set_error(aggregate.target, std::move(text));
        bound_targets_[id] = aggregate.target;
        previous.erase(id);
    }
    for (std::pair<const std::uint64_t, std::weak_ptr<Control>>& target_entry :
         previous) {
        std::weak_ptr<Control>& weak = target_entry.second;
        if (const std::shared_ptr<gui_forms::Control> target = weak.lock(); target && (*target).is_alive()) {
            set_error(target, {});
        }
    }
    BindingErrorMap::iterator error = binding_errors_.begin();
    while (error != binding_errors_.end()) {
        const std::shared_ptr<Control> target = (*error).second.first.lock();
        if (!target || !(*target).is_alive() ||
            !active_bindings.contains((*error).first)) {
            error = binding_errors_.erase(error);
        } else {
            ++error;
        }
    }
}

void ErrorProvider::set_tag(std::any tag_value) {
    require_access("ErrorProvider tag mutation");
    tag_ = std::move(tag_value);
}

namespace {

// The alignment on the other side of the target, at the same height.
ErrorIconAlignment mirrored_alignment(ErrorIconAlignment alignment) noexcept {
    switch (alignment) {
    case ErrorIconAlignment::top_left: return ErrorIconAlignment::top_right;
    case ErrorIconAlignment::top_right: return ErrorIconAlignment::top_left;
    case ErrorIconAlignment::middle_left: return ErrorIconAlignment::middle_right;
    case ErrorIconAlignment::middle_right: return ErrorIconAlignment::middle_left;
    case ErrorIconAlignment::bottom_left: return ErrorIconAlignment::bottom_right;
    case ErrorIconAlignment::bottom_right: return ErrorIconAlignment::bottom_left;
    }
    // An alignment outside the enumeration is left as it is.
    return alignment;
}

} // namespace

Rect ErrorProvider::icon_bounds(const Entry& entry) const noexcept {
    const std::shared_ptr<gui_forms::Control> target = entry.target.lock();
    Window* owner = bound_window();
    if (!target || !owner) return {};
    const Rect anchor = (*target).absolute_bounds();
    ErrorIconAlignment alignment = entry.alignment;
    if (right_to_left_) alignment = mirrored_alignment(alignment);
    const bool left = alignment == ErrorIconAlignment::top_left ||
                      alignment == ErrorIconAlignment::middle_left ||
                      alignment == ErrorIconAlignment::bottom_left;
    double x = left ? anchor.x - icon_size_ - entry.padding
                    : anchor.right() + entry.padding;
    double y = anchor.y;
    if (alignment == ErrorIconAlignment::middle_left ||
        alignment == ErrorIconAlignment::middle_right) {
        y = anchor.y + (anchor.height - icon_size_) * 0.5;
    } else if (alignment == ErrorIconAlignment::bottom_left ||
               alignment == ErrorIconAlignment::bottom_right) {
        y = anchor.bottom() - icon_size_;
    }
    const Size client = (*owner).client_size();
    x = std::clamp(x, 0.0, std::max(0.0, client.width - icon_size_));
    y = std::clamp(y, 0.0, std::max(0.0, client.height - icon_size_));
    return {x, y, icon_size_, icon_size_};
}

void ErrorProvider::position_visual(Entry& entry) {
    if (entry.glyph && entry.overlay && (*entry.overlay).connected()) {
        (*entry.glyph).set_requested_bounds(icon_bounds(entry));
    }
}

void ErrorProvider::close_visual(Entry& entry) noexcept {
    // The glyph's tooltip (its error text) goes with the glyph: closing the
    // overlay detaches it, which closes a tooltip that shows for it.
    if (entry.overlay) (*entry.overlay).disconnect();
    entry.overlay.reset();
    entry.glyph.reset();
    entry.layer.reset();
}

void ErrorProvider::refresh_visual(Entry& entry, bool error_changed) {
    Window* owner = bound_window();
    const std::shared_ptr<gui_forms::Control> target = entry.target.lock();
    const bool available = owner && target && (*target).is_alive() &&
        (*target).attached_window() == owner && (*target).effectively_visible();
    if (entry.error.empty() || !available) {
        close_visual(entry);
        return;
    }

    if (entry.overlay && !(*entry.overlay).connected()) close_visual(entry);
    if (!entry.overlay) {
        const std::string prefix = "error-provider." +
            std::to_string(provider_id_) + "." +
            std::to_string((*target).runtime_id().value);
        entry.layer = make_control<ErrorLayer>(StableId(prefix + ".layer"));
        (*entry.layer).set_requested_bounds(
            {0.0, 0.0, (*owner).client_size().width, (*owner).client_size().height});
        entry.glyph = make_control<ErrorGlyph>(StableId(prefix + ".glyph"));
        (*entry.glyph).set_requested_bounds(icon_bounds(entry));
        (*entry.glyph).set_error(entry.error);
        (*entry.glyph).set_icon(icon_);
        (*entry.glyph).configure_blink(blink_style_, blink_rate_, true);
        (*entry.layer).add_child(entry.glyph);
        OverlayToken overlay = (*owner).open_overlay(
            target, entry.layer, OverlayOptions{.require_enabled_owner = false});
        entry.overlay = std::make_unique<OverlayToken>(std::move(overlay));
        (*entry.glyph).tool_tip_text = entry.error;
        return;
    }

    (*entry.glyph).set_error(entry.error);
    (*entry.glyph).set_icon(icon_);
    (*entry.glyph).configure_blink(blink_style_, blink_rate_, error_changed);
    (*entry.glyph).tool_tip_text = entry.error;
    position_visual(entry);
}

void ErrorProvider::refresh_all_visuals(bool restart_blink) {
    for (std::pair<const std::uint64_t, std::unique_ptr<Entry>>& mapped_entry :
         entries_) {
        std::unique_ptr<Entry>& entry = mapped_entry.second;
        refresh_visual(*entry, restart_blink);
        if ((*entry).glyph) (*(*entry).glyph).refresh_motion_policy();
    }
}

void ErrorProvider::erase_if_empty(std::uint64_t runtime_id) {
    const EntryMap::iterator found = entries_.find(runtime_id);
    if (found == entries_.end()) return;
    const Entry& entry = *(*found).second;
    if (entry.error.empty() && entry.alignment == ErrorIconAlignment::middle_right &&
        entry.padding == 0.0) {
        entries_.erase(found);
    }
}

ErrorProviderSnapshot ErrorProvider::snapshot() const {
    if (Window* owner = bound_window()) (*owner).verify_access("ErrorProvider snapshot");
    ErrorProviderSnapshot result;
    result.icons.reserve(entries_.size());
    for (const std::pair<const std::uint64_t, std::unique_ptr<Entry>>&
             mapped_entry : entries_) {
        const std::unique_ptr<Entry>& entry = mapped_entry.second;
        const std::shared_ptr<gui_forms::Control> target = (*entry).target.lock();
        const bool available = target && (*target).is_alive() &&
            (*target).attached_window() == bound_window() &&
            (*target).effectively_visible();
        const bool presented = (*entry).overlay && (*(*entry).overlay).connected() &&
            (*entry).glyph;
        if (!(*entry).error.empty() && target && (*target).is_alive()) {
            ++result.live_errors;
        }
        if (presented) ++result.presented_icons;
        result.icons.push_back({
            target ? std::string((*target).stable_id().value()) : std::string{},
            (*entry).error, icon_bounds(*entry), available, presented,
            presented && (*(*entry).glyph).blink_active(),
            !presented || (*(*entry).glyph).phase_visible()});
    }
    std::sort(result.icons.begin(), result.icons.end(), error_snapshot_less);
    return result;
}

void ErrorProvider::verify_dispose_thread() {
    if (Window* owner = bound_window()) (*owner).verify_access("ErrorProvider disposal");
}

void ErrorProvider::on_dispose() noexcept {
    try {
        source_list_subscription_.disconnect();
        source_current_subscription_.disconnect();
        source_completion_subscription_.disconnect();
        source_disposed_subscription_.disconnect();
        data_source_.reset();
        binding_errors_.clear();
        bound_targets_.clear();
        for (std::pair<const std::uint64_t, std::unique_ptr<Entry>>&
                 mapped_entry : entries_) {
            std::unique_ptr<Entry>& entry = mapped_entry.second;
            if (const std::shared_ptr<gui_forms::Control> target = (*entry).target.lock(); target && (*target).is_alive()) {
                (*target).clear_provider_error(provider_id_);
            }
            close_visual(*entry);
        }
        entries_.clear();
    } catch (...) {
    }
    availability_subscription_.disconnect();
    presentation_subscription_.disconnect();
    root_bounds_subscription_.disconnect();
    tag_.reset();
    window_lifetime_.reset();
}

namespace {

// The events ErrorProvider adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 4> error_provider_interactions{{
    {.name = "right_to_left_changed", .payload = "bool", .mutable_payload = false},
    {.name = "data_source_changed", .payload = "", .mutable_payload = false},
    {.name = "data_member_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "error_changed", .payload = "ErrorProviderChange", .mutable_payload = false},
}};

} // namespace

void ErrorProvider::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), error_provider_interactions.begin(), error_provider_interactions.end());
}

} // namespace gui_forms
