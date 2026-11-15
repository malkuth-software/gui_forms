#pragma once

// The business card and the diagnostic overlay, one implementation for
// every GUI.Forms program (owner, 2026-11-15; the window manager
// specification's last section; decisions 1a-4a).
//
// What it replaces: Plan Paint's BusinessCard (a Button drawn by hand at
// the foot of its help book, with its own D-held flag fed from the
// editor's key preview), PlaySuite's BusinessCard (a page card beside the
// help book's dedication and a card held in the hand), the Gallery's
// Diagnostics group box (its counters and its "Show invalidation" check
// box) and PlaySuite's read-only settings row. Each program names its
// identity once (set_program_identity) and places or shows the library's
// card; the overlay is one process switch that the same gesture turns on
// and off.
//
// Version 1 is a flat card (decision 1a): drawn through the theme engine,
// lying on the page or held in the hand over the window, tilting slightly
// toward the pointer (its face leans toward it and its shadow away). The
// card turned in three dimensions comes later.
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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class Window;

// The program's identity, as the business card shows it. `vendor` is the
// display name printed large on the card (package names carry no vendor;
// this is display text only) and `website` the address the card's link
// names (https only; empty for no link). Every field is UTF-8 text; empty
// fields are left off the card.
struct GUI_FORMS_API ProgramIdentity final {
    std::string product{};
    std::string version{};
    std::string vendor{};
    std::string website{};
    std::string copyright{};
    std::string credits{};
};

// Sets the process's identity, read by every card when it lays out and
// paints. Validated whole before it replaces the previous identity: text
// that is not valid UTF-8, or a website that is neither empty nor an
// https:// address, throws std::invalid_argument and leaves the identity as
// it was. A program sets it before it shows a card; a card already shown
// shows the new identity when it next paints. UI thread.
GUI_FORMS_API void set_program_identity(const ProgramIdentity& identity);
// The identity last set; every field empty before the first set. The
// reference stays valid until the next set_program_identity. UI thread.
[[nodiscard]] GUI_FORMS_API const ProgramIdentity& program_identity() noexcept;

// The optional remark source (decision 2a): a program that speaks through
// its card (PlaySuite's remarks) names a function that writes one remark
// for the card in the hand into `remark` (cleared before the call; UTF-8;
// the card shows at most maximum_business_card_remark bytes, cut at a
// character boundary). It is called on the UI thread each time a card is
// shown in the hand; `context` is passed back and must stay valid until
// the source is replaced or cleared. An empty source gives no remark.
struct GUI_FORMS_API BusinessCardRemarkSource final {
    void* context{nullptr};
    void (*write_remark)(void* context, std::string& remark){nullptr};
};
inline constexpr std::size_t maximum_business_card_remark = 160U;
// UI thread.
GUI_FORMS_API void set_business_card_remark_source(BusinessCardRemarkSource source) noexcept;
[[nodiscard]] GUI_FORMS_API BusinessCardRemarkSource business_card_remark_source() noexcept;

// The diagnostic overlay: the invalidation outlines
// (invalidation_overlay.hpp) and, at the top left, the counters card in
// each window's overlay plane (renderer, controls and stable ids, measure
// and arrange passes, paints and damage rectangles, input and focus, wakes
// and timer ticks, presents and the worst present: the figures the
// Gallery's Diagnostics panel shows). The counters card shows whenever the
// overlay is on (decision 3a). It is the process switch:
// set_diagnostic_overlay(true) sets set_invalidation_overlay_default(true),
// so every window without a setting of its own shows both; false clears
// it. It draws nothing into the program's controls and takes no input; a
// window shows the change at its next present. Any thread.
GUI_FORMS_API void set_diagnostic_overlay(bool enabled) noexcept;
[[nodiscard]] GUI_FORMS_API bool diagnostic_overlay() noexcept;
// Turns the overlay to the other state and returns the new one (the
// gesture's action). Two threads toggling at once may both read the same
// state; the switch then ends in the state both wrote.
GUI_FORMS_API bool toggle_diagnostic_overlay() noexcept;

// Where a card is shown. `page`: lying still in a host control (a help
// page, an About pane), casting its shadow and rising a little under the
// pointer; a click, Enter or Space asks to open the card in the hand.
// `hand`: alone over the dimmed window (show_business_card); it tilts
// toward the pointer, the vendor name turns hot under it, a click on the
// website asks for the website, and Escape, its close mark or a click off
// the card puts it away.
enum class BusinessCardPresentation : std::uint8_t {
    page,
    hand,
};

// The argument of BusinessCard::open_requested and website_requested: a
// handler that answers the request itself sets handled.
struct GUI_FORMS_API BusinessCardRequest final {
    bool handled{false};
};

// The card. Drawn through the theme engine in the card family (parts:
// region, shadow, stock, vendor_name, rule, product, website, remark,
// close_mark, focus), so a theme gives it its stock and inks. Its text is
// the process identity's: the vendor large, a rule, the product and
// version, the website, and in the hand the remark (or, without one, the
// copyright). Its accessible name is "<vendor> business card", with the
// credits in its description; the press action opens it (page) or asks
// for the website (hand), and in the hand the close action puts it away.
class GUI_FORMS_API BusinessCard final : public Control {
public:
    explicit BusinessCard(StableId stable_id,
                          BusinessCardPresentation presentation = BusinessCardPresentation::page);
    ~BusinessCard() override;

    [[nodiscard]] BusinessCardPresentation presentation() const noexcept { return presentation_; }

    // On the page: the card asked to be opened in the hand (a click, Enter
    // or Space). Unhandled, the card calls show_business_card on its
    // window.
    [[nodiscard]] Event<BusinessCardRequest&>& open_requested() noexcept { return open_requested_; }
    // In the hand: the card was put away (Escape, its close mark, a click
    // off it, or hide_business_card).
    [[nodiscard]] Event<>& put_away() noexcept { return put_away_; }
    // The website was asked for (a click on it in the hand, or the press
    // action there). GUI.Forms has no host service that opens an address
    // yet: a program opens it in a handler and sets handled; unhandled, the
    // request ends here.
    [[nodiscard]] Event<BusinessCardRequest&>& website_requested() noexcept { return website_requested_; }

    // The card's stock, in this control's coordinates, before the tilt: the
    // control keeps a margin around it for the shadow and the tilt.
    [[nodiscard]] Rect card_bounds() const noexcept;
    // The band of the vendor name, in this control's coordinates, before
    // the tilt: the gesture and the hot vendor name count there.
    [[nodiscard]] Rect vendor_name_band() const noexcept;
    // The website line (empty without a website) and the close mark (hand
    // only; empty on the page), before the tilt.
    [[nodiscard]] Rect website_band() const noexcept;
    [[nodiscard]] Rect close_mark_bounds() const noexcept;
    // The face's offset toward the pointer, in logical units (the tilt):
    // zero with the pointer away or under reduced motion.
    [[nodiscard]] Point tilt() const noexcept { return tilt_; }
    // The remark the card in the hand shows (from the remark source when it
    // was shown; empty otherwise).
    [[nodiscard]] const std::string& remark() const& noexcept { return remark_; }
    const std::string& remark() const&& = delete;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    void on_key_bubble(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry shadow;
        PartGeometry stock;
        PartGeometry vendor_name;
        PartGeometry rule;
        PartGeometry product;
        PartGeometry website;
        PartGeometry remark;
        PartGeometry close_mark;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // show_business_card's and hide_business_card's access to the card in
    // the hand (business_card.cpp).
    friend class BusinessCardShowing;

    // The card's layout at its current size, before the tilt.
    struct Layout final {
        Rect card{};
        Rect vendor_name{};
        Rect rule{};
        Rect product{};
        Rect website{};
        Rect remark{};
        Rect close_mark{};
    };
    [[nodiscard]] Layout layout() const noexcept;
    [[nodiscard]] Size card_size() const noexcept;
    [[nodiscard]] bool reduced_motion() const noexcept;
    void follow_pointer(Point local_point);
    void ask_to_open();
    void ask_for_website();
    void put_self_away();
    void outside_pressed();
    void announce_put_away();

    // Where the primary press that is down began.
    enum class PressTarget : std::uint8_t {
        none,
        card,
        website,
        close_mark,
    };
    [[nodiscard]] PressTarget target_at(Point local_point) const noexcept;

    BusinessCardPresentation presentation_{BusinessCardPresentation::page};
    Point tilt_{};
    bool vendor_hot_{false};
    bool website_hot_{false};
    bool close_hot_{false};
    PressTarget press_target_{PressTarget::none};
    std::string remark_{};
    Event<BusinessCardRequest&> open_requested_{};
    Event<> put_away_{};
    Event<BusinessCardRequest&> website_requested_{};
};

// show_business_card(window) shows the card in the hand over `window`, in
// a modal layer of the window's own (so it works the same on every host):
// the window behind is dimmed and takes no input until the card is put
// away. It returns false, changing nothing, when the window already shows
// it. The window capsule's help entry (the surface manager's default
// capsule help target) and a page card's open_requested call it.
// hide_business_card(window) puts the card away (the card emits put_away,
// whose handlers may throw); it does nothing when the window shows none.
// Both UI thread.
GUI_FORMS_API bool show_business_card(Window& window);
GUI_FORMS_API void hide_business_card(Window& window);
[[nodiscard]] GUI_FORMS_API bool business_card_shown(const Window& window) noexcept;
// The card the window shows in the hand, else null (for tests and
// inspection). UI thread.
[[nodiscard]] GUI_FORMS_API std::shared_ptr<BusinessCard> shown_business_card(const Window& window) noexcept;

} // namespace gui_forms
