// GF_TABLE_SUBSURFACES (since 1.1): threaded sub-surfaces over the C++
// SubSurfaceHost, whose content a C callback builds on the sub-surface's
// own thread. See gf_abi.h and notes:planning THREADED_SUBSURFACES_DESIGN.md
// section 11.

#include "abi_tables.hpp"

#include "../registry/abi_registry.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity set_content_slot{GF_TABLE_SUBSURFACES, 0U};
constexpr SlotIdentity set_root_slot{GF_TABLE_SUBSURFACES, 1U};
constexpr SlotIdentity state_slot{GF_TABLE_SUBSURFACES, 2U};
constexpr SlotIdentity content_epoch_slot{GF_TABLE_SUBSURFACES, 3U};
constexpr SlotIdentity failure_slot{GF_TABLE_SUBSURFACES, 4U};

// The 1.1 size of gf_subsurface_content.
constexpr std::uint32_t content_size_1_1 = 40U;
static_assert(sizeof(gf_subsurface_content) == content_size_1_1);

class AbiSubSurfaceContent;

// The content whose build callback runs on this thread now; set_root hands
// its root to it. Null outside a build.
thread_local AbiSubSurfaceContent* building_content = nullptr;

// What a C caller's gf_subsurface_content describes, run as a C++
// SubSurfaceContent. Made on the UI thread by set_content; moved to the
// sub-surface's thread when the host starts it, and destroyed there (or on
// the UI thread when it never started).
//
// On its thread it makes that thread a registry domain before the build
// callback can create an object, so every object created there belongs to
// the sub-surface; it gives the domain the sub-surface's dispatcher once
// the root exists; and its destructor ends the domain, destroying the
// domain's objects on that thread after the sub-surface released its tree.
class AbiSubSurfaceContent final : public SubSurfaceContent {
public:
    explicit AbiSubSurfaceContent(const gf_callback& build) : build_(std::make_unique<CallbackBinding>(build)) {}

    ~AbiSubSurfaceContent() override {
        root_.reset();
        if (domain_) {
            registry().end_domain(*domain_);
        }
        // The build callback's release, if the build never ran, goes here.
        build_.reset();
    }

    AbiSubSurfaceContent(const AbiSubSurfaceContent&) = delete;
    AbiSubSurfaceContent& operator=(const AbiSubSurfaceContent&) = delete;

    [[nodiscard]] Control::Ptr build_root() override {
        domain_ = registry().begin_domain();
        const BuildScope scope(*this);
        gf_event event{};
        event.struct_size = sizeof(gf_event);
        event.sender = 0U;
        event.name.data = "build";
        event.name.size = 5U;
        (*build_).invoke(event);
        // Released after its one invoke, on this thread.
        build_.reset();
        Control::Ptr built = std::move(root_);
        return built;
    }

    void root_started(const WindowDispatcher& dispatcher) override {
        if (domain_) {
            registry().set_domain_wake(*domain_, dispatcher);
        }
    }

    void hand_over(Control::Ptr root) noexcept { root_ = std::move(root); }

private:
    // Marks the content as building on this thread for the callback's
    // duration, so a set_root inside finds it, whatever the callback does.
    class BuildScope final {
    public:
        explicit BuildScope(AbiSubSurfaceContent& content) noexcept : previous_(building_content) {
            building_content = &content;
        }
        ~BuildScope() { building_content = previous_; }
        BuildScope(const BuildScope&) = delete;
        BuildScope& operator=(const BuildScope&) = delete;

    private:
        AbiSubSurfaceContent* previous_;
    };

    std::unique_ptr<CallbackBinding> build_;
    std::optional<ThreadDomain> domain_;
    Control::Ptr root_;
};

// The SubSurfaceHost behind host, on the UI thread.
gf_status enter_host(gf_object host, SlotIdentity slot, std::shared_ptr<SubSurfaceHost>& found) {
    const gf_status entered = registry().enter_ui(slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Control::Ptr control;
    const gf_status looked_up = registry().find_object(host, slot, control);
    if (looked_up != GF_STATUS_OK) {
        return looked_up;
    }
    std::shared_ptr<SubSurfaceHost> typed = std::dynamic_pointer_cast<SubSurfaceHost>(control);
    if (!typed) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the object is not a sub_surface_host");
    }
    if (!(*typed).is_alive()) {
        return fail(GF_STATUS_DISPOSED, slot, "the object has been disposed");
    }
    found = std::move(typed);
    return GF_STATUS_OK;
}

gf_subsurface_state state_value(SubSurfaceState state) noexcept {
    switch (state) {
    case SubSurfaceState::starting:
        return GF_SUBSURFACE_STATE_STARTING;
    case SubSurfaceState::running:
        return GF_SUBSURFACE_STATE_RUNNING;
    case SubSurfaceState::failed:
        return GF_SUBSURFACE_STATE_FAILED;
    case SubSurfaceState::stopped:
        return GF_SUBSURFACE_STATE_STOPPED;
    default:
        // A state this table does not know reads as no thread.
        return GF_SUBSURFACE_STATE_STOPPED;
    }
}

gf_status GF_CALL subsurfaces_set_content(gf_object host, const gf_subsurface_content* content) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_content_slot.table_id, set_content_slot.slot_index);
        gf_callback build{};
        bool opaque = false;
        if (content != nullptr) {
            if ((*content).struct_size < content_size_1_1 || (*content).opaque > 1U) {
                return fail(GF_STATUS_INVALID_ARGUMENT, set_content_slot,
                            "a gf_subsurface_content needs struct_size of at least 40 and opaque 0 or 1");
            }
            const gf_status read = read_callback(&(*content).build, set_content_slot, build);
            if (read != GF_STATUS_OK) {
                return read;
            }
            opaque = (*content).opaque == 1U;
        }
        std::shared_ptr<SubSurfaceHost> found;
        const gf_status entered = enter_host(host, set_content_slot, found);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        std::unique_ptr<SubSurfaceContent> made;
        if (content != nullptr) {
            made = std::make_unique<AbiSubSurfaceContent>(build);
        }
        // From here the content (and its callback) is the host's; a throw
        // below destroys it, releasing the callback, as a taken callback.
        (*found).set_content(std::move(made), opaque);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_content_slot);
    }
}

gf_status GF_CALL subsurfaces_set_root(gf_object root) noexcept {
    try {
        const ShadowFrameScope shadow_frame(set_root_slot.table_id, set_root_slot.slot_index);
        const gf_status entered = registry().enter_ui(set_root_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        if (building_content == nullptr) {
            return fail(GF_STATUS_INVALID_STATE, set_root_slot,
                        "set_root is called inside a sub-surface's build callback only");
        }
        Control::Ptr control;
        const gf_status found = registry().find_object(root, set_root_slot, control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        if (!(*control).is_alive()) {
            return fail(GF_STATUS_DISPOSED, set_root_slot, "the object has been disposed");
        }
        if ((*control).parent()) {
            return fail(GF_STATUS_INVALID_STATE, set_root_slot, "a sub-surface's root has no parent");
        }
        (*building_content).hand_over(std::move(control));
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(set_root_slot);
    }
}

gf_status GF_CALL subsurfaces_state(gf_object host, gf_subsurface_state* state) noexcept {
    try {
        const ShadowFrameScope shadow_frame(state_slot.table_id, state_slot.slot_index);
        if (state == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, state_slot, "state needs an output");
        }
        std::shared_ptr<SubSurfaceHost> found;
        const gf_status entered = enter_host(host, state_slot, found);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *state = state_value((*found).state());
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(state_slot);
    }
}

gf_status GF_CALL subsurfaces_content_epoch(gf_object host, std::uint64_t* epoch) noexcept {
    try {
        const ShadowFrameScope shadow_frame(content_epoch_slot.table_id, content_epoch_slot.slot_index);
        if (epoch == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, content_epoch_slot, "content_epoch needs an output");
        }
        std::shared_ptr<SubSurfaceHost> found;
        const gf_status entered = enter_host(host, content_epoch_slot, found);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        *epoch = (*found).content_epoch();
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(content_epoch_slot);
    }
}

gf_status GF_CALL subsurfaces_failure(gf_object host, char* buffer, std::uint64_t capacity,
                                      std::uint64_t* required) noexcept {
    try {
        const ShadowFrameScope shadow_frame(failure_slot.table_id, failure_slot.slot_index);
        std::shared_ptr<SubSurfaceHost> found;
        const gf_status entered = enter_host(host, failure_slot, found);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        std::string text;
        if ((*found).state() == SubSurfaceState::failed) {
            text = (*found).failure();
        }
        const gf_status written = write_text(text, buffer, capacity, required, failure_slot);
        return written;
    } catch (...) {
        return fail_with_current_exception(failure_slot);
    }
}

constexpr gf_subsurfaces_table table{
    .struct_size = sizeof(gf_subsurfaces_table),
    .abi_minor = GF_ABI_MINOR,
    .set_content = &subsurfaces_set_content,
    .set_root = &subsurfaces_set_root,
    .state = &subsurfaces_state,
    .content_epoch = &subsurfaces_content_epoch,
    .failure = &subsurfaces_failure,
};

} // namespace

const gf_subsurfaces_table& subsurfaces_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
