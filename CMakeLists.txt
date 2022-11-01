cmake_minimum_required(VERSION 3.0)
project(uvc-gadget)
set(CMAKE_BUILD_TYPE Release)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -Werror")

include_directories(${CMAKE_BINARY_DIR}/generated)

add_subdirectory(lib)
add_subdirectory(src)

# Generate config.h
configure_file(${CMAKE_SOURCE_DIR}/include/config.h.in
	       ${CMAKE_BINARY_DIR}/generated/config.h)
