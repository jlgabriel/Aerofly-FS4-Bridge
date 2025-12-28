#!/usr/bin/env python3
"""
Aerofly FS4 Reader - Realtime Monitor (Tkinter)

This tool displays live Aerofly flight data in a tree view with
organized categories and real-time updates via TCP streaming.

Features:
- Organized tree view by category
- Real-time data updates
- Raw values and converted units (INTL/US)
- Export snapshots to JSON/CSV
- Connection status indicator

Usage:
    python realtime_monitor.py

Requirements:
    - AeroflyReader.dll installed in Aerofly FS 4
    - Aerofly FS 4 running with an active flight
"""

import json
import socket
import sys
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import threading
import datetime
import math


TCP_HOST = "localhost"
TCP_PORT = 12345
REFRESH_MS = 100


class DataReceiver:
    """Receives telemetry from AeroflyReader TCP JSON stream."""

    def __init__(self, host: str = TCP_HOST, port: int = TCP_PORT):
        self.host = host
        self.port = port
        self.socket = None
        self.latest_data = {}
        self.running = False
        self.receive_thread = None
        self.connected = False

    def connect(self):
        """Connect to the TCP server."""
        try:
            if self.socket:
                try:
                    self.socket.close()
                except Exception:
                    pass

            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(5.0)
            self.socket.connect((self.host, self.port))
            self.socket.settimeout(1.0)
            self.connected = True
            return True
        except Exception as e:
            print(f"Connection error: {e}")
            self.connected = False
            return False

    def start_receiving(self):
        """Start the background receiver thread."""
        self.running = True
        self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
        self.receive_thread.start()

    def _receive_loop(self):
        """Main receive loop."""
        buffer = ""
        while self.running:
            try:
                if not self.socket or not self.connected:
                    if not self.connect():
                        import time
                        time.sleep(2.0)
                        continue

                data = self.socket.recv(16384).decode("utf-8")
                if not data:
                    self.connected = False
                    import time
                    time.sleep(0.5)
                    continue

                buffer += data
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    if not line.strip():
                        continue
                    self._process_line(line)

            except socket.timeout:
                continue
            except Exception as e:
                print(f"Receive error: {e}")
                self.connected = False
                try:
                    if self.socket:
                        self.socket.close()
                except Exception:
                    pass
                self.socket = None
                import time
                time.sleep(1.0)

    def _process_line(self, line: str):
        """Process a single JSON line."""
        try:
            self.latest_data = json.loads(line)
        except json.JSONDecodeError:
            pass

    def stop(self):
        """Stop the receiver."""
        self.running = False
        if self.receive_thread:
            self.receive_thread.join(timeout=1.5)
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass


class AeroflyMonitorApp(tk.Tk):
    """Tkinter desktop app that visualizes Aerofly Reader data in real time."""

    def __init__(self):
        super().__init__()
        self.title("Aerofly FS4 Realtime Monitor - Reader")
        self.geometry("1100x700")

        self.receiver = DataReceiver()
        self.tree_items = {}  # item_id -> field_name
        self.group_nodes = {}  # group -> item_id

        # Top bar
        top = ttk.Frame(self)
        top.pack(side=tk.TOP, fill=tk.X, padx=8, pady=6)

        ttk.Label(top, text="AeroflyReader Monitor").pack(side=tk.LEFT)
        ttk.Button(top, text="Connect", command=self.on_connect).pack(side=tk.LEFT, padx=6)
        ttk.Button(top, text="Disconnect", command=self.on_disconnect).pack(side=tk.LEFT, padx=6)
        ttk.Button(top, text="Export...", command=self.on_export_snapshot).pack(side=tk.LEFT, padx=6)

        self.status_var = tk.StringVar(value="Status: Idle")
        ttk.Label(top, textvariable=self.status_var).pack(side=tk.RIGHT)

        # Tree
        cols = ("name", "value", "units", "intl", "us")
        self.tree = ttk.Treeview(self, columns=cols, show="tree headings")
        self.tree.heading("#0", text="Category")
        self.tree.heading("name", text="Variable")
        self.tree.heading("value", text="Raw Value")
        self.tree.heading("units", text="Units")
        self.tree.heading("intl", text="INTL")
        self.tree.heading("us", text="US")

        self.tree.column("#0", width=180, stretch=False)
        self.tree.column("name", width=240, stretch=True)
        self.tree.column("value", width=180, stretch=False)
        self.tree.column("units", width=100, stretch=False)
        self.tree.column("intl", width=160, stretch=False)
        self.tree.column("us", width=160, stretch=False)

        self.tree.pack(expand=True, fill=tk.BOTH, padx=8, pady=8)

        # Footer
        foot = ttk.Frame(self)
        foot.pack(side=tk.BOTTOM, fill=tk.X, padx=8, pady=6)
        self.info_var = tk.StringVar(value="Waiting for connection...")
        ttk.Label(foot, textvariable=self.info_var).pack(side=tk.LEFT)

        self._build_tree_structure()
        self.after(REFRESH_MS, self.refresh_tick)

    def _build_tree_structure(self):
        """Build the tree structure with categories."""
        # Define categories and their fields
        categories = {
            "System": [
                ("schema", "string"),
                ("version", "string"),
                ("update_hz", "hertz"),
                ("timestamp", "milliseconds"),
                ("data_valid", "boolean"),
                ("update_counter", "counter"),
            ],
            "Position": [
                ("latitude", "degrees"),
                ("longitude", "degrees"),
                ("altitude", "feet"),
                ("height", "feet"),
            ],
            "Orientation": [
                ("pitch", "radians"),
                ("bank", "radians"),
                ("true_heading", "radians"),
                ("magnetic_heading", "radians"),
            ],
            "Speed": [
                ("indicated_airspeed", "m/s"),
                ("ground_speed", "m/s"),
                ("vertical_speed", "m/s"),
                ("mach_number", "mach"),
                ("angle_of_attack", "radians"),
            ],
            "Aircraft State": [
                ("on_ground", "boolean"),
                ("gear", "position"),
                ("flaps", "position"),
                ("throttle", "position"),
                ("parking_brake", "boolean"),
            ],
            "Engines": [
                ("engine_running_1", "boolean"),
                ("engine_running_2", "boolean"),
                ("engine_throttle_1", "position"),
                ("engine_throttle_2", "position"),
            ],
            "Navigation": [
                ("nav1_frequency", "MHz"),
                ("nav2_frequency", "MHz"),
                ("com1_frequency", "MHz"),
                ("com2_frequency", "MHz"),
                ("selected_course_1", "radians"),
                ("selected_course_2", "radians"),
            ],
            "Autopilot": [
                ("autopilot_master", "boolean"),
                ("autopilot_heading", "radians"),
                ("autopilot_altitude", "feet"),
            ],
            "V-Speeds": [
                ("vs0", "m/s"),
                ("vs1", "m/s"),
                ("vfe", "m/s"),
                ("vno", "m/s"),
                ("vne", "m/s"),
            ],
            "Vectors": [
                ("position", "vector3d"),
                ("velocity", "vector3d"),
            ],
            "Aircraft Info": [
                ("aircraft_name", "string"),
                ("nearest_airport_id", "string"),
                ("nearest_airport_name", "string"),
            ],
        }

        for category, fields in categories.items():
            node = self.tree.insert("", "end", text=category, values=("", "", "", "", ""))
            self.group_nodes[category] = node

            for field_name, unit in fields:
                item_id = self.tree.insert(node, "end", text="", values=(field_name, "", unit, "", ""))
                self.tree_items[item_id] = (field_name, unit)

            self.tree.item(node, open=True)

    def on_connect(self):
        """Connect to the TCP server."""
        try:
            self.receiver.start_receiving()
            self.status_var.set("Status: Connecting...")
        except Exception as e:
            messagebox.showerror("Error", f"Could not start receiver:\n{e}")

    def on_disconnect(self):
        """Disconnect from the TCP server."""
        self.receiver.stop()
        self.status_var.set("Status: Disconnected")

    def refresh_tick(self):
        """Periodic UI refresh."""
        try:
            if self.receiver.connected and self.receiver.latest_data:
                self.status_var.set("Status: Connected")
                data = self.receiver.latest_data

                # Update info
                update_hz = data.get("update_hz", 0)
                update_count = data.get("update_counter", 0)
                self.info_var.set(f"Updates: {update_count} | Rate: {update_hz:.1f} Hz")

                # Update tree
                for item_id, (field_name, unit) in self.tree_items.items():
                    value = data.get(field_name)

                    if value is None:
                        raw_str = "N/A"
                        intl_str = "N/A"
                        us_str = "N/A"
                    else:
                        raw_str = self._format_raw(value, unit)
                        intl_str = self._format_converted(value, unit, "intl")
                        us_str = self._format_converted(value, unit, "us")

                    if self.tree.set(item_id, "value") != raw_str:
                        self.tree.set(item_id, "value", raw_str)
                    if self.tree.set(item_id, "intl") != intl_str:
                        self.tree.set(item_id, "intl", intl_str)
                    if self.tree.set(item_id, "us") != us_str:
                        self.tree.set(item_id, "us", us_str)
            elif self.receiver.running:
                self.status_var.set("Status: Connecting...")
            else:
                self.status_var.set("Status: Disconnected")

        except Exception as e:
            print(f"Refresh error: {e}")
        finally:
            self.after(REFRESH_MS, self.refresh_tick)

    def _format_raw(self, value, unit: str) -> str:
        """Format raw value."""
        if isinstance(value, dict):
            return f"{{x:{value.get('x', 0):.2f}, y:{value.get('y', 0):.2f}, z:{value.get('z', 0):.2f}}}"
        elif isinstance(value, str):
            return value
        elif isinstance(value, bool) or unit == "boolean":
            return "1" if value else "0"
        elif isinstance(value, (int, float)):
            return f"{value:.6f}"
        else:
            return str(value)

    def _format_converted(self, value, unit: str, target: str) -> str:
        """Format converted value for INTL or US."""
        try:
            # Handle vectors
            if isinstance(value, dict):
                x = value.get('x', 0)
                y = value.get('y', 0)
                z = value.get('z', 0)
                return f"({x:.2f}, {y:.2f}, {z:.2f})"

            # Handle strings
            if isinstance(value, str):
                return value

            # Handle booleans
            if isinstance(value, bool) or unit == "boolean":
                return "ON" if value else "OFF"

            # Handle numeric values
            if not isinstance(value, (int, float)):
                return str(value)

            # Conversions based on unit
            if unit == "radians":
                deg = math.degrees(value) % 360
                return f"{deg:.1f}°"

            elif unit == "m/s":
                if target == "us":
                    kts = value * 1.94384
                    return f"{kts:.1f} kts"
                else:
                    kmh = value * 3.6
                    return f"{kmh:.1f} km/h"

            elif unit == "feet":
                if target == "us":
                    return f"{value:.0f} ft"
                else:
                    m = value * 0.3048
                    return f"{m:.1f} m"

            elif unit == "degrees":
                return f"{value:.6f}°"

            elif unit == "MHz":
                return f"{value:.3f} MHz"

            elif unit == "hertz":
                return f"{value:.2f} Hz"

            elif unit == "mach":
                return f"M {value:.3f}"

            elif unit == "position":
                pct = value * 100
                return f"{pct:.0f}%"

            elif unit == "milliseconds":
                dt = datetime.datetime.fromtimestamp(value / 1000.0)
                return dt.strftime("%H:%M:%S.%f")[:-3]

            elif unit == "counter":
                return str(int(value))

            else:
                return "-"

        except Exception:
            return "N/A"

    def on_export_snapshot(self):
        """Export current data to JSON or CSV."""
        try:
            if not self.receiver.latest_data:
                messagebox.showinfo("Export", "No data to export.")
                return

            default_name = f"monitor_snapshot_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}"
            path = filedialog.asksaveasfilename(
                title="Save snapshot",
                defaultextension=".json",
                initialfile=default_name,
                filetypes=[("JSON", "*.json"), ("CSV", "*.csv"), ("All", "*.*")]
            )

            if not path:
                return

            import os
            _, ext = os.path.splitext(path)
            ext = ext.lower()

            snapshot = {
                "schema": "aerofly-reader-monitor-snapshot",
                "timestamp": datetime.datetime.now().isoformat(),
                "data": self.receiver.latest_data,
            }

            if ext == ".csv":
                import csv
                with open(path, "w", newline="", encoding="utf-8") as f:
                    writer = csv.writer(f)
                    writer.writerow(["variable", "value", "unit"])
                    for item_id, (field_name, unit) in self.tree_items.items():
                        value = self.receiver.latest_data.get(field_name, "")
                        writer.writerow([field_name, value, unit])
            else:
                with open(path, "w", encoding="utf-8") as f:
                    json.dump(snapshot, f, ensure_ascii=False, indent=2)

            self.status_var.set(f"Status: Exported to {os.path.basename(path)}")

        except Exception as e:
            messagebox.showerror("Error", f"Could not export snapshot:\n{e}")


def main():
    """Main entry point."""
    print()
    print("  Aerofly FS4 Reader - Realtime Monitor")
    print("  --------------------------------------")
    print()

    app = AeroflyMonitorApp()
    app.mainloop()


if __name__ == "__main__":
    main()
