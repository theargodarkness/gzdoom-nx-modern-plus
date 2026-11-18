# Native build of the tools the Switch (cross) build needs: lemon, re2c and zipdir.
#
#   cmake -S tools/host -B build-host -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-host
#
# then pass -DIMPORT_EXECUTABLES=<build-host>/ImportExecutables.cmake to the Switch configure.
cmake_minimum_required( VERSION 3.16 )
project( GZDoomHostTools C CXX )

macro( make_release_only )
endmacro()

set( GZDOOM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../.." )

# lemon is old K&R-style C; newer compilers default to C23, which rejects it.
set( CMAKE_C_STANDARD 17 )
set( CMAKE_C_EXTENSIONS ON )

find_package( BZip2 REQUIRED )

set( LZMA_INCLUDE_DIR "${GZDOOM_SRC}/libraries/lzma/C" )
add_subdirectory( "${GZDOOM_SRC}/libraries/lzma" lzma )
add_subdirectory( "${GZDOOM_SRC}/libraries/miniz" miniz )

add_subdirectory( "${GZDOOM_SRC}/tools/re2c" re2c )
add_subdirectory( "${GZDOOM_SRC}/tools/lemon" lemon )
add_subdirectory( "${GZDOOM_SRC}/tools/zipdir" zipdir )

export( TARGETS re2c lemon zipdir FILE "${CMAKE_BINARY_DIR}/ImportExecutables.cmake" )
