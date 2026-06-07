add_executable(cardcode_tests
  doctest_main.cpp
  test_lexer.cpp
  test_parser.cpp
  test_compiler.cpp
  test_ids.cpp
  test_execution.cpp
  test_errors.cpp
)

target_link_libraries(cardcode_tests PRIVATE cardcode_engine)
target_include_directories(cardcode_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_test(NAME cardcode_tests COMMAND cardcode_tests)
