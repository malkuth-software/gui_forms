#pragma once

// A Windows Forms `.resx` file read as data (stage MCP G7): its `data`
// entries, each a name with its text value and, for an entry that is not
// a string, its type or MIME type. Read with GUI.Forms' bounded XML reader
// (xml_types.hpp) under limits sized for resource files: a base64 image may
// be large, so one value may hold up to maximum_resx_value_bytes.
//
//   <data name="button1.Text" xml:space="preserve"><value>OK</value></data>
//   <data name="button1.Location" type="System.Drawing.Point, System.Drawing">
//     <value>12, 12</value>
//   </data>
//   <data name="$this.Icon" type="System.Drawing.Icon, System.Drawing"
//         mimetype="application/x-microsoft.net.object.bytearray.base64">
//     <value>AAABAAEAEBA...</value>
//   </data>
//
// A localizable form's designer reads its properties from these entries
// (`resources.ApplyResources(this.button1, "button1")` reads every entry
// named `button1.<Property>`); other forms read single entries
// (`resources.GetString("x")`, `resources.GetObject("x")`).

#include "conversion_diagnostics.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::winforms_converter {

inline constexpr std::size_t maximum_resx_value_bytes = 8U * 1024U * 1024U;
inline constexpr std::size_t maximum_resx_entries = 65536U;

struct ResxEntry final {
    std::string name{};
    // The `value` element's text.
    std::string value{};
    // The `type` attribute (`System.Drawing.Point, System.Drawing`); empty
    // for a string.
    std::string type{};
    std::string mime_type{};

    // Whether the entry is a serialized object or byte array (an image,
    // an icon), which the converter does not carry.
    [[nodiscard]] bool binary() const noexcept {
        const bool serialized = !mime_type.empty() || type.find("Bitmap") != std::string::npos ||
                                type.find("Icon") != std::string::npos ||
                                type.find("ImageListStreamer") != std::string::npos ||
                                type.find("Byte[]") != std::string::npos;
        return serialized;
    }
};

// One read file, its entries sorted by name. Caller-owned; replaced whole by
// a read that succeeds, left as it was by one that refuses.
struct ResxDocument final {
    std::string file{};
    std::vector<ResxEntry> entries{};

    // The entry named `name`, or null. The pointer borrows the document.
    [[nodiscard]] const ResxEntry* find(std::string_view name) const& noexcept;
    const ResxEntry* find(std::string_view name) const&& = delete;
};

enum class ResxReadOutcome : std::uint8_t {
    read,
    // Not XML the reader admits, or larger than maximum_source_bytes: one
    // resx_invalid or file_too_large, and `document` unchanged.
    refused,
};

// Reads `text`, the file named `file`.
[[nodiscard]] ResxReadOutcome read_resx(std::string_view file, std::string_view text, ResxDocument& document,
                                        ConversionDiagnosticList& diagnostics);

} // namespace gui_forms::winforms_converter
