#pragma once

// The model side of the AT-SPI Gallery test (stage A, A-L2): the Gallery
// window, a supplement window with the registry's base types the Gallery
// reaches only through subclasses, and the line commands the client sends
// over the fixture's standard input. Each command answers with one line;
// a report is the semantic tree the client compares against what AT-SPI
// publishes, as JSON.
//
// Portable C++: no ATK, GLib or X11 here. The Linux fixture
// (tests/linux_accessibility_gallery_fixture.cpp) publishes both windows
// through the ATK adapter after every command and before it writes the
// answer; nothing changes the model between the two, so the report and the
// published tree describe one semantic generation.
//
// Commands (one per line):
//   page ID         select Gallery page ID; answers its report
//   report          answers the Gallery's report
//   supplement      answers the supplement window's report
//   reset           closes overlays and dialogs (Escape), hides a toast and
//                   reselects the page
//   arm STABLE_ID   counts the clicks of that ButtonBase; answers "armed 1" or "armed 0"
//   replace STABLE_ID  disposes that Button and inserts a new Button with the
//                   same stable identity and text at its index; answers the report
//   quit            answers "bye"
// A refused command answers "error " and the reason.
//
// Numbers are written with 17 significant digits, so a double read back
// through AT-SPI's Value interface compares exactly.

#include "demo/gallery.hpp"
#include "demo/gallery_page_specs.hpp"
#include "gui_forms/controls/callout.hpp"
#include "gui_forms/controls/help_book_viewer.hpp"
#include "gui_forms/controls/sparkline.hpp"
#include "gui_forms/controls/modal_layer.hpp"
#include "gui_forms/controls/plot_view.hpp"
#include "gui_forms/controls/value_bar.hpp"
#include "gui_forms/controls/wizard_page.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/range_control.hpp"
#include "gui_forms/controls/scroll_bar.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "headless_host.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <locale>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gui_forms::accessibility_session {

inline void append_string(std::ostringstream& stream, std::string_view value) {
    stream << '"';
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character == '"') {
            stream << "\\\"";
        } else if (character == '\\') {
            stream << "\\\\";
        } else if (character < 0x20U) {
            char escaped[8] = {};
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned int>(character));
            stream << escaped;
        } else {
            stream << static_cast<char>(character);
        }
    }
    stream << '"';
}

inline void append_number(std::ostringstream& stream, const std::optional<double>& value) {
    if (value) {
        stream << *value;
    } else {
        stream << "null";
    }
}

inline void append_count(std::ostringstream& stream, const std::optional<std::size_t>& value) {
    if (value) {
        stream << *value;
    } else {
        stream << "null";
    }
}

// Every field the client compares, and the children in semantic order.
inline void append_node(std::ostringstream& stream, const SemanticNode& node) {
    stream << "{\"runtime_id\":" << node.runtime_id << ",\"stable_id\":";
    append_string(stream, node.stable_id);
    stream << ",\"role\":";
    append_string(stream, semantic_role_name(node.role));
    stream << ",\"name\":";
    append_string(stream, node.name);
    stream << ",\"value\":";
    append_string(stream, node.value);
    stream << ",\"description\":";
    append_string(stream, node.description);
    stream << ",\"numeric_value\":";
    append_number(stream, node.numeric_value);
    stream << ",\"minimum_value\":";
    append_number(stream, node.minimum_value);
    stream << ",\"maximum_value\":";
    append_number(stream, node.maximum_value);
    stream << ",\"position_in_set\":";
    append_count(stream, node.position_in_set);
    stream << ",\"set_size\":";
    append_count(stream, node.set_size);
    stream << ",\"has_popup\":" << (node.has_popup ? "true" : "false");
    stream << ",\"controlled_panel\":";
    append_string(stream, node.controlled_panel);
    stream << ",\"orientation\":";
    if (node.orientation) {
        append_string(stream, semantic_orientation_name(*node.orientation));
    } else {
        stream << "null";
    }
    stream << ",\"level\":";
    append_count(stream, node.level);
    stream << ",\"modal\":" << (node.modal ? "true" : "false");
    stream << ",\"row_index\":";
    append_count(stream, node.row_index);
    stream << ",\"column_index\":";
    append_count(stream, node.column_index);
    stream << ",\"row_count\":";
    append_count(stream, node.row_count);
    stream << ",\"column_count\":";
    append_count(stream, node.column_count);
    stream << ",\"access_key\":";
    append_string(stream, node.access_key);
    stream << ",\"small_step\":";
    append_number(stream, node.small_step);
    stream << ",\"text_selection\":";
    if (node.text_selection) {
        stream << "{\"anchor\":" << (*node.text_selection).anchor.value() << ",\"caret\":"
               << (*node.text_selection).caret.value() << '}';
    } else {
        stream << "null";
    }
    stream << ",\"multiline\":" << (node.multiline ? "true" : "false");
    stream << ",\"states\":" << static_cast<std::uint32_t>(node.states) << ",\"actions\":[";
    for (std::size_t index = 0; index < node.actions.size(); ++index) {
        if (index != 0U) stream << ',';
        append_string(stream, semantic_action_name(node.actions[index]));
    }
    stream << "],\"children\":[";
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        if (index != 0U) stream << ',';
        append_node(stream, node.children[index]);
    }
    stream << "]}";
}

// Appends every control of the tree under `root`, root first, in
// depth-first child order: its registry id (the anatomy's control id, as
// manifests/controls.json names it), stable identity, runtime identity,
// whether it is visible (hidden pages' controls are in the tree too), and
// whether its semantic descriptor is exposed.
inline void append_controls(std::ostringstream& stream, const Control::Ptr& root, bool& first) {
    std::vector<Control::Ptr> pending{root};
    while (!pending.empty()) {
        const Control::Ptr control = pending.back();
        pending.pop_back();
        if (!first) stream << ',';
        first = false;
        stream << "{\"type\":";
        append_string(stream, (*control).anatomy().control_id);
        stream << ",\"stable_id\":";
        append_string(stream, (*control).stable_id().value());
        const SemanticDescriptor descriptor = (*control).semantic_descriptor();
        stream << ",\"runtime_id\":" << (*control).runtime_id().value << ",\"visible\":"
               << ((*control).effectively_visible() ? "true" : "false") << ",\"exposed\":"
               << (descriptor.exposed ? "true" : "false") << '}';
        const std::span<const Control::Ptr> children = (*control).children();
        for (std::size_t index = children.size(); index > 0U; --index) {
            pending.push_back(children[index - 1U]);
        }
    }
}

// Answers every host dialog the Gallery requests (message boxes, file,
// folder and colour choosers) at once, as cancelled, so a default action
// that opens one completes without a person.
struct CancellingDialogHandler final {
    HostDialogResult operator()(const HostDialogRequest& request, host::HeadlessHostServices&) const {
        HostDialogResult result{};
        result.request_id = request.request_id;
        if (std::holds_alternative<HostMessageDialogRequest>(request.payload)) {
            result.payload = HostMessageDialogResult{};
        } else if (std::holds_alternative<HostColorDialogRequest>(request.payload)) {
            result.payload = HostColorDialogResult{};
        } else {
            result.payload = HostPathDialogResult{};
        }
        return result;
    }
};

// Counts a ButtonBase's clicks for the client's default-action checks.
struct ClickCounter final {
    std::uint64_t clicks{};
    void clicked(ButtonBase&) { ++clicks; }
};

// The supplement window: the registry types the Gallery shows only through
// subclasses (ButtonBase, RangeControl, ScrollBar), one of each, as the
// anatomy coverage test adds them; and the visual registry controls no
// Gallery page attaches (the instruments and the plot, and the wizard page,
// callout, modal layer and help viewer), each placed bare, as the Windows
// accessibility tests' off-tree fixture places them.
inline constexpr Size supplement_size{800.0, 600.0};

inline std::unique_ptr<Window> make_supplement() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("supplement"));
    std::shared_ptr<ButtonBase> button_base = make_control<ButtonBase>(StableId("supplement.button-base"), "Base");
    std::shared_ptr<ScrollBar> scroll_bar = make_control<ScrollBar>(StableId("supplement.scroll-bar"));
    std::shared_ptr<RangeControl> range_control = make_control<RangeControl>(StableId("supplement.range-control"));
    (*button_base).set_requested_bounds({10, 10, 120, 28});
    (*scroll_bar).set_requested_bounds({10, 50, 200, 16});
    (*range_control).set_requested_bounds({10, 80, 200, 24});
    (*root).add_child(button_base);
    (*root).add_child(scroll_bar);
    (*root).add_child(range_control);
    std::shared_ptr<Sparkline> sparkline = make_control<Sparkline>(StableId("supplement.sparkline"));
    std::shared_ptr<ValueBar> value_bar = make_control<ValueBar>(StableId("supplement.value-bar"));
    std::shared_ptr<PlotView> plot_view = make_control<PlotView>(StableId("supplement.plot-view"));
    std::shared_ptr<WizardPage> wizard_page = make_control<WizardPage>(StableId("supplement.wizard-page"));
    std::shared_ptr<Callout> callout = make_control<Callout>(StableId("supplement.callout"));
    std::shared_ptr<ModalLayer> modal_layer = make_control<ModalLayer>(StableId("supplement.modal-layer"));
    std::shared_ptr<HelpBookViewer> help_book_viewer =
        make_control<HelpBookViewer>(StableId("supplement.help-book-viewer"));
    (*sparkline).set_requested_bounds({330, 10, 160, 30});
    (*value_bar).set_requested_bounds({330, 50, 160, 24});
    (*plot_view).set_requested_bounds({10, 150, 300, 150});
    (*wizard_page).set_requested_bounds({330, 150, 200, 100});
    (*callout).set_requested_bounds({550, 150, 200, 80});
    (*modal_layer).set_requested_bounds({550, 250, 160, 80});
    (*help_book_viewer).set_requested_bounds({10, 320, 500, 260});
    (*root).add_child(sparkline);
    (*root).add_child(value_bar);
    (*root).add_child(plot_view);
    (*root).add_child(wizard_page);
    (*root).add_child(callout);
    (*root).add_child(modal_layer);
    (*root).add_child(help_book_viewer);
    std::unique_ptr<Window> window = std::make_unique<Window>(root, supplement_size);
    (*window).perform_layout();
    return window;
}

class GallerySession final {
public:
    // Installs CancellingDialogHandler on the host's services.
    GallerySession(Window& gallery, host::HeadlessHost& host, Window& supplement)
        : gallery_(gallery), host_(host), supplement_(supplement) {
        host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(host_.services());
        services.set_dialog_handler(CancellingDialogHandler{});
    }
    GallerySession(const GallerySession&) = delete;
    GallerySession& operator=(const GallerySession&) = delete;

    // The first line the fixture writes: the Gallery's page identities.
    [[nodiscard]] std::string ready_line() const {
        std::ostringstream stream;
        stream << "ready {\"pages\":[";
        for (std::size_t index = 0; index < gallery::gallery_pages.size(); ++index) {
            if (index != 0U) stream << ',';
            append_string(stream, gallery::gallery_pages[index].id);
        }
        stream << "]}";
        std::string line = stream.str();
        return line;
    }

    // Runs one command; returns its answer line without the newline. Sets
    // `finished` when the command was quit.
    [[nodiscard]] std::string handle(std::string_view line, bool& finished) {
        finished = false;
        const std::size_t space = line.find(' ');
        const std::string_view verb = line.substr(0, space);
        const std::string_view argument = space == std::string_view::npos ? std::string_view{} : line.substr(space + 1U);
        if (verb == "page") return select_page(argument);
        if (verb == "report") return answer_report(gallery_, page_);
        if (verb == "supplement") return answer_report(supplement_, "supplement");
        if (verb == "reset") return reset();
        if (verb == "arm") return arm(argument);
        if (verb == "replace") return replace(argument);
        if (verb == "quit") {
            finished = true;
            return "bye";
        }
        std::string refusal = "error unknown command";
        return refusal;
    }

private:
    // Runs the work the Gallery posted to its dispatcher and lays out both
    // windows. The headless host owns no clock, so timers and animations do
    // not advance between commands.
    void settle() {
        static_cast<void>(host_.pump_dispatcher());
        gallery_.perform_layout();
        supplement_.perform_layout();
    }

    std::string answer_report(Window& window, std::string_view page) {
        settle();
        const SemanticSnapshot snapshot = window.semantic_snapshot();
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream.precision(17);
        const MetricsSnapshot metrics = window.metrics_snapshot();
        stream << "report {\"page\":";
        append_string(stream, page);
        stream << ",\"generation\":" << snapshot.generation << ",\"metrics\":{\"mutations\":" << metrics.mutations
               << ",\"dirty_marks\":" << metrics.dirty_marks << ",\"activations\":" << metrics.activations
               << ",\"focus_transitions\":" << metrics.focus_transitions << "},\"armed\":{\"stable_id\":";
        append_string(stream, armed_id_);
        stream << ",\"clicks\":" << counter_.clicks << "},\"replacement_clicks\":" << replacement_counter_.clicks
               << ",\"controls\":[";
        // The root tree, then the tree of each snapshot root that lies
        // outside it (an overlay, or a dialog's scope), found by its stable
        // identity and listed once.
        bool first = true;
        append_controls(stream, window.root(), first);
        std::vector<Control::Ptr> listed{window.root()};
        for (std::size_t index = 0; index < snapshot.roots.size(); ++index) {
            Control::Ptr top = window.find(snapshot.roots[index].stable_id);
            while (top && (*top).parent()) top = (*top).parent();
            if (!top || std::find(listed.begin(), listed.end(), top) != listed.end()) continue;
            listed.push_back(top);
            append_controls(stream, top, first);
        }
        stream << "],\"roots\":[";
        for (std::size_t index = 0; index < snapshot.roots.size(); ++index) {
            if (index != 0U) stream << ',';
            append_node(stream, snapshot.roots[index]);
        }
        stream << "]}";
        std::string line = stream.str();
        return line;
    }

    std::string select_page(std::string_view page) {
        if (!gallery::select_gallery_page(gallery_, page)) {
            std::string refusal = "error unknown page";
            return refusal;
        }
        page_ = std::string(page);
        std::string answer = answer_report(gallery_, page_);
        return answer;
    }

    // Whether an overlay or a modal dialog is open over the Gallery: a focus
    // scope is active, or a snapshot root lies outside the root tree. (The
    // Gallery's root panel is not exposed, so its children are roots too.)
    bool overlaid() {
        if (gallery_.active_focus_scope_root()) return true;
        const SemanticSnapshot snapshot = gallery_.semantic_snapshot();
        for (std::size_t index = 0; index < snapshot.roots.size(); ++index) {
            Control::Ptr top = gallery_.find(snapshot.roots[index].stable_id);
            while (top && (*top).parent()) top = (*top).parent();
            if (top && top != gallery_.root()) return true;
        }
        return false;
    }

    // Escape closes the innermost overlay or dialog; six presses close any
    // stack the Gallery opens. Then the page the client chose is selected
    // again, since a default action may navigate.
    std::string reset() {
        settle();
        for (int press = 0; press < 6 && overlaid(); ++press) {
            KeyEvent down;
            down.action = KeyAction::down;
            down.physical_key = PhysicalKey::escape;
            KeyEvent up = down;
            up.action = KeyAction::up;
            static_cast<void>(gallery_.dispatch_key(down));
            static_cast<void>(gallery_.dispatch_key(up));
            settle();
        }
        // A toast takes no focus and Escape does not close it (it closes on
        // its duration, its close mark or its action), so it is hidden here.
        if (gallery_.toast_visible()) {
            gallery_.hide_toast();
            settle();
        }
        if (overlaid()) {
            std::string refusal = "error an overlay or dialog stays open after Escape";
            return refusal;
        }
        if (!page_.empty() && gallery::selected_gallery_page(gallery_) != page_) {
            static_cast<void>(gallery::select_gallery_page(gallery_, page_));
        }
        std::string answer = answer_report(gallery_, page_);
        return answer;
    }

    std::string arm(std::string_view stable_id) {
        clicked_ = SubscriptionToken{};
        counter_.clicks = 0;
        armed_id_ = std::string(stable_id);
        Control::Ptr control = gallery_.find(stable_id);
        if (!control) control = supplement_.find(stable_id);
        ButtonBase* const button = dynamic_cast<ButtonBase*>(control.get());
        if (button == nullptr) {
            std::string answer = "armed 0";
            return answer;
        }
        clicked_ = (*button).clicked().subscribe(Delegate<ButtonBase&>::bind<ClickCounter, &ClickCounter::clicked>(counter_));
        std::string answer = "armed 1";
        return answer;
    }

    // Disposes the Button `stable_id` names and puts a new Button with the
    // same stable identity and text at its index. A client still holding the
    // old button's object must be refused; the new button answers to its own
    // object.
    std::string replace(std::string_view stable_id) {
        const Control::Ptr control = gallery_.find(stable_id);
        Button* const button = dynamic_cast<Button*>(control.get());
        const Control::Ptr parent = control ? (*control).parent() : Control::Ptr{};
        if (button == nullptr || !parent) {
            std::string refusal = "error no Button with a parent has that stable identity";
            return refusal;
        }
        const std::string text = (*button).text.read();
        const std::optional<std::size_t> index = (*parent).child_index_from_top((*control).runtime_id());
        const Rect bounds = (*control).requested_bounds();
        Control::Ptr removed = (*parent).remove_child((*control).runtime_id());
        removed.reset();
        replacement_ = make_control<Button>(StableId(std::string(stable_id)), text);
        (*replacement_).set_requested_bounds(bounds);
        (*parent).add_child(replacement_);
        if (index) static_cast<void>((*parent).set_child_index_from_top((*replacement_).runtime_id(), *index));
        replacement_counter_.clicks = 0;
        replacement_clicked_ = (*replacement_).clicked().subscribe(
            Delegate<ButtonBase&>::bind<ClickCounter, &ClickCounter::clicked>(replacement_counter_));
        std::string answer = answer_report(gallery_, page_);
        return answer;
    }

    Window& gallery_;
    host::HeadlessHost& host_;
    Window& supplement_;
    std::string page_;
    std::string armed_id_;
    ClickCounter counter_;
    ClickCounter replacement_counter_;
    std::shared_ptr<Button> replacement_;
    // Declared after the counters they report into, so they disconnect first.
    SubscriptionToken clicked_;
    SubscriptionToken replacement_clicked_;
};

} // namespace gui_forms::accessibility_session
