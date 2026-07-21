#!/usr/bin/env python3
"""RavLight Companion — mini visual app for device discovery + multi-device setup.

Windows GUI, stdlib only (tkinter — ships with the python.org installer).
Speaks the same UDP protocol as discovery_udp.cpp:
  - R_DISCOVER  -> UDP 4210 (broadcast or unicast sweep)
  - JSON reply  <- UDP 4211
  - commands    -> UDP 4212 (HIGHLIGHT / RESET / APMODE / CONNECT)

Run:
  python ravlight_companion.py
"""

import ipaddress
import json
import socket
import threading
import time
import webbrowser
import queue
import tkinter as tk
from tkinter import ttk, messagebox

DISCOVER_PORT = 4210
RESPONSE_PORT = 4211
COMMAND_PORT = 4212
DISCOVER_MSG = b"R_DISCOVER"

COLUMNS = ("id", "ip", "fixture", "mode", "fw", "rssi", "temp", "mac")
HEADERS = {"id": "ID", "ip": "IP", "fixture": "Fixture", "mode": "Mode",
           "fw": "FW", "rssi": "RSSI", "temp": "Temp", "mac": "MAC"}


def local_broadcast_addrs():
    addrs = {"255.255.255.255"}
    try:
        hostname = socket.gethostname()
        for info in socket.getaddrinfo(hostname, None, socket.AF_INET):
            ip = info[4][0]
            if ip.startswith("127."):
                continue
            parts = ip.split(".")
            addrs.add(f"{parts[0]}.{parts[1]}.{parts[2]}.255")
    except socket.gaierror:
        pass
    return sorted(addrs)


def send_command(ip, cmd, ssid=None, pwd=None):
    payload = {"cmd": cmd}
    if cmd == "CONNECT":
        payload["ssid"] = ssid or ""
        payload["pwd"] = pwd or ""
    data = json.dumps(payload).encode("utf-8")
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        tx.sendto(data, (ip, COMMAND_PORT))
    finally:
        tx.close()


class Scanner:
    """Runs a discovery sweep on a background thread, reporting results via queue."""

    def __init__(self, result_queue):
        self.q = result_queue
        self.stop_event = threading.Event()
        self.thread = None

    def start(self, targets, timeout):
        self.stop_event.clear()
        self.thread = threading.Thread(target=self._run, args=(targets, timeout), daemon=True)
        self.thread.start()

    def _run(self, targets, timeout):
        tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        tx.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            rx.bind(("", RESPONSE_PORT))
        except OSError as e:
            self.q.put(("error", f"Could not open port {RESPONSE_PORT}: {e}"))
            tx.close()
            return
        rx.settimeout(0.2)

        self.q.put(("status", f"Sending R_DISCOVER to {len(targets)} target(s)..."))
        for ip in targets:
            if self.stop_event.is_set():
                break
            try:
                tx.sendto(DISCOVER_MSG, (ip, DISCOVER_PORT))
            except OSError:
                pass

        seen = set()
        deadline = time.time() + timeout
        while time.time() < deadline and not self.stop_event.is_set():
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
            seen.add(key)
            info["_from"] = addr[0]
            self.q.put(("device", info))

        tx.close()
        rx.close()
        self.q.put(("done", len(seen)))


class CompanionApp:
    def __init__(self, root):
        self.root = root
        root.title("RavLight Companion")
        root.geometry("880x560")

        self.result_queue = queue.Queue()
        self.scanner = Scanner(self.result_queue)
        self.devices = {}  # mac/ip -> info dict

        self._build_ui()
        self.root.after(100, self._poll_queue)

    def _build_ui(self):
        top = ttk.Frame(self.root, padding=8)
        top.pack(fill="x")

        ttk.Button(top, text="Scan local network", command=self.scan_local).pack(side="left")
        ttk.Label(top, text="   Different segment (CIDR):").pack(side="left")
        self.range_entry = ttk.Entry(top, width=20)
        self.range_entry.insert(0, "192.168.10.0/24")
        self.range_entry.pack(side="left", padx=4)
        ttk.Button(top, text="Scan range", command=self.scan_range).pack(side="left")

        ttk.Label(top, text="   Timeout(s):").pack(side="left", padx=(12, 2))
        self.timeout_entry = ttk.Entry(top, width=5)
        self.timeout_entry.insert(0, "2.5")
        self.timeout_entry.pack(side="left")

        # Device table
        table_frame = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        table_frame.pack(fill="both", expand=True)

        self.tree = ttk.Treeview(table_frame, columns=COLUMNS, show="headings",
                                  selectmode="extended")
        for c in COLUMNS:
            self.tree.heading(c, text=HEADERS[c])
            self.tree.column(c, width=90 if c != "mac" else 140, anchor="w")
        self.tree.pack(side="left", fill="both", expand=True)
        self.tree.bind("<Double-1>", lambda e: self.open_webui())

        scroll = ttk.Scrollbar(table_frame, orient="vertical", command=self.tree.yview)
        scroll.pack(side="left", fill="y")
        self.tree.configure(yscrollcommand=scroll.set)

        # Actions on selected device(s)
        actions = ttk.LabelFrame(self.root, text="Actions on selected device(s)", padding=8)
        actions.pack(fill="x", padx=8, pady=(0, 4))

        ttk.Button(actions, text="Open Web UI", command=self.open_webui).pack(side="left")
        ttk.Button(actions, text="Highlight", command=self.do_highlight).pack(side="left", padx=4)
        ttk.Button(actions, text="AP Mode", command=self.do_apmode).pack(side="left", padx=4)
        ttk.Button(actions, text="Reset (factory)", command=self.do_reset).pack(side="left", padx=4)

        # Batch WiFi config
        wifi = ttk.LabelFrame(self.root, text="Configure WiFi on all selected devices", padding=8)
        wifi.pack(fill="x", padx=8, pady=(0, 4))

        ttk.Label(wifi, text="SSID:").pack(side="left")
        self.ssid_entry = ttk.Entry(wifi, width=24)
        self.ssid_entry.pack(side="left", padx=4)
        ttk.Label(wifi, text="Password:").pack(side="left")
        self.pwd_entry = ttk.Entry(wifi, width=24, show="*")
        self.pwd_entry.pack(side="left", padx=4)
        ttk.Button(wifi, text="Apply to selected", command=self.do_connect).pack(side="left", padx=8)

        # Status/log
        self.status_var = tk.StringVar(value="Ready.")
        status_bar = ttk.Label(self.root, textvariable=self.status_var, relief="sunken", anchor="w", padding=4)
        status_bar.pack(fill="x", side="bottom")

    # --- scanning ---

    def scan_local(self):
        targets = local_broadcast_addrs()
        self._start_scan(targets)

    def scan_range(self):
        cidr = self.range_entry.get().strip()
        if not cidr:
            messagebox.showwarning("Missing range", "Enter a CIDR, e.g. 192.168.10.0/24")
            return
        try:
            net = ipaddress.ip_network(cidr, strict=False)
        except ValueError as e:
            messagebox.showerror("Invalid CIDR", str(e))
            return
        targets = [str(h) for h in net.hosts()]
        if len(targets) > 512:
            if not messagebox.askyesno("Large range",
                                        f"This range has {len(targets)} hosts, it may be slow. Continue?"):
                return
        self._start_scan(targets)

    def _start_scan(self, targets):
        try:
            timeout = float(self.timeout_entry.get())
        except ValueError:
            timeout = 2.5
        for item in self.tree.get_children():
            self.tree.delete(item)
        self.devices.clear()
        self.status_var.set("Scanning...")
        self.scanner.start(targets, timeout)

    def _poll_queue(self):
        try:
            while True:
                kind, payload = self.result_queue.get_nowait()
                if kind == "device":
                    self._add_device(payload)
                elif kind == "status":
                    self.status_var.set(payload)
                elif kind == "error":
                    self.status_var.set(payload)
                    messagebox.showerror("Error", payload)
                elif kind == "done":
                    self.status_var.set(f"Scan complete: {payload} device(s) found.")
        except queue.Empty:
            pass
        self.root.after(150, self._poll_queue)

    def _add_device(self, info):
        ip = info.get("ip") or info.get("_from")
        key = info.get("mac") or ip
        self.devices[key] = info
        self.tree.insert("", "end", iid=key, values=(
            info.get("id", "?"), ip, info.get("fixture", "?"), info.get("mode", "?"),
            info.get("fw", "?"), info.get("rssi", "?"), info.get("temp", "?"), info.get("mac", "?"),
        ))

    # --- selection helpers ---

    def _selected_ips(self):
        ips = []
        for key in self.tree.selection():
            info = self.devices.get(key)
            if info:
                ips.append(info.get("ip") or info.get("_from"))
        return ips

    def _require_selection(self):
        ips = self._selected_ips()
        if not ips:
            messagebox.showinfo("No selection", "Select at least one device in the table.")
        return ips

    # --- actions ---

    def open_webui(self):
        ips = self._selected_ips()
        if not ips:
            messagebox.showinfo("No selection", "Select a device in the table.")
            return
        webbrowser.open(f"http://{ips[0]}")

    def do_highlight(self):
        ips = self._require_selection()
        for ip in ips:
            send_command(ip, "HIGHLIGHT")
        self.status_var.set(f"Highlight sent to {len(ips)} device(s).")

    def do_apmode(self):
        ips = self._require_selection()
        if not ips:
            return
        if not messagebox.askyesno("Confirm AP Mode",
                                    f"Switch {len(ips)} device(s) into Access Point mode?"):
            return
        for ip in ips:
            send_command(ip, "APMODE")
        self.status_var.set(f"APMODE sent to {len(ips)} device(s).")

    def do_reset(self):
        ips = self._require_selection()
        if not ips:
            return
        if not messagebox.askyesno("Confirm factory reset",
                                    f"This resets the configuration of {len(ips)} selected device(s). Continue?"):
            return
        for ip in ips:
            send_command(ip, "RESET")
        self.status_var.set(f"RESET sent to {len(ips)} device(s).")

    def do_connect(self):
        ips = self._require_selection()
        if not ips:
            return
        ssid = self.ssid_entry.get().strip()
        pwd = self.pwd_entry.get()
        if not ssid:
            messagebox.showwarning("Missing SSID", "Enter the WiFi network SSID.")
            return
        if not messagebox.askyesno("Confirm WiFi configuration",
                                    f"Set SSID '{ssid}' on {len(ips)} device(s)? The devices will restart."):
            return
        for ip in ips:
            send_command(ip, "CONNECT", ssid=ssid, pwd=pwd)
        self.status_var.set(f"CONNECT ({ssid}) sent to {len(ips)} device(s).")


def main():
    root = tk.Tk()
    CompanionApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
