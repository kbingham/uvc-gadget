/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * UVC protocol handling
 *
 * Copyright (C) 2010-2018 Laurent Pinchart
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef __UVC_H__
#define __UVC_H__

#include "log.h"

struct events;
struct v4l2_device;
struct uvc_device;
struct uvc_function_config;
struct uvc_stream;

struct uvc_device *uvc_open(const char *devname, struct uvc_stream *stream);
void uvc_close(struct uvc_device *dev);
void uvc_events_init(struct uvc_device *dev, struct events *events);
void uvc_set_config(struct uvc_device *dev, struct uvc_function_config *fc);
int uvc_set_format(struct uvc_device *dev, struct v4l2_pix_format *format);
struct v4l2_device *uvc_v4l2_device(struct uvc_device *dev);

#define dump_usb_ctrlrequest(usb_ctrl) \
	log_trace("usb_ctrlrequest dump: \n" \
				"usb_ctrl->bRequestType  = 0x%02x\n" \
				"usb_ctrl->bRequest      = 0x%02x\n" \
				"usb_ctrl->wValue        = 0x%04x\n" \
				"usb_ctrl->wIndex        = 0x%04x\n" \
				"usb_ctrl->wLength       = 0x%04x", \
		usb_ctrl->bRequestType, usb_ctrl->bRequest, \
		usb_ctrl->wValue, usb_ctrl->wIndex, usb_ctrl->wLength);

#endif /* __UVC_H__ */
