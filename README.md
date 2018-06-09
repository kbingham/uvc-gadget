# uvcgadget - UVC gadget C library

uvcgadget is a pure C library that implements handling of UVC gadget functions.

## Utilities

- uvc-gadget - Sample test application

## Build instructions:

To compile:

```
$ mkdir build
$ cd build
$ cmake ..
$ make -j4
```

## Cross compiling instructions:

Directions for cross compiling depend on the build environment.

For buildroot-based builds, cmake can be pointed to the toolchain file provided
by buildroot:

```
$ mkdir build
$ cd build
$ cmake -DCMAKE_TOOLCHAIN_FILE=<buildrootpath>/output/host/usr/share/buildroot/toolchainfile.cmake ..
$ make -j4
```

If your build environment doesn't provide a CMake toolchain file, the following
template can be used as a starting point.

```
set(CMAKE_SYSTEM_NAME Linux)

set(BUILD_ENV_ROOT "/path/to/your/build/enviroment/root/")

# Specify the cross compiler
set(CMAKE_C_COMPILER   ${BUILD_ENV_ROOT}/host/usr/bin/arm-buildroot-linux-gnueabihf-gcc)

# Where is the target environment
set(CMAKE_FIND_ROOT_PATH ${BUILD_ENV_ROOT}/target ${BUILD_ENV_ROOT}/host)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```
