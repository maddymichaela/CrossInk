add_executable(Ao3HtmlMetadataParserTest
  Ao3HtmlMetadataParserTest.cpp
  ../../src/Ao3HtmlMetadataParser.cpp
)

target_include_directories(Ao3HtmlMetadataParserTest PRIVATE ../../src)
target_link_libraries(Ao3HtmlMetadataParserTest PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(Ao3HtmlMetadataParserTest)
