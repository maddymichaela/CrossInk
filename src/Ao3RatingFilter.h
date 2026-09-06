#pragma once

#include <stdint.h>

enum class Ao3RatingFilterMode : uint8_t { Only = 0, Exclude = 1 };

constexpr uint8_t AO3_INDEX_TOMBSTONE_FLAG = 0x01;
constexpr uint8_t AO3_INDEX_RATING_SHIFT = 1;
constexpr uint8_t AO3_INDEX_RATING_MASK = 0x0E;

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

inline bool matchesAo3RatingFilter(const uint8_t flags, const char selectedRating,
                                   const Ao3RatingFilterMode mode) {
  if (selectedRating == 0) return true;
  const uint8_t storedCode = ao3RatingCodeFromFlags(flags);
  const uint8_t selectedCode = ao3RatingCode(selectedRating);
  if (storedCode == 0 || selectedCode == 0) return mode == Ao3RatingFilterMode::Exclude;
  const bool matches = storedCode == selectedCode;
  return mode == Ao3RatingFilterMode::Only ? matches : !matches;
}
