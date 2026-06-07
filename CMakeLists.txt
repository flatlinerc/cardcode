cmake_minimum_required(VERSION 3.25)
project(cardcode_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Integrators embedding the engine (e.g. ESP-IDF) build only the cardcode_engine
# library; the CLI and tests are host-only.
option(CARDCODE_BUILD_TOOLS "Build the cardcode-run CLI" ON)
option(CARDCODE_BUILD_TESTS "Build the doctest test suite" ON)
option(CARDCODE_EMBEDDED
  "Configure the engine for embedded builds: no exceptions/RTTI, host tools/tests off" OFF)

# --- Engine core: the only target an embedded integrator needs ---------------
add_library(cardcode_engine
  src/lexer.cpp
  src/parser.cpp
  src/compiler.cpp
  src/engine.cpp
)
target_include_directories(cardcode_engine PUBLIC include)
target_compile_options(cardcode_engine PRIVATE -Wall -Wextra -Wpedantic)

if(CARDCODE_EMBEDDED)
  # Mirror the ESP-IDF defaults (exceptions and RTTI off). This both shrinks the
  # binary and guarantees the engine core has no hidden dependency on either.
  target_compile_options(cardcode_engine PRIVATE -fno-exceptions -fno-rtti)
  set(CARDCODE_BUILD_TOOLS OFF)
  set(CARDCODE_BUILD_TESTS OFF)
endif()

# --- Host-only CLI + harness -------------------------------------------------
if(CARDCODE_BUILD_TOOLS)
  add_executable(cardcode-run tools/cardcode_run.cpp)
  target_link_libraries(cardcode-run PRIVATE cardcode_engine)
  target_compile_options(cardcode-run PRIVATE -Wall -Wextra -Wpedantic)

  # Local websocket harness for developing the card UI (see docs/integration/).
  find_package(Threads REQUIRED)
  add_executable(cardcode-harness harness/main.cpp)
  target_include_directories(cardcode-harness PRIVATE harness)
  target_link_libraries(cardcode-harness PRIVATE cardcode_engine Threads::Threads)
  target_compile_options(cardcode-harness PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Host-only tests ---------------------------------------------------------
if(CARDCODE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
