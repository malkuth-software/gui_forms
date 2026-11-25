#pragma once

// A deterministic document for the LinkPage example and its test: numbered
// paragraphs of ordinary prose of varying length, each with links.

#include "link_page.hpp"

#include <cstddef>
#include <vector>

namespace link_page_example {

// `count` paragraphs; paragraph i has 2 + i % 3 links, and every seventh
// paragraph holds a line feed.
[[nodiscard]] std::vector<Paragraph> sample_document(std::size_t count);

} // namespace link_page_example
