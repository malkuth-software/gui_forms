#include "gui_forms/window.hpp"
#include "gui_forms/window/document_area/window_document_area.hpp"

#include <stxlib/optional.hpp>
#include <cstddef>
#include <stdexcept>

// The multiple document interface of a Window (DK decisions 60 to 63): the
// window's WinForms-named calls forward to the bound document area, which
// the docking workspace implements.

namespace gui_forms {

WindowDocumentArea::~WindowDocumentArea() {
    if (window_ != nullptr) (*window_).unbind_document_area(*this);
}

void WindowDocumentArea::raise_document_activated(const MdiChildActivation& activation) {
    if (window_ == nullptr) return;
    (*window_).mdi_child_activate_.emit(activation);
}

bool Window::is_mdi_container() const {
    if (document_area_ != nullptr) return (*document_area_).document_frames_shown();
    return mdi_container_;
}

void Window::set_is_mdi_container(bool container) {
    mdi_container_ = container;
    mdi_container_written_ = true;
    if (document_area_ != nullptr) (*document_area_).show_document_frames(container);
}

BorrowedSpan<DockPanelId> Window::mdi_children() const& {
    mdi_children_.clear();
    if (document_area_ != nullptr) {
        const std::size_t count = (*document_area_).document_count();
        mdi_children_.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            mdi_children_.push_back((*document_area_).document_at(index));
        }
    }
    return detail::lend_view(mdi_children_);
}

stxlib::Optional<DockPanelId> Window::active_mdi_child() const {
    if (document_area_ == nullptr) return stxlib::Optional<DockPanelId>::none();
    return (*document_area_).active_document();
}

bool Window::activate_mdi_child(const DockPanelId& child) {
    if (document_area_ == nullptr) return false;
    const bool activated = (*document_area_).activate_document(child);
    return activated;
}

bool Window::layout_mdi(MdiLayout layout) {
    const bool known = layout == MdiLayout::cascade || layout == MdiLayout::tile_horizontal ||
                       layout == MdiLayout::tile_vertical || layout == MdiLayout::arrange_icons;
    if (!known) throw std::invalid_argument("Window::layout_mdi: unknown MdiLayout");
    if (document_area_ == nullptr) return false;
    const bool arranged = (*document_area_).arrange_documents(layout);
    return arranged;
}

void Window::set_mdi_children_minimized_anchor_bottom(bool bottom) {
    mdi_icons_at_bottom_ = bottom;
    mdi_icons_written_ = true;
    if (document_area_ != nullptr) (*document_area_).set_icons_at_bottom(bottom);
}

void Window::set_main_menu_strip(const Control::Ptr& strip) {
    main_menu_strip_ = strip;
}

void Window::bind_document_area(WindowDocumentArea& area) {
    if (document_area_ == &area) return;
    if (document_area_ != nullptr) unbind_document_area(*document_area_);
    if (area.window_ != nullptr) (*area.window_).unbind_document_area(area);
    document_area_ = &area;
    area.window_ = this;
    // What the program wrote before the area came is given to it now; an
    // unwritten setting leaves the area's own (a restored layout's).
    if (mdi_container_written_) area.show_document_frames(mdi_container_);
    if (mdi_icons_written_) area.set_icons_at_bottom(mdi_icons_at_bottom_);
}

void Window::unbind_document_area(const WindowDocumentArea& area) noexcept {
    if (document_area_ != &area) return;
    (*document_area_).window_ = nullptr;
    document_area_ = nullptr;
    mdi_children_.clear();
}

void Window::release_document_area() noexcept {
    if (document_area_ == nullptr) return;
    (*document_area_).window_ = nullptr;
    document_area_ = nullptr;
}

} // namespace gui_forms
