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

/*
 * struct uvc_stream - Representation of a UVC stream
 * @cap: V4L2 capture device
 * @uvc: UVC V4L2 output device
 * @events: struct events containing event information
 */
struct uvc_stream
{
	struct v4l2_device *cap;
	struct uvc_device *uvc;

	struct events *events;
};

/*
 * uvc_stream_new - Create a new UVC stream
 * @uvc_device: Filename of UVC device node
 * @cap_device: Filename of V4L2 capture device node
 *
 * Create a new UVC stream with V4L2 @uvc_device as the output and @cap_device
 * as input.
 *
 * Streams allocated with this function can be deleted with uvc_stream_delete().
 *
 * On success, returns a pointer to newly allocated and populated struct uvc_stream.
 * On failure, returns NULL.
 */
struct uvc_stream *uvc_stream_new(const char *uvc_device,
				  const char *cap_device);

/*
 * uvc_stream_init_uvc - Initialize a UVC stream
 * @stream: the UVC stream
 * @fc: UVC function configuration
 *
 * Before it can be used, a UVC stream has to be initialized by calling this
 * function with a UVC function configuration @fc. The function configuration
 * contains all the parameters of the UVC function that will be handled by the
 * UVC stream. It can be parsed from the UVC function ConfigFS directory using
 * configfs_parse_uvc_function().
 *
 * uvc_stream_init_uvc() also registers UVC event notifiers for the stream. The
 * caller must have called the uvc_stream_set_event_handler() function first,
 * and ensure that the event handler is immediately usable. If the event loop is
 * already running, all initialization steps required to handle events must be
 * fully performed before calling this function.
 */
void uvc_stream_init_uvc(struct uvc_stream *stream,
			 struct uvc_function_config *fc);

/*
 * uvc_stream_set_event_handler - Set an event handler for a stream
 * @stream: the UVC stream
 * @events: the event handler
 *
 * This function sets the event handler that the stream can use to be notified
 * of file descriptor events.
 */
void uvc_stream_set_event_handler(struct uvc_stream *stream,
				  struct events *events);

/*
 * uvc_stream_delete - Delete a UVC stream
 * @stream: the UVC stream
 *
 * This functions deletes the @stream created with uvc_stream_new(). Upon return
 * the stream object may be freed, the @stream pointer thus becomes invalid and
 * the stream must not be touched anymore.
 *
 * Every stream allocated with uvc_stream_new() must be deleted when not needed
 * anymore.
 */
void uvc_stream_delete(struct uvc_stream *stream);

/*
 * uvc_stream_set_format - Set the active video format for the stream
 * @stream: the UVC stream
 *
 * This function is called from the UVC protocol handler to configure the video
 * format for the @stream. It must not be called directly by applications.
 *
 * Returns 0 on success, or a negative error code on failure.
 */
int uvc_stream_set_format(struct uvc_stream *stream);

/*
 * uvc_stream_enable - Turn on/off video streaming for the UVC stream
 * @stream: the UVC stream
 * @enable: 0 to stop the stream, 1 to start it
 *
 * This function is called from the UVC protocol handler to start video transfer
 * for the @stream. It must not be called directly by applications.
 */
void uvc_stream_enable(struct uvc_stream *stream, int enable);

#endif /* __STREAM_H__ */
