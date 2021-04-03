/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * UVC gadget test application
 *
 * Copyright (C) 2010-2018 Laurent Pinchart
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "configfs.h"
#include "events.h"
#include "stream.h"
#include "v4l2-source.h"
#include "test-source.h"
#include "jpg-source.h"
#include "slideshow-source.h"
#include "log.h"

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s [options] <uvc device>\n", argv0);
	fprintf(stderr, "Available options are\n");
	fprintf(stderr, " -c device	V4L2 source device\n");
	fprintf(stderr, " -i image	MJPEG image\n");
	fprintf(stderr, " -s directory	directory of slideshow images\n");
	fprintf(stderr, " -k keypad	keypad device (default: /dev/input/event1)\n");
	fprintf(stderr, " -v		use verbose logging\n");
	fprintf(stderr, " -h		Print this help screen and exit\n");
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
	fprintf(stderr, "    %s uvc.1\n", argv0);
	fprintf(stderr, "    %s g1/functions/uvc.1\n", argv0);
	fprintf(stderr, "\n");
	fprintf(stderr, "    %s musb-hdrc.0.auto\n", argv0);
}

/* Necessary for and only used by signal handler. */
static struct events *sigint_events;

static void sigint_handler(int signal)
{
	/* Stop the main loop when the user presses CTRL-C */
	events_stop(sigint_events);
}

int main(int argc, char *argv[])
{
	char *function = NULL;
	char *cap_device = NULL;
	char *img_path = NULL;
	char *slideshow_dir = NULL;
	char *keypad = NULL;

	struct uvc_function_config *fc;
	struct uvc_stream *stream = NULL;
	struct video_source *src = NULL;
	struct events events;
	int ret = 0;
	int opt;

	log_set_level(LOG_INFO);

	while ((opt = getopt(argc, argv, "c:i:s:k:vh")) != -1) {
		switch (opt) {
		case 'c':
			cap_device = optarg;
			break;

		case 'i':
			img_path = optarg;
			break;

		case 's':
			slideshow_dir = optarg;
			break;

		case 'k':
			keypad = optarg;
			break;
		case 'v':
			log_set_level(LOG_TRACE);
			break;

		case 'h':
			usage(argv[0]);
			return 0;

		default:
			log_error("Invalid option '-%c'", opt);
			usage(argv[0]);
			return 1;
		}
	}

	if (argv[optind] != NULL)
		function = argv[optind];

	fc = configfs_parse_uvc_function(function);
	if (!fc) {
		log_error("Failed to identify function configuration");
		return 1;
	}

	if (cap_device != NULL && img_path != NULL) {
		log_error("Both capture device and still image specified");
		log_error("Please specify only one");
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

	/* Create and initialize a video source. */
	if (cap_device)
		src = v4l2_video_source_create(cap_device);
	else if (img_path)
		src = jpg_video_source_create(img_path);
	else if (slideshow_dir)
		src = slideshow_video_source_create(slideshow_dir, keypad, &events);
	else
		src = test_video_source_create();
	if (src == NULL) {
		ret = 1;
		goto done;
	}

	if (cap_device)
		v4l2_video_source_init(src, &events);

	/* Create and initialise the stream. */
	stream = uvc_stream_new(fc->video);
	if (stream == NULL) {
		ret = 1;
		goto done;
	}

	uvc_stream_set_event_handler(stream, &events);
	uvc_stream_set_video_source(stream, src);
	uvc_stream_init_uvc(stream, fc);

	/* Main capture loop */
	events_loop(&events);

done:
	/* Cleanup */
	uvc_stream_delete(stream);
	video_source_destroy(src);
	events_cleanup(&events);
	configfs_free_uvc_function(fc);

	return ret;
}
