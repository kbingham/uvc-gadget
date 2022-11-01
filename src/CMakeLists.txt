add_executable(uvc-gadget main.c)
target_link_libraries(uvc-gadget uvcgadget)
install(TARGETS uvc-gadget DESTINATION bin)
