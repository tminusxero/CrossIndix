add_executable(ScriptFallbackTest
  ScriptBlockTest.cpp
  ScriptUiFontCacheTest.cpp
)

target_include_directories(ScriptFallbackTest PRIVATE
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/EpdFont
)

target_link_libraries(ScriptFallbackTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ScriptFallbackTest)
