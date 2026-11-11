// The engine state behind GF_TABLE_THEMES and GF_TABLE_HOST. See
// abi_engine.hpp.

#include "abi_engine.hpp"

#include "gui_forms/application/user_themes/user_themes.hpp"

#include "../registry/abi_registry.hpp"
#include "../schema/abi_schema.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {

ApplicationResources& EngineState::resources() {
    const std::shared_ptr<ApplicationResources>& shared = shared_resources();
    return *shared;
}

const std::shared_ptr<ApplicationResources>& EngineState::shared_resources() {
    if (!resources_) {
        std::shared_ptr<ApplicationResources> made = std::make_shared<ApplicationResources>();
        hold_theme_request(*made, default_theme_base);
        resources_ = std::move(made);
        registry().hold_binding();
    }
    return resources_;
}

void EngineState::hold_theme_request(ApplicationResources& resources, ThemeBase base) {
    // Read first, so a process started without one copies nothing.
    ThemeRequest request = process_theme_request();
    const bool asked = request.kind != ThemeRequestKind::none || !request.error.empty();
    if (!asked) return;
    std::unique_ptr<DeferredThemeRequest> held = std::make_unique<DeferredThemeRequest>();
    (*held).request = std::move(request);
    (*held).context.folders = process_theme_folders();
    (*held).context.library = library_;
    (*held).context.base = base;
    const std::span<const ControlAnatomy> types = controls();
    (*held).context.controls.assign(types.begin(), types.end());
    (*held).context.published_values.assign(published_names_.begin(), published_names_.end());
    resources.hold_theme_request(std::move(held));
}

void EngineState::adopt_application(std::unique_ptr<Application> made) {
    resources_ = (*made).resources();
    application_ = std::move(made);
    registry().hold_binding();
}

void EngineState::release_application() {
    std::shared_ptr<ApplicationResources> fresh = std::make_shared<ApplicationResources>();
    hold_theme_request(*fresh, default_theme_base);
    application_.reset();
    resources_ = std::move(fresh);
}

std::span<const ControlAnatomy> EngineState::controls() {
    if (!controls_built_) {
        const Schema& built = schema();
        std::vector<ControlAnatomy> made;
        made.reserve(built.types.size());
        for (const SchemaType& type : built.types) {
            if (type.anatomy != nullptr) {
                made.push_back(*type.anatomy);
            }
        }
        controls_ = std::move(made);
        controls_built_ = true;
    }
    return controls_;
}

void EngineState::replace_diagnostics(std::span<const ThemeCompileDiagnostic> findings) {
    std::vector<ThemeCompileDiagnostic> copied(findings.begin(), findings.end());
    diagnostics_ = std::move(copied);
}

namespace {

// Whether a kept owner's dependent has been destroyed.
struct DependentEnded final {
    bool operator()(const KeptOwner& kept) const noexcept {
        return kept.dependent.expired();
    }
};

} // namespace

void EngineState::keep_owner(const Component::Ptr& dependent, Component::Ptr owner) {
    // Owners are let go here, on the UI thread, outside any lock.
    const std::vector<KeptOwner>::iterator ended =
        std::remove_if(kept_owners_.begin(), kept_owners_.end(), DependentEnded{});
    kept_owners_.erase(ended, kept_owners_.end());
    KeptOwner kept{};
    kept.dependent = dependent;
    kept.owner = std::move(owner);
    kept_owners_.push_back(std::move(kept));
}

PublishedInput* EngineState::find_published(std::string_view name) noexcept {
    for (PublishedInput& input : published_) {
        if (input.name == name) {
            return &input;
        }
    }
    return nullptr;
}

PublishedInput& EngineState::add_published(std::string_view name, double value) {
    PublishedInput made;
    made.name = std::string(name);
    made.value = value;
    made.present = true;
    std::string copied_name(name);
    published_names_.reserve(published_names_.size() + 1U);
    published_.reserve(published_.size() + 1U);
    // Neither push can throw now that both have room.
    published_names_.push_back(std::move(copied_name));
    published_.push_back(std::move(made));
    return published_.back();
}

void EngineState::publish_all(Window& window) const {
    for (const PublishedInput& input : published_) {
        if (input.present) {
            static_cast<void>(window.publish_value(input.name, input.value));
        }
    }
}

EngineState& engine() {
    // Never destroyed: see abi_engine.hpp.
    static EngineState* const state = new EngineState();
    return *state;
}

std::unique_ptr<Window> make_run_window(const Control::Ptr& root, Size client_size) {
    EngineState& state = engine();
    std::unique_ptr<Window> window = std::make_unique<Window>(root, client_size, state.shared_resources());
    state.publish_all(*window);
    return window;
}

} // namespace gui_forms::abi1
