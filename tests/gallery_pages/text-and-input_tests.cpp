#include "support.hpp"

namespace gallery_pages_tests {

void test_text_and_input(gui_forms::Window& window,
                         gui_forms::host::HeadlessHost& host) {
    select_page(window, "text-and-input");
    std::shared_ptr<gui_forms::TextBox> text = std::dynamic_pointer_cast<TextBox>(window.find("gallery.text.primary"));
    std::shared_ptr<gui_forms::TextBox> password = std::dynamic_pointer_cast<TextBox>(
        window.find("gallery.text.password"));
    // The editing surface is one CommandBar; Undo and Redo are Commands whose
    // availability follows the field's history before any input arrives.
    const Control::Ptr undo = window.find("gallery.text.command.item.2");
    const Control::Ptr redo = window.find("gallery.text.command.item.3");
    require(undo && redo && !(*undo).effectively_enabled() && !(*redo).effectively_enabled(),
            "gallery Undo/Redo Commands must project a clean history as disabled buttons");
    require(window.semantic_snapshot().to_json().find("\"role\":\"toolbar\"") != std::string::npos,
            "gallery editing CommandBar must publish toolbar semantics");
    require(text && window.request_focus(text) && window.dispatch_text({"dogfood Ω"}) &&
                (*text).text.read() == "dogfood Ω" && (*text).can_undo(),
            "gallery public TextBox must accept normalized Unicode input");
    require((*undo).effectively_enabled(),
            "typing must enable the Undo Command through the bound bar button");
    click(window, window.find("gallery.text.command.item.0"));
    click(window, window.find("gallery.text.command.item.1"));
    require((*text).text.read() == "GUI.Forms",
            "gallery TextBox commands must operate on the public selection surface");
    click(window, undo);
    require((*text).text.read() == "dogfood Ω" && (*redo).effectively_enabled(),
            "gallery TextBox Undo command must restore its prior snapshot and enable Redo");
    (*text).select_all();
    click(window, window.find("gallery.text.command.item.4"));
    (*text).text = "replace me";
    (*text).select_all();
    click(window, window.find("gallery.text.command.item.6"));
    require((*text).text.read() == "dogfood Ω",
            "gallery clipboard buttons must round-trip through HostServices");
    (*text).select_all();
    click(window, window.find("gallery.text.command.item.5"));
    require((*text).text.read().empty(),
            "gallery Cut must mutate through the public TextBox command");
    click(window, window.find("gallery.text.command.item.6"));
    require((*text).text.read() == "dogfood Ω",
            "gallery Paste must restore the cut Unicode text");
    require(password && (*password).password_protected() &&
                (*password).semantic_descriptor().value.empty() &&
                has_semantic_state((*password).semantic_descriptor().states,
                                   SemanticState::protected_content) &&
                window.semantic_snapshot().to_json().find("Portsmouth-Ω-2026") ==
                    std::string::npos,
            "gallery protected field must mask paint/semantic export by contract");
    (*password).select_all();
    require(!(*password).copy(),
            "gallery protected field must reject clipboard export");
    const HostServicesSnapshot clipboard_metrics = host.services().snapshot();
    require(clipboard_metrics.clipboard_writes == 2U &&
                clipboard_metrics.clipboard_reads == 2U,
            "gallery clipboard commands must leave exact transport accounting");

    const std::shared_ptr<gui_forms::MaskedTextBox> phone = std::dynamic_pointer_cast<MaskedTextBox>(
        window.find("gallery.text.masked.phone"));
    const std::shared_ptr<gui_forms::Label> masked_status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.text.masked.status"));
    require(phone && masked_status && (*(*phone).editor()).text.read() == "(___) ___-____",
            "the masked specimen shows its prompts and literals");
    require(window.request_focus((*phone).editor()) && window.dispatch_text({"555"}) &&
                (*phone).text.read() == "(555" &&
                (*masked_status).text.read() == "Telephone (555 · incomplete",
            "typed digits pass the mask and report to the page");
    require(!window.dispatch_text({"x"}) &&
                (*masked_status).text.read().find("refused \"x\" at position 6") != std::string::npos,
            "a refused letter is reported with its position");

    const std::shared_ptr<gui_forms::TextBox> city = std::dynamic_pointer_cast<TextBox>(
        window.find("gallery.text.complete.city"));
    const std::shared_ptr<gui_forms::ComboBox> band = std::dynamic_pointer_cast<ComboBox>(
        window.find("gallery.text.complete.band"));
    const std::shared_ptr<gui_forms::Label> complete_status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.text.complete.status"));
    require(city && band && complete_status && (*band).editor(), "the auto-complete specimens are on the page");
    require(window.request_focus(city) && window.dispatch_text({"ed"}) && (*city).text.read() == "edinburgh" &&
                (*city).auto_complete_open() && (*city).auto_complete_suggestions().size() == 2U,
            "the city field appends the first match and suggests both");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::tab}) && (*city).text.read() == "edinburgh" &&
                (*complete_status).text.read() == "Accepted \"edinburgh\"",
            "Tab accepts the appended city and the page reports it");
    require(window.request_focus((*band).editor()) && window.dispatch_text({"m"}) &&
                (*(*band).editor()).auto_complete_suggestions().size() == 2U &&
                window.dispatch_key({KeyAction::down, PhysicalKey::down}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*band).text.read() == "Medium wave" && (*band).selected_index.read() == 1U,
            "the band field suggests from its items and the accepted item is selected");

    const std::shared_ptr<gui_forms::TokenBox> recipients = std::dynamic_pointer_cast<TokenBox>(
        window.find("gallery.text.tokens.recipients"));
    const std::shared_ptr<gui_forms::Label> tokens_status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.text.tokens.status"));
    require(recipients && tokens_status && (*recipients).tokens().size() == 1U,
            "the token box specimen starts with one recipient");
    require(window.request_focus((*recipients).editor()) && window.dispatch_text({"Alan Turing,"}) &&
                (*recipients).tokens().size() == 2U &&
                (*tokens_status).text.read() == "Added \"Alan Turing\" · recipients: 2",
            "a comma turns the typed name into a token and the page reports it");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::backspace}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::backspace}) &&
                (*recipients).tokens().size() == 1U &&
                (*tokens_status).text.read() == "Removed \"Alan Turing\" · recipients: 1",
            "Backspace selects and then removes the last token");

    const std::shared_ptr<gui_forms::RichTextBox> note = std::dynamic_pointer_cast<RichTextBox>(
        window.find("gallery.text.rich.note"));
    require(note && (*note).document().blocks.size() == 5U &&
                (*note).document().blocks[0].kind == MarkdownBlockKind::heading &&
                (*note).document().blocks[4].kind == MarkdownBlockKind::code_block &&
                (*note).document().links.size() == 1U,
            "the rich text specimen shows its heading, paragraph, list and code block");
    require((*note).text.read().starts_with("Release notes\nText may be bold, italic or code."),
            "the rich text specimen's plain text has no markup");

    // The search field (W-4): typing waits for the pause, Enter searches
    // now, and Escape clears, which shows everything again.
    const std::shared_ptr<SearchField> search =
        std::dynamic_pointer_cast<SearchField>(window.find("gallery.text.search.field"));
    const std::shared_ptr<gui_forms::Label> search_status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.text.search.status"));
    require(search && search_status && (*search).placeholder_text.read() == "Search the gallery",
            "the search field specimen shows its placeholder");
    require(window.request_focus((*search).editor()) && window.dispatch_text({"dock"}) &&
                (*search).text.read() == "dock" &&
                (*search_status).text.read() == "Type to search; Enter searches now and Escape clears",
            "typing waits for the pause before searching");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*search_status).text.read() == "Searching for \"dock\"",
            "Enter searches now");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) && (*search).text.read().empty() &&
                (*search_status).text.read() == "Showing everything",
            "Escape clears the field, then searches for everything");
}

} // namespace gallery_pages_tests
