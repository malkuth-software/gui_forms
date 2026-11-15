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
// The card is drawn through the theme engine, its stock a theme material
// (the default theme's is a bone paper stock: business-card::part(stock)),
// lying on the page or held in the hand over the window, tilting slightly
// toward the pointer (its face leans toward it and its shadow away). In
// the hand it also turns in three dimensions (PlaySuite and Plan Paint
// parity, 2026-11-15): dragged, it turns about its vertical axis, and a
// little about its horizontal one; let go, it springs to rest face up or
// face down, whichever is nearer along its spin; Left and Right turn it
// over. The turn's angles are motion values on the window's clock, so
// reduced motion ends each turn at once. Its two faces are painted flat
// through the theme engine into an offscreen raster of the host's renderer
// and drawn turned and lit (card_turn.hpp); a window whose host lends no
// offscreen renderer (turn_available() false) shows the flat card only.
// Around the card in the hand, remarks rise and fade while the pointer is
// over it (the remark source); and links open through the host's open-URL
// capability (HostCapability::open_url).
//
// The gesture: holding D while pressing the secondary button on the card's
// vendor name toggles the diagnostic overlay; pressing it again turns the
// overlay off, and the program keeps running. The key state is the
// window's (SurfaceRoot::is_key_down), so no program feeds it.
//
// Threading: everything here is used on the UI thread of the window
// concerned, except the overlay switch, which any thread may read or set.

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/export/application_export.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class PartRaster;
class SurfaceRoot;
class Window;

// The card's largest turn about its horizontal axis while dragged, in
// radians: it never turns over that way.
inline constexpr double maximum_business_card_pitch = 0.6;

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
// its card (PlaySuite's remarks, the ones made when such cards are
// compared) names a function that writes one remark into `remark` (cleared
// before the call; UTF-8; the card shows at most
// maximum_business_card_remark bytes, cut at a character boundary; text
// that is not UTF-8 is dropped). While the pointer is over the card in the
// hand, the card asks for a remark a moment after the pointer arrives and
// then every one to two and a half seconds, and shows each around the card
// (the remark part, clear of the card and of the others), rising and
// fading over two and a half to nine seconds, at most
// maximum_business_card_remarks at once. It is called on the UI thread;
// `context` is passed back and must stay valid until the source is
// replaced or cleared. An empty source gives no remarks, and the card
// shows its copyright line alone.
struct GUI_FORMS_API BusinessCardRemarkSource final {
    void* context{nullptr};
    void (*write_remark)(void* context, std::string& remark){nullptr};
};
inline constexpr std::size_t maximum_business_card_remark = 160U;
inline constexpr std::size_t maximum_business_card_remarks = 3U;
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

// What became of the last request for the website (BusinessCard::
// last_website_request): none yet; a handler answered it; the host took
// the address (HostServices::open_url accepted it); the window's host has
// no open-URL capability (or the card is in no window); or the host
// refused or failed it.
enum class BusinessCardLinkOutcome : std::uint8_t {
    none,
    handled,
    opened,
    unavailable,
    refused,
};

// The card. Drawn through the theme engine in the card family (parts:
// region, shadow, stock, vendor_name, rule, product, website, copyright,
// remark, close_mark, focus), so a theme gives it its stock and inks. Its
// text is the process identity's: the vendor large, a rule, the product
// and version, the website, and in the hand the copyright; the remarks lie
// around the card in the hand, never on it. Its accessible name is "<vendor> business card", with the
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
    // action there). A handler that opens it itself sets handled;
    // unhandled, the card hands the address to its window's host
    // (HostServices::open_url), where the host reports open_url.
    [[nodiscard]] Event<BusinessCardRequest&>& website_requested() noexcept { return website_requested_; }
    [[nodiscard]] BusinessCardLinkOutcome last_website_request() const noexcept { return last_website_request_; }

    // The heading band layout rule (page): the card lies under `band`, a
    // control of the same window drawn over the page above the card (a help
    // book's heading band), so that a page scrolled up carries the card
    // under the band rather than over it. The card and its shadow are
    // drawn, and take the pointer, only below the band's lower edge. The
    // card observes the band and never keeps it alive: a band destroyed or
    // in another window sets no rule. Null clears the rule. Ignored in the
    // hand. UI thread.
    void set_heading_band(const std::shared_ptr<const Control>& band);
    // The band's lower edge in this control's coordinates, or none without
    // a rule in force.
    [[nodiscard]] std::optional<double> heading_band_bottom() const noexcept;

    // The turn (hand): whether this card can show it (it is in a window
    // and, at its last try, its host lent an offscreen renderer for the
    // faces; a drag or Left and Right try again); the card's turn about its vertical
    // axis in radians (0 face up at rest, pi face down) and about its
    // horizontal axis (within plus or minus maximum_business_card_pitch);
    // and whether it shows its front.
    [[nodiscard]] bool turn_available() const;
    [[nodiscard]] double turn() const noexcept;
    [[nodiscard]] double pitch() const noexcept;
    [[nodiscard]] bool face_up() const noexcept;

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
    // The newest remark around the card in the hand (empty while none is
    // shown), and how many are shown.
    [[nodiscard]] const std::string& remark() const& noexcept;
    const std::string& remark() const&& = delete;
    [[nodiscard]] std::size_t remark_count() const noexcept { return remarks_.size(); }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    // In the hand, the remarks paint around the card: the outsets reach the
    // window's client edges.
    [[nodiscard]] Insets visual_outsets() const noexcept override;
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
        PartGeometry copyright;
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
        Rect copyright{};
        Rect close_mark{};
    };
    [[nodiscard]] Layout layout() const noexcept;
    // One face of the card as painted flat, in device pixels
    // (premultiplied BGRA8, rows tight), and the scale it was painted at.
    struct FacePixels final {
        std::vector<std::uint8_t> pixels{};
        std::uint32_t width{};
        std::uint32_t height{};
        double scale{};
    };
    // One remark around the card: its text, its box in this control's
    // coordinates, when it was said and how many seconds it stays.
    struct SaidRemark final {
        std::string text{};
        Rect box{};
        FrameTime born{};
        double life{};
    };
    // The remark timer's target. Observes the card.
    struct RemarkTick final {
        std::weak_ptr<BusinessCard> card;
        AnimationTickResult operator()(FrameTime now) const;
    };
    // Paints the face (`front`) or the back of the card laid out as
    // `placed`, moved by `offset`; `keyed` gives each part its engine key
    // (false for the offscreen faces, which are drawn statically).
    void paint_face(Painter& painter, const Layout& placed, Point offset, bool front, bool keyed) const;
    void paint_turned(Painter& painter, const Layout& placed);
    void paint_remarks(Painter& painter);
    // Paints both faces offscreen (the host's renderer); false, keeping
    // none, where the window lends no offscreen renderer.
    bool render_faces();
    bool render_face(SurfaceRoot& root, const Layout& at_origin, Size logical, double scale, bool front,
                     FacePixels& face) const;
    [[nodiscard]] bool turning() const noexcept;
    [[nodiscard]] bool faces_ready(double scale) const noexcept;
    // The point on the card's face (as laid out at rest) under
    // `local_point` while the card is turned, or none off the card or on
    // its back.
    [[nodiscard]] std::optional<Point> face_point(Point local_point) const noexcept;
    void begin_turn_gesture();
    void settle_turn();
    void turn_over(double direction);
    [[nodiscard]] FrameTime card_now() const noexcept;
    AnimationTickResult on_remark_tick(FrameTime now);
    void start_remarks();
    void say_remark(FrameTime now);
    [[nodiscard]] Rect remark_room() const noexcept;
    [[nodiscard]] double remark_opacity(const SaidRemark& remark, FrameTime now) const noexcept;
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
    // The turn (hand): radians about the vertical axis and the horizontal
    // one, on the window's motion clock.
    MotionValue turn_;
    MotionValue pitch_;
    // The drag that turns the card: where it began, the angles then, the
    // last sample and the turn's speed (radians a second) for the release.
    bool dragging_{false};
    Point drag_origin_{};
    double drag_turn_{};
    double drag_pitch_{};
    Point drag_last_{};
    FrameTime drag_last_time_{};
    double drag_speed_{};
    // The faces could not be painted offscreen at the last try.
    bool faces_refused_{false};
    FacePixels front_face_{};
    FacePixels back_face_{};
    // The turned picture, reused while nothing else holds it.
    std::shared_ptr<PartRaster> picture_{};
    // The remarks around the card in the hand, oldest first, and when the
    // next is due; their xorshift state for placement and timing.
    std::vector<SaidRemark> remarks_{};
    FrameTime next_remark_{};
    bool pointer_over_{false};
    std::uint32_t remark_seed_{0x51ED2701U};
    AnimationTimer remark_frames_{};
    std::weak_ptr<const Control> heading_band_{};
    BusinessCardLinkOutcome last_website_request_{BusinessCardLinkOutcome::none};
    Point tilt_{};
    bool vendor_hot_{false};
    bool website_hot_{false};
    bool close_hot_{false};
    PressTarget press_target_{PressTarget::none};
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
