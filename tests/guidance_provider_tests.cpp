#include "gui_forms/gui_forms.hpp"
#include "support/named_callbacks.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const SemanticNode* find_node(const std::vector<SemanticNode>& nodes,
                              std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        if (const SemanticNode* found = find_node(node.children, stable_id)) {
            return found;
        }
    }
    return nullptr;
}
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

struct Fixture final {
    Fixture() : window(root, {480.0, 260.0}) {
        (*root).set_requested_bounds({0.0, 0.0, 480.0, 260.0});
        (*panel).set_requested_bounds({30.0, 30.0, 360.0, 170.0});
        (*target).set_requested_bounds({25.0, 35.0, 160.0, 32.0});
        (*child).set_requested_bounds({25.0, 90.0, 160.0, 32.0});
        (*panel).add_child(target);
        (*panel).add_child(child);
        (*root).add_child(panel);
        window.perform_layout();
        static_cast<void>(window.take_damage());
    }

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("guidance.root"));
    std::shared_ptr<Panel> panel = make_control<Panel>(StableId("guidance.panel"));
    std::shared_ptr<TextBox> target =
        make_control<TextBox>(StableId("guidance.target"), "192.168.1.96:5555");
    std::shared_ptr<Button> child =
        make_control<Button>(StableId("guidance.child"), "Connect");
    Window window;
};

// Records nothing: stands in for a host's renderer.
class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

// Paints the window's pending damage, as a host does before its next wake:
// the provider's blink runs on a paint-gated timer (ADR-016).
void paint_damage(Window& window) {
    const DamageRegion damage = window.take_damage();
    if (damage.empty()) return;
    NullPainter painter;
    static_cast<void>(window.paint(painter, damage.bounds()));
}

void poll_next(Window& window) {
    paint_damage(window);
    const std::optional<FrameTime> wake = window.next_wake();
    require(wake.has_value(), "expected a scheduled provider frame");
    static_cast<void>(window.poll_frame_schedule(*wake));
}

class ObserveLocalHelpRequest final {
public:
    explicit ObserveLocalHelpRequest(std::vector<std::string>& order)
        : order_(order) {}

    void operator()(HelpRequestEvent& request) const {
        order_.push_back("control");
        require(request.keyboard_initiated &&
                    request.target_stable_id == "guidance.panel",
                "F1 must route to the nearest mapped focus ancestor");
    }

private:
    std::vector<std::string>& order_;
};

class ObserveProviderHelpRequest final {
public:
    explicit ObserveProviderHelpRequest(std::vector<std::string>& order)
        : order_(order) {}

    void operator()(HelpRequestEvent& request) const {
        order_.push_back("provider");
        require(request.help_namespace == "malkuth://local-help" &&
                    request.keyword == "network.endpoint" &&
                    request.navigator == HelpNavigator::keyword_index,
                "provider requests must retain namespace, keyword, and navigation intent");
        request.handled = true;
    }

private:
    std::vector<std::string>& order_;
};

class HandleLocalHelpRequest final {
public:
    explicit HandleLocalHelpRequest(std::vector<std::string>& order)
        : order_(order) {}

    void operator()(HelpRequestEvent& request) const {
        order_.push_back("handled-control");
        request.handled = true;
    }

private:
    std::vector<std::string>& order_;
};

void test_error_semantics_geometry_rtl_and_lifetime() {
    Fixture fixture;
    ErrorProvider errors(fixture.window);
    require(errors.can_extend(fixture.target) &&
                errors.container_control() == fixture.root,
            "provider extension and container lookup must stay bound to one Window");
    errors.tag = std::string("validation-channel");
    require(std::any_cast<std::string>(errors.tag.read()) == "validation-channel",
            "ErrorProvider Tag must retain application metadata inertly");
    const std::array<std::byte, 4> pixel{
        std::byte{0x20}, std::byte{0x30}, std::byte{0xd0}, std::byte{0xff}};
    const ImageLoadResult image = fixture.window.load_bgra32_premultiplied(
        1U, 1U, 4U, pixel);
    require(static_cast<bool>(image), "fixture must register a provider icon image");
    errors.icon = image.image;
    require(errors.icon.read() == image.image,
            "ErrorProvider must retain a validated portable ImageId override");
    errors.blink_style = ErrorBlinkStyle::never_blink;
    errors.set_error(fixture.target, "Server address is not reachable");
    fixture.window.perform_layout();

    ErrorProviderSnapshot first = errors.snapshot();
    require(errors.has_errors() && first.live_errors == 1U &&
                first.presented_icons == 1U && first.icons.size() == 1U,
            "an error must retain one presented adornment");
    const Rect target_bounds = (*fixture.target).absolute_bounds();
    require(first.icons.front().bounds.x >= target_bounds.right(),
            "the default middle-right error glyph must sit beyond the target edge");

    const std::array<ErrorIconAlignment, 6> alignments{
        ErrorIconAlignment::top_left, ErrorIconAlignment::top_right,
        ErrorIconAlignment::middle_left, ErrorIconAlignment::middle_right,
        ErrorIconAlignment::bottom_left, ErrorIconAlignment::bottom_right};
    for (const ErrorIconAlignment alignment : alignments) {
        errors.set_icon_alignment(fixture.target, alignment);
        const Rect glyph = errors.snapshot().icons.front().bounds;
        const bool expected_left = alignment == ErrorIconAlignment::top_left ||
            alignment == ErrorIconAlignment::middle_left ||
            alignment == ErrorIconAlignment::bottom_left;
        require(expected_left ? glyph.right() <= target_bounds.x
                              : glyph.x >= target_bounds.right(),
                "all six ErrorIconAlignment values must choose the authored side");
        if (alignment == ErrorIconAlignment::top_left ||
            alignment == ErrorIconAlignment::top_right) {
            require(glyph.y == target_bounds.y,
                    "top error alignment must use the target top edge");
        } else if (alignment == ErrorIconAlignment::bottom_left ||
                   alignment == ErrorIconAlignment::bottom_right) {
            require(glyph.bottom() == target_bounds.bottom(),
                    "bottom error alignment must use the target bottom edge");
        } else {
            require(glyph.y > target_bounds.y && glyph.bottom() < target_bounds.bottom(),
                    "middle error alignment must center within the target height");
        }
    }

    const SemanticSnapshot semantic_snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* target = find_node(
        semantic_snapshot.roots, "guidance.target");
    require(target && has_semantic_state((*target).states, SemanticState::invalid) &&
                (*target).description.find("Error: Server address is not reachable") !=
                    std::string::npos,
            "provider errors must enrich every control subclass at final semantic projection");

    errors.set_icon_alignment(fixture.target, ErrorIconAlignment::top_left);
    errors.set_icon_padding(fixture.target, 4.0);
    errors.icon_size = 24.0;
    ErrorProviderSnapshot left = errors.snapshot();
    require(left.icons.front().bounds.right() <= target_bounds.x - 4.0 &&
                left.icons.front().bounds.width == 24.0 &&
                errors.icon_size.read() == 24.0,
            "left alignment, padding, and explicit icon size must shape retained geometry");
    bool rejected_icon_size = false;
    try {
        errors.icon_size = 2.0;
    } catch (const std::invalid_argument&) {
        rejected_icon_size = true;
    }
    require(rejected_icon_size,
            "ErrorProvider must reject unusably small icon geometry");
    int direction_changes = 0;
    SubscriptionToken direction = errors.right_to_left_changed().subscribe(
        test_support::IncrementWhenTrue<int>(direction_changes));
    errors.right_to_left = true;
    ErrorProviderSnapshot mirrored = errors.snapshot();
    require(mirrored.icons.front().bounds.x >= target_bounds.right() &&
                direction_changes == 1,
            "provider RTL must mirror horizontal icon alignment deterministically");

    (*fixture.target).set_requested_bounds({190.0, 105.0, 130.0, 32.0});
    fixture.window.perform_layout();
    const ErrorProviderSnapshot moved = errors.snapshot();
    require(moved.icons.front().bounds != mirrored.icons.front().bounds,
            "error adornments must follow committed retained layout changes");

    (*fixture.panel).visible = false;
    require(errors.snapshot().presented_icons == 0U,
            "hiding an ancestor must synchronously revoke descendant adornments");
    (*fixture.panel).visible = true;
    fixture.window.perform_layout();
    require(errors.snapshot().presented_icons == 1U,
            "restoring an ancestor must rebuild descendant adornments without reauthoring errors");

    errors.clear();
    const SemanticSnapshot cleared_snapshot = fixture.window.semantic_snapshot();
    target = find_node(cleared_snapshot.roots, "guidance.target");
    require(!errors.has_errors() && target &&
                !has_semantic_state((*target).states, SemanticState::invalid),
            "clearing the provider must remove both visuals and invalid semantics");
}

void test_error_blink_is_bounded_occlusion_aware_and_reduced_motion_safe() {
    Fixture fixture;
    ErrorProvider errors(fixture.window);
    errors.blink_rate = 50ms;
    errors.set_error(fixture.target, "Transient validation failure");
    require(errors.snapshot().icons.front().blink_active && fixture.window.next_wake(),
            "changed-error blinking must use a scheduled blink timer");

    // An occluded window paints nothing, so the blink stops at the tick after
    // its last painted one; the damage it leaves resumes it with the exposed
    // window's next paint.
    fixture.window.set_occluded(true, FrameClock::now());
    for (std::size_t tick = 0U; tick < 2U && fixture.window.next_wake(); ++tick) {
        static_cast<void>(fixture.window.poll_frame_schedule(*fixture.window.next_wake()));
    }
    require(!fixture.window.next_wake(),
            "occlusion must suppress provider animation wakeups");
    fixture.window.set_occluded(false, FrameClock::now());
    paint_damage(fixture.window);
    require(fixture.window.next_wake().has_value(),
            "exposure must resume the retained blink deadline");

    PresentationSettings settings = fixture.window.presentation_settings();
    settings.reduced_motion = true;
    fixture.window.set_presentation_settings(settings);
    ErrorProviderSnapshot reduced = errors.snapshot();
    require(!reduced.icons.front().blink_active &&
                reduced.icons.front().blink_phase_visible &&
                !fixture.window.next_wake(),
            "reduced motion must settle the glyph visible and quiescent");

    settings.reduced_motion = false;
    fixture.window.set_presentation_settings(settings);
    // At most six transitions (one may have run while occluded).
    for (std::size_t index = 0; index < 6U; ++index) {
        paint_damage(fixture.window);
        if (!fixture.window.next_wake()) break;
        poll_next(fixture.window);
    }
    paint_damage(fixture.window);
    ErrorProviderSnapshot settled = errors.snapshot();
    require(!settled.icons.front().blink_active &&
                settled.icons.front().blink_phase_visible &&
                !fixture.window.next_wake(),
            "BlinkIfDifferentError must finish after a bounded deterministic sequence");

    errors.blink_style = ErrorBlinkStyle::always_blink;
    require(errors.snapshot().icons.front().blink_active,
            "AlwaysBlink must retain its blink timer while visible");
    errors.blink_style = ErrorBlinkStyle::never_blink;
    require(!errors.snapshot().icons.front().blink_active &&
                !fixture.window.next_wake(),
            "NeverBlink must synchronously revoke scheduled work");
}

void test_help_routes_f1_locally_then_to_provider_without_external_policy() {
    Fixture fixture;
    HelpProvider help(fixture.window);
    require(help.can_extend(fixture.panel),
            "HelpProvider must accept a live target in its Window");
    help.tag = std::string("local-help");
    require(std::any_cast<std::string>(help.tag.read()) == "local-help",
            "HelpProvider Tag must retain inert application metadata");
    help.help_namespace = "malkuth://local-help";
    help.set_help_string(fixture.panel, "Choose a reachable local service endpoint.");
    help.set_help_keyword(fixture.panel, "network.endpoint");
    help.set_help_navigator(fixture.panel, HelpNavigator::keyword_index);
    require(fixture.window.request_focus(fixture.child),
            "fixture child must accept keyboard focus");

    std::vector<std::string> order;
    SubscriptionToken local = (*fixture.panel).help_requested().subscribe(
        ObserveLocalHelpRequest(order));
    SubscriptionToken provider = help.help_requested().subscribe(
        ObserveProviderHelpRequest(order));

    const bool handled = fixture.window.dispatch_key(
        {KeyAction::down, PhysicalKey::f1, Modifier::none});
    require(handled && order == std::vector<std::string>({"control", "provider"}),
            "F1 must use deterministic local-then-provider event ordering");
    const HelpProviderSnapshot first = help.snapshot();
    require(first.requests == 1U && first.handled_requests == 1U,
            "help instrumentation must distinguish requested and handled work");

    const SemanticSnapshot help_semantics = fixture.window.semantic_snapshot();
    const SemanticNode* panel = find_node(
        help_semantics.roots, "guidance.panel");
    require(panel && (*panel).description.find(
                "Choose a reachable local service endpoint.") !=
                std::string::npos,
            "authored help must enrich native semantic output");

    help.set_show_help(fixture.panel, false);
    require(!help.show_help(*fixture.panel) &&
                !fixture.window.dispatch_key(
                    {KeyAction::down, PhysicalKey::f1, Modifier::none}),
            "an explicit false ShowHelp value must disable automatic metadata routing");
    help.reset_show_help(*fixture.panel);
    require(help.show_help(*fixture.panel),
            "ResetShowHelp must restore metadata-derived automatic behavior");

    local.disconnect();
    provider.disconnect();
    int provider_calls = 0;
    SubscriptionToken local_handler =
        (*fixture.panel).help_requested().subscribe(
            HandleLocalHelpRequest(order));
    SubscriptionToken provider_handler = help.help_requested().subscribe(
        test_support::IncrementCounter<int, HelpRequestEvent&>(provider_calls));
    require(fixture.window.dispatch_key(
                {KeyAction::down, PhysicalKey::f1, Modifier::none}) &&
                provider_calls == 0,
            "a handled control request must not fall through to provider policy");

    // The provider is a view onto the control's help fields (help H1): one
    // value, whichever side writes it.
    require((*fixture.panel).help_line.read() == "Choose a reachable local service endpoint." &&
                (*fixture.panel).keywords.read() == "network.endpoint",
            "HelpProvider writes the control's help line and keywords");
    (*fixture.panel).help_line = "Pick an endpoint you can reach.";
    require(help.help_string(*fixture.panel) == "Pick an endpoint you can reach.",
            "a help line set on the control is the provider's help string");
    bool refused = false;
    try {
        help.set_help_string(fixture.panel, std::string("Bell\x07"));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && help.help_string(*fixture.panel) == "Pick an endpoint you can reach.",
            "a control character is refused and nothing changes");
    help.clear();
    require((*fixture.panel).help_line.read() == "Pick an endpoint you can reach.",
            "clear drops the provider's settings and leaves the control's field");
}

void test_multiple_providers_and_disposal_cleanup() {
    Fixture fixture;
    std::unique_ptr<gui_forms::ErrorProvider> first = std::make_unique<ErrorProvider>(fixture.window);
    std::unique_ptr<gui_forms::ErrorProvider> second = std::make_unique<ErrorProvider>(fixture.window);
    (*first).blink_style = ErrorBlinkStyle::never_blink;
    (*second).blink_style = ErrorBlinkStyle::never_blink;
    (*first).set_error(fixture.target, "First validation channel");
    (*second).set_error(fixture.target, "Second validation channel");
    const SemanticSnapshot both_snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* both = find_node(
        both_snapshot.roots, "guidance.target");
    require(both && (*both).description.find("First validation channel") <
                         (*both).description.find("Second validation channel"),
            "multiple providers must project in stable provider-identity order");

    (*first).dispose();
    const SemanticSnapshot remaining_snapshot = fixture.window.semantic_snapshot();
    const SemanticNode* remaining = find_node(
        remaining_snapshot.roots, "guidance.target");
    require(remaining &&
                (*remaining).description.find("First validation channel") ==
                    std::string::npos &&
                (*remaining).description.find("Second validation channel") !=
                    std::string::npos,
            "disposing one provider must remove only its own extension state");

    (*fixture.target).dispose();
    require((*second).snapshot().presented_icons == 0U &&
                !fixture.window.next_wake(),
            "target disposal must revoke provider overlay and frame work synchronously");
}

} // namespace

int main() {
    try {
        test_error_semantics_geometry_rtl_and_lifetime();
        test_error_blink_is_bounded_occlusion_aware_and_reduced_motion_safe();
        test_help_routes_f1_locally_then_to_provider_without_external_policy();
        test_multiple_providers_and_disposal_cleanup();
        std::cout << "guidance provider tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "guidance provider tests failed: " << error.what() << '\n';
        return 1;
    }
}
