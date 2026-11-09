// The registry behind the C ABI tables. See abi_registry.hpp.

#include "abi_registry.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace gui_forms::abi1 {
namespace {

// The 1.0 size of gf_callback: a caller passes at least this much.
constexpr std::uint32_t callback_size_1_0 = 32U;
static_assert(sizeof(gf_callback) == callback_size_1_0);

// The next generation of a freed slot. Zero is never a generation, so no
// identity of a reused slot equals zero or the slot's previous identity.
std::uint32_t next_generation(std::uint32_t generation) noexcept {
    const std::uint32_t next = generation == std::numeric_limits<std::uint32_t>::max() ? 1U : generation + 1U;
    return next;
}

// The length of the well-formed UTF-8 sequence starting at text[index], or
// zero when the bytes there are not one.
std::size_t utf8_sequence_length(std::string_view text, std::size_t index) noexcept {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    std::size_t length = 0U;
    std::uint32_t minimum = 0U;
    std::uint32_t code_point = 0U;
    if (lead < 0x80U) {
        return 1U;
    }
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2U;
        minimum = 0x80U;
        code_point = lead & 0x1FU;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3U;
        minimum = 0x800U;
        code_point = lead & 0x0FU;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4U;
        minimum = 0x10000U;
        code_point = lead & 0x07U;
    } else {
        return 0U;
    }
    if (text.size() - index < length) {
        return 0U;
    }
    std::size_t offset = 1U;
    for (offset = 1U; offset < length; ++offset) {
        const unsigned char continuation = static_cast<unsigned char>(text[index + offset]);
        if ((continuation & 0xC0U) != 0x80U) {
            return 0U;
        }
        code_point = (code_point << 6U) | (continuation & 0x3FU);
    }
    const bool surrogate = code_point >= 0xD800U && code_point <= 0xDFFFU;
    if (code_point < minimum || code_point > 0x10FFFFU || surrogate) {
        return 0U;
    }
    return length;
}

} // namespace

Identity decode_identity(std::uint64_t value) noexcept {
    Identity identity{};
    identity.slot = static_cast<std::uint32_t>(value & 0xFFFFFFFFU);
    identity.generation = static_cast<std::uint32_t>(value >> 32U);
    return identity;
}

std::uint64_t encode_identity(Identity identity) noexcept {
    const std::uint64_t value = (static_cast<std::uint64_t>(identity.generation) << 32U) | identity.slot;
    return value;
}

// ---------------------------------------------------------------------------
// Callback bindings.

CallbackBinding::CallbackBinding(const gf_callback& callback) noexcept : callback_(callback) {}

CallbackBinding::~CallbackBinding() {
    if (callback_.release != nullptr) {
        callback_.release(callback_.context);
    }
}

void CallbackBinding::invoke(gf_event& event) const noexcept {
    callback_.invoke(callback_.context, &event);
}

void CallbackBinding::disarm() noexcept {
    callback_.release = nullptr;
}

gf_status read_callback(const gf_callback* callback, SlotIdentity slot, gf_callback& copy) noexcept {
    if (callback == nullptr || (*callback).struct_size < callback_size_1_0 || (*callback).reserved != 0U ||
        (*callback).invoke == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a gf_callback needs struct_size of at least 32, reserved zero and an invoke function");
    }
    copy = gf_callback{};
    copy.struct_size = callback_size_1_0;
    copy.invoke = (*callback).invoke;
    copy.release = (*callback).release;
    copy.context = (*callback).context;
    return GF_STATUS_OK;
}

SubscriptionBinding::SubscriptionBinding(const gf_callback& callback, gf_subscription subscription,
                                         const Component* source, std::string name) noexcept
    : callback_(callback), subscription_(subscription), source_(source), name_(std::move(name)) {}

SubscriptionBinding::~SubscriptionBinding() {
    registry().end_subscription(subscription_);
}

namespace {

// The subject of a shadow frame about component: a control's stable id,
// nothing for other components.
std::string_view frame_subject(const Component* component) noexcept {
    const Control* control = dynamic_cast<const Control*>(component);
    if (control == nullptr) {
        return std::string_view{};
    }
    return (*control).stable_id().value();
}

} // namespace

void SubscriptionBinding::emit(const gf_value* payload) noexcept {
    const ShadowFrameScope shadow_frame(ShadowFrameKind::event_callback, frame_subject(source_), name_, 0U);
    gf_event event{};
    event.struct_size = sizeof(gf_event);
    event.sender = registry().identity_of(source_);
    event.name.data = name_.data();
    event.name.size = name_.size();
    event.payload = payload;
    callback_.invoke(event);
}

void SubscriptionBinding::emit(gf_event& event) noexcept {
    const ShadowFrameScope shadow_frame(ShadowFrameKind::event_callback, frame_subject(source_), name_, 0U);
    event.struct_size = sizeof(gf_event);
    event.sender = registry().identity_of(source_);
    event.name.data = name_.data();
    event.name.size = name_.size();
    callback_.invoke(event);
}

void SubscriptionBinding::disarm() noexcept {
    callback_.disarm();
}

// ---------------------------------------------------------------------------
// Threads and deferred releases.

bool HandleRegistry::on_ui_thread_locked() const noexcept {
    const bool on_ui_thread = ui_thread_ == std::this_thread::get_id();
    return on_ui_thread;
}

ThreadDomain HandleRegistry::calling_domain_locked() const noexcept {
    const std::thread::id caller = std::this_thread::get_id();
    std::size_t index = 0U;
    for (index = 0U; index < domains_.size(); ++index) {
        if (domains_[index].occupied && domains_[index].thread == caller) {
            const ThreadDomain domain{static_cast<std::uint32_t>(index + 1U)};
            return domain;
        }
    }
    return ui_domain;
}

bool HandleRegistry::on_domain_thread_locked(ThreadDomain domain) const noexcept {
    if (domain.value == ui_domain.value) {
        return on_ui_thread_locked();
    }
    const SubSurfaceDomain& owner = domains_[domain.value - 1U];
    const bool on_thread = owner.occupied && owner.thread == std::this_thread::get_id();
    return on_thread;
}

std::vector<Component::Ptr>& HandleRegistry::deferred_locked(ThreadDomain domain) noexcept {
    if (domain.value == ui_domain.value) {
        return deferred_;
    }
    return domains_[domain.value - 1U].deferred;
}

std::deque<PostedCall>& HandleRegistry::posted_locked(ThreadDomain domain) noexcept {
    if (domain.value == ui_domain.value) {
        return posted_;
    }
    return domains_[domain.value - 1U].posted;
}

namespace {

// The tables a sub-surface's thread may call (enter_ui): the ones that act
// on objects and read the schema. Windows, themes, the host, diagnostics,
// overlays, property grids, custom controls and bindings stay on the UI
// thread.
bool admitted_on_sub_surface(gf_table_id table) noexcept {
    switch (table) {
    case GF_TABLE_OBJECTS:
    case GF_TABLE_PROPERTIES:
    case GF_TABLE_EVENTS:
    case GF_TABLE_DISPATCH:
    case GF_TABLE_SCHEMA:
    case GF_TABLE_CONTROLS:
    case GF_TABLE_TEXT_EDITING:
    case GF_TABLE_SUBSURFACES:
        return true;
    default:
        return false;
    }
}

// Posted to a sub-surface's dispatcher by its domain's wake: runs the
// domain's deferred releases and posted callbacks on its thread.
struct DrainDomain final {
    ThreadDomain domain{};
    void operator()() const {
        registry().drain_domain(domain);
    }
};

} // namespace

gf_status HandleRegistry::enter_ui(SlotIdentity slot, bool bind) {
    bool sub_surface = false;
    bool bound = false;
    {
        const std::scoped_lock lock(mutex_);
        sub_surface = calling_domain_locked().value != ui_domain.value;
        if (sub_surface && !admitted_on_sub_surface(slot.table_id)) {
            return fail(GF_STATUS_WRONG_THREAD, slot, "this function runs on the GUI.Forms UI thread only, "
                                                      "not on a sub-surface's thread");
        }
        if (!sub_surface && ui_thread_ != std::thread::id{} && !on_ui_thread_locked()) {
            return fail(GF_STATUS_WRONG_THREAD, slot, "this function runs on the GUI.Forms UI thread only");
        }
        bound = sub_surface || ui_thread_ != std::thread::id{};
    }
    if (bound) {
        // The calling thread's deferred releases. On the UI thread this may
        // unbind the library when nothing is left.
        complete_deferred();
    }
    // A sub-surface's thread never binds the UI thread.
    if (bind && !sub_surface) {
        const std::scoped_lock lock(mutex_);
        if (ui_thread_ == std::thread::id{}) {
            ui_thread_ = std::this_thread::get_id();
        } else if (!on_ui_thread_locked()) {
            return fail(GF_STATUS_WRONG_THREAD, slot, "another thread became the GUI.Forms UI thread first");
        }
    }
    return GF_STATUS_OK;
}

void HandleRegistry::complete_deferred() {
    std::vector<Component::Ptr> ended;
    {
        const std::scoped_lock lock(mutex_);
        ended.swap(deferred_locked(calling_domain_locked()));
    }
    // Destroying the controls may end subscriptions, which takes the lock.
    ended.clear();
    unbind_if_idle();
}

void HandleRegistry::unbind_if_idle() {
    const std::scoped_lock lock(mutex_);
    // Only the UI thread gives up its binding.
    const bool idle = on_ui_thread_locked() && live_objects_ == 0U && live_subscriptions_ == 0U &&
                      posted_.empty() && deferred_.empty() && !running_ && !binding_held_;
    if (idle) {
        ui_thread_ = std::thread::id{};
    }
}

void HandleRegistry::hold_binding() noexcept {
    const std::scoped_lock lock(mutex_);
    binding_held_ = true;
}

bool HandleRegistry::holds_objects() noexcept {
    const std::scoped_lock lock(mutex_);
    const bool holds = live_objects_ != 0U || running_;
    return holds;
}

WindowDispatcher HandleRegistry::wake_locked(ThreadDomain domain) noexcept {
    if (domain.value == ui_domain.value) {
        if (run_wake_) {
            run_wake_();
        }
        return WindowDispatcher{};
    }
    SubSurfaceDomain& owner = domains_[domain.value - 1U];
    if (!owner.occupied || owner.drain_pending || !owner.dispatcher.window_alive()) {
        return WindowDispatcher{};
    }
    owner.drain_pending = true;
    return owner.dispatcher;
}

namespace {

// Posts a domain's drain through the dispatcher wake_locked returned, after
// the registry's lock is left. An empty dispatcher posts nothing; a closed
// one cancels the work, which the domain's end then covers.
void post_drain(const WindowDispatcher& dispatcher, ThreadDomain domain) {
    if (!dispatcher.window_alive()) {
        return;
    }
    static_cast<void>(dispatcher.begin_invoke(DrainDomain{domain}));
}

} // namespace

ThreadDomain HandleRegistry::begin_domain() {
    const std::scoped_lock lock(mutex_);
    std::size_t index = 0U;
    for (index = 0U; index < domains_.size(); ++index) {
        if (!domains_[index].occupied) {
            break;
        }
    }
    if (index == domains_.size()) {
        domains_.emplace_back();
    }
    SubSurfaceDomain& owner = domains_[index];
    owner.occupied = true;
    owner.thread = std::this_thread::get_id();
    owner.drain_pending = false;
    const ThreadDomain domain{static_cast<std::uint32_t>(index + 1U)};
    return domain;
}

void HandleRegistry::set_domain_wake(ThreadDomain domain, const WindowDispatcher& dispatcher) {
    WindowDispatcher wake;
    {
        const std::scoped_lock lock(mutex_);
        SubSurfaceDomain& owner = domains_[domain.value - 1U];
        owner.dispatcher = dispatcher;
        owner.drain_pending = false;
        if (!owner.posted.empty() || !owner.deferred.empty()) {
            wake = wake_locked(domain);
        }
    }
    post_drain(wake, domain);
}

void HandleRegistry::drain_domain(ThreadDomain domain) {
    {
        const std::scoped_lock lock(mutex_);
        if (!on_domain_thread_locked(domain)) {
            return;
        }
        domains_[domain.value - 1U].drain_pending = false;
    }
    complete_deferred();
    static_cast<void>(run_posted(maximum_posted_callbacks));
}

void HandleRegistry::end_domain(ThreadDomain domain) noexcept {
    // One object, deferred release or posted callback at a time, each ended
    // without the lock (their destructors may call back in), so nothing is
    // allocated here.
    std::size_t cursor = 0U;
    for (;;) {
        Component::Ptr ended;
        {
            const std::scoped_lock lock(mutex_);
            while (cursor < objects_.size()) {
                ObjectSlot& slot = objects_[cursor];
                cursor = cursor + 1U;
                if (slot.occupied && !slot.orphaned && slot.domain.value == domain.value) {
                    object_index_.erase(slot.object.get());
                    ended = std::move(slot.object);
                    slot.object.reset();
                    slot.orphaned = true;
                    break;
                }
            }
            if (!ended) {
                std::vector<Component::Ptr>& deferred = domains_[domain.value - 1U].deferred;
                if (!deferred.empty()) {
                    ended = std::move(deferred.back());
                    deferred.pop_back();
                }
            }
        }
        if (!ended) {
            break;
        }
        ended.reset();
    }
    for (;;) {
        PostedCall call{};
        {
            const std::scoped_lock lock(mutex_);
            std::deque<PostedCall>& posted = domains_[domain.value - 1U].posted;
            if (posted.empty()) {
                break;
            }
            call = std::move(posted.front());
            posted.pop_front();
        }
        // The callback is released unrun, here; the posted reference goes.
        call.callback.reset();
        static_cast<void>(release(call.target, SlotIdentity{GF_TABLE_DISPATCH, 0U}));
    }
    WindowDispatcher gone;
    {
        const std::scoped_lock lock(mutex_);
        SubSurfaceDomain& owner = domains_[domain.value - 1U];
        owner.occupied = false;
        owner.thread = std::thread::id{};
        owner.drain_pending = false;
        gone = std::move(owner.dispatcher);
        owner.dispatcher = WindowDispatcher{};
    }
}

// ---------------------------------------------------------------------------
// Object identities.

HandleRegistry::ObjectSlot* HandleRegistry::object_slot_locked(gf_object object) noexcept {
    const Identity identity = decode_identity(object);
    if (identity.slot == 0U || identity.slot > objects_.size()) {
        return nullptr;
    }
    ObjectSlot& slot = objects_[identity.slot - 1U];
    if (!slot.occupied || slot.generation != identity.generation) {
        return nullptr;
    }
    return &slot;
}

gf_status HandleRegistry::find_component(gf_object object, SlotIdentity slot, Component::Ptr& component) {
    if (object == 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the object is zero");
    }
    const std::scoped_lock lock(mutex_);
    const ObjectSlot* found = object_slot_locked(object);
    if (found == nullptr) {
        return fail(GF_STATUS_STALE_HANDLE, slot, "the object has ended (its last reference was released)");
    }
    if ((*found).orphaned) {
        return fail(GF_STATUS_STALE_HANDLE, slot, "the object's sub-surface has stopped");
    }
    if ((*found).domain.value != calling_domain_locked().value) {
        return fail(GF_STATUS_WRONG_THREAD, slot,
                    "the object belongs to another thread (a sub-surface's, or the UI thread)");
    }
    component = (*found).object;
    return GF_STATUS_OK;
}

gf_status HandleRegistry::find_object(gf_object object, SlotIdentity slot, Control::Ptr& control) {
    Component::Ptr component;
    const gf_status found = find_component(object, slot, component);
    if (found != GF_STATUS_OK) {
        return found;
    }
    Control::Ptr typed = std::dynamic_pointer_cast<Control>(component);
    if (!typed) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, slot, "the object is a component, not a control");
    }
    control = std::move(typed);
    return GF_STATUS_OK;
}

gf_object HandleRegistry::add_reference(const Component::Ptr& component) {
    const std::scoped_lock lock(mutex_);
    const std::unordered_map<const Component*, std::uint32_t>::const_iterator existing =
        object_index_.find(component.get());
    if (existing != object_index_.end()) {
        ObjectSlot& slot = objects_[(*existing).second - 1U];
        ++slot.references;
        const gf_object reused = encode_identity(Identity{(*existing).second, slot.generation});
        return reused;
    }
    // Reserve every allocation before changing anything, so std::bad_alloc
    // leaves the registry as it was.
    std::uint32_t index = 0U;
    if (free_objects_.empty()) {
        objects_.reserve(objects_.size() + 1U);
        free_objects_.reserve(objects_.size() + 1U);
        index = static_cast<std::uint32_t>(objects_.size() + 1U);
    } else {
        index = free_objects_.back();
    }
    object_index_.reserve(object_index_.size() + 1U);
    object_index_.emplace(component.get(), index);
    if (free_objects_.empty()) {
        objects_.emplace_back();
    } else {
        free_objects_.pop_back();
    }
    ObjectSlot& slot = objects_[index - 1U];
    slot.occupied = true;
    slot.orphaned = false;
    slot.domain = calling_domain_locked();
    slot.references = 1U;
    slot.object = component;
    ++live_objects_;
    const gf_object created = encode_identity(Identity{index, slot.generation});
    return created;
}

gf_status HandleRegistry::retain(gf_object object, SlotIdentity slot) {
    if (object == 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the object is zero");
    }
    const std::scoped_lock lock(mutex_);
    ObjectSlot* found = object_slot_locked(object);
    if (found == nullptr) {
        return fail(GF_STATUS_STALE_HANDLE, slot, "the object has ended (its last reference was released)");
    }
    if ((*found).references == std::numeric_limits<std::uint64_t>::max()) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "the object's reference count is at its limit");
    }
    ++(*found).references;
    return GF_STATUS_OK;
}

void HandleRegistry::drop_reference_locked(gf_object object, ObjectSlot& slot, Component::Ptr& ended) {
    --slot.references;
    if (slot.references != 0U) {
        return;
    }
    const Identity identity = decode_identity(object);
    if (slot.object) {
        object_index_.erase(slot.object.get());
    }
    ended = std::move(slot.object);
    slot.object.reset();
    slot.occupied = false;
    slot.orphaned = false;
    slot.generation = next_generation(slot.generation);
    // free_objects_ has capacity for every slot (add_reference reserves it).
    free_objects_.push_back(identity.slot);
    --live_objects_;
}

gf_status HandleRegistry::release(gf_object object, SlotIdentity slot) {
    if (object == 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the object is zero");
    }
    Component::Ptr ended;
    bool on_owner_thread = false;
    bool on_ui_thread = false;
    ThreadDomain owner{};
    WindowDispatcher wake;
    {
        const std::scoped_lock lock(mutex_);
        ObjectSlot* found = object_slot_locked(object);
        if (found == nullptr) {
            return fail(GF_STATUS_STALE_HANDLE, slot, "the object has ended (its last reference was released)");
        }
        owner = (*found).domain;
        on_ui_thread = on_ui_thread_locked();
        // An orphaned identity holds no object: nothing to defer.
        on_owner_thread = (*found).orphaned || on_domain_thread_locked(owner);
        std::vector<Component::Ptr>& deferred = deferred_locked(owner);
        if (!on_owner_thread && (*found).references == 1U) {
            // The control is destroyed on its own thread. Reserve before the
            // identity ends, so a failed allocation changes nothing.
            deferred.reserve(deferred.size() + 1U);
        }
        drop_reference_locked(object, *found, ended);
        if (ended && !on_owner_thread) {
            deferred.push_back(std::move(ended));
            wake = wake_locked(owner);
        }
    }
    if (!on_owner_thread) {
        post_drain(wake, owner);
        return GF_STATUS_OK;
    }
    // On its own thread the control (and any subtree only it held) ends
    // here, without the lock.
    ended.reset();
    if (on_ui_thread) {
        unbind_if_idle();
    }
    return GF_STATUS_OK;
}

gf_object HandleRegistry::identity_of(const Component* component) {
    const std::scoped_lock lock(mutex_);
    const std::unordered_map<const Component*, std::uint32_t>::const_iterator found = object_index_.find(component);
    if (found == object_index_.end()) {
        return 0U;
    }
    const gf_object identity = encode_identity(Identity{(*found).second, objects_[(*found).second - 1U].generation});
    return identity;
}

gf_object HandleRegistry::add_reference_if_known(const Component* component) {
    const std::scoped_lock lock(mutex_);
    const std::unordered_map<const Component*, std::uint32_t>::const_iterator found = object_index_.find(component);
    if (found == object_index_.end()) {
        return 0U;
    }
    ObjectSlot& slot = objects_[(*found).second - 1U];
    if (slot.references == std::numeric_limits<std::uint64_t>::max()) {
        return 0U;
    }
    ++slot.references;
    const gf_object identity = encode_identity(Identity{(*found).second, slot.generation});
    return identity;
}

// ---------------------------------------------------------------------------
// Subscriptions.

HandleRegistry::SubscriptionSlot* HandleRegistry::subscription_slot_locked(gf_subscription subscription) noexcept {
    const Identity identity = decode_identity(subscription);
    if (identity.slot == 0U || identity.slot > subscriptions_.size()) {
        return nullptr;
    }
    SubscriptionSlot& slot = subscriptions_[identity.slot - 1U];
    if (!slot.occupied || slot.generation != identity.generation) {
        return nullptr;
    }
    return &slot;
}

gf_subscription HandleRegistry::reserve_subscription(const Component::Ptr& source) {
    const std::scoped_lock lock(mutex_);
    std::uint32_t index = 0U;
    if (free_subscriptions_.empty()) {
        subscriptions_.reserve(subscriptions_.size() + 1U);
        free_subscriptions_.reserve(subscriptions_.size() + 1U);
        subscriptions_.emplace_back();
        index = static_cast<std::uint32_t>(subscriptions_.size());
    } else {
        index = free_subscriptions_.back();
        free_subscriptions_.pop_back();
    }
    SubscriptionSlot& slot = subscriptions_[index - 1U];
    slot.occupied = true;
    slot.source = source;
    ++live_subscriptions_;
    const gf_subscription reserved = encode_identity(Identity{index, slot.generation});
    return reserved;
}

void HandleRegistry::complete_subscription(gf_subscription subscription, SubscriptionToken token) noexcept {
    SubscriptionToken ended;
    {
        const std::scoped_lock lock(mutex_);
        SubscriptionSlot* slot = subscription_slot_locked(subscription);
        if (slot != nullptr) {
            (*slot).token = std::move(token);
            return;
        }
        // The subscription ended while it was being connected (its source
        // died in the connection); the token goes without the lock.
        ended = std::move(token);
    }
}

void HandleRegistry::end_subscription(gf_subscription subscription) noexcept {
    SubscriptionToken ended;
    {
        const std::scoped_lock lock(mutex_);
        SubscriptionSlot* slot = subscription_slot_locked(subscription);
        if (slot == nullptr) {
            return;
        }
        ended = std::move((*slot).token);
        (*slot).source.reset();
        (*slot).occupied = false;
        (*slot).generation = next_generation((*slot).generation);
        free_subscriptions_.push_back(decode_identity(subscription).slot);
        --live_subscriptions_;
    }
    // Disconnecting a token whose slot is already ending is a no-op; it runs
    // without the lock because a live one would end the binding.
}

void HandleRegistry::abandon_subscription(gf_subscription subscription) noexcept {
    end_subscription(subscription);
}

gf_status HandleRegistry::disconnect(gf_subscription subscription, SlotIdentity slot) {
    if (subscription == 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the subscription is zero");
    }
    // Held past the lock: the last reference to a source must not end under it.
    Component::Ptr source;
    SubscriptionToken ended;
    {
        const std::scoped_lock lock(mutex_);
        SubscriptionSlot* found = subscription_slot_locked(subscription);
        if (found == nullptr) {
            return fail(GF_STATUS_STALE_HANDLE, slot, "the subscription has already ended");
        }
        // The source's event is touched by its own thread only.
        source = (*found).source.lock();
        if (source) {
            const std::unordered_map<const Component*, std::uint32_t>::const_iterator owner =
                object_index_.find(source.get());
            if (owner != object_index_.end() &&
                objects_[(*owner).second - 1U].domain.value != calling_domain_locked().value) {
                return fail(GF_STATUS_WRONG_THREAD, slot,
                            "the subscription's source belongs to another thread (a sub-surface's, or the UI thread)");
            }
        }
        ended = std::move((*found).token);
        (*found).source.reset();
        (*found).occupied = false;
        (*found).generation = next_generation((*found).generation);
        free_subscriptions_.push_back(decode_identity(subscription).slot);
        --live_subscriptions_;
    }
    // Ends the binding (and releases the callback) unless its invoke is
    // running, in which case the release follows that invoke.
    ended.disconnect();
    unbind_if_idle();
    return GF_STATUS_OK;
}

void HandleRegistry::end_disposed_subscriptions() {
    std::vector<SubscriptionToken> ended;
    {
        const std::scoped_lock lock(mutex_);
        ended.reserve(live_subscriptions_);
        std::size_t index = 0U;
        for (index = 0U; index < subscriptions_.size(); ++index) {
            SubscriptionSlot& slot = subscriptions_[index];
            if (!slot.occupied) {
                continue;
            }
            const Component::Ptr source = slot.source.lock();
            if (source && (*source).is_alive()) {
                continue;
            }
            ended.push_back(std::move(slot.token));
            slot.source.reset();
            slot.occupied = false;
            slot.generation = next_generation(slot.generation);
            free_subscriptions_.push_back(static_cast<std::uint32_t>(index + 1U));
            --live_subscriptions_;
        }
    }
    std::size_t index = 0U;
    for (index = 0U; index < ended.size(); ++index) {
        ended[index].disconnect();
    }
}

// ---------------------------------------------------------------------------
// Posted callbacks.

gf_status HandleRegistry::post(gf_object target, std::unique_ptr<CallbackBinding>& callback, SlotIdentity slot) {
    if (target == 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the target is zero");
    }
    WindowDispatcher wake;
    ThreadDomain owner{};
    {
        const std::scoped_lock lock(mutex_);
        ObjectSlot* found = object_slot_locked(target);
        if (found == nullptr) {
            return fail(GF_STATUS_STALE_HANDLE, slot, "the target has ended (its last reference was released)");
        }
        if ((*found).orphaned) {
            return fail(GF_STATUS_STALE_HANDLE, slot, "the target's sub-surface has stopped");
        }
        owner = (*found).domain;
        std::deque<PostedCall>& posted = posted_locked(owner);
        if (posted.size() >= maximum_posted_callbacks) {
            return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "4096 posted callbacks are already waiting");
        }
        if ((*found).references == std::numeric_limits<std::uint64_t>::max()) {
            return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "the target's reference count is at its limit");
        }
        PostedCall call{};
        call.target = target;
        posted.push_back(std::move(call));
        posted.back().callback = std::move(callback);
        ++(*found).references;
        wake = wake_locked(owner);
    }
    post_drain(wake, owner);
    return GF_STATUS_OK;
}

std::uint64_t HandleRegistry::run_posted(std::size_t maximum) {
    complete_deferred();
    std::size_t waiting = 0U;
    ThreadDomain domain{};
    {
        const std::scoped_lock lock(mutex_);
        domain = calling_domain_locked();
        const std::deque<PostedCall>& posted = posted_locked(domain);
        waiting = posted.size() < maximum ? posted.size() : maximum;
    }
    std::uint64_t invoked = 0U;
    std::size_t turn = 0U;
    for (turn = 0U; turn < waiting; ++turn) {
        PostedCall call{};
        Component::Ptr target;
        {
            const std::scoped_lock lock(mutex_);
            std::deque<PostedCall>& posted = posted_locked(domain);
            // A posted callback may run a nested loop (windows.show_modal)
            // whose turns run posted work too, taking calls this pass
            // counted: the pass ends when the queue is empty.
            if (posted.empty()) {
                break;
            }
            call = std::move(posted.front());
            posted.pop_front();
            const ObjectSlot* found = object_slot_locked(call.target);
            // The posted reference keeps the target's identity live.
            target = (*found).object;
        }
        if (target && (*target).is_alive()) {
            const ShadowFrameScope shadow_frame(ShadowFrameKind::posted_callback, frame_subject(target.get()),
                                                std::string_view{}, 0U);
            gf_event event{};
            event.struct_size = sizeof(gf_event);
            event.sender = call.target;
            event.name.data = "invoke";
            event.name.size = 6U;
            (*call.callback).invoke(event);
            ++invoked;
        }
        call.callback.reset();
        target.reset();
        // Gives back the posted reference.
        static_cast<void>(release(call.target, SlotIdentity{GF_TABLE_DISPATCH, 1U}));
    }
    WindowDispatcher wake;
    {
        const std::scoped_lock lock(mutex_);
        if (!posted_locked(domain).empty()) {
            wake = wake_locked(domain);
        }
    }
    post_drain(wake, domain);
    unbind_if_idle();
    return invoked;
}

// ---------------------------------------------------------------------------
// The window run.

gf_status HandleRegistry::begin_run(gf_object root, SlotIdentity slot) {
    const std::scoped_lock lock(mutex_);
    if (running_) {
        return fail(GF_STATUS_INVALID_STATE, slot, "a window is already running");
    }
    running_ = true;
    run_root_ = root;
    return GF_STATUS_OK;
}

void HandleRegistry::set_run_window(const ApplicationWindowHandle& window) {
    const std::scoped_lock lock(mutex_);
    run_window_ = window;
}

void HandleRegistry::set_run_wake(std::function<void()> wake) {
    const std::scoped_lock lock(mutex_);
    run_wake_ = std::move(wake);
    if (!posted_.empty() || !deferred_.empty()) {
        static_cast<void>(wake_locked(ui_domain));
    }
}

void HandleRegistry::end_run() {
    std::function<void()> wake;
    {
        const std::scoped_lock lock(mutex_);
        running_ = false;
        run_root_ = 0U;
        run_window_ = ApplicationWindowHandle{};
        wake.swap(run_wake_);
    }
    // The old wake is destroyed without the lock.
}

gf_status HandleRegistry::running_window(gf_object root, SlotIdentity slot, ApplicationWindowHandle& window) {
    const std::scoped_lock lock(mutex_);
    if (!running_ || run_root_ != root) {
        return fail(GF_STATUS_INVALID_STATE, slot, "the object is not the root of the running window");
    }
    window = run_window_;
    return GF_STATUS_OK;
}

gf_status HandleRegistry::current_run(SlotIdentity slot, gf_object& root, ApplicationWindowHandle& window) {
    const std::scoped_lock lock(mutex_);
    if (!running_) {
        return fail(GF_STATUS_INVALID_STATE, slot, "no window is running");
    }
    root = run_root_;
    window = run_window_;
    return GF_STATUS_OK;
}

// ---------------------------------------------------------------------------
// Shared helpers.

HandleRegistry& registry() noexcept {
    // Never destroyed: see abi_registry.hpp.
    static HandleRegistry* const instance = new HandleRegistry();
    return *instance;
}

gf_status fail_with_current_exception(SlotIdentity slot) noexcept {
    try {
        throw;
    } catch (const std::bad_alloc&) {
        return fail(GF_STATUS_OUT_OF_MEMORY, slot, "out of memory");
    } catch (const std::invalid_argument& error) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, error.what());
    } catch (const std::out_of_range& error) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, error.what());
    } catch (const std::logic_error& error) {
        return fail(GF_STATUS_INVALID_STATE, slot, error.what());
    } catch (const std::exception& error) {
        return fail(GF_STATUS_INTERNAL, slot, error.what());
    } catch (...) {
        return fail(GF_STATUS_INTERNAL, slot, "an unknown exception reached the C ABI");
    }
}

gf_status read_text(gf_utf8_view view, SlotIdentity slot, std::string_view& text) noexcept {
    if (view.data == nullptr && view.size != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "text has a size but no data");
    }
    if (view.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "text is larger than the address space");
    }
    const std::string_view candidate = view.size == 0U
        ? std::string_view{} : std::string_view(view.data, static_cast<std::size_t>(view.size));
    std::size_t index = 0U;
    while (index < candidate.size()) {
        const std::size_t length = utf8_sequence_length(candidate, index);
        if (length == 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "text is not well-formed UTF-8");
        }
        index += length;
    }
    text = candidate;
    return GF_STATUS_OK;
}

gf_status write_text(std::string_view text, char* buffer, std::uint64_t capacity, std::uint64_t* required,
                     SlotIdentity slot) noexcept {
    if (required == nullptr || (buffer == nullptr && capacity != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a text output needs a size output, and a buffer "
                                                      "unless its capacity is zero");
    }
    const std::uint64_t size = text.size();
    *required = size;
    if (capacity < size) {
        return fail(GF_STATUS_BUFFER_TOO_SMALL, slot, "the buffer is smaller than the text");
    }
    if (size != 0U) {
        std::memcpy(buffer, text.data(), text.size());
    }
    return GF_STATUS_OK;
}

} // namespace gui_forms::abi1
