cmake_minimum_required(VERSION 3.20)
project(furuyoni_cli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA REQUIRED lua5.4)
find_package(nlohmann_json REQUIRED)

add_library(furuyoni
  src/engine/engine.cpp
  src/engine/engine_attack.cpp
  src/engine/engine_content.cpp
  src/engine/engine_observe.cpp
  src/engine/engine_setup.cpp
  src/engine/effect_host.cpp
  src/protocol/agent.cpp
)

target_include_directories(furuyoni PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${LUA_INCLUDE_DIRS}
)

target_link_libraries(furuyoni PUBLIC
  ${LUA_LIBRARIES}
  nlohmann_json::nlohmann_json
)
target_link_directories(furuyoni PUBLIC ${LUA_LIBRARY_DIRS})

# -Wno-free-nonheap-object: GCC 16 false positive on std::vector<std::string> dtor.
target_compile_options(furuyoni PRIVATE
  -Wall -Wextra -Wno-unused-parameter -Wno-free-nonheap-object)

add_executable(furuyoni-cli src/tools/main.cpp)
target_link_libraries(furuyoni-cli PRIVATE furuyoni)

enable_testing()
add_executable(furuyoni-tests
  src/tests/tests.cpp
  src/tests/engine_tests.cpp
  src/tests/rules_matrix_tests.cpp
)
target_link_libraries(furuyoni-tests PRIVATE furuyoni)
add_test(NAME unit COMMAND furuyoni-tests)

# Run tests from the project root so content paths resolve.
set_tests_properties(unit PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# A deeper randomized sweep (scale with FY_FUZZ_GAMES).
add_test(NAME fuzz COMMAND furuyoni-tests "--test-case=invariants*,engine tolerates*")
set_tests_properties(fuzz PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  ENVIRONMENT "FY_FUZZ_GAMES=60"
  TIMEOUT 1800)
