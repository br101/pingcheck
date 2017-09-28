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
#include <unistd.h>
#include <libubus.h>
#include "main.h"
#include "log.h"

static struct ubus_context *ctx;


/*** network interface events ***/

static struct ubus_event_handler interface_event_handler;

enum {
	EVT_INTF,
	EVT_ACTN,
};

static const struct blobmsg_policy event_policy[] = {
	[EVT_INTF] = { .name = "interface", .type = BLOBMSG_TYPE_STRING },
	[EVT_ACTN] = { .name = "action", .type = BLOBMSG_TYPE_STRING },
};

static void ubus_receive_interface_event(
			__attribute__((unused))struct ubus_context *ctx,
			__attribute__((unused))struct ubus_event_handler *ev,
			__attribute__((unused))const char *type,
			struct blob_attr *msg)
{
	const char* interface = NULL;
	const char* action = NULL;
	struct blob_attr* tb[ARRAY_SIZE(event_policy)];

	blobmsg_parse(event_policy, ARRAY_SIZE(event_policy), tb, blob_data(msg), blob_len(msg));

	if (tb[EVT_INTF] && tb[EVT_ACTN]) {
		interface = blobmsg_get_string(tb[EVT_INTF]);
		action = blobmsg_get_string(tb[EVT_ACTN]);
		notify_interface(interface, action);
	}
}

bool ubus_listen_network_events(void)
{
	/* ubus event listener */
	memset(&interface_event_handler, 0, sizeof(interface_event_handler));
	interface_event_handler.cb = ubus_receive_interface_event;
	int ret = ubus_register_event_handler(ctx, &interface_event_handler, "network.interface");
	if (ret < 0)
		return false;

	ubus_add_uloop(ctx);
	return true;
}


/*** generic result callback: just copies msg to last_result_msg ***/

static struct blob_attr* last_result_msg;

static void ubus_result_cb(__attribute__((unused)) struct ubus_request *req,
			   __attribute__((unused)) int type,
			   struct blob_attr *msg)
{
	if (!msg)
		return;

	unsigned int len = blob_raw_len(msg);
	last_result_msg = malloc(len);
	memcpy(last_result_msg, msg, len);
	// client needs to free(last_result_msg);
}


/*** network interface status ***/

static bool ubus_interface_status(const char* name)
{
	uint32_t id;
	int ret;
	char idstr[32];

	ret = snprintf(idstr, sizeof(idstr), "network.interface.%s", name);
	if (ret <= 0 || (unsigned int)ret >= sizeof(idstr)) // error or truncated
		return false;

	ret = ubus_lookup_id(ctx, idstr, &id);
	if (ret)
		return false;

	ret = ubus_invoke(ctx, id, "status", NULL, ubus_result_cb, NULL, UBUS_TIMEOUT);
	if (ret < 0)
		return false;

	// client needs to free(last_result_msg);
	return true;
}

enum {
	IFSTAT_UP,
	IFSTAT_DEVICE,
	IFSTAT_L3DEVICE,
	IFSTAT_ROUTE,
	IFSTAT_TARGET,
};

static const struct blobmsg_policy ifstat_policy[] = {
	[IFSTAT_UP] = { .name = "up", .type = BLOBMSG_TYPE_BOOL },
	[IFSTAT_DEVICE] = { .name = "device", .type = BLOBMSG_TYPE_STRING },
	[IFSTAT_L3DEVICE] = { .name = "l3_device", .type = BLOBMSG_TYPE_STRING },
	[IFSTAT_ROUTE] = { .name = "route", .type = BLOBMSG_TYPE_ARRAY },
};

enum {
	ROUTE_TARGET,
};

static const struct blobmsg_policy route_policy[] = {
	[ROUTE_TARGET] = { .name = "target", .type = BLOBMSG_TYPE_STRING },
};

/*
 * checks interface is up and default route goes thru it
 *
 * returns: -1 error or not found
 * 	     0 down
 * 	     1 up but no default route
 * 	     2 default route exists
 */
int ubus_interface_get_status(const char* name, char* device, size_t device_len)
{
	int ret;
	const char* dev;
	const char* route;

	ret = ubus_interface_status(name);
	if (!ret) {
		return -1;
	}

	struct blob_attr* tb[ARRAY_SIZE(ifstat_policy)];
	blobmsg_parse(ifstat_policy, ARRAY_SIZE(ifstat_policy), tb,
		      blob_data(last_result_msg), blob_len(last_result_msg));

	// up
	if (!tb[IFSTAT_UP]) {
		ret = -1; goto exit; // error
	}
	if (!blobmsg_get_bool(tb[IFSTAT_UP])) {
		ret = 0; goto exit; // down
	}

	// device
	if (tb[IFSTAT_DEVICE]) {
		dev = blobmsg_get_string(tb[IFSTAT_DEVICE]);
	} else if (tb[IFSTAT_L3DEVICE]) {
		dev = blobmsg_get_string(tb[IFSTAT_L3DEVICE]);
	} else {
		ret = -1; goto exit; // error
	}
	if (strlen(dev) >= device_len) {
		LOG_ERR("ubus_interface_get_status: device_len too short");
		ret = -1; goto exit; // error
	}
	strcpy(device, dev);

	// routes
	if (!tb[IFSTAT_ROUTE]) {
		ret = 1; goto exit; // up, no route
	}
	int len = blobmsg_data_len(tb[IFSTAT_ROUTE]);
	struct blob_attr *arr = blobmsg_data(tb[IFSTAT_ROUTE]);
	struct blob_attr *attr;
	__blob_for_each_attr(attr, arr, len) {
		struct blob_attr* tb2[ARRAY_SIZE(route_policy)];
		blobmsg_parse(route_policy, ARRAY_SIZE(route_policy), tb2,
			      blobmsg_data(attr), blobmsg_data_len(attr));
		if (tb2[ROUTE_TARGET]) {
			route = blobmsg_get_string(tb2[ROUTE_TARGET]);
			if (route != NULL && strcmp(route, "0.0.0.0") == 0) {
				ret = 2; goto exit; // default route found
			}
		}
	}

exit:
	free(last_result_msg);
	last_result_msg = NULL;
	return ret;
}


/*** server ***/

enum {
	PINGCHECK_INTF,
	PINGCHECK_RESET,
	__PINGCHECK_MAX
};

static const struct blobmsg_policy intf_policy[] = {
	[PINGCHECK_INTF] = { .name = "interface", .type = BLOBMSG_TYPE_STRING },
	[PINGCHECK_RESET] = { .name = "reset", .type = BLOBMSG_TYPE_BOOL },
};

static struct blob_buf b;

static int server_status(struct ubus_context *ctx,
			 __attribute__((unused)) struct ubus_object *obj,
			 struct ubus_request_data *req,
			 __attribute__((unused)) const char *method,
			 struct blob_attr *msg)
{
	struct blob_attr *tb[__PINGCHECK_MAX];
	const char* intf = NULL;

	blobmsg_parse(intf_policy, ARRAY_SIZE(intf_policy), tb, blob_data(msg), blob_len(msg));

	blob_buf_init(&b, 0);

	if (tb[PINGCHECK_INTF]) {
		/* detailed interface status */
		intf = blobmsg_get_string(tb[PINGCHECK_INTF]);
		struct ping_intf* pi = get_interface(intf);
		if (pi == NULL) {
			return -1;
		}
		blobmsg_add_string(&b, "status", get_status_str(pi->state));
		blobmsg_add_string(&b, "interface", pi->name);
		blobmsg_add_string(&b, "device", pi->device);
		blobmsg_add_u32(&b, "percent", pi->cnt_sent > 0 ? pi->cnt_succ*100/pi->cnt_sent : 0);
		blobmsg_add_u32(&b, "sent", pi->cnt_sent);
		blobmsg_add_u32(&b, "success", pi->cnt_succ);
		blobmsg_add_u32(&b, "last_rtt", pi->last_rtt);
		blobmsg_add_u32(&b, "max_rtt", pi->max_rtt);
	} else {
		/* global status / summary */
		int num;
		void* arr;
		const char* dest[MAX_NUM_INTERFACES];

		blobmsg_add_string(&b, "status", get_status_str(get_global_status()));

		arr = blobmsg_open_array(&b, "online_interfaces");
		num = get_online_interface_names(dest, MAX_NUM_INTERFACES);
		for (int i = 0; i < MAX_NUM_INTERFACES && i < num; i++)
			blobmsg_add_string(&b, NULL, dest[i]);
		blobmsg_close_array(&b, arr);

		arr = blobmsg_open_array(&b, "known_interfaces");
		num = get_all_interface_names(dest, MAX_NUM_INTERFACES);
		for (int i = 0; i < MAX_NUM_INTERFACES && i < num; i++)
			blobmsg_add_string(&b, NULL, dest[i]);
		blobmsg_close_array(&b, arr);
	}

	if (tb[PINGCHECK_RESET] && blobmsg_get_bool(tb[PINGCHECK_RESET])) {
		reset_counters(intf);
	}

	ubus_send_reply(ctx, req, b.head);
	return 0;
}

static const struct blobmsg_policy reset_policy[] = {
	[PINGCHECK_INTF] = { .name = "interface", .type = BLOBMSG_TYPE_STRING },
};

static int server_reset(__attribute__((unused)) struct ubus_context *ctx,
			__attribute__((unused)) struct ubus_object *obj,
			__attribute__((unused)) struct ubus_request_data *req,
			__attribute__((unused)) const char *method,
			struct blob_attr *msg)
{
	struct blob_attr *tb[__PINGCHECK_MAX];
	const char* intf = NULL;

	blobmsg_parse(reset_policy, ARRAY_SIZE(reset_policy), tb, blob_data(msg), blob_len(msg));

	if (tb[PINGCHECK_INTF])
		intf = blobmsg_get_string(tb[PINGCHECK_INTF]);

	reset_counters(intf);
	return 0;
}

static const struct ubus_method server_methods[] = {
	UBUS_METHOD("status", server_status, intf_policy),
	UBUS_METHOD("reset", server_reset, reset_policy),
};

static struct ubus_object_type server_object_type =
	UBUS_OBJECT_TYPE("pingcheck", server_methods);

static struct ubus_object server_object = {
	.name = "pingcheck",
	.type = &server_object_type,
	.methods = server_methods,
	.n_methods = ARRAY_SIZE(server_methods),
};

bool ubus_register_server(void) {
	int ret;

	ret = ubus_add_object(ctx, &server_object);
	if (ret)
		LOG_ERR("Failed to add server object: %s\n", ubus_strerror(ret));
	return ret ? false : true;
}


/*** init / finish ***/

bool ubus_init(void)
{
	ctx = ubus_connect(NULL);
	if (!ctx) {
		LOG_ERR("Failed to connect to ubus");
		return false;
	}
	return true;
}

void ubus_finish(void)
{
	free(last_result_msg);
	if (ctx == NULL)
		return;

	ubus_remove_object(ctx, &server_object);
	ubus_free(ctx);
}
