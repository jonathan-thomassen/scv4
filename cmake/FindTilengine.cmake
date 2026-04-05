# FindTilengine.cmake
#
# Locate the Tilengine library and headers.
#
# Set TILENGINE_DIR to the root of the Tilengine source tree if it is not
# installed system-wide. Example:
#   cmake -DTILENGINE_DIR=~/dev/Tilengine ..
#
# This module defines:
#   Tilengine_FOUND        - TRUE if headers and library are found
#   TILENGINE_INCLUDE_DIRS - Include directories (public + internal headers)
#   TILENGINE_LIBRARIES    - Libraries to link against

# Allow the user to point at a Tilengine source/install tree.
set(TILENGINE_DIR "" CACHE PATH "Root of the Tilengine source or install tree")

# --- headers ---
find_path(TILENGINE_INCLUDE_DIR
  NAMES Tilengine.h
  HINTS
    "${TILENGINE_DIR}/src"
    "${TILENGINE_DIR}/include"
    "${TILENGINE_DIR}"
  PATH_SUFFIXES include src
)

# Internal headers (LoadFile.h, Sprite.h, Draw.h) live in src/
find_path(TILENGINE_INTERNAL_INCLUDE_DIR
  NAMES LoadFile.h
  HINTS
    "${TILENGINE_DIR}/src"
    "${TILENGINE_DIR}"
  PATH_SUFFIXES src
)

# --- library ---
find_library(TILENGINE_LIBRARY
  NAMES Tilengine
  HINTS
    "${TILENGINE_DIR}/build"
    "${TILENGINE_DIR}/lib"
    "${TILENGINE_DIR}"
  PATH_SUFFIXES lib build
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Tilengine
  REQUIRED_VARS TILENGINE_LIBRARY TILENGINE_INCLUDE_DIR
)

if(Tilengine_FOUND)
  set(TILENGINE_LIBRARIES    "${TILENGINE_LIBRARY}")
  set(TILENGINE_INCLUDE_DIRS "${TILENGINE_INCLUDE_DIR}")
  if(TILENGINE_INTERNAL_INCLUDE_DIR)
    list(APPEND TILENGINE_INCLUDE_DIRS "${TILENGINE_INTERNAL_INCLUDE_DIR}")
  endif()

  # Provide an imported target for modern CMake usage.
  if(NOT TARGET Tilengine::Tilengine)
    add_library(Tilengine::Tilengine UNKNOWN IMPORTED)
    set_target_properties(Tilengine::Tilengine PROPERTIES
      IMPORTED_LOCATION "${TILENGINE_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${TILENGINE_INCLUDE_DIRS}"
    )
  endif()
endif()
