# ESP-IDF managed component for the CardCode engine.
#
# This file lives in cardcode/idf_component/ (a SUBDIR), NOT at the repo root.
# The repo root CMakeLists.txt is a standalone HOST project() build
# (project(cardcode_engine ...) + add_library). An IDF component must call
# idf_component_register() inside an IDF build context and must NOT call
# project(); the two cannot share one file. Keeping the IDF component in a
# subdir lets the host CMake build and the IDF component build coexist with
# zero interference -- the host build never descends into this dir, and the
# Component Manager points at this dir (path: idf_component) when it clones
# the git dependency.
#
# The engine sources/headers are NOT duplicated here: they are referenced one
# level up (the real src/ and include/ at the repo root) so there is a single
# source of truth. The harness/ dir is also exported so consumers (e.g.
# cardcode_runtime's ws_event_sink.cpp) can #include "json.hpp" for byte-for-byte
# protocol parity with the desktop harness.

set(_cc_root "${COMPONENT_DIR}/..")

idf_component_register(
    SRCS "${_cc_root}/src/lexer.cpp"
         "${_cc_root}/src/parser.cpp"
         "${_cc_root}/src/compiler.cpp"
         "${_cc_root}/src/engine.cpp"
    INCLUDE_DIRS "${_cc_root}/include"
                 "${_cc_root}/harness"
)

# The engine targets C++23 and is verified clean with exceptions and RTTI off
# (this mirrors the host CARDCODE_EMBEDDED=ON build). ESP-IDF already builds
# C++ with -fno-exceptions -fno-rtti by default, but we set both explicitly on
# the component library so the contract holds regardless of project sdkconfig.
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_23)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++23
    -fno-exceptions
    -fno-rtti
)
