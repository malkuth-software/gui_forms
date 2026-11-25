#pragma once

// Mockups (docs/user/mockups.md): a form document, its fixture data and a
// theme sheet shown at run time, with no compiled code behind them. A
// mockup is files: a program such as the devkit's gui-forms-mockup reads
// them, and a mockup added or edited later is shown without a build.
//
//     gui_forms::FormMockup mockup(host);   // host: a panel in the program's window
//     const gui_forms::FormMockupText form{view_of("claim.form"), view_of(form_text)};
//     const gui_forms::FormMockupText data{view_of("claim.fixture"), view_of(fixture_text)};
//     if (mockup.load(gui_forms::FormDocumentSchema::registry(), form, data) ==
//         gui_forms::FormMockupLoadOutcome::refused) {
//         // mockup.check(): each diagnostic's file, line, column, code and detail
//     }
//
// A load reads the document as the form compiler does (the same E8xx
// diagnostics, at the same lines and columns), with three differences that
// make it a mockup:
// - Its bindings read the fixture, not a model: each `<bind to="x"/>` gets
//   the fixture's value or records named x, or a stand-in (the property's
//   default, an empty record set) when the fixture has none. A command
//   binding gets a command that records its invocations.
// - An `on-<interaction>` attribute (`on-click="save_claim"`) is a declared
//   handler, never refused: it names no code and records each time the
//   interaction is raised. The form compiler still refuses it (E816).
// - The fixture fills the lists, grids and trees it names by stable ID.
//
// Retained: a load builds the tree once, and a load that succeeds replaces
// the shown tree in one step (hot reload); nothing is rebuilt per frame or
// per event. A load that is refused leaves the shown tree as it was and
// says why in check(). UI thread throughout: the host's.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/forms/form_documents/form_documents.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include "stxlib/types.hpp"
#include "stxlib/unique.hpp"
#include "stxlib/view.hpp"

#include <cstdint>

namespace gui_forms {

class Control;

namespace detail {
struct FormMockupState;
} // namespace detail

// One text a mockup reads: its file name, for the diagnostics, and its
// UTF-8 bytes. Both borrowed for the call that takes it. An empty text is
// an empty fixture or sheet.
struct GUI_FORMS_API FormMockupText final {
    stxlib::View<char> file{};
    stxlib::View<char> text{};
};

// One declared handler of the shown tree: `on-click="save_claim"` on
// `claim.save`, or a command binding's source (interaction `invoked`).
// Texts borrowed from the mockup until its next load or clear.
struct GUI_FORMS_API FormMockupHandler final {
    stxlib::View<char> handler{};
    // The object's stable ID; a command's source name for a command.
    stxlib::View<char> stable_id{};
    // As registered: `click`, `checked_changed`, `invoked`.
    stxlib::View<char> interaction{};
    // Whether the mockup records it. False for an interaction the object's
    // type does not raise, or one the mockup cannot observe (listed, never
    // refused): the base control's click, double_click, mouse_enter,
    // mouse_hover and mouse_leave, a button's clicked (its click too, raised
    // by every activation, perform_click included), every
    // `<property>_changed` of a property the control has, and every
    // command's invoked are observed.
    bool observed{};
};

// One recorded event. Texts borrowed from the mockup until the event is
// dropped (clear_events, or kept_events newer ones).
struct GUI_FORMS_API FormMockupEvent final {
    // Counts every event since the mockup was made, from 1.
    stxlib::u64 sequence{};
    stxlib::View<char> handler{};
    stxlib::View<char> stable_id{};
    stxlib::View<char> interaction{};
};

// Called on the UI thread after each event is recorded, for a log or a
// console; `event` is borrowed for the call. It must not load or clear the
// mockup that called it.
using FormMockupEventSink = void (*)(void* context, const FormMockupEvent& event);

enum class FormMockupLoadOutcome : std::uint8_t {
    // The new tree is shown.
    loaded,
    // The document, the fixture or the load was refused: check() says
    // where, and the tree shown before (or none) stays.
    refused,
};

class GUI_FORMS_API FormMockup final {
public:
    // The events kept; older ones are dropped, newer ones still counted.
    static constexpr stxlib::usize kept_events = 1024U;

    // A mockup shown in `host`, which it borrows: a control the program
    // keeps in its window (a panel), whose children the mockup replaces.
    // Declare the mockup after the host, so it ends first.
    explicit FormMockup(Control& host);
    // Removes the shown tree from the host.
    ~FormMockup();
    FormMockup(const FormMockup&) = delete;
    FormMockup& operator=(const FormMockup&) = delete;
    FormMockup(FormMockup&&) = delete;
    FormMockup& operator=(FormMockup&&) = delete;

    // Reads `document` and `fixture` (an empty text for none), loads them
    // against `schema` and, when nothing is refused, shows the new tree in
    // place of the old one in one step. The document's root may be a form
    // (its control is shown; its accept and cancel buttons are given to the
    // host's window) or a control. A record set or tree the fixture fills
    // needs the host in a window. Events recorded so far are kept.
    [[nodiscard]] FormMockupLoadOutcome load(const FormDocumentSchema& schema, const FormMockupText& document,
                                             const FormMockupText& fixture);
    // Removes the shown tree, if any.
    void clear();

    // The theme the shown tree uses: `sheet` (a theme sheet's text; empty
    // for none) compiled onto `base`, set as the host's theme override
    // (Control::set_theme_override), so that only the mockup's tree shows
    // it. A refused sheet changes nothing; the result holds its
    // diagnostics. Needs the host in a window, whose resources compile it.
    ThemeCompileResult set_theme(ThemeBase base, const FormMockupText& sheet);

    // Whether a tree is shown, and how many loads have succeeded.
    [[nodiscard]] bool shown() const noexcept;
    [[nodiscard]] stxlib::u64 generation() const noexcept;
    // The last load's diagnostics, document's and fixture's together, in
    // the order found; warnings only after a load that succeeded.
    [[nodiscard]] const FormDocumentCheck& check() const& noexcept { return check_; }
    const FormDocumentCheck& check() const&& = delete;
    // The shown form's title and client size; empty and zero for a control
    // root, a form that sets none, or no tree.
    [[nodiscard]] stxlib::View<char> title() const& noexcept;
    stxlib::View<char> title() const&& = delete;
    [[nodiscard]] Size client_size() const noexcept;

    // The shown tree's commands, in the order the document first binds
    // them, then its declared handlers, in document order.
    [[nodiscard]] stxlib::usize handler_count() const noexcept;
    // The handler at `index` (below handler_count()); empty past the end.
    [[nodiscard]] FormMockupHandler handler(stxlib::usize index) const& noexcept;
    FormMockupHandler handler(stxlib::usize index) const&& = delete;

    // The events kept, oldest first.
    [[nodiscard]] stxlib::usize event_count() const noexcept;
    // The event at `index` (below event_count()); empty past the end.
    [[nodiscard]] FormMockupEvent event(stxlib::usize index) const& noexcept;
    FormMockupEvent event(stxlib::usize index) const&& = delete;
    void clear_events() noexcept;
    // Calls `sink` with `context` after each event is recorded; a null
    // sink stops it. `context` must outlive the mockup or the next call.
    void set_event_sink(FormMockupEventSink sink, void* context) noexcept;

private:
    stxlib::Unique<detail::FormMockupState> state_{};
    FormDocumentCheck check_{};
};

} // namespace gui_forms
