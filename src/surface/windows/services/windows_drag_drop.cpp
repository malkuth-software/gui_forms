#include "windows_drag_drop.hpp"
#include "../../../core/failure/fail_fast.hpp"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace gui_forms::host::detail {
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
namespace {

// The OLE effect bits are the portable ones; the conversions below rely on it.
static_assert(DROPEFFECT_COPY == static_cast<DWORD>(DragEffect::copy));
static_assert(DROPEFFECT_MOVE == static_cast<DWORD>(DragEffect::move));
static_assert(DROPEFFECT_LINK == static_cast<DWORD>(DragEffect::link));

constexpr DWORD portable_effect_bits = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;

// UTF-8 from UTF-16; empty when the text does not convert (an unpaired
// surrogate) or is empty.
std::string utf8_text(std::wstring_view text) {
    if (text.empty() || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                            result.data(), count, nullptr, nullptr);
    if (written != count) return {};
    return result;
}

// UTF-16 from UTF-8; empty when the text is not UTF-8 or is empty.
std::wstring wide_text(std::string_view text) {
    if (text.empty() || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                            static_cast<int>(text.size()), result.data(), count);
    if (written != count) return {};
    return result;
}

FORMATETC global_format(CLIPFORMAT format) noexcept {
    FORMATETC result{};
    result.cfFormat = format;
    result.ptd = nullptr;
    result.dwAspect = DVASPECT_CONTENT;
    result.lindex = -1;
    result.tymed = TYMED_HGLOBAL;
    return result;
}

CLIPFORMAT png_format() noexcept {
    const UINT registered = RegisterClipboardFormatW(L"PNG");
    return static_cast<CLIPFORMAT>(registered);
}

// The clipboard format a media type names: "image/png" is the format other
// programs register for PNG; any other media type registers its own name.
// Zero when it cannot be registered.
CLIPFORMAT media_format(std::string_view media_type) {
    if (media_type == "image/png") return png_format();
    const std::wstring name = wide_text(media_type);
    if (name.empty()) return 0;
    const UINT registered = RegisterClipboardFormatW(name.c_str());
    return static_cast<CLIPFORMAT>(registered);
}

// Owns one medium a GetData filled, released on every exit.
struct MediumScope final {
    STGMEDIUM medium{};
    MediumScope() = default;
    ~MediumScope() { ReleaseStgMedium(&medium); }
    MediumScope(const MediumScope&) = delete;
    MediumScope& operator=(const MediumScope&) = delete;
};

// Holds a global memory block locked for reading, unlocked on every exit.
class GlobalReadScope final {
public:
    explicit GlobalReadScope(HGLOBAL handle) noexcept
        : handle_(handle), bytes_(handle == nullptr ? nullptr : GlobalLock(handle)),
          size_(handle == nullptr ? 0U : GlobalSize(handle)) {}
    ~GlobalReadScope() {
        if (bytes_ != nullptr) GlobalUnlock(handle_);
    }
    GlobalReadScope(const GlobalReadScope&) = delete;
    GlobalReadScope& operator=(const GlobalReadScope&) = delete;
    [[nodiscard]] const void* bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_ == nullptr ? 0U : size_; }

private:
    HGLOBAL handle_{};
    void* bytes_{};
    SIZE_T size_{};
};

// Holds OleInitialize for one scope on the calling thread.
class OleScope final {
public:
    OleScope() noexcept : initialized_(SUCCEEDED(OleInitialize(nullptr))) {}
    ~OleScope() {
        if (initialized_) OleUninitialize();
    }
    OleScope(const OleScope&) = delete;
    OleScope& operator=(const OleScope&) = delete;
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    bool initialized_{};
};

// Releases one COM reference on every exit.
class ReleaseScope final {
public:
    explicit ReleaseScope(IUnknown* object) noexcept : object_(object) {}
    ~ReleaseScope() {
        if (object_ != nullptr) (*object_).Release();
    }
    ReleaseScope(const ReleaseScope&) = delete;
    ReleaseScope& operator=(const ReleaseScope&) = delete;

private:
    IUnknown* object_{};
};

bool add_size(std::size_t& total, std::size_t value) noexcept {
    if (value > DragLimits::maximum_total_bytes - total) return false;
    total += value;
    return true;
}

// The file list of CF_HDROP, empty when absent, over a bound, or when a name
// does not convert.
DragFileListData read_files(IDataObject& data, std::size_t& total) {
    DragFileListData files;
    FORMATETC format = global_format(CF_HDROP);
    MediumScope scope;
    if (FAILED(data.GetData(&format, &scope.medium)) || scope.medium.tymed != TYMED_HGLOBAL) return files;
    const HDROP drop = static_cast<HDROP>(scope.medium.hGlobal);
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
    if (count == 0 || count > DragLimits::maximum_paths) return files;
    std::size_t used = total;
    std::vector<std::string> paths;
    paths.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        const UINT length = DragQueryFileW(drop, index, nullptr, 0);
        if (length == 0 || length > DragLimits::maximum_path_bytes) return files;
        std::wstring path(static_cast<std::size_t>(length) + 1U, L'\0');
        if (DragQueryFileW(drop, index, path.data(), length + 1U) != length) return files;
        path.resize(length);
        std::string converted = utf8_text(path);
        if (converted.empty() || converted.size() > DragLimits::maximum_path_bytes ||
            !add_size(used, converted.size())) {
            return files;
        }
        paths.push_back(std::move(converted));
    }
    total = used;
    files.paths_utf8 = std::move(paths);
    return files;
}

// The text of CF_UNICODETEXT up to its terminator; empty when absent, over
// its bound or not convertible.
std::string read_text(IDataObject& data, std::size_t& total) {
    FORMATETC format = global_format(CF_UNICODETEXT);
    MediumScope scope;
    if (FAILED(data.GetData(&format, &scope.medium)) || scope.medium.tymed != TYMED_HGLOBAL) return {};
    const GlobalReadScope block(scope.medium.hGlobal);
    if (block.bytes() == nullptr) return {};
    // A UTF-8 byte count is at most three per UTF-16 unit: reading more
    // units than this can never fit the text bound.
    const std::size_t units = std::min(block.size() / sizeof(wchar_t), DragLimits::maximum_text_bytes);
    const wchar_t* const characters = static_cast<const wchar_t*>(block.bytes());
    std::size_t length = 0;
    while (length < units && characters[length] != L'\0') ++length;
    std::string text = utf8_text(std::wstring_view(characters, length));
    if (text.empty() || text.size() > DragLimits::maximum_text_bytes || !add_size(total, text.size())) return {};
    return text;
}

// The bytes of the registered PNG format; empty when absent or over the
// total.
std::vector<std::uint8_t> read_png(IDataObject& data, std::size_t& total) {
    const CLIPFORMAT png = png_format();
    if (png == 0) return {};
    FORMATETC format = global_format(png);
    MediumScope scope;
    if (FAILED(data.GetData(&format, &scope.medium)) || scope.medium.tymed != TYMED_HGLOBAL) return {};
    const GlobalReadScope block(scope.medium.hGlobal);
    if (block.bytes() == nullptr || block.size() == 0U || !add_size(total, block.size())) return {};
    const std::uint8_t* const begin = static_cast<const std::uint8_t*>(block.bytes());
    std::vector<std::uint8_t> bytes(begin, begin + block.size());
    return bytes;
}

// One format a drag from the program offers, and its bytes as GetData hands
// them over (a fresh global block each time; the receiver owns it).
struct OfferedFormat final {
    CLIPFORMAT format{};
    std::vector<std::byte> bytes;
};

// CF_HDROP's block: a DROPFILES header, then each path in UTF-16 with its
// terminator, then one more terminator.
std::vector<std::byte> file_list_block(const DragFileListData& files) {
    std::wstring list;
    for (const std::string& path : files.paths_utf8) {
        const std::wstring wide = wide_text(path);
        if (wide.empty()) return {};
        list += wide;
        list += L'\0';
    }
    list += L'\0';
    DROPFILES header{};
    header.pFiles = sizeof(DROPFILES);
    header.fNC = FALSE;
    header.fWide = TRUE;
    const std::size_t list_bytes = list.size() * sizeof(wchar_t);
    std::vector<std::byte> block(sizeof(DROPFILES) + list_bytes);
    std::memcpy(block.data(), &header, sizeof(DROPFILES));
    std::memcpy(block.data() + sizeof(DROPFILES), list.data(), list_bytes);
    return block;
}

// CF_UNICODETEXT's block: the text in UTF-16 with its terminator.
std::vector<std::byte> text_block(const DragTextData& text) {
    std::wstring wide = wide_text(text.text_utf8);
    if (wide.empty() && !text.text_utf8.empty()) return {};
    wide += L'\0';
    const std::size_t bytes = wide.size() * sizeof(wchar_t);
    std::vector<std::byte> block(bytes);
    std::memcpy(block.data(), wide.data(), bytes);
    return block;
}

bool offers(const std::vector<OfferedFormat>& formats, CLIPFORMAT format) noexcept {
    for (const OfferedFormat& offered : formats) {
        if (offered.format == format) return true;
    }
    return false;
}

bool matches(const FORMATETC& requested, const OfferedFormat& offered) noexcept {
    return requested.cfFormat == offered.format && (requested.tymed & TYMED_HGLOBAL) != 0 &&
           requested.dwAspect == DVASPECT_CONTENT;
}

// The data of a drag from the program: fixed formats, read by any number of
// destinations, never changed (SetData is refused).
class WindowsDragData final : public IDataObject {
public:
    explicit WindowsDragData(std::vector<OfferedFormat> formats) noexcept : formats_(std::move(formats)) {}
    WindowsDragData(const WindowsDragData&) = delete;
    WindowsDragData& operator=(const WindowsDragData&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        return gui_forms::detail::call_failing_fast("windows.WindowsDragData.QueryInterface",
                                                    &WindowsDragData::answer_QueryInterface, *this, iid, out);
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override {
        const ULONG count = ++references_;
        return count;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = --references_;
        if (count == 0U) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        return gui_forms::detail::call_failing_fast("windows.WindowsDragData.GetData", &WindowsDragData::answer_GetData,
                                                    *this, format, medium);
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) noexcept override { return DATA_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        return gui_forms::detail::call_failing_fast("windows.WindowsDragData.QueryGetData",
                                                    &WindowsDragData::answer_QueryGetData, *this, format);
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) noexcept override {
        if (out != nullptr) (*out).ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) override {
        return gui_forms::detail::call_failing_fast("windows.WindowsDragData.EnumFormatEtc",
                                                    &WindowsDragData::answer_EnumFormatEtc, *this, direction, out);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) noexcept override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) noexcept override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) noexcept override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    ~WindowsDragData() = default;

    HRESULT answer_QueryInterface(REFIID iid, void** out) {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDataObject) {
            *out = static_cast<IDataObject*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    HRESULT answer_GetData(FORMATETC* format, STGMEDIUM* medium) {
        if (format == nullptr || medium == nullptr) return E_INVALIDARG;
        for (const OfferedFormat& offered : formats_) {
            if (!matches(*format, offered)) continue;
            // GlobalAlloc of zero bytes is a discarded block; a block holds
            // at least one byte.
            const SIZE_T size = std::max<SIZE_T>(offered.bytes.size(), 1U);
            const HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
            if (block == nullptr) return E_OUTOFMEMORY;
            void* const destination = GlobalLock(block);
            if (destination == nullptr) {
                GlobalFree(block);
                return E_OUTOFMEMORY;
            }
            if (!offered.bytes.empty()) std::memcpy(destination, offered.bytes.data(), offered.bytes.size());
            GlobalUnlock(block);
            (*medium).tymed = TYMED_HGLOBAL;
            (*medium).hGlobal = block;
            (*medium).pUnkForRelease = nullptr;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT answer_QueryGetData(FORMATETC* format) {
        if (format == nullptr) return E_INVALIDARG;
        for (const OfferedFormat& offered : formats_) {
            if (matches(*format, offered)) return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT answer_EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (direction != DATADIR_GET) return E_NOTIMPL;
        std::vector<FORMATETC> listed;
        listed.reserve(formats_.size());
        for (const OfferedFormat& offered : formats_) listed.push_back(global_format(offered.format));
        const HRESULT created = SHCreateStdEnumFmtEtc(static_cast<UINT>(listed.size()), listed.data(), out);
        return created;
    }

    std::atomic<ULONG> references_{1U};
    const std::vector<OfferedFormat> formats_;
};

// The drop source of a drag from the program: it drops when every button
// held at the start is released, cancels on Escape or once the source
// window is destroyed (`source_closed`, which DoDragDrop's loop can set by
// dispatching a handler that destroys it), and leaves the cursor to OLE's
// default effect cursors.
class WindowsDropSource final : public IDropSource {
public:
    WindowsDropSource(DWORD buttons, const bool& source_closed) noexcept
        : buttons_(buttons), source_closed_(source_closed) {}
    WindowsDropSource(const WindowsDropSource&) = delete;
    WindowsDropSource& operator=(const WindowsDropSource&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDropSource) {
            *out = static_cast<IDropSource*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override {
        const ULONG count = ++references_;
        return count;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = --references_;
        if (count == 0U) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape_pressed, DWORD key_state) noexcept override {
        if (escape_pressed != FALSE || source_closed_) return DRAGDROP_S_CANCEL;
        if ((key_state & buttons_) == 0U) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) noexcept override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    ~WindowsDropSource() = default;

    std::atomic<ULONG> references_{1U};
    const DWORD buttons_;
    // Set when the source window is destroyed (observing; the window's
    // services own it and outlive the drag).
    const bool& source_closed_;
};

// GUI_FORMS_TRACE_DROP set: each call OLE makes on a window's drop target,
// and what the window answered, as one line on standard error. Read once.
bool drop_traced() noexcept {
    static const bool traced = std::getenv("GUI_FORMS_TRACE_DROP") != nullptr;
    return traced;
}

void trace_drop_call(const char* call, DWORD offered, DWORD key_state, POINTL point, std::uint64_t session,
                     DWORD answered) noexcept {
    if (!drop_traced()) return;
    std::fprintf(stderr, "gui_forms drop: %s offered=0x%lx keys=0x%lx screen=%ld,%ld session=%llu answered=0x%lx\n",
                 call, static_cast<unsigned long>(offered), static_cast<unsigned long>(key_state),
                 static_cast<long>(point.x), static_cast<long>(point.y), static_cast<unsigned long long>(session),
                 static_cast<unsigned long>(answered));
    std::fflush(stderr);
}

// The mouse buttons held now, as OLE key-state bits.
DWORD held_buttons() noexcept {
    DWORD buttons = 0;
    if ((GetKeyState(VK_LBUTTON) & 0x8000) != 0) buttons |= MK_LBUTTON;
    if ((GetKeyState(VK_RBUTTON) & 0x8000) != 0) buttons |= MK_RBUTTON;
    if ((GetKeyState(VK_MBUTTON) & 0x8000) != 0) buttons |= MK_MBUTTON;
    return buttons;
}

const char* drag_action_trace_name(DragAction action) noexcept {
    switch (action) {
    case DragAction::enter:
        return "enter";
    case DragAction::over:
        return "over";
    case DragAction::drop:
        return "drop";
    case DragAction::leave:
        return "leave";
    default:
        return "other";
    }
}

// One effect of `effects`, or none: OLE reports one, and a window that
// answers with several or outside the allowed set accepts nothing.
DragEffect single_effect(DragEffect effect, DragEffect allowed) noexcept {
    const bool single = effect == DragEffect::copy || effect == DragEffect::move || effect == DragEffect::link;
    if (!single || !has_drag_effect(allowed, effect)) return DragEffect::none;
    return effect;
}

} // namespace

// The window as a drop target. A session starts at DragEnter when the data
// offers something the program reads and the window is enabled (a window
// under a modal dialog is not), and ends at DragLeave or Drop; between, the
// items read at DragEnter travel with each event, as the X11 host's do.
// The shell's drag image helper, where it exists, draws the drag image over
// the window.
class WindowsDropTarget final : public IDropTarget {
public:
    WindowsDropTarget(HWND window, WindowsDragDispatch& dispatch) noexcept : window_(window), dispatch_(&dispatch) {
        void* helper = nullptr;
        const HRESULT created =
            CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_IDropTargetHelper, &helper);
        if (SUCCEEDED(created)) helper_ = static_cast<IDropTargetHelper*>(helper);
    }
    WindowsDropTarget(const WindowsDropTarget&) = delete;
    WindowsDropTarget& operator=(const WindowsDropTarget&) = delete;

    // Stops every later call from reaching the window.
    void detach() noexcept {
        dispatch_ = nullptr;
        session_id_ = 0;
        items_.clear();
        if (helper_ != nullptr) {
            (*helper_).Release();
            helper_ = nullptr;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override {
        const ULONG count = ++references_;
        return count;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = --references_;
        if (count == 0U) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD key_state, POINTL point, DWORD* effect) override {
        return gui_forms::detail::call_failing_fast("windows.WindowsDropTarget.DragEnter",
                                                    &WindowsDropTarget::answer_DragEnter, *this, data, key_state,
                                                    point, effect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD key_state, POINTL point, DWORD* effect) override {
        return gui_forms::detail::call_failing_fast("windows.WindowsDropTarget.DragOver",
                                                    &WindowsDropTarget::answer_DragOver, *this, key_state, point,
                                                    effect);
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        return gui_forms::detail::call_failing_fast("windows.WindowsDropTarget.DragLeave",
                                                    &WindowsDropTarget::answer_DragLeave, *this);
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD key_state, POINTL point, DWORD* effect) override {
        return gui_forms::detail::call_failing_fast("windows.WindowsDropTarget.Drop", &WindowsDropTarget::answer_Drop,
                                                    *this, data, key_state, point, effect);
    }

private:
    ~WindowsDropTarget() {
        if (helper_ != nullptr) (*helper_).Release();
    }

    HRESULT answer_DragEnter(IDataObject* data, DWORD key_state, POINTL point, DWORD* effect) {
        if (effect == nullptr) return E_INVALIDARG;
        end_session();
        const DWORD offered = *effect;
        *effect = DROPEFFECT_NONE;
        if (data != nullptr && dispatch_ != nullptr && IsWindowEnabled(window_) != FALSE) {
            items_ = windows_drag_items(*data);
            allowed_ = windows_drag_effects(offered);
            if (!items_.empty() && allowed_ != DragEffect::none) {
                ++sequence_;
                session_id_ = sequence_;
                accepted_ = send(DragAction::enter, key_state, point);
                *effect = windows_drop_effects(accepted_);
            } else {
                items_.clear();
            }
        }
        trace_drop_call("DragEnter", offered, key_state, point, session_id_, *effect);
        POINT screen{point.x, point.y};
        if (helper_ != nullptr && data != nullptr) static_cast<void>((*helper_).DragEnter(window_, data, &screen, *effect));
        return S_OK;
    }

    HRESULT answer_DragOver(DWORD key_state, POINTL point, DWORD* effect) {
        if (effect == nullptr) return E_INVALIDARG;
        const DWORD offered = *effect;
        *effect = DROPEFFECT_NONE;
        if (session_id_ != 0 && dispatch_ != nullptr) {
            // The source may narrow its effects as modifiers change.
            allowed_ = windows_drag_effects(offered);
            accepted_ = allowed_ == DragEffect::none ? DragEffect::none : send(DragAction::over, key_state, point);
            *effect = windows_drop_effects(accepted_);
        }
        trace_drop_call("DragOver", offered, key_state, point, session_id_, *effect);
        POINT screen{point.x, point.y};
        if (helper_ != nullptr) static_cast<void>((*helper_).DragOver(&screen, *effect));
        return S_OK;
    }

    HRESULT answer_DragLeave() {
        trace_drop_call("DragLeave", 0U, 0U, POINTL{}, session_id_, 0U);
        end_session();
        if (helper_ != nullptr) static_cast<void>((*helper_).DragLeave());
        return S_OK;
    }

    HRESULT answer_Drop(IDataObject* data, DWORD key_state, POINTL point, DWORD* effect) {
        if (effect == nullptr) return E_INVALIDARG;
        const DWORD offered = *effect;
        const std::uint64_t session = session_id_;
        *effect = DROPEFFECT_NONE;
        if (drop_traced() && (session_id_ == 0 || dispatch_ == nullptr || accepted_ == DragEffect::none)) {
            std::fprintf(stderr, "gui_forms drop: Drop not sent (session=%llu dispatch=%d accepted=0x%x)\n",
                         static_cast<unsigned long long>(session_id_), dispatch_ != nullptr ? 1 : 0,
                         static_cast<unsigned>(accepted_));
            std::fflush(stderr);
        }
        if (session_id_ != 0 && dispatch_ != nullptr && accepted_ != DragEffect::none) {
            const DragEffect performed = send(DragAction::drop, key_state, point);
            *effect = windows_drop_effects(performed);
        }
        trace_drop_call("Drop", offered, key_state, point, session, *effect);
        session_id_ = 0;
        items_.clear();
        accepted_ = DragEffect::none;
        POINT screen{point.x, point.y};
        if (helper_ != nullptr && data != nullptr) static_cast<void>((*helper_).Drop(data, &screen, *effect));
        return S_OK;
    }

    // Ends a running session with a leave, as an XdndLeave would.
    void end_session() {
        if (session_id_ != 0 && dispatch_ != nullptr) {
            DragEvent event;
            event.action = DragAction::leave;
            event.session_id = session_id_;
            event.allowed_effects = allowed_;
            POINT client{};
            static_cast<void>(GetCursorPos(&client));
            static_cast<void>(ScreenToClient(window_, &client));
            static_cast<void>((*dispatch_).dispatch_external_drag(std::move(event), client));
        }
        session_id_ = 0;
        items_.clear();
        accepted_ = DragEffect::none;
    }

    DragEffect send(DragAction action, DWORD key_state, POINTL point) {
        DragEvent event;
        event.action = action;
        event.session_id = session_id_;
        event.modifiers = windows_drag_modifiers(key_state);
        event.allowed_effects = allowed_;
        event.items = items_;
        POINT client{point.x, point.y};
        static_cast<void>(ScreenToClient(window_, &client));
        const DragEffect answered = (*dispatch_).dispatch_external_drag(std::move(event), client);
        const DragEffect accepted = single_effect(answered, allowed_);
        if (drop_traced()) {
            std::fprintf(stderr, "gui_forms drop:   %s client=%ld,%ld window answered 0x%x, accepted 0x%x\n",
                         drag_action_trace_name(action), static_cast<long>(client.x), static_cast<long>(client.y),
                         static_cast<unsigned>(answered), static_cast<unsigned>(accepted));
            std::fflush(stderr);
        }
        return accepted;
    }

    std::atomic<ULONG> references_{1U};
    HWND window_{};
    WindowsDragDispatch* dispatch_{};
    IDropTargetHelper* helper_{};
    std::uint64_t sequence_{};
    // Zero while no session runs.
    std::uint64_t session_id_{};
    std::vector<DragDataItem> items_;
    DragEffect allowed_{DragEffect::none};
    DragEffect accepted_{DragEffect::none};
};

IDropTarget* WindowsDropRegistration::registered_target() const noexcept {
    IDropTarget* const target = target_;
    return target;
}

DragEffect windows_drag_effects(DWORD effects) noexcept {
    const DragEffect result = static_cast<DragEffect>(static_cast<std::uint8_t>(effects & portable_effect_bits));
    return result;
}

DWORD windows_drop_effects(DragEffect effects) noexcept {
    const DWORD result = static_cast<DWORD>(static_cast<std::uint8_t>(effects)) & portable_effect_bits;
    return result;
}

Modifier windows_drag_modifiers(DWORD key_state) noexcept {
    Modifier result = Modifier::none;
    if ((key_state & MK_SHIFT) != 0U) result = result | Modifier::shift;
    if ((key_state & MK_CONTROL) != 0U) result = result | Modifier::control;
    if ((key_state & MK_ALT) != 0U) result = result | Modifier::alt;
    return result;
}

std::vector<DragDataItem> windows_drag_items(IDataObject& data) {
    try {
        std::vector<DragDataItem> items;
        std::size_t total = 0;
        DragFileListData files = read_files(data, total);
        if (!files.paths_utf8.empty()) items.emplace_back(std::move(files));
        std::string text = read_text(data, total);
        if (!text.empty()) items.emplace_back(DragTextData{std::move(text)});
        std::vector<std::uint8_t> png = read_png(data, total);
        if (!png.empty()) items.emplace_back(DragBinaryData{"image/png", std::move(png)});
        return items;
    } catch (const std::bad_alloc&) {
        // A drag too large to copy offers nothing (house style: a failed
        // content allocation is the operation's failure).
        return {};
    }
}

IDataObject* make_windows_drag_data(const std::vector<DragDataItem>& items) {
    std::vector<OfferedFormat> formats;
    for (const DragDataItem& item : items) {
        OfferedFormat offered;
        if (const DragFileListData* files = std::get_if<DragFileListData>(&item)) {
            offered.format = CF_HDROP;
            // Never empty when every path converts.
            offered.bytes = file_list_block(*files);
            if (offered.bytes.empty()) return nullptr;
        } else if (const DragTextData* text = std::get_if<DragTextData>(&item)) {
            offered.format = CF_UNICODETEXT;
            // Never empty when the text converts: it holds the terminator.
            offered.bytes = text_block(*text);
            if (offered.bytes.empty()) return nullptr;
        } else if (const DragBinaryData* binary = std::get_if<DragBinaryData>(&item)) {
            offered.format = media_format((*binary).media_type);
            if (offered.format == 0) return nullptr;
            offered.bytes.resize((*binary).bytes.size());
            if (!(*binary).bytes.empty()) {
                std::memcpy(offered.bytes.data(), (*binary).bytes.data(), (*binary).bytes.size());
            }
        } else {
            return nullptr;
        }
        if (!offers(formats, offered.format)) formats.push_back(std::move(offered));
    }
    WindowsDragData* const data = new (std::nothrow) WindowsDragData(std::move(formats));
    return data;
}

WindowsDropRegistration::~WindowsDropRegistration() { revoke(); }

bool WindowsDropRegistration::register_window(HWND window, WindowsDragDispatch& dispatch) {
    if (target_ != nullptr || window == nullptr) return false;
    const HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) return false;
    WindowsDropTarget* const target = new (std::nothrow) WindowsDropTarget(window, dispatch);
    if (target == nullptr) {
        OleUninitialize();
        return false;
    }
    const HRESULT registered = RegisterDragDrop(window, target);
    if (FAILED(registered)) {
        (*target).detach();
        (*target).Release();
        OleUninitialize();
        return false;
    }
    window_ = window;
    target_ = target;
    ole_initialized_ = true;
    return true;
}

void WindowsDropRegistration::revoke() noexcept {
    if (target_ == nullptr) return;
    // RevokeDragDrop releases OLE's reference; the target may still be held
    // by a drag in progress, so it is detached before this one is released.
    static_cast<void>(RevokeDragDrop(window_));
    (*target_).detach();
    (*target_).Release();
    target_ = nullptr;
    window_ = nullptr;
    if (ole_initialized_) {
        OleUninitialize();
        ole_initialized_ = false;
    }
}

IDropSource* make_windows_drop_source(DWORD buttons, const bool& source_closed) {
    IDropSource* const source = new (std::nothrow) WindowsDropSource(buttons, source_closed);
    return source;
}

WindowsDragOutcome run_windows_drag(const HostDragSourceRequest& request, const bool& source_closed) {
    WindowsDragOutcome outcome;
    const DWORD buttons = held_buttons();
    if (buttons == 0U) {
        outcome.status = {HostServiceError::backend_failure};
        return outcome;
    }
    const OleScope ole;
    if (!ole.initialized()) {
        outcome.status = {HostServiceError::backend_failure};
        return outcome;
    }
    IDataObject* const data = make_windows_drag_data(request.items);
    const ReleaseScope data_reference(data);
    IDropSource* const source = data == nullptr ? nullptr : make_windows_drop_source(buttons, source_closed);
    const ReleaseScope source_reference(source);
    if (data == nullptr || source == nullptr) {
        outcome.status = {HostServiceError::backend_failure};
        return outcome;
    }
    DWORD performed = DROPEFFECT_NONE;
    const HRESULT ran = DoDragDrop(data, source, windows_drop_effects(request.allowed_effects), &performed);
    if (ran == DRAGDROP_S_DROP) {
        outcome.performed_effect = single_effect(windows_drag_effects(performed), request.allowed_effects);
    } else if (ran == DRAGDROP_S_CANCEL) {
        outcome.performed_effect = DragEffect::none;
    } else {
        outcome.status = {HostServiceError::backend_failure};
    }
    return outcome;
}

#endif
} // namespace gui_forms::host::detail
