include(FetchContent)

# GoogleTest via FetchContent. Pinned to a release tag for reproducibility.
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(MDFH_TEST_SOURCES
    test_serialization.cpp
    test_sequence_tracker.cpp
    test_parser.cpp
    test_order_book.cpp
    test_book_builder.cpp
    test_capture_replay.cpp
    test_simulator.cpp
    test_spsc_queue.cpp
    test_pipeline.cpp
    test_network.cpp)

add_executable(mdfh_tests ${MDFH_TEST_SOURCES})
target_link_libraries(mdfh_tests PRIVATE mdfh GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(mdfh_tests)
