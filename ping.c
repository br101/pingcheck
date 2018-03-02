/* pingcheck - Check connectivity of interfaces in OpenWRT
 *
 * Copyright (C) 2015 Bruno Randolf <br1@einfach.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include "main.h"

static void ping_uloop_fd_close(struct uloop_fd *ufd)
{
	if (ufd != NULL && ufd->fd > 0) {
		uloop_fd_delete(ufd);
		close(ufd->fd);
		ufd->fd = 0;
	}
}

/* uloop callback when received something on a ping socket */
static void ping_fd_handler(struct uloop_fd *fd,
			    __attribute__((unused)) unsigned int events)
{
	struct ping_intf* pi = container_of(fd, struct ping_intf, ufd);

	if (pi->conf_proto == ICMP) {
		if (!icmp_echo_receive(fd->fd))
			return;
	} else if (pi->conf_proto == TCP) {
		/* with TCP, the handler is called when connect() succeds or fails.
		*
		* if the connect takes longer than the ping interval, it is timed
		* out and assumed failed before we open the next regular connection,
		* and this handler is not called. but if the interval is large and
		* in other cases, this handler can be called for failed connections,
		* and to be sure we need to check if connect was successful or not.
		*
		* after that we just close the socket, as we don't need to send or
		* receive any data */
		bool succ = tcp_check_connect(fd->fd);
		ping_uloop_fd_close(fd);
		//printf("TCP connected %d\n", succ);
		if (!succ)
			return;
	}

	//ULOG_DBG("Received pong on '%s'", pi->name);
	pi->cnt_succ++;

	/* calculate round trip time */
	struct timespec time_recv;
	clock_gettime(CLOCK_MONOTONIC, &time_recv);
	pi->last_rtt = timespec_diff_ms(pi->time_sent, time_recv);
	if (pi->last_rtt > pi->max_rtt)
		pi->max_rtt = pi->last_rtt;

	/* online just confirmed: move timeout for offline to later
	 * and give the next reply an extra window of two times the last RTT */
	uloop_timeout_set(&pi->timeout_offline, pi->conf_timeout * 1000 + pi->last_rtt * 2);

	state_change(ONLINE, pi);
}

/* uloop timeout callback when we did not receive a ping reply for a certain time */
static void uto_offline_cb(struct uloop_timeout *t)
{
	struct ping_intf* pi = container_of(t, struct ping_intf, timeout_offline);
	state_change(OFFLINE, pi);
}

/* uloop timeout callback when it's time to send a ping */
static void uto_ping_send_cb(struct uloop_timeout *t)
{
	struct ping_intf* pi = container_of(t, struct ping_intf, timeout_send);
	ping_send(pi);
	/* re-schedule next sending */
	uloop_timeout_set(t, pi->conf_interval * 1000);
}

bool ping_init(struct ping_intf* pi)
{
	int ret;

	if (pi->ufd.fd != 0) {
		ULOG_ERR("Ping on '%s' already init\n", pi->name);
		return true;
	}

	ret = ubus_interface_get_status(pi->name, pi->device, MAX_IFNAME_LEN);
	if (ret < 0) {
		ULOG_INFO("Interface '%s' not found or error\n", pi->name);
		pi->state = UNKNOWN;
		return false;
	} else if (ret == 0) {
		ULOG_INFO("Interface '%s' not up\n", pi->name);
		pi->state = DOWN;
		return false;
	} else if (ret == 1) {
		ULOG_INFO("Interface '%s' no route\n", pi->name);
		pi->state = NO_ROUTE;
		return false;
	} else if (ret == 2) {
		pi->state = UP;
	}

	ULOG_INFO("Init %s ping on '%s'\n",
		 pi->conf_proto == TCP ? "TCP" : "ICMP", pi->name);

	/* init ICMP socket. for TCP we open a new socket every time */
	if (pi->conf_proto == ICMP) {
		ret = icmp_init(pi->device);
		if (ret < 0)
			return false;

		/* add socket handler to uloop */
		pi->ufd.fd = ret;
		pi->ufd.cb = ping_fd_handler;
		ret = uloop_fd_add(&pi->ufd, ULOOP_READ);
		if (ret < 0) {
			ULOG_ERR("Could not add uloop fd %d for '%s'\n",
				pi->ufd.fd, pi->name);
			return false;
		}
	}

	/* regular sending of ping (start first in 1 sec) */
	pi->timeout_send.cb = uto_ping_send_cb;
	ret = uloop_timeout_set(&pi->timeout_send, 1000);
	if (ret < 0) {
		ULOG_ERR("Could not add uloop send timeout for '%s'\n",
			 pi->name);
		return false;
	}

	/* timeout for offline state, if no reply has been received
	 *
	 * add 900ms to the timeout to give the last reply a chance to arrive
	 * before the timeout triggers, in case the timout is a multiple of
	 * interval. this will later be adjusted to the last RTT
	 */
	pi->timeout_offline.cb = uto_offline_cb;
	ret = uloop_timeout_set(&pi->timeout_offline, pi->conf_timeout * 1000 + 900);
	if (ret < 0) {
		ULOG_ERR("Could not add uloop offline timeout for '%s'\n",
			 pi->name);
		return false;
	}

	/* reset counters */
	pi->cnt_sent = 0;
	pi->cnt_succ = 0;
	pi->last_rtt = 0;
	pi->max_rtt = 0;

	return true;
}

static void ping_resolve(struct ping_intf* pi)
{
	struct addrinfo hints;
	struct addrinfo* addr;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = pi->conf_proto == ICMP? SOCK_DGRAM : SOCK_STREAM;

	int r = getaddrinfo(pi->conf_hostname, NULL, &hints, &addr);
	if (r < 0 || addr == NULL) {
		ULOG_ERR("Failed to resolve\n");
		return;
	}

	/* use only first address */
	struct sockaddr_in* sa = (struct sockaddr_in*)addr->ai_addr;
	printf("Resolved %s to %s\n", pi->conf_hostname,
	       inet_ntoa((struct in_addr)sa->sin_addr));
	pi->conf_host = sa->sin_addr.s_addr;

	freeaddrinfo(addr);
}

/* for "ping" using TCP it's enough to just open a connection and see if the
 * connect fails or succeeds. If the host is unavailable or there is no
 * connectivity the connect will fail, otherwise it will succeed. This will be
 * checked in the uloop socket callback above */
static bool ping_send_tcp(struct ping_intf* pi)
{
	if (pi->ufd.fd > 0) {
		//ULOG_DBG("TCP connection timed out '%s'", pi->name);
		ping_uloop_fd_close(&pi->ufd);
	}

	int ret = tcp_connect(pi->device, pi->conf_host, pi->conf_tcp_port);
	if (ret > 0) {
		/* add socket handler to uloop.
		 * when connect() finishes, select indicates writability */
		pi->ufd.fd = ret;
		pi->ufd.cb = ping_fd_handler;
		ret = uloop_fd_add(&pi->ufd, ULOOP_WRITE);
		if (ret < 0) {
			ULOG_ERR("Could not add uloop fd %d for '%s'\n",
				pi->ufd.fd, pi->name);
			return false;
		}
	}
	return true;
}

bool ping_send(struct ping_intf* pi)
{
	bool ret = false;

	/* resolve at least every 10th time */
	if (pi->conf_host == 0 || pi->state != ONLINE || pi->cnt_sent % 10 == 0)
		ping_resolve(pi);

	/* either send ICMP ping or start TCP connection */
	if (pi->conf_proto == ICMP) {
		if (pi->ufd.fd <= 0) {
			ULOG_ERR("ping not init on '%s'\n", pi->name);
			return false;
		}
		ret = icmp_echo_send(pi->ufd.fd, pi->conf_host, pi->cnt_sent);
	} else if (pi->conf_proto == TCP) {
		ret = ping_send_tcp(pi);
	}

	/* common code */
	if (ret) {
		pi->cnt_sent++;
		clock_gettime(CLOCK_MONOTONIC, &pi->time_sent);
	} else
		ULOG_ERR("Could not send ping on '%s'\n", pi->name);
	return ret;
}

void ping_stop(struct ping_intf* pi)
{
	uloop_timeout_cancel(&pi->timeout_offline);
	uloop_timeout_cancel(&pi->timeout_send);
	ping_uloop_fd_close(&pi->ufd);
}
