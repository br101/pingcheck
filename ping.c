#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include "main.h"

/* uloop callback when received something on a ping socket */
static void ping_fd_handler(struct uloop_fd *fd,
			    __attribute__((unused)) unsigned int events)
{
	struct ping_intf* pi = container_of(fd, struct ping_intf, ufd);

	if (!icmp_echo_receive(fd->fd))
		return;

	//printlog(LOG_DEBUG, "Received pong on '%s'", pi->name);

	/* online just confirmed: move timeout for offline to later */
	uloop_timeout_set(&pi->timeout_offline, pi->conf_timeout * 1000);

	pi->cnt_succ++;
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

int ping_init(struct ping_intf* pi)
{
	int ret;

	if (pi->ufd.fd != 0) {
		printlog(LOG_ERR, "Ping on '%s' already init", pi->name);
		return 1;
	}

	ret = ubus_interface_get_status(pi->name, pi->device, MAX_IFNAME_LEN);
	if (ret < 0) {
		printlog(LOG_INFO, "Interface '%s' not found or error", pi->name);
		pi->state = UNKNOWN;
		return 0;
	} else if (ret == 0) {
		printlog(LOG_INFO, "Interface '%s' not up", pi->name);
		pi->state = DOWN;
		return 0;
	} else if (ret == 1) {
		printlog(LOG_INFO, "Interface '%s' no route", pi->name);
		pi->state = NO_ROUTE;
		return 0;
	} else if (ret == 2) {
		pi->state = UP;
	}

	printlog(LOG_INFO, "Init ping on '%s'", pi->name);

	/* init icmp socket */
	ret = icmp_init(pi->device);
	if (ret < 0)
		return 0;

	/* add socket handler to uloop */
	pi->ufd.fd = ret;
	pi->ufd.cb = ping_fd_handler;
	ret = uloop_fd_add(&pi->ufd, ULOOP_READ);
	if (ret < 0) {
		printlog(LOG_ERR, "Could not add uloop fd %d for '%s'",
			 pi->ufd.fd, pi->name);
		return 0;
	}

	/* regular sending of ping (start first in 1 sec) */
	pi->timeout_send.cb = uto_ping_send_cb;
	ret = uloop_timeout_set(&pi->timeout_send, 1000);
	if (ret < 0) {
		printlog(LOG_ERR, "Could not add uloop send timeout for '%s'",
			 pi->name);
		return 0;
	}

	/* timeout for offline state, if no reply has been received */
	pi->timeout_offline.cb = uto_offline_cb;
	ret = uloop_timeout_set(&pi->timeout_offline, pi->conf_timeout * 1000);
	if (ret < 0) {
		printlog(LOG_ERR, "Could not add uloop offline timeout for '%s'",
			 pi->name);
		return 0;
	}

	/* reset counters */
	pi->cnt_sent = 0;
	pi->cnt_succ = 0;

	return 1;
}

int ping_send(struct ping_intf* pi)
{
	if (pi->ufd.fd <= 0) {
		printlog(LOG_ERR, "ping not init on '%s'", pi->name);
		return 0;
	}

	int ret = icmp_echo_send(pi->ufd.fd, pi->conf_host, pi->cnt_sent);
	if (ret) {
		//printlog(LOG_DEBUG, "Sent ping on '%s'", pi->name);
		pi->cnt_sent++;
	} else {
		printlog(LOG_ERR, "Could not send ping on '%s'", pi->name);
	}
	return ret;
}

void ping_stop(struct ping_intf* pi)
{
	uloop_timeout_cancel(&pi->timeout_offline);
	uloop_timeout_cancel(&pi->timeout_send);

	if (pi->ufd.fd > 0) {
		uloop_fd_delete(&pi->ufd);
		close(pi->ufd.fd);
		pi->ufd.fd = 0;
	}
}
