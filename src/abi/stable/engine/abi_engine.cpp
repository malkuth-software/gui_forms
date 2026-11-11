// The engine state behind GF_TABLE_THEMES and GF_TABLE_HOST. See
// abi_engine.hpp.

#include "abi_engine.hpp"

#include "../registry/abi_registry.hpp"
#include "../schema/abi_schema.hpp"

#include "gui_forms/detail/storage_growth.hpp"

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
        resources_ = std::make_shared<ApplicationResources>();
        registry().hold_binding();
    }
    return resources_;
}

void EngineState::adopt_application(std::unique_ptr<Application> made) {
    resources_ = (*made).resources();
    application_ = std::move(made);
    registry().hold_binding();
}

void EngineState::release_application() {
    std::shared_ptr<ApplicationResources> fresh = std::make_shared<ApplicationResources>();
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
    detail::reserve_one_more(published_names_);
    detail::reserve_one_more(published_);
    // Neither push can throw now that both have room.
    published_names_.push_back(std::move(copied_name));
    published_.push_back(std::move(made));
    return published_.back();
}

void EngineState::attach_owned_window(gf_object root, Window& window) {
    OpenOwnedWindow open{};
    open.root = root;
    open.window = &window;
    detail::reserve_one_more(owned_);
    owned_.push_back(open);
}

void EngineState::detach_owned_window(gf_object root) noexcept {
    std::size_t index = 0U;
    for (index = 0U; index < owned_.size(); ++index) {
        if (owned_[index].root == root) {
            owned_.erase(owned_.begin() + static_cast<std::ptrdiff_t>(index));
            return;
        }
    }
}

Window* EngineState::window_over(gf_object root, gf_object run_root) const noexcept {
    if (root == 0U || root == run_root) {
        return running_;
    }
    std::size_t index = 0U;
    for (index = 0U; index < owned_.size(); ++index) {
        if (owned_[index].root == root) {
            return owned_[index].window;
        }
    }
    return nullptr;
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
