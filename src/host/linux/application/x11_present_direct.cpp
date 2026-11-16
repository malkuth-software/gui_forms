#include "x11_present_direct.hpp"
#include "../services/x11_foreign_window.hpp"

#if defined(GUI_FORMS_HAVE_XPRESENT) && defined(GUI_FORMS_HAVE_XSHM)
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xpresent.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#define GUI_FORMS_X11_PRESENT_DIRECT 1
#endif

namespace gui_forms::host::linux_detail {

#if defined(GUI_FORMS_X11_PRESENT_DIRECT)
namespace {

// Set by attach_error while an attach is being checked: the server refused
// it (a remote display answers BadAccess). UI thread only.
thread_local bool attach_failed = false;

int attach_error(Display *, XErrorEvent *) noexcept {
  attach_failed = true;
  return 0;
}

// The largest pixmap made: the copy path's presentation limit (64 MiB).
constexpr std::size_t maximum_pixmap_bytes = 64U * 1024U * 1024U;

} // namespace

int x11_extension_opcode(Display *display, const char *name) noexcept {
  int opcode = 0;
  int event_base = 0;
  int error_base = 0;
  if (XQueryExtension(display, name, &opcode, &event_base, &error_base) == False) return -1;
  return opcode;
}

bool x11_raster_layout(Display *display, Visual *visual, int depth) noexcept {
  if (visual == nullptr || (depth != 24 && depth != 32)) return false;
  if ((*visual).red_mask != 0xFF0000UL || (*visual).green_mask != 0x00FF00UL || (*visual).blue_mask != 0x0000FFUL)
    return false;
  if (ImageByteOrder(display) != LSBFirst) return false;
  int count = 0;
  XPixmapFormatValues *formats = XListPixmapFormats(display, &count);
  if (formats == nullptr) return false;
  bool found = false;
  for (int index = 0; index < count; ++index) {
    if (formats[index].depth == depth) found = formats[index].bits_per_pixel == 32;
  }
  XFree(formats);
  return found;
}

int x11_present_opcode(Display *display) noexcept {
  int opcode = 0;
  int event_base = 0;
  int error_base = 0;
  if (XPresentQueryExtension(display, &opcode, &event_base, &error_base) == False) return -1;
  return opcode;
}

bool read_x11_present_event(Display *display, int opcode, XEvent &event, X11PresentEvent &read) {
  if (opcode < 0 || event.type != GenericEvent || event.xcookie.extension != opcode) return false;
  if (!XGetEventData(display, &event.xcookie)) return false;
  bool idle = false;
  if (event.xcookie.evtype == PresentIdleNotify) {
    const XPresentIdleNotifyEvent &notice = *static_cast<const XPresentIdleNotifyEvent *>(event.xcookie.data);
    read.idle = true;
    read.window = notice.window;
    read.pixmap = notice.pixmap;
    read.serial = notice.serial_number;
    idle = true;
  }
  XFreeEventData(display, &event.xcookie);
  return idle;
}

X11PresentPath::X11PresentPath(Display *display, ::Window window, Visual *visual, int depth) noexcept
    : display_(display), window_(window), visual_(visual), depth_(depth) {}

X11PresentPath::~X11PresentPath() {
  free_slots();
  if (selection_ != 0U) XPresentFreeInput(display_, window_, selection_);
  selection_ = 0U;
}

bool X11PresentPath::usable() {
  if (checked_ != Check::unchecked) return checked_ == Check::usable;
  checked_ = Check::unsupported;
  int major = 0;
  int minor = 0;
  Bool pixmaps = False;
  if (XShmQueryVersion(display_, &major, &minor, &pixmaps) == False || pixmaps == False) return false;
  if (XShmPixmapFormat(display_) != ZPixmap) return false;
  if (x11_present_opcode(display_) < 0) return false;
  if (!x11_raster_layout(display_, visual_, depth_)) return false;
  opcodes_[0] = x11_extension_opcode(display_, "MIT-SHM");
  opcodes_[1] = x11_extension_opcode(display_, "Present");
  opcodes_[2] = x11_extension_opcode(display_, "XFIXES");
  selection_ = XPresentSelectInput(display_, window_, PresentIdleNotifyMask);
  checked_ = Check::usable;
  return true;
}

bool X11PresentPath::make_slot(PixmapSlot &slot, presentation::PresentExtent extent) {
  const std::size_t row_bytes = static_cast<std::size_t>(extent.width) * 4U;
  const std::size_t bytes = row_bytes * static_cast<std::size_t>(extent.height);
  if (bytes == 0U || bytes > maximum_pixmap_bytes) return false;
  XShmSegmentInfo *segment = new XShmSegmentInfo{};
  (*segment).shmid = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
  if ((*segment).shmid < 0) {
    delete segment;
    return false;
  }
  void *address = shmat((*segment).shmid, nullptr, 0);
  if (address == reinterpret_cast<void *>(-1)) {
    shmctl((*segment).shmid, IPC_RMID, nullptr);
    delete segment;
    return false;
  }
  (*segment).shmaddr = static_cast<char *>(address);
  (*segment).readOnly = False;
  // The attach is checked synchronously: a refusal arrives as an error.
  XSync(display_, False);
  attach_failed = false;
  const XErrorHandler previous = XSetErrorHandler(attach_error);
  const Bool attached = XShmAttach(display_, segment);
  XSync(display_, False);
  XSetErrorHandler(previous);
  // Marked for removal now: the segment goes when both sides detach.
  shmctl((*segment).shmid, IPC_RMID, nullptr);
  if (attached == False || attach_failed) {
    shmdt((*segment).shmaddr);
    delete segment;
    return false;
  }
  // The pixmap is checked synchronously too: a refusal (BadAlloc,
  // BadMatch) is absorbed here and refuses resources, instead of reaching
  // Xlib's default handler later.
  bool refused = false;
  Pixmap pixmap = 0U;
  {
    ForeignWindowScope scope(display_, window_, std::span<const int>(opcodes_));
    pixmap = XShmCreatePixmap(display_, window_, (*segment).shmaddr, segment, extent.width, extent.height,
                              static_cast<unsigned>(depth_));
    refused = scope.synchronize();
    if (refused) {
      if (pixmap != 0U) XFreePixmap(display_, pixmap);
      XShmDetach(display_, segment);
      static_cast<void>(scope.synchronize());
    }
  }
  if (refused) {
    shmdt((*segment).shmaddr);
    delete segment;
    return false;
  }
  slot.pixmap = pixmap;
  slot.segment = segment;
  slot.pixels = reinterpret_cast<std::byte *>((*segment).shmaddr);
  slot.row_bytes = row_bytes;
  slot.reading = 0U;
  slot.shown_at = 0U;
  return true;
}

void X11PresentPath::free_slot(PixmapSlot &slot) noexcept {
  {
    // After a loss the server may already have dropped the pixmap or the
    // segment: those errors are absorbed. The scope's end waits until the
    // server has detached, before this process unmaps the segment.
    ForeignWindowScope scope(display_, window_, std::span<const int>(opcodes_));
    if (slot.pixmap != 0U) XFreePixmap(display_, slot.pixmap);
    if (slot.segment != nullptr) XShmDetach(display_, static_cast<XShmSegmentInfo *>(slot.segment));
  }
  if (slot.segment != nullptr) {
    XShmSegmentInfo *segment = static_cast<XShmSegmentInfo *>(slot.segment);
    shmdt((*segment).shmaddr);
    delete segment;
  }
  slot = PixmapSlot{};
}

void X11PresentPath::free_slots() noexcept {
  holding_ = false;
  for (std::size_t index = 0U; index < count_; ++index) free_slot(slots_[index]);
  count_ = 0U;
}

presentation::DirectAcquire X11PresentPath::acquire(presentation::PresentExtent extent) {
  if (!usable()) return presentation::DirectAcquire::refused(presentation::DirectRefusal::unsupported);
  // A 32-bit window presenting opaque has its alpha set to 255 by the
  // copy path's conversion; the raster's own alpha would show here.
  if (depth_ == 32 && !present_alpha)
    return presentation::DirectAcquire::refused(presentation::DirectRefusal::unsupported);
  if (holding_) return presentation::DirectAcquire::refused(presentation::DirectRefusal::busy);
  if (extent != extent_) {
    free_slots();
    extent_ = extent;
  }
  std::size_t chosen = count_;
  for (std::size_t index = 0U; index < count_; ++index) {
    if (slots_[index].reading != 0U) continue;
    chosen = index;
    break;
  }
  if (chosen == count_) {
    if (count_ == maximum_slots) return presentation::DirectAcquire::refused(presentation::DirectRefusal::busy);
    if (!make_slot(slots_[count_], extent))
      return presentation::DirectAcquire::refused(presentation::DirectRefusal::resources);
    ++count_;
  }
  held_ = chosen;
  holding_ = true;
  const PixmapSlot &slot = slots_[chosen];
  presentation::DirectBuffer buffer{};
  buffer.pixels = slot.pixels;
  buffer.row_bytes = slot.row_bytes;
  buffer.extent = extent;
  buffer.age = slot.shown_at == 0U ? 0U : static_cast<std::uint32_t>(presents_ - slot.shown_at + 1U);
  return presentation::DirectAcquire::granted(buffer);
}

presentation::PresentOutcome X11PresentPath::present(std::span<const presentation::PresentRect> damage) {
  if (!holding_) return presentation::PresentOutcome::failed;
  holding_ = false;
  PixmapSlot &slot = slots_[held_];
  // The update region: the damage, which the shim clipped to the extent.
  rectangles_.clear();
  for (std::size_t index = 0U; index < damage.size(); ++index) {
    const presentation::PresentRect rect = damage[index];
    if (rect.empty()) continue;
    XRectangle native{};
    native.x = static_cast<short>(rect.left);
    native.y = static_cast<short>(rect.top);
    native.width = static_cast<unsigned short>(rect.right - rect.left);
    native.height = static_cast<unsigned short>(rect.bottom - rect.top);
    rectangles_.push_back(native);
  }
  // Serial 0 means "not reading" in a slot: skip it.
  ++serial_;
  if (serial_ == 0U) ++serial_;
  // The present's requests run inside a scope that absorbs their errors
  // (a pixmap or window the server rejects, Present withdrawn) and waits
  // for the server's answer: a rejected present is lost, and the shim
  // presents the frame on the copy path. Other X errors are not absorbed.
  bool rejected = false;
  {
    ForeignWindowScope scope(display_, window_, std::span<const int>(opcodes_));
    XserverRegion update =
        XFixesCreateRegion(display_, rectangles_.data(), static_cast<int>(rectangles_.size()));
    // As soon as possible (target MSC 0), no fences, no notifies.
    XPresentPixmap(display_, window_, slot.pixmap, serial_, None, update, 0, 0, None, None, None,
                   PresentOptionNone, 0U, 0U, 0U, nullptr, 0);
    XFixesDestroyRegion(display_, update);
    rejected = scope.synchronize();
  }
  if (rejected) return presentation::PresentOutcome::lost;
  slot.reading = serial_;
  ++presents_;
  slot.shown_at = presents_;
  return presentation::PresentOutcome::presented;
}

void X11PresentPath::release() noexcept { free_slots(); }

void X11PresentPath::idle(Pixmap pixmap, std::uint32_t serial) noexcept {
  for (std::size_t index = 0U; index < count_; ++index) {
    PixmapSlot &slot = slots_[index];
    if (slot.pixmap != pixmap) continue;
    // An older present's notice for a pixmap presented again is not this
    // one's.
    if (slot.reading == serial) slot.reading = 0U;
    return;
  }
}

#else

int x11_present_opcode(Display *) noexcept { return -1; }

bool read_x11_present_event(Display *, int, XEvent &, X11PresentEvent &) { return false; }

bool x11_raster_layout(Display *, Visual *, int) noexcept { return false; }

int x11_extension_opcode(Display *, const char *) noexcept { return -1; }

X11PresentPath::X11PresentPath(Display *display, ::Window window, Visual *visual, int depth) noexcept
    : display_(display), window_(window), visual_(visual), depth_(depth) {}

X11PresentPath::~X11PresentPath() {}

bool X11PresentPath::usable() { return false; }

bool X11PresentPath::make_slot(PixmapSlot &, presentation::PresentExtent) { return false; }

void X11PresentPath::free_slot(PixmapSlot &slot) noexcept { slot = PixmapSlot{}; }

void X11PresentPath::free_slots() noexcept {}

presentation::DirectAcquire X11PresentPath::acquire(presentation::PresentExtent) {
  return presentation::DirectAcquire::refused(presentation::DirectRefusal::unsupported);
}

presentation::PresentOutcome X11PresentPath::present(std::span<const presentation::PresentRect>) {
  return presentation::PresentOutcome::failed;
}

void X11PresentPath::release() noexcept {}

void X11PresentPath::idle(Pixmap, std::uint32_t) noexcept {}

#endif

} // namespace gui_forms::host::linux_detail
