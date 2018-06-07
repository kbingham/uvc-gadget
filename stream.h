/*
 * UVC gadget test application
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

#ifndef __STREAM_H__
#define __STREAM_H__

struct events;
struct uvc_device;
struct uvc_function_config;
struct v4l2_device;

struct uvc_stream
{
	struct v4l2_device *cap;
	struct uvc_device *uvc;

	struct events *events;
};

struct uvc_stream *uvc_stream_new(const char *uvc_device,
				  const char *cap_device);
void uvc_stream_init_uvc(struct uvc_stream *stream,
			 struct uvc_function_config *fc);
void uvc_stream_set_event_handler(struct uvc_stream *stream,
				  struct events *events);
void uvc_stream_delete(struct uvc_stream *stream);
int uvc_stream_set_format(struct uvc_stream *stream);
void uvc_stream_enable(struct uvc_stream *stream, int enable);

#endif /* __STREAM_H__ */
