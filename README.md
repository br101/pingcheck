# pingcheck
Check connectivity of interfaces in OpenWRT

Checks by using "ping" (ICMP echo) wether a configured host (normally on the internet) can be reached via a specific interface. Then makes this information available via `ubus` and triggers "online" and "offline" scripts.
