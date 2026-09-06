#include <gtest/gtest.h>

#include "Ao3RatingFilter.h"

TEST(Ao3RatingFilter, EncodesRatingWithoutChangingTombstone) {
  const uint8_t flags = withAo3Rating(AO3_INDEX_TOMBSTONE_FLAG, 'M');
  EXPECT_NE(flags & AO3_INDEX_TOMBSTONE_FLAG, 0);
  EXPECT_EQ(ao3RatingCodeFromFlags(flags), ao3RatingCode('M'));
}

TEST(Ao3RatingFilter, IncludesOnlySelectedRating) {
  const uint8_t explicitFlags = withAo3Rating(0, 'E');
  EXPECT_TRUE(matchesAo3RatingFilter(explicitFlags, 'E', Ao3RatingFilterMode::Only));
  EXPECT_FALSE(matchesAo3RatingFilter(explicitFlags, 'M', Ao3RatingFilterMode::Only));
}

TEST(Ao3RatingFilter, ExcludesSelectedRating) {
  const uint8_t teenFlags = withAo3Rating(0, 'T');
  EXPECT_FALSE(matchesAo3RatingFilter(teenFlags, 'T', Ao3RatingFilterMode::Exclude));
  EXPECT_TRUE(matchesAo3RatingFilter(teenFlags, 'G', Ao3RatingFilterMode::Exclude));
}

TEST(Ao3RatingFilter, SupportsNotRatedAndLegacyRecords) {
  const uint8_t notRatedFlags = withAo3Rating(0, '-');
  EXPECT_TRUE(matchesAo3RatingFilter(notRatedFlags, '-', Ao3RatingFilterMode::Only));
  EXPECT_FALSE(matchesAo3RatingFilter(0, 'G', Ao3RatingFilterMode::Only));
  EXPECT_TRUE(matchesAo3RatingFilter(0, 'G', Ao3RatingFilterMode::Exclude));
}
