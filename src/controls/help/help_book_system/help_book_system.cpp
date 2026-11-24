#include "gui_forms/controls/help_book_system.hpp"

#include "gui_forms/components/help_provider/help_provider.hpp"
#include "gui_forms/components/ribbon_tool_tip/ribbon_tool_tip.hpp"
#include "gui_forms/controls/ribbon.hpp"
#include "gui_forms/help/help_page/help_page.hpp"
#include "gui_forms/window/window.hpp"

#include <stxlib/optional.hpp>
#include <array>
#include <algorithm>
#include <limits>
#include <utility>

namespace gui_forms {
namespace detail {

// The help window's state, shared with its ready and closed callbacks,
// which hold it weakly: a callback after the HelpBookWindow is gone does
// nothing.
struct HelpBookWindowState final {
    std::shared_ptr<HelpBookViewer> viewer{};
    ApplicationWindowHandle handle{};
    bool open{};
};

// What Shift+F1 lays over the window: an input layer the size of the client
// that takes the next click (the control under it gets its help) or Escape
// (the mode ends). It paints nothing; over it the pointer is the help cursor
// (GAPS H3), an arrow with a question mark where the host has one.
class HelpPickLayer final : public Control {
public:
    HelpPickLayer(StableId stable_id, HelpBookSystem& system, Window& window)
        : Control(std::move(stable_id)), system_(&system), window_(&window) {
        focusable = true;
        cursor = stxlib::Optional<CursorKind>::of(CursorKind::help);
    }

    void on_mouse_down(MouseEventArgs& arguments) override {
        arguments.handled = true;
        if (arguments.button != PointerButton::primary) {
            (*system_).cancel_help_mode();
        } else {
            // The layer covers the client from its origin: its points are
            // the window's.
            (*system_).on_pick(*window_, arguments.window_position);
        }
        Control::on_mouse_down(arguments);
    }

    void on_key_down(KeyEventArgs& arguments) override {
        Control::on_key_down(arguments);
        if (arguments.handled || arguments.physical_key != PhysicalKey::escape) return;
        arguments.handled = true;
        (*system_).cancel_help_mode();
    }

private:
    // The system owns the layer and outlives it (it retires the layer
    // rather than dropping it inside the layer's own events).
    HelpBookSystem* system_;
    Window* window_;
};

} // namespace detail

namespace {

constexpr double popup_gap = 4.0;
constexpr double popup_minimum_height = 96.0;
constexpr double popup_maximum_height = 420.0;
// The popup paper's margins and title band around the print layout's
// content, so the measured page fits without a scroll bar.
constexpr double popup_chrome_height = 72.0;
constexpr double popup_text_inset = 24.0;
constexpr std::size_t maximum_resolve_depth = 256U;

// The help window's ready callback: records the handle.
struct HelpWindowReady final {
    std::weak_ptr<detail::HelpBookWindowState> state;

    void operator()(Window& window, ApplicationWindowHandle handle) const {
        static_cast<void>(window);
        const std::shared_ptr<detail::HelpBookWindowState> held = state.lock();
        if (!held) return;
        (*held).handle = handle;
    }
};

// The help window's closed callback (destroyed, with its owner or by
// close): the viewer goes with it.
struct HelpWindowClosed final {
    std::weak_ptr<detail::HelpBookWindowState> state;

    void operator()() const {
        const std::shared_ptr<detail::HelpBookWindowState> held = state.lock();
        if (!held) return;
        (*held).open = false;
        (*held).handle = ApplicationWindowHandle{};
        (*held).viewer.reset();
    }
};

// F1 and Shift+F1 in an attached window.
struct HelpKeyCallback final {
    HelpBookSystem* system;
    Window* window;
    bool help_mode;

    bool operator()() const;
};

} // namespace

// --- HelpBookWindow -----------------------------------------------------------------------

HelpBookWindow::HelpBookWindow(std::string stable_id)
    : stable_id_(std::move(stable_id)), state_(std::make_shared<detail::HelpBookWindowState>()) {}

HelpBookWindow::~HelpBookWindow() { close(); }

HostServiceStatus HelpBookWindow::open(Window& owner, const ApplicationWindowHandle& owner_handle,
                                       std::string_view owner_id, std::string_view title, Size size) {
    detail::HelpBookWindowState& state = *state_;
    if (state.open && state.handle.active()) {
        const HostServiceStatus shown = state.handle.show();
        if (shown.accepted()) static_cast<void>(state.handle.bring_to_front());
        return shown;
    }
    const ApplicationWindows windows = owner_handle.windows();
    std::shared_ptr<HelpBookViewer> viewer = make_control<HelpBookViewer>(StableId(stable_id_ + ".viewer"));
    ApplicationWindow entry{};
    entry.stable_id = stable_id_;
    entry.owner_id = std::string(owner_id);
    entry.model = std::make_unique<Window>(viewer, size, owner.resources());
    entry.options.title = std::string(title);
    entry.options.initial_size = size;
    entry.options.hide_on_close = true;
    entry.options.ready = HelpWindowReady{state_};
    entry.options.closed = HelpWindowClosed{state_};
    // The viewer is wired (its section, labels and events) before the
    // window exists, so its first frame shows the page.
    state.viewer = viewer;
    created_.emit(*viewer);
    const ApplicationWindowOpenResult opened = windows.open_window(std::move(entry));
    if (!opened.status.accepted()) {
        state.viewer.reset();
        state.open = false;
        return opened.status;
    }
    state.open = true;
    if (opened.window.active()) state.handle = opened.window;
    return opened.status;
}

void HelpBookWindow::close() {
    if (!state_) return;
    detail::HelpBookWindowState& state = *state_;
    if (state.open && state.handle.active()) {
        const ApplicationWindowHandle handle = state.handle;
        static_cast<void>(handle.windows().destroy_window(handle));
    }
    state.open = false;
    state.handle = ApplicationWindowHandle{};
    state.viewer.reset();
}

bool HelpBookWindow::is_open() const noexcept { return (*state_).open && (*state_).handle.active(); }

std::shared_ptr<HelpBookViewer> HelpBookWindow::viewer() const noexcept { return (*state_).viewer; }

// --- HelpBookSystem -----------------------------------------------------------------------

bool HelpKeyCallback::operator()() const {
    return help_mode ? (*system).on_help_mode_key(*window) : (*system).on_help_key(*window);
}

HelpBookSystem::HelpBookSystem(HelpBookSystemOptions options)
    : options_(std::move(options)), locale_(options_.built_in_language), window_(options_.stable_id + ".window") {
    on(window_.created(), *this, &HelpBookSystem::on_window_viewer_created);
}

HelpBookSystem::~HelpBookSystem() {
    cancel_help_mode();
    close_popup();
    attachments_.clear();
    window_.close();
}

void HelpBookSystem::refresh_catalog() { catalog_.adopt_locale(locale_); }

bool HelpBookSystem::refresh_program_values() {
    LocaleLayer program;
    for (const Attachment& attachment : attachments_) append_control_help_fields(*attachment.window, program);
    const bool changed = locale_.set_program_values(std::move(program));
    if (changed) refresh_catalog();
    return changed;
}

LocaleReadResult HelpBookSystem::load_built_in(std::string_view locale_xml) {
    LocaleReadResult result = locale_.load_built_in(locale_xml);
    if (result) refresh_catalog();
    return result;
}

LocaleReadResult HelpBookSystem::load_translation(std::string_view locale_xml) {
    LocaleReadResult result = locale_.load_translation(locale_xml);
    if (result) refresh_catalog();
    return result;
}

LocaleReadResult HelpBookSystem::load_user_overrides(std::string_view locale_xml) {
    LocaleReadResult result = locale_.load_user_overrides(locale_xml);
    if (result) refresh_catalog();
    return result;
}

void HelpBookSystem::clear_user_overrides() {
    locale_.clear_user_overrides();
    refresh_catalog();
}

void HelpBookSystem::use_built_in_language(bool built_in_only) {
    locale_.set_built_in_only(built_in_only);
    refresh_catalog();
    external_section_ = false;
    compile();
}

XmlWriteResult HelpBookSystem::export_template(std::string_view language) {
    static_cast<void>(refresh_program_values());
    LocaleLayer records;
    put_declared_topics(records);
    LocaleLayer authored;
    locale_template_records(locale_, language, authored);
    records.merge_over(authored);
    LocaleFileInfo info{};
    info.language = language.empty() ? locale_.built_in_language() : std::string(language);
    const LocaleFileInfo& translation = locale_.translation_info();
    if (info.language == translation.language) {
        info.name = translation.name;
        info.direction = translation.direction;
    }
    const XmlWriteResult written = write_locale(records, info);
    return written;
}

LocaleResolvedField HelpBookSystem::resolve_field(std::string_view stable_id, LocaleField field) {
    static_cast<void>(refresh_program_values());
    LocaleResolvedField resolved = locale_.resolve(stable_id, field);
    return resolved;
}

const HelpBookCompileResult& HelpBookSystem::compile() {
    static_cast<void>(refresh_program_values());
    std::vector<HelpSkeletonNode> skeleton;
    for (const Attachment& attachment : attachments_) append_help_skeleton(*attachment.window, skeleton);
    std::vector<HelpTopicPlacement> topics;
    collect_declared_topics(topics);
    put_declared_topics(catalog_.built_in());
    compiled_ = compile_helpbook_section(catalog_, skeleton, topics, options_.compile);
    section_ = std::make_shared<const HelpBookSection>(compiled_.section);
    compiled_revision_ = catalog_.revision();
    compiled_once_ = true;
    external_section_ = false;
    ++counters_.compiles;
    counters_.rejections = compiled_.rejections.size();
    if (embedded_viewer_) (*embedded_viewer_).set_section(section_);
    const std::shared_ptr<HelpBookViewer> window_viewer = window_.viewer();
    if (window_viewer) (*window_viewer).set_section(section_);
    return compiled_;
}

void HelpBookSystem::set_section(std::shared_ptr<const HelpBookSection> section) {
    section_ = std::move(section);
    external_section_ = true;
    if (embedded_viewer_) (*embedded_viewer_).set_section(section_);
    const std::shared_ptr<HelpBookViewer> window_viewer = window_.viewer();
    if (window_viewer) (*window_viewer).set_section(section_);
}

std::shared_ptr<const HelpBookSection> HelpBookSystem::section() {
    if (!external_section_) static_cast<void>(refresh_program_values());
    if (!external_section_ && (!compiled_once_ || catalog_.revision() != compiled_revision_)) compile();
    return section_;
}

// --- Windows ------------------------------------------------------------------------------

HelpBookSystem::Attachment* HelpBookSystem::find_attachment(const Window& window) noexcept {
    for (Attachment& attachment : attachments_) {
        if (attachment.window == &window) return &attachment;
    }
    return nullptr;
}

void HelpBookSystem::attach(Window& window, ApplicationWindowHandle handle, std::string window_id) {
    if (find_attachment(window) != nullptr) return;
    Attachment attachment{};
    attachment.window = &window;
    attachment.handle = handle;
    attachment.window_id = std::move(window_id);
    attachment.help_key = window.register_accelerator(*this, KeyGesture{PhysicalKey::f1, Modifier::none},
                                                      HelpKeyCallback{this, &window, false});
    attachment.help_mode_key = window.register_accelerator(*this, KeyGesture{PhysicalKey::f1, Modifier::shift},
                                                           HelpKeyCallback{this, &window, true});
    attachments_.push_back(std::move(attachment));
    // The window's structure joins the contents on the next use.
    compiled_once_ = false;
}

void HelpBookSystem::detach(Window& window) noexcept {
    const PickShown* const pick = pick_presentation_.shown_state();
    if (pick != nullptr && (*pick).window == &window) cancel_help_mode();
    const PopupShown* const popup = popup_presentation_.open_state();
    const Control::Ptr anchor = popup != nullptr && (*popup).layer ? (*(*popup).layer).anchor() : Control::Ptr{};
    if (anchor && (*anchor).attached_window() == &window) close_popup();
    for (std::size_t index = 0U; index < attachments_.size(); ++index) {
        if (attachments_[index].window != &window) continue;
        attachments_.erase(attachments_.begin() + static_cast<std::ptrdiff_t>(index));
        compiled_once_ = false;
        return;
    }
}

void HelpBookSystem::set_viewer(std::shared_ptr<HelpBookViewer> viewer) {
    if (viewer == embedded_viewer_) return;
    embedded_viewer_ = std::move(viewer);
    if (embedded_viewer_) wire_viewer(*embedded_viewer_);
}

std::shared_ptr<HelpBookViewer> HelpBookSystem::active_viewer() const {
    if (embedded_viewer_) return embedded_viewer_;
    return window_.viewer();
}

void HelpBookSystem::attach_ribbon_tool_tip(RibbonToolTip& tool_tip) {
    on(tool_tip.help_requested(), *this, &HelpBookSystem::on_ribbon_help);
}

void HelpBookSystem::attach_ribbon(Ribbon& ribbon) {
    on(ribbon.help_button_clicked(), *this, &HelpBookSystem::on_ribbon_button_help);
}

void HelpBookSystem::wire_viewer(HelpBookViewer& viewer) {
    on(viewer.external_link(), *this, &HelpBookSystem::on_viewer_external);
    on(viewer.popup_requested(), *this, &HelpBookSystem::on_viewer_popup);
    on(viewer.print_requested(), *this, &HelpBookSystem::on_viewer_print);
    on(viewer.bookmarks_changed(), *this, &HelpBookSystem::on_viewer_bookmarks);
    on(viewer.built_in_language_requested(), *this, &HelpBookSystem::on_viewer_built_in_language);
    viewer.set_labels(options_.labels);
    viewer.set_images(options_.images);
    viewer.set_home(options_.home);
    viewer.set_bookmarks(bookmarks_);
    viewer.set_section(section());
}

void HelpBookSystem::on_window_viewer_created(HelpBookViewer& viewer) { wire_viewer(viewer); }

std::shared_ptr<HelpBookViewer> HelpBookSystem::present() {
    if (embedded_viewer_) return embedded_viewer_;
    for (const Attachment& attachment : attachments_) {
        if (!attachment.handle.active()) continue;
        const HostServiceStatus status = window_.open(*attachment.window, attachment.handle, attachment.window_id,
                                                      options_.window_title, options_.window_size);
        if (!status.accepted()) return {};
        return window_.viewer();
    }
    return window_.viewer();
}

// --- Finding and showing help ---------------------------------------------------------------

HelpTarget HelpBookSystem::resolve(const Control& control) {
    HelpTarget target{};
    const std::shared_ptr<const HelpBookSection> held = section();
    if (!held) return target;
    const HelpBookSection& book = *held;
    // Up from the control: the first record with a body is the page; a
    // record with only a help line on the way is shown as a line on that
    // page, so it becomes the anchor.
    // `held_parent` keeps each ancestor alive while it is read.
    const Control* current = &control;
    Control::Ptr held_parent{};
    std::size_t steps = 0U;
    while (current != nullptr && steps < maximum_resolve_depth) {
        const std::string id((*current).stable_id().value());
        const stxlib::Optional<std::size_t> record = book.find(id);
        if (record.present()) {
            const HelpBookRecord& found = book.records[(*record.value())];
            if (!found.help.empty()) {
                target.page = id;
                return target;
            }
            if (target.anchor.empty() && !found.help_line.empty()) target.anchor = id;
        }
        held_parent = (*current).parent();
        current = held_parent.get();
        ++steps;
    }
    // Nothing had a page: home.
    target.home = true;
    target.anchor.clear();
    if (!options_.home.empty() && book.find(options_.home).present()) {
        target.page = options_.home;
        return target;
    }
    for (const HelpContentsNode& node : book.contents.nodes) {
        if (book.find(node.stable_id).present()) {
            target.page = node.stable_id;
            return target;
        }
    }
    if (!book.records.empty()) target.page = book.records.front().stable_id;
    return target;
}

bool HelpBookSystem::show(std::string_view stable_id, std::string_view anchor) {
    const std::shared_ptr<HelpBookViewer> viewer = present();
    if (!viewer) return false;
    const bool shown = (*viewer).show(stable_id, anchor);
    if (shown) ++counters_.pages_shown;
    return shown;
}

bool HelpBookSystem::show_help_for(const Control& control) {
    const HelpTarget target = resolve(control);
    if (target.page.empty()) return false;
    return show(target.page, target.anchor);
}

bool HelpBookSystem::show_contents() {
    const std::shared_ptr<HelpBookViewer> viewer = present();
    if (!viewer) return false;
    (*viewer).show_contents();
    return true;
}

bool HelpBookSystem::show_index(std::string_view keyword) {
    const std::shared_ptr<HelpBookViewer> viewer = present();
    if (!viewer) return false;
    (*viewer).show_index(keyword);
    return true;
}

bool HelpBookSystem::show_search(std::string_view text) {
    const std::shared_ptr<HelpBookViewer> viewer = present();
    if (!viewer) return false;
    (*viewer).show_search(text);
    return true;
}

// --- The paper popup ---------------------------------------------------------------------------

bool HelpBookSystem::show_popup(std::string_view stable_id, const Control::Ptr& anchor) {
    const std::shared_ptr<const HelpBookSection> held = section();
    if (!held || !anchor) return false;
    const stxlib::Optional<std::size_t> record = (*held).find(stable_id);
    if (record.absent()) return false;
    HelpPageModel page{};
    static_cast<void>(build_help_page(*held, (*record.value()), page));
    return open_popup(std::move(page), anchor);
}

bool HelpBookSystem::show_card(const Control::Ptr& anchor) { return show_card_for(anchor, anchor); }

bool HelpBookSystem::show_card_for(const Control::Ptr& subject, const Control::Ptr& anchor) {
    const std::shared_ptr<const HelpBookSection> held = section();
    if (!held || !subject || !anchor) return false;
    const HelpTarget target = resolve(*subject);
    // The help line: the subject's own, else the nearest one above it.
    const HelpBookRecord* line_record = nullptr;
    Control::Ptr current = subject;
    std::size_t steps = 0U;
    while (current && steps < maximum_resolve_depth && line_record == nullptr) {
        const stxlib::Optional<std::size_t> record = (*held).find((*current).stable_id().value());
        if (record.present() && !(*held).records[(*record.value())].help_line.empty()) line_record = &(*held).records[(*record.value())];
        current = (*current).parent();
        ++steps;
    }
    if (line_record == nullptr) {
        if (target.page.empty()) return false;
        return show_popup(target.page, anchor);
    }
    HelpPageModel page{};
    page.stable_id = (*line_record).stable_id;
    page.title = (*line_record).title;
    page.fallback = (*line_record).fallback;
    HelpPageBlock block{};
    block.kind = HelpPageBlockKind::paragraph;
    block.first_run = 0U;
    HelpPageRun line{};
    line.text = (*line_record).help_line;
    page.runs.push_back(std::move(line));
    if (!target.page.empty()) {
        HelpPageLink more{};
        more.kind = HelpLinkKind::topic;
        more.target = target.page;
        more.fragment = target.anchor;
        page.links.push_back(std::move(more));
        HelpPageRun space{};
        space.text = " ";
        page.runs.push_back(std::move(space));
        HelpPageRun link{};
        link.text = options_.more;
        link.link.set(0U);
        page.runs.push_back(std::move(link));
    }
    block.run_count = page.runs.size();
    page.blocks.push_back(std::move(block));
    return open_popup(std::move(page), anchor);
}

bool HelpBookSystem::open_popup(HelpPageModel page, const Control::Ptr& anchor) {
    close_popup();
    // A popup still closing is not replaced until it has closed.
    if (!popup_presentation_.is_closed()) return false;
    retired_layer_.reset();
    retired_book_.reset();
    Window* const window = (*anchor).attached_window();
    if (window == nullptr) return false;
    const std::string prefix = options_.stable_id;
    std::shared_ptr<HelpBook> book = make_control<HelpBook>(StableId(prefix + ".popup"));
    (*book).set_close_visible(true);
    (*book).set_labels(options_.book_labels);
    (*book).set_images(options_.images);
    (*book).show_page(std::move(page));
    AnchoredOverlayPlacement placement{};
    placement.preferred_size = Size{options_.popup_width, popup_minimum_height};
    placement.gap = popup_gap;
    std::shared_ptr<AnchoredOverlayLayer> layer =
        make_control<AnchoredOverlayLayer>(StableId(prefix + ".popup-layer"), anchor, placement);
    const Size client = (*window).client_size();
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    (*layer).set_content(book);
    (*layer).dismiss_on_click_away = true;
    (*layer).dismiss_on_escape = true;
    on((*layer).dismiss_requested(), *this, &HelpBookSystem::on_popup_dismissed);
    on((*book).close_requested(), *this, &HelpBookSystem::on_popup_close);
    on((*book).link_activated(), *this, &HelpBookSystem::on_popup_link);
    PopupShown shown{};
    shown.overlay = (*window).open_overlay(anchor, layer);
    if (!shown.overlay.connected()) {
        (*layer).clear_content();
        return false;
    }
    shown.layer = layer;
    shown.book = book;
    // No focus scope: the popup is open as soon as it is shown.
    popup_presentation_ = PresentationState<PopupShown>::open(std::move(shown));
    // Now the book can lay text out: size the paper to the page.
    const double content = (*book).layout_for_print(std::max(0.0, options_.popup_width - popup_text_inset));
    placement.preferred_size.height =
        std::clamp(content + popup_chrome_height, popup_minimum_height, popup_maximum_height);
    (*layer).placement = placement;
    static_cast<void>((*window).request_focus(book));
    ++counters_.popups;
    return true;
}

// The layer and book stay alive (retired) past a close from inside their
// own event. A close or open asked meanwhile is refused.
void HelpBookSystem::close_popup() noexcept {
    if (!popup_presentation_.begin_closing()) return;
    PopupShown& closing = *popup_presentation_.closing_state();
    closing.overlay.disconnect();
    if (closing.layer) {
        retired_layer_ = std::move(closing.layer);
        retired_book_ = std::move(closing.book);
    }
    popup_presentation_.finish_closing();
}

bool HelpBookSystem::popup_open() const noexcept {
    const PopupShown* const shown = popup_presentation_.open_state();
    return shown != nullptr && (*shown).overlay.connected();
}

std::shared_ptr<HelpBook> HelpBookSystem::popup_book() const noexcept {
    const PopupShown* const shown = popup_presentation_.open_state();
    if (shown == nullptr) return {};
    return (*shown).book;
}

void HelpBookSystem::on_popup_link(HelpLinkActivation& activation) {
    if (activation.kind == HelpLinkKind::external) {
        ++counters_.external_links;
        external_link_.emit(activation);
        return;
    }
    if (activation.kind != HelpLinkKind::topic && activation.kind != HelpLinkKind::popup) return;
    const std::string target = activation.target;
    const std::string fragment = activation.fragment;
    close_popup();
    activation.handled = show(target, fragment);
}

void HelpBookSystem::on_popup_close() { close_popup(); }

void HelpBookSystem::on_popup_dismissed(OverlayDismissReason reason) {
    static_cast<void>(reason);
    close_popup();
}

// --- What's This -------------------------------------------------------------------------------------

bool HelpBookSystem::enter_help_mode(Window& window) {
    cancel_help_mode();
    close_popup();
    const Control::Ptr root = window.root();
    if (!root) return false;
    if (!pick_presentation_.is_closed()) return false;
    retired_pick_.reset();
    std::shared_ptr<detail::HelpPickLayer> layer =
        make_control<detail::HelpPickLayer>(StableId(options_.stable_id + ".pick"), *this, window);
    const Size client = window.client_size();
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    PickShown opening{};
    opening.lease.overlay = window.open_overlay(root, layer);
    if (!opening.lease.overlay.connected()) return false;
    opening.layer = layer;
    opening.window = &window;
    pick_presentation_ = PresentationState<PickShown>::opening(std::move(opening));
    // Beginning the scope runs focus handlers; one may cancel help mode,
    // and then the scope begun here is ended here.
    const FocusScopeId scope = window.begin_focus_scope(layer, layer);
    if (!pick_presentation_.settle_open()) {
        if (scope) static_cast<void>(window.end_focus_scope(scope));
        return false;
    }
    (*pick_presentation_.open_state()).lease.scope = scope;
    ++counters_.help_mode_entries;
    return true;
}

// The pick layer stays alive (retired) past a cancel from inside its own
// event. A cancel or entry asked meanwhile is refused.
void HelpBookSystem::cancel_help_mode() noexcept {
    if (!pick_presentation_.begin_closing()) return;
    PickShown& closing = *pick_presentation_.closing_state();
    if (closing.window != nullptr && closing.lease.scope) {
        static_cast<void>((*closing.window).end_focus_scope(closing.lease.scope));
    }
    closing.lease.scope = FocusScopeId{};
    closing.lease.overlay.disconnect();
    if (closing.layer) retired_pick_ = std::move(closing.layer);
    pick_presentation_.finish_closing();
}

bool HelpBookSystem::help_mode_active() const noexcept {
    const PickShown* const shown = pick_presentation_.shown_state();
    return shown != nullptr && (*shown).lease.overlay.connected();
}

void HelpBookSystem::on_pick(Window& window, Point position) {
    cancel_help_mode();
    const Control::Ptr picked = window.hit_test(position);
    if (!picked) return;
    show_card(picked);
}

// --- Keys and program routes ---------------------------------------------------------------------------

bool HelpBookSystem::on_help_key(Window& window) {
    ++counters_.requests;
    if (help_mode_active()) {
        cancel_help_mode();
        return true;
    }
    Control::Ptr focused = window.focused_control();
    if (!focused) focused = window.root();
    if (!focused) return false;
    return show_help_for(*focused);
}

bool HelpBookSystem::on_help_mode_key(Window& window) {
    ++counters_.requests;
    return enter_help_mode(window);
}

void HelpBookSystem::on_help_requested(HelpRequestEvent& request) {
    if (request.handled) return;
    ++counters_.requests;
    for (const Attachment& attachment : attachments_) {
        const Control::Ptr target = (*attachment.window).find(request.target_stable_id);
        if (!target) continue;
        request.handled = show_help_for(*target);
        return;
    }
    request.handled = show(request.target_stable_id);
}

void HelpBookSystem::on_ribbon_help(const RibbonToolTipHelpRequest& request) {
    ++counters_.requests;
    // The tooltip's topic; else the item's record, then the target's.
    if (!request.help_topic.empty() && show(request.help_topic)) return;
    if (!request.item_id.empty() && show(request.item_id)) return;
    for (const Attachment& attachment : attachments_) {
        const Control::Ptr target = (*attachment.window).find(request.target_stable_id);
        if (!target) continue;
        show_help_for(*target);
        return;
    }
    show(request.target_stable_id);
}

void HelpBookSystem::on_ribbon_button_help(const RibbonHelpRequest& request) {
    ++counters_.requests;
    for (const Attachment& attachment : attachments_) {
        const Control::Ptr ribbon = (*attachment.window).find(request.ribbon_id);
        if (!ribbon) continue;
        static_cast<void>(show_card_for((*attachment.window).root(), ribbon));
        return;
    }
}

// --- Viewer events ---------------------------------------------------------------------------------------

void HelpBookSystem::on_viewer_external(HelpLinkActivation& activation) {
    ++counters_.external_links;
    external_link_.emit(activation);
}

void HelpBookSystem::on_viewer_popup(HelpLinkActivation& activation) {
    const std::shared_ptr<HelpBookViewer> viewer = active_viewer();
    if (!viewer) return;
    activation.handled = show_popup(activation.target, (*viewer).book());
}

void HelpBookSystem::on_viewer_print(HelpPrintRequest& request) { print_requested_.emit(request); }

void HelpBookSystem::on_viewer_bookmarks(const std::vector<std::string>& bookmarks) {
    bookmarks_ = bookmarks;
    // A copy (A-gf-abi-02-F7): the C ABI's collection field borrows the
    // argument for the whole callback, and a handler may change bookmarks_.
    const std::vector<std::string> published = bookmarks_;
    bookmarks_changed_.emit(published);
}

void HelpBookSystem::on_viewer_built_in_language() { use_built_in_language(true); }

void HelpBookSystem::set_bookmarks(std::vector<std::string> stable_ids) {
    bookmarks_ = std::move(stable_ids);
    if (embedded_viewer_) (*embedded_viewer_).set_bookmarks(bookmarks_);
    const std::shared_ptr<HelpBookViewer> window_viewer = window_.viewer();
    if (window_viewer) (*window_viewer).set_bookmarks(bookmarks_);
}

HelpBookSystemSnapshot HelpBookSystem::snapshot() const noexcept {
    HelpBookSystemSnapshot snapshot = counters_;
    snapshot.attached_windows = attachments_.size();
    snapshot.popup_open = popup_open();
    snapshot.help_mode = help_mode_active();
    return snapshot;
}

namespace {

// The events HelpBookSystem adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 3> help_book_system_interactions{{
    {.name = "external_link", .payload = "HelpLinkActivation", .mutable_payload = true},
    {.name = "print_requested", .payload = "HelpPrintRequest", .mutable_payload = true},
    {.name = "bookmarks_changed", .payload = "std::vector<std::string>", .mutable_payload = false},
}};

} // namespace

void HelpBookSystem::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), help_book_system_interactions.begin(), help_book_system_interactions.end());
}

} // namespace gui_forms
