#pragma once

// The state behind every table of the C ABI (major 1): object identities,
// subscriptions, posted callbacks, deferred releases and the window run. See
// include/gui_forms/c/gf_abi.h ("Threads", "Callbacks") for the contract this
// implements.

#include "gui_forms/c/gf_abi.h"

#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/dispatcher/window_dispatcher/window_dispatcher.hpp"
#include "gui_forms/event.hpp"

#include "../failure/abi_failure.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace gui_forms::abi1 {

// A gf_object or gf_subscription split into its parts: the slot index plus
// one in the low 32 bits (so zero is no identity) and the generation in the
// high 32 bits.
struct Identity {
    std::uint32_t slot = 0U;
    std::uint32_t generation = 0U;
};

[[nodiscard]] Identity decode_identity(std::uint64_t value) noexcept;
[[nodiscard]] std::uint64_t encode_identity(Identity identity) noexcept;

// The most callbacks that wait for one thread at once (gf_abi.h).
inline constexpr std::size_t maximum_posted_callbacks = 4096U;

// The thread an object belongs to (gf_abi.h, "Threads"): the library's UI
// thread, or one running sub-surface (GF_TABLE_SUBSURFACES, since 1.1).
// Zero is the UI thread; a sub-surface's domain is its slot index plus one.
struct ThreadDomain {
    std::uint32_t value = 0U;
};
inline constexpr ThreadDomain ui_domain{0U};

// One callback a caller lent the library. The binding owns the loan: its
// destruction calls the callback's release, so each binding is destroyed on
// the UI thread, exactly once, after its last invoke. disarm() is for a call
// that fails after building the binding: the library then never took the
// callback and must not release it.
class CallbackBinding final {
public:
    explicit CallbackBinding(const gf_callback& callback) noexcept;
    ~CallbackBinding();
    CallbackBinding(const CallbackBinding&) = delete;
    CallbackBinding& operator=(const CallbackBinding&) = delete;

    void invoke(gf_event& event) const noexcept;
    void disarm() noexcept;

private:
    gf_callback callback_{};
};

// Validates a caller's gf_callback (struct_size at least the 1.0 size,
// reserved zero, invoke set) and copies its 1.0 fields into copy.
[[nodiscard]] gf_status read_callback(const gf_callback* callback, SlotIdentity slot,
                                      gf_callback& copy) noexcept;

// The binding of one subscription. The source's event owns it through the
// forwarder it subscribed (events table), so it dies when the subscription is
// disconnected or the source ends; its destructor frees the subscription's
// identity and then releases the callback.
class SubscriptionBinding final {
public:
    SubscriptionBinding(const gf_callback& callback, gf_subscription subscription,
                        const Component* source, std::string name) noexcept;
    ~SubscriptionBinding();
    SubscriptionBinding(const SubscriptionBinding&) = delete;
    SubscriptionBinding& operator=(const SubscriptionBinding&) = delete;

    // Calls the callback with the source's current identity as sender.
    void emit(const gf_value* payload) noexcept;
    // Calls the callback with event, whose payload and fields the caller
    // filled, after setting its sender and name.
    void emit(gf_event& event) noexcept;
    void disarm() noexcept;

private:
    CallbackBinding callback_;
    gf_subscription subscription_ = 0U;
    // Observed only as a key into the registry's identity map; the source
    // owns this binding, so it outlives every emission.
    const Component* source_ = nullptr;
    std::string name_;
};

// One callback posted with begin_invoke, waiting for its target's thread.
// The target is retained (one internal reference) until the callback has
// run.
struct PostedCall {
    std::unique_ptr<CallbackBinding> callback;
    gf_object target = 0U;
};

class HandleRegistry final {
public:
    HandleRegistry() noexcept = default;
    HandleRegistry(const HandleRegistry&) = delete;
    HandleRegistry& operator=(const HandleRegistry&) = delete;

    // The entry of every UI-thread slot. Returns GF_STATUS_WRONG_THREAD off
    // the UI thread; with bind, an unbound library binds the calling thread.
    // On the UI thread it first completes deferred releases. On the thread
    // of a running sub-surface it admits the tables a sub-surface may use
    // (objects, properties, events, dispatch, schema, controls, text
    // editing and subsurfaces), completes that thread's deferred releases,
    // and never binds the UI thread; every other table is
    // GF_STATUS_WRONG_THREAD there.
    [[nodiscard]] gf_status enter_ui(SlotIdentity slot, bool bind);
    // Unbinds the UI thread when nothing holds it: no object, subscription,
    // posted or deferred callback, or run. Ends a call that bound the
    // library for itself alone (a schema read).
    void unbind_if_idle();
    // Keeps the UI thread bound for good: the engine state (themes and host
    // services) is the UI thread's from its first use. The caller has
    // entered the UI thread with bind.
    void hold_binding() noexcept;
    // Whether the library holds an object or a run: an application is made
    // only before either (application.create).
    [[nodiscard]] bool holds_objects() noexcept;

    // The control behind a live identity (GF_STATUS_INVALID_ARGUMENT for
    // zero, GF_STATUS_STALE_HANDLE for an ended one or one whose sub-surface
    // has stopped, GF_STATUS_WRONG_THREAD for one that belongs to another
    // thread than the caller's).
    // The control behind object; GF_STATUS_WRONG_HANDLE_KIND for a
    // component that is not a control.
    [[nodiscard]] gf_status find_object(gf_object object, SlotIdentity slot, Control::Ptr& control);
    // The component (a control among them) behind object.
    [[nodiscard]] gf_status find_component(gf_object object, SlotIdentity slot, Component::Ptr& component);
    // A new reference to component, reusing its identity when it has one.
    // A new identity belongs to the calling thread: the UI thread, or the
    // sub-surface whose thread calls. Throws std::bad_alloc, leaving the
    // registry unchanged.
    [[nodiscard]] gf_object add_reference(const Component::Ptr& component);
    [[nodiscard]] gf_status retain(gf_object object, SlotIdentity slot);
    [[nodiscard]] gf_status release(gf_object object, SlotIdentity slot);
    // The identity of component, or zero when it has none. Borrowed.
    [[nodiscard]] gf_object identity_of(const Component* component);
    // A new reference to the component that has the identity of component,
    // or zero when it has none (a component not owned through the
    // registry cannot be lent by address alone).
    [[nodiscard]] gf_object add_reference_if_known(const Component* component);

    // A subscription is reserved before its source is connected, so its
    // binding knows its identity, then completed with the source's token or
    // abandoned. UI thread.
    [[nodiscard]] gf_subscription reserve_subscription(const Component::Ptr& source);
    void complete_subscription(gf_subscription subscription, SubscriptionToken token) noexcept;
    void abandon_subscription(gf_subscription subscription) noexcept;
    [[nodiscard]] gf_status disconnect(gf_subscription subscription, SlotIdentity slot);
    // Frees the identity of a subscription whose binding died. A no-op for
    // one already freed by disconnect.
    void end_subscription(gf_subscription subscription) noexcept;
    // Ends every subscription whose source has been disposed or destroyed.
    void end_disposed_subscriptions();

    // Queues callback for the thread target belongs to and wakes it (a
    // running window, or the sub-surface's loop). Any thread. On failure
    // callback is left with the caller.
    [[nodiscard]] gf_status post(gf_object target, std::unique_ptr<CallbackBinding>& callback,
                                 SlotIdentity slot);
    // Runs the callbacks posted to the calling thread before this call, at
    // most maximum of them, and returns how many were invoked. The UI thread
    // or a sub-surface's thread.
    [[nodiscard]] std::uint64_t run_posted(std::size_t maximum);

    // Sub-surface threads (GF_TABLE_SUBSURFACES). begin_domain, on the
    // sub-surface's thread before it makes any object, makes that thread a
    // domain: objects it creates from then on belong to it. set_domain_wake
    // gives the domain the dispatcher its posted callbacks and deferred
    // releases are run through (until then they wait), and runs what
    // already waits. end_domain, on the same thread when the sub-surface
    // stops, destroys every object of the domain there (their identities
    // stay until released, and give GF_STATUS_STALE_HANDLE), releases the
    // callbacks posted to it unrun, and ends the domain. begin_domain throws
    // std::bad_alloc, leaving the registry unchanged.
    [[nodiscard]] ThreadDomain begin_domain();
    void set_domain_wake(ThreadDomain domain, const WindowDispatcher& dispatcher);
    void end_domain(ThreadDomain domain) noexcept;
    // Runs a sub-surface domain's deferred releases and posted callbacks: the
    // work its wake posts. The domain's thread.
    void drain_domain(ThreadDomain domain);

    // The window run. begin_run refuses a second run; the host's callbacks
    // publish its window and wake; end_run forgets both.
    [[nodiscard]] gf_status begin_run(gf_object root, SlotIdentity slot);
    void set_run_window(const ApplicationWindowHandle& window);
    void set_run_wake(std::function<void()> wake);
    void end_run();
    [[nodiscard]] gf_status running_window(gf_object root, SlotIdentity slot,
                                           ApplicationWindowHandle& window);

private:
    struct ObjectSlot {
        std::uint32_t generation = 1U;
        bool occupied = false;
        // The object's sub-surface stopped: object is gone, the identity
        // stays until its last reference is released.
        bool orphaned = false;
        ThreadDomain domain{};
        std::uint64_t references = 0U;
        Component::Ptr object;
    };
    // One sub-surface thread's domain (domain value = index + 1). Move-only:
    // its posted calls own their callbacks. The copy operations are deleted
    // explicitly because libstdc++'s std::deque move constructor is not
    // noexcept, so std::vector growth would otherwise pick the (implicitly
    // declared, then ill-formed) copy constructor.
    struct SubSurfaceDomain {
        SubSurfaceDomain() = default;
        SubSurfaceDomain(const SubSurfaceDomain&) = delete;
        SubSurfaceDomain& operator=(const SubSurfaceDomain&) = delete;
        SubSurfaceDomain(SubSurfaceDomain&&) = default;
        SubSurfaceDomain& operator=(SubSurfaceDomain&&) = default;

        bool occupied = false;
        std::thread::id thread{};
        // Objects of the domain released last off its thread.
        std::vector<Component::Ptr> deferred;
        std::deque<PostedCall> posted;
        // Empty until set_domain_wake; posts a DrainDomain.
        WindowDispatcher dispatcher;
        // A DrainDomain is posted and has not run yet.
        bool drain_pending = false;
    };
    struct SubscriptionSlot {
        std::uint32_t generation = 1U;
        bool occupied = false;
        SubscriptionToken token;
        std::weak_ptr<Component> source;
    };

    [[nodiscard]] bool on_ui_thread_locked() const noexcept;
    // The calling thread's domain: a sub-surface's, else the UI domain
    // (whether or not the caller is the UI thread; enter_ui decides that).
    [[nodiscard]] ThreadDomain calling_domain_locked() const noexcept;
    // Whether the caller runs on the thread of domain.
    [[nodiscard]] bool on_domain_thread_locked(ThreadDomain domain) const noexcept;
    [[nodiscard]] std::vector<Component::Ptr>& deferred_locked(ThreadDomain domain) noexcept;
    [[nodiscard]] std::deque<PostedCall>& posted_locked(ThreadDomain domain) noexcept;
    // Wakes domain's thread: the UI's run wake, called with the lock held,
    // or, for a sub-surface, the dispatcher to post a DrainDomain through,
    // which the caller posts after leaving the lock (an empty one when a
    // drain is pending or there is no dispatcher yet).
    [[nodiscard]] WindowDispatcher wake_locked(ThreadDomain domain) noexcept;
    [[nodiscard]] ObjectSlot* object_slot_locked(gf_object object) noexcept;
    [[nodiscard]] SubscriptionSlot* subscription_slot_locked(gf_subscription subscription) noexcept;
    // Drops one reference; at zero frees the identity and moves the control
    // into ended. ended is destroyed by the caller without the lock.
    void drop_reference_locked(gf_object object, ObjectSlot& slot, Component::Ptr& ended);
    // Takes the calling thread's deferred controls and destroys them without
    // the lock.
    void complete_deferred();

    std::mutex mutex_;
    // The UI thread, or a default id while unbound.
    std::thread::id ui_thread_{};
    std::vector<ObjectSlot> objects_;
    std::vector<std::uint32_t> free_objects_;
    std::unordered_map<const Component*, std::uint32_t> object_index_;
    std::size_t live_objects_ = 0U;
    std::vector<SubSurfaceDomain> domains_;
    std::vector<SubscriptionSlot> subscriptions_;
    std::vector<std::uint32_t> free_subscriptions_;
    std::size_t live_subscriptions_ = 0U;
    // UI-thread objects whose last reference was released off the UI thread.
    std::vector<Component::Ptr> deferred_;
    std::deque<PostedCall> posted_;
    bool running_ = false;
    // Set once the engine state exists; the binding then never ends.
    bool binding_held_ = false;
    gf_object run_root_ = 0U;
    ApplicationWindowHandle run_window_{};
    // The running window's wake: called with the lock held, which the host
    // allows (it only schedules), and cleared before the window closes.
    std::function<void()> run_wake_{};
};

// The library's one registry. It is created on first use and never
// destroyed, so no callback is released by a static destructor at exit.
[[nodiscard]] HandleRegistry& registry() noexcept;

// The status and message for the exception being handled; for use inside a
// catch block of a slot, which is the subsystem boundary.
gf_status fail_with_current_exception(SlotIdentity slot) noexcept;

// Validates borrowed UTF-8 text: data may be null only when size is zero,
// and the bytes must be well-formed UTF-8.
[[nodiscard]] gf_status read_text(gf_utf8_view view, SlotIdentity slot, std::string_view& text) noexcept;

// Copies text into a caller's buffer (not terminated). required receives the
// size; a short buffer gives GF_STATUS_BUFFER_TOO_SMALL and writes nothing else.
[[nodiscard]] gf_status write_text(std::string_view text, char* buffer, std::uint64_t capacity,
                                   std::uint64_t* required, SlotIdentity slot) noexcept;

} // namespace gui_forms::abi1
