#include "windows_text_automation.hpp"

#include "windows_automation.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The TextPattern's providers (see windows_text_automation.hpp for what
// they publish, their lifetime and their errors).

namespace gui_forms::host {

namespace {

const HRESULT element_not_available = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
const HRESULT invalid_operation = static_cast<HRESULT>(UIA_E_INVALIDOPERATION);

// The interface by which a range finds the object behind another range a
// client passes back (Compare, CompareEndpoints, MoveEndpointByRange): only
// UiaTextRange answers it, so a range from another provider is refused.
// QueryInterface for it returns the UiaTextRange itself, with a reference.
const IID iid_gui_forms_text_range = {
    0x5d0c1c8eU, 0x3b5fU, 0x4a49U, {0x9eU, 0x31U, 0x6aU, 0x2fU, 0x58U, 0x0bU, 0x7cU, 0x14U}};

// -- one call's view of the text --------------------------------------------

// A text node as one call reads it: the shared snapshot, the node's location
// in it (which borrows the tree `client` keeps alive), the node's layout,
// and scratch storage for unit boundaries.
struct TextRead final {
    SemanticTreeRef client;
    SemanticLocation self;
    SemanticTextLayout layout;
    std::vector<Utf8Offset> boundaries;

    [[nodiscard]] const std::string& value() const noexcept { return (*self.node).value; }
};

// Fills `read` for the element `id`: a fresh snapshot and the node's
// layout. Throws std::bad_alloc when memory runs out.
HRESULT read_text(SemanticSource& source, SemanticElementId id, TextRead& read) {
    if (!source.snapshot(read.client)) return element_not_available;
    if (!locate_semantic_element(*read.client, id, read.self)) return element_not_available;
    if (read.self.parent == nullptr || !text_pattern(*read.self.node)) return invalid_operation;
    if (source.model == nullptr) return element_not_available;
    const RuntimeId control{(*read.self.node).runtime_id};
    const SemanticTextResult result = (*source.model).semantic_text_layout(control, read.layout);
    if (result == SemanticTextResult::stale_identity) return element_not_available;
    if (result != SemanticTextResult::done) return invalid_operation;
    return S_OK;
}

// `offset` moved into `value`: to its end, then back to the start of a
// character, so that a range kept across a change of text stays valid.
std::size_t clamp_offset(std::string_view value, std::size_t offset) noexcept {
    std::size_t result = std::min(offset, value.size());
    while (result > 0U && result < value.size() &&
           (static_cast<unsigned char>(value[result]) & 0xC0U) == 0x80U) {
        --result;
    }
    return result;
}

// The unit the core moves by for a UI Automation unit: Format and Page are
// the whole text (it has one format and no pages).
SemanticTextUnit semantic_unit(TextUnit unit) noexcept {
    switch (unit) {
    case TextUnit_Character: return SemanticTextUnit::character;
    case TextUnit_Word: return SemanticTextUnit::word;
    case TextUnit_Line: return SemanticTextUnit::line;
    case TextUnit_Paragraph: return SemanticTextUnit::paragraph;
    case TextUnit_Format:
    case TextUnit_Page:
    case TextUnit_Document: return SemanticTextUnit::document;
    default:
        // A client may send any integer as a TextUnit: an unknown unit moves
        // by the whole text, as Document does.
        return SemanticTextUnit::document;
    }
}

// The first boundary after `offset`, or none.
std::optional<std::size_t> boundary_after(const std::vector<Utf8Offset>& boundaries,
                                          std::size_t offset) noexcept {
    for (const Utf8Offset boundary : boundaries) {
        if (boundary.value() > offset) return boundary.value();
    }
    return std::nullopt;
}

// The last boundary before `offset`, or none.
std::optional<std::size_t> boundary_before(const std::vector<Utf8Offset>& boundaries,
                                           std::size_t offset) noexcept {
    std::optional<std::size_t> found;
    for (const Utf8Offset boundary : boundaries) {
        if (boundary.value() >= offset) break;
        found = boundary.value();
    }
    return found;
}

// The unit that holds `offset`: from the last boundary at or before it to
// the next. At the end of non-empty text, the last unit.
void enclosing_unit(const std::vector<Utf8Offset>& boundaries, std::size_t size, std::size_t offset,
                    std::size_t& start, std::size_t& end) noexcept {
    std::size_t low = 0U;
    for (const Utf8Offset boundary : boundaries) {
        if (boundary.value() > offset) break;
        low = boundary.value();
    }
    if (low == size && size > 0U) {
        const std::optional<std::size_t> before = boundary_before(boundaries, size);
        low = before.value_or(0U);
    }
    const std::optional<std::size_t> high = boundary_after(boundaries, low);
    start = low;
    end = high.value_or(size);
}

// Moves `offset` across `count` boundaries (backward when negative) and
// returns how many it crossed. `stop_before_end` keeps it off the end of
// the text, where no unit starts (a non-degenerate range's Move).
int move_across(const std::vector<Utf8Offset>& boundaries, std::size_t size, int count,
                bool stop_before_end, std::size_t& offset) noexcept {
    int moved = 0;
    while (moved < count) {
        const std::optional<std::size_t> next = boundary_after(boundaries, offset);
        if (!next || (stop_before_end && *next >= size)) break;
        offset = *next;
        ++moved;
    }
    while (moved > count) {
        const std::optional<std::size_t> previous = boundary_before(boundaries, offset);
        if (!previous) break;
        offset = *previous;
        --moved;
    }
    return moved;
}

// A logical rectangle in screen pixels.
UiaRect to_screen(const SemanticSource& source, Rect bounds) noexcept {
    POINT origin{};
    ClientToScreen(source.hwnd, &origin);
    const double scale = source.scale;
    UiaRect result{};
    result.left = origin.x + std::round(bounds.x * scale);
    result.top = origin.y + std::round(bounds.y * scale);
    result.width = std::round(bounds.width * scale);
    result.height = std::round(bounds.height * scale);
    return result;
}

// The boxes of [start, end) of `read`'s text, one per visual line it
// touches, cut to the node's bounds; a degenerate range is the caret's box,
// one device pixel wide. Logical coordinates.
void range_boxes(const TextRead& read, std::size_t start, std::size_t end, double scale,
                 std::vector<Rect>& boxes) {
    boxes.clear();
    const Rect visible = (*read.self.node).bounds;
    const std::vector<SemanticTextLine>& lines = read.layout.lines;
    if (start == end) {
        if (lines.empty()) return;
        std::size_t line = 0U;
        double x = lines[0].bounds.x;
        bool placed = false;
        for (const SemanticTextGrapheme& grapheme : read.layout.graphemes) {
            if (grapheme.range.start.value() == start) {
                line = grapheme.line.value();
                x = grapheme.bounds.x;
                placed = true;
                break;
            }
        }
        if (!placed) {
            for (const SemanticTextGrapheme& grapheme : read.layout.graphemes) {
                if (grapheme.range.end.value() == start) {
                    line = grapheme.line.value();
                    x = grapheme.bounds.x + grapheme.bounds.width;
                }
            }
        }
        if (line >= lines.size()) return;
        const Rect caret{x, lines[line].bounds.y, 1.0 / scale, lines[line].bounds.height};
        const Rect shown = Rect::intersection(caret, visible);
        if (!shown.empty()) boxes.push_back(shown);
        return;
    }
    for (std::size_t line = 0U; line < lines.size(); ++line) {
        std::optional<Rect> united;
        for (const SemanticTextGrapheme& grapheme : read.layout.graphemes) {
            if (grapheme.line.value() != line) continue;
            if (grapheme.range.start.value() < start || grapheme.range.end.value() > end) continue;
            united = united ? Rect::united(*united, grapheme.bounds) : grapheme.bounds;
        }
        if (!united) continue;
        const Rect shown = Rect::intersection(*united, visible);
        if (!shown.empty()) boxes.push_back(shown);
    }
}

// ASCII letters folded to lower case, for FindText's ignoreCase (other
// letters are compared as they are).
//
// This is a known limit (audit P8): GUI.Forms carries no Unicode case
// data yet, and the host's case mapping is not used because our engine,
// not the platform, defines text behaviour. So with ignoreCase U+00C9
// does not match U+00E9, and a match is aligned to UTF-8 sequences but not to
// graphemes ("e" matches the base of "e" plus a combining accent). Full
// folding and grapheme snapping wait for case data in the text engine.
void fold_ascii_case(std::string& text) noexcept {
    for (char& character : text) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    }
}

// -- the range ----------------------------------------------------------------

class UiaTextRange final : public ITextRangeProvider {
public:
    UiaTextRange(std::shared_ptr<SemanticSource> source, SemanticElementId id, std::size_t start,
                 std::size_t end) noexcept
        : source_(std::move(source)), id_(id), start_(start), end_(end) {}
    UiaTextRange(const UiaTextRange&) = delete;
    UiaTextRange& operator=(const UiaTextRange&) = delete;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // ITextRangeProvider
    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** out) override;
    HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* range, BOOL* out) override;
    HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint endpoint,
                                               ITextRangeProvider* target,
                                               TextPatternRangeEndpoint target_endpoint,
                                               int* out) override;
    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override;
    HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID attribute, VARIANT value, BOOL backward,
                                            ITextRangeProvider** out) override;
    HRESULT STDMETHODCALLTYPE FindText(BSTR text, BOOL backward, BOOL ignore_case,
                                       ITextRangeProvider** out) override;
    HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID attribute, VARIANT* out) override;
    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple** out) override;
    HRESULT STDMETHODCALLTYPE GetText(int max_length, BSTR* out) override;
    HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int* out) override;
    HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit,
                                                 int count, int* out) override;
    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint endpoint,
                                                  ITextRangeProvider* target,
                                                  TextPatternRangeEndpoint target_endpoint) override;
    HRESULT STDMETHODCALLTYPE Select() override;
    HRESULT STDMETHODCALLTYPE AddToSelection() override;
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
    HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL align_to_top) override;
    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** out) override;

private:
    ~UiaTextRange() = default;

    // Reads the text into `read` and clamps this range to it. Throws
    // std::bad_alloc when memory runs out.
    HRESULT read(TextRead& read);
    // The UiaTextRange behind `range` when it is one of this element's,
    // with a reference the caller releases; null otherwise.
    UiaTextRange* same_element_range(ITextRangeProvider* range) const noexcept;
    [[nodiscard]] std::size_t endpoint_offset(TextPatternRangeEndpoint which) const noexcept {
        return which == TextPatternRangeEndpoint_Start ? start_ : end_;
    }
    // Sets one endpoint; the other follows it when they would cross.
    void set_endpoint(TextPatternRangeEndpoint which, std::size_t offset) noexcept;

    HRESULT find_text(BSTR text, BOOL backward, BOOL ignore_case, ITextRangeProvider** out);
    HRESULT bounding_rectangles(SAFEARRAY** out);
    HRESULT text(int max_length, BSTR* out);
    HRESULT move(TextUnit unit, int count, int* out);
    HRESULT move_endpoint(TextPatternRangeEndpoint which, TextUnit unit, int count, int* out);
    HRESULT expand(TextUnit unit);
    HRESULT scroll_into_view();

    std::atomic<ULONG> references_{1U};
    std::shared_ptr<SemanticSource> source_;
    SemanticElementId id_;
    // UTF-8 byte offsets into the node's value; start_ <= end_.
    std::size_t start_{};
    std::size_t end_{};
};

// A new range, as ITextRangeProvider, in `*out`.
HRESULT make_range(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                   std::size_t start, std::size_t end, ITextRangeProvider** out) noexcept {
    UiaTextRange* range = new (std::nothrow) UiaTextRange(source, id, start, end);
    if (range == nullptr) return E_OUTOFMEMORY;
    *out = static_cast<ITextRangeProvider*>(range);
    return S_OK;
}

// A one-dimensional SAFEARRAY of `ranges` (VT_UNKNOWN), each moved into it,
// in `*out`. Releases every range whether or not it succeeds.
HRESULT range_array(std::vector<ITextRangeProvider*>& ranges, SAFEARRAY** out) noexcept {
    SAFEARRAY* array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(ranges.size()));
    HRESULT status = array == nullptr ? E_OUTOFMEMORY : S_OK;
    for (std::size_t index = 0U; index < ranges.size(); ++index) {
        if (SUCCEEDED(status)) {
            LONG position = static_cast<LONG>(index);
            // SafeArrayPutElement adds its own reference.
            status = SafeArrayPutElement(array, &position, static_cast<IUnknown*>(ranges[index]));
        }
        (*ranges[index]).Release();
    }
    ranges.clear();
    if (FAILED(status)) {
        if (array != nullptr) SafeArrayDestroy(array);
        return status;
    }
    *out = array;
    return S_OK;
}

HRESULT UiaTextRange::read(TextRead& read) {
    const HRESULT status = read_text(*source_, id_, read);
    if (FAILED(status)) return status;
    start_ = clamp_offset(read.value(), start_);
    end_ = clamp_offset(read.value(), end_);
    if (end_ < start_) end_ = start_;
    return S_OK;
}

UiaTextRange* UiaTextRange::same_element_range(ITextRangeProvider* range) const noexcept {
    if (range == nullptr) return nullptr;
    void* found = nullptr;
    if (FAILED((*range).QueryInterface(iid_gui_forms_text_range, &found)) || found == nullptr) return nullptr;
    UiaTextRange* other = static_cast<UiaTextRange*>(found);
    if ((*other).source_ != source_ || (*other).id_ != id_) {
        (*other).Release();
        return nullptr;
    }
    return other;
}

void UiaTextRange::set_endpoint(TextPatternRangeEndpoint which, std::size_t offset) noexcept {
    if (which == TextPatternRangeEndpoint_Start) {
        start_ = offset;
        if (end_ < start_) end_ = start_;
    } else {
        end_ = offset;
        if (start_ > end_) start_ = end_;
    }
}

HRESULT STDMETHODCALLTYPE UiaTextRange::QueryInterface(REFIID iid, void** out) {
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITextRangeProvider) {
        *out = static_cast<ITextRangeProvider*>(this);
    } else if (iid == iid_gui_forms_text_range) {
        *out = this;
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE UiaTextRange::AddRef() {
    const ULONG count = ++references_;
    return count;
}

ULONG STDMETHODCALLTYPE UiaTextRange::Release() {
    const ULONG count = --references_;
    if (count == 0U) delete this;
    return count;
}

HRESULT STDMETHODCALLTYPE UiaTextRange::Clone(ITextRangeProvider** out) {
    if (out == nullptr) return E_INVALIDARG;
    *out = nullptr;
    return make_range(source_, id_, start_, end_, out);
}

HRESULT STDMETHODCALLTYPE UiaTextRange::Compare(ITextRangeProvider* range, BOOL* out) {
    if (out == nullptr || range == nullptr) return E_INVALIDARG;
    *out = FALSE;
    UiaTextRange* other = same_element_range(range);
    if (other == nullptr) return S_OK;
    const bool same = (*other).start_ == start_ && (*other).end_ == end_;
    (*other).Release();
    *out = same ? TRUE : FALSE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE UiaTextRange::CompareEndpoints(TextPatternRangeEndpoint endpoint,
                                                         ITextRangeProvider* target,
                                                         TextPatternRangeEndpoint target_endpoint,
                                                         int* out) {
    if (out == nullptr) return E_INVALIDARG;
    *out = 0;
    UiaTextRange* other = same_element_range(target);
    if (other == nullptr) return E_INVALIDARG;
    const std::size_t mine = endpoint_offset(endpoint);
    const std::size_t theirs = (*other).endpoint_offset(target_endpoint);
    (*other).Release();
    *out = mine < theirs ? -1 : (mine > theirs ? 1 : 0);
    return S_OK;
}

HRESULT UiaTextRange::expand(TextUnit unit) {
    TextRead text;
    const HRESULT status = read(text);
    if (FAILED(status)) return status;
    fill_semantic_text_boundaries(text.layout, text.value(), semantic_unit(unit), text.boundaries);
    enclosing_unit(text.boundaries, text.value().size(), start_, start_, end_);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE UiaTextRange::ExpandToEnclosingUnit(TextUnit unit) {
    try {
        return expand(unit);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

// Only IsReadOnly is published, and it holds for the whole text: a match is
// this range; any other attribute or value finds nothing.
HRESULT STDMETHODCALLTYPE UiaTextRange::FindAttribute(TEXTATTRIBUTEID attribute, VARIANT value, BOOL,
                                                      ITextRangeProvider** out) {
    if (out == nullptr) return E_INVALIDARG;
    *out = nullptr;
    if (attribute != UIA_IsReadOnlyAttributeId || value.vt != VT_BOOL) return S_OK;
    try {
        TextRead text;
        const HRESULT status = read(text);
        if (FAILED(status)) return status;
        const bool read_only = has_semantic_state((*text.self.node).states, SemanticState::read_only);
        const bool wanted = value.boolVal != VARIANT_FALSE;
        if (read_only != wanted) return S_OK;
        return make_range(source_, id_, start_, end_, out);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT UiaTextRange::find_text(BSTR text, BOOL backward, BOOL ignore_case, ITextRangeProvider** out) {
    TextRead read_back;
    const HRESULT status = read(read_back);
    if (FAILED(status)) return status;
    const std::wstring_view wanted_wide(text, SysStringLen(text));
    std::string wanted;
    if (!utf8_from_wide(wanted_wide, wanted)) return E_INVALIDARG;
    if (wanted.empty()) return S_OK;
    std::string within = read_back.value().substr(start_, end_ - start_);
    if (ignore_case != FALSE) {
        fold_ascii_case(within);
        fold_ascii_case(wanted);
    }
    const std::size_t found = backward != FALSE ? within.rfind(wanted) : within.find(wanted);
    if (found == std::string::npos) return S_OK;
    const std::size_t match_start = start_ + found;
    return make_range(source_, id_, match_start, match_start + wanted.size(), out);
}

HRESULT STDMETHODCALLTYPE UiaTextRange::FindText(BSTR text, BOOL backward, BOOL ignore_case,
                                                 ITextRangeProvider** out) {
    if (out == nullptr || text == nullptr) return E_INVALIDARG;
    *out = nullptr;
    try {
        return find_text(text, backward, ignore_case, out);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT STDMETHODCALLTYPE UiaTextRange::GetAttributeValue(TEXTATTRIBUTEID attribute, VARIANT* out) {
    if (out == nullptr) return E_INVALIDARG;
    VariantInit(out);
    if (attribute != UIA_IsReadOnlyAttributeId) {
        // Not published: the reserved value, which UI Automation owns.
        (*out).vt = VT_UNKNOWN;
        return UiaGetReservedNotSupportedValue(&(*out).punkVal);
    }
    try {
        TextRead text;
        const HRESULT status = read(text);
        if (FAILED(status)) return status;
        const bool read_only = has_semantic_state((*text.self.node).states, SemanticState::read_only);
        (*out).vt = VT_BOOL;
        (*out).boolVal = read_only ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT UiaTextRange::bounding_rectangles(SAFEARRAY** out) {
    TextRead text;
    const HRESULT status = read(text);
    if (FAILED(status)) return status;
    std::vector<Rect> boxes;
    range_boxes(text, start_, end_, (*source_).scale, boxes);
    // Four doubles per box: left, top, width and height in screen pixels.
    SAFEARRAY* array = SafeArrayCreateVector(VT_R8, 0, static_cast<ULONG>(boxes.size() * 4U));
    if (array == nullptr) return E_OUTOFMEMORY;
    for (std::size_t index = 0U; index < boxes.size(); ++index) {
        const UiaRect screen = to_screen(*source_, boxes[index]);
        const double values[4] = {screen.left, screen.top, screen.width, screen.height};
        for (std::size_t part = 0U; part < 4U; ++part) {
            LONG position = static_cast<LONG>(index * 4U + part);
            double component = values[part];
            const HRESULT put = SafeArrayPutElement(array, &position, &component);
            if (FAILED(put)) {
                SafeArrayDestroy(array);
                return put;
            }
        }
    }
    *out = array;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE UiaTextRange::GetBoundingRectangles(SAFEARRAY** out) {
    if (out == nullptr) return E_INVALIDARG;
    *out = nullptr;
    try {
        return bounding_rectangles(out);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT STDMETHODCALLTYPE UiaTextRange::GetEnclosingElement(IRawElementProviderSimple** out) {
    if (out == nullptr) return E_INVALIDARG;
    *out = nullptr;
    try {
        TextRead text;
        const HRESULT status = read(text);
        if (FAILED(status)) return status;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
    IRawElementProviderSimple* element = make_automation_element(source_, id_);
    if (element == nullptr) return E_OUTOFMEMORY;
    *out = element;
    return S_OK;
}

HRESULT UiaTextRange::text(int max_length, BSTR* out) {
    TextRead read_back;
    const HRESULT status = read(read_back);
    if (FAILED(status)) return status;
    const std::string_view within = std::string_view(read_back.value()).substr(start_, end_ - start_);
    BSTR whole = nullptr;
    const HRESULT converted = utf8_to_bstr(within, &whole);
    if (FAILED(converted)) return converted;
    const UINT length = whole == nullptr ? 0U : SysStringLen(whole);
    const bool cut = max_length >= 0 && static_cast<UINT>(max_length) < length;
    if (whole != nullptr && !cut) {
        *out = whole;
        return S_OK;
    }
    // Empty text is an empty string, never null; a cut keeps the first
    // max_length UTF-16 units, one fewer when the last would be a high
    // surrogate whose pair the cut separates (audit P7).
    UINT kept = cut ? static_cast<UINT>(max_length) : 0U;
    if (kept > 0U && whole[kept - 1U] >= 0xD800 && whole[kept - 1U] <= 0xDBFF) kept = kept - 1U;
    BSTR result = SysAllocStringLen(whole, kept);
    SysFreeString(whole);
    if (result == nullptr) return E_OUTOFMEMORY;
    *out = result;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE UiaTextRange::GetText(int max_length, BSTR* out) {
    if (out == nullptr || max_length < -1) return E_INVALIDARG;
    *out = nullptr;
    try {
        return text(max_length, out);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

// A degenerate range moves its position by `count` units and stays
// degenerate; a non-degenerate range moves to the start of the unit
// `count` units from the one holding its start, and becomes that unit.
HRESULT UiaTextRange::move(TextUnit unit, int count, int* out) {
    TextRead text;
    const HRESULT status = read(text);
    if (FAILED(status)) return status;
    const std::size_t size = text.value().size();
    fill_semantic_text_boundaries(text.layout, text.value(), semantic_unit(unit), text.boundaries);
    const bool degenerate = start_ == end_;
    std::size_t position = start_;
    if (!degenerate) {
        std::size_t unit_end = end_;
        enclosing_unit(text.boundaries, size, start_, position, unit_end);
    }
    const int moved = move_across(text.boundaries, size, count, !degenerate, position);
    *out = moved;
    if (moved == 0 && !degenerate) return S_OK;
    if (degenerate) {
        start_ = position;
        end_ = position;
        return S_OK;
    }
    enclosing_unit(text.boundaries, size, position, start_, end_);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE UiaTextRange::Move(TextUnit unit, int count, int* out) {
    if (out == nullptr) return E_INVALIDARG;
    *out = 0;
    try {
        return move(unit, count, out);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT UiaTextRange::move_endpoint(TextPatternRangeEndpoint which, TextUnit unit, int count, int* out) {
    TextRead text;
    const HRESULT status = read(text);
    if (FAILED(status)) return status;
    fill_semantic_text_boundaries(text.layout, text.value(), semantic_unit(unit), text.boundaries);
    std::size_t position = endpoint_offset(which);
    const int moved = move_across(text.boundaries, text.value().size(), count, false, position);
    set_endpoint(which, position);
    *out = moved;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE UiaTextRange::MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit,
                                                           int count, int* out) {
    if (out == nullptr) return E_INVALIDARG;
    *out = 0;
    if (endpoint != TextPatternRangeEndpoint_Start && endpoint != TextPatternRangeEndpoint_End) {
        return E_INVALIDARG;
    }
    try {
        return move_endpoint(endpoint, unit, count, out);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT STDMETHODCALLTYPE UiaTextRange::MoveEndpointByRange(TextPatternRangeEndpoint endpoint,
                                                            ITextRangeProvider* target,
                                                            TextPatternRangeEndpoint target_endpoint) {
    if (endpoint != TextPatternRangeEndpoint_Start && endpoint != TextPatternRangeEndpoint_End) {
        return E_INVALIDARG;
    }
    UiaTextRange* other = same_element_range(target);
    if (other == nullptr) return E_INVALIDARG;
    const std::size_t offset = (*other).endpoint_offset(target_endpoint);
    (*other).Release();
    set_endpoint(endpoint, offset);
    return S_OK;
}

// The control's selection becomes this range, anchored at its start with
// the caret at its end.
HRESULT STDMETHODCALLTYPE UiaTextRange::Select() {
    try {
        TextRead text;
        const HRESULT status = read(text);
        if (FAILED(status)) return status;
        const RuntimeId control{(*text.self.node).runtime_id};
        const SemanticTextSelection selection{Utf8Offset{start_}, Utf8Offset{end_}};
        const SemanticTextResult result = (*(*source_).model).set_semantic_text_selection(control, selection);
        if (result == SemanticTextResult::stale_identity) return element_not_available;
        if (result != SemanticTextResult::done) return invalid_operation;
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

// A text box holds one selection.
HRESULT STDMETHODCALLTYPE UiaTextRange::AddToSelection() {
    return invalid_operation;
}

HRESULT STDMETHODCALLTYPE UiaTextRange::RemoveFromSelection() {
    return invalid_operation;
}

// A text box scrolls only to show its caret, so a range is brought into
// view only when it already is: S_OK when every box of it lies within the
// node, UIA_E_INVALIDOPERATION otherwise.
HRESULT UiaTextRange::scroll_into_view() {
    TextRead text;
    const HRESULT status = read(text);
    if (FAILED(status)) return status;
    const Rect visible = (*text.self.node).bounds;
    for (const SemanticTextGrapheme& grapheme : text.layout.graphemes) {
        if (grapheme.range.start.value() < start_ || grapheme.range.end.value() > end_) continue;
        const Rect shown = Rect::intersection(grapheme.bounds, visible);
        if (!(shown == grapheme.bounds)) return invalid_operation;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE UiaTextRange::ScrollIntoView(BOOL) {
    try {
        return scroll_into_view();
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

// Text holds no embedded objects.
HRESULT STDMETHODCALLTYPE UiaTextRange::GetChildren(SAFEARRAY** out) {
    if (out == nullptr) return E_INVALIDARG;
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0U);
    if (*out == nullptr) return E_OUTOFMEMORY;
    return S_OK;
}

// The grapheme edge nearest the logical point (x, y): the line whose band
// holds y, or the nearest line, then the edge of its grapheme nearest x.
std::size_t offset_at_point(const TextRead& text, double x, double y) noexcept {
    const std::vector<SemanticTextLine>& lines = text.layout.lines;
    if (lines.empty()) return 0U;
    std::size_t line = 0U;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < lines.size(); ++index) {
        const Rect band = lines[index].bounds;
        const double above = band.y - y;
        const double below = y - (band.y + band.height);
        const double distance = std::max(0.0, std::max(above, below));
        if (distance < best) {
            best = distance;
            line = index;
        }
    }
    std::size_t result = lines[line].range.start.value();
    double nearest = std::numeric_limits<double>::infinity();
    for (const SemanticTextGrapheme& grapheme : text.layout.graphemes) {
        if (grapheme.line.value() != line) continue;
        const double left = std::abs(x - grapheme.bounds.x);
        const double right = std::abs(x - (grapheme.bounds.x + grapheme.bounds.width));
        if (left < nearest) {
            nearest = left;
            result = grapheme.range.start.value();
        }
        if (right < nearest) {
            nearest = right;
            result = grapheme.range.end.value();
        }
    }
    return result;
}

HRESULT selection_ranges(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                         SAFEARRAY** out) {
    TextRead text;
    const HRESULT status = read_text(*source, id, text);
    if (FAILED(status)) return status;
    std::vector<ITextRangeProvider*> ranges;
    const std::optional<SemanticTextSelection>& selection = (*text.self.node).text_selection;
    if (selection) {
        const std::size_t anchor = clamp_offset(text.value(), (*selection).anchor.value());
        const std::size_t caret = clamp_offset(text.value(), (*selection).caret.value());
        ITextRangeProvider* range = nullptr;
        const HRESULT made = make_range(source, id, std::min(anchor, caret), std::max(anchor, caret), &range);
        if (FAILED(made)) return made;
        ranges.push_back(range);
    }
    return range_array(ranges, out);
}

HRESULT visible_ranges(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                       SAFEARRAY** out) {
    TextRead text;
    const HRESULT status = read_text(*source, id, text);
    if (FAILED(status)) return status;
    const Rect visible = (*text.self.node).bounds;
    std::optional<std::size_t> first;
    std::size_t last = 0U;
    for (const SemanticTextGrapheme& grapheme : text.layout.graphemes) {
        const Rect shown = Rect::intersection(grapheme.bounds, visible);
        if (shown.empty()) continue;
        if (!first) first = grapheme.range.start.value();
        last = grapheme.range.end.value();
    }
    const std::size_t start = first.value_or(0U);
    const std::size_t end = first ? last : 0U;
    std::vector<ITextRangeProvider*> ranges;
    ITextRangeProvider* range = nullptr;
    const HRESULT made = make_range(source, id, start, end, &range);
    if (FAILED(made)) return made;
    ranges.push_back(range);
    return range_array(ranges, out);
}

HRESULT range_from_point(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                         UiaPoint point, ITextRangeProvider** out) {
    TextRead text;
    const HRESULT status = read_text(*source, id, text);
    if (FAILED(status)) return status;
    POINT origin{};
    ClientToScreen((*source).hwnd, &origin);
    const double logical_x = (point.x - origin.x) / (*source).scale;
    const double logical_y = (point.y - origin.y) / (*source).scale;
    const std::size_t offset = offset_at_point(text, logical_x, logical_y);
    return make_range(source, id, offset, offset, out);
}

HRESULT document_range(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                       ITextRangeProvider** out) {
    TextRead text;
    const HRESULT status = read_text(*source, id, text);
    if (FAILED(status)) return status;
    return make_range(source, id, 0U, text.value().size(), out);
}

HRESULT caret_range(const std::shared_ptr<SemanticSource>& source, SemanticElementId id, BOOL* active,
                    ITextRangeProvider** out) {
    TextRead text;
    const HRESULT status = read_text(*source, id, text);
    if (FAILED(status)) return status;
    const SemanticNode& node = *text.self.node;
    const std::size_t caret = node.text_selection
        ? clamp_offset(text.value(), (*node.text_selection).caret.value()) : 0U;
    *active = has_semantic_state(node.states, SemanticState::focused) ? TRUE : FALSE;
    return make_range(source, id, caret, caret, out);
}

void raise_text_event(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                      EVENTID event) noexcept {
    IRawElementProviderSimple* element = make_automation_element(source, id);
    if (element == nullptr) return;
    UiaRaiseAutomationEvent(element, event);
    (*element).Release();
}

} // namespace

bool text_pattern(const SemanticNode& node) noexcept {
    const bool text = semantic_role_fallback(node.role) == SemanticRole::text_box &&
                      node.text_selection.has_value() &&
                      control_backed(node) &&
                      !has_semantic_state(node.states, SemanticState::protected_content);
    return text;
}

HRESULT text_selection_ranges(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                              SAFEARRAY** out) noexcept {
    if (out == nullptr) return E_INVALIDARG;
    *out = nullptr;
    try {
        return selection_ranges(source, id, out);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT text_visible_ranges(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                            SAFEARRAY** out) noexcept {
    if (out == nullptr) return E_INVALIDARG;
    *out = nullptr;
    try {
        return visible_ranges(source, id, out);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT text_range_from_point(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                              UiaPoint point, ITextRangeProvider** out) noexcept {
    if (out == nullptr) return E_INVALIDARG;
    *out = nullptr;
    try {
        return range_from_point(source, id, point, out);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT text_document_range(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                            ITextRangeProvider** out) noexcept {
    if (out == nullptr) return E_INVALIDARG;
    *out = nullptr;
    try {
        return document_range(source, id, out);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT text_caret_range(const std::shared_ptr<SemanticSource>& source, SemanticElementId id, BOOL* active,
                         ITextRangeProvider** out) noexcept {
    if (out == nullptr || active == nullptr) return E_INVALIDARG;
    *out = nullptr;
    *active = FALSE;
    try {
        return caret_range(source, id, active, out);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

void raise_text_changes(const std::shared_ptr<SemanticSource>& source, SemanticElementId id,
                        const SemanticNode& before, const SemanticNode& after) noexcept {
    if (!text_pattern(after)) return;
    if (before.value != after.value) raise_text_event(source, id, UIA_Text_TextChangedEventId);
    if (before.text_selection != after.text_selection) {
        raise_text_event(source, id, UIA_Text_TextSelectionChangedEventId);
    }
}

} // namespace gui_forms::host
