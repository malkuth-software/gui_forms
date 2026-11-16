#include "windows_clipboard_image.hpp"
#include "windows_clipboard_open.hpp"
#include "../../../core/host/image/clipboard_image_wire.hpp"
#include "../../../core/host/image/clipboard_dib.hpp"
#include <cstring>

namespace gui_forms::host::detail {
#if !defined(GUI_FORMS_HIGH_INTEGRITY)
// Not compiled in a high-integrity build (cmake/HighIntegrity.cmake).

namespace {
// The private format written: GFIM version 2, premultiplied BGRA8.
UINT raw_format() { return RegisterClipboardFormatW(L"GUI.Forms.BGRA8.v2"); }
// The private format written before SK2 (GFIM version 1, straight RGBA8),
// still read.
UINT version_one_format() { return RegisterClipboardFormatW(L"GUI.Forms.RGBA8.v1"); }
class ClipboardLease final {
public:
    explicit ClipboardLease(HWND owner) noexcept : opened_(open_clipboard_with_retry(owner)) {}
    ~ClipboardLease() { if (opened_) CloseClipboard(); }
    ClipboardLease(const ClipboardLease&) = delete;
    ClipboardLease& operator=(const ClipboardLease&) = delete;
    [[nodiscard]] bool opened() const noexcept { return opened_; }
private:
    bool opened_;
};
class GlobalStorage final {
public:
    explicit GlobalStorage(std::span<const std::byte> bytes)
        : storage_(GlobalAlloc(GMEM_MOVEABLE, bytes.size())) {
        if (storage_ == nullptr) return;
        void* target = GlobalLock(storage_);
        if (target == nullptr) { GlobalFree(storage_); storage_ = nullptr; return; }
        std::memcpy(target, bytes.data(), bytes.size());
        GlobalUnlock(storage_);
    }
    ~GlobalStorage() { if (storage_ != nullptr) GlobalFree(storage_); }
    GlobalStorage(const GlobalStorage&) = delete;
    GlobalStorage& operator=(const GlobalStorage&) = delete;
    [[nodiscard]] bool valid() const noexcept { return storage_ != nullptr; }
    [[nodiscard]] bool publish(UINT format) noexcept {
        if (SetClipboardData(format, storage_) == nullptr) return false;
        storage_ = nullptr; // Windows owns the allocation after successful publication.
        return true;
    }
private:
    HGLOBAL storage_;
};
class GlobalRead final {
public:
    explicit GlobalRead(HANDLE storage) noexcept : storage_(storage),
        data_(storage == nullptr ? nullptr : GlobalLock(storage)) {}
    ~GlobalRead() { if (data_ != nullptr) GlobalUnlock(storage_); }
    GlobalRead(const GlobalRead&) = delete;
    GlobalRead& operator=(const GlobalRead&) = delete;
    [[nodiscard]] const void* data() const noexcept { return data_; }
private:
    HANDLE storage_;
    const void* data_;
};
}
HostClipboardImageResult read_windows_clipboard_image(HWND owner) {
    const UINT exact = raw_format();
    const UINT version_one = version_one_format();
    const ClipboardLease lease(owner);
    if (!lease.opened() || exact == 0U || version_one == 0U) {
        return HostClipboardImageResult::failed(HostServiceError::backend_failure);
    }
    const std::uint64_t generation = GetClipboardSequenceNumber();
    const UINT format = IsClipboardFormatAvailable(exact) ? exact :
        IsClipboardFormatAvailable(version_one) ? version_one :
        IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 :
        IsClipboardFormatAvailable(CF_DIB) ? CF_DIB : 0U;
    if (format == 0U) return HostClipboardImageResult::nothing_read(generation);
    HANDLE memory = GetClipboardData(format);
    const SIZE_T size = memory == nullptr ? 0U : GlobalSize(memory);
    const GlobalRead locked(memory);
    if (locked.data() == nullptr || size == 0U) {
        return HostClipboardImageResult::failed(HostServiceError::backend_failure);
    }
    if (size > HostImage::maximum_bytes + 4096U) return HostClipboardImageResult::failed(HostServiceError::too_large);
    const std::span<const std::byte> bytes(static_cast<const std::byte*>(locked.data()), size);
    // A global allocation may be rounded up past the private format's pixels.
    if (format == exact || format == version_one) {
        return gui_forms::detail::decode_clipboard_image(bytes, generation, true);
    }
    return gui_forms::detail::decode_clipboard_dib(bytes, generation);
}
HostServiceStatus write_windows_clipboard_image(HWND owner, HostImageView image) {
    const UINT exact = raw_format();
    if (owner == nullptr || exact == 0U) return {HostServiceError::backend_failure};
    const std::vector<std::byte> wire = gui_forms::detail::encode_clipboard_image(image);
    const std::vector<std::byte> dib = gui_forms::detail::encode_clipboard_dib(image);
    GlobalStorage exact_storage(wire);
    GlobalStorage dib_storage(dib);
    if (!exact_storage.valid() || !dib_storage.valid()) return {HostServiceError::backend_failure};
    const ClipboardLease lease(owner);
    if (!lease.opened() || !EmptyClipboard()) return {HostServiceError::backend_failure};
    if (!exact_storage.publish(exact)) return {HostServiceError::backend_failure};
    if (!dib_storage.publish(CF_DIBV5)) {
        // Not left half written: a failure leaves the clipboard empty
        // (A-gf-host-03-F14).
        static_cast<void>(EmptyClipboard());
        return {HostServiceError::backend_failure};
    }
    return {};
}
#endif
} // namespace gui_forms::host::detail
