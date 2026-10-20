# Test suites (GoogleTest, vendored in third_party/googletest).
#   ev33_unit_tests         tests/unit/*        one test group per module
#   ev33_integration_tests  tests/integration   IT01-IT08 + regression
#   ev33_adversarial_tests  tests/adversarial   AD01-AD25
# Run all:   ctest --test-dir build --output-on-failure
# Run one:   ctest --test-dir build -L unit | integration | adversarial
set(EV33_TEST_DEFS EV33_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

function(ev33_test name label)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE ev33_core GTest::gtest_main)
  target_compile_definitions(${name} PRIVATE ${EV33_TEST_DEFS})
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
  # tests write temporary files into their working directory
  set(work ${CMAKE_CURRENT_BINARY_DIR}/work_${label})
  file(MAKE_DIRECTORY ${work})
  add_test(NAME ${name} COMMAND ${name} --gtest_output=xml:${CMAKE_CURRENT_BINARY_DIR}/${name}.xml WORKING_DIRECTORY ${work})
  set_tests_properties(${name} PROPERTIES LABELS ${label})
endfunction()

file(GLOB EV33_UNIT_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)
ev33_test(ev33_unit_tests unit ${EV33_UNIT_TESTS})
ev33_test(ev33_integration_tests integration ${CMAKE_CURRENT_SOURCE_DIR}/integration/integration_tests.cpp)
ev33_test(ev33_adversarial_tests adversarial ${CMAKE_CURRENT_SOURCE_DIR}/adversarial/adversarial_tests.cpp)
