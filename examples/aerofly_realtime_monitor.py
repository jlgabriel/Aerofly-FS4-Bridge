"""Aerofly FS4 Realtime Monitor (Tk)

This tool displays live Aerofly Bridge variables by reading a shared-memory
region created by the Aerofly Bridge DLL. A JSON offsets file describes the
memory layout (base, stride, per-variable offsets and data types).

UI shows:
- Grouped variable tree
- Raw values exactly as provided by the DLL (no transformation)
- Convenience conversions for INTL/US where applicable
- Snapshot export to JSON/CSV
"""
import json
import os
import struct
import sys
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import mmap
import traceback
import math
import datetime

MMAP_TAGNAME = "AeroflyBridgeData"
REFRESH_MS = 200

class SharedMemoryReader:
    """Read-only helper to access the named shared-memory block published by the DLL."""
    def __init__(self):
        self.mm = None
        self.length = 0

    def open(self, tagname: str, length: int):
        """Open a read-only mapping to the named shared memory of a given length."""
        self.close()
        # On Windows: length must be <= actual mapping size
        self.mm = mmap.mmap(-1, length, tagname=tagname, access=mmap.ACCESS_READ)
        self.length = length

    def close(self):
        """Close the current mapping if present."""
        if self.mm is not None:
            try:
                self.mm.close()
            except Exception:
                pass
        self.mm = None
        self.length = 0

    def read_double(self, offset: int):
        """Read a little-endian double at byte offset, or None on error/out-of-range."""
        if self.mm is None or offset < 0 or offset + 8 > self.length:
            return None
        try:
            # Little-endian double
            return struct.unpack_from("<d", self.mm, offset)[0]
        except Exception:
            return None
        
    def read_string(self, offset: int, length: int):
        """Read a zero-terminated byte slice as text (utf-8 preferred, latin-1 fallback)."""
        if self.mm is None or offset < 0 or offset + length > self.length:
            return None
        try:
            raw = self.mm[offset:offset+length]
            nul = raw.find(b'\x00')
            if nul != -1:
                raw = raw[:nul]
            # Try utf-8, fallback to latin-1
            try:
                return raw.decode('utf-8', errors='replace')
            except Exception:
                return raw.decode('latin-1', errors='replace')
        except Exception:
            return None


class OffsetsMeta:
    """Holds the parsed offsets metadata and provides helpers for grouping and sizing."""
    def __init__(self, meta: dict):
        self.layout_version = int(meta.get("layout_version", 1))
        self.base = int(meta["array_base_offset"])
        self.stride = int(meta["stride_bytes"])
        self.count = int(meta["count"])
        self.variables = list(meta["variables"])  # list of dicts

    @staticmethod
    def from_json_file(path: str):
        """Load offsets metadata from a JSON file path."""
        with open(path, "r", encoding="utf-8") as f:
            meta = json.load(f)
        return OffsetsMeta(meta)

    def required_length(self):
        """Compute minimum mapping length needed to safely read all declared variables."""
        # Reach the end of all_variables and also cover string fields at the end of the struct
        end_all_vars = self.base + self.count * self.stride
        end_strings = 0
        for v in self.variables:
            dt = v.get("data_type", "double")
            off = int(v.get("byte_offset", 0))
            if dt == "string":
                length = int(v.get("byte_length", 0)) or 0
            else:
                length = 8  # double
            end_strings = max(end_strings, off + length)
        return max(end_all_vars, end_strings)

    def groups(self):
        """Return variables grouped by their 'group' field, ordered by logical_index."""
        # Return dict group -> list of var dicts (ordered by logical_index)
        g = {}
        for v in self.variables:
            g.setdefault(v.get("group", "other"), []).append(v)
        for k in g:
            g[k].sort(key=lambda x: int(x.get("logical_index", 0)))
        return dict(sorted(g.items(), key=lambda x: x[0]))


class AeroflyTkApp(tk.Tk):
    """Tkinter desktop app that visualizes Aerofly Bridge variables in real time."""
    def __init__(self, json_path: str = None):
        super().__init__()
        self.title("Aerofly FS4 Realtime Monitor - Tk")
        self.geometry("1100x700")
        self.reader = SharedMemoryReader()
        self.meta = None
        self.json_path = json_path
        self.tree_items = {}  # item_id -> var dict
        self.group_nodes = {}  # group -> item_id

        # Top bar
        top = ttk.Frame(self)
        top.pack(side=tk.TOP, fill=tk.X, padx=8, pady=6)

        self.json_label_var = tk.StringVar(value="JSON: (not loaded)")
        ttk.Label(top, textvariable=self.json_label_var).pack(side=tk.LEFT)

        ttk.Button(top, text="Open JSON...", command=self.on_open_json).pack(side=tk.LEFT, padx=6)
        ttk.Button(top, text="Connect", command=self.on_connect).pack(side=tk.LEFT, padx=6)
        ttk.Button(top, text="Disconnect", command=self.on_disconnect).pack(side=tk.LEFT, padx=6)
        ttk.Button(top, text="Export...", command=self.on_export_snapshot).pack(side=tk.LEFT, padx=6)

        self.status_var = tk.StringVar(value="Status: Idle")
        ttk.Label(top, textvariable=self.status_var).pack(side=tk.RIGHT)

        # Tree (value is raw + original units; add derived INTL/US columns)
        cols = ("name", "offset", "value", "units", "intl", "us")
        self.tree = ttk.Treeview(self, columns=cols, show="tree headings")
        self.tree.heading("#0", text="Group")
        self.tree.heading("name", text="Name")
        self.tree.heading("offset", text="Offset")
        self.tree.heading("value", text="Value")
        self.tree.heading("units", text="Units")
        self.tree.heading("intl", text="INTL")
        self.tree.heading("us", text="US")

        self.tree.column("#0", width=180, stretch=False)
        self.tree.column("name", width=380, stretch=True)
        self.tree.column("offset", width=100, stretch=False)
        self.tree.column("value", width=180, stretch=False)
        self.tree.column("units", width=90, stretch=False)
        self.tree.column("intl", width=160, stretch=False)
        self.tree.column("us", width=160, stretch=False)

        self.tree.pack(expand=True, fill=tk.BOTH, padx=8, pady=8)

        # Footer
        foot = ttk.Frame(self)
        foot.pack(side=tk.BOTTOM, fill=tk.X, padx=8, pady=6)
        self.layout_var = tk.StringVar(value="layout_version: ?")
        ttk.Label(foot, textvariable=self.layout_var).pack(side=tk.LEFT)

        # Auto-load if provided
        if json_path:
            try:
                self.load_json(json_path)
            except Exception as e:
                messagebox.showerror("Error", f"Could not load JSON:\n{e}")

        self.after(REFRESH_MS, self.refresh_tick)

    def _format_time_hms(self, seconds: float) -> str:
        """Format seconds as HH:MM:SSZ."""
        try:
            secs = int(seconds) % 86400
            hh = secs // 3600
            mm = (secs % 3600) // 60
            ss = secs % 60
            return f"{hh:02d}:{mm:02d}:{ss:02d}Z"
        except Exception:
            return "--:--:--Z"

    def _rad_to_deg(self, radians: float) -> float:
        return radians * 180.0 / math.pi

    def _normalize_heading(self, deg: float) -> float:
        """Normalize degrees into [0, 360)."""
        d = deg % 360.0
        if d < 0:
            d += 360.0
        return d

    def _east_ccw_to_north_cw(self, deg_east_ccw: float) -> float:
        """Convert math-style degrees (0=East, CCW positive) to aviation heading (0=North, CW)."""
        return self._normalize_heading(90.0 - deg_east_ccw)

    def _get_unit_from_meta(self, v: dict) -> str:
        """Pick the most specific unit-like field present in the metadata."""
        unit = (
            v.get("units")
            or v.get("unit")
            or v.get("raw_unit")
            or v.get("tm_unit")
        )
        return unit or ""

    def _is_boolean_by_meta(self, name: str, unit: str) -> bool:
        """Heuristic to detect boolean variables from metadata and naming.

        Prefer explicit unit == 'Boolean'. Fallback to name keywords that
        conventionally indicate boolean states in Aerofly variables.
        """
        try:
            if (unit or "").strip().lower() == "boolean":
                return True
            lname = (name or "").strip().lower()
            boolean_tokens = (
                "engaged", "disengage", "master", "swap", "running", "paused",
                "warning", "caution", "reverse", "ignition", "starter",
                "fire", "overspeed", "overheat", "available", "armed",
                "onground", "onrunway", "sound", "usemousecontrol",
                "playbackstart", "playbackstop"
            )
            return any(tok in lname for tok in boolean_tokens)
        except Exception:
            return False

    def _format_value_raw(self, v: dict, val: float) -> str:
        """Show raw numeric value from DLL without conversions and without unit text."""
        try:
            name = v.get("name", "")
            unit = self._get_unit_from_meta(v)
            if self._is_boolean_by_meta(name, unit):
                return "1" if (val or 0.0) >= 0.5 else "0"
            return f"{val:.6f}"
        except Exception:
            return f"{val:.6f}"

    def _deg(self, rad: float) -> float:
        return rad * 180.0 / math.pi

    def _format_converted_scalar(self, name: str, unit: str, value: float, target: str) -> str:
        """Human-friendly conversions for selected units.

        target is 'intl' or 'us'. Raw values are preserved elsewhere; this is
        purely for display convenience and does not alter the underlying data.
        Special-case: headings/courses from math coordinates (0=East, CCW+) to
        aviation convention (0=North, CW).
        """
        if value is None:
            return "N/A"
        try:
            lname = name.lower()

            # Booleans → ON/OFF for both INTL and US
            if self._is_boolean_by_meta(name, unit):
                return "ON" if value >= 0.5 else "OFF"

            # Normalized controls (0..1) → percentage for both INTL and US
            if unit == "none" and 0.0 <= value <= 1.0:
                if any(key in lname for key in (
                    "throttle", "flaps", "slats", "airbrake", "spoiler", "mixture",
                    "collective", "cyclic", "tiller", "trim"
                )) and "engaged" not in lname and "cursor" not in lname:
                    return f"{value*100:.1f} %"

            # Angles in rad → degrees
            if unit == "radian":
                # Special-case headings/courses: Aerofly uses 0=East CCW+, aviation uses 0=North CW
                if ("Heading" in name) or ("Course" in name):
                    deg_math = self._deg(value)
                    hdg = self._east_ccw_to_north_cw(deg_math)
                    return f"{hdg:.1f} °"
                deg = self._deg(value)
                return f"{deg:.2f} °"
            if unit == "radian_per_second":
                deg_s = self._deg(value)
                return f"{deg_s:.3f} °/s"
            if unit == "radian_per_second_squared":
                deg_s2 = self._deg(value)
                return f"{deg_s2:.3f} °/s²"

            # Time
            if unit == "second":
                return f"{value:.2f} s"

            # Frequency → MHz
            if unit == "hertz":
                mhz = value / 1_000_000.0
                return f"{mhz:.3f} MHz"

            # Speed
            if unit == "meter_per_second":
                if "VerticalSpeed" in name:
                    if target == "us":
                        fpm = value * 196.850394
                        return f"{fpm:.0f} fpm"
                    else:
                        return f"{value:.3f} m/s"
                else:
                    if target == "us":
                        kt = value * 1.943844492
                        return f"{kt:.1f} kt"
                    else:
                        kmh = value * 3.6
                        return f"{kmh:.1f} km/h"

            # Length
            if unit == "meter":
                if target == "us":
                    ft = value * 3.280839895
                    return f"{ft:.1f} ft"
                else:
                    return f"{value:.2f} m"

            # Acceleration
            if unit == "meter_per_second_squared":
                if target == "us":
                    ft_s2 = value * 3.280839895
                    return f"{ft_s2:.3f} ft/s²"
                else:
                    return f"{value:.3f} m/s²"

            # Per second
            if unit == "per_second":
                return f"{value:.3f} 1/s"

            # None or unknown
            return "-"
        except Exception:
            return "N/A"

    def _format_converted(self, v: dict, values, target: str) -> str:
        """Vector-aware wrapper around _format_converted_scalar."""
        name = v.get("name", "")
        unit = self._get_unit_from_meta(v)
        # values can be float or list of floats
        if isinstance(values, (list, tuple)):
            parts = [self._format_converted_scalar(name, unit, x, target) for x in values]
            # If all parts are numeric with same suffix, they'd differ in suffix; keep as list
            return "[" + ", ".join(parts) + "]"
        else:
            return self._format_converted_scalar(name, unit, values, target)

    def on_open_json(self):
        """Prompt user to select the offsets JSON and load it into the app."""
        p = filedialog.askopenfilename(
            title="Select AeroflyBridge_offsets.json",
            filetypes=[("JSON", "*.json"), ("All", "*.*")]
        )
        if not p:
            return
        try:
            self.load_json(p)
        except Exception as e:
            traceback.print_exc()
            messagebox.showerror("Error", f"Could not load JSON:\n{e}")

    def load_json(self, path: str):
        """Parse offsets JSON and rebuild the tree grouped by variable group."""
        meta = OffsetsMeta.from_json_file(path)
        self.meta = meta
        self.json_path = path
        self.json_label_var.set(f"JSON: {os.path.basename(path)}")
        self.layout_var.set(f"layout_version: {meta.layout_version}, count: {meta.count}, stride: {meta.stride}, base: {meta.base}")

        # Rebuild tree
        for child in self.tree.get_children(""):
            self.tree.delete(child)
        self.group_nodes.clear()
        self.tree_items.clear()

        for group, vars_list in meta.groups().items():
            node = self.tree.insert("", "end", text=group, values=("", "", "", "", "", ""))
            self.group_nodes[group] = node
            for v in vars_list:
                name = v.get("name", "")
                off = int(v.get("byte_offset", 0))
                unit_text = self._get_unit_from_meta(v)
                item_id = self.tree.insert(node, "end", text="", values=(name, off, "", unit_text, "", ""))
                self.tree_items[item_id] = v

        self.tree.expand_all = getattr(self.tree, "expand_all", None)
        # Expand all groups
        for node in self.group_nodes.values():
            self.tree.item(node, open=True)

    def on_connect(self):
        """Open the shared-memory mapping sized according to the offsets metadata."""
        if not self.meta:
            messagebox.showwarning("Warning", "Load the offsets JSON first.")
            return
        try:
            required_len = self.meta.required_length()
            self.reader.open(MMAP_TAGNAME, required_len)
            self.status_var.set(f"Status: Connected (len={required_len})")
        except Exception as e:
            traceback.print_exc()
            self.status_var.set("Status: Connection error")
            messagebox.showerror("Error", f"Could not open the shared memory '{MMAP_TAGNAME}':\n{e}")

    def on_disconnect(self):
        """Close the shared-memory mapping and update status."""
        self.reader.close()
        self.status_var.set("Status: Disconnected")

    def refresh_tick(self):
        """Periodic UI refresh: read values from shared memory and update tree cells.

        Supports scalar doubles, strings, and vector types (2D/3D/4D). Some
        variables may be flagged as 'message_only' storage and thus are not
        present in shared memory; these are shown as N/A.
        """
        try:
            if self.reader.mm and self.meta:
                for item_id, v in self.tree_items.items():
                    off = int(v.get("byte_offset", 0))
                    dt = v.get("data_type", "double")
                    storage = v.get("storage", "")
                    if dt == "string":
                        length = int(v.get("byte_length", 0)) or 0
                        s = self.reader.read_string(off, length) or ""
                        intl_s = s
                        us_s = s
                    elif dt == "vector2d":
                        if storage == "message_only":
                            s = "N/A"; intl_s = "N/A"; us_s = "N/A"
                        else:
                            try:
                                x, y = struct.unpack_from("<2d", self.reader.mm, off)
                                s = f"[{x:.6f}, {y:.6f}]"
                                intl_s = self._format_converted(v, [x, y], "intl")
                                us_s = self._format_converted(v, [x, y], "us")
                            except Exception:
                                s = "N/A"; intl_s = "N/A"; us_s = "N/A"
                    elif dt == "vector3d":
                        if storage == "message_only":
                            s = "N/A"; intl_s = "N/A"; us_s = "N/A"
                        else:
                            try:
                                x, y, z = struct.unpack_from("<3d", self.reader.mm, off)
                                s = f"[{x:.6f}, {y:.6f}, {z:.6f}]"
                                intl_s = self._format_converted(v, [x, y, z], "intl")
                                us_s = self._format_converted(v, [x, y, z], "us")
                            except Exception:
                                s = "N/A"; intl_s = "N/A"; us_s = "N/A"
                    elif dt == "vector4d" or dt == "vector4dquaternion":
                        if storage == "message_only":
                            s = "N/A"; intl_s = "N/A"; us_s = "N/A"
                        else:
                            try:
                                x, y, z, w = struct.unpack_from("<4d", self.reader.mm, off)
                                s = f"[{x:.6f}, {y:.6f}, {z:.6f}, {w:.6f}]"
                                intl_s = self._format_converted(v, [x, y, z, w], "intl")
                                us_s = self._format_converted(v, [x, y, z, w], "us")
                            except Exception:
                                s = "N/A"; intl_s = "N/A"; us_s = "N/A"
                    else:
                        val = self.reader.read_double(off)
                        if val is None:
                            s = "N/A"; intl_s = "N/A"; us_s = "N/A"
                        else:
                            s = self._format_value_raw(v, val)
                            intl_s = self._format_converted(v, val, "intl")
                            us_s = self._format_converted(v, val, "us")
                    current = self.tree.set(item_id, "value")
                    if current != s:
                        self.tree.set(item_id, "value", s)
                    if self.tree.set(item_id, "intl") != intl_s:
                        self.tree.set(item_id, "intl", intl_s)
                    if self.tree.set(item_id, "us") != us_s:
                        self.tree.set(item_id, "us", us_s)
        except Exception:
            # Do not interrupt the UI due to read errors
            pass
        finally:
            self.after(REFRESH_MS, self.refresh_tick)

    def _build_snapshot(self) -> dict:
        """Collect current tree rows into a serializable snapshot structure."""
        now_iso = datetime.datetime.now().isoformat()
        rows = []
        for item_id, v in self.tree_items.items():
            parent_id = self.tree.parent(item_id)
            group = self.tree.item(parent_id, "text") if parent_id else ""
            name = self.tree.set(item_id, "name")
            offset = self.tree.set(item_id, "offset")
            value = self.tree.set(item_id, "value")
            units = self.tree.set(item_id, "units")
            rows.append({
                "group": group,
                "name": name,
                "offset": int(offset) if str(offset).isdigit() else offset,
                "value": value,
                "units": units,
            })
        snapshot = {
            "schema": "aerofly-bridge-monitor-snapshot",
            "schema_version": 1,
            "timestamp": now_iso,
            "source_offsets_json": os.path.basename(self.json_path) if self.json_path else None,
            "layout_version": self.meta.layout_version if self.meta else None,
            "variables": rows,
        }
        return snapshot

    def on_export_snapshot(self):
        """Export current view to JSON or CSV; filenames include a timestamp."""
        try:
            if not self.tree_items:
                messagebox.showinfo("Export", "There is no data to export.")
                return
            default_name = f"monitor_snapshot_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}"
            p = filedialog.asksaveasfilename(
                title="Save snapshot",
                defaultextension=".json",
                initialfile=default_name,
                filetypes=[("JSON", "*.json"), ("CSV", "*.csv"), ("All", "*.*")]
            )
            if not p:
                return
            snap = self._build_snapshot()
            _, ext = os.path.splitext(p)
            ext = ext.lower()
            if ext == ".csv":
                import csv
                with open(p, "w", newline="", encoding="utf-8") as f:
                    writer = csv.writer(f)
                    writer.writerow(["group", "name", "offset", "value", "units"]) 
                    for row in snap["variables"]:
                        writer.writerow([row["group"], row["name"], row["offset"], row["value"], row["units"]])
            else:
                with open(p, "w", encoding="utf-8") as f:
                    json.dump(snap, f, ensure_ascii=False, indent=2)
            self.status_var.set(f"Status: Snapshot exported to {os.path.basename(p)}")
        except Exception as e:
            traceback.print_exc()
            messagebox.showerror("Error", f"Could not export snapshot:\n{e}")


def main():
    json_path = None
    if len(sys.argv) >= 2:
        json_path = sys.argv[1]
    app = AeroflyTkApp(json_path)
    app.mainloop()

if __name__ == "__main__":
    main()