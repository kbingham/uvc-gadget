/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * ConfigFS Gadget device handling
 *
 * Copyright (C) 2018 Kieran Bingham
 *
 * Contact: Kieran Bingham <kieran.bingham@ideasonboard.com>
 */

#ifndef __CONFIGFS_H__
#define __CONFIGFS_H__

struct uvc_function_config {
	char *video;
	char *udc;

	unsigned int streaming_interval;
	unsigned int streaming_maxburst;
	unsigned int streaming_maxpacket;
};

struct uvc_function_config *configfs_parse_uvc_function(const char *function);
void configfs_free_uvc_function(struct uvc_function_config *fc);

#endif
