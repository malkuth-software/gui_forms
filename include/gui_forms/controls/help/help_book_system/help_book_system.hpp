#pragma once

#include "gui_forms/application/application.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/help/help_book/help_book.hpp"
#include "gui_forms/controls/help/help_book_viewer/help_book_viewer.hpp"
#include "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_layer.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/help/help_contents/help_contents.hpp"
#include "gui_forms/help/help_records/help_records.hpp"
#include "gui_forms/help/helpbook_section/helpbook_section.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class RibbonToolTip;
class Window;
struct HelpRequestEvent;
struct RibbonToolTipHelpRequest;

namespace detail {
struct HelpBookWindowState;
class HelpPickLayer;
} // namespace detail

// HelpBookWindow: a HelpBookViewer in a window of its own (help system
// study, section 6), owned by one of the program's windows. The window
// hides when the user closes it and is shown again on the next open, so its
// history, tab and pane width carry over; it is destroyed with its owner or
// by close(). Without application windowing (a headless run) open refuses.
class HelpBookWindow final {
public:
    explicit HelpBookWindow(std::string stable_id);
    ~HelpBookWindow();
    HelpBookWindow(const HelpBookWindow&) = delete;
    HelpBookWindow& operator=(const HelpBookWindow&) = delete;

    // Opens the window, owned by window `owner_id` of the run `owner_handle`
    // belongs to, with `owner`'s resources and key bindings, or shows and
    // raises it when it is open. `title` and `size` apply to a new window.
    // Returns the open's status; on acceptance viewer() is the window's.
    HostServiceStatus open(Window& owner, const ApplicationWindowHandle& owner_handle, std::string_view owner_id,
                           std::string_view title, Size size);
    // Destroys the window (the viewer goes with it).
    void close();
    [[nodiscard]] bool is_open() const noexcept;
    // The window's viewer while the window exists, else null. A new viewer
    // is made for each new window: created() says when.
    [[nodiscard]] std::shared_ptr<HelpBookViewer> viewer() const noexcept;
    [[nodiscard]] Event<HelpBookViewer&>& created() noexcept { return created_; }

private:
    std::string stable_id_;
    std::shared_ptr<detail::HelpBookWindowState> state_;
    Event<HelpBookViewer&> created_;
};

struct HelpBookSystemOptions final {
    // The prefix of every stable ID the system makes (its window, viewer
    // and popups).
    std::string stable_id{"help"};
    GUI_FORMS_LOCALIZABLE(text) std::string window_title{"Help"};
    Size window_size{900.0, 640.0};
    // The home page; empty takes the contents' first page.
    std::string home{};
    HelpBookViewerLabels labels{};
    HelpBookLabels book_labels{};
    GUI_FORMS_LOCALIZABLE(text) std::string more{"More..."};
    std::vector<HelpBookImage> images{};
    HelpBookCompileOptions compile{};
    // The paper popup's width; its height follows the page.
    double popup_width{360.0};
};

// Where help for a control is: the page and the anchor on it.
struct HelpTarget final {
    std::string page{};
    std::string anchor{};
    // Nothing on the way up had help: `page` is the home page.
    bool home{};
};

struct HelpBookSystemSnapshot final {
    std::uint64_t compiles{};
    std::size_t rejections{};
    std::uint64_t requests{};
    std::uint64_t pages_shown{};
    std::uint64_t popups{};
    std::uint64_t help_mode_entries{};
    std::uint64_t external_links{};
    std::size_t attached_windows{};
    bool popup_open{};
    bool help_mode{};
};

// HelpBookSystem: the program's help (help system study, sections 1 to 8).
// It holds the help catalog (the built-in locale file's help fields, a
// translation and the user's overrides), compiles the helpbook section from
// it, the attached windows' structure and the declared HelpTopics, and
// presents it:
//
// - F1 shows the focused control's page, walking up to the window's page
//   and then home when the control has none (resolve);
// - Shift+F1 enters What's This mode: the next click shows the clicked
//   control's help in a paper popup, Escape leaves the mode;
// - a window card (show_card): the control's help line and "More..." to
//   its page;
// - the ribbon's "?" and RibbonToolTip's F1 (on_ribbon_help);
// - HelpProvider's requests, routed by the program (on_help_requested).
//
// Pages show in an embedded viewer when one is set (set_viewer), else in a
// HelpBookWindow owned by the first attached window that has a handle.
// External links are raised to the program (external_link): the system
// never opens a browser, a file or a network resource. Printing is raised
// too (print_requested); help_print.hpp paginates.
//
// One execution context: the UI thread of the attached windows. Attached
// windows must be detached before they are destroyed.
class HelpBookSystem final : public Component {
public:
    explicit HelpBookSystem(HelpBookSystemOptions options = {});
    ~HelpBookSystem() override;

    [[nodiscard]] HelpCatalog& catalog() noexcept { return catalog_; }
    [[nodiscard]] const HelpBookSystemOptions& options() const noexcept { return options_; }
    // Reads a locale file's help fields: into the built-in layer (the
    // embedded built-in locale file) or as the translation. A refused file
    // changes nothing; rejected fields are left out and listed.
    HelpLocaleReadResult load_built_in(std::string_view locale_xml);
    HelpLocaleReadResult load_translation(std::string_view locale_xml);
    // The untranslatable way back: help in the built-in language only.
    void use_built_in_language(bool built_in_only);

    // Compiles the section from the catalog, the attached windows' structure
    // and the declared topics, and shows it in the viewers. The result keeps
    // the rejections; the section holds every record that compiled.
    const HelpBookCompileResult& compile();
    // Uses a section compiled elsewhere (the language-file compiler pass,
    // read_helpbook_section) instead of compiling.
    void set_section(std::shared_ptr<const HelpBookSection> section);
    // The section, compiled first when the catalog changed since.
    [[nodiscard]] std::shared_ptr<const HelpBookSection> section();

    // Attaches a window: F1 and Shift+F1 become help keys in it, and its
    // structure joins the contents. `handle` and `window_id` (the window's
    // ApplicationWindow::stable_id) let the help window be owned by it.
    void attach(Window& window, ApplicationWindowHandle handle = {}, std::string window_id = {});
    void detach(Window& window) noexcept;
    // Shows pages in `viewer` (a viewer in a dock panel, say) instead of the
    // help window; null goes back to the window.
    void set_viewer(std::shared_ptr<HelpBookViewer> viewer);
    [[nodiscard]] std::shared_ptr<HelpBookViewer> active_viewer() const;
    void attach_ribbon_tool_tip(RibbonToolTip& tool_tip);

    [[nodiscard]] HelpTarget resolve(const Control& control);
    bool show(std::string_view stable_id, std::string_view anchor = {});
    bool show_help_for(const Control& control);
    bool show_contents();
    bool show_index(std::string_view keyword);
    bool show_search(std::string_view text);
    // The paper popup beside `anchor`: the page of record `stable_id`.
    bool show_popup(std::string_view stable_id, const Control::Ptr& anchor);
    // The window card beside `anchor`: its help line and "More...", which
    // opens its page. Falls back to the page popup when it has no line.
    bool show_card(const Control::Ptr& anchor);
    void close_popup() noexcept;
    [[nodiscard]] bool popup_open() const noexcept;
    [[nodiscard]] std::shared_ptr<HelpBook> popup_book() const noexcept { return popup_book_; }

    bool enter_help_mode(Window& window);
    void cancel_help_mode() noexcept;
    [[nodiscard]] bool help_mode_active() const noexcept;

    // Program routes: a HelpProvider's request (its target's page unless a
    // handler took it) and the ribbon's "?" or a RibbonToolTip's F1 (the
    // help topic, else the target's page).
    void on_help_requested(HelpRequestEvent& request);
    void on_ribbon_help(const RibbonToolTipHelpRequest& request);

    [[nodiscard]] HelpBookSystemSnapshot snapshot() const noexcept;
    [[nodiscard]] Event<HelpLinkActivation&>& external_link() noexcept { return external_link_; }
    [[nodiscard]] Event<HelpPrintRequest&>& print_requested() noexcept { return print_requested_; }
    [[nodiscard]] Event<const std::vector<std::string>&>& bookmarks_changed() noexcept { return bookmarks_changed_; }
    void set_bookmarks(std::vector<std::string> stable_ids);
    [[nodiscard]] const std::vector<std::string>& bookmarks() const noexcept { return bookmarks_; }

    // The accelerators' targets in an attached window: F1 shows the focused
    // control's help (ending What's This when it is on), Shift+F1 enters
    // What's This. True when the key was used.
    bool on_help_key(Window& window);
    bool on_help_mode_key(Window& window);

private:
    friend class detail::HelpPickLayer;
    struct Attachment final {
        Window* window{};
        ApplicationWindowHandle handle{};
        std::string window_id{};
        AcceleratorToken help_key{};
        AcceleratorToken help_mode_key{};
    };

    void on_pick(Window& window, Point position);
    void wire_viewer(HelpBookViewer& viewer);
    void on_window_viewer_created(HelpBookViewer& viewer);
    [[nodiscard]] std::shared_ptr<HelpBookViewer> present();
    bool open_popup(HelpPageModel page, const Control::Ptr& anchor);
    void on_viewer_external(HelpLinkActivation& activation);
    void on_viewer_popup(HelpLinkActivation& activation);
    void on_viewer_print(HelpPrintRequest& request);
    void on_viewer_bookmarks(const std::vector<std::string>& bookmarks);
    void on_viewer_built_in_language();
    void on_popup_link(HelpLinkActivation& activation);
    void on_popup_close();
    void on_popup_dismissed(OverlayDismissReason reason);
    [[nodiscard]] Attachment* find_attachment(const Window& window) noexcept;

    HelpBookSystemOptions options_;
    HelpCatalog catalog_{};
    HelpBookCompileResult compiled_{};
    std::shared_ptr<const HelpBookSection> section_{};
    std::uint64_t compiled_revision_{};
    bool external_section_{};
    bool compiled_once_{};
    std::vector<Attachment> attachments_{};
    std::vector<std::string> bookmarks_{};
    std::shared_ptr<HelpBookViewer> embedded_viewer_{};
    HelpBookWindow window_;
    std::shared_ptr<AnchoredOverlayLayer> popup_layer_{};
    std::shared_ptr<HelpBook> popup_book_{};
    OverlayToken popup_token_{};
    // A popup or pick layer closed from inside its own event stays alive
    // here until the next open or the system's end.
    std::shared_ptr<AnchoredOverlayLayer> retired_layer_{};
    std::shared_ptr<HelpBook> retired_book_{};
    std::shared_ptr<detail::HelpPickLayer> retired_pick_{};
    std::shared_ptr<detail::HelpPickLayer> pick_layer_{};
    OverlayToken pick_token_{};
    FocusScopeId pick_scope_{};
    Window* pick_window_{};
    HelpBookSystemSnapshot counters_{};

    Event<HelpLinkActivation&> external_link_;
    Event<HelpPrintRequest&> print_requested_;
    Event<const std::vector<std::string>&> bookmarks_changed_;
};

} // namespace gui_forms
