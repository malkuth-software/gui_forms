#include "gui_forms/controls/dock_workspace.hpp"

#include "gui_forms/application/workspace_transaction/workspace_transaction.hpp"
#include "gui_forms/commands/command_route/command_route.hpp"
#include "gui_forms/controls/dock_site.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// Moves `panel` to the front of a most-recently-active history.
void bring_to_front(std::vector<DockPanelId>& history, const DockPanelId& panel) {
    const std::vector<DockPanelId>::iterator found = std::find(history.begin(), history.end(), panel);
    if (found != history.end()) history.erase(found);
    history.insert(history.begin(), panel);
}

// The events DockWorkspace adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 3> workspace_interactions{{
    {.name = "document_closing", .payload = "DockDocumentClosing", .mutable_payload = true},
    {.name = "activation_changed", .payload = "DockActivationChange", .mutable_payload = false},
    {.name = "layout_changed", .payload = "DockLayoutChange", .mutable_payload = false},
}};

// The smallest client area a floating window is opened with.
constexpr Size smallest_floating_window{64.0, 48.0};

// The stacks under a node in tree order (a split's first child first).
// Recurses once per dock node level: a committed tree is at most
// dock_maximum_depth (32) deep, refused beyond by the invariant check;
// a few hundred bytes of stack per level.
void collect_stacks(const DockLayout& layout, DockNodeId node_id, std::vector<DockNodeId>& out) {
    const DockNode* const node = layout.find_node(node_id);
    if (node == nullptr) return;
    if ((*node).kind == DockNodeKind::split || (*node).kind == DockNodeKind::document_area) {
        if (!((*node).first == DockNodeId{})) collect_stacks(layout, (*node).first, out);
        if (!((*node).second == DockNodeId{})) collect_stacks(layout, (*node).second, out);
        return;
    }
    out.push_back(node_id);
}

// Whether a node stands in the main site's tree.
bool in_main_site(const DockLayout& layout, DockNodeId node_id) {
    const DockNode* node = layout.find_node(node_id);
    DockNodeId top = node_id;
    while (node != nullptr && !((*node).parent == DockNodeId{})) {
        top = (*node).parent;
        node = layout.find_node(top);
    }
    const bool inside = node != nullptr && top == layout.state().main_root;
    return inside;
}

const DockPanelRecord* find_record(const DockLayout& layout, const DockPanelId& panel) {
    for (const DockPanelRecord& record : layout.state().panels) {
        if (record.id == panel) return &record;
    }
    return nullptr;
}
// The result points into `layout`: a temporary layout may not be passed.
const DockPanelRecord* find_record(DockLayout&& layout, const DockPanelId& panel) = delete;

double overlap_area(Rect left, Rect right) noexcept {
    const double width = std::min(left.right(), right.right()) - std::max(left.x, right.x);
    const double height = std::min(left.bottom(), right.bottom()) - std::max(left.y, right.y);
    if (width <= 0.0 || height <= 0.0) return 0.0;
    const double area = width * height;
    return area;
}

} // namespace

// -- the floating windows' callbacks -------------------------------------------

struct DockWorkspace::FloatingReady final {
    std::shared_ptr<WindowLiaison> liaison{};
    DockSiteId site{};
    void operator()(Window& window, ApplicationWindowHandle handle) const {
        if (!liaison || (*liaison).workspace == nullptr) return;
        (*(*liaison).workspace).on_window_ready(site, window, handle);
    }
};

struct DockWorkspace::FloatingClosing final {
    std::shared_ptr<WindowLiaison> liaison{};
    DockSiteId site{};
    void operator()(HostCloseRequest& request) const {
        if (!liaison || (*liaison).workspace == nullptr) return;
        (*(*liaison).workspace).on_window_closing(site, request);
    }
};

struct DockWorkspace::FloatingClosed final {
    std::shared_ptr<WindowLiaison> liaison{};
    DockSiteId site{};
    void operator()() const {
        if (!liaison || (*liaison).workspace == nullptr) return;
        (*(*liaison).workspace).on_window_closed(site);
    }
};

struct DockWorkspace::FloatingFrame final {
    std::shared_ptr<WindowLiaison> liaison{};
    DockSiteId site{};
    void operator()(const HostWindowFrameEvent& event) const {
        if (!liaison || (*liaison).workspace == nullptr) return;
        (*(*liaison).workspace).on_window_frame(site, event);
    }
};

// Posted from a floating window's close request, so the transaction that
// closes the window runs after the request returns.
struct DockWorkspace::CloseFloatingSite final {
    std::shared_ptr<WindowLiaison> liaison{};
    DockSiteId site{};
    void operator()() const {
        if (!liaison || (*liaison).workspace == nullptr) return;
        static_cast<void>((*(*liaison).workspace).close_floating_site(site));
    }
};

// -- construction --------------------------------------------------------------

DockWorkspace::DockWorkspace(std::string stable_id) : stable_id_(std::move(stable_id)) {
    // The workspace's only subscription to its layout: it prunes the tracks
    // and then has the sites realize the change, in a fixed order.
    layout_token_ = layout_.changed().subscribe(
        *this, Delegate<const DockLayoutChange&>::bind<DockWorkspace, &DockWorkspace::on_layout_changed>(*this));
}

DockWorkspace::~DockWorkspace() {
    release_sites();
}

void DockWorkspace::on_dispose() noexcept {
    close_request_.reset();
    release_sites();
    Component::on_dispose();
}

// The workspace goes: it stops hearing its layout, destroys its floating
// windows, and unbinds every site, which then shows nothing. A site whose
// list cannot be copied (no memory) is unbound without being told.
void DockWorkspace::release_sites() noexcept {
    layout_token_.disconnect();
    detach_windows();
    std::vector<DockSite*> sites{};
    try {
        sites = bound_sites();
    } catch (...) {
    }
    site_ = nullptr;
    floating_sites_.clear();
    for (DockSite* const site : sites) (*site).release_workspace();
}

// -- registration --------------------------------------------------------------

DockWorkspace::PanelEntry* DockWorkspace::find_entry(const DockPanelId& panel) noexcept {
    for (PanelEntry& entry : panels_) {
        if (entry.registration.descriptor.id == panel) return &entry;
    }
    return nullptr;
}

const DockWorkspace::PanelEntry* DockWorkspace::find_entry(const DockPanelId& panel) const noexcept {
    for (const PanelEntry& entry : panels_) {
        if (entry.registration.descriptor.id == panel) return &entry;
    }
    return nullptr;
}

bool DockWorkspace::register_panel(DockPanelRegistration registration) {
    if (!layout_.register_panel(registration.descriptor)) return false;
    PanelEntry entry{};
    entry.registration = std::move(registration);
    panels_.push_back(std::move(entry));
    return true;
}

const DockPanelRegistration* DockWorkspace::find_registration(const DockPanelId& panel) const noexcept {
    const PanelEntry* const entry = find_entry(panel);
    if (entry == nullptr) return nullptr;
    return &(*entry).registration;
}

bool DockWorkspace::set_panel_title(const DockPanelId& panel, std::string title) {
    PanelEntry* const entry = find_entry(panel);
    if (entry == nullptr) return false;
    if ((*entry).registration.title == title) return true;
    (*entry).registration.title = std::move(title);
    const std::vector<DockSite*> sites = bound_sites();
    for (DockSite* const site : sites) {
        if (bound(site)) (*site).realize();
    }
    for (FloatingWindow& window : windows_) {
        const DockFloatingSite* const floating = layout_.find_floating_site(window.site);
        if (floating == nullptr || window.destroying) continue;
        std::string retitled = window_title(*floating);
        if (retitled == window.title) continue;
        window.title = std::move(retitled);
        static_cast<void>(window.handle.set_title(window.title));
    }
    return true;
}

bool DockWorkspace::update_panel(const DockPanelDescriptor& descriptor) {
    PanelEntry* const entry = find_entry(descriptor.id);
    if (entry == nullptr || !layout_.update_panel(descriptor)) return false;
    (*entry).registration.descriptor = descriptor;
    realize_sites();
    return true;
}

void DockWorkspace::realize_sites() {
    const std::vector<DockSite*> sites = bound_sites();
    for (DockSite* const site : sites) {
        if (bound(site)) (*site).realize();
    }
}

void DockWorkspace::set_floats_shown_while_inactive(bool shown) {
    if (floats_shown_while_inactive_ == shown) return;
    floats_shown_while_inactive_ = shown;
    for (const FloatingWindow& window : windows_) {
        if (!window.destroying) apply_window_visibility(window);
    }
}

void DockWorkspace::set_floating_window_shown_while_inactive(DockSiteId site, std::optional<bool> shown) {
    std::size_t index = 0U;
    while (index < floating_visibility_.size() && !(floating_visibility_[index].site == site)) ++index;
    if (shown) {
        if (index == floating_visibility_.size()) {
            FloatingVisibility entry{};
            entry.site = site;
            floating_visibility_.push_back(entry);
        }
        floating_visibility_[index].shown = *shown;
    } else if (index < floating_visibility_.size()) {
        floating_visibility_.erase(floating_visibility_.begin() + static_cast<std::ptrdiff_t>(index));
    }
    const FloatingWindow* const window = find_window(site);
    if (window != nullptr && !(*window).destroying) apply_window_visibility(*window);
}

bool DockWorkspace::window_shown_while_inactive(DockSiteId site) const noexcept {
    for (const FloatingVisibility& entry : floating_visibility_) {
        if (entry.site == site) return entry.shown;
    }
    return floats_shown_while_inactive_;
}

void DockWorkspace::apply_window_visibility(const FloatingWindow& window) const {
    if (!window.handle.active()) return;
    // Refused before the window is ready; on_window_ready applies it again.
    static_cast<void>(window.handle.set_shown_while_inactive(window_shown_while_inactive(window.site)));
}

void DockWorkspace::set_document_tabs_shown(bool shown) {
    if (document_tabs_shown_ == shown) return;
    document_tabs_shown_ = shown;
    realize_sites();
}

void DockWorkspace::set_auto_hide_opens_on_hover(bool opens) { auto_hide_opens_on_hover_ = opens; }

void DockWorkspace::set_area_background(std::optional<Color> colour) {
    if (area_background_ == colour) return;
    area_background_ = colour;
    const std::vector<DockSite*> sites = bound_sites();
    for (DockSite* const site : sites) {
        if (bound(site)) (*site).invalidate(Dirty::paint);
    }
}

std::string DockWorkspace::panel_title(const DockPanelId& panel) const {
    const PanelEntry* const entry = find_entry(panel);
    if (entry == nullptr || (*entry).registration.title.empty()) return panel.value;
    return (*entry).registration.title;
}

// -- placement -----------------------------------------------------------------

bool DockWorkspace::placed(const DockPanelId& panel) const {
    const DockPlacementKind placement = layout_.locate(panel).placement;
    const bool result = placement == DockPlacementKind::docked || placement == DockPlacementKind::floating ||
                        placement == DockPlacementKind::auto_hidden;
    return result;
}

bool DockWorkspace::docked_in_main_site(const DockPanelId& panel) const {
    const DockPanelLocation location = layout_.locate(panel);
    const bool docked = location.placement == DockPlacementKind::docked;
    return docked;
}

DockPanelKind DockWorkspace::panel_kind(const DockPanelId& panel) const {
    const DockPanelDescriptor* const descriptor = layout_.find_descriptor(panel);
    if (descriptor == nullptr) return DockPanelKind::tool;
    return (*descriptor).kind;
}

DockTransactionResult DockWorkspace::apply(const DockTransaction& transaction) {
    const DockTransactionResult result = layout_.apply(transaction);
    return result;
}

DockTransactionResult DockWorkspace::show_panel(const DockPanelId& panel, DockEditOrigin origin) {
    DockTransactionResult result{};
    if (!placed(panel)) {
        DockTransaction transaction{};
        transaction.origin = origin;
        transaction.edits.push_back(DockEdit::show(panel));
        result = layout_.apply(transaction);
        if (!result) return result;
    } else {
        result.applied = true;
        result.generation = layout_.generation();
    }
    const DockActivationCause cause =
        origin == DockEditOrigin::user ? DockActivationCause::keyboard : DockActivationCause::programmatic;
    static_cast<void>(activate_panel(panel, cause));
    return result;
}

DockTransactionResult DockWorkspace::hide_panel(const DockPanelId& panel, DockEditOrigin origin) {
    DockTransaction transaction{};
    transaction.origin = origin;
    transaction.edits.push_back(DockEdit::hide(panel));
    const DockTransactionResult result = layout_.apply(transaction);
    return result;
}

DockTransactionResult DockWorkspace::close_panel(const DockPanelId& panel, DockEditOrigin origin) {
    DockTransaction transaction{};
    transaction.origin = origin;
    transaction.edits.push_back(DockEdit::close(panel));
    const DockTransactionResult result = layout_.apply(transaction);
    if (!result) return result;
    // The layout change already released a closed document's content; a
    // closed tool panel's goes here (its placeholder stays).
    PanelEntry* const entry = find_entry(panel);
    if (entry != nullptr) (*entry).content.reset();
    return result;
}

// -- closing documents at the user's request (phase D6) ------------------------

DockCloseOutcome DockWorkspace::request_close(const DockPanelId& panel) {
    if (close_request_) return DockCloseOutcome::busy;
    const DockPanelDescriptor* const descriptor = layout_.find_descriptor(panel);
    if (descriptor == nullptr || !placed(panel) || !(*descriptor).allowed.closed) return DockCloseOutcome::refused;
    if ((*descriptor).kind != DockPanelKind::document) {
        const DockTransactionResult result = close_panel(panel, DockEditOrigin::user);
        return result ? DockCloseOutcome::closed : DockCloseOutcome::refused;
    }
    CloseRequest request{};
    request.cause = DockCloseCause::document;
    request.documents.push_back(panel);
    return begin_close(std::move(request));
}

DockCloseOutcome DockWorkspace::close_all_documents() {
    if (close_request_) return DockCloseOutcome::busy;
    CloseRequest request{};
    request.cause = DockCloseCause::all_documents;
    request.documents = closable_documents();
    return begin_close(std::move(request));
}

DockCloseOutcome DockWorkspace::close_other_documents(const DockPanelId& kept) {
    if (close_request_) return DockCloseOutcome::busy;
    CloseRequest request{};
    request.cause = DockCloseCause::other_documents;
    for (const DockPanelId& document : closable_documents()) {
        if (!(document == kept)) request.documents.push_back(document);
    }
    return begin_close(std::move(request));
}

DockCloseOutcome DockWorkspace::answer_close(bool close) {
    if (!close_request_ || (*close_request_).asked == 0U) return DockCloseOutcome::refused;
    if (!close) {
        close_request_.reset();
        return DockCloseOutcome::cancelled;
    }
    CloseRequest& request = *close_request_;
    request.next = request.next + request.asked;
    request.asked = 0U;
    return run_close();
}

bool DockWorkspace::close_pending() const noexcept {
    const bool pending = close_request_ && (*close_request_).asked != 0U;
    return pending;
}

std::span<const DockPanelId> DockWorkspace::pending_close() const noexcept {
    if (!close_pending()) return {};
    const CloseRequest& request = *close_request_;
    const std::span<const DockPanelId> asked(request.documents.data() + request.next, request.asked);
    return asked;
}

std::vector<DockPanelId> DockWorkspace::closable_documents() const {
    std::vector<DockNodeId> stacks{};
    const DockLayoutState& state = layout_.state();
    collect_stacks(layout_, state.main_root, stacks);
    for (const DockFloatingSite& site : state.floating_sites) collect_stacks(layout_, site.root, stacks);
    std::vector<DockPanelId> documents{};
    for (const DockNodeId stack : stacks) {
        const DockNode* const node = layout_.find_node(stack);
        if (node == nullptr) continue;
        for (const DockPanelId& panel : (*node).panels) {
            const DockPanelDescriptor* const descriptor = layout_.find_descriptor(panel);
            const bool closable = descriptor != nullptr && (*descriptor).kind == DockPanelKind::document &&
                                  (*descriptor).allowed.closed;
            if (closable) documents.push_back(panel);
        }
    }
    return documents;
}

DockCloseOutcome DockWorkspace::begin_close(CloseRequest request) {
    if (close_request_) return DockCloseOutcome::busy;
    if (request.documents.empty() && request.companions.empty()) return DockCloseOutcome::refused;
    close_request_ = std::move(request);
    return run_close();
}

// Drops the request's documents not yet asked about that are no longer
// placed (a handler, or the program, closed them meanwhile).
void DockWorkspace::drop_unplaced_close_documents() {
    CloseRequest& request = *close_request_;
    std::size_t index = request.next;
    while (index < request.documents.size()) {
        if (placed(request.documents[index])) {
            ++index;
            continue;
        }
        request.documents.erase(request.documents.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

// The prompts run in the request's order. A handler's own changes are
// allowed: the request holds its documents by identity; one that left the
// layout before it is asked about is not asked about, and those that left
// when the request finishes are skipped.
DockCloseOutcome DockWorkspace::run_close() {
    while (close_request_) {
        drop_unplaced_close_documents();
        CloseRequest& request = *close_request_;
        if (request.next >= request.documents.size()) break;
        const std::size_t remaining = request.documents.size() - request.next;
        const std::size_t count = close_prompt.read() == DockClosePrompt::combined ? remaining : 1U;
        // The handlers see the documents through a copy, which stays valid
        // whatever they do to the request.
        const std::vector<DockPanelId> asked(request.documents.begin() + static_cast<std::ptrdiff_t>(request.next),
                                             request.documents.begin() +
                                                 static_cast<std::ptrdiff_t>(request.next + count));
        // The raise owns the payload's view; declared after `asked`, so the
        // view ends before the copy does.
        const detail::BorrowScope raise;
        DockDocumentClosing closing{};
        closing.documents = raise.lend_view(std::span<const DockPanelId>(asked.data(), asked.size()));
        closing.cause = request.cause;
        document_closing_.emit(closing);
        // A handler may have disposed the workspace, which drops the
        // request.
        if (!is_alive() || !close_request_) return DockCloseOutcome::cancelled;
        if (closing.cancel) {
            close_request_.reset();
            return DockCloseOutcome::cancelled;
        }
        if (closing.defer) {
            (*close_request_).asked = count;
            return DockCloseOutcome::pending;
        }
        (*close_request_).next = (*close_request_).next + count;
    }
    if (!close_request_) return DockCloseOutcome::cancelled;
    return finish_close();
}

// Every document was allowed: they close, with the request's companion
// edits, in one user transaction.
DockCloseOutcome DockWorkspace::finish_close() {
    CloseRequest request = std::move(*close_request_);
    close_request_.reset();
    DockTransaction transaction{};
    transaction.origin = DockEditOrigin::user;
    for (const DockPanelId& document : request.documents) {
        if (placed(document)) transaction.edits.push_back(DockEdit::close(document));
    }
    transaction.edits.insert(transaction.edits.end(), request.companions.begin(), request.companions.end());
    if (transaction.edits.empty()) return DockCloseOutcome::closed;
    const DockTransactionResult result = layout_.apply(transaction);
    if (!result) return DockCloseOutcome::refused;
    for (const DockPanelId& panel : request.closed_tools) {
        PanelEntry* const entry = find_entry(panel);
        if (entry != nullptr) (*entry).content.reset();
    }
    return DockCloseOutcome::closed;
}

// -- saving and restoring layouts (phase D7) -------------------------------------

DockLayoutWriteResult DockWorkspace::save_layout() const {
    DockLayoutWriteResult written = layout_.write_xml();
    return written;
}

// The restore is one layout change: on_layout_changed then carries it out
// on the windows in one WorkspaceTransaction (floating windows opened and
// destroyed as for any change) and the sites create only what they show.
DockRestoreResult DockWorkspace::load_layout(std::string_view xml, const DockRestoreOptions& options) {
    // The monitors must outlive the restore, which reads them through the
    // options' span.
    const std::vector<DockMonitor> attached = options.monitors.empty() ? monitors() : std::vector<DockMonitor>{};
    DockRestoreOptions used = options;
    if (options.monitors.empty()) used.monitors = attached;
    // A waiting close request holds documents of the layout being replaced:
    // it ends, closing nothing, before the restore changes the layout.
    close_request_.reset();
    DockRestoreResult result = layout_.restore_xml(xml, used);
    return result;
}

const DockNamedLayout* DockWorkspace::find_named_layout(std::string_view name) const noexcept {
    for (const DockNamedLayout& layout : named_layouts_) {
        if (layout.name == name) return &layout;
    }
    return nullptr;
}

namespace {

void require_layout_name(std::string_view name) {
    if (name.empty() || !validate_utf8(name).valid()) {
        throw std::invalid_argument("GUI.Forms named layout names must be non-empty valid UTF-8");
    }
}

} // namespace

bool DockWorkspace::save_named_layout(std::string name) {
    require_layout_name(name);
    DockLayoutWriteResult written = layout_.write_xml();
    if (!written) return false;
    for (DockNamedLayout& layout : named_layouts_) {
        if (layout.name != name) continue;
        layout.xml = std::move(written.text);
        return true;
    }
    DockNamedLayout added{};
    added.name = std::move(name);
    added.xml = std::move(written.text);
    named_layouts_.push_back(std::move(added));
    return true;
}

DockRestoreResult DockWorkspace::apply_named_layout(std::string_view name, const DockRestoreOptions& options) {
    const DockNamedLayout* const layout = find_named_layout(name);
    if (layout == nullptr) {
        DockRestoreResult missing{};
        missing.status = DockRestoreStatus::kept_current;
        DockRestoreDiagnostic diagnostic{};
        diagnostic.kind = DockRestoreDiagnosticKind::not_a_layout;
        diagnostic.detail = "no named layout \"" + std::string(name) + "\"";
        missing.diagnostics.push_back(std::move(diagnostic));
        return missing;
    }
    // The restore's handlers may save or remove named layouts: the XML is
    // copied first so it stays valid through the restore.
    const std::string xml = (*layout).xml;
    DockRestoreResult result = load_layout(xml, options);
    return result;
}

bool DockWorkspace::remove_named_layout(std::string_view name) {
    for (std::size_t index = 0; index < named_layouts_.size(); ++index) {
        if (named_layouts_[index].name != name) continue;
        named_layouts_.erase(named_layouts_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }
    return false;
}

void DockWorkspace::set_named_layouts(std::vector<DockNamedLayout> layouts) {
    for (std::size_t index = 0; index < layouts.size(); ++index) {
        require_layout_name(layouts[index].name);
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (layouts[earlier].name == layouts[index].name) {
                throw std::invalid_argument("GUI.Forms named layout names must not repeat");
            }
        }
    }
    named_layouts_ = std::move(layouts);
}

// -- content -------------------------------------------------------------------

Control::Ptr DockWorkspace::panel_content(const DockPanelId& panel) const {
    const PanelEntry* const entry = find_entry(panel);
    if (entry == nullptr) return {};
    return (*entry).content;
}

Control::Ptr DockWorkspace::create_panel_content(const DockPanelId& panel) {
    PanelEntry* entry = find_entry(panel);
    if (entry == nullptr) return {};
    if ((*entry).content) return (*entry).content;
    if (!(*entry).registration.factory) return {};
    DockPanelCreation creation{};
    creation.panel = panel;
    // Copied: the factory may register panels, which moves the entries.
    const Delegate<DockPanelCreation&> factory = (*entry).registration.factory;
    factory(creation);
    if (!creation.content) return {};
    entry = find_entry(panel);
    if (entry == nullptr) return {};
    (*entry).content = std::move(creation.content);
    ++created_count_;
    // The new content may carry the active panel's or document's targets.
    publish_command_targets();
    return (*entry).content;
}

// -- activation ----------------------------------------------------------------

bool DockWorkspace::activate_panel(const DockPanelId& panel, DockActivationCause cause) {
    if (find_entry(panel) == nullptr) return false;
    const DockPanelLocation location = layout_.locate(panel);
    if (location.placement == DockPlacementKind::auto_hidden) {
        // Its flyout opens with focus inside, which makes it active; the
        // stack's selection stays as it is (a flyout never edits the layout).
        if (site_ == nullptr || !(*site_).open_flyout(panel, DockFlyoutCause::programmatic)) return false;
        note_active(panel, cause);
        return true;
    }
    if (location.placement != DockPlacementKind::docked && location.placement != DockPlacementKind::floating) {
        return false;
    }
    if (location.placement == DockPlacementKind::floating && floating_site(location.site) == nullptr) return false;
    const DockNode* const stack = layout_.find_node(location.stack);
    if (stack != nullptr && (*stack).selected != location.index) {
        DockTransaction transaction{};
        transaction.origin = cause == DockActivationCause::programmatic ? DockEditOrigin::program : DockEditOrigin::user;
        transaction.edits.push_back(DockEdit::select(panel));
        if (!layout_.apply(transaction)) return false;
    }
    note_active(panel, cause);
    DockSite* const site = site_of(layout_.locate(panel));
    if (site != nullptr) (*site).focus_panel(panel);
    return true;
}

void DockWorkspace::set_track(DockActivationTrack track, const std::optional<DockPanelId>& panel,
                              DockActivationCause cause) {
    std::optional<DockPanelId>& held = track == DockActivationTrack::panel ? active_panel_ : active_document_;
    if (held == panel) return;
    DockActivationChange change{};
    change.track = track;
    change.old_panel = held;
    change.new_panel = panel;
    change.cause = cause;
    held = panel;
    activation_changed_.emit(change);
}

void DockWorkspace::note_active(const DockPanelId& panel, DockActivationCause cause) {
    if (find_entry(panel) == nullptr) return;
    const bool document = panel_kind(panel) == DockPanelKind::document;
    if (document) {
        bring_to_front(document_history_, panel);
    } else {
        bring_to_front(tool_history_, panel);
    }
    const std::optional<DockPanelId> chosen = panel;
    set_track(DockActivationTrack::panel, chosen, cause);
    if (document) set_track(DockActivationTrack::document, chosen, cause);
    publish_command_targets();
}

void DockWorkspace::publish_command_targets() {
    std::shared_ptr<CommandTarget> panel_target{};
    if (active_panel_) {
        const Control::Ptr content = panel_content(*active_panel_);
        if (content) panel_target = (*content).command_target();
    }
    std::shared_ptr<CommandTarget> document_target{};
    if (active_document_) {
        const Control::Ptr content = panel_content(*active_document_);
        if (content) document_target = (*content).command_target();
    }
    Window* const main = main_model();
    std::shared_ptr<CommandTarget> main_target{};
    if (main != nullptr) {
        (*main).set_command_target(CommandRouteStage::active_panel, panel_target);
        (*main).set_command_target(CommandRouteStage::active_document, document_target);
        main_target = (*main).command_target(CommandRouteStage::main_window);
    }
    for (DockSite* const site : floating_sites_) {
        Window* const host = (*site).attached_window();
        if (host == nullptr || host == main) continue;
        (*host).set_command_target(CommandRouteStage::active_panel, panel_target);
        (*host).set_command_target(CommandRouteStage::active_document, document_target);
        (*host).set_command_target(CommandRouteStage::main_window, main_target);
    }
}

// The layout committed a transaction: panels that left it leave the
// histories, a document that left releases its content, and an active panel
// or document that left is replaced by the next in its history (the active
// panel by the active document). Then the sites realize the change and the
// floating windows follow the floating sites, inside one cross-window
// transaction while windows are attached (a nested change joins it).
void DockWorkspace::on_layout_changed(const DockLayoutChange& change) {
    if (change.frames_only) {
        follow_layout_frames();
        return;
    }
    tree_generation_ = change.generation;
    const std::array<std::vector<DockPanelId>*, 2> histories{&tool_history_, &document_history_};
    for (std::vector<DockPanelId>* const history : histories) {
        std::size_t index = 0U;
        while (index < (*history).size()) {
            if (placed((*history)[index])) {
                ++index;
                continue;
            }
            (*history).erase((*history).begin() + static_cast<std::ptrdiff_t>(index));
        }
    }
    for (PanelEntry& entry : panels_) {
        const bool document = entry.registration.descriptor.kind == DockPanelKind::document;
        if (document && entry.content && layout_.locate(entry.registration.descriptor.id).placement ==
                                             DockPlacementKind::absent) {
            entry.content.reset();
        }
    }
    if (active_document_ && !placed(*active_document_)) {
        std::optional<DockPanelId> next{};
        if (!document_history_.empty()) next = document_history_.front();
        set_track(DockActivationTrack::document, next, DockActivationCause::fallback);
    }
    if (active_panel_ && !placed(*active_panel_)) {
        set_track(DockActivationTrack::panel, active_document_, DockActivationCause::fallback);
    }
    publish_command_targets();
    Window* const main = main_model();
    if (transaction_ != nullptr || !windows_attached() || main == nullptr) {
        realize_on_sites(change);
        if (transaction_ != nullptr) reconcile_windows(*transaction_);
        return;
    }
    WorkspaceTransaction transaction(main_window_.windows());
    transaction.include(*main);
    for (const FloatingWindow& window : windows_) {
        if (window.model != nullptr && !window.destroying) transaction.include(*window.model);
    }
    transaction_ = &transaction;
    try {
        realize_on_sites(change);
        reconcile_windows(transaction);
    } catch (...) {
        transaction_ = nullptr;
        throw;
    }
    transaction_ = nullptr;
    static_cast<void>(transaction.commit());
    write_pending_frames();
}

// The bound sites realize a change, the main site first. The list is copied
// first, and a site a handler unbound in the meantime is skipped.
void DockWorkspace::realize_on_sites(const DockLayoutChange& change) {
    const std::vector<DockSite*> sites = bound_sites();
    for (DockSite* const site : sites) {
        if (bound(site)) (*site).on_layout_changed(change);
    }
}

// -- sites ---------------------------------------------------------------------

void DockWorkspace::attach_site(DockSite& site) {
    if (!site.floating()) {
        site_ = &site;
        return;
    }
    for (DockSite*& bound_site : floating_sites_) {
        if ((*bound_site).floating_site() == site.floating_site()) {
            bound_site = &site;
            return;
        }
    }
    floating_sites_.push_back(&site);
}

void DockWorkspace::detach_site(DockSite& site) noexcept {
    if (site_ == &site) site_ = nullptr;
    const std::vector<DockSite*>::iterator found = std::find(floating_sites_.begin(), floating_sites_.end(), &site);
    if (found != floating_sites_.end()) floating_sites_.erase(found);
}

std::vector<DockSite*> DockWorkspace::bound_sites() const {
    std::vector<DockSite*> sites{};
    sites.reserve(floating_sites_.size() + 1U);
    if (site_ != nullptr) sites.push_back(site_);
    sites.insert(sites.end(), floating_sites_.begin(), floating_sites_.end());
    return sites;
}

bool DockWorkspace::bound(const DockSite* site) const noexcept {
    if (site == nullptr) return false;
    if (site == site_) return true;
    const bool floating = std::find(floating_sites_.begin(), floating_sites_.end(), site) != floating_sites_.end();
    return floating;
}

DockSite* DockWorkspace::floating_site(DockSiteId site) const noexcept {
    for (DockSite* const bound_site : floating_sites_) {
        if ((*bound_site).floating_site() == site) return bound_site;
    }
    return nullptr;
}

DockSite* DockWorkspace::site_of(const DockPanelLocation& location) const noexcept {
    if (location.placement == DockPlacementKind::floating) {
        DockSite* const site = floating_site(location.site);
        return site;
    }
    if (location.placement == DockPlacementKind::docked || location.placement == DockPlacementKind::auto_hidden) {
        return site_;
    }
    return nullptr;
}

// -- floating windows ----------------------------------------------------------

bool DockWorkspace::attach_windows(ApplicationWindowHandle main_window, std::string main_stable_id) {
    if (main_window_.active() || !main_window.active() || main_stable_id.empty()) return false;
    const HostCapabilities capabilities = main_window.windows().capabilities();
    if (!capabilities.supports(HostCapability::runtime_windows)) return false;
    main_window_ = std::move(main_window);
    main_window_id_ = std::move(main_stable_id);
    liaison_ = std::make_shared<WindowLiaison>();
    (*liaison_).workspace = this;
    Window* const main = main_model();
    if (main == nullptr || layout_.state().floating_sites.empty()) return true;
    WorkspaceTransaction transaction(main_window_.windows());
    transaction.include(*main);
    transaction_ = &transaction;
    try {
        reconcile_windows(transaction);
    } catch (...) {
        transaction_ = nullptr;
        throw;
    }
    transaction_ = nullptr;
    static_cast<void>(transaction.commit());
    write_pending_frames();
    return true;
}

void DockWorkspace::detach_windows() noexcept {
    if (liaison_) (*liaison_).workspace = nullptr;
    liaison_.reset();
    std::vector<FloatingWindow> windows{};
    windows.swap(windows_);
    const ApplicationWindows run = main_window_.windows();
    for (FloatingWindow& window : windows) {
        DockSite* const site = floating_site(window.site);
        if (site != nullptr) {
            detach_site(*site);
            (*site).release_workspace();
        }
        if (window.handle.active()) static_cast<void>(run.destroy_window(window.handle));
    }
    pending_frames_.clear();
    main_window_ = ApplicationWindowHandle{};
    main_window_id_.clear();
}

ApplicationWindowHandle DockWorkspace::floating_window(DockSiteId site) const {
    const FloatingWindow* const window = find_window(site);
    if (window == nullptr || (*window).destroying) return {};
    const ApplicationWindowHandle handle = (*window).handle;
    return handle;
}

std::size_t DockWorkspace::floating_window_count() const noexcept {
    std::size_t count = 0U;
    for (const FloatingWindow& window : windows_) {
        if (!window.destroying && window.handle.active()) ++count;
    }
    return count;
}

DockWorkspace::FloatingWindow* DockWorkspace::find_window(DockSiteId site) noexcept {
    for (FloatingWindow& window : windows_) {
        if (window.site == site) return &window;
    }
    return nullptr;
}

const DockWorkspace::FloatingWindow* DockWorkspace::find_window(DockSiteId site) const noexcept {
    for (const FloatingWindow& window : windows_) {
        if (window.site == site) return &window;
    }
    return nullptr;
}

Window* DockWorkspace::main_model() const noexcept {
    if (site_ == nullptr) return nullptr;
    Window* const window = (*site_).attached_window();
    return window;
}

std::vector<DockMonitor> DockWorkspace::monitors() const {
    std::vector<DockMonitor> converted{};
    if (!main_window_.active()) return converted;
    const HostMonitorResult result = main_window_.windows().monitors();
    if (!result.status.accepted()) return converted;
    converted.reserve(result.monitors.size());
    for (const HostMonitor& monitor : result.monitors) {
        DockMonitor entry{};
        entry.id = monitor.id;
        entry.work_area = monitor.work_area;
        entry.primary = monitor.primary;
        converted.push_back(std::move(entry));
    }
    return converted;
}

// The monitor whose work area holds the frame's centre, else the one it
// overlaps most; empty without monitors.
std::string DockWorkspace::monitor_at(Rect frame) const {
    const std::vector<DockMonitor> known = monitors();
    const Point centre{frame.x + frame.width * 0.5, frame.y + frame.height * 0.5};
    for (const DockMonitor& monitor : known) {
        if (monitor.work_area.contains(centre)) return monitor.id;
    }
    double best = 0.0;
    std::string chosen{};
    for (const DockMonitor& monitor : known) {
        const double area = overlap_area(monitor.work_area, frame);
        if (area <= best) continue;
        best = area;
        chosen = monitor.id;
    }
    return chosen;
}

std::string DockWorkspace::window_title(const DockFloatingSite& site) const {
    std::vector<DockNodeId> stacks{};
    collect_stacks(layout_, site.root, stacks);
    if (stacks.empty()) return {};
    const DockNode* const stack = layout_.find_node(stacks.front());
    if (stack == nullptr || (*stack).panels.empty() || (*stack).selected >= (*stack).panels.size()) return {};
    const std::string title = panel_title((*stack).panels[(*stack).selected]);
    return title;
}

HostWindowFloating DockWorkspace::window_kind(const DockFloatingSite& site) const {
    std::vector<DockNodeId> stacks{};
    collect_stacks(layout_, site.root, stacks);
    for (const DockNodeId stack : stacks) {
        const DockNode* const node = layout_.find_node(stack);
        if (node != nullptr && (*node).kind == DockNodeKind::document_stack) return HostWindowFloating::document;
    }
    return HostWindowFloating::tool;
}

void DockWorkspace::reconcile_windows(WorkspaceTransaction& transaction) {
    if (!windows_attached() || main_model() == nullptr) return;
    for (FloatingWindow& window : windows_) {
        if (window.destroying) continue;
        const DockFloatingSite* const site = layout_.find_floating_site(window.site);
        if (site == nullptr) {
            window.destroying = true;
            if (window.handle.active()) static_cast<void>(transaction.destroy_window(window.handle));
            continue;
        }
        std::string title = window_title(*site);
        if (title != window.title) {
            window.title = std::move(title);
            static_cast<void>(window.handle.set_title(window.title));
        }
    }
    // Copied: opening a window runs callbacks that may edit the layout.
    const std::vector<DockFloatingSite> sites = layout_.state().floating_sites;
    for (const DockFloatingSite& site : sites) {
        if (find_window(site.id) == nullptr && layout_.find_floating_site(site.id) != nullptr) {
            open_window(site, transaction);
        }
    }
}

// Opens the floating window of `site`: hidden in the transaction, its root a
// DockSite realizing the site, its client area the site's frame placed on a
// monitor so its caption can be reached.
void DockWorkspace::open_window(const DockFloatingSite& site, WorkspaceTransaction& transaction) {
    Window* const main = main_model();
    if (main == nullptr) return;
    const ApplicationWindows run = main_window_.windows();
    if (run.window_count() >= Application::maximum_windows) return;
    const std::vector<DockMonitor> known = monitors();
    const DockMonitorPlacement placement = place_on_monitor(site.frame, site.monitor_id, known);
    Rect frame = placement.frame;
    frame.width = std::max(frame.width, smallest_floating_window.width);
    frame.height = std::max(frame.height, smallest_floating_window.height);
    // A frame the fallback had to move (its monitor gone, or off its work
    // area) comes wholly inside the work area when it fits there, not only
    // with its caption reachable.
    if (placement.clamped) {
        for (const DockMonitor& monitor : known) {
            if (monitor.id != placement.monitor_id) continue;
            const Rect area = monitor.work_area;
            if (frame.width <= area.width) frame.x = std::clamp(frame.x, area.x, area.right() - frame.width);
            if (frame.height <= area.height) frame.y = std::clamp(frame.y, area.y, area.bottom() - frame.height);
        }
    }
    const std::string identity = stable_id_ + ".floating." + std::to_string(site.id.value);
    // The site binds itself to this workspace as it is made.
    const std::shared_ptr<DockSite> control = make_control<DockSite>(StableId(identity), *this, site.id);
    const Size minimum = (*control).minimum_size();
    FloatingWindow record{};
    record.site = site.id;
    record.kind = window_kind(site);
    record.frame = frame;
    record.title = window_title(site);
    windows_.push_back(record);
    ApplicationWindow entry{};
    entry.stable_id = identity;
    entry.owner_id = main_window_id_;
    entry.floating = record.kind;
    entry.model = std::make_unique<Window>(control, Size{frame.width, frame.height}, (*main).resources());
    // One application key set (owner decision DK-44).
    (*entry.model).set_key_bindings((*main).share_key_bindings());
    entry.options.title = record.title;
    entry.options.initial_size = Size{frame.width, frame.height};
    entry.options.minimum_size = Size{std::clamp(minimum.width, smallest_floating_window.width, frame.width),
                                      std::clamp(minimum.height, smallest_floating_window.height, frame.height)};
    entry.options.initially_visible = true;
    entry.options.ready = FloatingReady{liaison_, site.id};
    entry.options.closing = FloatingClosing{liaison_, site.id};
    entry.options.closed = FloatingClosed{liaison_, site.id};
    entry.options.frame_changed = FloatingFrame{liaison_, site.id};
    const ApplicationWindowOpenResult opened = transaction.open_window(std::move(entry));
    if (!opened.status.accepted() || !opened.window.active()) {
        // The site stays in the layout without a window; the next change or
        // attach tries again.
        forget_window(site.id);
        return;
    }
    FloatingWindow* const kept = find_window(site.id);
    if (kept == nullptr) return;
    (*kept).handle = opened.window;
    static_cast<void>((*kept).handle.request_frame(frame));
    apply_window_visibility(*kept);
}

void DockWorkspace::on_window_ready(DockSiteId site, Window& window, const ApplicationWindowHandle& handle) {
    FloatingWindow* const record = find_window(site);
    if (record == nullptr) return;
    (*record).model = &window;
    (*record).handle = handle;
    // The frame and the inactive-visibility option asked for at opening
    // reach the host now: before the window is ready both are refused.
    static_cast<void>((*record).handle.request_frame((*record).frame));
    apply_window_visibility(*record);
    publish_command_targets();
}

// The close button. With a main site bound, the window stays for now: its
// panels go first, in a transaction posted to the main window (so it runs
// after this request returns), and that transaction destroys the window.
// Without one (no main site is bound, or it is in no window) there is no
// main window to post to and no transaction would destroy the window: the
// panels go at once, as close_floating_site does (once its documents are
// allowed to close, phase D6), and the close goes ahead,
// so the host closes the window and on_window_closed forgets it. If that
// transaction is refused (the docking lock), the window still closes and
// its site stays in the layout without a window, as after a refused opening;
// the next attach opens it again.
void DockWorkspace::on_window_closing(DockSiteId site, HostCloseRequest& request) {
    Window* const main = main_model();
    if (main == nullptr) {
        // A prompt that kept the documents, or waits for its answer, keeps
        // the window too; an answer that closes them later leaves the site
        // empty in a window that stays until the next attach.
        const DockCloseOutcome outcome = close_floating_site(site);
        if (outcome == DockCloseOutcome::cancelled || outcome == DockCloseOutcome::pending) request.cancel = true;
        return;
    }
    request.cancel = true;
    static_cast<void>((*main).begin_invoke((*site_).shared_from_this(), CloseFloatingSite{liaison_, site}));
}

void DockWorkspace::on_window_closed(DockSiteId site) noexcept {
    forget_window(site);
}

// Drops the record of a site's floating window and unbinds the DockSite that
// realized it (the window's root, which then shows nothing).
void DockWorkspace::forget_window(DockSiteId site) noexcept {
    if (layout_.find_floating_site(site) == nullptr) {
        for (std::size_t entry = 0; entry < floating_visibility_.size(); ++entry) {
            if (!(floating_visibility_[entry].site == site)) continue;
            floating_visibility_.erase(floating_visibility_.begin() + static_cast<std::ptrdiff_t>(entry));
            break;
        }
    }
    for (std::size_t index = 0; index < windows_.size(); ++index) {
        if (!(windows_[index].site == site)) continue;
        windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(index));
        break;
    }
    DockSite* const control = floating_site(site);
    if (control == nullptr) return;
    detach_site(*control);
    (*control).release_workspace();
}

void DockWorkspace::on_window_frame(DockSiteId site, const HostWindowFrameEvent& event) {
    FloatingWindow* const record = find_window(site);
    if (record == nullptr || (*record).destroying) return;
    (*record).frame = event.frame.content;
    const DockFloatingSite* const floating = layout_.find_floating_site(site);
    if (floating == nullptr || (*floating).frame == event.frame.content) return;
    if (transaction_ != nullptr) {
        pending_frames_.push_back(PendingFrame{site, event.frame.content});
        return;
    }
    DockTransaction transaction{};
    transaction.origin = DockEditOrigin::user;
    transaction.edits.push_back(
        DockEdit::set_floating_frame(site, event.frame.content, monitor_at(event.frame.content)));
    static_cast<void>(layout_.apply(transaction));
}

void DockWorkspace::write_pending_frames() {
    if (pending_frames_.empty()) return;
    std::vector<PendingFrame> frames{};
    frames.swap(pending_frames_);
    DockTransaction transaction{};
    transaction.origin = DockEditOrigin::user;
    for (const PendingFrame& frame : frames) {
        const DockFloatingSite* const floating = layout_.find_floating_site(frame.site);
        if (floating == nullptr || (*floating).frame == frame.frame) continue;
        transaction.edits.push_back(DockEdit::set_floating_frame(frame.site, frame.frame, monitor_at(frame.frame)));
    }
    static_cast<void>(layout_.apply(transaction));
}

void DockWorkspace::follow_layout_frames() {
    for (FloatingWindow& window : windows_) {
        if (window.destroying || !window.handle.active()) continue;
        const DockFloatingSite* const floating = layout_.find_floating_site(window.site);
        if (floating == nullptr || (*floating).frame == window.frame) continue;
        window.frame = (*floating).frame;
        static_cast<void>(window.handle.request_frame(window.frame));
    }
}

// Hides the site's tool panels and closes its documents and the panels that
// may not be hidden, as one user transaction, once the documents are allowed
// to close.
DockCloseOutcome DockWorkspace::close_floating_site(DockSiteId site) {
    const DockFloatingSite* const floating = layout_.find_floating_site(site);
    if (floating == nullptr) return DockCloseOutcome::refused;
    std::vector<DockNodeId> stacks{};
    collect_stacks(layout_, (*floating).root, stacks);
    // The documents are asked about first (phase D6); the tool panels hide,
    // or close, with them in one transaction, so a cancelled prompt leaves
    // the window as it was.
    CloseRequest request{};
    request.cause = DockCloseCause::floating_window;
    for (const DockNodeId stack : stacks) {
        const DockNode* const node = layout_.find_node(stack);
        if (node == nullptr) continue;
        for (const DockPanelId& panel : (*node).panels) {
            const DockPanelDescriptor* const descriptor = layout_.find_descriptor(panel);
            const bool tool = descriptor != nullptr && (*descriptor).kind == DockPanelKind::tool;
            if (!tool) {
                request.documents.push_back(panel);
            } else if ((*descriptor).allowed.hidden) {
                request.companions.push_back(DockEdit::hide(panel));
            } else {
                request.companions.push_back(DockEdit::close(panel));
                request.closed_tools.push_back(panel);
            }
        }
    }
    const DockCloseOutcome outcome = begin_close(std::move(request));
    return outcome;
}

bool DockWorkspace::can_float(const DockSubject& subject) const {
    if (!windows_attached() || main_model() == nullptr) return false;
    if (main_window_.windows().window_count() >= Application::maximum_windows) return false;
    // The probe's frame only has to be valid: the restrictions, the lock and
    // the site limit decide, not where the window would stand.
    DockTransaction transaction{};
    transaction.origin = DockEditOrigin::user;
    transaction.edits.push_back(DockEdit::float_subject(subject, Rect{0.0, 0.0, 320.0, 240.0}, std::string{}));
    const bool accepted = layout_.check(transaction).applied;
    return accepted;
}

DockTransactionResult DockWorkspace::float_subject(const DockSubject& subject, Rect frame, DockEditOrigin origin) {
    if (!can_float(subject)) {
        DockTransactionResult refused{};
        refused.generation = layout_.generation();
        refused.refusal.reason = DockRefusalReason::placement_not_allowed;
        refused.refusal.layer = DockRefusalLayer::model;
        return refused;
    }
    DockTransaction transaction{};
    transaction.origin = origin;
    transaction.edits.push_back(DockEdit::float_subject(subject, frame, monitor_at(frame)));
    const DockTransactionResult result = layout_.apply(transaction);
    return result;
}

std::optional<DockEdit> DockWorkspace::dock_back_edit(const DockSubject& subject, DockEditOrigin origin) const {
    DockPanelId panel = subject.panel;
    if (subject.kind == DockSubjectKind::stack) {
        const DockNode* const node = layout_.find_node(subject.stack);
        if (node == nullptr || (*node).panels.empty() || (*node).selected >= (*node).panels.size()) {
            return std::nullopt;
        }
        panel = (*node).panels[(*node).selected];
    }
    const DockPanelDescriptor* const descriptor = layout_.find_descriptor(panel);
    if (descriptor == nullptr) return std::nullopt;
    std::vector<DockEdit> candidates{};
    candidates.reserve(3U);
    const DockPanelRecord* const record = find_record(layout_, panel);
    if (record != nullptr && (*record).docked) {
        const DockDockedMemory& memory = *(*record).docked;
        const DockNode* const stack = layout_.find_node(memory.stack);
        const bool a_stack = stack != nullptr && ((*stack).kind == DockNodeKind::tool_stack ||
                                                  (*stack).kind == DockNodeKind::document_stack);
        if (a_stack && in_main_site(layout_, memory.stack) && !(memory.stack == subject.stack)) {
            candidates.push_back(DockEdit::dock_into(subject, memory.stack, memory.index));
        }
        if (layout_.find_node(memory.neighbour) != nullptr && in_main_site(layout_, memory.neighbour)) {
            const double ratio = memory.ratio > 0.0 && memory.ratio < 1.0 ? memory.ratio : 0.25;
            candidates.push_back(DockEdit::dock_beside(subject, memory.neighbour, memory.side, ratio));
        }
    }
    if ((*descriptor).kind == DockPanelKind::document) {
        candidates.push_back(DockEdit::dock_into(subject, layout_.document_area()));
    } else {
        const DockDefaultPlacement& placement = (*descriptor).default_placement;
        candidates.push_back(DockEdit::dock_at_edge(subject, placement.side, placement.ratio));
    }
    DockTransaction trial{};
    trial.origin = origin;
    trial.edits.resize(1U);
    for (const DockEdit& candidate : candidates) {
        trial.edits[0] = candidate;
        if (layout_.check(trial).applied) return candidate;
    }
    return std::nullopt;
}

DockTransactionResult DockWorkspace::dock_back(const DockSubject& subject, DockEditOrigin origin) {
    const std::optional<DockEdit> edit = dock_back_edit(subject, origin);
    if (!edit) {
        DockTransactionResult refused{};
        refused.generation = layout_.generation();
        refused.refusal.reason = DockRefusalReason::not_placed;
        refused.refusal.layer = DockRefusalLayer::model;
        return refused;
    }
    DockPanelId panel = subject.panel;
    if (subject.kind == DockSubjectKind::stack) {
        const DockNode* const node = layout_.find_node(subject.stack);
        if (node != nullptr && !(*node).panels.empty()) panel = (*node).panels[(*node).selected];
    }
    DockTransaction transaction{};
    transaction.origin = origin;
    transaction.edits.push_back(*edit);
    const DockTransactionResult result = layout_.apply(transaction);
    if (result) {
        const DockActivationCause cause =
            origin == DockEditOrigin::user ? DockActivationCause::pointer : DockActivationCause::programmatic;
        static_cast<void>(activate_panel(panel, cause));
    }
    return result;
}

// -- geometry across windows ---------------------------------------------------

ApplicationWindowHandle DockWorkspace::window_of(const DockSite& site) const {
    if (&site == site_) return main_window_;
    if (!site.floating()) return {};
    const ApplicationWindowHandle handle = floating_window(site.floating_site());
    return handle;
}

std::optional<Point> DockWorkspace::desktop_origin(const DockSite& site) const {
    const ApplicationWindowHandle handle = window_of(site);
    if (!handle.active()) return std::nullopt;
    const HostWindowFrameResult frame = handle.frame();
    if (!frame.status.accepted()) return std::nullopt;
    const Rect absolute = site.absolute_bounds();
    const Point origin{frame.frame.content.x + absolute.x, frame.frame.content.y + absolute.y};
    return origin;
}

bool DockWorkspace::site_contains(const DockSite& site, Point desktop) const {
    const std::optional<Point> origin = desktop_origin(site);
    if (!origin) return false;
    const Rect client = site.client_rectangle();
    const Rect bounds{(*origin).x, (*origin).y, client.width, client.height};
    const bool inside = bounds.contains(desktop);
    return inside;
}

DockSite* DockWorkspace::site_at(Point desktop, const DockSite* excluded) const {
    if (!windows_attached()) return nullptr;
    // The docking controls keep desktop positions as Point in logical units.
    const DesktopLogicalPoint desktop_point{desktop.x, desktop.y};
    const ApplicationWindowAtResult at = main_window_.windows().window_at(desktop_point);
    if (!at.status.accepted() || !at.found) return nullptr;
    const HostWindowId topmost = at.window.id();
    const std::vector<DockSite*> sites = bound_sites();
    bool excluded_on_top = false;
    for (DockSite* const site : sites) {
        const ApplicationWindowHandle handle = window_of(*site);
        if (!handle.active() || !(handle.id() == topmost)) continue;
        if (site == excluded) {
            excluded_on_top = true;
            break;
        }
        if (site_contains(*site, desktop)) return site;
        return nullptr;
    }
    if (!excluded_on_top) return nullptr;
    // The excluded window is on top (it is being moved under the pointer):
    // the floating windows opened later first, then the earlier ones, then
    // the main window, which floating windows always stand above.
    for (std::vector<FloatingWindow>::const_reverse_iterator window = windows_.rbegin(); window != windows_.rend();
         ++window) {
        DockSite* const site = floating_site((*window).site);
        if (site == nullptr || site == excluded || (*window).destroying) continue;
        if (site_contains(*site, desktop)) return site;
    }
    if (site_ != nullptr && site_ != excluded && site_contains(*site_, desktop)) return site_;
    return nullptr;
}

void DockWorkspace::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), workspace_interactions.begin(), workspace_interactions.end());
}

// property_codemod begin DockWorkspace (tools/property_families/dock_workspace.json; generated, do not edit)

void DockWorkspace::prepare_write(const decltype(close_prompt)&, DockClosePrompt&) {}

void DockWorkspace::written(const decltype(close_prompt)&) {}

void DockWorkspace::prepare_write(const decltype(flyout_opening)&, DockFlyoutOpening&) {}

void DockWorkspace::written(const decltype(flyout_opening)&) {}

// property_codemod end DockWorkspace

} // namespace gui_forms
