# SPDX-License-Identifier: CC0-1.0

libuvcgadget_sources = files([
  'configfs.c',
  'events.c',
  'jpg-source.c',
  'stream.c',
  'test-source.c',
  'uvc.c',
  'v4l2.c',
  'v4l2-source.c',
  'video-buffers.c',
  'video-source.c',
])

libuvcgadget = shared_library('uvcgadget',
                              libuvcgadget_sources,
                              version : uvc_gadget_version,
                              install : true,
                              include_directories : includes)

libuvcgadget_dep = declare_dependency(sources : [
                                          uvcgadget_public_headers,
                                     ],
                                     include_directories : includes,
                                     link_with : libuvcgadget)

pkg_mod = import('pkgconfig')
pkg_mod.generate(libuvcgadget,
                 description : 'UVC Gadget support library',
                 subdirs : 'uvcgadget')

meson.override_dependency('uvcgadget', libuvcgadget_dep)
