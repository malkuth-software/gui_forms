// The business card and the diagnostic overlay (business_card.hpp; owner
// decisions 1a-4a, 2026-11-15): the identity is validated whole and kept
// when refused; the overlay switch is the invalidation overlay's process
// switch and toggles; D held with a secondary press on the vendor name
// toggles it, and without D nothing changes; a page card asks to open and,
// unhandled, shows the card in the hand over its window with the remark
// source's remark; a handled request shows nothing; the card in the hand
// comes out once, asks for the website, and is put away by Escape, by a
// press beside it, by its close mark and by hide_business_card, each
// emitting put_away; its parts and semantics follow the identity.
//
// Parity with PlaySuite's and Plan Paint's cards (2026-11-15): the turn
// kernel draws the front at rest exactly as painted, the back mirrored and
// nothing beside the turned outline; the card in the hand turns over with
// Left and Right and by a drag, and settles on a half turn, only where the
// window lends an offscreen renderer; the remarks rise around the card in
// the hand while the pointer stays over it, at most three, never on the
// card; the copyright is a part of its own; the heading band rule hides
// and disarms the page card above the band and lapses with the band; the
// website goes to the host's open-URL service when no handler answers.
#include "../src/controls/business_card/card_turn.hpp"
#include "gui_forms/components/business_card/business_card.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "headless_host.hpp"
#include "support/fake_framebuffer.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override {}
    void draw_box_shadow(Rect, double, Point, double, double, Color) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "business card failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

struct CountRequests final {
    int* count;
    bool handle;
    void operator()(BusinessCardRequest& request) const {
        ++(*count);
        request.handled = handle;
    }
};

struct CountPutAway final {
    int* count;
    void operator()() const { ++(*count); }
};

void write_test_remark(void* context, std::string& remark) {
    int* const calls = static_cast<int*>(context);
    ++(*calls);
    remark = "Dealt from the bottom of the deck";
}

ProgramIdentity test_identity() {
    ProgramIdentity identity;
    identity.product = "Card Test";
    identity.version = "1.2";
    identity.vendor = "Example Vendor";
    identity.website = "https://example.test/";
    identity.copyright = "(c) 2026";
    identity.credits = "Everyone";
    return identity;
}

Point centre(Rect rect) {
    const Point middle{rect.x + rect.width * 0.5, rect.y + rect.height * 0.5};
    return middle;
}

// `local` in `control`'s coordinates, in the window's.
Point in_window(const Control& control, Point local) {
    const Rect absolute = control.absolute_bounds();
    const Point placed{absolute.x + local.x, absolute.y + local.y};
    return placed;
}

void press(Window& window, PointerButton button, Point position) {
    PointerEvent move{PointerAction::move, PointerButton::none, position};
    static_cast<void>(window.dispatch_pointer(move));
    PointerEvent down{PointerAction::down, button, position};
    static_cast<void>(window.dispatch_pointer(down));
    PointerEvent up{PointerAction::up, button, position};
    static_cast<void>(window.dispatch_pointer(up));
}

void key(Window& window, KeyAction action, std::uint32_t physical) {
    KeyEvent event;
    event.action = action;
    event.physical_key = physical;
    static_cast<void>(window.dispatch_key(event));
}

void identity_validation() {
    set_program_identity(test_identity());
    expect(program_identity().vendor == "Example Vendor", "a valid identity is kept");
    ProgramIdentity plain = test_identity();
    plain.website = "http://example.test/";
    bool refused = false;
    try {
        set_program_identity(plain);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && program_identity().website == "https://example.test/",
           "a website that is not https is refused and the identity stays");
    ProgramIdentity broken = test_identity();
    broken.vendor = std::string("Bad \xff vendor");
    refused = false;
    try {
        set_program_identity(broken);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused && program_identity().vendor == "Example Vendor", "text that is not UTF-8 is refused");
    ProgramIdentity bare = test_identity();
    bare.website.clear();
    set_program_identity(bare);
    expect(program_identity().website.empty(), "an empty website is admitted");
    set_program_identity(test_identity());
}

void overlay_switch() {
    set_diagnostic_overlay(false);
    expect(!diagnostic_overlay() && !invalidation_overlay_default(), "off is the invalidation switch off");
    expect(toggle_diagnostic_overlay() && diagnostic_overlay() && invalidation_overlay_default(),
           "a toggle turns both on");
    expect(!toggle_diagnostic_overlay() && !invalidation_overlay_default(), "a second toggle turns both off");
}

struct PageFixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<BusinessCard> card;
    std::unique_ptr<Window> window;

    PageFixture() {
        root = make_control<Panel>(StableId("card-test.root"));
        card = make_control<BusinessCard>(StableId("card-test.page"));
        const Size wanted = (*card).measure(Size{600.0, 400.0});
        (*card).set_bounds({20.0, 20.0, wanted.width, wanted.height});
        (*root).add_child(card);
        window = std::make_unique<Window>(root, Size{640.0, 420.0});
        (*window).perform_layout();
    }
};

void page_geometry_and_semantics() {
    PageFixture f;
    BusinessCard& card = *f.card;
    expect(card.presentation() == BusinessCardPresentation::page, "a card is on the page by default");
    const Rect stock = card.card_bounds();
    const Rect vendor = card.vendor_name_band();
    expect(!stock.empty() && stock.contains(vendor), "the vendor name lies on the stock");
    expect(stock.width / stock.height > 1.7 && stock.width / stock.height < 1.8, "a business card's proportion");
    expect(card.close_mark_bounds().empty(), "a page card has no close mark");
    expect(!card.website_band().empty(), "the website has its line");
    NullPainter painter;
    (*f.window).paint(painter, {0.0, 0.0, 640.0, 420.0});
    const BusinessCard::Parts parts = card.parts();
    expect(!parts.stock.bounds.empty() && !parts.vendor_name.bounds.empty() && !parts.website.bounds.empty(),
           "the stock, vendor name and website are published parts");
    expect(parts.close_mark.bounds.empty(), "the close mark is not published on the page");
    const SemanticDescriptor described = card.semantic_descriptor();
    expect(described.name == "Example Vendor business card" && described.role == SemanticRole::button,
           "the page card is the vendor's business card, a button");
    expect(described.description.find("Card Test 1.2") != std::string::npos &&
               described.description.find("Everyone") != std::string::npos,
           "the product, version and credits describe it");
}

void gesture_toggles_overlay() {
    PageFixture f;
    set_diagnostic_overlay(false);
    const Point vendor = in_window(*f.card, centre((*f.card).vendor_name_band()));
    press(*f.window, PointerButton::secondary, vendor);
    expect(!diagnostic_overlay(), "a secondary press without D changes nothing");
    key(*f.window, KeyAction::down, PhysicalKey::d);
    press(*f.window, PointerButton::secondary, vendor);
    expect(diagnostic_overlay(), "D held with a secondary press on the vendor name turns the overlay on");
    press(*f.window, PointerButton::secondary, vendor);
    expect(!diagnostic_overlay(), "the same gesture turns it off again");
    const Point product = in_window(*f.card, centre((*f.card).website_band()));
    press(*f.window, PointerButton::secondary, product);
    expect(!diagnostic_overlay(), "beside the vendor name the gesture does nothing");
    key(*f.window, KeyAction::up, PhysicalKey::d);
    expect(!business_card_shown(*f.window), "the gesture opens no card");
}

void page_opens_hand() {
    PageFixture f;
    int remarks = 0;
    set_business_card_remark_source(BusinessCardRemarkSource{&remarks, &write_test_remark});
    int open_requests = 0;
    SubscriptionToken handled_token = (*f.card).open_requested().subscribe(CountRequests{&open_requests, true});
    const Point middle = in_window(*f.card, centre((*f.card).card_bounds()));
    press(*f.window, PointerButton::primary, middle);
    expect(open_requests == 1 && !business_card_shown(*f.window), "a handled request shows nothing");
    handled_token.disconnect();

    SubscriptionToken counting = (*f.card).open_requested().subscribe(CountRequests{&open_requests, false});
    press(*f.window, PointerButton::primary, middle);
    expect(open_requests == 2 && business_card_shown(*f.window), "unhandled, a click shows the card in the hand");
    expect(!show_business_card(*f.window), "a window shows one card at a time");
    const std::shared_ptr<BusinessCard> hand = shown_business_card(*f.window);
    expect(hand != nullptr && (*hand).presentation() == BusinessCardPresentation::hand, "the shown card is in the hand");
    expect(remarks == 0 && (*hand).remark().empty() && (*hand).remark_count() == 0U,
           "the remark source is not asked as the card comes out");
    (*f.window).perform_layout();
    expect(!(*hand).close_mark_bounds().empty(), "the card in the hand has its close mark");
    NullPainter painter;
    (*f.window).paint(painter, {0.0, 0.0, 640.0, 420.0});
    const BusinessCard::Parts hand_parts = (*hand).parts();
    expect(!hand_parts.copyright.bounds.empty() && hand_parts.remark.bounds.empty(),
           "in the hand the copyright is its own part, and no remark is shown yet");
    const SemanticDescriptor described = (*hand).semantic_descriptor();
    bool closes = false;
    for (const SemanticAction action : described.actions) {
        if (action == SemanticAction::close) closes = true;
    }
    expect(described.role == SemanticRole::link && closes, "in the hand it is a link that can be closed");

    int put_away = 0;
    SubscriptionToken away = (*hand).put_away().subscribe(CountPutAway{&put_away});
    int websites = 0;
    SubscriptionToken website = (*hand).website_requested().subscribe(CountRequests{&websites, true});
    const Point site = in_window(*hand, centre((*hand).website_band()));
    press(*f.window, PointerButton::primary, site);
    expect(websites == 1 && business_card_shown(*f.window), "a click on the website asks for it");
    expect((*hand).last_website_request() == BusinessCardLinkOutcome::handled, "a handler answered it");
    website.disconnect();
    press(*f.window, PointerButton::primary, site);
    expect((*hand).last_website_request() == BusinessCardLinkOutcome::unavailable,
           "unhandled, in a window without a host, the address has nowhere to go");

    // The card itself holds the focus: the key reaches it as the focused
    // control, not through the bubble phase (which skips the target).
    expect((*f.window).request_focus(hand) && (*f.window).focused_control() == hand,
           "the card in the hand takes focus");
    key(*f.window, KeyAction::down, PhysicalKey::escape);
    key(*f.window, KeyAction::up, PhysicalKey::escape);
    expect(!business_card_shown(*f.window) && put_away == 1, "Escape puts the card away");

    expect(show_business_card(*f.window), "a card put away comes out again");
    expect(shown_business_card(*f.window) == hand, "the same card comes out");
    (*f.window).perform_layout();
    press(*f.window, PointerButton::primary, Point{4.0, 4.0});
    expect(!business_card_shown(*f.window) && put_away == 2, "a press beside the card puts it away");

    expect(show_business_card(*f.window), "out a third time");
    (*f.window).perform_layout();
    const Point close = in_window(*hand, centre((*hand).close_mark_bounds()));
    press(*f.window, PointerButton::primary, close);
    expect(!business_card_shown(*f.window) && put_away == 3, "the close mark puts it away");

    expect(show_business_card(*f.window), "out a fourth time");
    hide_business_card(*f.window);
    expect(!business_card_shown(*f.window) && put_away == 4, "hide_business_card puts it away");
    hide_business_card(*f.window);
    expect(put_away == 4, "hiding a card that is not shown does nothing");
    expect(shown_business_card(*f.window) == nullptr, "no card is shown");
    set_business_card_remark_source(BusinessCardRemarkSource{});
}

// -- the turn kernel ---------------------------------------------------------------

// A face of one colour, opaque, `width` by `height`.
std::vector<std::uint8_t> plain_face(std::uint32_t width, std::uint32_t height, std::uint8_t blue,
                                     std::uint8_t green, std::uint8_t red) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t index = 0U; index < pixels.size(); index += 4U) {
        pixels[index] = blue;
        pixels[index + 1U] = green;
        pixels[index + 2U] = red;
        pixels[index + 3U] = 255U;
    }
    return pixels;
}

void turn_kernel() {
    constexpr double pi = 3.14159265358979323846;
    constexpr std::uint32_t side_width = 40U;
    constexpr std::uint32_t side_height = 20U;
    // The front: left half red, right half blue, so a mirror shows.
    std::vector<std::uint8_t> front_pixels = plain_face(side_width, side_height, 0U, 0U, 200U);
    for (std::uint32_t y = 0U; y < side_height; ++y) {
        for (std::uint32_t x = side_width / 2U; x < side_width; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * side_width + x) * 4U;
            front_pixels[at] = 200U;
            front_pixels[at + 2U] = 0U;
        }
    }
    const std::vector<std::uint8_t> back_pixels = plain_face(side_width, side_height, 0U, 160U, 0U);
    const detail::CardFaceView front{front_pixels, side_width, side_height, side_width * 4U};
    const detail::CardFaceView back{back_pixels, side_width, side_height, side_width * 4U};
    detail::CardTurnPose pose{};
    pose.card = Rect{10.0, 10.0, 40.0, 20.0};
    pose.eye = 240.0;
    std::vector<std::uint8_t> out(60U * 40U * 4U);
    const PartRasterView view{out, DeviceExtent{60U, 40U}, 60U * 4U};
    const detail::CardTurnLight light{};
    expect(detail::draw_turned_card(front, back, pose, light, 1.0, view), "a valid pose draws");
    const std::size_t inside_left = (static_cast<std::size_t>(20U) * 60U + 15U) * 4U;
    const std::size_t inside_right = (static_cast<std::size_t>(20U) * 60U + 45U) * 4U;
    const std::size_t beside = (static_cast<std::size_t>(5U) * 60U + 5U) * 4U;
    expect(out[inside_left + 2U] == 200U && out[inside_left] == 0U && out[inside_left + 3U] == 255U,
           "at rest the front's left half shows as painted");
    expect(out[inside_right] == 200U && out[inside_right + 2U] == 0U, "and its right half");
    expect(out[beside + 3U] == 0U, "nothing is drawn beside the card");
    expect(detail::card_front_faces_eye(0.0, 0.0) && !detail::card_front_faces_eye(pi, 0.0),
           "a half turn shows the back");

    // A whole half turn: the back, as painted (its own side), lit as at rest.
    pose.yaw = pi;
    expect(detail::draw_turned_card(front, back, pose, light, 1.0, view), "the back draws");
    expect(out[inside_left + 1U] > 150U && out[inside_left + 2U] == 0U, "a half turn shows the back's colour");
    // A quarter turn less a little: a sliver, narrower than the card.
    pose.yaw = pi * 0.45;
    const Rect sliver = detail::turned_card_bounds(pose);
    expect(sliver.width < 20.0 && sliver.width > 0.0, "turned nearly edge on, the card is a sliver");
    const detail::CardTurnHit middle = detail::turned_card_hit(pose, Point{30.0, 20.0});
    expect(middle.hit && middle.front && std::abs(middle.u - 0.5) < 0.05 && std::abs(middle.v - 0.5) < 0.05,
           "the centre stays the centre");
    expect(!detail::turned_card_hit(pose, Point{12.0, 20.0}).hit, "beside the sliver there is no card");
    // The back seen from its own side reads as painted: a point left of
    // centre meets the back's own left half, in the hit and in the drawing.
    pose.yaw = pi;
    const detail::CardTurnHit turned_over = detail::turned_card_hit(pose, Point{15.0, 20.0});
    expect(turned_over.hit && !turned_over.front && turned_over.u < 0.5,
           "the back reads left to right, as painted");
    const detail::CardTurnHit turned_over_right = detail::turned_card_hit(pose, Point{45.0, 20.0});
    expect(turned_over_right.hit && !turned_over_right.front && turned_over_right.u > 0.5,
           "and its right half lies on the right");
    // Refusals leave the destination transparent.
    pose.pitch = pi;
    expect(!detail::draw_turned_card(front, back, pose, light, 1.0, view), "a pitch past a quarter turn is refused");
    pose.pitch = 0.0;
    const detail::CardFaceView short_face{std::span<const std::uint8_t>(front_pixels).first(16U), side_width,
                                          side_height, side_width * 4U};
    expect(!detail::draw_turned_card(short_face, back, pose, light, 1.0, view) && out[inside_left + 3U] == 0U,
           "a face whose pixels do not fill its shape is refused, leaving nothing drawn");
}

// -- the turn in the hand ---------------------------------------------------------------

void reduce_motion(Window& window) {
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
}

void hand_turns() {
    // Declared before the window that borrows it, so it outlives the window.
    test_support::FakeRaster raster{};
    PageFixture f;
    reduce_motion(*f.window);
    expect(show_business_card(*f.window), "the card comes out");
    (*f.window).perform_layout();
    const std::shared_ptr<BusinessCard> hand = shown_business_card(*f.window);
    key(*f.window, KeyAction::down, PhysicalKey::right);
    expect(!(*hand).turn_available() && (*hand).turn() == 0.0 && (*hand).face_up(),
           "without an offscreen renderer the card stays flat");
    (*f.window).set_framebuffer_painter(&raster);
    key(*f.window, KeyAction::down, PhysicalKey::right);
    constexpr double pi = 3.14159265358979323846;
    expect((*hand).turn_available() && std::abs((*hand).turn() - pi) < 1e-9 && !(*hand).face_up(),
           "Right turns it over (at once under reduced motion)");
    expect(raster.made == 2U, "both faces were painted offscreen");
    key(*f.window, KeyAction::down, PhysicalKey::left);
    expect(std::abs((*hand).turn()) < 1e-9 && (*hand).face_up(), "Left turns it back");

    const Rect stock = (*hand).card_bounds();
    const Point from = in_window(*hand, centre(stock));
    const Point to{from.x + stock.width * 0.4, from.y};
    PointerEvent down{PointerAction::down, PointerButton::primary, from};
    static_cast<void>((*f.window).dispatch_pointer(down));
    PointerEvent move{PointerAction::move, PointerButton::none, to};
    static_cast<void>((*f.window).dispatch_pointer(move));
    expect((*hand).turn() > 0.5 && (*hand).turn() < pi * 0.5, "a drag across turns the card with the pointer");
    PointerEvent up{PointerAction::up, PointerButton::primary, to};
    static_cast<void>((*f.window).dispatch_pointer(up));
    const double rest = std::round((*hand).turn() / pi) * pi;
    expect(std::abs((*hand).turn() - rest) < 1e-9 && business_card_shown(*f.window),
           "let go, it settles on a half turn, and the drag was no click");
    hide_business_card(*f.window);
    expect((*hand).turn() == 0.0, "put away, it comes out face up next time");
    (*f.window).set_framebuffer_painter(nullptr);
}

// -- the remarks around the card -----------------------------------------------------------

class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

void hand_remarks() {
    // Declared before the window whose clock borrows it. The motion clock
    // and the frame schedule must share a time base: the remark timer
    // (AnimationTimer) places its first tick on the steady clock
    // (FrameClock), and the card dates its remarks on the motion clock, so
    // the manual time starts at the steady clock's now (it once started at
    // 1000 s, before the timer's first tick, which then never came).
    ManualTime time{};
    time.time = FrameClock::now();
    PageFixture f;
    (*f.window).motion_clock().set_time_source(&time);
    int remarks = 0;
    set_business_card_remark_source(BusinessCardRemarkSource{&remarks, &write_test_remark});
    expect(show_business_card(*f.window), "the card comes out");
    (*f.window).perform_layout();
    const std::shared_ptr<BusinessCard> hand = shown_business_card(*f.window);
    const Point over = in_window(*hand, centre((*hand).card_bounds()));
    PointerEvent move{PointerAction::move, PointerButton::none, over};
    static_cast<void>((*f.window).dispatch_pointer(move));
    NullPainter painter;
    for (int frame = 0; frame < 90; ++frame) {
        time.time += std::chrono::milliseconds(16);
        static_cast<void>((*f.window).poll_frame_schedule(time.time));
        (*f.window).paint(painter, {0.0, 0.0, 640.0, 420.0});
    }
    expect(remarks >= 1 && (*hand).remark_count() >= 1U && (*hand).remark() == "Dealt from the bottom of the deck",
           "with the pointer over it, the card speaks a moment later");
    expect((*hand).remark_count() <= maximum_business_card_remarks, "at most three at once");
    const BusinessCard::Parts parts = (*hand).parts();
    const Rect stock = (*hand).card_bounds();
    const Rect said = parts.remark.bounds;
    const bool clear = said.right() <= stock.x || said.x >= stock.right() || said.bottom() <= stock.y ||
                       said.y >= stock.bottom();
    expect(!said.empty() && clear, "the remark lies around the card, never on it");
    hide_business_card(*f.window);
    expect((*hand).remark_count() == 0U, "put away, the remarks are gone");
    set_business_card_remark_source(BusinessCardRemarkSource{});
    (*f.window).motion_clock().set_time_source(nullptr);
}

// -- the heading band rule ---------------------------------------------------------------

void heading_band_rule() {
    PageFixture f;
    std::shared_ptr<Panel> band = make_control<Panel>(StableId("card-test.band"));
    const Rect stock = (*f.card).card_bounds();
    const Rect absolute = (*f.card).absolute_bounds();
    const double band_bottom = absolute.y + stock.y + stock.height * 0.5;
    (*band).set_bounds({0.0, 0.0, 640.0, band_bottom});
    (*f.root).add_child(band);
    (*f.window).perform_layout();
    expect(!(*f.card).heading_band_bottom().has_value(), "no rule by default");
    (*f.card).set_heading_band(band);
    const std::optional<double> bottom = (*f.card).heading_band_bottom();
    expect(bottom.has_value() && std::abs(*bottom - (band_bottom - absolute.y)) < 1e-9,
           "the band's lower edge, in the card's coordinates");
    const Point under_band{stock.x + stock.width * 0.5, stock.y + stock.height * 0.25};
    const Point below_band{stock.x + stock.width * 0.5, stock.y + stock.height * 0.75};
    expect(!(*f.card).hit_test_local(under_band) && (*f.card).hit_test_local(below_band),
           "above the band's edge the card takes no pointer");
    Control::Ptr removed = (*f.root).remove_child((*band).runtime_id());
    expect(removed == band, "the band leaves the page");
    removed.reset();
    band.reset();
    expect(!(*f.card).heading_band_bottom().has_value() && (*f.card).hit_test_local(under_band),
           "a band destroyed sets no rule");
}

// -- the website through the host ------------------------------------------------------

void website_through_host() {
    PageFixture f;
    host::HeadlessHost host(*f.window);
    expect(host.dispatch(HostAttachEvent{{640.0, 420.0}, 1.0}, 1000U).accepted(), "the host attaches");
    expect(show_business_card(*f.window), "the card comes out");
    (*f.window).perform_layout();
    const std::shared_ptr<BusinessCard> hand = shown_business_card(*f.window);
    key(*f.window, KeyAction::down, PhysicalKey::enter);
    expect((*hand).last_website_request() == BusinessCardLinkOutcome::opened &&
               host.headless_services().opened_urls().size() == 1U &&
               host.headless_services().opened_urls().front() == "https://example.test/",
           "unhandled, the website goes to the host's open-URL service");
    expect(host.headless_services().open_url("file:///etc/passwd").error == HostServiceError::invalid_argument &&
               host.headless_services().open_url("https://a b").error == HostServiceError::invalid_argument &&
               host.headless_services().open_url("mailto:someone@example.test").accepted(),
           "only web and mail addresses without spaces reach the platform");
    hide_business_card(*f.window);
}

} // namespace

int main() {
    identity_validation();
    overlay_switch();
    page_geometry_and_semantics();
    gesture_toggles_overlay();
    page_opens_hand();
    turn_kernel();
    hand_turns();
    hand_remarks();
    heading_band_rule();
    website_through_host();
    set_diagnostic_overlay(false);
    std::cout << "business card tests passed\n";
    return EXIT_SUCCESS;
}
