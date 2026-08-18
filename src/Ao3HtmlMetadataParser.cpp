#include "Ao3HtmlMetadataParser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {

const char* findBounded(const char* begin, const char* end, const char* needle) {
  if (!begin || !end || begin >= end || !needle || !*needle) return nullptr;
  const size_t needleSize = strlen(needle);
  if (needleSize > static_cast<size_t>(end - begin)) return nullptr;
  for (const char* cursor = begin; cursor + needleSize <= end; ++cursor) {
    if (memcmp(cursor, needle, needleSize) == 0) return cursor;
  }
  return nullptr;
}

void appendSpace(char* output, size_t& used, const size_t capacity) {
  if (used == 0 || output[used - 1] == ' ' || used + 1 >= capacity) return;
  output[used++] = ' ';
}

size_t appendEntity(const char* cursor, const char* end, char* output, size_t& used, const size_t capacity) {
  struct Entity {
    const char* encoded;
    char decoded;
  };
  constexpr Entity entities[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'},
                                 {"&quot;", '"'}, {"&#39;", '\''}, {"&apos;", '\''}};
  for (const auto& entity : entities) {
    const size_t length = strlen(entity.encoded);
    if (cursor + length <= end && memcmp(cursor, entity.encoded, length) == 0) {
      if (used + 1 < capacity) output[used++] = entity.decoded;
      return length;
    }
  }
  if (used + 1 < capacity) output[used++] = *cursor;
  return 1;
}

size_t visibleText(const char* begin, const char* end, char* output, const size_t capacity) {
  if (!output || capacity == 0) return 0;
  size_t used = 0;
  bool inTag = false;
  const char* tagStart = nullptr;
  for (const char* cursor = begin; cursor < end && used + 1 < capacity; ++cursor) {
    if (*cursor == '<') {
      inTag = true;
      tagStart = cursor;
      continue;
    }
    if (inTag) {
      if (*cursor == '>') {
        if (tagStart && (strncasecmp(tagStart, "<br", 3) == 0 || strncasecmp(tagStart, "</p", 3) == 0 ||
                         strncasecmp(tagStart, "</div", 5) == 0 || strncasecmp(tagStart, "</li", 4) == 0)) {
          appendSpace(output, used, capacity);
        }
        inTag = false;
      }
      continue;
    }
    if (*cursor == '&') {
      cursor += appendEntity(cursor, end, output, used, capacity) - 1;
    } else if (isspace(static_cast<unsigned char>(*cursor))) {
      appendSpace(output, used, capacity);
    } else {
      output[used++] = *cursor;
    }
  }
  while (used > 0 && isspace(static_cast<unsigned char>(output[used - 1]))) --used;
  output[used] = '\0';
  return used;
}

bool usefulTag(const char* tag) {
  if (!tag || !*tag || strcasecmp(tag, "Other") == 0) return false;
  for (const char* cursor = tag; *cursor; ++cursor) {
    if (isalnum(static_cast<unsigned char>(*cursor))) return true;
  }
  return false;
}

void normalizeTag(const char* begin, const char* end,
                  char (&output)[Ao3HtmlMetadataParser::TAG_SIZE]) {
  while (begin < end && (isspace(static_cast<unsigned char>(*begin)) || *begin == ',')) ++begin;
  while (end > begin && (isspace(static_cast<unsigned char>(end[-1])) || end[-1] == ',')) --end;

  char normalized[96] = {};
  size_t normalizedSize = static_cast<size_t>(end - begin);
  normalizedSize = std::min(normalizedSize, sizeof(normalized) - 1);
  memcpy(normalized, begin, normalizedSize);
  normalized[normalizedSize] = '\0';

  constexpr char auPrefix[] = "Alternate Universe - ";
  constexpr char impliedPrefix[] = "Implied/Referenced ";
  char compact[96] = {};
  if (strncasecmp(normalized, auPrefix, sizeof(auPrefix) - 1) == 0) {
    snprintf(compact, sizeof(compact), "AU-%s", normalized + sizeof(auPrefix) - 1);
  } else if (strncasecmp(normalized, impliedPrefix, sizeof(impliedPrefix) - 1) == 0) {
    snprintf(compact, sizeof(compact), "I/R %s", normalized + sizeof(impliedPrefix) - 1);
  } else {
    strncpy(compact, normalized, sizeof(compact) - 1);
  }

  const size_t length = strlen(compact);
  if (length < Ao3HtmlMetadataParser::TAG_SIZE) {
    strcpy(output, compact);
  } else {
    memcpy(output, compact, Ao3HtmlMetadataParser::TAG_SIZE - 3);
    output[Ao3HtmlMetadataParser::TAG_SIZE - 3] = '.';
    output[Ao3HtmlMetadataParser::TAG_SIZE - 2] = '.';
    output[Ao3HtmlMetadataParser::TAG_SIZE - 1] = '\0';
  }
}

}  // namespace

namespace Ao3HtmlMetadataParser {

size_t extractTags(const char* html, const size_t htmlSize, const char* anchor,
                   char (&tags)[TAG_COUNT][TAG_SIZE]) {
  if (!html || htmlSize == 0 || !anchor) return 0;
  const char* const end = html + htmlSize;
  const char* marker = findBounded(html, end, anchor);
  if (!marker) return 0;

  const char* valueStart = marker + strlen(anchor);
  const char* dd = findBounded(valueStart, end, "<dd");
  const char* nextField = findBounded(valueStart, end, "<dt");
  if (dd && (!nextField || dd < nextField)) {
    const char* openEnd = static_cast<const char*>(memchr(dd, '>', static_cast<size_t>(end - dd)));
    if (!openEnd) return 0;
    valueStart = openEnd + 1;
  }

  const char* valueEnd = findBounded(valueStart, end, "</dd>");
  bool complete = valueEnd != nullptr;
  if (!valueEnd) {
    valueEnd = findBounded(valueStart, end, "<br");
    complete = valueEnd != nullptr;
  }
  if (!valueEnd) valueEnd = end;

  char visible[640] = {};
  const size_t visibleSize = visibleText(valueStart, valueEnd, visible, sizeof(visible));
  if (visibleSize == 0) return 0;

  char parsed[TAG_COUNT][TAG_SIZE] = {};
  size_t count = 0;
  const char* tokenStart = visible;
  const char* const visibleEnd = visible + visibleSize;
  while (tokenStart < visibleEnd && count < TAG_COUNT) {
    const char* comma = static_cast<const char*>(memchr(tokenStart, ',', static_cast<size_t>(visibleEnd - tokenStart)));
    if (!comma && !complete) break;
    const char* tokenEnd = comma ? comma : visibleEnd;
    char candidate[TAG_SIZE] = {};
    normalizeTag(tokenStart, tokenEnd, candidate);
    if (usefulTag(candidate)) {
      bool duplicate = false;
      for (size_t i = 0; i < count; ++i) duplicate = duplicate || strcasecmp(parsed[i], candidate) == 0;
      if (!duplicate) strcpy(parsed[count++], candidate);
    }
    if (!comma) break;
    tokenStart = comma + 1;
  }

  if (count == 0) return 0;
  memset(tags, 0, TAG_COUNT * TAG_SIZE);
  for (size_t i = 0; i < count; ++i) strcpy(tags[i], parsed[i]);
  return count;
}

size_t extractSummary(const char* html, const size_t htmlSize, char* summary, const size_t summarySize) {
  if (!html || htmlSize == 0 || !summary || summarySize == 0) return 0;
  const char* const end = html + htmlSize;
  const char* marker = findBounded(html, end, ">Summary</");
  if (!marker) marker = findBounded(html, end, "Summary:");
  if (!marker) return 0;

  const char* blockquote = findBounded(marker, end, "<blockquote");
  if (!blockquote) return 0;
  const char* openEnd = static_cast<const char*>(memchr(blockquote, '>', static_cast<size_t>(end - blockquote)));
  if (!openEnd) return 0;
  const char* close = findBounded(openEnd + 1, end, "</blockquote>");
  if (!close) close = end;
  return visibleText(openEnd + 1, close, summary, summarySize);
}

}  // namespace Ao3HtmlMetadataParser
