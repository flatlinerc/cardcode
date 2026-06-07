add_executable(cardcode_tests
  doctest_main.cpp
  test_lexer.cpp
  test_parser.cpp
  test_compiler.cpp
  test_ids.cpp
  test_execution.cpp
  test_errors.cpp
  test_conditions.cpp
  test_cancel.cpp
  test_variables.cpp
  test_functions.cpp
  test_while.cpp
  test_harness.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(cardcode_tests PRIVATE cardcode_engine Threads::Threads)
target_include_directories(cardcode_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${CMAKE_CURRENT_SOURCE_DIR}/../harness)

add_test(NAME cardcode_tests COMMAND cardcode_tests)
