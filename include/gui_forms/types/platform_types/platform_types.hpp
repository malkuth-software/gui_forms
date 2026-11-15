#pragma once

// The platform types GUI.Forms' public headers name (STXlib swap-in A2, T1):
// owned aliases, so the headers spell GUI.Forms' names, not the standard
// library's, and the move to STXlib's own types after 1.0 (T2:
// stxlib::Path, IntervalTime, Thread, Mutex and ThreadIdentity) changes
// this header, not every site. Each is today exactly the standard type, as
// BorrowedSpan is std::span in an ordinary build: same layout, same
// behaviour, interchangeable at every call.

#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>

namespace gui_forms {

// A file system path (a font, a light-plane or a resource directory, an
// executable to host, a sound file).
using FilePath = std::filesystem::path;

// A point on the monotonic clock: intervals between two of them are
// meaningful, the points themselves are not times of day.
using IntervalPoint = std::chrono::steady_clock::time_point;

// A thread the library starts and joins (a sub-surface's).
using Thread = std::thread;

// A mutual-exclusion lock.
using Mutex = std::mutex;

// A thread's identity (an owner thread, a UI thread); default-constructed,
// no thread.
using ThreadId = std::thread::id;

} // namespace gui_forms
