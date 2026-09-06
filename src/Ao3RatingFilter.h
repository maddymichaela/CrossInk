#pragma once

#include <stdint.h>

constexpr uint8_t AO3_INDEX_TOMBSTONE_FLAG = 0x01;
constexpr uint8_t AO3_INDEX_RATING_SHIFT = 1;
constexpr uint8_t AO3_INDEX_RATING_MASK = 0x0E;
constexpr uint8_t AO3_ALL_RATINGS_MASK = 0x1F;

inline uint8_t ao3RatingCode(const char rating) {
  switch (rating) {
    case 'G': return 1;
    case 'T': return 2;
    case 'M': return 3;
    case 'E': return 4;
    case '-': return 5;
    default: return 0;
  }
}

inline uint8_t withAo3Rating(uint8_t flags, const char rating) {
  flags &= static_cast<uint8_t>(~AO3_INDEX_RATING_MASK);
  flags |= static_cast<uint8_t>(ao3RatingCode(rating) << AO3_INDEX_RATING_SHIFT);
  return flags;
}

inline uint8_t ao3RatingCodeFromFlags(const uint8_t flags) {
  return static_cast<uint8_t>((flags & AO3_INDEX_RATING_MASK) >> AO3_INDEX_RATING_SHIFT);
}

inline uint8_t ao3RatingBit(const char rating) {
  const uint8_t code = ao3RatingCode(rating);
  return code == 0 ? 0 : static_cast<uint8_t>(1U << (code - 1));
}

inline bool matchesAo3RatingFilter(const uint8_t flags, const uint8_t selectedRatings) {
  if (selectedRatings == 0) return true;
  const uint8_t storedCode = ao3RatingCodeFromFlags(flags);
  if (storedCode == 0) return false;
  return (selectedRatings & static_cast<uint8_t>(1U << (storedCode - 1))) != 0;
}
