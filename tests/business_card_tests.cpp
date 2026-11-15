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
#include "gui_forms/components/business_card/business_card.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

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
    expect(remarks == 1 && (*hand).remark() == "Dealt from the bottom of the deck",
           "the remark source speaks once as the card comes out");
    (*f.window).perform_layout();
    expect(!(*hand).close_mark_bounds().empty(), "the card in the hand has its close mark");
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

    key(*f.window, KeyAction::down, PhysicalKey::escape);
    key(*f.window, KeyAction::up, PhysicalKey::escape);
    expect(!business_card_shown(*f.window) && put_away == 1, "Escape puts the card away");

    expect(show_business_card(*f.window), "a card put away comes out again");
    expect(shown_business_card(*f.window) == hand && remarks == 2, "the same card, with a fresh remark");
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

} // namespace

int main() {
    identity_validation();
    overlay_switch();
    page_geometry_and_semantics();
    gesture_toggles_overlay();
    page_opens_hand();
    set_diagnostic_overlay(false);
    std::cout << "business card tests passed\n";
    return EXIT_SUCCESS;
}
