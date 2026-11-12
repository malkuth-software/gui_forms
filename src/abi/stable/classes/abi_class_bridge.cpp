// The bridge's table-dependent half: validating a caller's class table,
// reading its entries, running an entry inside a frame, and the base
// functions' common body. See abi_class_bridge.hpp and gf_abi_classes.h.

#include "abi_class_bridge.hpp"

#include "../registry/abi_registry.hpp"

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"

#include <cstring>
#include <limits>

namespace gui_forms::abi1 {
namespace {

// Base functions report their failures under base_raiser's slot: they are
// the functions base_raiser hands out.
constexpr SlotIdentity base_slot{GF_TABLE_CLASSES, 1U};

// The innermost entry this thread runs (HookFrame); frames are pushed and
// popped by enter_hook alone, in stack order.
thread_local HookFrame* top_frame = nullptr;

// The first ABI minor with class tables.
constexpr std::uint32_t first_class_minor = 3U;

bool holds_class(std::span<const gf_class_id> chain, gf_class_id class_id) noexcept {
    std::size_t index = 0U;
    for (index = 0U; index < chain.size(); ++index) {
        if (chain[index] == class_id) {
            return true;
        }
    }
    return false;
}

} // namespace

gf_status ClassTableView::read(const gf_class_header* table, std::span<const gf_class_id> chain, SlotIdentity slot,
                               ClassTableView& view) noexcept {
    view = ClassTableView{};
    if (table == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "create needs a class table");
    }
    // struct_size first: the rest of the header is read only once it is
    // known to be there.
    const std::uint32_t header_size = (*table).struct_size;
    if (header_size < class_header_size_1_3 || header_size % 8U != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "a class table's header is at least the 1.3 gf_class_header, a multiple of 8 bytes");
    }
    if ((*table).abi_minor < first_class_minor) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a class table is compiled with ABI 1.3 or later");
    }
    if ((*table).abi_minor > GF_ABI_MINOR) {
        return fail(GF_STATUS_UNSUPPORTED_VERSION, slot, "the class table is newer than this library");
    }
    if ((*table).segment_count > chain.size()) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the class table has more segments than its class's chain");
    }
    ClassTableView read_view{};
    const unsigned char* pointers = reinterpret_cast<const unsigned char*>(table) + header_size;
    std::uint32_t index = 0U;
    for (index = 0U; index < (*table).segment_count; ++index) {
        const void* pointer = nullptr;
        std::memcpy(&pointer, pointers + static_cast<std::size_t>(index) * sizeof(pointer), sizeof(pointer));
        if (pointer == nullptr) {
            // A null segment pointer is no override.
            continue;
        }
        gf_segment_header segment{};
        std::memcpy(&segment.struct_size, pointer, sizeof(segment.struct_size));
        if (segment.struct_size < segment_header_bytes) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a segment is at least its 8-byte header");
        }
        std::memcpy(&segment, pointer, sizeof(segment));
        if (!holds_class(chain, segment.class_id)) {
            return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a segment's class_id is not of the class's chain");
        }
        std::size_t known = 0U;
        for (known = 0U; known < read_view.segment_count_; ++known) {
            if (read_view.segments_[known].class_id == segment.class_id) {
                return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a class table holds a class's segment once");
            }
        }
        Segment& stored = read_view.segments_[read_view.segment_count_];
        stored.class_id = segment.class_id;
        stored.struct_size = segment.struct_size;
        stored.bytes = static_cast<const unsigned char*>(pointer);
        ++read_view.segment_count_;
    }
    read_view.release_ = (*table).release;
    view = read_view;
    return GF_STATUS_OK;
}

gf_hook_entry ClassTableView::entry(HookKey hook) const noexcept {
    constexpr std::uint32_t last_entry = (std::numeric_limits<std::uint32_t>::max() - segment_header_bytes) /
                                         hook_entry_bytes - 1U;
    if (hook.entry > last_entry) {
        return nullptr;
    }
    const std::uint32_t offset = segment_header_bytes + hook.entry * hook_entry_bytes;
    std::size_t index = 0U;
    for (index = 0U; index < segment_count_; ++index) {
        const Segment& segment = segments_[index];
        if (segment.class_id != hook.class_id) {
            continue;
        }
        if (segment.struct_size < offset + hook_entry_bytes) {
            // An entry the caller's segment predates: absent.
            return nullptr;
        }
        gf_hook_entry found = nullptr;
        std::memcpy(&found, segment.bytes + offset, sizeof(found));
        return found;
    }
    return nullptr;
}

ClassBridge::~ClassBridge() {
    const ClassRelease release = table_.release();
    if (armed_ && release != nullptr) {
        armed_ = false;
        release(instance_);
    }
}

void ClassBridge::bind(const ClassTableView& view, void* instance) noexcept {
    table_ = view;
    instance_ = instance;
}

HookFrame* ClassBridge::find_frame(const gf_event* event) noexcept {
    HookFrame* frame = top_frame;
    while (frame != nullptr && (*frame).event != event) {
        frame = (*frame).previous;
    }
    return frame;
}

gf_hook_entry ClassBridge::hook_entry(HookKey hook) const noexcept {
    if (!armed_) {
        return nullptr;
    }
    const gf_hook_entry found = table_.entry(hook);
    return found;
}

bool ClassBridge::enter_hook(Control& control, gf_hook_entry entry, HookKey hook, std::string_view property,
                             gf_base_raiser base) {
    if (!control.is_alive()) {
        return false;
    }
    // The entry may drop the caller's last reference; the object lives until
    // the entry and the raise below it are done.
    const std::shared_ptr<Control> keep_alive = control.weak_from_this().lock();
    const gf_object self = registry().identity_of(&control);
    gf_event event{};
    event.struct_size = static_cast<std::uint32_t>(sizeof(gf_event));
    event.sender = self;
    event.name.data = property.data();
    event.name.size = property.size();
    HookFrame frame{};
    frame.bridge = this;
    frame.hook = hook;
    frame.event = &event;
    frame.previous = top_frame;
    top_frame = &frame;
    // A C function: it does not throw (the C# entries catch everything).
    entry(instance_, self, &event, base);
    top_frame = frame.previous;
    if (frame.failure) {
        const std::exception_ptr failure = std::move(frame.failure);
        std::rethrow_exception(failure);
    }
    return true;
}

gf_status run_class_base(gf_object self, gf_event* event, HookKey hook) noexcept {
    try {
        const ShadowFrameScope shadow_frame(base_slot.table_id, base_slot.slot_index);
        if (event == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, base_slot, "a base function needs an event");
        }
        HookFrame* frame = ClassBridge::find_frame(event);
        if (frame != nullptr && (*frame).hook.class_id == hook.class_id && (*frame).hook.entry == hook.entry) {
            ClassBridge& framed = *(*frame).bridge;
            if (!framed.bridged_control().is_alive()) {
                return fail(GF_STATUS_STALE_HANDLE, base_slot, "the object was disposed");
            }
            try {
                const bool framed_ran = framed.run_base(hook);
                static_cast<void>(framed_ran);
            } catch (...) {
                // Rethrown once the entry returns (enter_hook); the first
                // failure of the frame wins.
                if (!(*frame).failure) {
                    (*frame).failure = std::current_exception();
                }
                const gf_status failed = fail_with_current_exception(base_slot);
                return failed;
            }
            return GF_STATUS_OK;
        }
        Component::Ptr component;
        const gf_status entered = registry().enter_component(self, base_slot, component);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        ClassBridge* bridge = dynamic_cast<ClassBridge*>(component.get());
        if (bridge == nullptr) {
            return fail(GF_STATUS_WRONG_HANDLE_KIND, base_slot, "the object was not made from a class table");
        }
        if (!(*bridge).bridged_control().is_alive()) {
            return fail(GF_STATUS_STALE_HANDLE, base_slot, "the object was disposed");
        }
        const bool ran = (*bridge).run_base(hook);
        if (!ran) {
            return fail(GF_STATUS_WRONG_HANDLE_KIND, base_slot, "the object's class has no such segment");
        }
        return GF_STATUS_OK;
    } catch (...) {
        const gf_status failed = fail_with_current_exception(base_slot);
        return failed;
    }
}

} // namespace gui_forms::abi1
