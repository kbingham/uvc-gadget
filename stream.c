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
#include "video-buffers.h"
#include "video-source.h"

/*
 * struct uvc_stream - Representation of a UVC stream
 * @src: video source
 * @uvc: UVC V4L2 output device
 * @events: struct events containing event information
 */
struct uvc_stream
{
	struct video_source *src;
	struct uvc_device *uvc;

	struct events *events;
};

/* ---------------------------------------------------------------------------
 * Video streaming
 */

static void uvc_stream_source_process(void *d, struct video_source *src,
				      struct video_buffer *buffer)
{
	struct uvc_stream *stream = d;

	v4l2_queue_buffer(stream->uvc->vdev, buffer);
}

static void uvc_stream_uvc_process(void *d)
{
	struct uvc_stream *stream = d;
	struct video_buffer buf;
	int ret;

	ret = v4l2_dequeue_buffer(stream->uvc->vdev, &buf);
	if (ret < 0)
		return;

	video_source_queue_buffer(stream->src, &buf);
}

static int uvc_stream_start(struct uvc_stream *stream)
{
	struct video_buffer_set *buffers = NULL;
	int ret;

	printf("Starting video stream.\n");

	/* Allocate and export the buffers on the source. */
	ret = video_source_alloc_buffers(stream->src, 4);
	if (ret < 0) {
		printf("Failed to allocate source buffers: %s (%d)\n",
		       strerror(-ret), -ret);
		return ret;
	}

	ret = video_source_export_buffers(stream->src, &buffers);
	if (ret < 0) {
		printf("Failed to export buffers on source: %s (%d)\n",
		       strerror(-ret), -ret);
		goto error_free_source;
	}

	/* Allocate and import the buffers on the sink. */
	ret = v4l2_alloc_buffers(stream->uvc->vdev, V4L2_MEMORY_DMABUF,
				 buffers->nbufs);
	if (ret < 0) {
		printf("Failed to allocate sink buffers: %s (%d)\n",
		       strerror(-ret), -ret);
		goto error_free_source;
	}

	ret = v4l2_import_buffers(stream->uvc->vdev, buffers);
	if (ret < 0) {
		printf("Failed to import buffers on sink: %s (%d)\n",
		       strerror(-ret), -ret);
		goto error_free_sink;
	}

	/* Start the source and sink. */
	video_source_stream_on(stream->src);
	v4l2_stream_on(stream->uvc->vdev);

	events_watch_fd(stream->events, stream->uvc->vdev->fd, EVENT_WRITE,
			uvc_stream_uvc_process, stream);

	return 0;

error_free_sink:
	v4l2_free_buffers(stream->uvc->vdev);
error_free_source:
	video_source_free_buffers(stream->src);
	if (buffers)
		video_buffer_set_delete(buffers);
	return ret;
}

static int uvc_stream_stop(struct uvc_stream *stream)
{
	printf("Stopping video stream.\n");

	events_unwatch_fd(stream->events, stream->uvc->vdev->fd, EVENT_WRITE);

	v4l2_stream_off(stream->uvc->vdev);
	video_source_stream_off(stream->src);

	v4l2_free_buffers(stream->uvc->vdev);
	video_source_free_buffers(stream->src);

	return 0;
}

void uvc_stream_enable(struct uvc_stream *stream, int enable)
{
	if (enable)
		uvc_stream_start(stream);
	else
		uvc_stream_stop(stream);
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

	return video_source_set_format(stream->src, &fmt);
}

/* ---------------------------------------------------------------------------
 * Stream handling
 */

struct uvc_stream *uvc_stream_new(const char *uvc_device)
{
	struct uvc_stream *stream;

	stream = malloc(sizeof(*stream));
	if (stream == NULL)
		return NULL;

	memset(stream, 0, sizeof(*stream));

	stream->uvc = uvc_open(uvc_device, stream);
	if (stream->uvc == NULL)
		goto error;

	return stream;

error:
	free(stream);
	return NULL;
}

void uvc_stream_delete(struct uvc_stream *stream)
{
	if (stream == NULL)
		return;

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

void uvc_stream_set_video_source(struct uvc_stream *stream,
				 struct video_source *src)
{
	stream->src = src;

	video_source_set_buffer_handler(src, uvc_stream_source_process, stream);
}
