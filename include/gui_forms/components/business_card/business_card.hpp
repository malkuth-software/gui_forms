#pragma once

// The business card and the diagnostic overlay, one implementation for
// every GUI.Forms program (owner, 2026-11-15; the window manager
// specification's last section). PROPOSED API: declared for the
// coordinator's review, not yet implemented or built; no source includes
// this header until the implementation lands with its tests.
//
// What it replaces: Plan Paint's BusinessCard (a Button drawn by hand at
// the foot of its help book, with its own D-held flag fed from the
// editor's key preview), PlaySuite's BusinessCard (a page card beside the
// help book's dedication and a card held in the hand, turned in three
// dimensions), the Gallery's Diagnostics group box (its counters and its
// "Show invalidation" check box) and PlaySuite's read-only settings row.
// Each program now names its identity once (set_program_identity) and
// places or shows the library's card; the overlay is one process switch
// that the same gesture turns on and off.
//
// The gesture: holding D while pressing the secondary button on the card's
// vendor name toggles the diagnostic overlay; pressing it again turns the
// overlay off, and the program keeps running. The key state is the
// window's (SurfaceRoot::is_key_down), so no program feeds it.
//
// Threading: everything here is used on the UI thread of the window
// concerned, except the overlay switch, which any thread may read or set.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/export/application_export.hpp"

#include <cstdint>
#include <string>

namespace gui_forms {

class Window;

// The program's identity, as the business card and the About box show it.
// `vendor` is the display name printed large on the card (package names
// carry no vendor; this is display text only) and `website` the address a
// click on the card opens (https only; empty for no link). Every field is
// UTF-8 text; empty fields are left off the card.
struct GUI_FORMS_API ProgramIdentity final {
    std::string product{};
    std::string version{};
    std::string vendor{};
    std::string website{};
    std::string copyright{};
    std::string credits{};
};

// Sets the process's identity, read by every card and by an AboutDialog
// made without its own AboutInfo. Validated whole before it replaces the
// previous identity: text that is not valid UTF-8, or a website that is not
// an https URL, throws std::invalid_argument and leaves the identity as it
// was. Cards already shown repaint with the new identity.
GUI_FORMS_API void set_program_identity(const ProgramIdentity& identity);
// The identity last set; every field empty before the first set.
[[nodiscard]] GUI_FORMS_API const ProgramIdentity& program_identity() noexcept;

// The diagnostic overlay: the invalidation outlines
// (invalidation_overlay.hpp) and, beside them, a counters card in each
// window's overlay plane (renderer, controls, measure and arrange, paint
// and damage, input and focus, wake and ticks, present and worst frame,
// the text faces; the figures the Gallery's Diagnostics panel shows). It
// is the process switch: set_diagnostic_overlay(true) sets
// set_invalidation_overlay_default(true) and shows the counters card in
// every window without a setting of its own; false clears both. It draws
// nothing into the program's controls and takes no input. Any thread.
GUI_FORMS_API void set_diagnostic_overlay(bool enabled) noexcept;
[[nodiscard]] GUI_FORMS_API bool diagnostic_overlay() noexcept;
// Turns the overlay to the other state and returns the new one (the
// gesture's action, also offered as the help's "Diagnostic overlay" entry).
GUI_FORMS_API bool toggle_diagnostic_overlay() noexcept;

// Where a card is shown. `page`: lying still in a host control (a help
// page, an About pane), casting its shadow and rising a little under the
// pointer; a click or Enter opens the card in the hand. `hand`: over the
// whole window, alone on the dimmed window; the pointer turns it (a live
// drag), the vendor name turns to foil under the pointer and a click there
// opens the website; Escape, its close mark or a click off the card puts it
// away.
enum class BusinessCardPresentation : std::uint8_t {
    page,
    hand,
};

// The card. Drawn through the theme engine (its parts are the business-card
// family: stock, shadow, vendor_name, rule, website, close_mark, focus), so
// a theme gives it its stock, inks and finish; the default themes print
// cream stock with the vendor name large and the website beneath. Its text
// is the process identity's. Its accessible name is "<vendor> business
// card", with the website as its link action.
class GUI_FORMS_API BusinessCard final : public Control {
public:
    BusinessCard(StableId stable_id, BusinessCardPresentation presentation);
    ~BusinessCard() override;

    [[nodiscard]] BusinessCardPresentation presentation() const noexcept { return presentation_; }

    // On the page: the card asked to be opened in the hand (a click or
    // Enter). show_business_card answers it unless a handler set handled.
    [[nodiscard]] Event<bool&>& open_requested() noexcept { return open_requested_; }
    // In the hand: the card was put away.
    [[nodiscard]] Event<>& put_away() noexcept { return put_away_; }
    // The website was asked for (a click on it, or the link action). The
    // default opens it through the host's URL service; a handler that sets
    // handled opens it itself (or records it, in tests).
    [[nodiscard]] Event<bool&>& website_requested() noexcept { return website_requested_; }

    // The band of the vendor name, in this control's coordinates: the
    // gesture and the foil hover count there.
    [[nodiscard]] Rect vendor_name_band() const noexcept;

private:
    BusinessCardPresentation presentation_{BusinessCardPresentation::page};
    Event<bool&> open_requested_{};
    Event<> put_away_{};
    Event<bool&> website_requested_{};
};

// Shows the card in the hand over `window` (a modal layer of the window's
// own, so it works the same on every host). Returns false when the window
// already shows it. The capsule's help entry and a page card's
// open_requested call this.
GUI_FORMS_API bool show_business_card(Window& window);
// Puts the card away; does nothing when the window shows none.
GUI_FORMS_API void hide_business_card(Window& window) noexcept;
[[nodiscard]] GUI_FORMS_API bool business_card_shown(const Window& window) noexcept;

} // namespace gui_forms
