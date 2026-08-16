#pragma once
#include <stdint.h>

#pragma pack(push, 1)
struct CompactIndexRecord {
    char     title[64];         // truncated title
    char     author[32];        // truncated author
    uint32_t wordCount;         // from scraping
    uint32_t addedSequence;     // monotonically increasing counter assigned at index write time
    char     seriesName[32];    // from scraping
    uint16_t seriesPart;        // position within series, 0 if none
    char     fandom[32];        // scraped from epub HTML
    char     relationship1[32]; // primary pairing — scraped from epub HTML
    char     relationship2[32]; // secondary pairing — scraped from epub HTML
    uint32_t cacheHash;         // stable uint32_t path hash used as compact record key
    uint8_t  flags;             // bit 0 = tombstone (deleted)
};
#pragma pack(pop)

// Exactly 239 bytes on disk
static_assert(sizeof(CompactIndexRecord) == 239, "CompactIndexRecord must be exactly 239 bytes");

// AvesO3's fixed-size view model is deliberately capped here: 400 ViewEntry
// records consume about 34 KB, which leaves enough contiguous heap for the
// reader and network activities on the X4.
constexpr uint16_t MAX_LIBRARY_BOOKS = 400;
constexpr uint32_t INDEX_HEADER_SIZE = 12;

inline uint32_t offsetOf(uint16_t i) {
    return INDEX_HEADER_SIZE + i * (uint32_t)sizeof(CompactIndexRecord);
}
