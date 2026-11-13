#pragma once

// Pure-C++ fixture model for the GUI.Forms Correspondence demonstration.
// It owns no controls and performs no I/O: every folder and message is a
// deterministic in-memory fixture, so the model is unit-testable on its own
// and the retained tree in correspondence_controls.* only projects it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::correspondence {

enum class Folder : std::uint8_t {
    inbox,
    drafts,
    sent,
    archive,
    junk,
    trash,
};

inline constexpr std::array<Folder, 6> all_folders{
    Folder::inbox, Folder::drafts, Folder::sent,
    Folder::archive, Folder::junk, Folder::trash};

// Display name ("Inbox") and stable-ID token ("inbox") for a folder.
[[nodiscard]] std::string_view folder_name(Folder folder) noexcept;
[[nodiscard]] std::string_view folder_token(Folder folder) noexcept;
[[nodiscard]] std::optional<Folder> folder_from_token(std::string_view token) noexcept;

struct Message final {
    std::uint32_t id{};
    Folder folder{Folder::inbox};
    // The other party. For incoming mail this is the sender; for drafts and
    // sent mail it is the recipient (see `outgoing`).
    std::string correspondent;
    std::string address;
    std::string subject;
    // Fixed ISO-like timestamp text; the model never reads a clock.
    std::string date;
    std::string preview;
    std::string body;
    bool outgoing{};
    bool read{};
    bool flagged{};
};

// The checked-in deterministic fixture: forty messages across six folders.
// Two calls return equal vectors.
[[nodiscard]] std::vector<Message> fixture_messages();

// Case-insensitive ASCII substring test shared by the search filter and tests.
[[nodiscard]] bool contains_ignoring_case(std::string_view haystack,
                                          std::string_view needle) noexcept;

class CorrespondenceModel final {
public:
    CorrespondenceModel();

    [[nodiscard]] std::span<const Message> messages() const& noexcept { return messages_; }
    std::span<const Message> messages() const&& = delete;
    [[nodiscard]] const Message* find(std::uint32_t id) const noexcept;

    [[nodiscard]] Folder selected_folder() const noexcept { return folder_; }
    // Changing folder clears the message selection and keeps the search text.
    bool select_folder(Folder folder);

    [[nodiscard]] std::optional<std::uint32_t> selected_message() const noexcept {
        return selected_;
    }
    // Selecting a visible message marks it read. An ID outside the visible
    // list is refused. nullopt clears the selection.
    bool select_message(std::optional<std::uint32_t> id);

    bool mark_read(std::uint32_t id, bool read);
    bool toggle_flag(std::uint32_t id);
    // Moves a message into Archive. Refused when it is already archived.
    bool archive(std::uint32_t id);
    // Moves a message into Trash; a message already in Trash is removed for
    // good. Both forms are "delete" from the user's point of view.
    bool delete_message(std::uint32_t id);

    [[nodiscard]] std::size_t count(Folder folder) const noexcept;
    [[nodiscard]] std::size_t unread_count(Folder folder) const noexcept;
    [[nodiscard]] std::size_t total_count() const noexcept { return messages_.size(); }

    [[nodiscard]] const std::string& search() const& noexcept { return search_; }
    const std::string& search() const&& = delete;
    // Filters the current folder on correspondent, subject, and preview. A
    // selected message that stops matching is deselected.
    bool set_search(std::string text);
    [[nodiscard]] bool matches_search(const Message& message) const noexcept;
    // IDs in the selected folder that pass the search filter, newest first.
    [[nodiscard]] std::vector<std::uint32_t> visible_messages() const;

    // Compose operations. No secondary window is opened: each creates a
    // Drafts fixture, switches to Drafts, and selects the new draft so the
    // reading pane shows it. They never read a clock.
    std::uint32_t create_draft();
    std::optional<std::uint32_t> reply_to(std::uint32_t id);
    std::optional<std::uint32_t> forward(std::uint32_t id);

    // Monotonic observable mutation counter for tests and diagnostics.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    [[nodiscard]] Message* find_mutable(std::uint32_t id) noexcept;
    [[nodiscard]] std::optional<std::uint32_t> neighbour_of(std::uint32_t id) const;
    bool move_to(std::uint32_t id, Folder destination);
    std::uint32_t add_draft(std::string correspondent, std::string address,
                            std::string subject, std::string body);
    void bump() noexcept { ++revision_; }

    std::vector<Message> messages_;
    Folder folder_{Folder::inbox};
    std::optional<std::uint32_t> selected_;
    std::string search_;
    std::uint32_t next_id_{1U};
    std::uint64_t revision_{};
};

}  // namespace gui_forms::correspondence
