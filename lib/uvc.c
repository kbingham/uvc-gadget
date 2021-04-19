/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * UVC protocol handling
 *
 * Copyright (C) 2010-2018 Laurent Pinchart
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <errno.h>
#include <limits.h>
#include <linux/usb/ch9.h>
#include <linux/usb/g_uvc.h>
#include <linux/usb/video.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "configfs.h"
#include "events.h"
#include "stream.h"
#include "tools.h"
#include "uvc.h"
#include "v4l2.h"
#include "log.h"

#define PU_BRIGHTNESS_MIN_VAL		0
#define PU_BRIGHTNESS_MAX_VAL		255
#define PU_BRIGHTNESS_STEP_SIZE		1
#define PU_BRIGHTNESS_DEFAULT_VAL	127

struct uvc_device
{
	struct v4l2_device *vdev;

	struct uvc_stream *stream;
	struct uvc_function_config *fc;

	struct uvc_streaming_control probe;
	struct uvc_streaming_control commit;

	int control;

	unsigned int fcc;
	unsigned int width;
	unsigned int height;
	unsigned int maxsize;

	int brightness;
};

struct uvc_device *uvc_open(const char *devname, struct uvc_stream *stream)
{
	struct uvc_device *dev;

	dev = malloc(sizeof *dev);
	if (dev == NULL)
		return NULL;

	memset(dev, 0, sizeof *dev);
	dev->stream = stream;

	dev->vdev = v4l2_open(devname);
	if (dev->vdev == NULL) {
		free(dev);
		return NULL;
	}

	return dev;
}

void uvc_close(struct uvc_device *dev)
{
	v4l2_close(dev->vdev);
	dev->vdev = NULL;

	free(dev);
}

/* ---------------------------------------------------------------------------
 * Request processing
 */

static void
uvc_fill_streaming_control(struct uvc_device *dev,
			   struct uvc_streaming_control *ctrl,
			   int iformat, int iframe, unsigned int ival)
{
	const struct uvc_function_config_format *format;
	const struct uvc_function_config_frame *frame;
	unsigned int i;

	/*
	 * Restrict the iformat, iframe and ival to valid values. Negative
	 * values for iformat or iframe will result in the maximum valid value
	 * being selected.
	 */
        iformat = clamp((unsigned int)iformat, 1U,
                        dev->fc->streaming.num_formats);
	format = &dev->fc->streaming.formats[iformat-1];

	iframe = clamp((unsigned int)iframe, 1U, format->num_frames);
	frame = &format->frames[iframe-1];

	for (i = 0; i < frame->num_intervals; ++i) {
		if (ival <= frame->intervals[i]) {
			ival = frame->intervals[i];
			break;
		}
	}

	if (i == frame->num_intervals)
		ival = frame->intervals[frame->num_intervals-1];

	memset(ctrl, 0, sizeof *ctrl);

	ctrl->bmHint = 1;
	ctrl->bFormatIndex = iformat;
	ctrl->bFrameIndex = iframe ;
	ctrl->dwFrameInterval = ival;

	switch (format->fcc) {
	case V4L2_PIX_FMT_YUYV:
		ctrl->dwMaxVideoFrameSize = frame->width * frame->height * 2;
		break;
	case V4L2_PIX_FMT_MJPEG:
		ctrl->dwMaxVideoFrameSize = dev->maxsize;
		break;
	}

	ctrl->dwMaxPayloadTransferSize = dev->fc->streaming.ep.wMaxPacketSize;
	ctrl->bmFramingInfo = 3;
	ctrl->bPreferedVersion = 1;
	ctrl->bMaxVersion = 1;
}

static void
uvc_events_process_standard(struct uvc_device *dev,
			    const struct usb_ctrlrequest *ctrl,
			    struct uvc_request_data *resp)
{
	log_debug("STUB! standard request");
	(void)dev;
	(void)ctrl;
	(void)resp;
}

// Processing unit brightness control
static void
uvc_events_pu_brightness_control(struct uvc_device *dev, uint8_t req,
				struct uvc_request_data *resp)
{
	switch (req) {
		case UVC_GET_INFO:
			resp->data[0] = 0x03;
			resp->length = 1;
			break;
		case UVC_SET_CUR:
			resp->data[0] = 0x0;
			resp->length = 1;
			break;
		case UVC_GET_DEF:
			resp->data[0] = PU_BRIGHTNESS_DEFAULT_VAL;
			resp->length = 2;
			break;
		case UVC_GET_RES:
			resp->data[0] = PU_BRIGHTNESS_STEP_SIZE;
			resp->length = 2;
			break;
		case UVC_GET_MIN:
			resp->data[0] = PU_BRIGHTNESS_MIN_VAL;
			resp->length = 2;
			break;
		case UVC_GET_MAX:
			resp->data[0] = PU_BRIGHTNESS_MAX_VAL;
			resp->length = 2;
			break;
		case UVC_GET_CUR:
			resp->length = 2;
			memcpy(&resp->data[0], &dev->brightness,
					resp->length);
	}
}

// Default values for an unimplemented control, this function
// will only be hit if the f_uvc driver gets updated to expose
// more than just the controls currently handled in
// uvc_events_setup_pu_control().
static void
uvc_events_pu_unimplemented_control(struct uvc_device *dev, uint8_t req,
				struct uvc_request_data *resp)
{
	/*
	* We don't support this control, so STALL the
	* default control endpoint.
	*/
	resp->length = -EL2HLT;
	// switch (req) {
	// 	case UVC_GET_INFO:
	// 		// Report that we support GET, SET
	// 		// and that this control is disabled
	// 		// ("under device control");
	// 		resp->data[0] = 0x07;
	// 		resp->length = 1;
	// 		break;
	// 	case UVC_SET_CUR:
	// 		log_debug("Unimplemented control SET_CUR");
	// 		break;
	// 	case UVC_GET_DEF:
	// 		log_debug("Unimplmented control GET_DEF");
	// 		resp->data[0] = 127;
	// 		resp->length = 2;
	// 		break;
	// 	case UVC_GET_
	// }
}

// Handle processing unit controls
static void
uvc_events_setup_pu_control(struct uvc_device *dev, uint8_t req, uint8_t cs,
			   struct uvc_request_data *resp)
{
	log_debug("control request (req 0x%02x cs 0x%02x)", req, cs);

	// The f_uvc driver is hard coded to only support a brightness
	// control, but that should change in the future.
	switch (cs) {
		case UVC_PU_BRIGHTNESS_CONTROL:
			uvc_events_pu_brightness_control(dev, req, resp);
			break;
		default:
			log_warn("Unimplemented control, making best guess");
			uvc_events_pu_unimplemented_control(dev, req, resp);
			break;
	}

	// switch (req) {
	// 	case UVC_GET_INFO:
	// 		resp->data[0] = 0x03;
	// 		resp->length = 1;
	// 		break;
	// 	case UVC_SET_CUR:
	// 		log_debug("Control SET_CUR");
	// 		break;
	// 	case UVC_GET_DEF:
	// 		log_debug("Control GET_DEF");
	// 		resp->data[0] = PU_BRIGHTNESS_DEFAULT_VAL;
	// 		resp->length = 2;
	// 		break;
	// }
}

// Handle video streaming controls
static void
uvc_events_setup_vs_control(struct uvc_device *dev, uint8_t req, uint8_t cs,
			     struct uvc_request_data *resp)
{
	struct uvc_streaming_control *ctrl;

	log_debug("streaming request (req 0x%02x cs 0x%02x)", req, cs);

	if (cs != UVC_VS_PROBE_CONTROL && cs != UVC_VS_COMMIT_CONTROL)
		return;

	ctrl = (struct uvc_streaming_control *)&resp->data;
	resp->length = sizeof *ctrl;

	switch (req) {
	case UVC_GET_CUR:
		if (cs == UVC_VS_PROBE_CONTROL)
			memcpy(ctrl, &dev->probe, sizeof *ctrl);
		else
			memcpy(ctrl, &dev->commit, sizeof *ctrl);
		break;

	case UVC_GET_MIN:
	case UVC_GET_MAX:
	case UVC_GET_DEF:
		if (req == UVC_GET_MAX)
			uvc_fill_streaming_control(dev, ctrl, -1, -1, UINT_MAX);
		else
			uvc_fill_streaming_control(dev, ctrl, 1, 1, 0);
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
	// Id of the entity (unit) we're handling
	unsigned int entity_id = ctrl->wIndex & 0xff;
	// Control selector (which control to handle)
	uint8_t cs =ctrl->wValue >> 8;

	if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE)
		return;

	// This is the Processing Unit control
	if (entity_id == dev->fc->control.intf.bInterfaceNumber)
		uvc_events_setup_pu_control(dev, ctrl->bRequest, cs, resp);
	// This is the VideoStreaming control
	else if (entity_id == dev->fc->streaming.intf.bInterfaceNumber)
		uvc_events_setup_vs_control(dev, ctrl->bRequest, cs, resp);
}

static void
uvc_events_process_setup(struct uvc_device *dev,
			 const struct usb_ctrlrequest *ctrl,
			 struct uvc_request_data *resp)
{
	dump_usb_ctrlrequest(ctrl);

	switch (ctrl->bRequestType & USB_TYPE_MASK) {
	// Do we EVER hit this case????
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
uvc_events_process_data_pu_control(struct uvc_device *dev, uint8_t cs,
			const struct uvc_request_data *data,
			struct uvc_request_data *resp)
{
	switch (cs)
	{
		case UVC_PU_BRIGHTNESS_CONTROL:
			memcpy(&dev->brightness, data->data, data->length);
			log_debug("Set brightness value: %d", dev->brightness);
			break;
		
		default:
			break;
	}
}

static void
uvc_events_process_data_vs_control(struct uvc_device *dev, uint8_t cs,
			const struct uvc_request_data *data,
			struct uvc_request_data *resp)
{
	const struct uvc_streaming_control *ctrl =
		(const struct uvc_streaming_control *)&data->data;
	struct uvc_streaming_control *target;

	switch (cs) {
	case UVC_VS_PROBE_CONTROL:
		log_debug("setting probe control, length = %d", data->length);
		target = &dev->probe;
		break;

	case UVC_VS_COMMIT_CONTROL:
		log_debug("setting commit control, length = %d", data->length);
		target = &dev->commit;
		break;

	default:
		log_error("unknown control, length = %d", data->length);
		target = &dev->probe;
		//return;
	}

	uvc_fill_streaming_control(dev, target, ctrl->bFormatIndex,
				   ctrl->bFrameIndex, ctrl->dwFrameInterval);

	if (cs == UVC_VS_COMMIT_CONTROL) {
		const struct uvc_function_config_format *format;
		const struct uvc_function_config_frame *frame;
		struct v4l2_pix_format pixfmt;
		unsigned int fps;

		format = &dev->fc->streaming.formats[target->bFormatIndex-1];
		frame = &format->frames[target->bFrameIndex-1];

		dev->fcc = format->fcc;
		dev->width = frame->width;
		dev->height = frame->height;

		memset(&pixfmt, 0, sizeof pixfmt);
		pixfmt.width = frame->width;
		pixfmt.height = frame->height;
		pixfmt.pixelformat = format->fcc;
		pixfmt.field = V4L2_FIELD_NONE;
		if (format->fcc == V4L2_PIX_FMT_MJPEG)
			pixfmt.sizeimage = dev->maxsize * 1.5;

		uvc_stream_set_format(dev->stream, &pixfmt);

		/* fps is guaranteed to be non-zero and thus valid. */
		fps = 1.0 / (target->dwFrameInterval / 10000000.0);
		uvc_stream_set_frame_rate(dev->stream, fps);
	}
}

static void
uvc_events_process_data(struct uvc_device *dev,
			const struct uvc_request_data *data,
			struct uvc_request_data *resp)
{
	
	const struct usb_ctrlrequest *ctrl = &data->setup;

	dump_usb_ctrlrequest(ctrl);

	unsigned int entity_id = ctrl->wIndex & 0xff;
	uint8_t cs = ctrl->wValue >> 8;

	// This is the Processing Unit control
	if (entity_id == dev->fc->control.intf.bInterfaceNumber)
		uvc_events_process_data_pu_control(dev, cs, data, resp);
	// This is the VideoStreaming control
	else if (entity_id == dev->fc->streaming.intf.bInterfaceNumber)
		uvc_events_process_data_vs_control(dev, cs, data, resp);
	
	resp->length = 0;
}

static void uvc_events_process(void *d)
{
	struct uvc_device *dev = d;
	struct v4l2_event v4l2_event;
	const struct uvc_event *uvc_event = (void *)&v4l2_event.u.data;
	struct uvc_request_data resp;
	int ret;

	ret = ioctl(dev->vdev->fd, VIDIOC_DQEVENT, &v4l2_event);
	if (ret < 0) {
		log_error("VIDIOC_DQEVENT failed: %s (%d)", strerror(errno),
			errno);
		return;
	}
	
	if (uvc_event->data.length == 26 && uvc_event->data.setup.wLength == 0) {
		log_error("Detected an empty (null) packet");
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
		uvc_events_process_data(dev, &uvc_event->data, &resp);
		break;

	case UVC_EVENT_STREAMON:
		log_debug("Enabling UVC stream");
		uvc_stream_enable(dev->stream, 1);
		return;

	case UVC_EVENT_STREAMOFF:
		log_debug("Disabling UVC stream");
		uvc_stream_enable(dev->stream, 0);
		return;
	}

	ret = ioctl(dev->vdev->fd, UVCIOC_SEND_RESPONSE, &resp);
	if (ret < 0) {
		printf("UVCIOC_SEND_RESPONSE failed: %s (%d)\n",
		       strerror(errno), errno);
		return;
	}
}

/* ---------------------------------------------------------------------------
 * Initialization and setup
 */

void uvc_events_init(struct uvc_device *dev, struct events *events)
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

	events_watch_fd(events, dev->vdev->fd, EVENT_EXCEPTION,
			uvc_events_process, dev);
}

void uvc_set_config(struct uvc_device *dev, struct uvc_function_config *fc)
{
	/* FIXME: The maximum size should be specified per format and frame. */
	dev->maxsize = 0;
	dev->fc = fc;
}

int uvc_set_format(struct uvc_device *dev, struct v4l2_pix_format *format)
{
	return v4l2_set_format(dev->vdev, format);
}

struct v4l2_device *uvc_v4l2_device(struct uvc_device *dev)
{
	/*
	 * TODO: The V4L2 device shouldn't be exposed. We should replace this
	 * with an abstract video sink class when one will be avaiilable.
	 */
	return dev->vdev;
}
