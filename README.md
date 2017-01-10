# pingcheck
Check connectivity of network interfaces in OpenWRT

Checks wether a configured host (normally on the Internet) can be reached via a specific network interface. Then makes this information available via `ubus` and triggers "online" and "offline" scripts. It's like hotplug for internet connectivity and especially useful if your router could be connected via multiple interfaces at the same time. The check be done with classic ICMP echo requests like `ping` or by trying to establish a TCP connection to a web-server (or any TCP server), which can be useful when ICMP is blocked by a firewall.

## Config options

### Section `default` or section `interface`

| Name		| Type		| Required	| Default	| Description |
| ------------- | ------------- | ------------- | ------------- | ----------- |
| `host`	| IP address	| yes		| (none)	| IP Address or hostname of ping destination |
| `interval`	| seconds	| yes		| (none)	| Ping will be sent every 'interval' seconds |
| `timeout`	| seconds	| yes		| (none)	| After no Ping replies have been received for 'timeout' seconds, the offline scripts will be executed |
| `protocol`	| `icmp` or `tcp` | no		| `icmp`        | Use classic ICMP ping (default) or TCP connect |
| `tcp_port`    | port number	| no		| 80	        | TCP port to connect to when protocol is `tcp` |
| `panic`       | minutes	| no		| (not used)	| If the system is OFFLINE for more than this time, the scripts in '/etc/pingcheck/panic.d' will be called |

All these values can either be defined in defaults, or in the interface, but the are required in one of them. Interface config overrides default.

### Section `interface`

| Name		| Type		| Required	| Default	| Description |
| ------------- | ------------- | ------------- | ------------- | ----------- |
| `name`	| interface name | yes		| (none)	| UCI name of interface |

Here is an example config:

```
config default
        option host 8.8.8.8
        option interval 10
        option timeout 30

config interface
        option name wan
        # default options will be used

config interface
        option name sta
        option host 192.168.11.1

config interface
        option name umts
        option interval 5
        option timeout 60

config interface
        option name bat_cl
```

## ubus Interface

The overview status shown on ubus looks like this:

```
root@OpenWrt:~# ubus call pingcheck status
{
        "status": "ONLINE",
        "online_interfaces": [
                "wan",
                "sta"
        ],
        "known_interfaces": [
                "wan",
                "sta",
                "umts",
                "bat_cl"
        ]
}
```

Detailed interface status is also available:

```
root@OpenWrt:~# ubus call pingcheck status "{'interface':'sta'}"
{
        "status": "ONLINE",
        "interface": "sta",
        "device": "wlan1",
        "percent": 100,
        "sent": 16,
        "success": 16,
        "last_rtt": 101,
        "max_rtt": 136
}
```

You can reset the counters and interface status for all interfaces like this:

```
root@OpenWrt:~# ubus call pingcheck reset
```

Or for just one specific interface:

```
root@OpenWrt:~# ubus call pingcheck reset '{"interface":"wan"}'
```

## Shell Scripts

When a interface status changes, scripts in `/etc/pingcheck/online.d/` or `/etc/pingcheck/offline.d/` are called and provided with `INTERFACE`, `DEVICE` and `GLOBAL` environment variables, similar to hotplug scripts. 

| Variable      | Description                                                                           |
|---------------|---------------------------------------------------------------------------------------|
| `INTERFACE`   | logical network interface (e.g. `wan`) which goes online or offline                   |
| `DEVICE`      | physical device (e.g. `eth0`) which goes online or offline                            |
| `GLOBAL`      | `ONLINE` or `OFFLINE` depending on wether device is online thru other interfaces      |

Additionally, if option `panic` is set, scripts in `/etc/pingcheck/panic.d/` are called after the system has been globally offline for more than `panic` minutes.
