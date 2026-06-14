cmake_minimum_required(VERSION 3.25)

# -----------------------------------------------------------------------------
# Dual-mode CMakeLists.
#
#   * Under ESP-IDF (ESP_PLATFORM is set) this file acts as a managed COMPONENT:
#     it calls idf_component_register() and returns. The ESP-IDF Component
#     Manager copies the WHOLE repo for a git dependency whose component is the
#     repo root, so the engine sources/headers (src/, include/, harness/) sit
#     right beside this file -- no `..` references and a single source of truth.
#   * Standalone (host) it is the normal project() build below (engine library +
#     cardcode-run CLI + cardcode-harness + doctest suite).
#
# NOTE: a per-subdir component (idf_component/ referencing ${COMPONENT_DIR}/..)
# CANNOT work as a git dependency -- the Component Manager only copies the
# component directory, not its parent -- so the engine MUST be the repo-root
# component. That is why idf_component_register lives here, guarded by
# ESP_PLATFORM, rather than in a subdir.
# -----------------------------------------------------------------------------
if(ESP_PLATFORM)
  idf_component_register(
    SRCS src/lexer.cpp src/parser.cpp src/compiler.cpp src/engine.cpp
    INCLUDE_DIRS include harness
  )
  # C++23, exceptions/RTTI off (mirrors the host CARDCODE_EMBEDDED=ON build).
  # IDF already builds C++ this way by default; set it explicitly so the
  # contract holds regardless of the consuming project's sdkconfig.
  target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_23)
  target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++23 -fno-exceptions -fno-rtti)
  return()
endif()

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
