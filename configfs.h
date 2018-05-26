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

/*
 * struct uvc_function_config_endpoint - Endpoint parameters
 * @bInterval: Transfer interval (interrupt and isochronous only)
 * @bMaxBurst: Transfer burst size (super-speed only)
 * @wMaxPacketSize: Maximum packet size (including the multiplier)
 */
struct uvc_function_config_endpoint {
	unsigned int bInterval;
	unsigned int bMaxBurst;
	unsigned int wMaxPacketSize;
};

/*
 * struct uvc_function_config_interface - Interface parameters
 * @bInterfaceNumber: Interface number
 */
struct uvc_function_config_interface {
	unsigned int bInterfaceNumber;
};

/*
 * struct uvc_function_config_control - Control interface parameters
 * @intf: Generic interface parameters
 */
struct uvc_function_config_control {
	struct uvc_function_config_interface intf;
};

/*
 * struct uvc_function_config_streaming - Streaming interface parameters
 * @intf: Generic interface parameters
 * @ep: Endpoint parameters
 */
struct uvc_function_config_streaming {
	struct uvc_function_config_interface intf;
	struct uvc_function_config_endpoint ep;
};

/*
 * struct uvc_function_config - UVC function configuration parameters
 * @video: Full path to the video device node
 * @udc: UDC name
 * @control: Control interface configuration
 * @streaming: Streaming interface configuration
 */
struct uvc_function_config {
	char *video;
	char *udc;

	struct uvc_function_config_control control;
	struct uvc_function_config_streaming streaming;
};

struct uvc_function_config *configfs_parse_uvc_function(const char *function);
void configfs_free_uvc_function(struct uvc_function_config *fc);

#endif
