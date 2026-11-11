#include "x11_shm_present.hpp"

#if defined(GUI_FORMS_HAVE_XSHM)
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#include <cstddef>

namespace gui_forms::host::linux_detail {

#if defined(GUI_FORMS_HAVE_XSHM)
namespace {

// Set by attach_error while an attach is being checked: the server refused
// it (a remote display answers BadAccess). UI thread only.
thread_local bool attach_failed = false;

int attach_error(Display *, XErrorEvent *) {
  attach_failed = true;
  return 0;
}

// The longest frame the shared image takes: the presentation limit the
// XPutImage path states (64 MiB).
constexpr std::size_t maximum_shared_bytes = 64U * 1024U * 1024U;

} // namespace

X11SharedImage::~X11SharedImage() { release(); }

bool X11SharedImage::prepare(Display *display, Visual *visual, int depth, unsigned width, unsigned height) {
  if (unavailable)
    return false;
  if (image_ != nullptr && display == display_ && visual == visual_ && depth == depth_ && width == width_ &&
      height == height_)
    return true;
  release();
  if (XShmQueryExtension(display) == False) {
    unavailable = true;
    return false;
  }
  if (segment_ == nullptr)
    segment_ = new XShmSegmentInfo{};
  XShmSegmentInfo &segment = *static_cast<XShmSegmentInfo *>(segment_);
  segment = XShmSegmentInfo{};
  segment.shmid = -1;
  XImage *made = XShmCreateImage(display, visual, static_cast<unsigned>(depth), ZPixmap, nullptr, &segment,
                                 width, height);
  if (made == nullptr) {
    unavailable = true;
    return false;
  }
  const std::size_t bytes =
      static_cast<std::size_t>((*made).bytes_per_line) * static_cast<std::size_t>((*made).height);
  if ((*made).bytes_per_line <= 0 || bytes == 0U || bytes > maximum_shared_bytes) {
    XDestroyImage(made);
    unavailable = true;
    return false;
  }
  segment.shmid = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
  if (segment.shmid < 0) {
    XDestroyImage(made);
    unavailable = true;
    return false;
  }
  void *address = shmat(segment.shmid, nullptr, 0);
  if (address == reinterpret_cast<void *>(-1)) {
    shmctl(segment.shmid, IPC_RMID, nullptr);
    XDestroyImage(made);
    unavailable = true;
    return false;
  }
  segment.shmaddr = static_cast<char *>(address);
  segment.readOnly = False;
  (*made).data = segment.shmaddr;
  // The attach is checked synchronously: a refusal arrives as an error.
  XSync(display, False);
  attach_failed = false;
  const XErrorHandler previous = XSetErrorHandler(attach_error);
  const Bool attached = XShmAttach(display, &segment);
  XSync(display, False);
  XSetErrorHandler(previous);
  // Marked for removal now: the segment goes when both sides detach, even
  // if the process ends without release().
  shmctl(segment.shmid, IPC_RMID, nullptr);
  if (attached == False || attach_failed) {
    shmdt(segment.shmaddr);
    (*made).data = nullptr;
    XDestroyImage(made);
    segment = XShmSegmentInfo{};
    unavailable = true;
    return false;
  }
  display_ = display;
  image_ = made;
  visual_ = visual;
  depth_ = depth;
  width_ = width;
  height_ = height;
  return true;
}

void X11SharedImage::put(Drawable drawable, GC gc, int left, int top, unsigned columns, unsigned rows) {
  if (image_ == nullptr)
    return;
  XShmPutImage(display_, drawable, gc, image_, left, top, left, top, columns, rows, False);
}

void X11SharedImage::finish() {
  if (display_ != nullptr)
    XSync(display_, False);
}

void X11SharedImage::release() noexcept {
  if (image_ != nullptr && segment_ != nullptr) {
    XShmSegmentInfo &segment = *static_cast<XShmSegmentInfo *>(segment_);
    XShmDetach(display_, &segment);
    XSync(display_, False);
    shmdt(segment.shmaddr);
    (*image_).data = nullptr;
    XDestroyImage(image_);
    segment = XShmSegmentInfo{};
  }
  image_ = nullptr;
  if (segment_ != nullptr) {
    delete static_cast<XShmSegmentInfo *>(segment_);
    segment_ = nullptr;
  }
  display_ = nullptr;
  width_ = 0U;
  height_ = 0U;
}

#else

X11SharedImage::~X11SharedImage() {}

bool X11SharedImage::prepare(Display *, Visual *, int, unsigned, unsigned) {
  unavailable = true;
  return false;
}

void X11SharedImage::put(Drawable, GC, int, int, unsigned, unsigned) {}

void X11SharedImage::finish() {}

void X11SharedImage::release() noexcept {}

#endif

} // namespace gui_forms::host::linux_detail
