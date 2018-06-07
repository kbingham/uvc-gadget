/*
 * UVC protocol handling
 *
 * Copyright (C) 2010 Ideas on board SPRL <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 */

#ifndef __UVC_H__
#define __UVC_H__

#include <linux/usb/video.h>

struct v4l2_device;
struct uvc_function_config;

struct uvc_device
{
	struct v4l2_device *vdev;

	struct uvc_function_config *fc;

	struct uvc_streaming_control probe;
	struct uvc_streaming_control commit;

	int control;

	unsigned int fcc;
	unsigned int width;
	unsigned int height;
	unsigned int maxsize;
};

void uvc_events_process(void *d);
struct uvc_device *uvc_open(const char *devname);
void uvc_close(struct uvc_device *dev);
void uvc_events_init(struct uvc_device *dev);

#endif /* __UVC_H__ */
