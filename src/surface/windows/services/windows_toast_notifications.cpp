#include "windows_toast_notifications.hpp"

#include "../../../core/failure/fail_fast.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"

#include <windows.h>
#include <appmodel.h>
#include <eventtoken.h>
#include <inspectable.h>
#include <objbase.h>
#include <roapi.h>
#include <shobjidl.h>
#include <winstring.h>

#include <atomic>
#include <climits>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stxlib/threads.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::host::detail {
namespace {

// -- the Windows Runtime interfaces used ----------------------------------------
//
// Declared by hand from the Windows SDK's metadata (Windows.UI.Notifications
// and Windows.Data.Xml.Dom, contract 1.0), with their interface identities:
// only the vtable slots matter, in the metadata's order, and only the slots
// called here are given their parameter types; the others take pointers of
// the same size. No IID is retrieved with __uuidof, so the declarations need
// no compiler extension.

constexpr GUID iid_toast_notification_manager_statics{
    0x50AC103FU, 0xD235U, 0x4598U, {0xBBU, 0xEFU, 0x98U, 0xFEU, 0x4DU, 0x1AU, 0x3AU, 0xD4U}};
constexpr GUID iid_toast_notification_factory{
    0x04124B20U, 0x82C6U, 0x4229U, {0xB1U, 0x09U, 0xFDU, 0x9EU, 0xD4U, 0x66U, 0x2BU, 0x53U}};
constexpr GUID iid_toast_dismissed_event_args{
    0x3F89D935U, 0xD9CBU, 0x4538U, {0xA0U, 0xF0U, 0xFFU, 0xE7U, 0x65U, 0x99U, 0x38U, 0xF8U}};
constexpr GUID iid_xml_document_io{
    0x6CD0E74EU, 0xEE65U, 0x4489U, {0x9EU, 0xBFU, 0xCAU, 0x43U, 0xE8U, 0x7BU, 0xA6U, 0x37U}};
constexpr GUID iid_xml_document{
    0xF7F3A506U, 0x1E87U, 0x42D6U, {0xBCU, 0xFBU, 0xB8U, 0xC8U, 0x09U, 0xFAU, 0x54U, 0x94U}};
// ITypedEventHandler<ToastNotification, Object>, <ToastNotification,
// ToastDismissedEventArgs> and <ToastNotification, ToastFailedEventArgs>.
constexpr GUID iid_activated_handler{
    0xAB54DE2DU, 0x97D9U, 0x5528U, {0xB6U, 0xADU, 0x10U, 0x5AU, 0xFEU, 0x15U, 0x65U, 0x30U}};
constexpr GUID iid_dismissed_handler{
    0x61C2402FU, 0x0ED0U, 0x5A18U, {0xABU, 0x69U, 0x59U, 0xF4U, 0xAAU, 0x99U, 0xA3U, 0x68U}};
constexpr GUID iid_failed_handler{
    0x95E3E803U, 0xC969U, 0x5E3AU, {0x97U, 0x53U, 0xEAU, 0x2AU, 0xD2U, 0x2AU, 0x9AU, 0x33U}};
constexpr GUID iid_agile_object{
    0x94EA2B94U, 0xE9CCU, 0x49E0U, {0xC0U, 0xFFU, 0xEEU, 0x64U, 0xCAU, 0x8FU, 0x5BU, 0x90U}};

struct ToastNotificationAbi : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Content(IInspectable** content) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ExpirationTime(IInspectable* expiration) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ExpirationTime(IInspectable** expiration) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Dismissed(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Dismissed(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Activated(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Activated(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Failed(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Failed(EventRegistrationToken token) = 0;
};

struct ToastNotifierAbi : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Show(ToastNotificationAbi* notification) = 0;
    virtual HRESULT STDMETHODCALLTYPE Hide(ToastNotificationAbi* notification) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Setting(INT32* setting) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddToSchedule(IInspectable* scheduled) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveFromSchedule(IInspectable* scheduled) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetScheduledToastNotifications(IInspectable** scheduled) = 0;
};

struct ToastNotificationManagerStaticsAbi : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifier(ToastNotifierAbi** notifier) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifierWithId(HSTRING application_id,
                                                                ToastNotifierAbi** notifier) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTemplateContent(INT32 template_type, IInspectable** content) = 0;
};

struct ToastNotificationFactoryAbi : public IInspectable {
    // `content` is an IXmlDocument.
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotification(IInspectable* content,
                                                              ToastNotificationAbi** notification) = 0;
};

struct XmlDocumentIoAbi : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE LoadXml(HSTRING xml) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadXmlWithSettings(HSTRING xml, IInspectable* settings) = 0;
    virtual HRESULT STDMETHODCALLTYPE SaveToFileAsync(IInspectable* file, IInspectable** operation) = 0;
};

struct ToastDismissedEventArgsAbi : public IInspectable {
    // ToastDismissalReason: 0 user canceled, 1 application hidden, 2 timed out.
    virtual HRESULT STDMETHODCALLTYPE get_Reason(INT32* reason) = 0;
};

// ITypedEventHandler<TSender, TResult>'s layout.
struct TypedEventHandlerAbi : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(IInspectable* sender, IInspectable* arguments) = 0;
};

constexpr INT32 dismissal_application_hidden = 1;

// The message a handler posts: WPARAM the HostDesktopNotificationAction,
// LPARAM the notification's identity value (64 bits on every supported
// target).
constexpr UINT toast_message = WM_APP + 0x2BU;
constexpr const wchar_t* window_class_name = L"GUIFormsToastNotifications";

// The window a handler posts to, or null once the notifications object is
// gone. Shared by the object and every handler; the lock orders the post
// against the window's destruction.
struct ToastMailbox final {
    stxlib::Mutex lock;
    HWND window{};
};

// One of a toast's three handlers. Agile (IAgileObject), so the runtime
// calls it on its own thread; Invoke only posts to the mailbox's window.
class ToastEventHandler final : public TypedEventHandlerAbi {
public:
    ToastEventHandler(std::shared_ptr<ToastMailbox> mailbox, GUID interface_id, HostDesktopNotificationAction action,
                      HostDesktopNotificationId notification_id) noexcept
        : mailbox_(std::move(mailbox)), interface_id_(interface_id), action_(action),
          notification_id_(notification_id) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (!IsEqualIID(iid, IID_IUnknown) && !IsEqualIID(iid, interface_id_) && !IsEqualIID(iid, iid_agile_object)) {
            return E_NOINTERFACE;
        }
        *out = static_cast<TypedEventHandlerAbi*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override {
        const ULONG count = references_.fetch_add(1U, std::memory_order_relaxed) + 1U;
        return count;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = references_.fetch_sub(1U, std::memory_order_acq_rel) - 1U;
        if (count == 0U) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IInspectable* sender, IInspectable* arguments) override {
        return gui_forms::detail::call_failing_fast("windows.ToastEventHandler.Invoke",
                                                    &ToastEventHandler::answer_invoke, *this, sender, arguments);
    }

private:
    ~ToastEventHandler() {}

    HRESULT answer_invoke(IInspectable* sender, IInspectable* arguments) {
        static_cast<void>(sender);
        if (action_ == HostDesktopNotificationAction::dismissed && arguments != nullptr) {
            ToastDismissedEventArgsAbi* dismissal = nullptr;
            if (SUCCEEDED((*arguments).QueryInterface(iid_toast_dismissed_event_args,
                                                      reinterpret_cast<void**>(&dismissal)))) {
                INT32 reason = 0;
                const HRESULT read = (*dismissal).get_Reason(&reason);
                (*dismissal).Release();
                // The application's own Hide (withdraw) reports nothing.
                if (SUCCEEDED(read) && reason == dismissal_application_hidden) return S_OK;
            }
        }
        const stxlib::ScopedLock guard((*mailbox_).lock);
        if ((*mailbox_).window != nullptr) {
            PostMessageW((*mailbox_).window, toast_message, static_cast<WPARAM>(action_),
                         static_cast<LPARAM>(notification_id_.value));
        }
        return S_OK;
    }

    std::atomic<ULONG> references_{1U};
    std::shared_ptr<ToastMailbox> mailbox_;
    GUID interface_id_{};
    HostDesktopNotificationAction action_{};
    HostDesktopNotificationId notification_id_{};
};

// UTF-8 to UTF-16; the request's text is valid UTF-8 (HostServices checked).
std::wstring widened(std::string_view text) {
    if (text.empty()) return std::wstring();
    // MultiByteToWideChar counts in an int; a longer text is not shown.
    if (text.size() > static_cast<std::size_t>(INT_MAX)) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length > 0 ? length : 0), L'\0');
    if (length > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    }
    return wide;
}

// Appends `text` to the toast's XML with the five markup characters escaped.
void append_escaped(std::wstring& xml, const std::wstring& text) {
    for (const wchar_t character : text) {
        switch (character) {
        case L'&':
            xml += L"&amp;";
            break;
        case L'<':
            xml += L"&lt;";
            break;
        case L'>':
            xml += L"&gt;";
            break;
        case L'"':
            xml += L"&quot;";
            break;
        case L'\'':
            xml += L"&apos;";
            break;
        default:
            xml += character;
            break;
        }
    }
}

// The toast's XML (the ToastGeneric template): the title and the text, the
// urgent scenario for a critical notification when `urgent`, and silent
// audio when asked.
std::wstring toast_xml(const HostDesktopNotificationRequest& request, bool urgent) {
    std::wstring xml = urgent ? L"<toast scenario=\"urgent\">" : L"<toast>";
    xml += L"<visual><binding template=\"ToastGeneric\">";
    if (!request.title.empty()) {
        xml += L"<text>";
        append_escaped(xml, widened(request.title));
        xml += L"</text>";
    }
    xml += L"<text>";
    append_escaped(xml, widened(request.text));
    xml += L"</text></binding></visual>";
    if (request.silent) xml += L"<audio silent=\"true\"/>";
    xml += L"</toast>";
    return xml;
}

// The process's explicit AppUserModelID, or empty.
std::wstring explicit_application_id() {
    PWSTR id = nullptr;
    std::wstring result;
    if (SUCCEEDED(GetCurrentProcessExplicitAppUserModelID(&id)) && id != nullptr) result = id;
    if (id != nullptr) CoTaskMemFree(id);
    return result;
}

bool packaged_process() noexcept {
    UINT32 length = 0U;
    const LONG answer = GetCurrentPackageFullName(&length, nullptr);
    return answer != APPMODEL_ERROR_NO_PACKAGE;
}

// An entry of combase, typed by its declaration.
template <typename Function>
Function combase_function(HMODULE module, const char* name) noexcept {
    static_assert(sizeof(Function) == sizeof(FARPROC));
    const FARPROC raw = GetProcAddress(module, name);
    Function function{};
    std::memcpy(&function, &raw, sizeof(function));
    return function;
}

LRESULT answer_toast_window_procedure(HWND window, UINT message, WPARAM w_parameter, LPARAM l_parameter);

LRESULT CALLBACK toast_window_procedure(HWND window, UINT message, WPARAM w_parameter, LPARAM l_parameter) {
    return gui_forms::detail::call_failing_fast("windows.toast_window_procedure", &answer_toast_window_procedure,
                                                window, message, w_parameter, l_parameter);
}

} // namespace

struct WindowsToastNotifications::Impl final {
    // combase's Windows Runtime entries, typed by their declarations.
    using GetActivationFactory = decltype(&RoGetActivationFactory);
    using ActivateInstance = decltype(&RoActivateInstance);
    using CreateString = decltype(&WindowsCreateString);
    using DeleteString = decltype(&WindowsDeleteString);

    struct Toast final {
        HostDesktopNotificationId id{};
        ToastNotificationAbi* toast{};
        EventRegistrationToken activated{};
        EventRegistrationToken dismissed{};
        EventRegistrationToken failed{};
    };

    Sink sink{};
    HMODULE combase{};
    GetActivationFactory get_activation_factory{};
    ActivateInstance activate_instance{};
    CreateString create_string{};
    DeleteString delete_string{};
    ToastNotifierAbi* notifier{};
    ToastNotificationFactoryAbi* factory{};
    std::shared_ptr<ToastMailbox> mailbox{gui_forms::detail::make_library_shared<ToastMailbox>()};
    std::vector<Toast> toasts{};
    // Whether ensure ran and failed; it is not tried again.
    bool unavailable{};

    [[nodiscard]] bool ensure();
    [[nodiscard]] HSTRING make_string(const std::wstring& text) const;
    [[nodiscard]] ToastNotificationAbi* make_toast(const std::wstring& xml) const;
    void stop(Toast& toast) noexcept;
    void deliver(HostDesktopNotificationAction action, HostDesktopNotificationId id);
};

namespace {

LRESULT answer_toast_window_procedure(HWND window, UINT message, WPARAM w_parameter, LPARAM l_parameter) {
    if (message != toast_message) return DefWindowProcW(window, message, w_parameter, l_parameter);
    WindowsToastNotifications::Impl* const impl =
        reinterpret_cast<WindowsToastNotifications::Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (impl == nullptr) return 0;
    const HostDesktopNotificationAction action = static_cast<HostDesktopNotificationAction>(w_parameter);
    const HostDesktopNotificationId id{static_cast<std::uint64_t>(l_parameter)};
    (*impl).deliver(action, id);
    return 0;
}

} // namespace

bool WindowsToastNotifications::available() noexcept {
    const bool available = packaged_process() || !explicit_application_id().empty();
    return available;
}

WindowsToastNotifications::WindowsToastNotifications(Sink sink) : impl_(std::make_unique<Impl>()) {
    (*impl_).sink = sink;
}

WindowsToastNotifications::~WindowsToastNotifications() {
    Impl& impl = *impl_;
    HWND window = nullptr;
    {
        const stxlib::ScopedLock guard((*impl.mailbox).lock);
        window = (*impl.mailbox).window;
        (*impl.mailbox).window = nullptr;
    }
    for (Impl::Toast& toast : impl.toasts) impl.stop(toast);
    impl.toasts.clear();
    if (impl.factory != nullptr) (*impl.factory).Release();
    if (impl.notifier != nullptr) (*impl.notifier).Release();
    if (window != nullptr) DestroyWindow(window);
    if (impl.combase != nullptr) FreeLibrary(impl.combase);
}

HSTRING WindowsToastNotifications::Impl::make_string(const std::wstring& text) const {
    HSTRING made = nullptr;
    if (FAILED(create_string(text.c_str(), static_cast<UINT32>(text.size()), &made))) return nullptr;
    return made;
}

// The runtime, the notifier, the toast factory and the message window, on
// the first show.
bool WindowsToastNotifications::Impl::ensure() {
    if (notifier != nullptr) return true;
    if (unavailable) return false;
    unavailable = true;
    // Pinned for the thread: S_FALSE (already single-threaded) and
    // RPC_E_CHANGED_MODE (multithreaded) serve as well.
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    static_cast<void>(apartment);
    combase = LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (combase == nullptr) return false;
    get_activation_factory = combase_function<GetActivationFactory>(combase, "RoGetActivationFactory");
    activate_instance = combase_function<ActivateInstance>(combase, "RoActivateInstance");
    create_string = combase_function<CreateString>(combase, "WindowsCreateString");
    delete_string = combase_function<DeleteString>(combase, "WindowsDeleteString");
    if (get_activation_factory == nullptr || activate_instance == nullptr || create_string == nullptr ||
        delete_string == nullptr) {
        return false;
    }
    const HSTRING manager_class = make_string(L"Windows.UI.Notifications.ToastNotificationManager");
    ToastNotificationManagerStaticsAbi* statics = nullptr;
    const HRESULT got_statics = get_activation_factory(manager_class, iid_toast_notification_manager_statics,
                                                       reinterpret_cast<void**>(&statics));
    delete_string(manager_class);
    if (FAILED(got_statics) || statics == nullptr) return false;
    const std::wstring application_id = explicit_application_id();
    HRESULT made_notifier = E_FAIL;
    if (!application_id.empty()) {
        const HSTRING id = make_string(application_id);
        made_notifier = (*statics).CreateToastNotifierWithId(id, &notifier);
        delete_string(id);
    } else {
        made_notifier = (*statics).CreateToastNotifier(&notifier);
    }
    (*statics).Release();
    if (FAILED(made_notifier) || notifier == nullptr) {
        notifier = nullptr;
        return false;
    }
    const HSTRING toast_class = make_string(L"Windows.UI.Notifications.ToastNotification");
    const HRESULT got_factory =
        get_activation_factory(toast_class, iid_toast_notification_factory, reinterpret_cast<void**>(&factory));
    delete_string(toast_class);
    if (FAILED(got_factory) || factory == nullptr) {
        factory = nullptr;
        (*notifier).Release();
        notifier = nullptr;
        return false;
    }
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &toast_window_procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = window_class_name;
    // A class registered by an earlier object stays registered.
    static_cast<void>(RegisterClassExW(&window_class));
    HWND const window = CreateWindowExW(0, window_class_name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                        window_class.hInstance, nullptr);
    if (window == nullptr) {
        (*factory).Release();
        factory = nullptr;
        (*notifier).Release();
        notifier = nullptr;
        return false;
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    {
        const stxlib::ScopedLock guard((*mailbox).lock);
        (*mailbox).window = window;
    }
    unavailable = false;
    return true;
}

// A toast from its XML, or null.
ToastNotificationAbi* WindowsToastNotifications::Impl::make_toast(const std::wstring& xml) const {
    const HSTRING document_class = make_string(L"Windows.Data.Xml.Dom.XmlDocument");
    IInspectable* document = nullptr;
    const HRESULT activated = activate_instance(document_class, &document);
    delete_string(document_class);
    if (FAILED(activated) || document == nullptr) return nullptr;
    XmlDocumentIoAbi* loader = nullptr;
    HRESULT status = (*document).QueryInterface(iid_xml_document_io, reinterpret_cast<void**>(&loader));
    if (SUCCEEDED(status)) {
        const HSTRING text = make_string(xml);
        status = (*loader).LoadXml(text);
        delete_string(text);
        (*loader).Release();
    }
    IInspectable* content = nullptr;
    if (SUCCEEDED(status)) status = (*document).QueryInterface(iid_xml_document, reinterpret_cast<void**>(&content));
    ToastNotificationAbi* toast = nullptr;
    if (SUCCEEDED(status)) status = (*factory).CreateToastNotification(content, &toast);
    if (content != nullptr) (*content).Release();
    (*document).Release();
    if (FAILED(status)) return nullptr;
    return toast;
}

// Removes the toast's handlers and releases it; the toast stays where it is.
void WindowsToastNotifications::Impl::stop(Toast& toast) noexcept {
    if (toast.toast == nullptr) return;
    static_cast<void>((*toast.toast).remove_Activated(toast.activated));
    static_cast<void>((*toast.toast).remove_Dismissed(toast.dismissed));
    static_cast<void>((*toast.toast).remove_Failed(toast.failed));
    (*toast.toast).Release();
    toast.toast = nullptr;
}

// A handler's report, on the UI thread. The toast is finished before the
// sink runs, so the application's handler may show or withdraw again.
void WindowsToastNotifications::Impl::deliver(HostDesktopNotificationAction action, HostDesktopNotificationId id) {
    for (std::size_t index = 0U; index < toasts.size(); ++index) {
        if (toasts[index].id != id) continue;
        stop(toasts[index]);
        toasts.erase(toasts.begin() + static_cast<std::ptrdiff_t>(index));
        const HostDesktopNotificationEvent event{id, action};
        sink(event);
        return;
    }
}

HostServiceStatus WindowsToastNotifications::show(HostDesktopNotificationId notification_id,
                                                  const HostDesktopNotificationRequest& request) {
    Impl& impl = *impl_;
    if (!impl.ensure()) return {HostServiceError::backend_failure};
    const bool urgent = request.urgency == HostDesktopNotificationUrgency::critical;
    ToastNotificationAbi* toast = impl.make_toast(toast_xml(request, urgent));
    // A system without the urgent scenario refuses it: the toast is made
    // again without it.
    if (toast == nullptr && urgent) toast = impl.make_toast(toast_xml(request, false));
    if (toast == nullptr) return {HostServiceError::backend_failure};
    Impl::Toast entry{};
    entry.id = notification_id;
    entry.toast = toast;
    ToastEventHandler* const on_activated = new ToastEventHandler(
        impl.mailbox, iid_activated_handler, HostDesktopNotificationAction::activated, notification_id);
    ToastEventHandler* const on_dismissed = new ToastEventHandler(
        impl.mailbox, iid_dismissed_handler, HostDesktopNotificationAction::dismissed, notification_id);
    ToastEventHandler* const on_failed = new ToastEventHandler(impl.mailbox, iid_failed_handler,
                                                               HostDesktopNotificationAction::failed, notification_id);
    // The toast holds its own references to the handlers it took.
    static_cast<void>((*toast).add_Activated(on_activated, &entry.activated));
    static_cast<void>((*toast).add_Dismissed(on_dismissed, &entry.dismissed));
    static_cast<void>((*toast).add_Failed(on_failed, &entry.failed));
    (*on_activated).Release();
    (*on_dismissed).Release();
    (*on_failed).Release();
    const HRESULT shown = (*impl.notifier).Show(toast);
    if (FAILED(shown)) {
        impl.stop(entry);
        return {HostServiceError::backend_failure};
    }
    impl.toasts.push_back(entry);
    return {};
}

void WindowsToastNotifications::withdraw(HostDesktopNotificationId notification_id) noexcept {
    Impl& impl = *impl_;
    for (std::size_t index = 0U; index < impl.toasts.size(); ++index) {
        if (impl.toasts[index].id != notification_id) continue;
        if (impl.notifier != nullptr) static_cast<void>((*impl.notifier).Hide(impl.toasts[index].toast));
        impl.stop(impl.toasts[index]);
        impl.toasts.erase(impl.toasts.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
}

} // namespace gui_forms::host::detail
