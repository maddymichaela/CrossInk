#include <gtest/gtest.h>

#include <cstring>

#include "Ao3HtmlMetadataParser.h"

TEST(Ao3HtmlMetadataParser, ExtractsNestedKoboTagsWithoutSeparatorTokens) {
  constexpr char html[] = R"HTML(
    <dt><span>Additional Tags:</span></dt>
    <dd><a><span>Minor Character Death</span></a><span>, </span>
        <a><span>Blood and Injury</span></a><span>, </span>
        <a><span>Gun Kink</span></a><span>, </span>
        <a><span>Alternate Universe - Gangsters</span></a></dd>)HTML";
  char tags[Ao3HtmlMetadataParser::TAG_COUNT][Ao3HtmlMetadataParser::TAG_SIZE] = {};

  EXPECT_EQ(Ao3HtmlMetadataParser::extractTags(html, strlen(html), "Additional Tags:", tags), 4u);
  EXPECT_STREQ(tags[0], "Minor Charact..");
  EXPECT_STREQ(tags[1], "Blood and Inj..");
  EXPECT_STREQ(tags[2], "Gun Kink");
  EXPECT_STREQ(tags[3], "AU-Gangsters");
}

TEST(Ao3HtmlMetadataParser, IgnoresEmptyAndPunctuationOnlyTags) {
  constexpr char html[] = "Additional Tags:</b> , , Other, Hurt/Comfort<br/>";
  char tags[Ao3HtmlMetadataParser::TAG_COUNT][Ao3HtmlMetadataParser::TAG_SIZE] = {};

  EXPECT_EQ(Ao3HtmlMetadataParser::extractTags(html, strlen(html), "Additional Tags:", tags), 1u);
  EXPECT_STREQ(tags[0], "Hurt/Comfort");
  EXPECT_EQ(tags[1][0], '\0');
}

TEST(Ao3HtmlMetadataParser, ExtractsFullNestedKoboSummary) {
  constexpr char html[] = R"HTML(
    <p><span>Summary</span></p><blockquote><p><span>First sentence. </span>
    <span>Second &amp; third.</span></p><p><span>Final paragraph.</span></p></blockquote>)HTML";
  char summary[256] = {};

  EXPECT_GT(Ao3HtmlMetadataParser::extractSummary(html, strlen(html), summary, sizeof(summary)), 0u);
  EXPECT_STREQ(summary, "First sentence. Second & third. Final paragraph.");
}
