#include <libubox/uloop.h>
#include <libubox/runqueue.h>

#define MAX_IFNAME_LEN		8
#define MAX_NUM_INTERFACES	8
#define SCRIPTS_TIMEOUT		10 /* 10 sec */
#define UBUS_TIMEOUT		3000 /* 3 sec */

enum online_state { UNKNOWN, DOWN, NO_ROUTE, UP, OFFLINE, ONLINE };

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

	/* config items */
	int conf_interval;
	int conf_timeout;
	int conf_host;

	/* internal state for ping */
	struct uloop_fd ufd;
	struct uloop_timeout timeout_offline;
	struct uloop_timeout timeout_send;

	/* internal state for scripts */
	struct scripts_proc scripts_on;
	struct scripts_proc scripts_off;
};

// utils.c
void __attribute__ ((format (printf, 2, 3)))
printlog(int level, const char *format, ...);

// icmp.c
int icmp_init(const char* ifname);
int icmp_echo_send(int fd, int dst, int cnt);
int icmp_echo_receive(int fd);

// ping.c
int ping_init(struct ping_intf* pi);
int ping_send(struct ping_intf* pi);
void ping_stop(struct ping_intf* pi);

// ubus.c
int ubus_init(void);
int ubus_listen_network_events(void);
int ubus_interface_get_status(const char* name, char* device, size_t device_len);
int ubus_register_server(void);
void ubus_finish(void);

// uci.c
int uci_init(void);
int uci_get_option(const char* nameIn, char* value, size_t valLen);
int uci_get_option_int(const char* nameIn);
void uci_get_pingcheck_interfaces(struct ping_intf* intf, int len);
void uci_finish(void);

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
