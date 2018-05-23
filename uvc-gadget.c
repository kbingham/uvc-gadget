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

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/usb/ch9.h>
#include <linux/usb/g_uvc.h>
#include <linux/usb/video.h>
#include <linux/videodev2.h>

#include "events.h"
#include "tools.h"
#include "v4l2.h"

#define UVC_INTF_CONTROL	0
#define UVC_INTF_STREAMING	1

struct uvc_device
{
	struct v4l2_device *vdev;

	struct uvc_streaming_control probe;
	struct uvc_streaming_control commit;

	int control;

	unsigned int fcc;
	unsigned int width;
	unsigned int height;
	unsigned int maxsize;
};

struct uvc_stream
{
	struct v4l2_device *cap;
	struct uvc_device *uvc;

	struct events *events;
};

static struct uvc_device *
uvc_open(const char *devname)
{
	struct uvc_device *dev;

	dev = malloc(sizeof *dev);
	if (dev == NULL)
		return NULL;

	memset(dev, 0, sizeof *dev);

	dev->vdev = v4l2_open(devname);
	if (dev->vdev == NULL) {
		free(dev);
		return NULL;
	}

	return dev;
}

static void
uvc_close(struct uvc_device *dev)
{
	v4l2_close(dev->vdev);
	dev->vdev = NULL;

	free(dev);
}

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

static void
uvc_video_process(void *d)
{
	struct uvc_stream *stream = d;
	struct v4l2_video_buffer buf;
	int ret;

	ret = v4l2_dequeue_buffer(stream->uvc->vdev, &buf);
	if (ret < 0)
		return;

	v4l2_queue_buffer(stream->cap, &buf);
}

static int
uvc_video_stream(struct uvc_stream *stream, int enable)
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

static int
uvc_video_set_format(struct uvc_stream *stream)
{
	struct v4l2_device *cap = stream->cap;
	struct uvc_device *dev = stream->uvc;
	struct v4l2_pix_format fmt;
	int ret;

	printf("Setting format to 0x%08x %ux%u\n",
		dev->fcc, dev->width, dev->height);

	memset(&fmt, 0, sizeof fmt);
	fmt.width = dev->width;
	fmt.height = dev->height;
	fmt.pixelformat = dev->fcc;
	fmt.field = V4L2_FIELD_NONE;
	if (dev->fcc == V4L2_PIX_FMT_MJPEG)
		fmt.sizeimage = dev->maxsize * 1.5;

	ret = v4l2_set_format(dev->vdev, &fmt);
	if (ret < 0)
		return ret;

	return v4l2_set_format(cap, &fmt);
}

static int
uvc_video_init(struct uvc_device *dev __attribute__((__unused__)))
{
	return 0;
}

/* ---------------------------------------------------------------------------
 * Request processing
 */

struct uvc_frame_info
{
	unsigned int width;
	unsigned int height;
	unsigned int intervals[8];
};

struct uvc_format_info
{
	unsigned int fcc;
	const struct uvc_frame_info *frames;
};

static const struct uvc_frame_info uvc_frames_yuyv[] = {
	{  640, 360, { 666666, 10000000, 50000000, 0 }, },
	{ 1280, 720, { 50000000, 0 }, },
	{ 0, 0, { 0, }, },
};

static const struct uvc_frame_info uvc_frames_mjpeg[] = {
	{  640, 360, { 666666, 10000000, 50000000, 0 }, },
	{ 1280, 720, { 50000000, 0 }, },
	{ 0, 0, { 0, }, },
};

static const struct uvc_format_info uvc_formats[] = {
	{ V4L2_PIX_FMT_YUYV, uvc_frames_yuyv },
	{ V4L2_PIX_FMT_MJPEG, uvc_frames_mjpeg },
};

static void
uvc_fill_streaming_control(struct uvc_device *dev,
			   struct uvc_streaming_control *ctrl,
			   int iformat, int iframe, unsigned int ival)
{
	const struct uvc_format_info *format;
	const struct uvc_frame_info *frame;
	const unsigned int *interval;
	unsigned int nframes;

	/*
	 * Restrict the iformat, iframe and ival to valid values. Negative
	 * values for iformat or iframe will result in the maximum valid value
	 * being selected.
	 */
        iformat = clamp((unsigned int)iformat, 1U,
                        (unsigned int)ARRAY_SIZE(uvc_formats));
	format = &uvc_formats[iformat-1];

	nframes = 0;
	while (format->frames[nframes].width != 0)
		++nframes;

	iframe = clamp((unsigned int)iframe, 1U, nframes);
	frame = &format->frames[iframe-1];

	interval = frame->intervals;
	while (interval[0] < ival && interval[1])
		++interval;

	memset(ctrl, 0, sizeof *ctrl);

	ctrl->bmHint = 1;
	ctrl->bFormatIndex = iformat;
	ctrl->bFrameIndex = iframe ;
	ctrl->dwFrameInterval = *interval;

	switch (format->fcc) {
	case V4L2_PIX_FMT_YUYV:
		ctrl->dwMaxVideoFrameSize = frame->width * frame->height * 2;
		break;
	case V4L2_PIX_FMT_MJPEG:
		ctrl->dwMaxVideoFrameSize = dev->maxsize;
		break;
	}

	ctrl->dwMaxPayloadTransferSize = 512;	/* TODO this should be filled by the driver. */
	ctrl->bmFramingInfo = 3;
	ctrl->bPreferedVersion = 1;
	ctrl->bMaxVersion = 1;
}

static void
uvc_events_process_standard(struct uvc_device *dev,
			    const struct usb_ctrlrequest *ctrl,
			    struct uvc_request_data *resp)
{
	printf("standard request\n");
	(void)dev;
	(void)ctrl;
	(void)resp;
}

static void
uvc_events_process_control(struct uvc_device *dev, uint8_t req, uint8_t cs,
			   struct uvc_request_data *resp)
{
	printf("control request (req %02x cs %02x)\n", req, cs);
	(void)dev;
	(void)resp;
}

static void
uvc_events_process_streaming(struct uvc_device *dev, uint8_t req, uint8_t cs,
			     struct uvc_request_data *resp)
{
	struct uvc_streaming_control *ctrl;

	printf("streaming request (req %02x cs %02x)\n", req, cs);

	if (cs != UVC_VS_PROBE_CONTROL && cs != UVC_VS_COMMIT_CONTROL)
		return;

	ctrl = (struct uvc_streaming_control *)&resp->data;
	resp->length = sizeof *ctrl;

	switch (req) {
	case UVC_SET_CUR:
		dev->control = cs;
		resp->length = 34;
		break;

	case UVC_GET_CUR:
		if (cs == UVC_VS_PROBE_CONTROL)
			memcpy(ctrl, &dev->probe, sizeof *ctrl);
		else
			memcpy(ctrl, &dev->commit, sizeof *ctrl);
		break;

	case UVC_GET_MIN:
	case UVC_GET_MAX:
	case UVC_GET_DEF:
		uvc_fill_streaming_control(dev, ctrl, req == UVC_GET_MAX ? -1 : 1,
					   req == UVC_GET_MAX ? -1 : 1, 0);
		break;

	case UVC_GET_RES:
		memset(ctrl, 0, sizeof *ctrl);
		break;

	case UVC_GET_LEN:
		resp->data[0] = 0x00;
		resp->data[1] = 0x22;
		resp->length = 2;
		break;

	case UVC_GET_INFO:
		resp->data[0] = 0x03;
		resp->length = 1;
		break;
	}
}

static void
uvc_events_process_class(struct uvc_device *dev,
			 const struct usb_ctrlrequest *ctrl,
			 struct uvc_request_data *resp)
{
	if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE)
		return;

	switch (ctrl->wIndex & 0xff) {
	case UVC_INTF_CONTROL:
		uvc_events_process_control(dev, ctrl->bRequest, ctrl->wValue >> 8, resp);
		break;

	case UVC_INTF_STREAMING:
		uvc_events_process_streaming(dev, ctrl->bRequest, ctrl->wValue >> 8, resp);
		break;

	default:
		break;
	}
}

static void
uvc_events_process_setup(struct uvc_device *dev,
			 const struct usb_ctrlrequest *ctrl,
			 struct uvc_request_data *resp)
{
	dev->control = 0;

	printf("bRequestType %02x bRequest %02x wValue %04x wIndex %04x "
		"wLength %04x\n", ctrl->bRequestType, ctrl->bRequest,
		ctrl->wValue, ctrl->wIndex, ctrl->wLength);

	switch (ctrl->bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		uvc_events_process_standard(dev, ctrl, resp);
		break;

	case USB_TYPE_CLASS:
		uvc_events_process_class(dev, ctrl, resp);
		break;

	default:
		break;
	}
}

static void
uvc_events_process_data(struct uvc_stream *stream,
			const struct uvc_request_data *data)
{
	struct uvc_device *dev = stream->uvc;
	const struct uvc_streaming_control *ctrl =
		(const struct uvc_streaming_control *)&data->data;
	struct uvc_streaming_control *target;

	switch (dev->control) {
	case UVC_VS_PROBE_CONTROL:
		printf("setting probe control, length = %d\n", data->length);
		target = &dev->probe;
		break;

	case UVC_VS_COMMIT_CONTROL:
		printf("setting commit control, length = %d\n", data->length);
		target = &dev->commit;
		break;

	default:
		printf("setting unknown control, length = %d\n", data->length);
		return;
	}

	uvc_fill_streaming_control(dev, target, ctrl->bFormatIndex,
				   ctrl->bFrameIndex, ctrl->dwFrameInterval);

	if (dev->control == UVC_VS_COMMIT_CONTROL) {
		const struct uvc_format_info *format;
		const struct uvc_frame_info *frame;

		format = &uvc_formats[target->bFormatIndex-1];
		frame = &format->frames[target->bFrameIndex-1];

		dev->fcc = format->fcc;
		dev->width = frame->width;
		dev->height = frame->height;

		uvc_video_set_format(stream);
	}
}

static void
uvc_events_process(void *d)
{
	struct uvc_stream *stream = d;
	struct uvc_device *dev = stream->uvc;
	struct v4l2_event v4l2_event;
	const struct uvc_event *uvc_event = (void *)&v4l2_event.u.data;
	struct uvc_request_data resp;
	int ret;

	ret = ioctl(dev->vdev->fd, VIDIOC_DQEVENT, &v4l2_event);
	if (ret < 0) {
		printf("VIDIOC_DQEVENT failed: %s (%d)\n", strerror(errno),
			errno);
		return;
	}

	memset(&resp, 0, sizeof resp);
	resp.length = -EL2HLT;

	switch (v4l2_event.type) {
	case UVC_EVENT_CONNECT:
	case UVC_EVENT_DISCONNECT:
		return;

	case UVC_EVENT_SETUP:
		uvc_events_process_setup(dev, &uvc_event->req, &resp);
		break;

	case UVC_EVENT_DATA:
		uvc_events_process_data(stream, &uvc_event->data);
		return;

	case UVC_EVENT_STREAMON:
		capture_video_stream(stream, 1);
		uvc_video_stream(stream, 1);
		return;

	case UVC_EVENT_STREAMOFF:
		uvc_video_stream(stream, 0);
		capture_video_stream(stream, 0);
		return;
	}

	ioctl(dev->vdev->fd, UVCIOC_SEND_RESPONSE, &resp);
	if (ret < 0) {
		printf("UVCIOC_S_EVENT failed: %s (%d)\n", strerror(errno),
			errno);
		return;
	}
}

static void
uvc_events_init(struct uvc_device *dev)
{
	struct v4l2_event_subscription sub;

	/* Default to the minimum values. */
	uvc_fill_streaming_control(dev, &dev->probe, 1, 1, 0);
	uvc_fill_streaming_control(dev, &dev->commit, 1, 1, 0);

	memset(&sub, 0, sizeof sub);
	sub.type = UVC_EVENT_SETUP;
	ioctl(dev->vdev->fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
	sub.type = UVC_EVENT_DATA;
	ioctl(dev->vdev->fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
	sub.type = UVC_EVENT_STREAMON;
	ioctl(dev->vdev->fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
	sub.type = UVC_EVENT_STREAMOFF;
	ioctl(dev->vdev->fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
}

/* ---------------------------------------------------------------------------
 * Stream handling
 */

static struct uvc_stream *uvc_stream_new(const char *uvc_device,
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

	stream->uvc = uvc_open(uvc_device);
	if (stream->uvc == NULL)
		goto error;

	return stream;

error:
	if (stream->cap)
		v4l2_close(stream->cap);

	free(stream);
	return NULL;
}

static void uvc_stream_delete(struct uvc_stream *stream)
{
	if (stream == NULL)
		return;

	v4l2_close(stream->cap);
	uvc_close(stream->uvc);

	free(stream);
}

static void uvc_stream_init_uvc(struct uvc_stream *stream)
{
	/*
	 * FIXME: The maximum size should be specified per format and frame.
	 */
	stream->uvc->maxsize = 0;

	uvc_events_init(stream->uvc);
	uvc_video_init(stream->uvc);
}

static void uvc_stream_set_event_handler(struct uvc_stream *stream,
					 struct events *events)
{
	stream->events = events;

	events_watch_fd(stream->events, stream->uvc->vdev->fd, EVENT_EXCEPTION,
			uvc_events_process, stream);
}

/* ---------------------------------------------------------------------------
 * main
 */

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s [options]\n", argv0);
	fprintf(stderr, "Available options are\n");
	fprintf(stderr, " -c device	V4L2 source device\n");
	fprintf(stderr, " -d device	Video device\n");
	fprintf(stderr, " -h		Print this help screen and exit\n");
	fprintf(stderr, " -i image	MJPEG image\n");
}

/* Necessary for and only used by signal handler. */
static struct events *sigint_events;

static void sigint_handler(int signal __attribute__((__unused__)))
{
	/* Stop the main loop when the user presses CTRL-C */
	events_stop(sigint_events);
}

int main(int argc, char *argv[])
{
	char *uvc_device = "/dev/video0";
	char *cap_device = "/dev/video1";
	struct uvc_stream *stream;
	struct events events;
	int ret = 0;
	int opt;

	while ((opt = getopt(argc, argv, "c:d:h")) != -1) {
		switch (opt) {
		case 'c':
			cap_device = optarg;
			break;

		case 'd':
			uvc_device = optarg;
			break;

		case 'h':
			usage(argv[0]);
			return 0;

		default:
			fprintf(stderr, "Invalid option '-%c'\n", opt);
			usage(argv[0]);
			return 1;
		}
	}

	/*
	 * Create the events handler. Register a signal handler for SIGINT,
	 * received when the user presses CTRL-C. This will allow the main loop
	 * to be interrupted, and resources to be freed cleanly.
	 */
	events_init(&events);

	sigint_events = &events;
	signal(SIGINT, sigint_handler);

	/* Create and initialise the stream. */
	stream = uvc_stream_new(uvc_device, cap_device);
	if (stream == NULL) {
		ret = 1;
		goto done;
	}

	uvc_stream_init_uvc(stream);
	uvc_stream_set_event_handler(stream, &events);

	/* Main capture loop */
	events_loop(&events);

done:
	/* Cleanup */
	uvc_stream_delete(stream);
	events_cleanup(&events);

	return ret;
}
