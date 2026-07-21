#!/usr/bin/env python3
"""RavLight UDP device discovery tool (Windows/cross-platform).

Speaks the same protocol as discovery_udp.cpp:
  - broadcast/unicast "R_DISCOVER" to UDP port 4210 (DISC_UDP_DISCOVER_PORT)
  - devices reply with a JSON blob to UDP port 4211 (DISC_UDP_RESPONSE_PORT)
  - optional commands to UDP port 4212 (DISC_UDP_COMMAND_PORT)

Two discovery modes:
  1. Broadcast (default) — fast, but only reaches devices on the same
     L2 broadcast domain / VLAN as this PC. Routers do not forward
     broadcast traffic between subnets.
  2. Unicast sweep (--range) — sends R_DISCOVER as point-to-point UDP to
     every host in a CIDR range. Works across routed subnets as long as
     UDP ports 4210/4211 are not firewalled between segments.

Usage:
  python discovery_tool.py                          # broadcast on all local subnets
  python discovery_tool.py --range 192.168.10.0/24   # unicast sweep of a remote segment
  python discovery_tool.py --range 192.168.10.0/24 --range 10.0.5.0/24
  python discovery_tool.py --targets 192.168.1.50,192.168.1.51
  python discovery_tool.py --highlight 192.168.10.42
  python discovery_tool.py --reset 192.168.10.42
"""

import argparse
import ipaddress
import json
import socket
import sys
import time

DISCOVER_PORT = 4210
RESPONSE_PORT = 4211
COMMAND_PORT = 4212
DISCOVER_MSG = b"R_DISCOVER"


def local_broadcast_addrs():
    """Best-effort list of broadcast addresses for locally configured IPv4 subnets."""
    addrs = {"255.255.255.255"}
    try:
        hostname = socket.gethostname()
        for info in socket.getaddrinfo(hostname, None, socket.AF_INET):
            ip = info[4][0]
            if ip.startswith("127."):
                continue
            # Assume a /24 — good enough for typical home/office LANs; use
            # --range for anything more precise or for other segments.
            parts = ip.split(".")
            addrs.add(f"{parts[0]}.{parts[1]}.{parts[2]}.255")
    except socket.gaierror:
        pass
    return sorted(addrs)


def make_rx_socket(timeout):
    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    rx.bind(("", RESPONSE_PORT))
    rx.settimeout(0.25)
    return rx


def send_discover(tx, ip):
    try:
        tx.sendto(DISCOVER_MSG, (ip, DISCOVER_PORT))
    except OSError as e:
        print(f"  ! send to {ip} failed: {e}", file=sys.stderr)


def collect_responses(rx, deadline, seen):
    while time.time() < deadline:
        try:
            data, addr = rx.recvfrom(2048)
        except socket.timeout:
            continue
        try:
            info = json.loads(data.decode("utf-8", errors="replace"))
        except json.JSONDecodeError:
            continue
        mac = info.get("mac", "")
        key = mac or addr[0]
        if key in seen:
            continue
        info["_from"] = addr[0]
        seen[key] = info
        print_device(info)


def print_device(info):
    print(f"  {info.get('id','?'):<12} {info.get('ip', info.get('_from','?')):<15} "
          f"fixture={info.get('fixture','?'):<8} fw={info.get('fw','?'):<10} "
          f"mode={info.get('mode','?'):<6} mac={info.get('mac','?')}")


def cmd_discover(args):
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tx.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    rx = make_rx_socket(args.timeout)

    seen = {}
    targets = []

    if args.range:
        for cidr in args.range:
            net = ipaddress.ip_network(cidr, strict=False)
            targets.extend(str(h) for h in net.hosts())
        print(f"Unicast sweep: {len(targets)} host(s) across {len(args.range)} range(s)")
    elif args.targets:
        targets = [t.strip() for t in args.targets.split(",") if t.strip()]
    else:
        targets = local_broadcast_addrs()
        print(f"Broadcasting R_DISCOVER to: {', '.join(targets)}")

    print(f"Listening for replies on UDP {RESPONSE_PORT} (timeout {args.timeout}s)...\n")

    for ip in targets:
        send_discover(tx, ip)

    deadline = time.time() + args.timeout
    collect_responses(rx, deadline, seen)

    print(f"\n{len(seen)} device(s) found.")
    if not seen:
        print("Nothing replied. If devices are on a different subnet, make sure:")
        print("  - you used --range/--targets (broadcast never crosses routers)")
        print("  - UDP 4210 (outbound) and 4211 (inbound) aren't blocked by firewall/ACL")
    tx.close()
    rx.close()


def send_command(ip, cmd, ssid=None, pwd=None):
    payload = {"cmd": cmd}
    if cmd == "CONNECT":
        payload["ssid"] = ssid or ""
        payload["pwd"] = pwd or ""
    data = json.dumps(payload).encode("utf-8")
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tx.sendto(data, (ip, COMMAND_PORT))
    tx.close()
    print(f"Sent {cmd} to {ip}:{COMMAND_PORT}")


def main():
    p = argparse.ArgumentParser(description="RavLight UDP device discovery")
    p.add_argument("--range", action="append", metavar="CIDR",
                    help="Unicast-sweep a subnet, e.g. 192.168.10.0/24. Repeatable for multiple segments.")
    p.add_argument("--targets", metavar="IP,IP,...",
                    help="Comma-separated explicit IPs/broadcast addresses to probe.")
    p.add_argument("--timeout", type=float, default=2.5, help="Seconds to wait for replies (default 2.5)")
    p.add_argument("--highlight", metavar="IP", help="Send HIGHLIGHT command to a device IP")
    p.add_argument("--reset", metavar="IP", help="Send RESET (factory reset) command to a device IP")
    p.add_argument("--apmode", metavar="IP", help="Send APMODE command to a device IP")
    args = p.parse_args()

    if args.highlight:
        send_command(args.highlight, "HIGHLIGHT")
        return
    if args.reset:
        send_command(args.reset, "RESET")
        return
    if args.apmode:
        send_command(args.apmode, "APMODE")
        return

    cmd_discover(args)


if __name__ == "__main__":
    main()
