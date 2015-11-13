# pingcheck
Check connectivity of interfaces in OpenWRT

Checks with "ping" (ICMP echo request/reply) wether a configured host (normally on the internet) can be reached via a specific interface. Then makes this information available via `ubus` and triggers "online" and "offline" scripts. It's like hotplug for internet connectivity.

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

And detailled interface status is also available:

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

When a interface status changes, scripts in `/etc/pingcheck/online.d/` or `/etc/pingcheck/offline.d/` are called and provided with `INTERFACE` and `DEVICE` environment variables, similar to hotplug scripts.
