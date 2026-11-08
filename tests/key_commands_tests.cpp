// Named keyboard commands (owner decision DK-30, interview G5-1 and G6-1):
// the PC convention's map; the headless host's reported map and its
// installation in an attached window; a macOS-style host map binding Cmd+W,
// reaching PageStrip and PageView; application rebinding over a host's
// defaults and its restoration; the keyboard context request;
// refusals that leave the map unchanged.

#include "gui_forms/application.hpp"
#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

KeyEvent key(std::uint32_t physical, Modifier modifiers = Modifier::none) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = physical;
    event.modifiers = modifiers;
    return event;
}

// The host map a macOS host reports (src/host/macos keeps the real one; the
// macOS build is CI's): Cmd+W closes a document, every other command keeps
// the PC keys.
KeyMap macos_style_key_map() {
    KeyMap map = pc_key_map();
    map.set_binding(KeyCommand::close_document, {{PhysicalKey::w, Modifier::meta}, {}});
    return map;
}

HostCapabilities macos_style_capabilities() {
    HostCapabilities capabilities{HostCapabilities::current_protocol_version, "macos-style-fixture",
                                  HostCapability::keyboard_input, macos_style_key_map()};
    return capabilities;
}

void add_pages(PageSet& pages, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        PageRecord record{};
        record.id = PageId{"p" + std::to_string(index)};
        record.title = "Page " + std::to_string(index);
        record.closable = true;
        const PageEditStatus status = pages.insert(record, PagePlacement::end_of_group());
        require(status == PageEditStatus::applied, "the fixture's pages are inserted");
    }
}

struct CloseLog final {
    std::vector<PageCloseRequest> requests;
};

struct RecordClose final {
    CloseLog* log{};
    void operator()(PageCloseRequest& request) const {
        (*log).requests.push_back(request);
        request.handled = true;
    }
};

struct RecordContextMenu final {
    std::vector<PageContextMenuRequest>* menus{};
    void operator()(PageContextMenuRequest& request) const {
        (*menus).push_back(request);
        request.handled = true;
    }
};

void pc_convention() {
    const KeyMap map = pc_key_map();
    require(map.matches(KeyCommand::close_document, key(PhysicalKey::f4, Modifier::control)) &&
                !map.matches(KeyCommand::close_document, key(PhysicalKey::w, Modifier::meta)) &&
                !map.matches(KeyCommand::close_document, key(PhysicalKey::f4, Modifier::control | Modifier::shift)),
            "the PC convention closes with Ctrl+F4 and exactly those modifiers");
    require(map.matches(KeyCommand::cycle_forward, key(PhysicalKey::tab, Modifier::control)) &&
                map.matches(KeyCommand::cycle_backward, key(PhysicalKey::tab, Modifier::control | Modifier::shift)) &&
                map.matches(KeyCommand::next_tab, key(PhysicalKey::page_down, Modifier::control)) &&
                map.matches(KeyCommand::move_tab_earlier,
                            key(PhysicalKey::page_up, Modifier::control | Modifier::shift)),
            "the tab keys are Ctrl+Tab and Ctrl+PageDown with their variants");
    require(map.matches(KeyCommand::next_document, key(PhysicalKey::f6, Modifier::control)) &&
                map.matches(KeyCommand::next_tool_panel, key(PhysicalKey::f7, Modifier::alt)) &&
                map.matches(KeyCommand::position_menu, key(PhysicalKey::minus, Modifier::alt)) &&
                map.matches(KeyCommand::hide_tool_panel, key(PhysicalKey::escape, Modifier::shift)) &&
                map.matches(KeyCommand::maximize_document_area,
                            key(PhysicalKey::f12, Modifier::control | Modifier::shift)),
            "docking's keys are Visual Studio's (G2-1, G7-1)");
    KeyEvent released = key(PhysicalKey::f4, Modifier::control);
    released.action = KeyAction::up;
    KeyEvent repeated = key(PhysicalKey::f4, Modifier::control);
    repeated.repeat = true;
    require(!map.matches(KeyCommand::close_document, released) && map.matches(KeyCommand::close_document, repeated),
            "a command is a key-down, repeats included");
    for (std::size_t index = 0; index < key_command_count; ++index) {
        const KeyCommand command = static_cast<KeyCommand>(index);
        require(key_command_name(command) != "unknown" && !map.binding(command).primary.empty(),
                "every command has a name and a PC default");
    }
    require(key_command_name(KeyCommand::count) == "unknown", "a value outside the commands has no name");
    require(!map.matches(KeyCommand::count, key(PhysicalKey::f4, Modifier::control)),
            "a value outside the commands matches nothing");
}

void refusals_leave_the_map() {
    KeyMap map = pc_key_map();
    const KeyMap before = map;
    bool refused = false;
    try {
        map.set_binding(KeyCommand::close_document, {{PhysicalKey::w, static_cast<Modifier>(0x30U)}, {}});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && map == before, "a chord with unknown modifier bits is refused, the map unchanged");
    refused = false;
    try {
        map.set_binding(KeyCommand::close_document, {{}, {0U, Modifier::control}});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && map == before, "modifiers without a key are refused");
    refused = false;
    try {
        static_cast<void>(map.binding(KeyCommand::count));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a command outside the enumeration is refused");

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    Window window(root, {100.0, 100.0});
    refused = false;
    try {
        window.rebind_key_command(KeyCommand::count, {});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && window.key_map() == pc_key_map(), "a window refuses an unknown command, its map unchanged");
}

void headless_host_map() {
    const HostCapabilities capabilities = host::headless_capabilities();
    require(capabilities.protocol_version >= 8U && capabilities.key_map == pc_key_map(),
            "the headless host reports the PC convention from protocol 8");
    const std::string json = capabilities.to_json();
    require(json.find("\"key_map\":{\"close_document\":[{\"key\":61,\"modifiers\":2}]") != std::string::npos &&
                json.find("\"next_tab\":[{\"key\":78,\"modifiers\":2}]") != std::string::npos &&
                json.find("\"context_menu\"") == std::string::npos,
            "the capability report lists each command's chords");

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    Window window(root, {100.0, 100.0});
    require(window.key_map() == pc_key_map() && (*root).effective_key_map() == pc_key_map(),
            "a window starts with the PC convention");
    window.rebind_key_command(KeyCommand::next_document, {{PhysicalKey::f8, Modifier::control}, {}});
    {
        host::HeadlessHost host(window);
        require(window.key_map().matches(KeyCommand::close_document, key(PhysicalKey::f4, Modifier::control)) &&
                    window.key_map().matches(KeyCommand::next_document, key(PhysicalKey::f8, Modifier::control)),
                "the headless host installs its map, under a rebinding made before it attached");
    }
    std::shared_ptr<Panel> detached = make_control<Panel>(StableId("detached"));
    require((*detached).effective_key_map() == pc_key_map(), "a detached control asks the PC convention");
}

// A strip of closable pages in a window whose host reports the macOS map.
void macos_style_map_reaches_the_strip() {
    PageSet pages{};
    add_pages(pages, 3U);
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    Window window(strip, {400.0, 26.0});
    HostSession session(window, macos_style_capabilities());
    window.perform_layout();
    require(window.key_map() == macos_style_key_map(), "the session installs the host's map");
    CloseLog log{};
    SubscriptionToken closing = (*strip).page_close_requested().subscribe(RecordClose{&log});
    require(window.request_focus(strip), "the strip takes focus");
    require(!window.dispatch_key(key(PhysicalKey::f4, Modifier::control)) && log.requests.empty(),
            "under the macOS map Ctrl+F4 closes nothing");
    require(window.dispatch_key(key(PhysicalKey::w, Modifier::meta)) && log.requests.size() == 1U &&
                log.requests[0].page.value == "p0" && log.requests[0].cause == PageCloseCause::keyboard,
            "Cmd+W asks to close the selected page, from the keyboard");
    require(window.dispatch_key(key(PhysicalKey::page_down, Modifier::control)) &&
                (*pages.selected_page()).value == "p1",
            "the macOS map keeps Ctrl+PageDown");
}

void macos_style_map_reaches_the_view() {
    PageViewOptions options{};
    options.preset = PageViewPreset::document;
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("docs"), options);
    std::shared_ptr<TextBox> editor = make_control<TextBox>(StableId("docs.editor"));
    PageRecord record{};
    record.id = PageId{"d0"};
    record.title = "Document";
    record.closable = true;
    require((*view).add_page(record, editor) == PageEditStatus::applied, "the document is added");
    Window window(view, {400.0, 200.0});
    HostSession session(window, macos_style_capabilities());
    window.perform_layout();
    CloseLog log{};
    SubscriptionToken closing = (*(*view).strip()).page_close_requested().subscribe(RecordClose{&log});
    require(window.request_focus(editor), "the document's editor takes focus");
    require(window.dispatch_key(key(PhysicalKey::w, Modifier::meta)) && log.requests.size() == 1U &&
                log.requests[0].page.value == "d0",
            "Cmd+W inside the content closes through the view");
}

void rebinding_over_the_host() {
    PageSet pages{};
    add_pages(pages, 3U);
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    Window window(strip, {400.0, 26.0});
    window.perform_layout();
    CloseLog log{};
    SubscriptionToken closing = (*strip).page_close_requested().subscribe(RecordClose{&log});
    require(window.request_focus(strip), "the strip takes focus");
    window.rebind_key_command(KeyCommand::close_document, {{PhysicalKey::w, Modifier::control}, {}});
    require(!window.dispatch_key(key(PhysicalKey::f4, Modifier::control)) &&
                window.dispatch_key(key(PhysicalKey::w, Modifier::control)) && log.requests.size() == 1U,
            "an application's rebinding replaces Ctrl+F4 with Ctrl+W");
    {
        HostSession session(window, macos_style_capabilities());
        require(window.dispatch_key(key(PhysicalKey::w, Modifier::control)) && log.requests.size() == 2U &&
                    !window.dispatch_key(key(PhysicalKey::w, Modifier::meta)),
                "a host attaching later keeps the application's rebinding");
        window.restore_key_command(KeyCommand::close_document);
        require(window.dispatch_key(key(PhysicalKey::w, Modifier::meta)) && log.requests.size() == 3U,
                "restoring the command gives the host's default, Cmd+W");
    }
    window.rebind_key_command(KeyCommand::close_document, {});
    require(!window.dispatch_key(key(PhysicalKey::w, Modifier::meta)) &&
                !window.dispatch_key(key(PhysicalKey::f4, Modifier::control)) && log.requests.size() == 3U,
            "an empty binding leaves the command without keys");
    window.rebind_key_command(KeyCommand::next_tab, {{PhysicalKey::right, Modifier::alt}, {}});
    require(window.dispatch_key(key(PhysicalKey::right, Modifier::alt)) && (*pages.selected_page()).value == "p1" &&
                !window.dispatch_key(key(PhysicalKey::page_down, Modifier::control)),
            "every strip command follows its rebinding");
}

void context_menu_key() {
    PageSet pages{};
    add_pages(pages, 3U);
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    Window window(strip, {400.0, 26.0});
    window.perform_layout();
    std::vector<PageContextMenuRequest> menus;
    SubscriptionToken menu_token = (*strip).page_context_menu_requested().subscribe(RecordContextMenu{&menus});
    require(window.request_focus(strip), "the strip takes focus");
    require(window.dispatch_context_request(ContextRequestOrigin::keyboard, Point{}) && menus.size() == 1U &&
                menus[0].page.value == "p0" && menus[0].cause == PageContextMenuCause::keyboard,
            "a keyboard context request asks for the keyboard's tab's menu");
    require(!window.dispatch_key(key(PhysicalKey::context_menu)) &&
                !window.dispatch_key(key(PhysicalKey::f10, Modifier::shift)) && menus.size() == 1U,
            "the keys themselves are the host's: the core binds none");
}

// One key map per application (owner decision DK-44): windows sharing one
// KeyBindings see each other's rebindings; the first host seeds it, a later
// one does not; Application's set is one object.
void application_key_map() {
    std::shared_ptr<Panel> first_root = make_control<Panel>(StableId("first"));
    std::shared_ptr<Panel> second_root = make_control<Panel>(StableId("second"));
    Window first(first_root, {100.0, 100.0});
    Window second(second_root, {100.0, 100.0});
    // A window made alone holds no table until one is needed (Part K's lazy
    // tables); sharing makes it, and each window makes its own.
    require(first.share_key_bindings() != second.share_key_bindings(), "windows made alone have sets of their own");
    second.set_key_bindings(first.key_bindings());
    second.rebind_key_command(KeyCommand::next_tab, {{PhysicalKey::right, Modifier::alt}, {}});
    require(first.key_map().matches(KeyCommand::next_tab, key(PhysicalKey::right, Modifier::alt)),
            "a rebinding in one window holds in the windows sharing its set");
    {
        HostSession seeding(first, macos_style_capabilities());
        require(second.key_map().matches(KeyCommand::close_document, key(PhysicalKey::w, Modifier::meta)) &&
                    (*second.key_bindings()).seeded(),
                "the first host seeds the shared set for every window");
    }
    {
        HostSession later(second, host::headless_capabilities());
        require(first.key_map().matches(KeyCommand::close_document, key(PhysicalKey::w, Modifier::meta)),
                "a later host does not reseed the set");
    }
    first.restore_key_command(KeyCommand::next_tab);
    require(second.key_map().matches(KeyCommand::next_tab, key(PhysicalKey::page_down, Modifier::control)),
            "restoring returns every sharing window to the default");
    bool refused = false;
    try {
        first.set_key_bindings({});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && first.key_bindings() == second.key_bindings(), "a null set is refused");
    const ApplicationCreateResult created = Application::create(ApplicationConfig{});
    require(static_cast<bool>(created) && (*created.application).key_bindings() != nullptr &&
                (*created.application).key_bindings() == (*created.application).key_bindings(),
            "the application has one set");
}

} // namespace

int main() {
    try {
        pc_convention();
        refusals_leave_the_map();
        headless_host_map();
        macos_style_map_reaches_the_strip();
        macos_style_map_reaches_the_view();
        rebinding_over_the_host();
        context_menu_key();
        application_key_map();
        std::cout << "Key commands: the PC convention, the headless host's map, a macOS-style map with Cmd+W in "
                     "PageStrip and PageView, rebinding and restoring over a host, the context-menu key and "
                     "refusals passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_key_commands_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
