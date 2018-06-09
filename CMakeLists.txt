cmake_minimum_required(VERSION 3.0)
project(uvc-gadget)
set(CMAKE_BUILD_TYPE Release)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -Werror")

add_subdirectory(lib)
add_executable(uvc-gadget main.c)
target_link_libraries(uvc-gadget uvcgadget)
install(TARGETS uvc-gadget DESTINATION bin)
