#pragma once

#include <cstddef>

namespace Ao3HtmlMetadataParser {

constexpr size_t TAG_COUNT = 4;
constexpr size_t TAG_SIZE = 16;

size_t extractTags(const char* html, size_t htmlSize, const char* anchor,
                   char (&tags)[TAG_COUNT][TAG_SIZE]);

size_t extractSummary(const char* html, size_t htmlSize, char* summary, size_t summarySize);

}  // namespace Ao3HtmlMetadataParser
