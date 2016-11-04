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
#include <stdbool.h>
#include <libubox/uloop.h>
#include <libubox/runqueue.h>

#define MAX_IFNAME_LEN		16
#define MAX_NUM_INTERFACES	8
#define SCRIPTS_TIMEOUT		10 /* 10 sec */
#define UBUS_TIMEOUT		3000 /* 3 sec */

enum online_state { UNKNOWN, DOWN, NO_ROUTE, UP, OFFLINE, ONLINE };

enum protocol { ICMP, TCP };

struct scripts_proc {
	struct runqueue_process proc;
	struct ping_intf* intf;
	enum online_state state;
};

struct ping_intf {
	/* public state */
	char name[MAX_IFNAME_LEN];
	char device[MAX_IFNAME_LEN];
	enum online_state state;
	unsigned int cnt_sent;
	unsigned int cnt_succ;
	unsigned int last_rtt;	/* in ms */
	unsigned int max_rtt;	/* in ms */

	/* config items */
	int conf_interval;
	int conf_timeout;
	int conf_host;
	enum protocol conf_proto;
	int conf_tcp_port;

	/* internal state for ping */
	struct uloop_fd ufd;
	struct uloop_timeout timeout_offline;
	struct uloop_timeout timeout_send;
	struct timespec time_sent;

	/* internal state for scripts */
	struct scripts_proc scripts_on;
	struct scripts_proc scripts_off;
};

// utils.c
void __attribute__ ((format (printf, 2, 3)))
printlog(int level, const char *format, ...);
long timespec_diff_ms(struct timespec start, struct timespec end);

// icmp.c
int icmp_init(const char* ifname);
bool icmp_echo_send(int fd, int dst, int cnt);
bool icmp_echo_receive(int fd);

// tcp.c
int tcp_connect(const char* ifname, int dst, int port);
bool tcp_check_connect(int fd);

// ping.c
bool ping_init(struct ping_intf* pi);
bool ping_send(struct ping_intf* pi);
void ping_stop(struct ping_intf* pi);

// ubus.c
bool ubus_init(void);
bool ubus_listen_network_events(void);
int ubus_interface_get_status(const char* name, char* device, size_t device_len);
bool ubus_register_server(void);
void ubus_finish(void);

// uci.c
int uci_config_pingcheck(struct ping_intf* intf, int len);

// scripts.c
void scripts_init(void);
void scripts_run(struct ping_intf* pi, enum online_state state_new);
void scripts_finish(void);

// main.c
void notify_interface(const char* interface, const char* action);
struct ping_intf* get_interface(const char* interface);
const char* get_status_str(struct ping_intf* pi);
const char* get_global_status_str();
int get_online_interface_names(const char** dest, int destLen);
int get_all_interface_names(const char** dest, int destLen);
void state_change(enum online_state state_new, struct ping_intf* pi);
void reset_counters(const char* interface);
