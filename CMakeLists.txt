cmake_minimum_required(VERSION 3.25)
project(cardcode_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cardcode_engine
  src/lexer.cpp
  src/parser.cpp
  src/compiler.cpp
  src/engine.cpp
)

target_include_directories(cardcode_engine PUBLIC include)

target_compile_options(cardcode_engine PRIVATE
  -Wall -Wextra -Wpedantic
)

add_executable(cardcode-run tools/cardcode_run.cpp)
target_link_libraries(cardcode-run PRIVATE cardcode_engine)
target_compile_options(cardcode-run PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_subdirectory(tests)
