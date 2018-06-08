/*
 * UVC stream handling
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "stream.h"
#include "uvc.h"
#include "v4l2.h"

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

/* ---------------------------------------------------------------------------
 * Video streaming
 */

static void capture_video_process(void *d)
{
	struct uvc_stream *stream = d;
	struct v4l2_video_buffer buf;
	int ret;

	ret = v4l2_dequeue_buffer(stream->cap, &buf);
	if (ret < 0)
		return;

	v4l2_queue_buffer(stream->uvc->vdev, &buf);
}

static void uvc_video_process(void *d)
{
	struct uvc_stream *stream = d;
	struct v4l2_video_buffer buf;
	int ret;

	ret = v4l2_dequeue_buffer(stream->uvc->vdev, &buf);
	if (ret < 0)
		return;

	v4l2_queue_buffer(stream->cap, &buf);
}

static int uvc_video_enable(struct uvc_stream *stream, int enable)
{
	struct v4l2_device *cap = stream->cap;
	struct uvc_device *dev = stream->uvc;
	int ret;

	if (!enable) {
		printf("Stopping video stream.\n");
		events_unwatch_fd(stream->events, dev->vdev->fd, EVENT_WRITE);
		v4l2_stream_off(dev->vdev);
		v4l2_free_buffers(dev->vdev);
		return 0;
	}

	printf("Starting video stream.\n");

	ret = v4l2_alloc_buffers(dev->vdev, V4L2_MEMORY_DMABUF, 4);
	if (ret < 0) {
		printf("Failed to allocate buffers: %s (%d)\n", strerror(-ret), -ret);
		return ret;
	}

	ret = v4l2_import_buffers(dev->vdev, dev->vdev->nbufs, cap->buffers);
	if (ret < 0) {
		printf("Failed to import buffers: %s (%d)\n", strerror(-ret), -ret);
		goto error;
	}

	v4l2_stream_on(dev->vdev);
	events_watch_fd(stream->events, dev->vdev->fd, EVENT_WRITE,
			uvc_video_process, stream);

	return 0;

error:
	v4l2_free_buffers(dev->vdev);
	return ret;
}

static int capture_video_stream(struct uvc_stream *stream, int enable)
{
	struct v4l2_device *cap = stream->cap;
	unsigned int i;
	int ret;

	if (!enable) {
		printf("Stopping video capture stream.\n");
		events_unwatch_fd(stream->events, cap->fd, EVENT_READ);
		v4l2_stream_off(cap);
		v4l2_free_buffers(cap);
		return 0;
	}

	printf("Starting video capture stream.\n");

	ret = v4l2_alloc_buffers(cap, V4L2_MEMORY_MMAP, 4);
	if (ret < 0) {
		printf("Failed to allocate capture buffers.\n");
		return ret;
	}

	ret = v4l2_export_buffers(cap);
	if (ret < 0) {
		printf("Failed to export buffers.\n");
		goto error;
	}

	for (i = 0; i < cap->nbufs; ++i) {
		struct v4l2_video_buffer *buf = &cap->buffers[i];

		ret = v4l2_queue_buffer(cap, buf);
		if (ret < 0)
			goto error;
	}

	v4l2_stream_on(cap);
	events_watch_fd(stream->events, cap->fd, EVENT_READ,
			capture_video_process, stream);

	return 0;

error:
	v4l2_free_buffers(cap);
	return ret;
}

void uvc_stream_enable(struct uvc_stream *stream, int enable)
{
	if (enable) {
		capture_video_stream(stream, 1);
		uvc_video_enable(stream, 1);
	} else {
		uvc_video_enable(stream, 0);
		capture_video_stream(stream, 0);
	}
}

int uvc_stream_set_format(struct uvc_stream *stream,
			  const struct v4l2_pix_format *format)
{
	struct v4l2_pix_format fmt = *format;
	int ret;

	printf("Setting format to 0x%08x %ux%u\n",
		format->pixelformat, format->width, format->height);

	ret = uvc_set_format(stream->uvc, &fmt);
	if (ret < 0)
		return ret;

	return v4l2_set_format(stream->cap, &fmt);
}

/* ---------------------------------------------------------------------------
 * Stream handling
 */

struct uvc_stream *uvc_stream_new(const char *uvc_device,
				  const char *cap_device)
{
	struct uvc_stream *stream;

	stream = malloc(sizeof(*stream));
	if (stream == NULL)
		return NULL;

	memset(stream, 0, sizeof(*stream));

	stream->cap = v4l2_open(cap_device);
	if (stream->cap == NULL)
		goto error;

	stream->uvc = uvc_open(uvc_device, stream);
	if (stream->uvc == NULL)
		goto error;

	return stream;

error:
	if (stream->cap)
		v4l2_close(stream->cap);

	free(stream);
	return NULL;
}

void uvc_stream_delete(struct uvc_stream *stream)
{
	if (stream == NULL)
		return;

	v4l2_close(stream->cap);
	uvc_close(stream->uvc);

	free(stream);
}

void uvc_stream_init_uvc(struct uvc_stream *stream,
			 struct uvc_function_config *fc)
{
	uvc_set_config(stream->uvc, fc);
	uvc_events_init(stream->uvc, stream->events);
}

void uvc_stream_set_event_handler(struct uvc_stream *stream,
				  struct events *events)
{
	stream->events = events;
}
