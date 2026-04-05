cmake_minimum_required(VERSION 3.16)

project(sc4 LANGUAGES C)

# Generate compile_commands.json for IDE/language server support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Custom find-modules live in cmake/
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------- Tilengine dependency ----------
# Point TILENGINE_DIR at the Tilengine source tree if not installed system-wide:
#   cmake -DTILENGINE_DIR=~/dev/Tilengine ..
find_package(Tilengine REQUIRED)

if(UNIX AND NOT APPLE)
  set(CMAKE_C_FLAGS "-m64 -msse2")
elseif(UNIX AND APPLE)
  # nothing – per-target linking below
endif()

find_package(SDL3 REQUIRED CONFIG)

# Collect Tilengine's transitive dependencies so they appear AFTER the static
# library on the link line (required for static archive symbol resolution).
set(TILENGINE_TRANSITIVE_DEPS "z" "png" SDL3::SDL3)
if(WIN32)
  list(APPEND TILENGINE_TRANSITIVE_DEPS "winmm")
endif()
if(UNIX AND APPLE)
  list(APPEND TILENGINE_TRANSITIVE_DEPS "framework")
endif()
if(UNIX)
  list(APPEND TILENGINE_TRANSITIVE_DEPS "c" "m")
endif()

# Compiler-specific flags
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/W3)
else()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wextra")
  if(NOT WIN32)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fpic")
  endif()
  set(CMAKE_C_FLAGS
      "${CMAKE_C_FLAGS} -Wno-unused-result -Wno-format-truncation")
endif()

# Sanitizers: cmake -DSANITIZE=ON ..
option(SANITIZE "Enable AddressSanitizer + UBSan" OFF)
if(SANITIZE AND NOT MSVC)
  message(STATUS "Sanitizers enabled")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g
                      -O1)
  add_link_options(-fsanitize=address,undefined)
endif()

# Copy assets at build time so changes to source assets are reflected on every
# incremental build, not only when CMake is re-configured. The destination is
# removed first so deleted source files don't linger.
add_custom_target(
  copy_sc4_assets ALL
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/assets"
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "${CMAKE_CURRENT_BINARY_DIR}/assets"
  COMMENT "Syncing assets to build directory")

add_executable(
  intro
  src/main/Intro.c
  src/main/Profiling.c
  src/main/Simon.c
  src/main/SimonCollision.c
  src/main/Sandblock.c
  src/main/Torch.c
  src/main/Prop.c
  src/main/Hud.c
  src/main/Drawbridge.c
  src/main/Whip.c)

target_include_directories(intro PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/main
                                         ${TILENGINE_INCLUDE_DIRS})
target_link_libraries(intro PRIVATE ${TILENGINE_LIBRARIES} ${TILENGINE_TRANSITIVE_DEPS})

# Unit test for SimonCollision (self-contained, no Tilengine/SDL link needed)
add_executable(test_collision src/test/test_collision.c)
target_include_directories(test_collision PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/main
                                                  ${TILENGINE_INCLUDE_DIRS})
target_compile_definitions(test_collision PRIVATE LIB_EXPORTS)
# Unlink the default libraries — the test provides its own mocks.
set_target_properties(test_collision PROPERTIES LINK_LIBRARIES "")
enable_testing()
add_test(NAME SimonCollision COMMAND test_collision)

# Coverage: cmake -DCOVERAGE=ON .. && cmake --build . --target coverage
option(COVERAGE "Build test_collision with gcov coverage instrumentation" OFF)
if(COVERAGE AND NOT MSVC)
  target_compile_options(test_collision PRIVATE --coverage -O0 -g)
  target_link_options(test_collision PRIVATE --coverage)
  find_program(GCOVR gcovr)
  if(GCOVR)
    add_custom_target(
      coverage
      COMMAND test_collision
      COMMAND ${GCOVR} --root ${CMAKE_CURRENT_SOURCE_DIR}
              --filter ${CMAKE_CURRENT_SOURCE_DIR}/src/main/SimonCollision.c
              --print-summary
              --html-details ${CMAKE_CURRENT_BINARY_DIR}/coverage.html
              --txt ${CMAKE_CURRENT_BINARY_DIR}/coverage.txt
              ${CMAKE_CURRENT_BINARY_DIR}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      DEPENDS test_collision
      COMMENT "Running tests and generating coverage report")
  else()
    message(WARNING "gcovr not found — install with: pip install gcovr")
  endif()
endif()

# Master list of all sc4 targets, used for dependency wiring below.
set(SC4_TARGETS intro)

# Must wait for assets to be in the build directory.
foreach(SC4_TARGET ${SC4_TARGETS})
  add_dependencies(${SC4_TARGET} copy_sc4_assets)
endforeach()

# Copy required DLLs on MinGW once
if(MINGW)
  if(EXISTS "C:/msys64/mingw64/bin")
    set(MINGW_BIN_DIR "C:/msys64/mingw64/bin")
  endif()

  if(DEFINED MINGW_BIN_DIR)
    add_custom_target(
      copy_sc4_dlls
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different "${MINGW_BIN_DIR}/libpng16-16.dll"
        "${MINGW_BIN_DIR}/zlib1.dll" "${MINGW_BIN_DIR}/SDL3.dll"
        "${CMAKE_CURRENT_BINARY_DIR}"
      COMMENT "Copying required DLLs to sc4 directory")

    foreach(SC4_TARGET ${SC4_TARGETS})
      add_dependencies(${SC4_TARGET} copy_sc4_dlls)
    endforeach()
  endif()
endif()
