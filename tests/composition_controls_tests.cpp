#include "gui_forms/composition_controls.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/window.hpp"
#include "support/named_callbacks.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class AppendCardSelection final {
public:
    explicit AppendCardSelection(std::string& trace) : trace_(trace) {}

    void operator()(bool selected) const {
        trace_ += selected ? "selected\n" : "cleared\n";
    }

private:
    std::string& trace_;
};

class ObserveReviewRecord final {
public:
    explicit ObserveReviewRecord(unsigned& changes) : changes_(changes) {}

    void operator()(const ReviewRecord& record) const {
        require(record.key == "surface-pipeline",
                "ReviewCard event must observe committed record identity");
        ++changes_;
    }

private:
    unsigned& changes_;
};

void test_card_section_ownership_and_layout() {
    std::shared_ptr<gui_forms::Card> card = make_control<Card>(StableId("card.layout"));
    (*card).set_requested_bounds({0.0, 0.0, 200.0, 160.0});
    CardLayout layout;
    layout.padding = {10.0, 10.0, 10.0, 10.0};
    layout.section_gap = 5.0;
    layout.header_extent = 30.0;
    layout.footer_extent = 20.0;
    (*card).card_layout = layout;
    std::shared_ptr<gui_forms::Label> header = make_control<Label>(StableId("card.header"), "Header");
    std::shared_ptr<gui_forms::Panel> body = make_control<Panel>(StableId("card.body"));
    std::shared_ptr<gui_forms::Label> footer = make_control<Label>(StableId("card.footer"), "Footer");
    require(!(*card).set_header(header) && !(*card).set_body(body) &&
                !(*card).set_footer(footer),
            "initial card section installation must not report a predecessor");
    Window window(card, {200.0, 160.0});
    window.perform_layout();
    require((*header).committed_arranged_bounds() == Rect{10.0, 10.0, 180.0, 30.0} &&
                (*body).committed_arranged_bounds() == Rect{10.0, 45.0, 180.0, 80.0} &&
                (*footer).committed_arranged_bounds() == Rect{10.0, 130.0, 180.0, 20.0},
            "card must deterministically allocate header, body, and footer slots");

    std::shared_ptr<gui_forms::Label> replacement = make_control<Label>(StableId("card.replacement"), "New");
    require((*card).set_header(replacement) == header && !(*header).parent() &&
                (*replacement).parent() == card,
            "section replacement must return the detached predecessor");
    std::shared_ptr<gui_forms::Panel> foreign_parent = make_control<Panel>(StableId("card.foreign.parent"));
    std::shared_ptr<gui_forms::Label> foreign = make_control<Label>(StableId("card.foreign"), "Foreign");
    (*foreign_parent).add_child(foreign);
    bool rejected{};
    try {
        static_cast<void>((*card).set_header(foreign));
    } catch (const std::logic_error&) {
        rejected = true;
    }
    require(rejected && (*card).header() == replacement &&
                (*replacement).parent() == card,
            "invalid replacement must leave the installed section intact");
}

void test_interactive_card_state_input_and_semantics() {
    std::shared_ptr<gui_forms::Card> card = make_control<Card>(StableId("card.interactive"));
    (*card).set_requested_bounds({10.0, 10.0, 180.0, 100.0});
    (*card).accessible_name = "Sapphire specimen";
    (*card).interactive = true;
    Window window(card, {200.0, 120.0});
    window.perform_layout();
    unsigned activations{};
    SubscriptionToken activation = (*card).activated().subscribe(
        test_support::IncrementCounter<unsigned, Card&>(activations));
    unsigned selection_changes{};
    SubscriptionToken selection = (*card).selected_changed().subscribe(
        test_support::IncrementCounter<unsigned, bool>(selection_changes));

    PointerEvent move;
    move.action = PointerAction::move;
    move.position = {50.0, 50.0};
    static_cast<void>(window.dispatch_pointer(move));
    require((*card).hovered_visual(),
            "interactive card must retain routed hover state");
    require(window.dispatch_pointer(
                {PointerAction::down, PointerButton::primary, {50.0, 50.0}}) &&
                (*card).pressed_visual(),
            "interactive card must retain primary press state");
    require(window.dispatch_pointer(
                {PointerAction::up, PointerButton::primary, {50.0, 50.0}}) &&
                !(*card).pressed_visual() && activations == 1U,
            "qualified pointer release must activate the card once");

    (*card).selected = true;
    (*card).selected = true;
    require(selection_changes == 1U,
            "card selection assignment must be synchronous and silent on no-op");
    const SemanticDescriptor descriptor = (*card).semantic_descriptor();
    require(descriptor.role == SemanticRole::list_item &&
                has_semantic_state(descriptor.states, SemanticState::selected) &&
                descriptor.name == "Sapphire specimen",
            "interactive selected card must expose stable selectable semantics");
    require(window.perform_semantic_action(
                "card.interactive", SemanticAction::press) &&
                activations == 2U,
            "semantic press must share card activation authority");
}

void test_card_selection_behavior_is_explicit_and_ordered() {
    std::shared_ptr<gui_forms::Card> card = make_control<Card>(StableId("card.selection-behavior"));
    (*card).set_requested_bounds({0.0, 0.0, 180.0, 80.0});
    (*card).interactive = true;
    (*card).selection_behavior = CardSelectionBehavior::toggle_on_activation;
    Window window(card, {180.0, 80.0});
    std::string trace;
    SubscriptionToken selection = (*card).selected_changed().subscribe(
        AppendCardSelection(trace));
    SubscriptionToken activation = (*card).activated().subscribe(
        test_support::AppendLiteral<Card&>(trace, "activated\n"));

    require(window.perform_semantic_action(
                "card.selection-behavior", SemanticAction::press) &&
                (*card).selected.read() &&
                trace == "selected\nactivated\n",
            "toggle-on-activation must commit selection before activation");
    trace.clear();
    require(window.perform_semantic_action(
                "card.selection-behavior", SemanticAction::press) &&
                !(*card).selected.read() &&
                trace == "cleared\nactivated\n",
            "toggle-on-activation must remain symmetric and ordered");

    bool rejected{};
    try {
        (*card).selection_behavior = static_cast<CardSelectionBehavior>(255);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && (*card).selection_behavior.read() ==
                            CardSelectionBehavior::toggle_on_activation,
            "invalid card selection customization must fail atomically");
}

void test_card_theme_status_and_outsets_are_reusable() {
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "shadow-card";
    ControlVisualRecipe& invalid = definition
        .roles[static_cast<std::size_t>(ControlVisualRole::card)]
        .ordinary[static_cast<std::size_t>(ControlSurfaceState::invalid)];
    invalid.material.shadows = {
        {{2.0, 3.0}, 4.0, 1.0, Color::rgba(20, 30, 40, 90)}};
    const std::shared_ptr<const Theme> theme = Theme::create(definition);
    std::shared_ptr<gui_forms::Card> card = make_control<Card>(StableId("card.theme"));
    (*card).set_theme_override(theme);
    (*card).set_visual_status(ControlVisualStatus::invalid);
    require((*card).visual_outsets() == Insets{6.0, 5.0, 10.0, 11.0},
            "card must project active recipe shadows into compositor outsets");
    const SemanticDescriptor descriptor = (*card).semantic_descriptor();
    require(!descriptor.exposed,
            "noninteractive unnamed card must remain a semantic composition detail");
}

void test_compositions_follow_structural_theme_tokens_until_overridden() {
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "structural-composition";
    definition.structure.spacing.medium = 9.0;
    definition.structure.spacing.large = 15.0;
    definition.structure.spacing.xlarge = 19.0;
    definition.structure.spacing.section = 28.0;
    definition.structure.geometry.control_height = 32.0;
    definition.structure.geometry.large_control_height = 38.0;
    definition.structure.geometry.splitter_width = 5.0;
    definition.structure.geometry.splitter_hit_width = 13.0;
    definition.structure.geometry.navigation_extent = 310.0;
    definition.structure.geometry.navigation_minimum = 150.0;
    definition.structure.geometry.content_minimum = 260.0;
    definition.structure.geometry.compact_breakpoint = 780.0;
    const std::shared_ptr<const Theme> theme =
        Theme::create(definition);

    std::shared_ptr<gui_forms::Card> card = make_control<Card>(StableId("card.structural-theme"));
    (*card).set_theme_override(theme);
    const CardLayout themed_card = (*card).effective_card_layout();
    require((*card).uses_theme_layout() &&
                themed_card.padding == Insets{15.0, 9.0, 15.0, 9.0} &&
                themed_card.section_gap == 9.0 &&
                themed_card.header_extent == 32.0,
            "Card defaults must derive from inherited structural theme tokens");
    CardLayout explicit_layout;
    explicit_layout.padding = {3.0, 4.0, 5.0, 6.0};
    explicit_layout.section_gap = 2.0;
    (*card).card_layout = explicit_layout;
    require(!(*card).uses_theme_layout() &&
                (*card).effective_card_layout() == explicit_layout,
            "an explicit Card layout must remain authoritative");
    (*card).reset_card_layout_to_theme();
    require((*card).uses_theme_layout() &&
                (*card).effective_card_layout() == themed_card,
            "Card must support deterministic return to inherited structure");

    std::shared_ptr<gui_forms::MasterDetailView> view = make_control<MasterDetailView>(
        StableId("master-detail.structural-theme"));
    (*view).set_theme_override(theme);
    const MasterDetailLayout themed_master =
        (*view).effective_master_detail_layout();
    require((*view).uses_theme_layout() && themed_master.master_extent == 310.0 &&
                themed_master.splitter_width == 5.0 &&
                themed_master.splitter_hit_width == 13.0 &&
                themed_master.compact_threshold == 780.0,
            "MasterDetailView defaults must derive from structural theme tokens");
    std::shared_ptr<gui_forms::Panel> master = make_control<Panel>(StableId("structural.master"));
    std::shared_ptr<gui_forms::Panel> detail = make_control<Panel>(StableId("structural.detail"));
    static_cast<void>((*view).set_master(master));
    static_cast<void>((*view).set_detail(detail));
    Window window(view, {900.0, 400.0});
    window.perform_layout();
    window.perform_layout();
    require((*(*view).split_container()).splitter_distance.read() == 310.0,
            "structural navigation extent must reach genuine splitter geometry");

    MasterDetailLayout explicit_master = themed_master;
    explicit_master.master_extent = 280.0;
    (*view).set_master_detail_layout(explicit_master);
    require(!(*view).uses_theme_layout() &&
                (*view).effective_master_detail_layout() == explicit_master,
            "an explicit MasterDetail layout must remain authoritative");
    (*view).reset_master_detail_layout_to_theme();
    require((*view).uses_theme_layout() &&
                (*view).effective_master_detail_layout() == themed_master,
            "MasterDetailView must support deterministic return to theme structure");
}

void test_review_card_owns_typed_theme_aware_projection() {
    std::shared_ptr<gui_forms::ReviewCard> card = make_control<ReviewCard>(StableId("review.typed"));
    (*card).set_requested_bounds({0.0, 0.0, 360.0, 150.0});
    (*card).interactive = true;
    unsigned changes{};
    SubscriptionToken changed = (*card).record_changed().subscribe(
        ObserveReviewRecord(changes));
    const ReviewRecord pending{
        "surface-pipeline", "Retained surface pipeline",
        "Painting records renderer-neutral commands before host replay.",
        "MEASURED PARTIAL", ReviewDisposition::pending};
    (*card).record = pending;
    (*card).record = pending;
    require(changes == 1U && (*card).header() == (*card).title_label() &&
                (*card).body() == (*card).summary_label() &&
                (*card).footer() == (*card).verdict_label() &&
                (*(*card).title_label()).text_style_role.read() ==
                    TextStyleRole::heading &&
                (*(*card).summary_label()).text_style_role.read() ==
                    TextStyleRole::body &&
                (*(*card).verdict_label()).text_style_role.read() ==
                    TextStyleRole::caption &&
                !(*(*card).title_label()).font.read().has_value(),
            "ReviewCard must own one typed theme-aware title/body/verdict projection");
    const SemanticDescriptor pending_semantics = (*card).semantic_descriptor();
    require(pending_semantics.name == pending.title &&
                pending_semantics.description == pending.summary &&
                pending_semantics.value == pending.verdict &&
                has_semantic_state(pending_semantics.states,
                                   SemanticState::busy),
            "pending ReviewCard must project its complete record and busy state");

    Window window(card, {360.0, 150.0});
    window.perform_layout();
    require((*(*card).title_label()).committed_arranged_bounds().height > 0.0 &&
                (*(*card).summary_label()).committed_arranged_bounds().height > 0.0 &&
                (*(*card).verdict_label()).committed_arranged_bounds().height > 0.0,
            "ReviewCard must arrange all owned sections through public Card layout");

    ReviewRecord rejected = pending;
    rejected.disposition = ReviewDisposition::rejected;
    (*card).record = rejected;
    require((*card).visual_status() == ControlVisualStatus::invalid &&
                has_semantic_state((*card).semantic_descriptor().states,
                                   SemanticState::invalid),
            "rejected ReviewCard must share invalid visual and semantic authority");
    ReviewRecord invalid = rejected;
    invalid.key.clear();
    bool rejected_invalid{};
    try {
        (*card).record = std::move(invalid);
    } catch (const std::invalid_argument&) {
        rejected_invalid = true;
    }
    require(rejected_invalid && (*card).record.read() == rejected && changes == 2U,
            "invalid ReviewCard mutation must fail atomically");
}

void test_master_detail_ownership_and_responsive_presentation() {
    std::shared_ptr<gui_forms::MasterDetailView> view = make_control<MasterDetailView>(StableId("master-detail"));
    (*view).accessible_name = "Decision browser";
    MasterDetailLayout layout;
    layout.master_extent = 220.0;
    layout.master_minimum = 120.0;
    layout.detail_minimum = 240.0;
    layout.compact_threshold = 700.0;
    (*view).set_master_detail_layout(layout);
    std::shared_ptr<gui_forms::Panel> master = make_control<Panel>(StableId("master-detail.master"));
    std::shared_ptr<gui_forms::Panel> detail = make_control<Panel>(StableId("master-detail.detail"));
    (*master).focusable = true;
    (*detail).focusable = true;
    require(!(*view).set_master(master) && !(*view).set_detail(detail),
            "master/detail initial role installation must not return predecessors");
    Window window(view, {900.0, 420.0});
    window.perform_layout();
    window.perform_layout();
    const std::shared_ptr<SplitContainer> split = (*view).split_container();
    require((*view).effective_display_mode() ==
                MasterDetailDisplayMode::side_by_side &&
                (*master).committed_arranged_bounds() ==
                    Rect{0.0, 0.0, 220.0, 420.0} &&
                (*detail).committed_arranged_bounds() ==
                    Rect{0.0, 0.0, 677.0, 420.0} &&
                !(*split).first_collapsed() && !(*split).second_collapsed(),
            "wide master/detail must allocate both retained roles around one splitter");

    std::vector<MasterDetailPresentationChange> changes;
    SubscriptionToken changed = (*view).presentation_changed().subscribe(
        test_support::PushBack<std::vector<MasterDetailPresentationChange>,
                               const MasterDetailPresentationChange&>(changes));
    window.resize({520.0, 420.0});
    window.perform_layout();
    window.perform_layout();
    require((*view).effective_display_mode() ==
                MasterDetailDisplayMode::master_only &&
                !(*split).first_collapsed() && (*split).second_collapsed() &&
                (*master).effectively_visible() && !(*detail).effectively_visible() &&
                changes.size() == 1U && changes.back().automatic,
            "compact automatic presentation must retain master and remove detail from visibility");
    require(window.request_focus(master),
            "compact master must remain a genuine focusable subtree");
    (*view).show_detail();
    window.perform_layout();
    window.perform_layout();
    require((*view).effective_display_mode() ==
                MasterDetailDisplayMode::detail_only &&
                (*split).first_collapsed() && !(*split).second_collapsed() &&
                !(*master).effectively_visible() && (*detail).effectively_visible() &&
                window.focused_control() != master && changes.size() == 2U,
            "compact detail navigation must transfer focus out of the hidden master");
    const SemanticDescriptor descriptor = (*view).semantic_descriptor();
    require(descriptor.role == SemanticRole::group &&
                descriptor.name == "Decision browser" &&
                descriptor.value == "detail",
            "master/detail semantics must report the current compact presentation");

    window.resize({900.0, 420.0});
    window.perform_layout();
    window.perform_layout();
    require((*view).effective_display_mode() ==
                MasterDetailDisplayMode::side_by_side &&
                (*master).effectively_visible() && (*detail).effectively_visible(),
            "wide accommodation must restore both automatically collapsed roles");

    std::shared_ptr<gui_forms::Panel> replacement = make_control<Panel>(StableId("master-detail.replacement"));
    require((*view).set_detail(replacement) == detail && !(*detail).parent() &&
                (*replacement).parent() == (*split).second_panel(),
            "detail replacement must return the detached predecessor");
    bool rejected{};
    try {
        static_cast<void>((*view).set_detail(master));
    } catch (const std::logic_error&) {
        rejected = true;
    }
    require(rejected && (*view).detail() == replacement &&
                (*replacement).parent() == (*split).second_panel(),
            "invalid cross-role replacement must preserve installed detail content");
}

} // namespace

int main() {
    try {
        test_card_section_ownership_and_layout();
        test_interactive_card_state_input_and_semantics();
        test_card_selection_behavior_is_explicit_and_ordered();
        test_card_theme_status_and_outsets_are_reusable();
        test_compositions_follow_structural_theme_tokens_until_overridden();
        test_review_card_owns_typed_theme_aware_projection();
        test_master_detail_ownership_and_responsive_presentation();
        std::cout << "gui_forms_composition_controls_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_composition_controls_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
