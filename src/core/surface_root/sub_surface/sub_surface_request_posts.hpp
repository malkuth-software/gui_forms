#pragma once

// Private to the sub-surface's requesters (tooltips and alerts): the work
// they post to the parent's dispatcher, outside the channel's lock.

#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"

#include <cstdint>
#include <memory>
#include <utility>

namespace gui_forms::detail {

// Posted to the parent's dispatcher with one request: delivers it on the
// parent's thread, unless the parent released the target first.
class DeliverSubSurfaceRequest final {
public:
    DeliverSubSurfaceRequest(std::weak_ptr<SubSurfaceRequestTarget> target, SubSurfaceRequest request) noexcept
        : target_(std::move(target)), request_(std::move(request)) {}
    void operator()() const {
        const std::shared_ptr<SubSurfaceRequestTarget> target = target_.lock();
        if (!target) return;
        (*target).receive_request(request_);
    }

private:
    std::weak_ptr<SubSurfaceRequestTarget> target_;
    SubSurfaceRequest request_{};
};

// Posted the same way with the id of a request the sub-surface revoked.
class DeliverSubSurfaceRevoke final {
public:
    DeliverSubSurfaceRevoke(std::weak_ptr<SubSurfaceRequestTarget> target, std::uint64_t id) noexcept
        : target_(std::move(target)), id_(id) {}
    void operator()() const {
        const std::shared_ptr<SubSurfaceRequestTarget> target = target_.lock();
        if (!target) return;
        (*target).receive_revoke(id_);
    }

private:
    std::weak_ptr<SubSurfaceRequestTarget> target_;
    std::uint64_t id_{};
};

} // namespace gui_forms::detail
