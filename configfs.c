/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * ConfigFS Gadget device handling
 *
 * Copyright (C) 2018 Kieran Bingham
 *
 * Contact: Kieran Bingham <kieran.bingham@ideasonboard.com>
 */

/* To provide basename and asprintf from the GNU library. */
 #define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "configfs.h"

/* -----------------------------------------------------------------------------
 * Path handling and support
 */

static char *path_join(const char *dirname, const char *name)
{
	char *path;

	asprintf(&path, "%s/%s", dirname, name);

	return path;
}

static char *path_glob_first_match(const char *g)
{
	glob_t globbuf;
	char *match = NULL;

	glob(g, 0, NULL, &globbuf);

	if (globbuf.gl_pathc)
		match = strdup(globbuf.gl_pathv[0]);

	globfree(&globbuf);

	return match;
}

/* -----------------------------------------------------------------------------
 * Attribute handling
 */

static int attribute_read(const char *path, const char *file, char *buf,
			  unsigned int len)
{
	char *f;
	int ret;
	int fd;

	f = path_join(path, file);
	if (!f)
		return -ENOMEM;

	fd = open(f, O_RDONLY);
	free(f);
	if (fd == -1) {
		printf("Failed to open attribute %s: %s\n", file,
		       strerror(errno));
		return -ENOENT;
	}

	ret = read(fd, buf, len - 1);
	close(fd);

	if (ret < 0) {
		printf("Failed to read attribute %s: %s\n", file,
		       strerror(errno));
		return -ENODATA;
	}

	buf[ret] = '\0';

	return 0;
}

static int attribute_read_uint(const char *path, const char *file,
			       unsigned int *val)
{
	/* 4,294,967,295 */
	char buf[11];
	char *endptr;
	int ret;

	ret = attribute_read(path, file, buf, sizeof(buf));
	if (ret)
		return ret;

	errno = 0;

	/* base 0: Autodetect hex, octal, decimal. */
	*val = strtoul(buf, &endptr, 0);
	if (errno)
		return -errno;

	if (endptr == buf)
		return -ENODATA;

	return 0;
}

static char *attribute_read_str(const char *path, const char *file)
{
	char buf[1024];
	char *p;
	int ret;

	ret = attribute_read(path, file, buf, sizeof(buf));
	if (ret)
		return NULL;

	p = strrchr(buf, '\n');
	if (p != buf)
		*p = '\0';

	return strdup(buf);
}

/* -----------------------------------------------------------------------------
 * UDC parsing
 */

/*
 * udc_find_video_device - Find the video device node for a UVC function
 * @udc: The UDC name
 * @function: The UVC function name
 *
 * This function finds the video device node corresponding to a UVC function as
 * specified by a @function name and @udc name.
 *
 * The @function parameter specifies the name of the USB function, usually in
 * the form "uvc.%u". If NULL the first function found will be used.
 *
 * The @udc parameter specifies the name of the UDC. If NULL any UDC that
 * contains a function matching the @function name will be used.
 *
 * Return a pointer to a newly allocated string containing the video device node
 * full path if the function is found. Otherwise return NULL. The returned
 * pointer must be freed by the caller with a call to free().
 */
static char *udc_find_video_device(const char *udc, const char *function)
{
	char *vpath;
	char *video = NULL;
	glob_t globbuf;
	unsigned int i;

	asprintf(&vpath, "/sys/class/udc/%s/device/gadget/video4linux/video*",
		 udc ? udc : "*");
	if (!vpath)
		return NULL;

	glob(vpath, 0, NULL, &globbuf);
	free(vpath);

	for (i = 0; i < globbuf.gl_pathc; ++i) {
		char *config;
		bool match;

		/* Match on the first if no search string. */
		if (!function)
			break;

		config = attribute_read_str(globbuf.gl_pathv[i],
					    "function_name");
		match = strcmp(function, config) == 0;

		free(config);

		if (match)
			break;
	}

	if (i < globbuf.gl_pathc) {
		const char *v = basename(globbuf.gl_pathv[i]);

		video = path_join("/dev", v);
	}

	globfree(&globbuf);

	return video;
}

/* -----------------------------------------------------------------------------
 * Legacy g_webcam support
 */

static const struct uvc_function_config g_webcam_config = {
	.control = {
		.intf = {
			.bInterfaceNumber = 0,
		},
	},
	.streaming = {
		.intf = {
			.bInterfaceNumber = 1,
		},
		.ep = {
			.bInterval = 1,
			.bMaxBurst = 0,
			.wMaxPacketSize = 1024,
		},
	},
};

static int parse_legacy_g_webcam(const char *udc,
				 struct uvc_function_config *fc)
{
	*fc = g_webcam_config;

	fc->video = udc_find_video_device(udc, NULL);

	return fc->video ? 0 : -ENODEV;
}

/* -----------------------------------------------------------------------------
 * ConfigFS support
 */

/*
 * configfs_find_uvc_function - Find the ConfigFS full path for a UVC function
 * @function: The UVC function name
 *
 * Return a pointer to a newly allocated string containing the full ConfigFS
 * path to the function if the function is found. Otherwise return NULL. The
 * returned pointer must be freed by the caller with a call to free().
 */
static char *configfs_find_uvc_function(const char *function)
{
	const char *target = function ? function : "*";
	const char *root;
	char *func_path;
	char *path;

	/*
	 * The function description can be provided as a path from the
	 * usb_gadget root "g1/functions/uvc.0", or if there is no ambiguity
	 * over the gadget name, a shortcut "uvc.0" can be provided.
	 */
	if (!strchr(target, '/'))
		root = "/sys/kernel/config/usb_gadget/*/functions";
	else
		root = "/sys/kernel/config/usb_gadget";

	path = path_join(root, target);

	func_path = path_glob_first_match(path);
	free(path);

	return func_path;
}

/*
 * configfs_free_uvc_function - Free a uvc_function_config object
 * @fc: The uvc_function_config to be freed
 *
 * Free the given @fc function previously allocated by a call to
 * configfs_parse_uvc_function().
 */
void configfs_free_uvc_function(struct uvc_function_config *fc)
{
	free(fc->udc);
	free(fc->video);

	free(fc);
}

#define configfs_parse_child(parent, child, cfg, parse)			\
({									\
	char *__path;							\
	int __ret;							\
									\
	__path = path_join((parent), (child));				\
	if (__path) {							\
		__ret = parse(__path, (cfg));				\
		free(__path);						\
	} else {							\
		__ret = -ENOMEM;					\
	}								\
									\
	__ret;								\
})

static int configfs_parse_interface(const char *path,
				    struct uvc_function_config_interface *cfg)
{
	int ret;

	ret = attribute_read_uint(path, "bInterfaceNumber",
				  &cfg->bInterfaceNumber);

	return ret;
}

static int configfs_parse_control(const char *path,
				  struct uvc_function_config_control *cfg)
{
	int ret;

	ret = configfs_parse_interface(path, &cfg->intf);

	return ret;
}

static int configfs_parse_streaming(const char *path,
				    struct uvc_function_config_streaming *cfg)
{
	int ret;

	ret = configfs_parse_interface(path, &cfg->intf);

	return ret;
}

static int configfs_parse_uvc(const char *fpath,
			      struct uvc_function_config *fc)
{
	int ret = 0;

	ret = ret ? : configfs_parse_child(fpath, "control", &fc->control,
					   configfs_parse_control);
	ret = ret ? : configfs_parse_child(fpath, "streaming", &fc->streaming,
					   configfs_parse_streaming);

	/*
	 * These parameters should be part of the streaming interface in
	 * ConfigFS, but for legacy reasons they are located directly in the
	 * function directory.
	 */
	ret = ret ? : attribute_read_uint(fpath, "streaming_interval",
					  &fc->streaming.ep.bInterval);
	ret = ret ? : attribute_read_uint(fpath, "streaming_maxburst",
					  &fc->streaming.ep.bMaxBurst);
	ret = ret ? : attribute_read_uint(fpath, "streaming_maxpacket",
					  &fc->streaming.ep.wMaxPacketSize);

	return ret;
}

/*
 * configfs_parse_uvc_function - Parse a UVC function configuration in ConfigFS
 * @function: The function name
 *
 * This function locates and parse the configuration of a UVC function in
 * ConfigFS as specified by the @function name argument. The function name can
 * be fully qualified with a gadget name (e.g. "g%u/functions/uvc.%u"), or as a
 * shortcut can be an unqualified function name (e.g. "uvc.%u"). When the
 * function name is unqualified, the first function matching the name in any
 * UDC will be returned.
 *
 * Return a pointer to a newly allocated UVC function configuration structure
 * that contains configuration parameters for the function, if the function is
 * found. Otherwise return NULL. The returned pointer must be freed by the
 * caller with a call to free().
 */
struct uvc_function_config *configfs_parse_uvc_function(const char *function)
{
	struct uvc_function_config *fc;
	char *fpath;
	int ret = 0;

	fc = malloc(sizeof *fc);
	if (fc == NULL)
		return NULL;

	memset(fc, 0, sizeof *fc);

	/* Find the function in ConfigFS. */
	fpath = configfs_find_uvc_function(function);
	if (!fpath) {
		/*
		 * If the function can't be found attempt legacy parsing to
		 * support the g_webcam gadget. The function parameter contains
		 * a UDC name in that case.
		 */
		ret = parse_legacy_g_webcam(function, fc);
		if (ret) {
			configfs_free_uvc_function(fc);
			fc = NULL;
		}

		return fc;
	}

	/*
	 * Parse the function configuration. Remove the gadget name qualifier
	 * from the function name, if any.
	 */
	if (function)
		function = basename(function);

	fc->udc = attribute_read_str(fpath, "../../UDC");
	fc->video = udc_find_video_device(fc->udc, function);
	if (!fc->video) {
		ret = -ENODEV;
		goto done;
	}

	ret = configfs_parse_uvc(fpath, fc);

done:
	if (ret) {
		configfs_free_uvc_function(fc);
		fc = NULL;
	}

	free(fpath);

	return fc;
}
