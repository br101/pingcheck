/* pingcheck - Check connectivity of interfaces in OpenWRT
 *
 * Copyright (C) 2016 Bruno Randolf <br1@einfach.org>
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
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include "main.h"

int tcp_connect(const char* ifname, int dst, int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd == -1) {
		warn("Could not open TCP socket");
		return -1;
	}

	/* bind to interface */
	if (ifname != NULL) {
		if (strlen(ifname) >= IFNAMSIZ) {
			fprintf(stderr, "TCP: ifname too long");
			return -1;
		}
		struct ifreq ifr;
		strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
		int ret = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
		if (ret < 0) {
			warn("TCP: could not bind to '%s'", ifname);
			close(fd);
			return -1;
		}
	}

	/* make non-blocking */
	unsigned int fl = fcntl(fd, F_GETFL, 0);
	fl |= O_NONBLOCK;
	fcntl(fd, F_SETFL, fl);

	/* connect */
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(port);
	addr.sin_addr.s_addr=dst;

	int ret = connect(fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
	if (ret == -1  && errno != EINPROGRESS) {
		warn("TCP: could not connect");
		return -1;
	}

	return fd;
}

bool tcp_check_connect(int fd)
{
	int err;
	socklen_t len = sizeof(err);
	getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
	return err == 0;
}
