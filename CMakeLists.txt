cmake_minimum_required(VERSION 3.24)
project(CruelWorld)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
option(CRUEL_WORLD_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)

FetchContent_Declare(
  libmw
  GIT_REPOSITORY https://github.com/MetroWind/libmw.git
)

FetchContent_Declare(
  macrodown
  GIT_REPOSITORY https://git.xeno.darksair.org/macrodown.git
)

FetchContent_Declare(
  ryml
  GIT_REPOSITORY https://github.com/biojppm/rapidyaml.git
  GIT_SHALLOW FALSE  # ensure submodules are checked out
)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_Declare(
  inja
  GIT_REPOSITORY https://github.com/pantor/inja.git
  GIT_TAG main
)

set(LIBMW_BUILD_URL ON)
set(LIBMW_BUILD_SQLITE ON)
set(LIBMW_BUILD_HTTP_SERVER ON)
set(LIBMW_BUILD_CRYPTO ON)
set(INJA_USE_EMBEDDED_JSON FALSE)
set(INJA_BUILD_TESTS FALSE)
set(MACRODOWN_BUILD_TESTS FALSE)

FetchContent_MakeAvailable(libmw ryml json inja macrodown)

if(CRUEL_WORLD_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  )
  FetchContent_MakeAvailable(googletest)
endif()

set(SOURCE_FILES_NO_MAIN
  src/config.cpp
  src/db.cpp
  src/crypto_engine.cpp
  src/session_manager.cpp
  src/app.cpp
)

set(SOURCE_FILES
  src/main.cpp
  ${SOURCE_FILES_NO_MAIN}
)

set(LIBS
  mw::mw
  mw::url
  mw::sqlite
  mw::http-server
  mw::crypto
  ryml::ryml
  macrodown_lib
)

set(INCLUDES
  src
  ${libmw_SOURCE_DIR}/includes
  ${json_SOURCE_DIR}/single_include
  ${inja_SOURCE_DIR}/single_include/inja
  ${macrodown_SOURCE_DIR}/include
)

add_executable(cruel_world ${SOURCE_FILES})
set_property(TARGET cruel_world PROPERTY CXX_STANDARD 23)
set_property(TARGET cruel_world PROPERTY CXX_EXTENSIONS FALSE)

set_property(TARGET cruel_world PROPERTY COMPILE_WARNING_AS_ERROR TRUE)
target_compile_options(cruel_world PRIVATE -Wall -Wextra -Wpedantic -Wno-deprecated-literal-operator -Wno-maybe-uninitialized)
target_include_directories(cruel_world PRIVATE ${INCLUDES})
target_link_libraries(cruel_world PRIVATE ${LIBS})

# Sanitize options
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CRUEL_WORLD_BUILD_TESTS)
  target_compile_options(cruel_world PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(cruel_world PRIVATE -fsanitize=address,undefined)
endif()

if(CRUEL_WORLD_BUILD_TESTS)
  set(TEST_FILES
    src/crypto_test.cpp
    src/db_test.cpp
    src/session_test.cpp
    src/config_test.cpp
    src/app_test.cpp
  )

  # ctest --test-dir build
  add_executable(cruel_world_test ${SOURCE_FILES_NO_MAIN} ${TEST_FILES})
  set_property(TARGET cruel_world_test PROPERTY CXX_STANDARD 23)
  set_property(TARGET cruel_world_test PROPERTY COMPILE_WARNING_AS_ERROR TRUE)
  target_compile_options(cruel_world_test PRIVATE -Wall -Wextra -Wpedantic -Wno-deprecated-literal-operator -Wno-maybe-uninitialized -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(cruel_world_test PRIVATE -fsanitize=address,undefined)
  target_include_directories(cruel_world_test PRIVATE
    ${INCLUDES}
    ${googletest_SOURCE_DIR}/googletest/include
    ${googletest_SOURCE_DIR}/googlemock/include
  )

  target_link_libraries(cruel_world_test PRIVATE
    ${LIBS}
    GTest::gtest_main
    GTest::gmock_main
  )

  enable_testing()
  include(GoogleTest)
  gtest_discover_tests(cruel_world_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
