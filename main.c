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

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "configfs.h"
#include "events.h"
#include "stream.h"

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s [options] <uvc device>\n", argv0);
	fprintf(stderr, "Available options are\n");
	fprintf(stderr, " -c device	V4L2 source device\n");
	fprintf(stderr, " -h		Print this help screen and exit\n");
	fprintf(stderr, " -i image	MJPEG image\n");
	fprintf(stderr, "\n");
	fprintf(stderr, " <uvc device>	UVC device instance specifier\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "  For ConfigFS devices the <uvc device> parameter can take the form of a shortened\n");
	fprintf(stderr, "  function specifier such as: 'uvc.0', or if multiple gadgets are configured, the\n");
	fprintf(stderr, "  gadget name should be included to prevent ambiguity: 'g1/functions/uvc.0'.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  For legacy g_webcam UVC instances, this parameter will identify the UDC that the\n");
	fprintf(stderr, "  UVC function is bound to.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  The parameter is optional, and if not provided the first UVC function on the first\n");
	fprintf(stderr, "  gadget identified will be used.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Example usage:\n");
	fprintf(stderr, "    uvc-gadget uvc.1\n");
	fprintf(stderr, "    uvc-gadget g1/functions/uvc.1\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    uvc-gadget musb-hdrc.0.auto\n");
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
	char *function = NULL;
	char *cap_device = "/dev/video1";
	struct uvc_function_config *fc;
	struct uvc_stream *stream;
	struct events events;
	int ret = 0;
	int opt;

	while ((opt = getopt(argc, argv, "c:h")) != -1) {
		switch (opt) {
		case 'c':
			cap_device = optarg;
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

	if (argv[optind] != NULL)
		function = argv[optind];

	fc = configfs_parse_uvc_function(function);
	if (!fc) {
		printf("Failed to identify function configuration\n");
		return 1;
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
	stream = uvc_stream_new(fc->video, cap_device);
	if (stream == NULL) {
		ret = 1;
		goto done;
	}

	uvc_stream_set_event_handler(stream, &events);
	uvc_stream_init_uvc(stream, fc);

	/* Main capture loop */
	events_loop(&events);

done:
	/* Cleanup */
	uvc_stream_delete(stream);
	events_cleanup(&events);
	configfs_free_uvc_function(fc);

	return ret;
}
