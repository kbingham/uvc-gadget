/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Slideshow video source
 *
 * Copyright (C) 2018 Paul Elder
 *
 * Contact: Paul Elder <paul.elder@ideasonboard.com>
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <linux/videodev2.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>

#include "events.h"
#include "tools.h"
#include "slideshow-source.h"
#include "video-buffers.h"
#include "log.h"

struct slide {
	unsigned int index;
	unsigned int imgsize;
	void *imgdata;
};

struct slideshow_source {
	struct video_source src;

	unsigned int cur_slide;
	unsigned int nslides;
	struct slide *slides;

	int keypad_fd;
};

#define to_slideshow_source(s) container_of(s, struct slideshow_source, src)

static void slideshow_read_keypad(void *d)
{
	struct slideshow_source *src = d;
	struct input_event ev;

	if(read(src->keypad_fd, &ev, sizeof ev) <= 0)
		return;

	if (ev.type != EV_KEY)
		return;

	if (ev.code == KEY_ENTER && ev.value == 1 &&
	    src->cur_slide + 1 < src->nslides)
		src->cur_slide++;

	if (ev.code == KEY_BACKSPACE && ev.value == 1 &&
	    src->cur_slide >= 1)
		src->cur_slide--;
}

static void slideshow_source_destroy(struct video_source *s)
{
	struct slideshow_source *src = to_slideshow_source(s);
	unsigned int i;

	for (i = 0; i < src->nslides; i++)
		free(src->slides[i].imgdata);
	free(src->slides);
	close(src->keypad_fd);
	free(src);
}

static int slideshow_source_set_format(struct video_source *s,
				  struct v4l2_pix_format *fmt)
{
	if (fmt->pixelformat != v4l2_fourcc('M', 'J', 'P', 'G')) {
		log_error("invalid pixel format");
		return -EINVAL;
	}

	return 0;
}

static int slideshow_source_set_frame_rate(struct video_source *s, unsigned int fps)
{
	return 0;
}

static int slideshow_source_free_buffers(struct video_source *s)
{
	return 0;
}

static int slideshow_source_stream_on(struct video_source *s)
{
	return 0;
}

static int slideshow_source_stream_off(struct video_source *s)
{
	return 0;
}

static void slideshow_source_fill_buffer(struct video_source *s,
					 struct video_buffer *buf)
{
	struct slideshow_source *src = to_slideshow_source(s);

	memcpy(buf->mem, src->slides[src->cur_slide].imgdata,
		    src->slides[src->cur_slide].imgsize);
	buf->bytesused = src->slides[src->cur_slide].imgsize;
}

static const struct video_source_ops slideshow_source_ops = {
	.destroy = slideshow_source_destroy,
	.set_format = slideshow_source_set_format,
	.set_frame_rate = slideshow_source_set_frame_rate,
	.free_buffers = slideshow_source_free_buffers,
	.stream_on = slideshow_source_stream_on,
	.stream_off = slideshow_source_stream_off,
	.queue_buffer = NULL,
	.fill_buffer = slideshow_source_fill_buffer,
};

struct video_source *slideshow_video_source_create(const char *img_dir,
						   const char *keypad,
						   struct events *events)
{
	struct slideshow_source *src;
	int fd = -1;
	int i;
	int slide_index = 0;
	DIR* dir;
	struct dirent *file;

	printf("CREATING SLIDESHOW VIDEO SOURCE\n");

	if (img_dir == NULL)
		return NULL;

	src = malloc(sizeof *src);
	if (!src)
		return NULL;

	memset(src, 0, sizeof *src);
	src->src.ops = &slideshow_source_ops;

	dir = opendir(img_dir);
	if (dir == NULL) {
		printf("Unable to open slides directory '%s'\n", img_dir);
		goto err1;
	}

	if (chdir(img_dir)) {
		printf("Unable to cd to slides directory '%s'\n", img_dir);
		goto err2;
	}

	while ((file = readdir(dir))) {
		if (!strcmp(file->d_name, ".") ||
		    !strcmp(file->d_name, ".."))
			continue;

		fd = open(file->d_name, O_RDONLY);
		if (fd == -1) {
			printf("Unable to open MJPEG image '%s'\n", file->d_name);
			goto err2;
		}

		src->slides = realloc(src->slides,
				sizeof (struct slide) * (slide_index + 1));
		if (src->slides == NULL) {
			printf("Unable to allocate memory for slides\n");
			goto err3;
		}

		src->slides[slide_index].index = slide_index;
		src->slides[slide_index].imgsize = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);
		src->slides[slide_index].imgdata =
			malloc(src->slides[slide_index].imgsize);
		if (src->slides[slide_index].imgdata == NULL) {
			printf("Unable to allocate memory for MJPEG image\n");
			goto err4;
		}

		read(fd, src->slides[slide_index].imgdata,
			 src->slides[slide_index].imgsize);
		close(fd);
		slide_index++;
	}

	closedir(dir);

	src->nslides = slide_index;

	src->keypad_fd = open(keypad ? keypad : "/dev/input/event1",
			      O_RDONLY | O_NONBLOCK);
	events_watch_fd(events, src->keypad_fd, EVENT_READ,
			slideshow_read_keypad, src);

	return &src->src;

err4:
	for (i = 0; i <= slide_index; i++)
		free(src->slides[i].imgdata);
	free(src->slides);
err3:
	close(fd);
err2:
	closedir(dir);
err1:
	free(src);
	return NULL;
}

void slideshow_video_source_init(struct video_source *s, struct events *events)
{
	struct slideshow_source *src = to_slideshow_source(s);

	src->src.events = events;
}
