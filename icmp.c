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
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <net/if.h>
#include <err.h>
#include "main.h"

static int pid = -1;

/* standard 1s complement checksum */
static unsigned short checksum(void *b, int len)
{
	unsigned short *buf = b;
	unsigned int sum = 0;
	unsigned short result;

	for (sum = 0; len > 1; len -= 2)
		sum += *buf++;
	if (len == 1)
		sum += *(unsigned char*)buf;
	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += (sum >> 16);
	result = ~sum;
	return result;
}

int icmp_init(const char* ifname)
{
	int ret;
	pid = getpid();

	int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (fd == -1) {
		warn("Could not open socket");
		return -1;
	}

	if (ifname != NULL) {
		if (strlen(ifname) >= IFNAMSIZ) {
			fprintf(stderr, "icmp_init: ifname too long");
			return -1;
		}
		struct ifreq ifr;
		strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
		ret = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
		if (ret < 0) {
			warn("Could not bind to '%s'", ifname);
			close(fd);
			return -1;
		}
	}
	return fd;
}

int icmp_echo_send(int fd, int dst_ip, int cnt)
{
	char buf[500];
	int ret;
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	addr.sin_addr.s_addr = dst_ip;

	struct icmphdr* icmp = (struct icmphdr*)buf;

	icmp->type = ICMP_ECHO;
	icmp->code = 0;
	icmp->un.echo.id = htons(pid);
	icmp->un.echo.sequence = htons(cnt);
	icmp->checksum = 0;
	icmp->checksum = checksum(buf, sizeof(struct icmphdr));

	ret = sendto(fd, &buf, sizeof(struct icmphdr), 0, (struct sockaddr*)&addr, sizeof(addr));
	if (ret <= 0) {
		warn("sendto");
		return 0;
	}
	return 1;
}

int icmp_echo_receive(int fd)
{
	char buf[500];
	int ret;

	ret = recv(fd, buf, sizeof(buf), 0);
	if (ret < (int)(sizeof(struct icmphdr) + sizeof(struct iphdr))) {
		warn("received packet too short");
		return 0;
	}

	struct iphdr *ip = (struct iphdr*)buf;
	struct icmphdr *icmp = (struct icmphdr*)(buf + ip->ihl*4);

	int csum_recv = icmp->checksum;
	icmp->checksum = 0; // need to zero before calculating checksum
	int csum_calc = checksum(icmp, sizeof(struct icmphdr));

	if (csum_recv == csum_calc &&		// checksum correct
	    icmp->type == ICMP_ECHOREPLY &&	// correct type
	    ntohs(icmp->un.echo.id) == pid) {	// we are sender
		return 1;
	}
	return 0;
}
