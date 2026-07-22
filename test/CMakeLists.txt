cmake_minimum_required(VERSION 3.10)

#project(test LANGUAGES C CXX)
add_executable(dashcam_test test_main.cpp)

include(GoogleTest)
gtest_discover_tests(dashcam_test)

target_link_libraries(dashcam_test PRIVATE 
    source
    GTest::gtest
    GTest::gtest_main)

