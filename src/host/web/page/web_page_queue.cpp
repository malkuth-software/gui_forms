// The page-to-program queue (web_page_queue.hpp).
#include "web_page_queue.hpp"

#include <utility>

namespace gui_forms::host::web_detail {

bool PageQueue::post(PageRecord record) {
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        if (records_.size() >= maximum_records) {
            ++dropped_;
            return false;
        }
        records_.push_back(std::move(record));
    }
    ready_.notify_one();
    return true;
}

void PageQueue::wake() noexcept {
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        woken_ = true;
    }
    ready_.notify_one();
}

bool PageQueue::take(std::deque<PageRecord>& taken,
                     std::optional<std::chrono::steady_clock::time_point> deadline) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (records_.empty() && !woken_) {
        if (!deadline.has_value()) {
            ready_.wait(lock);
            continue;
        }
        const std::cv_status status = ready_.wait_until(lock, *deadline);
        if (status == std::cv_status::timeout) break;
    }
    const bool any = !records_.empty() || woken_;
    while (!records_.empty()) {
        taken.push_back(std::move(records_.front()));
        records_.pop_front();
    }
    woken_ = false;
    return any;
}

std::uint64_t PageQueue::dropped() const {
    const std::lock_guard<std::mutex> guard(mutex_);
    const std::uint64_t count = dropped_;
    return count;
}

PageQueue& page_queue() noexcept {
    // Constructed on first use, from whichever thread posts first; never
    // destroyed, so a late post from the page after the run ended finds it.
    static PageQueue* const queue = new PageQueue();
    return *queue;
}

} // namespace gui_forms::host::web_detail
