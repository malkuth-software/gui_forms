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

gf_status HandleRegistry::enter_ui(SlotIdentity slot, bool bind) {
    bool bound = false;
    {
        const std::scoped_lock lock(mutex_);
        if (ui_thread_ != std::thread::id{} && !on_ui_thread_locked()) {
            return fail(GF_STATUS_WRONG_THREAD, slot, "this function runs on the GUI.Forms UI thread only");
        }
        bound = ui_thread_ != std::thread::id{};
    }
    if (bound) {
        // May unbind the library when nothing is left.
        complete_deferred();
    }
    if (bind) {
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
        ended.swap(deferred_);
    }
    // Destroying the controls may end subscriptions, which takes the lock.
    ended.clear();
    unbind_if_idle();
}

void HandleRegistry::unbind_if_idle() {
    const std::scoped_lock lock(mutex_);
    const bool idle = live_objects_ == 0U && live_subscriptions_ == 0U && posted_.empty() &&
                      deferred_.empty() && !running_ && !binding_held_;
    if (idle) {
        ui_thread_ = std::thread::id{};
    }
}

void HandleRegistry::hold_binding() noexcept {
    const std::scoped_lock lock(mutex_);
    binding_held_ = true;
}

void HandleRegistry::wake_locked() noexcept {
    if (run_wake_) {
        run_wake_();
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
    object_index_.erase(slot.object.get());
    ended = std::move(slot.object);
    slot.object.reset();
    slot.occupied = false;
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
    bool on_ui_thread = false;
    {
        const std::scoped_lock lock(mutex_);
        ObjectSlot* found = object_slot_locked(object);
        if (found == nullptr) {
            return fail(GF_STATUS_STALE_HANDLE, slot, "the object has ended (its last reference was released)");
        }
        on_ui_thread = on_ui_thread_locked();
        if (!on_ui_thread && (*found).references == 1U) {
            // The control is destroyed on the UI thread. Reserve before the
            // identity ends, so a failed allocation changes nothing.
            deferred_.reserve(deferred_.size() + 1U);
        }
        drop_reference_locked(object, *found, ended);
        if (ended && !on_ui_thread) {
            deferred_.push_back(std::move(ended));
            wake_locked();
            return GF_STATUS_OK;
        }
    }
    // On the UI thread the control (and any subtree only it held) ends here,
    // without the lock.
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
    SubscriptionToken ended;
    {
        const std::scoped_lock lock(mutex_);
        SubscriptionSlot* found = subscription_slot_locked(subscription);
        if (found == nullptr) {
            return fail(GF_STATUS_STALE_HANDLE, slot, "the subscription has already ended");
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
    const std::scoped_lock lock(mutex_);
    ObjectSlot* found = object_slot_locked(target);
    if (found == nullptr) {
        return fail(GF_STATUS_STALE_HANDLE, slot, "the target has ended (its last reference was released)");
    }
    if (posted_.size() >= maximum_posted_callbacks) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "4096 posted callbacks are already waiting");
    }
    if ((*found).references == std::numeric_limits<std::uint64_t>::max()) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "the target's reference count is at its limit");
    }
    PostedCall call{};
    call.target = target;
    posted_.push_back(std::move(call));
    posted_.back().callback = std::move(callback);
    ++(*found).references;
    wake_locked();
    return GF_STATUS_OK;
}

std::uint64_t HandleRegistry::run_posted(std::size_t maximum) {
    complete_deferred();
    std::size_t waiting = 0U;
    {
        const std::scoped_lock lock(mutex_);
        waiting = posted_.size() < maximum ? posted_.size() : maximum;
    }
    std::uint64_t invoked = 0U;
    std::size_t turn = 0U;
    for (turn = 0U; turn < waiting; ++turn) {
        PostedCall call{};
        Component::Ptr target;
        {
            const std::scoped_lock lock(mutex_);
            call = std::move(posted_.front());
            posted_.pop_front();
            const ObjectSlot* found = object_slot_locked(call.target);
            // The posted reference keeps the target's identity live.
            target = (*found).object;
        }
        if ((*target).is_alive()) {
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
    {
        const std::scoped_lock lock(mutex_);
        if (!posted_.empty()) {
            wake_locked();
        }
    }
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
        wake_locked();
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
