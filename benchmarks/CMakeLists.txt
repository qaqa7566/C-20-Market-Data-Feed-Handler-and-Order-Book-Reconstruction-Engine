add_executable(mdfh_benchmark bench_feed.cpp)
target_link_libraries(mdfh_benchmark PRIVATE mdfh)
# Benchmarks are meaningful only in an optimized build.
target_compile_options(mdfh_benchmark PRIVATE
    $<$<CONFIG:Release>:-O3>)
