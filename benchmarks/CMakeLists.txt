# Benchmark tools.
#   benchmark_data_generator  writes data/generated/* and metadata.json (seed 12345 by default)
#   ev33_bench                the reproducible harness -> benchmarks/results.csv
#   ev33_gbench               optional Google Benchmark micro-benchmarks (vendored library)
add_executable(benchmark_data_generator generator.cpp)
target_link_libraries(benchmark_data_generator PRIVATE ev33_core)
target_compile_options(benchmark_data_generator PRIVATE ${EV33_WARN})

add_executable(ev33_bench benchmark.cpp)
target_link_libraries(ev33_bench PRIVATE ev33_core)
target_compile_definitions(ev33_bench PRIVATE EV33_BUILD_TYPE="$<CONFIG>")

if(EV33_BUILD_GBENCH)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
  add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/benchmark ${CMAKE_BINARY_DIR}/third_party/benchmark EXCLUDE_FROM_ALL)
  add_executable(ev33_gbench gbench.cpp)
  target_link_libraries(ev33_gbench PRIVATE ev33_core benchmark::benchmark)
endif()
