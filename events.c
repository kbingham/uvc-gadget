/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic Event Handling
 *
 * Copyright (C) 2018 Laurent Pinchart
 *
 * This file comes from the omap3-isp-live project
 * (git://git.ideasonboard.org/omap3-isp-live.git)
 *
 * Copyright (C) 2010-2011 Ideas on board SPRL
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#define _BSD_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/select.h>

#include "events.h"
#include "list.h"
#include "tools.h"

#define SELECT_TIMEOUT		2000		/* in milliseconds */

struct event_fd {
	struct list_entry list;

	int fd;
	enum event_type type;
	void (*callback)(void *priv);
	void *priv;
};

void events_watch_fd(struct events *events, int fd, enum event_type type,
		     void(*callback)(void *), void *priv)
{
	struct event_fd *event;

	event = malloc(sizeof *event);
	if (event == NULL)
		return;

	event->fd = fd;
	event->type = type;
	event->callback = callback;
	event->priv = priv;

	switch (event->type) {
	case EVENT_READ:
		FD_SET(fd, &events->rfds);
		break;
	case EVENT_WRITE:
		FD_SET(fd, &events->wfds);
		break;
	case EVENT_EXCEPTION:
		FD_SET(fd, &events->efds);
		break;
	}

	events->maxfd = max(events->maxfd, fd);

	list_append(&event->list, &events->events);
}

void events_unwatch_fd(struct events *events, int fd)
{
	struct event_fd *event = NULL;
	struct event_fd *entry;
	int maxfd = 0;

	list_for_each_entry(entry, &events->events, list) {
		if (entry->fd == fd)
			event = entry;
		else
			maxfd = max(maxfd, entry->fd);
	}

	if (event == NULL)
		return;

	switch (event->type) {
	case EVENT_READ:
		FD_CLR(fd, &events->rfds);
		break;
	case EVENT_WRITE:
		FD_CLR(fd, &events->wfds);
		break;
	case EVENT_EXCEPTION:
		FD_CLR(fd, &events->efds);
		break;
	}

	events->maxfd = maxfd;

	list_remove(&event->list);
	free(event);
}

static void events_dispatch(struct events *events, const fd_set *rfds,
			    const fd_set *wfds, const fd_set *efds)
{
	struct event_fd *event;
	struct event_fd *next;

	list_for_each_entry_safe(event, next, &events->events, list) {
		if (event->type == EVENT_READ &&
		    FD_ISSET(event->fd, rfds))
			event->callback(event->priv);
		else if (event->type == EVENT_WRITE &&
			 FD_ISSET(event->fd, wfds))
			event->callback(event->priv);
		else if (event->type == EVENT_EXCEPTION &&
			 FD_ISSET(event->fd, efds))
			event->callback(event->priv);

		/* If the callback stopped events processing, we're done. */
		if (events->done)
			break;
	}
}

bool events_loop(struct events *events)
{
	events->done = false;

	while (!events->done) {
		struct timeval timeout;
		fd_set rfds;
		fd_set wfds;
		fd_set efds;
		int ret;

		timeout.tv_sec = SELECT_TIMEOUT / 1000;
		timeout.tv_usec = (SELECT_TIMEOUT % 1000) * 1000;
		rfds = events->rfds;
		wfds = events->wfds;
		efds = events->efds;

		ret = select(events->maxfd + 1, &rfds, &wfds, &efds, &timeout);
		if (ret < 0) {
			/* EINTR means that a signal has been received, continue
			 * to the next iteration in that case.
			 */
			if (errno == EINTR)
				continue;

			printf("error: select failed with %d\n", errno);
			break;
		}
		if (ret == 0) {
			/* select() should never time out as the ISP is supposed
			 * to capture images continuously. A timeout is thus
			 * considered as a fatal error.
			 */
			printf("error: select timeout\n");
			break;
		}

		events_dispatch(events, &rfds, &wfds, &efds);
	}

	return !events->done;
}

void events_stop(struct events *events)
{
	events->done = true;
}

void events_init(struct events *events)
{
	memset(events, 0, sizeof *events);

	FD_ZERO(&events->rfds);
	FD_ZERO(&events->wfds);
	FD_ZERO(&events->efds);
	events->maxfd = 0;
	list_init(&events->events);
}
