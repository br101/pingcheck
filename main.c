#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "main.h"

/* main list of interfaces */
static struct ping_intf intf[MAX_NUM_INTERFACES];

/* config items (with default values) */
static int conf_ping_interval = 10; /* sec */
static int conf_offline_timeout = 30; /* sec */
static int conf_ping_host = 0x08080808; /* IP 8.8.8.8 */

void state_change(enum online_state state_new, struct ping_intf* pi)
{
	if (pi->state == state_new) /* no change */
		return;

	pi->state = state_new;

	printlog(LOG_INFO, "Interface '%s' changed to %s", pi->name,
		 get_status_str(pi));

	scripts_run(pi, state_new);
}

const char* get_status_str(struct ping_intf* pi)
{
	switch (pi->state) {
		case UNKNOWN: return "UNKNOWN";
		case DOWN: return "DOWN";
		case NO_ROUTE: return "NO_ROUTE";
		case UP: return "UP";
		case OFFLINE: return "OFFLINE";
		case ONLINE: return "ONLINE";
		default: return "INVALID";
	}
}

const char* get_global_status_str(void)
{
	for (int i=0; i < MAX_NUM_INTERFACES && intf[i].name[0]; i++) {
		if (intf[i].state == ONLINE)
			return "ONLINE";
	}
	return "OFFLINE";
}

int get_online_interface_names(const char** dest, int destLen)
{
	int j = 0;
	for (int i=0; i < MAX_NUM_INTERFACES && intf[i].name[0]; i++) {
		if (intf[i].state == ONLINE && j < destLen)
			dest[j++] = intf[i].name;
	}
	return j;
}

int get_all_interface_names(const char** dest, int destLen)
{
	int i = 0;
	for (; i < MAX_NUM_INTERFACES && intf[i].name[0] && i < destLen; i++) {
		dest[i] = intf[i].name;
	}
	return i;
}

/* called from ubus interface event */
void notify_interface(const char* interface, const char* action)
{
	struct ping_intf* pi = get_interface(interface);
	if (pi == NULL)
		return;

	if (strcmp(action, "ifup") == 0) {
		printlog(LOG_INFO, "Interface '%s' event UP", interface);
		ping_init(pi);
		ping_send(pi);
	} else if (strcmp(action, "ifdown") == 0) {
		printlog(LOG_INFO, "Interface '%s' event DOWN", interface);
		ping_stop(pi);
		state_change(DOWN, pi);
	}
}

/* also called from ubus server_status */
struct ping_intf* get_interface(const char* interface)
{
	struct ping_intf* pi = NULL;
	/* find interface in our list */
	for (int i=0; i < MAX_NUM_INTERFACES && intf[i].name[0]; i++) {
		if (strncmp(intf[i].name, interface, MAX_IFNAME_LEN) == 0) {
			pi = &intf[i];
			break;
		}
	}
	return pi;
}

static void get_uci_options(void)
{
	int ret;

	uci_init();

	ret = uci_get_option_int("pingcheck.@main[0].interval");
	if (ret > 0)
		conf_ping_interval = ret;

	ret = uci_get_option_int("pingcheck.@main[0].timeout");
	if (ret > 0)
		conf_offline_timeout = ret;

	char hostname[200];
	struct in_addr inaddr;
	ret = uci_get_option("pingcheck.@main[0].host", hostname, sizeof(hostname));
	if (ret) {
		inet_aton(hostname, &inaddr);
		conf_ping_host = inaddr.s_addr;
	}

	printf("Interval: %d\n", conf_ping_interval);
	printf("Timeout: %d\n", conf_offline_timeout);
	printf("Host: %s (%x)\n", inet_ntoa(inaddr), inaddr.s_addr);

	uci_get_pingcheck_interfaces(intf, MAX_NUM_INTERFACES);

	uci_finish();
}

int main(__attribute__((unused)) int argc, __attribute__((unused)) char** argv)
{
	int ret;

	openlog("pingcheck", LOG_PID|LOG_CONS, LOG_DAEMON);

	get_uci_options();

	ret = uloop_init();
	if (ret < 0)
		return EXIT_FAILURE;

	scripts_init();

	ret = ubus_init();
	if (!ret)
		goto exit;

	/* listen for ubus network interface events */
	ret = ubus_listen_network_events();
	if (!ret)
		goto exit;

	ubus_register_server();

	/* start ping on all available interfaces */
	for (int i=0; i < MAX_NUM_INTERFACES && intf[i].name[0]; i++) {
		// TODO: make config per interface
		intf[i].conf_interval = conf_ping_interval;
		intf[i].conf_timeout = conf_offline_timeout;
		intf[i].conf_host = conf_ping_host;
		ping_init(&intf[i]);
	}

	/* main loop */
	uloop_run();

	/* print statistics and cleanup */
	printf("\n");
	for (int i=0; i < MAX_NUM_INTERFACES && intf[i].name[0]; i++) {
		printf("%s:\t%-8s %3.0f%% (%d/%d on %s)\n", intf[i].name,
		       get_status_str(&intf[i]),
		       (float)intf[i].cnt_succ*100/intf[i].cnt_sent,
		       intf[i].cnt_succ, intf[i].cnt_sent, intf[i].device);
		ping_stop(&intf[i]);
	}

exit:
	scripts_finish();
	uloop_done();
	ubus_finish();
	closelog();

	return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
