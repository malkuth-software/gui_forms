#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/component/failures/component_failures.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "../drag_engine/drag_engine.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"
#include "requests/alert_route.hpp"

#include "../dispatcher/state/dispatcher_state.hpp"
// The constructor destroys its built members if a later one throws, so every
// member held through a forward-declared type must be complete here.
#include "../window/tool_tips/tool_tip_presenter.hpp"
#include "../window/interaction/interaction_state.hpp"

#include <stdexcept>
#include <string>
#include <stxlib/threads.hpp>

namespace gui_forms {

// The layout marker of the SurfaceRoot this library compiles
// (detail::SurfaceRootLayout, surface_root.hpp): a unit that sees another
// layout refers to a marker that is defined nowhere, and does not link.
// The member is defined for the primary template here only and instantiated
// for this library's layout; an explicit specialization would come after the
// header's own use of the marker has instantiated it.
template <std::size_t Size, std::size_t Alignment>
const int detail::SurfaceRootLayout<Size, Alignment>::marker = 1;
template struct detail::SurfaceRootLayout<sizeof(SurfaceRoot), alignof(SurfaceRoot)>;

SurfaceRoot::SurfaceRoot(Control::Ptr root, Size client_size, std::shared_ptr<ApplicationResources> resources)
    : root_thread_(stxlib::current_thread()),
      dispatcher_state_(gui_forms::detail::make_library_shared<detail::DispatcherState>(root_thread_)),
      root_(std::move(root)), client_size_(client_size),
      theme_(resources ? (*resources).theme_ptr() : default_theme()),
      resources_(std::move(resources)),
      lifetime_(gui_forms::detail::make_library_shared<detail::WindowLifetime>()) {
    // A theme with no light variant shows GUI.Forms' own light theme.
    if (resources_) theme_ = scheme_theme((*resources_).theme_ptr());
}

Window* WindowRef::get() const noexcept {
    const std::shared_ptr<detail::WindowLifetime> locked = lifetime_.lock();
    if (!locked) return nullptr;
    Window* const window = (*locked).window;
    return window;
}

WindowRef SurfaceRoot::window_ref() const {
    const std::weak_ptr<detail::WindowLifetime> observed = lifetime_;
    const WindowRef made(observed);
    return made;
}

bool SurfaceRoot::check_access() const noexcept {
    return stxlib::current_thread() == root_thread_;
}

void SurfaceRoot::verify_access(std::string_view operation) {
    require_root_thread(operation);
}

void SurfaceRoot::require_root_thread(std::string_view operation) {
    if (stxlib::current_thread() == root_thread_) {
        return;
    }
    metrics_.record_wrong_thread_rejection();
    throw WrongThreadError("GUI.Forms rejected wrong-thread " + std::string(operation));
}

DragEngine& SurfaceRoot::drag_engine() {
    require_root_thread("drag engine");
    if (!drag_engine_) drag_engine_ = std::make_unique<DragEngine>(*this);
    return *drag_engine_;
}

SurfaceRoot::DamageCauseScope::DamageCauseScope(SurfaceRoot& root, InvalidationCause cause,
                                                const Control* source) noexcept
    : root_(&root), cause_(root.damage_cause_), source_(root.damage_source_) {
    root.damage_cause_ = cause;
    root.damage_source_ = source;
}

SurfaceRoot::DamageCauseScope::~DamageCauseScope() {
    (*root_).damage_cause_ = cause_;
    (*root_).damage_source_ = source_;
}

} // namespace gui_forms
