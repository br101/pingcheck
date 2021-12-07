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
#include "log.h"
#include "main.h"
#include <arpa/inet.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uci.h>

/** analogous to uci_lookup_option_string from uci.h, returns -1 when not found
 */
static int uci_lookup_option_int(struct uci_context* uci, struct uci_section* s,
								 const char* name)
{
	const char* str = uci_lookup_option_string(uci, s, name);
	return str == NULL ? -1 : atoi(str);
}

int uci_config_pingcheck(struct ping_intf* intf, int len)
{
	struct uci_context* uci;
	struct uci_package* p;
	struct uci_element* e;
	const char* str;
	int val;
	int idx = 0;
	int default_interval = 0;
	int default_timeout = 0;
	const char* default_hostname = NULL;
	enum protocol default_proto = ICMP;
	int default_tcp_port = 80;
	int default_panic_to = -1; // don't use
	bool default_ignore_ubus = false;

	uci = uci_alloc_context();
	if (uci == NULL) {
		return 0;
	}

	if (uci_load(uci, "pingcheck", &p)) {
		uci_free_context(uci);
		return 0;
	}

	uci_foreach_element(&p->sections, e)
	{
		struct uci_section* s = uci_to_section(e);
		if (strcmp(s->type, "default") == 0) {
			/* default values, most useful when first in file */
			default_interval = uci_lookup_option_int(uci, s, "interval");
			default_timeout = uci_lookup_option_int(uci, s, "timeout");
			default_hostname = uci_lookup_option_string(uci, s, "host");
			default_panic_to = uci_lookup_option_int(uci, s, "panic");
			str = uci_lookup_option_string(uci, s, "protocol");
			if (str != NULL && strcmp(str, "tcp") == 0) {
				default_proto = TCP;
			}
			val = uci_lookup_option_int(uci, s, "tcp_port");
			if (val > 0) {
				default_tcp_port = val;
			}
			val = uci_lookup_option_int(uci, s, "ignore_ubus");
			if (val > 0) {
				default_ignore_ubus = true;
			}
		} else if (strcmp(s->type, "interface") == 0) {
			/* interface config, needs at least name */
			str = uci_lookup_option_string(uci, s, "name");
			if (str == NULL) {
				continue;
			}
			if (strlen(str) >= MAX_IFNAME_LEN) {
				LOG_ERR("UCI: Interface name too long");
				continue;
			}
			strcpy(intf[idx].name, str);

			val = uci_lookup_option_int(uci, s, "interval");
			intf[idx].conf_interval = val > 0 ? val : default_interval;

			val = uci_lookup_option_int(uci, s, "timeout");
			intf[idx].conf_timeout = val > 0 ? val : default_timeout;

			val = uci_lookup_option_int(uci, s, "panic");
			intf[idx].conf_panic_timeout = val > 0 ? val : default_panic_to;

			str = uci_lookup_option_string(uci, s, "host");
			if (str != NULL)
				strncpy(intf[idx].conf_hostname, str, MAX_HOSTNAME_LEN);
			else if (default_hostname != NULL)
				strncpy(intf[idx].conf_hostname, default_hostname,
						MAX_HOSTNAME_LEN);

			str = uci_lookup_option_string(uci, s, "protocol");
			if (str != NULL && strcmp(str, "tcp") == 0) {
				intf[idx].conf_proto = TCP;
			} else if (str != NULL && strcmp(str, "icmp") == 0) {
				intf[idx].conf_proto = ICMP;
			} else {
				intf[idx].conf_proto = default_proto;
			}

			val = uci_lookup_option_int(uci, s, "tcp_port");
			intf[idx].conf_tcp_port = val > 0 ? val : default_tcp_port;

			val = uci_lookup_option_int(uci, s, "ignore_ubus");
			intf[idx].conf_ignore_ubus = val > 0 ? true : default_ignore_ubus;

			if (intf[idx].conf_interval <= 0 || intf[idx].conf_timeout <= 0
				|| intf[idx].conf_hostname[0] == '\0') {
				LOG_ERR("UCI: interface '%s' config not complete",
						intf[idx].name);
				continue;
			} else {
				LOG_INF("Configured interface '%s' interval %d timeout %d host "
						"%s %s (%d) ignore_ubus %d",
						intf[idx].name, intf[idx].conf_interval,
						intf[idx].conf_timeout, intf[idx].conf_hostname,
						intf[idx].conf_proto == TCP ? "TCP" : "ICMP",
						intf[idx].conf_tcp_port, intf[idx].conf_ignore_ubus);
			}

			if (++idx > len) {
				LOG_ERR("UCI: Can not handle more than %d interfaces", len);
				return 0;
			}
		}
	}

	uci_unload(uci, p);
	uci_free_context(uci);
	return idx;
}
