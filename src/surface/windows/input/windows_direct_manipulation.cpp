#include "windows_direct_manipulation.hpp"

#include "../../../core/failure/fail_fast.hpp"

#include <directmanipulation.h>

#include <atomic>

namespace gui_forms::host::windows_detail {

namespace {

// POINTER_INPUT_TYPE's PT_TOUCHPAD (Windows 10 SDKs name it; older ones
// do not).
constexpr POINTER_INPUT_TYPE touchpad_pointer = static_cast<POINTER_INPUT_TYPE>(5);

// What the viewport configuration lets DirectManipulation recognize: the
// interaction itself, both pans with their inertia (delivered as precise
// wheel events, since the contact no longer becomes WM_MOUSEWHEEL), and the
// pinch's scale (no scale inertia: a pinch ends with the fingers).
constexpr DIRECTMANIPULATION_CONFIGURATION viewport_configuration =
    static_cast<DIRECTMANIPULATION_CONFIGURATION>(
        DIRECTMANIPULATION_CONFIGURATION_INTERACTION | DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_X |
        DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_Y | DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_INERTIA |
        DIRECTMANIPULATION_CONFIGURATION_SCALING);

ManipulationStatus status_for(DIRECTMANIPULATION_STATUS status) noexcept {
  switch (status) {
  case DIRECTMANIPULATION_READY:
    return ManipulationStatus::ready;
  case DIRECTMANIPULATION_RUNNING:
    return ManipulationStatus::running;
  case DIRECTMANIPULATION_INERTIA:
    return ManipulationStatus::inertia;
  default:
    return ManipulationStatus::other;
  }
}

template <typename Interface> void release(Interface *&pointer) noexcept {
  if (pointer != nullptr)
    (*pointer).Release();
  pointer = nullptr;
}

// The handler's reference count is a plain atomic, never a lock.
static_assert(std::atomic<ULONG>::is_always_lock_free);

} // namespace

// DirectManipulation's viewport event handler: forwards each report to the
// window's DirectManipulationInput while it is attached.
class DirectManipulationHandler final : public IDirectManipulationViewportEventHandler {
public:
  explicit DirectManipulationHandler(DirectManipulationInput &owner) noexcept : owner_(&owner) {}
  DirectManipulationHandler(const DirectManipulationHandler &) = delete;
  DirectManipulationHandler &operator=(const DirectManipulationHandler &) = delete;

  void forget_owner() noexcept { owner_ = nullptr; }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  HRESULT STDMETHODCALLTYPE OnViewportStatusChanged(IDirectManipulationViewport *viewport,
                                                    DIRECTMANIPULATION_STATUS current,
                                                    DIRECTMANIPULATION_STATUS previous) override;
  HRESULT STDMETHODCALLTYPE OnViewportUpdated(IDirectManipulationViewport *viewport) noexcept override;
  HRESULT STDMETHODCALLTYPE OnContentUpdated(IDirectManipulationViewport *viewport,
                                             IDirectManipulationContent *content) override;

private:
  ~DirectManipulationHandler() = default;

  HRESULT answer_QueryInterface(REFIID iid, void **out);
  ULONG answer_AddRef();
  ULONG answer_Release();
  HRESULT answer_status(DIRECTMANIPULATION_STATUS current);
  HRESULT answer_content(IDirectManipulationContent *content);

  std::atomic<ULONG> references_{1U};
  DirectManipulationInput *owner_{};
};

HRESULT DirectManipulationHandler::answer_QueryInterface(REFIID iid, void **out) {
  if (out == nullptr)
    return E_POINTER;
  *out = nullptr;
  const bool known = iid == __uuidof(IUnknown) || iid == __uuidof(IDirectManipulationViewportEventHandler);
  if (!known)
    return E_NOINTERFACE;
  *out = static_cast<IDirectManipulationViewportEventHandler *>(this);
  AddRef();
  return S_OK;
}
HRESULT STDMETHODCALLTYPE DirectManipulationHandler::QueryInterface(REFIID iid, void **out) {
  return gui_forms::detail::call_failing_fast("windows.DirectManipulationHandler.QueryInterface",
                                              &DirectManipulationHandler::answer_QueryInterface, *this, iid, out);
}

ULONG DirectManipulationHandler::answer_AddRef() {
  const ULONG count = ++references_;
  return count;
}
ULONG STDMETHODCALLTYPE DirectManipulationHandler::AddRef() {
  return gui_forms::detail::call_failing_fast("windows.DirectManipulationHandler.AddRef",
                                              &DirectManipulationHandler::answer_AddRef, *this);
}

ULONG DirectManipulationHandler::answer_Release() {
  const ULONG count = --references_;
  if (count == 0U)
    delete this;
  return count;
}
ULONG STDMETHODCALLTYPE DirectManipulationHandler::Release() {
  return gui_forms::detail::call_failing_fast("windows.DirectManipulationHandler.Release",
                                              &DirectManipulationHandler::answer_Release, *this);
}

HRESULT DirectManipulationHandler::answer_status(DIRECTMANIPULATION_STATUS current) {
  if (owner_ != nullptr)
    (*owner_).report_status(status_for(current));
  return S_OK;
}
HRESULT STDMETHODCALLTYPE DirectManipulationHandler::OnViewportStatusChanged(IDirectManipulationViewport *viewport,
                                                                             DIRECTMANIPULATION_STATUS current,
                                                                             DIRECTMANIPULATION_STATUS previous) {
  static_cast<void>(viewport);
  static_cast<void>(previous);
  return gui_forms::detail::call_failing_fast("windows.DirectManipulationHandler.OnViewportStatusChanged",
                                              &DirectManipulationHandler::answer_status, *this, current);
}

HRESULT STDMETHODCALLTYPE DirectManipulationHandler::OnViewportUpdated(IDirectManipulationViewport *viewport) noexcept {
  // Nothing: each content update carries the transform. No library code
  // runs here (noexcept, so nothing to route through call_failing_fast).
  static_cast<void>(viewport);
  return S_OK;
}

HRESULT DirectManipulationHandler::answer_content(IDirectManipulationContent *content) {
  if (owner_ == nullptr || content == nullptr)
    return S_OK;
  float matrix[6] = {1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
  if (FAILED((*content).GetContentTransform(matrix, 6)))
    return S_OK;
  // [m11 m12 m21 m22 m31 m32]: DirectManipulation scales uniformly, so m11
  // is the scale and m31, m32 the offset.
  (*owner_).report_transform(static_cast<double>(matrix[0]),
                             Point{static_cast<double>(matrix[4]), static_cast<double>(matrix[5])});
  return S_OK;
}
HRESULT STDMETHODCALLTYPE DirectManipulationHandler::OnContentUpdated(IDirectManipulationViewport *viewport,
                                                                      IDirectManipulationContent *content) {
  static_cast<void>(viewport);
  return gui_forms::detail::call_failing_fast("windows.DirectManipulationHandler.OnContentUpdated",
                                              &DirectManipulationHandler::answer_content, *this, content);
}

// The COM objects one window holds, released in reverse order.
struct DirectManipulationObjects final {
  IDirectManipulationManager *manager{};
  IDirectManipulationUpdateManager *updates{};
  IDirectManipulationViewport *viewport{};
  DirectManipulationHandler *handler{};
  DWORD handler_cookie{};
  bool handler_added{};
  bool activated{};
  int width{1};
  int height{1};
};

DirectManipulationInput::DirectManipulationInput() noexcept = default;

DirectManipulationInput::~DirectManipulationInput() { detach(); }

bool DirectManipulationInput::attach(HWND window, Sink &sink) noexcept {
  detach();
  if (window == nullptr)
    return false;
  std::unique_ptr<DirectManipulationObjects> objects = std::make_unique<DirectManipulationObjects>();
  DirectManipulationObjects &held = *objects;
  if (FAILED(CoCreateInstance(__uuidof(DirectManipulationManager), nullptr, CLSCTX_INPROC_SERVER,
                              __uuidof(IDirectManipulationManager), reinterpret_cast<void **>(&held.manager))))
    return false;
  window_ = window;
  sink_ = &sink;
  objects_ = std::move(objects);
  bool ready = SUCCEEDED((*held.manager).GetUpdateManager(__uuidof(IDirectManipulationUpdateManager),
                                                         reinterpret_cast<void **>(&held.updates)));
  ready = ready && SUCCEEDED((*held.manager).CreateViewport(nullptr, window, __uuidof(IDirectManipulationViewport),
                                                            reinterpret_cast<void **>(&held.viewport)));
  ready = ready && SUCCEEDED((*held.viewport).ActivateConfiguration(viewport_configuration));
  ready = ready && SUCCEEDED((*held.viewport).SetViewportOptions(DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE));
  if (ready) {
    held.handler = new DirectManipulationHandler(*this);
    held.handler_added =
        SUCCEEDED((*held.viewport).AddEventHandler(window, held.handler, &held.handler_cookie));
    ready = held.handler_added;
  }
  if (ready) {
    RECT client{};
    GetClientRect(window, &client);
    resize(client.right - client.left, client.bottom - client.top);
    held.activated = SUCCEEDED((*held.manager).Activate(window));
    ready = held.activated && SUCCEEDED((*held.viewport).Enable());
  }
  if (!ready) {
    detach();
    return false;
  }
  return true;
}

void DirectManipulationInput::detach() noexcept {
  if (!objects_) {
    window_ = nullptr;
    sink_ = nullptr;
    return;
  }
  DirectManipulationObjects &held = *objects_;
  if (held.handler != nullptr)
    (*held.handler).forget_owner();
  if (held.viewport != nullptr) {
    static_cast<void>((*held.viewport).Stop());
    if (held.handler_added)
      static_cast<void>((*held.viewport).RemoveEventHandler(held.handler_cookie));
    static_cast<void>((*held.viewport).Abandon());
  }
  if (held.activated && held.manager != nullptr)
    static_cast<void>((*held.manager).Deactivate(window_));
  release(held.handler);
  release(held.viewport);
  release(held.updates);
  release(held.manager);
  objects_.reset();
  window_ = nullptr;
  sink_ = nullptr;
}

bool DirectManipulationInput::attached() const noexcept { return static_cast<bool>(objects_); }

void DirectManipulationInput::pointer_hit_test(WPARAM wparam) noexcept {
  if (!objects_ || (*objects_).viewport == nullptr)
    return;
  const UINT32 pointer = GET_POINTERID_WPARAM(wparam);
  POINTER_INPUT_TYPE type{};
  if (GetPointerType(pointer, &type) != FALSE && type == touchpad_pointer)
    static_cast<void>((*(*objects_).viewport).SetContact(pointer));
}

void DirectManipulationInput::pump() noexcept {
  if (objects_ && (*objects_).updates != nullptr)
    static_cast<void>((*(*objects_).updates).Update(nullptr));
}

void DirectManipulationInput::reset_content() noexcept {
  if (!objects_ || (*objects_).viewport == nullptr)
    return;
  const DirectManipulationObjects &held = *objects_;
  static_cast<void>((*held.viewport).ZoomToRect(0.0F, 0.0F, static_cast<float>(held.width),
                                                static_cast<float>(held.height), FALSE));
}

void DirectManipulationInput::resize(int width, int height) noexcept {
  if (!objects_ || (*objects_).viewport == nullptr)
    return;
  DirectManipulationObjects &held = *objects_;
  held.width = width > 0 ? width : 1;
  held.height = height > 0 ? height : 1;
  const RECT rectangle{0, 0, held.width, held.height};
  static_cast<void>((*held.viewport).SetViewportRect(&rectangle));
}

void DirectManipulationInput::report_status(ManipulationStatus status) {
  if (sink_ != nullptr)
    (*sink_).manipulation_status(status);
}

void DirectManipulationInput::report_transform(double scale, Point device_offset) {
  if (sink_ != nullptr)
    (*sink_).manipulation_transform(scale, device_offset);
}

} // namespace gui_forms::host::windows_detail
