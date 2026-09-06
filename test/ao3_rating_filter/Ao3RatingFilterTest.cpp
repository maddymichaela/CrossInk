#include <gtest/gtest.h>

#include "Ao3RatingFilter.h"

TEST(Ao3RatingFilter, EncodesRatingWithoutChangingTombstone) {
  const uint8_t flags = withAo3Rating(AO3_INDEX_TOMBSTONE_FLAG, 'M');
  EXPECT_NE(flags & AO3_INDEX_TOMBSTONE_FLAG, 0);
  EXPECT_EQ(ao3RatingCodeFromFlags(flags), ao3RatingCode('M'));
}

TEST(Ao3RatingFilter, IncludesOneSelectedRating) {
  const uint8_t explicitFlags = withAo3Rating(0, 'E');
  EXPECT_TRUE(matchesAo3RatingFilter(explicitFlags, ao3RatingBit('E')));
  EXPECT_FALSE(matchesAo3RatingFilter(explicitFlags, ao3RatingBit('M')));
}

TEST(Ao3RatingFilter, IncludesAnySelectedRating) {
  const uint8_t teenFlags = withAo3Rating(0, 'T');
  const uint8_t teenAndMature = ao3RatingBit('T') | ao3RatingBit('M');
  EXPECT_TRUE(matchesAo3RatingFilter(teenFlags, teenAndMature));
  EXPECT_TRUE(matchesAo3RatingFilter(withAo3Rating(0, 'M'), teenAndMature));
  EXPECT_FALSE(matchesAo3RatingFilter(withAo3Rating(0, 'E'), teenAndMature));
}

TEST(Ao3RatingFilter, SupportsNotRatedAndLegacyRecords) {
  const uint8_t notRatedFlags = withAo3Rating(0, '-');
  EXPECT_TRUE(matchesAo3RatingFilter(notRatedFlags, ao3RatingBit('-')));
  EXPECT_FALSE(matchesAo3RatingFilter(0, ao3RatingBit('G')));
  EXPECT_TRUE(matchesAo3RatingFilter(0, 0));
}
