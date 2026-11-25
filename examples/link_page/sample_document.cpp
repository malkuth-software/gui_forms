#include "sample_document.hpp"

#include <array>
#include <string>
#include <string_view>

namespace link_page_example {

namespace {

constexpr std::array<std::string_view, 6> sentences = {
    "A retained control keeps what it computed until one of its inputs changes. ",
    "Paint and hit testing read that kept result and change nothing. ",
    "Scrolling moves the origin the kept lines are drawn from, so no line is flowed again. ",
    "Resizing changes the width the text flows at, which is an input, so the page flows once. ",
    "Hovering a link repaints that link and nothing else. ",
    "The counters prove it: measure them instead of guessing. ",
};

constexpr std::array<std::string_view, 4> link_words = {"layout", "paint", "input", "counters"};

} // namespace

std::vector<Paragraph> sample_document(const std::size_t count) {
    std::vector<Paragraph> paragraphs;
    paragraphs.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        Paragraph paragraph{};
        paragraph.text = "Paragraph " + std::to_string(index + 1U) + ". ";
        const std::size_t links = 2U + index % 3U;
        const std::size_t length = 2U + index % sentences.size();
        for (std::size_t sentence = 0U; sentence < length; ++sentence) {
            if (sentence < links) {
                // A link on a word of its own, then the sentence.
                const std::string_view word = link_words[(index + sentence) % link_words.size()];
                LinkSpan link{};
                link.begin = paragraph.text.size();
                paragraph.text.append(word);
                link.end = paragraph.text.size();
                link.target = std::string(word) + "#" + std::to_string(index);
                paragraph.links.push_back(link);
                paragraph.text.append(": ");
            }
            paragraph.text.append(sentences[(index + sentence) % sentences.size()]);
            if (index % 7U == 0U && sentence == 0U) paragraph.text.append("\n");
        }
        paragraphs.push_back(paragraph);
    }
    return paragraphs;
}

} // namespace link_page_example
