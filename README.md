# pingcheck
Check connectivity of interfaces in OpenWRT

Checks with "ping" (ICMP echo request/reply) wether a configured host (normally on the Internet) can be reached via a specific network interface. Then makes this information available via `ubus` and triggers "online" and "offline" scripts. It's like hotplug for internet connectivity and especially useful if your router could be connected via multiple interfaces at the same time.

## Config options

Section `default` or section `interface`

| Name		| Type		| Required	| Default	| Description |
| ------------- | ------------- | ------------- | ------------- | ----------- |
| `host`	| IP address	| yes		| (none)	| IP Address or hostname of ping destination |
| `interval`	| seconds	| yes		| (none)	| Ping will be sent every 'interval' seconds |
| `timeout`	| seconds	| yes		| (none)	| After no Ping replies have been received for 'timeout' seconds, the offline scripts will be executed |

All these values can either be defined in defaults, or in the interface, but the are required in one of them. Interface config overrides default.

Section `interface`

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

config interface
        option name sta

config interface
        option name umts

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

Detailled interface status is also available:

```
root@OpenWrt:~# ubus call pingcheck status "{'interface':'sta'}"
{
        "status": "ONLINE",
        "interface": "sta",
        "device": "wlan1",
        "percent": 100,
        "sent": 16,
        "success": 16
}
```

## Shell Scripts

When a interface status changes, scripts in `/etc/pingcheck/online.d/` or `/etc/pingcheck/offline.d/` are called and provided with `INTERFACE` and `DEVICE` environment variables, similar to hotplug scripts.
