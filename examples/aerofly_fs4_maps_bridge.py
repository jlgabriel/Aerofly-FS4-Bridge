# Copyright (c) 2025 Juan Luis Gabriel
# This software is released under the MIT License.
# https://opensource.org/licenses/MIT

"""Aircraft Tracker (AeroflyBridge)

Open-source real-time aircraft tracker that visualizes Aerofly FS4 data on an
interactive map. This version uses AeroflyBridge.dll JSON TCP stream (port
12345) instead of UDP, while preserving the same GUI and functionality.

Features:
- Receives JSON telemetry via AeroflyBridge TCP interface (localhost:12345)
- Displays the aircraft's position on a customizable map interface
- Shows real-time latitude, longitude, altitude, ground speed, heading, pitch, and roll
- Allows users to switch between different map styles
- Updates the aircraft's position and orientation in real-time
- Connection status indicator and graceful shutdown
"""

from __future__ import annotations

import json
import socket
import threading
import time
from dataclasses import dataclass
from typing import Optional, Dict, Any, Tuple, List

import tkinter as tk
from tkinter import font as tkfont
from tkintermapview import TkinterMapView
from PIL import Image, ImageTk, ImageDraw


# Constants
TCP_DATA_HOST = "localhost"
TCP_DATA_PORT = 12345
WINDOW_SIZE = "1000x600"
MAP_SIZE: Tuple[int, int] = (800, 600)
CONTROL_FRAME_WIDTH = 200
INFO_DISPLAY_SIZE: Tuple[int, int] = (24, 9)
UPDATE_INTERVAL_MS = 100  # GUI refresh interval
RECEIVE_TIMEOUT_S = 5.0   # Connection considered lost beyond this


@dataclass
class GPSData:
    longitude: float
    latitude: float
    altitude_m: float
    track_deg: float
    ground_speed_mps: float


@dataclass
class AttitudeData:
    true_heading_deg: float
    pitch_deg: float
    roll_deg: float


class BridgeReceiver:
    """Receives telemetry from AeroflyBridge TCP JSON stream (port 12345)."""

    def __init__(self, host: str = TCP_DATA_HOST, port: int = TCP_DATA_PORT) -> None:
        self.host = host
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.latest_gps_data: Optional[GPSData] = None
        self.latest_attitude_data: Optional[AttitudeData] = None
        self.running: bool = False
        self.receive_thread: Optional[threading.Thread] = None
        self.last_receive_time: float = 0.0

    def start_receiving(self) -> None:
        self.running = True
        self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
        self.receive_thread.start()

    def _connect(self) -> None:
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        sock.connect((self.host, self.port))
        sock.settimeout(1.0)
        self.socket = sock

    def _receive_loop(self) -> None:
        buffer = ""
        while self.running:
            try:
                if not self.socket:
                    self._connect()

                data = self.socket.recv(16384).decode("utf-8")
                if not data:
                    # Connection closed by peer; reconnect
                    time.sleep(0.5)
                    self._connect()
                    continue

                buffer += data
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    if not line.strip():
                        continue
                    self._process_line(line)

            except (socket.timeout, TimeoutError):
                continue
            except (ConnectionResetError, ConnectionAbortedError, OSError) as e:
                # Attempt reconnect after brief delay
                print(f"BridgeReceiver reconnecting: {e}")
                try:
                    if self.socket:
                        self.socket.close()
                except Exception:
                    pass
                self.socket = None
                time.sleep(1.0)
            except Exception as e:
                print(f"BridgeReceiver error: {e}")
                time.sleep(1.0)

    def _process_line(self, line: str) -> None:
        try:
            payload = json.loads(line)
        except json.JSONDecodeError:
            return

        v = payload.get("variables", {})

        # Extract canonical variables
        lat = float(v.get("Aircraft.Latitude", 0.0))
        lon = float(v.get("Aircraft.Longitude", 0.0))
        alt_m = float(v.get("Aircraft.Altitude", 0.0))
        gs_mps = float(v.get("Aircraft.GroundSpeed", 0.0))
        heading_rad = float(v.get("Aircraft.TrueHeading", 0.0))
        pitch_rad = float(v.get("Aircraft.Pitch", 0.0))
        bank_rad = float(v.get("Aircraft.Bank", 0.0))

        RAD_TO_DEG = 180.0 / 3.141592653589793
        heading_deg = (heading_rad * RAD_TO_DEG) % 360.0
        pitch_deg = pitch_rad * RAD_TO_DEG
        roll_deg = bank_rad * RAD_TO_DEG

        self.latest_gps_data = GPSData(
            longitude=lon,
            latitude=lat,
            altitude_m=alt_m,
            track_deg=heading_deg,
            ground_speed_mps=gs_mps,
        )
        self.latest_attitude_data = AttitudeData(
            true_heading_deg=heading_deg,
            pitch_deg=pitch_deg,
            roll_deg=roll_deg,
        )
        self.last_receive_time = time.time()

    def get_latest_data(self) -> Dict[str, Any]:
        return {
            "gps": self.latest_gps_data,
            "attitude": self.latest_attitude_data,
            "connected": (time.time() - self.last_receive_time) < RECEIVE_TIMEOUT_S,
        }

    def stop(self) -> None:
        self.running = False
        if self.receive_thread:
            self.receive_thread.join(timeout=1.5)
        if self.socket:
            try:
                self.socket.close()
            except Exception:
                pass


class AircraftTrackerApp:
    """Main GUI application for the Aircraft Tracker (Bridge backend)."""

    def __init__(self, master: tk.Tk) -> None:
        self.master = master
        self.master.title("Aircraft Tracker")
        self.master.geometry(WINDOW_SIZE)

        self._setup_ui()

        self.receiver = BridgeReceiver()
        self.receiver.start_receiving()

        self._setup_aircraft_marker()
        self._update_aircraft_position()

    def _setup_ui(self) -> None:
        self.main_frame = tk.Frame(self.master)
        self.main_frame.pack(fill="both", expand=True)

        # Map widget
        self.map_widget = TkinterMapView(
            self.main_frame,
            width=MAP_SIZE[0],
            height=MAP_SIZE[1],
            corner_radius=0,
        )
        self.map_widget.pack(side="left", fill="both", expand=True)

        # Control frame
        self.control_frame = tk.Frame(self.main_frame, width=CONTROL_FRAME_WIDTH)
        self.control_frame.pack(side="right", fill="y")

        self._setup_map_selection()
        self._setup_info_display()

        # Connection status
        self.connection_status = tk.Label(self.control_frame, text="Disconnected", fg="red")
        self.connection_status.pack(pady=5)

        # Close button
        self.close_button = tk.Button(self.control_frame, text="Close Map", command=self._close_application)
        self.close_button.pack(side="bottom", pady=10)

        self.master.protocol("WM_DELETE_WINDOW", self._close_application)

    def _setup_map_selection(self) -> None:
        tk.Label(self.control_frame, text="Select Map:").pack(pady=(10, 5))

        listbox_frame = tk.Frame(self.control_frame)
        listbox_frame.pack(padx=0, pady=5)

        self.map_listbox = tk.Listbox(listbox_frame, width=24, height=13)
        self.map_listbox.pack(side="left")

        for option, _ in self._get_map_options():
            self.map_listbox.insert(tk.END, option)

        self.map_listbox.bind("<<ListboxSelect>>", lambda e: self._change_map())

    def _setup_info_display(self) -> None:
        tk.Label(self.control_frame, text="Aircraft Position:").pack(pady=(10, 5))

        info_font = tkfont.Font(family="Consolas", size=9)
        self.info_display = tk.Text(
            self.control_frame,
            width=INFO_DISPLAY_SIZE[0],
            height=INFO_DISPLAY_SIZE[1],
            wrap=tk.NONE,
            font=info_font,
        )
        self.info_display.pack(padx=10, pady=10)

    def _setup_aircraft_marker(self) -> None:
        # Load custom icon if available; otherwise generate a simple arrow
        try:
            self.aircraft_image = Image.open("aircraft_icon.png").resize((32, 32))
        except Exception:
            self.aircraft_image = self._generate_default_icon(32, 32)
        self.rotated_image = ImageTk.PhotoImage(self.aircraft_image)
        self.aircraft_marker = None
        self.initial_position_set = False

    def _update_aircraft_position(self) -> None:
        data = self.receiver.get_latest_data()
        if data["connected"]:
            self.connection_status.config(text="Connected", fg="green")
            if data["gps"] and data["attitude"]:
                self._update_map_and_marker(data)
                self._update_info_display(data)
        else:
            self.connection_status.config(text="Disconnected", fg="red")
            self._clear_info_display()

        self.master.after(UPDATE_INTERVAL_MS, self._update_aircraft_position)

    def _clear_info_display(self) -> None:
        self.info_display.delete(1.0, tk.END)
        self.info_display.insert(tk.END, "Waiting for data...")

    def _update_map_and_marker(self, data: Dict[str, Any]) -> None:
        gps: GPSData = data["gps"]
        att: AttitudeData = data["attitude"]

        if not self.initial_position_set:
            self.map_widget.set_position(gps.latitude, gps.longitude)
            self.map_widget.set_zoom(10)
            self.initial_position_set = True

        self.rotated_image = self._rotate_image(att.true_heading_deg)

        if self.aircraft_marker:
            self.aircraft_marker.delete()

        self.aircraft_marker = self.map_widget.set_marker(
            gps.latitude,
            gps.longitude,
            icon=self.rotated_image,
            icon_anchor="center",
        )

        self.map_widget.set_position(gps.latitude, gps.longitude)

    def _update_info_display(self, data: Dict[str, Any]) -> None:
        gps: GPSData = data["gps"]
        att: AttitudeData = data["attitude"]

        M_TO_FT = 3.280839895
        MPS_TO_KT = 1.943844492

        alt_ft = gps.altitude_m * M_TO_FT
        gs_kts = gps.ground_speed_mps * MPS_TO_KT

        info_text = "=" * 24 + "\n"
        info_text += f"{'Latitude:':<15}{gps.latitude:>8.2f}°\n"
        info_text += f"{'Longitude:':<15}{gps.longitude:>8.2f}°\n"
        info_text += f"{'Altitude:':<15}{alt_ft:>6.0f} ft\n"
        info_text += f"{'Ground Speed:':<15}{gs_kts:>5.2f} kts\n"
        info_text += f"{'True Heading:':<15}{att.true_heading_deg:>8.2f}°\n"
        info_text += f"{'Pitch:':<15}{att.pitch_deg:>8.2f}°\n"
        info_text += f"{'Roll:':<15}{att.roll_deg:>8.2f}°\n"
        info_text += "=" * 24 + "\n"

        self.info_display.delete(1.0, tk.END)
        self.info_display.insert(tk.END, info_text)

    def _rotate_image(self, angle_deg: float) -> ImageTk.PhotoImage:
        return ImageTk.PhotoImage(self.aircraft_image.rotate(-angle_deg))

    @staticmethod
    def _generate_default_icon(width: int, height: int) -> Image.Image:
        # Create a transparent image with a simple upward-pointing arrow
        img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        # Triangle arrow
        w, h = width, height
        triangle = [(w * 0.5, h * 0.1), (w * 0.1, h * 0.9), (w * 0.9, h * 0.9)]
        draw.polygon(triangle, fill=(255, 255, 255, 255))
        # Center dot
        r = max(1, int(min(w, h) * 0.06))
        cx, cy = int(w * 0.5), int(h * 0.6)
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(0, 0, 0, 255))
        return img

    def _change_map(self) -> None:
        selected = self.map_listbox.curselection()
        if selected:
            _, tile_server = self._get_map_options()[selected[0]]
            self.map_widget.set_tile_server(tile_server)

    @staticmethod
    def _get_map_options() -> List[Tuple[str, str]]:
        return [
            ("OpenStreetMap", "https://a.tile.openstreetmap.org/{z}/{x}/{y}.png"),
            ("OpenStreetMap DE", "https://tile.openstreetmap.de/tiles/osmde/{z}/{x}/{y}.png"),
            ("OpenStreetMap FR", "https://a.tile.openstreetmap.fr/osmfr/{z}/{x}/{y}.png"),
            ("OpenTopoMap", "https://a.tile.opentopomap.org/{z}/{x}/{y}.png"),
            ("Google Normal", "https://mt0.google.com/vt/lyrs=m&hl=en&x={x}&y={y}&z={z}"),
            ("Google Satellite", "https://mt0.google.com/vt/lyrs=s&hl=en&x={x}&y={y}&z={z}"),
            ("Google Terrain", "https://mt0.google.com/vt/lyrs=p&hl=en&x={x}&y={y}&z={z}"),
            ("Google Hybrid", "https://mt0.google.com/vt/lyrs=y&hl=en&x={x}&y={y}&z={z}"),
            ("Carto Dark Matter", "https://cartodb-basemaps-a.global.ssl.fastly.net/dark_all/{z}/{x}/{y}.png"),
            ("Carto Positron", "https://cartodb-basemaps-a.global.ssl.fastly.net/light_all/{z}/{x}/{y}.png"),
            ("ESRI World Imagery", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"),
            ("ESRI World Street Map", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}"),
            ("ESRI World Topo Map", "https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}"),
        ]

    def _close_application(self) -> None:
        print("Closing Aircraft Tracker (Bridge)...")
        self.receiver.stop()
        self.master.destroy()


def main() -> None:
    print("Starting Aircraft Tracker (AeroflyBridge TCP)...")
    print(f"Connecting to AeroflyBridge at {TCP_DATA_HOST}:{TCP_DATA_PORT} ...")
    root = tk.Tk()
    app = AircraftTrackerApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()


