// A mockup loaded at run time (docs/user/mockups.md): a form document and
// its fixture as text, shown in a panel of a window with no build step. The
// example clicks the mockup's button and reads the event its declared
// handler recorded, then reloads a broken document (refused, with its line
// and column; the shown tree stays) and a repaired one (shown in its place).

#include <gui_forms/controls/button.hpp>
#include <gui_forms/controls/check_box.hpp>
#include <gui_forms/controls/panel.hpp>
#include <gui_forms/forms.hpp>
#include <gui_forms/window.hpp>

#include <cstdio>
#include <memory>
#include <string_view>

namespace {

constexpr std::string_view form_text = R"(<?xml version="1.0" encoding="UTF-8"?>
<form xmlns="urn:gui-forms:form:1" id="claim.window" text="Claim" client-size="320 160">
  <panel id="claim.root">
    <check-box id="claim.urgent" text="Urgent" bounds="12 12 200 28">
      <bind property="checked" to="urgent"/>
    </check-box>
    <list-box id="claim.items" bounds="12 48 200 60"/>
    <button id="claim.save" text="Save" bounds="12 116 88 32" on-click="save_claim"/>
  </panel>
</form>
)";

constexpr std::string_view fixture_text = R"(<?xml version="1.0" encoding="UTF-8"?>
<fixture xmlns="urn:gui-forms:fixture:1">
  <value name="urgent" value="true"/>
  <records name="items" columns="item paid:number" shown-in="claim.items">
    <row values="Carpet 640.00"/>
    <row values="'Ceiling repair' 1250.00"/>
  </records>
</fixture>
)";

// The same document with an unknown type on line 4.
constexpr std::string_view broken_text = R"(<?xml version="1.0" encoding="UTF-8"?>
<form xmlns="urn:gui-forms:form:1" id="claim.window" text="Claim" client-size="320 160">
  <panel id="claim.root">
    <check-bx id="claim.urgent" text="Urgent" bounds="12 12 200 28"/>
  </panel>
</form>
)";

stxlib::View<char> view_of(std::string_view text) {
    return stxlib::View<char>(text.data(), text.size());
}

gui_forms::FormMockupText mockup_text(std::string_view file, std::string_view text) {
    gui_forms::FormMockupText result{};
    result.file = view_of(file);
    result.text = view_of(text);
    return result;
}

bool same(stxlib::View<char> text, std::string_view expected) {
    return std::string_view(text.data(), text.size()) == expected;
}

// The shown tree's control with `stable_id`, searched depth first.
gui_forms::Control::Ptr find(const gui_forms::Control::Ptr& from, std::string_view stable_id) {
    if (!from) return gui_forms::Control::Ptr();
    if ((*from).stable_id().value() == stable_id) return from;
    for (std::size_t index = 0U; index < (*from).child_count(); ++index) {
        gui_forms::Control::Ptr found = find((*from).child_at(index), stable_id);
        if (found) return found;
    }
    return gui_forms::Control::Ptr();
}

int fail(const char* why) {
    std::fprintf(stderr, "form_mockup: %s\n", why);
    return 1;
}

} // namespace

int main() {
    const std::shared_ptr<gui_forms::Panel> host = gui_forms::make_control<gui_forms::Panel>(gui_forms::StableId("host"));
    gui_forms::Window window(host, gui_forms::Size{320.0, 160.0});
    // Declared after the host it borrows, so it ends first.
    gui_forms::FormMockup mockup(*host);
    const gui_forms::FormDocumentSchema& schema = gui_forms::FormDocumentSchema::registry();
    const gui_forms::FormMockupText fixture = mockup_text("claim.fixture", fixture_text);

    if (mockup.load(schema, mockup_text("claim.form", form_text), fixture) != gui_forms::FormMockupLoadOutcome::loaded) {
        return fail("the mockup was refused");
    }
    if (mockup.generation() != 1U || !same(mockup.title(), "Claim")) return fail("the first load is not shown");

    // The fixture's value is bound; the declared handler is listed.
    const std::shared_ptr<gui_forms::CheckBox> urgent =
        std::dynamic_pointer_cast<gui_forms::CheckBox>(find(host, "claim.urgent"));
    if (!urgent || !(*urgent).checked.read()) return fail("the fixture value is not bound");
    if (mockup.handler_count() != 1U || !same(mockup.handler(0U).handler, "save_claim")) {
        return fail("the declared handler is not listed");
    }

    // A click records an event; nothing is refused.
    const std::shared_ptr<gui_forms::Button> save =
        std::dynamic_pointer_cast<gui_forms::Button>(find(host, "claim.save"));
    if (!save) return fail("no save button");
    static_cast<void>((*save).perform_click());
    if (mockup.event_count() == 0U) return fail("the click recorded nothing");
    const gui_forms::FormMockupEvent last = mockup.event(mockup.event_count() - 1U);
    if (!same(last.handler, "save_claim") || !same(last.stable_id, "claim.save")) return fail("wrong event");

    // A broken document is refused where it is wrong; the tree stays.
    if (mockup.load(schema, mockup_text("claim.form", broken_text), fixture) !=
        gui_forms::FormMockupLoadOutcome::refused) {
        return fail("the broken document was shown");
    }
    if (mockup.check().error_count() == 0U) return fail("the refusal says nothing");
    const gui_forms::FormDocumentDiagnostic first = mockup.check().diagnostic(0U);
    if (first.line != 4U) return fail("the refusal is not on line 4");
    if (mockup.generation() != 1U || !find(host, "claim.save")) return fail("the shown tree did not stay");

    // The repaired document replaces it in one step.
    if (mockup.load(schema, mockup_text("claim.form", form_text), fixture) != gui_forms::FormMockupLoadOutcome::loaded) {
        return fail("the repaired document was refused");
    }
    if (mockup.generation() != 2U) return fail("the reload is not shown");
    return 0;
}
