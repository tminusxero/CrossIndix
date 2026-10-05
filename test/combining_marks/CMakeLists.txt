add_executable(CombiningMarkAnchorTest
  CombiningMarkAnchorTest.cpp
  ${REPO_ROOT}/lipi/engine/Lipi.cpp  # the mark queries of LipiMarks.h live in the engine
)

target_include_directories(CombiningMarkAnchorTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
)

target_link_libraries(CombiningMarkAnchorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CombiningMarkAnchorTest)
