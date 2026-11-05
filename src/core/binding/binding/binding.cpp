#include "gui_forms/binding.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"

#include "../support/binding_support.hpp"

#include <array>
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms {

Binding::Binding(Control& target, std::string property_name,
                 std::shared_ptr<BindingSource> source,
                 std::string data_member, BindingOptions options)
    : target_(&target), source_(source),
      property_name_(registered_name(property_name)),
      data_member_(canonical_binding_name(data_member)),
      options_(std::move(options)) {
    detail::validate_binding_options(options_);
    if (!source || !(*source).is_alive()) {
        throw std::invalid_argument("GUI.Forms Binding requires a live BindingSource");
    }
    if (!target.is_alive()) {
        throw std::invalid_argument("GUI.Forms Binding requires a live target Control");
    }
    if (!target.has_bindable_property(property_name_)) {
        throw std::invalid_argument("GUI.Forms target property is not bindable: " +
                                    property_name_);
    }
}

Binding::~Binding() {
    if (is_alive()) {
        try { dispose(); } catch (...) {}
    }
}

void Binding::SourceChangedCallback::operator()(
    const BindingListChange& change) const {
    if (const std::shared_ptr<Binding> self = binding.lock()) {
        (*self).source_changed(change);
    }
}

void Binding::SourceDisposedCallback::operator()() const {
    if (const std::shared_ptr<Binding> self = binding.lock()) {
        (*self).source_changed_.disconnect();
        (*self).target_changed_.disconnect();
        (*self).target_validating_.disconnect();
        (*self).active_ = false;
        (*self).source_.reset();
    }
}

void Binding::TargetChangedCallback::operator()() const {
    if (const std::shared_ptr<Binding> self = binding.lock()) {
        (*self).target_changed();
    }
}

void Binding::TargetValidatingCallback::operator()(
    ControlValidationEvent& event) const {
    if (const std::shared_ptr<Binding> self = binding.lock();
        self && (*self).active_ &&
        (*self).options_.data_source_update_mode ==
            DataSourceUpdateMode::on_validation) {
        event.cancel = !(*self).validate() || event.cancel;
    }
}

void Binding::start() {
    if (active_) return;
    const std::shared_ptr<gui_forms::BindingSource> source = source_.lock();
    if (!target_ || !(*target_).is_alive() || !source || !(*source).is_alive()) {
        throw std::logic_error("GUI.Forms cannot start a binding with a retired endpoint");
    }
    // The target's property, from its registry or its type's table.
    const std::optional<PropertyDescriptor> property = (*target_).property_descriptor(property_name_);
    if (!property || !(*property).bindable || !(*property).readable) {
        throw std::invalid_argument("GUI.Forms Binding target property is not readable");
    }
    const std::weak_ptr<Binding> weak = weak_from_this();
    source_changed_ = (*source).model_changed_.subscribe(*this,
        SourceChangedCallback{weak});
    source_disposed_ = (*source).disposed_event().subscribe(
        *this, SourceDisposedCallback{weak});
    if ((*property).change_notifications) {
        target_changed_ = (*target_).subscribe_property_changed(property_name_, *this, TargetChangedCallback{weak});
    }
    target_validating_ = (*target_).validating().subscribe(
        *this, TargetValidatingCallback{weak});
    active_ = true;
    (*source).register_binding(shared_from_this());
    if (options_.control_update_mode == ControlUpdateMode::on_property_changed) {
        static_cast<void>(update_control(true));
    }
}

void Binding::set_options(BindingOptions options) {
    detail::validate_binding_options(options);
    if (target_ && (*target_).attached_window()) {
        (*(*target_).attached_window()).verify_access("Binding options mutation");
    }
    options_ = std::move(options);
    if (active_ &&
        options_.control_update_mode == ControlUpdateMode::on_property_changed) {
        static_cast<void>(update_control(true));
    }
}

bool Binding::read_value() { return update_control(false); }
bool Binding::write_value() { return update_source(false); }
bool Binding::validate() {
    return options_.data_source_update_mode == DataSourceUpdateMode::never
        ? true : update_source(false);
}

void Binding::source_changed(const BindingListChange&) {
    if (!active_ || updating_ ||
        options_.control_update_mode == ControlUpdateMode::never) return;
    static_cast<void>(update_control(true));
}

void Binding::target_changed() {
    if (!active_) return;
    if (updating_) {
        ++suppressed_reentrant_updates_;
        return;
    }
    if (options_.data_source_update_mode ==
        DataSourceUpdateMode::on_property_changed) {
        static_cast<void>(update_source(true));
    }
}

bool Binding::update_control(bool automatic) {
    if (!active_) return false;
    if (updating_) {
        ++suppressed_reentrant_updates_;
        return false;
    }
    if (automatic && options_.control_update_mode == ControlUpdateMode::never) {
        return false;
    }
    const std::shared_ptr<gui_forms::Binding> self = shared_from_this();
    const std::shared_ptr<gui_forms::BindingSource> source = source_.lock();
    std::optional<PropertyDescriptor> property{};
    if (target_ && (*target_).is_alive()) property = (*target_).property_descriptor(property_name_);
    if (!source || !(*source).is_alive() || !target_ || !(*target_).is_alive() ||
        !property || !(*property).bindable || !(*property).writable) {
        ++failed_updates_;
        return complete(BindingCompleteContext::control_update,
                        BindingCompleteState::data_error,
                        detail::binding_failure_text(
                            "control update", property_name_, data_member_));
    }
    const std::optional<BindingValue> source_value = (*source).current_field(data_member_);
    if (!source_value) {
        ++failed_updates_;
        return complete(BindingCompleteContext::control_update,
                        BindingCompleteState::data_error,
                        "GUI.Forms binding source field is unavailable: " + data_member_);
    }
    try {
        updating_ = true;
        BindingValue proposed = *source_value;
        if (std::holds_alternative<std::monostate>(proposed) &&
            !std::holds_alternative<std::monostate>(options_.null_value)) {
            proposed = options_.null_value;
        }
        if (options_.formatting_enabled) {
            BindingConvertEvent event{
                proposed, (*property).kind, false};
            format_.emit(event);
            proposed = std::move(event.value);
            if (!event.handled && !options_.format_string.empty()) {
                proposed = detail::format_binding_value(
                    proposed, options_.format_string);
            }
        }
        const std::optional<BindingValue> converted = convert_property_value(
            proposed, *property);
        if (!converted) {
            throw std::invalid_argument("binding value cannot convert to target kind");
        }
        (*target_).set_property_value(property_name_, *converted);
        ++control_reads_;
        ++successful_updates_;
        updating_ = false;
    } catch (const std::exception& error) {
        updating_ = false;
        ++failed_updates_;
        return complete(BindingCompleteContext::control_update,
                        BindingCompleteState::exception, error.what());
    } catch (...) {
        updating_ = false;
        ++failed_updates_;
        return complete(BindingCompleteContext::control_update,
                        BindingCompleteState::exception,
                        "unknown binding control-update exception");
    }
    return complete(BindingCompleteContext::control_update,
                    BindingCompleteState::success);
}

bool Binding::update_source(bool automatic) {
    if (!active_) return false;
    if (updating_) {
        ++suppressed_reentrant_updates_;
        return false;
    }
    if (automatic && options_.data_source_update_mode !=
                         DataSourceUpdateMode::on_property_changed) return false;
    const std::shared_ptr<gui_forms::Binding> self = shared_from_this();
    const std::shared_ptr<gui_forms::BindingSource> source = source_.lock();
    std::optional<PropertyDescriptor> property{};
    if (target_ && (*target_).is_alive()) property = (*target_).property_descriptor(property_name_);
    if (!source || !(*source).is_alive() || !target_ || !(*target_).is_alive() ||
        !property || !(*property).bindable || !(*property).readable) {
        ++failed_updates_;
        return complete(BindingCompleteContext::data_source_update,
                        BindingCompleteState::data_error,
                        detail::binding_failure_text(
                            "source update", property_name_, data_member_));
    }
    try {
        updating_ = true;
        const std::optional<BindingValue> current = (*target_).property_value(property_name_);
        if (!current) throw std::logic_error("binding target property is not readable");
        BindingValue proposed = *current;
        if (!std::holds_alternative<std::monostate>(options_.null_value) &&
            proposed == options_.null_value) {
            proposed = options_.data_source_null_value;
        }
        const std::optional<BindingValue> existing = (*source).current_field(data_member_);
        const BindingValueKind desired = existing
            ? binding_value_kind(*existing) : binding_value_kind(proposed);
        if (options_.formatting_enabled) {
            BindingConvertEvent event{proposed, desired, false};
            parse_.emit(event);
            const bool handled = event.handled;
            proposed = std::move(event.value);
            // Text shown under a number format ("1,234.50", "12.5 %") goes
            // back as the plain number it shows.
            const std::string* const shown = std::get_if<std::string>(&proposed);
            const bool number_kind = desired == BindingValueKind::number ||
                                     desired == BindingValueKind::signed_integer ||
                                     desired == BindingValueKind::unsigned_integer;
            if (!handled && shown != nullptr && number_kind && !options_.format_string.empty()) {
                const std::optional<NumberFormat> format = parse_number_format(options_.format_string);
                const std::optional<std::string> plain =
                    format ? plain_number_text(*shown, *format) : std::optional<std::string>{};
                if (plain) proposed = *plain;
            }
        }
        const std::optional<BindingValue> converted = convert_binding_value(proposed, desired);
        if (!converted) {
            throw std::invalid_argument("binding value cannot convert to source kind");
        }
        const bool changed = (*source).set_current_field(data_member_, *converted);
        ++source_writes_;
        ++successful_updates_;
        updating_ = false;
        if (!(changed || existing == converted)) return false;
    } catch (const std::exception& error) {
        updating_ = false;
        ++failed_updates_;
        return complete(BindingCompleteContext::data_source_update,
                        BindingCompleteState::exception, error.what());
    } catch (...) {
        updating_ = false;
        ++failed_updates_;
        return complete(BindingCompleteContext::data_source_update,
                        BindingCompleteState::exception,
                        "unknown binding source-update exception");
    }
    return complete(BindingCompleteContext::data_source_update,
                    BindingCompleteState::success);
}

bool Binding::complete(BindingCompleteContext context,
                       BindingCompleteState state, std::string error) {
    BindingCompleteEvent completion{this, context, state, std::move(error), false};
    if (options_.formatting_enabled) {
        binding_complete_.emit(completion);
        if (const std::shared_ptr<gui_forms::BindingSource> source = source_.lock(); source && (*source).is_alive()) {
            (*source).binding_complete().emit(completion);
        }
    }
    if (!completion.error_text.empty()) {
        if (const std::shared_ptr<gui_forms::BindingSource> source = source_.lock()) {
            try { (*source).data_error().emit(completion.error_text); } catch (...) {}
        }
    }
    return state == BindingCompleteState::success && !completion.cancel;
}

BindingSnapshot Binding::snapshot() const {
    return {property_name_, data_member_, control_reads_, source_writes_,
            successful_updates_, failed_updates_, suppressed_reentrant_updates_,
            active_};
}

void Binding::verify_dispose_thread() {
    if (target_ && (*target_).attached_window()) {
        (*(*target_).attached_window()).verify_access("Binding disposal");
    } else if (const std::shared_ptr<gui_forms::BindingSource> source = source_.lock()) {
        if (Window* owner = (*source).bound_window()) {
            (*owner).verify_access("Binding disposal");
        }
    }
}

void Binding::on_dispose() noexcept {
    active_ = false;
    if (const std::shared_ptr<gui_forms::BindingSource> source = source_.lock()) (*source).unregister_binding(this);
    source_changed_.disconnect();
    source_disposed_.disconnect();
    target_changed_.disconnect();
    target_validating_.disconnect();
    format_.disconnect_all();
    parse_.disconnect_all();
    binding_complete_.disconnect_all();
    source_.reset();
    target_ = nullptr;
}

namespace {

// The events Binding adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 3> binding_interactions{{
    {.name = "format", .payload = "BindingConvertEvent", .mutable_payload = true},
    {.name = "parse", .payload = "BindingConvertEvent", .mutable_payload = true},
    {.name = "binding_complete", .payload = "BindingCompleteEvent", .mutable_payload = true},
}};

} // namespace

void Binding::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), binding_interactions.begin(), binding_interactions.end());
}

} // namespace gui_forms
