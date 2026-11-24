#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <utility>
#include <variant>

namespace gui_forms {

// The lease of an overlay a control shows: the token that keeps its layer
// in the window, the subscription to the lease's revocation, and the focus
// scope begun for it. The scope is empty while the presentation is opening
// (PresentationState), because beginning it runs focus handlers.
struct GUI_FORMS_API OverlayLease final {
    OverlayToken overlay{};
    SubscriptionToken overlay_closed{};
    FocusScopeId scope{};
};

// Where a popup, drop-down or overlay a control shows stands, with what
// each phase holds (owner, 2026-11-23: one presentation state instead of a
// flag beside the pointers and tokens it qualifies):
// - closed: nothing is held;
// - opening: `Held` is built and its overlay is in the window, and the
//   control is beginning its focus scope, which runs focus and validation
//   handlers; a second open is refused, a close is served;
// - open: `Held` is complete, its scope begun;
// - closing: `Held` is being dismantled; a second open or close is refused
//   until it is closed, so a handler run by the teardown cannot open a
//   second presentation over the first's remains.
// `Held` is the owner's record of one presentation (its layer, content,
// lease and subscriptions); a contradictory combination of those fields
// and the phase cannot be represented. Each control states which phases
// its public queries report as open. UI thread only.
template <typename Held>
class PresentationState final {
public:
    PresentationState() noexcept : state_(Closed{}) {}
    PresentationState(PresentationState&& other) noexcept : state_(std::move(other.state_)) {
        other.state_ = Closed{};
    }
    PresentationState& operator=(PresentationState&& other) noexcept {
        if (this != &other) {
            state_ = std::move(other.state_);
            other.state_ = Closed{};
        }
        return *this;
    }
    PresentationState(const PresentationState&) = delete;
    PresentationState& operator=(const PresentationState&) = delete;

    // The named states.
    [[nodiscard]] static PresentationState closed() noexcept {
        PresentationState result{};
        return result;
    }
    [[nodiscard]] static PresentationState opening(Held held) noexcept {
        PresentationState result{};
        result.state_ = Opening{std::move(held)};
        return result;
    }
    [[nodiscard]] static PresentationState open(Held held) noexcept {
        PresentationState result{};
        result.state_ = Open{std::move(held)};
        return result;
    }

    // The phase.
    [[nodiscard]] bool is_closed() const noexcept { return std::holds_alternative<Closed>(state_); }
    [[nodiscard]] bool is_opening() const noexcept { return std::holds_alternative<Opening>(state_); }
    [[nodiscard]] bool is_open() const noexcept { return std::holds_alternative<Open>(state_); }
    [[nodiscard]] bool is_closing() const noexcept { return std::holds_alternative<Closing>(state_); }
    // Opening or open: shown, as the controls' shown and open queries mean
    // it while the scope is being begun.
    [[nodiscard]] bool is_shown() const noexcept { return is_opening() || is_open(); }

    // What the phase holds; null in any other phase.
    [[nodiscard]] Held* opening_state() noexcept {
        Opening* const phase = std::get_if<Opening>(&state_);
        return phase != nullptr ? &(*phase).held : nullptr;
    }
    [[nodiscard]] Held* open_state() noexcept {
        Open* const phase = std::get_if<Open>(&state_);
        return phase != nullptr ? &(*phase).held : nullptr;
    }
    [[nodiscard]] const Held* open_state() const noexcept {
        const Open* const phase = std::get_if<Open>(&state_);
        return phase != nullptr ? &(*phase).held : nullptr;
    }
    [[nodiscard]] Held* closing_state() noexcept {
        Closing* const phase = std::get_if<Closing>(&state_);
        return phase != nullptr ? &(*phase).held : nullptr;
    }
    // Opening or open: the record of the presentation that is shown.
    [[nodiscard]] Held* shown_state() noexcept {
        Held* const opening_held = opening_state();
        if (opening_held != nullptr) return opening_held;
        return open_state();
    }
    [[nodiscard]] const Held* shown_state() const noexcept {
        const Opening* const phase = std::get_if<Opening>(&state_);
        if (phase != nullptr) return &(*phase).held;
        return open_state();
    }

    // Opening becomes open, keeping its record. False, changing nothing,
    // in any other phase (a handler closed the presentation meanwhile).
    bool settle_open() noexcept {
        Opening* const phase = std::get_if<Opening>(&state_);
        if (phase == nullptr) return false;
        Held held = std::move((*phase).held);
        state_ = Open{std::move(held)};
        return true;
    }
    // Opening or open becomes closing, keeping its record for the
    // teardown. False, changing nothing, when closed or closing already.
    bool begin_closing() noexcept {
        Held* const shown = shown_state();
        if (shown == nullptr) return false;
        Held held = std::move(*shown);
        state_ = Closing{std::move(held)};
        return true;
    }
    // Ends any phase: the record, with whatever leases it still holds, is
    // released (its tokens close and its subscriptions end as they are
    // destroyed).
    void finish_closing() noexcept { state_ = Closed{}; }

private:
    struct Closed final {};
    struct Opening final {
        Held held;
    };
    struct Open final {
        Held held;
    };
    struct Closing final {
        Held held;
    };
    std::variant<Closed, Opening, Open, Closing> state_;
};

// One opening of an anchored picker whose layer and content the control
// keeps between openings (ColorValueEditor, RibbonColorPicker,
// RibbonGallery): its lease, and the subscription to the picker Popup's
// closing while the content is lifted into a Popup (empty otherwise).
struct GUI_FORMS_API AnchoredOpening final {
    OverlayLease lease{};
    SubscriptionToken popup_closed{};
};

// The presentation of such a picker.
using AnchoredPresentation = PresentationState<AnchoredOpening>;

} // namespace gui_forms
