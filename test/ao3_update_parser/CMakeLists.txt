add_executable(Ao3UpdateParserTest
  Ao3UpdateParserTest.cpp
  ${REPO_ROOT}/src/Ao3UpdateParser.cpp
)

target_include_directories(Ao3UpdateParserTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(Ao3UpdateParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(Ao3UpdateParserTest)
